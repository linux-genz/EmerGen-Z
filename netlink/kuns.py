#!/usr/bin/python3

# Co-opt the pyroute2 NetlinkSocket (which defaults to GenericNetlinkSocket) to
# listen on the Netlink KOBJECT_UEVENT bus for module and device activity.
# modprobe/rmmod of the fee_bridge module shows lots of stuff.

# https://docs.pyroute2.org/ for info, but the source is best, especially
# /usr/lib/python3/dist-packages/pyroute2/netlink/nlsocket.py

import os
import socket
import sys
import time

from pdb import set_trace
from pprint import pprint
from types import SimpleNamespace

from pyroute2.netlink import NETLINK_KOBJECT_UEVENT
from pyroute2.netlink.nlsocket import NetlinkSocket     # RTFF


class KobjectUeventNetlinkSocket(NetlinkSocket):

    def __init__(self, *args, **kwargs):
        # NetlinkSocket has no __init__ so end up in nlsocket.py::NetlinkMixin.__init__
        # which is expecting family, port, pid, and fileno.  Its "post_init()" creates
        # self._sock (visible here).  It also provides accessor methods so I should
        # never hit self._sock directly.
        kwargs['family'] = NETLINK_KOBJECT_UEVENT       # Override
        async = 'async'
        if async in kwargs:
            asyncval = bool(kwargs[async])
            del kwargs[async]
        else:
            asyncval = False
        super().__init__(*args, **kwargs)
        self.bind(groups=-1, async=asyncval)      # zero was a bad choice

    def _cooked(self, raw):
        elems = [ r for r in raw.split(b'\x00') ]
        elem0 = elems.pop(0).decode()
        if elem0 == 'libudev':
            retobj = SimpleNamespace(src='libudev')
            grunge = bytes()    # FIXME: grunge kernel src, decode this mess
            while not elems[0].startswith(b'ACTION='):
                grunge += elems.pop(0)
            retobj.header = grunge
        else:
            retobj = SimpleNamespace(src='kernel', header=elem0)
        # Should just be key=value left
        for e in elems:
            if e:
                key, value = e.decode().split('=')
                setattr(retobj, key.strip(), value.strip())
        return retobj

    def __call__(self):
        '''Returns None or an object based on blocking/timeout.'''
        try:
            raw = self.recv(4096)
        except BlockingIOError as err:  # EWOULDBLOCK
            return None
        return self._cooked(raw)

    @property
    def blocking(self):
        '''Per docs, True -> timeout == None, False -> timeount == 0.0'''
        to = self.gettimeout()
        return True if to is None else bool(to)

    @blocking.setter
    def blocking(self, newstate):
        '''Per docs, True -> timeout == None, False -> timeount == 0.0'''
        self.setblocking(bool(newstate))

    @property
    def timeout(self):
        return self.gettimeout()

    @timeout.setter
    def timeout(self, newto):
        '''newto must be None or float(>= 0.0)'''
        assert newto is None or float(newto) >= 0.0, 'bad timeout value'
        self.settimeout(newto)

    @property
    def timeout(self):
        return self.gettimeout()

    @property
    def rdlen(self):
        return self.buffer_queue.qsize()

    def dequeue(self, count=-1):
        retlist = []
        if count <= 0:
            while True:
                try:
                    retlist.append(
                        self._cooked(self.buffer_queue.get(block=False)))
                except Empty as err:
                    return retlist

        while count > 0:
            retlist.append(self._cooked(self.buffer_queue.get(block=True)))
            count -= 1
        return retlist

# Callback is hit only during kuns.get() which can hang.  The message it gets
# is crap; maybe the logic is more suited to (generic) netlink than the
# multiple udev notifiers?

if __name__ == '__main__':
    async = len(sys.argv) > 1
    kuns = KobjectUeventNetlinkSocket(async=async)
    if async:
        print('\nAsync reads...', end='')
        while True:
            while not kuns.rdlen:
                print('.', end='')
                sys.stdout.flush()
                time.sleep(1)
            print('\n', kuns.dequeue(count=1))

    print('\nBlocking reads...')
    kuns.blocking = True        # It's the default value; play with others.
    while True:
        val = kuns()
        if val is None:
            print('Nada')
        else:
            pprint(val)
