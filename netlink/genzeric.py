#!/usr/bin/python3

# Get on the Generic bus.  Uses the llamas test module.

# https://docs.pyroute2.org/ for info, but the source is best, especially
# /usr/lib/python3/dist-packages/pyroute2/netlink/generic/__init__.py

import os
import socket
import sys
import time

from pdb import set_trace
from pprint import pprint
from types import SimpleNamespace

from pyroute2.common import map_namespace
from pyroute2.netlink import NETLINK_GENERIC
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

# These classes are passed to internals to build packed structs for
# passing to C routines.  They're really just linked lookup tables
# whose attributes are proscribed by internals.  JFDI.

class GENZ_genlmsg(genlmsg):
    prefix = 'GENZ_A_'

    nla_map = ((prefix + 'GCID',    'uint32'),
               (prefix + 'CCLASS',  'uint16'),
               (prefix + 'UUID',    'cdata'),   # asciiz ?
    )

    # NO __init__ override!  Internals will instantiate this and
    # have certain expectations.

    def prepare4cmd(self, cmd):
        self['cmd'] = GENZ_C_name2num[cmd]
        self['pid'] = os.getpid()
        self['version'] = GENZ_GENL_VERSION


class GENZ_Marshal(Marshal):
    msg_map = {
        GENZ_C_ADD_COMPONENT:       GENZ_genlmsg,
        GENZ_C_REMOVE_COMPONENT:    GENZ_genlmsg,
        GENZ_C_SYMLINK_COMPONENT:   GENZ_genlmsg,
    }


class GENZ_Socket(GenericNetlinkSocket):

    def __init__(self):
        super().__init__()
        self.Marshal = GENZ_Marshal()

    def bind(self, **kwarg):
        super().bind(GENZ_GENL_FAMILY_NAME, GENZ_genlmsg,
            groups=0, pid=os.getpid(), **kwarg)


class GENZ_channel(GENZ_Socket):

    def __init__(self, *arg, **kwarg):
        super().__init__(*arg, **kwarg)

        try:
            self.bind()
        except Exception as err:
            super().close()
            raise(err)


if __name__ == '__main__':
    channel = GENZ_channel()
    msg = GENZ_genlmsg()
    msg.prepare4cmd('GENZ_C_ADD_COMPONENT')
    msg['attrs'].append([ 'GENZ_A_GCID', 4242 ])
    msg['attrs'].append([ 'GENZ_A_CCLASS', 43 ])
    # msg['attrs'].append([ 'GENZ_A_UUID', b'12345678' ])
    print('Prepared message PID', msg['pid'])
    try:
        retval = channel.nlm_request(msg,
                                     msg_type=channel.prid,
                                     msg_flags=NLM_F_REQUEST|NLM_F_ACK)
        pass    # look at retval
    except Exception as exc:
        set_trace()
        pass
