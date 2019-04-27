Program Snippets for kernel communiction via netlink(7)

- KUNS: Kernel Udev Netlink Socket

insmod and rmmod of "fee_bridge" will show you lots of things as you run these programs.

kuns.sh merely wraps "udevadm monitor".

kuns.c is a simple blocking read on the NETLINK_KEVENT_UDEV family "bus".

kuns.py is a fancier way leveraging the pyroute2 encasulation of netlink.

