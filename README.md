## Loading the Gen-Z prototype subsystem into F.E.E.

This assumes you already have the Fabric Emulation Environment running
and have created at least one virtual machine configured to join the
emulated fabric.  Those instructions [given in the F.E.E. repo.](https://github.com/linux-genz/F.E.E.)

If the switch process is running, and you've started the VM, you should
also see a description

    Driverless QEMU

in one of the slots of the switch interface.

### Log into VM and clone this repo

First, 'lspci -v' and insure you have the RedHat IVSHMEM pseudo device
with 16 MSI-X interrupts.

Depending on how you built your VM, you may need to download additional
packages like git, Linux headers for your kernel, build-essential, etc.

1. git clone https://github.com/linux-genz/EmerGen-Z.git
1. cd EmerGen-Z/subsystem
1. make all
1. make modules install
1. cd ../shim_driver
1. make all
1. make modules install

Now try 

    sudo modprobe genz_fee

Two modules should load and the description in the switch window should
change.  There should also be about twenty genz entries under /sys/class.

A third module is needed for the actual bridge driver:

    sudo modprobe genz_fee_bridge

Insure there is a device file /dev/genz_fee_bridgeXX.

Messages are sent with

echo "CID,SID:the message" > /dev/genz_fee_bridgeXX

If the CID,SID is not assigned (to be documented SOON) then you can use
a single digit to target the emulated fabric index.

"cat < /dev/famez_bridgeXX" to read data.
