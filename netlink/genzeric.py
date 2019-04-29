#!/usr/bin/python3

# Get on the Generic bus.  Uses the llamas test module.

# https://docs.pyroute2.org/ for info, but the source is best, especially
# /usr/lib/python3/dist-packages/pyroute2/netlink/generic/__init__.py

import errno
import os
import socket
import sys
import time
import uuid

from pdb import set_trace
from pprint import pprint
from types import SimpleNamespace

from pyroute2.common import map_namespace
from pyroute2.netlink import NETLINK_GENERIC, NetlinkError
from pyroute2.netlink import NLM_F_REQUEST, NLM_F_DUMP, NLM_F_ACK
from pyroute2.netlink import nla, nla_base, genlmsg
from pyroute2.netlink.generic import GenericNetlinkSocket
from pyroute2.netlink.nlsocket import Marshal

# Used in kernel module: genl_register_family(struct genl_family.name).
# After insmod, run "genl -d ctrl  list" and eventually see
# Name: genz_cmd
#       ID: 0x18  Version: 0x1  header size: 0  max attribs: 3
#       commands supported:
#               #1:  ID-0x0
#               #2:  ID-0x1
#               #3:  ID-0x2
#
# ID == 0x18 (== 24) is dynamic and only valid in this scenario, and I've
# seen the 24 in embedded structures.  Version in kernel == 1, no header,
# max 3 attribs == fields in 'MsgProps': gcid, cclass, uuid (user_send.c).

GENZ_GENL_FAMILY_NAME   = 'genz_cmd'
GENZ_GENL_VERSION       = 1

# Commands are matched from kern_recv.c::struct genl_ops genz_gnl_ops

GENZ_C_PREFIX            = 'GENZ_C_'

GENZ_C_ADD_COMPONENT     = 0    # from genz_genl.h "enum"
GENZ_C_REMOVE_COMPONENT  = 1
GENZ_C_SYMLINK_COMPONENT = 2
GENZ_C_MAX               = GENZ_C_SYMLINK_COMPONENT

# Coalesce the commands into a forward and reverse map.
# From https://www.open-mesh.org/attachments/857/neighbor_extend_dump.py

(GENZ_C_name2num, GENZ_C_num2name) = map_namespace(GENZ_C_PREFIX, globals())

# These mixins are given to pyroute2 to build packed structs for sending to
# C routines (libnl).  They're just linked lookup tables whose attributes are
# proscribed by internals.  The choices for encoding (uint32, asciiz, etc)
# are each a class in netlink/__init.__.py "class nlmsg_atoms".  If you
# don't like those choices, add a class here then add it as a new class
# attribute to nlmsg.  Only override __init__ for a set_trace, nothing else.
# See also netlink/__init__.py::register_nlas() for optional fields.


class GENZ_genlmsg(genlmsg):
    '''The set of all Netlink Attributes (NLAs) that could be passed.
       This is the analog of the kernel "struct nla_policy".'''

    prefix = 'GENZ_A_'      # A for "Attribute", a convenice for me.

    # Zero-based arrays are sort-of needed here, but also somewhat frowned
    # upon.  This needs further research, maybe in pyroute2 itself.

    nla_map = (
               ('UnUsed',           'none'),
               (prefix + 'GCID',    'uint32'),
               (prefix + 'CCLASS',  'uint16'),
               (prefix + 'UUID',    'string')   # bytearray not supported
    )


class GENZ_Marshal(Marshal):
    '''The set of all command numbers and their associated message structures.
       This is the analog of the kernel "struct genl_ops".'''

    msg_map = {
        GENZ_C_ADD_COMPONENT:       GENZ_genlmsg,
        GENZ_C_REMOVE_COMPONENT:    GENZ_genlmsg,
        GENZ_C_SYMLINK_COMPONENT:   GENZ_genlmsg,
    }


class GENZ_Netlink(GenericNetlinkSocket):

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.marshal = GENZ_Marshal()       # Now pyroute2 methods see it.

    def bind(self, **kwargs):
        '''Exposed here instead of just automatically doing it.'''
        try:
            super().bind(
                GENZ_GENL_FAMILY_NAME,
                GENZ_genlmsg,
                groups=0,
                pid=os.getpid(),
                **kwargs)
            return          # self.prid is now set, BTW

        except NetlinkError as exc:
            self.close()
            if exc.code == errno.ENOENT:
                raise RuntimeError(
                    'No kernel response for "%s"' % GENZ_GENL_FAMILY_NAME)
            raise(exc)
        except Exception as exc:
            self.close()
            raise RuntimeError('bind() failed: %s' % str(exc))

    def newmsg(self, cmd, GCID, CCLASS, UUID):
        if not isinstance(UUID, uuid.UUID):
            raise RuntimeError('UUID must be type uuid.UUID')
        msg = GENZ_genlmsg()
        msg['cmd'] = GENZ_C_name2num[cmd]
        msg['pid'] = os.getpid()
        msg['version'] = GENZ_GENL_VERSION

        # The policy map in the kernel code says always send three.
        msg['attrs'].append([ 'GENZ_A_GCID', GCID ])
        msg['attrs'].append([ 'GENZ_A_CCLASS', CCLASS ])
        msg['attrs'].append([ 'GENZ_A_UUID', UUID.bytes ])
        return msg

    def sendmsg(self, msg):
        return self.nlm_request(msg,
                                msg_type=self.prid,
                                msg_flags=NLM_F_REQUEST|NLM_F_ACK)

# See https://docs.python.org/3.5/library/uuid.html
# Remember, RFC 4122 mixes endianness in subfields so watch that ntoh!

def YodelAyHeHUUID(random=True):
    '''Return a uuid.UUID object.'''
    if random:
        return uuid.uuid4()

    # Pick your favorite constructor, and refactor this routine accordingly.

    this = uuid.UUID('12345678123456781234567812345678')
    this = uuid.UUID(int=0x12345678123456781234567812345678)
    this = uuid.UUID('urn:uuid:12345678-1234-5678-1234-567812345678')
    this = uuid.UUID(bytes=b'\x12\x34\x56\x78' * 4)
    this = uuid.UUID(bytes_le=b'\x78\x56\x34\x12\x34\x12\x78\x56' +
                              b'\x12\x34\x56\x78\x12\x34\x56\x78')
    this = uuid.UUID(
        fields=(0x12345678, 0x1234, 0x5678, 0x12, 0x34, 0x567812345678))
    this = uuid.UUID('{12345678-1234-5678-1234-567812345678}')
    this = uuid.UUID('12345678-1234-5678-1234-567812345678')
    return this


if __name__ == '__main__':
    genznl = GENZ_Netlink()
    genznl.bind()
    UUID = YodelAyHeHUUID()
    msg = genznl.newmsg('GENZ_C_ADD_COMPONENT', 4242, 43, UUID)
    print('Sending PID=%d UUID=%s' % (msg['pid'], str(UUID)))
    try:
        retval = genznl.sendmsg(msg)
        pprint(retval)
    except Exception as exc:
        raise SystemExit(str(exc))

    raise SystemExit(0)
