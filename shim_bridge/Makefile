# Makefile for linux-genz Fabric Emulation Environment and bridge

VERBOSE=0

CONFIG_GENZ_FEE ?= m

KERNELBASE = /lib/modules/$(shell uname -r)
KERNELDIR ?= $(KERNELBASE)/build
SHELL=/bin/bash

# struct wait_queue_head, among others, didn't show up until 4.13, but
# was backported into SLES15 (kernel 4.12).  It's a gross test but
# better than nothing.
VMIN:=4
PMIN:=13
MAYBE:=$(shell grep 'struct wait_queue_head' ${KERNELDIR}/include/linux/wait.h)
V:=$(shell make --no-print-directory -C ${KERNELDIR} kernelversion | cut -d. -f1)
P:=$(shell make --no-print-directory -C ${KERNELDIR} kernelversion | cut -d. -f2)
VFAIL:=Kernel headers are $V.$P, need \>= ${VMIN}.${PMIN}
VFAILNOBACK:=Kernel headers are $V.$P, no backport from \>= ${VMIN}.${PMIN}

obj-$(CONFIG_GENZ_FEE) += genz_fee.o fee_bridge.o

# fee_pci.c has the MODULE declarations

genz_fee-objs := fee_pci.o fee_adapter.o fee_IVSHMSG.o \
	fee_register.o fee_MSI-X.o fee_link.o

fee_bridge-objs := gf_bridge.o

ccflags-y:=-I$(PWD)/../subsystem

RUNNING_ARCH := $(shell dpkg-architecture -qDEB_BUILD_ARCH_CPU 2>/dev/null)

all:	modules

modules:	versioncheck
ifeq "$(RUNNING_ARCH)" "amd64"
	make V=$(VERBOSE) $(CFLAGS) -C $(KERNELDIR) M=$(PWD) ARCH=x86 modules
else
	make V=$(VERBOSE) $(CFLAGS) -C $(KERNELDIR) M=$(PWD) modules
endif

modules_install:	modules
	INSTALL_MOD_DIR=genz/FEE sudo -E make V=$(VERBOSE) -C $(KERNELDIR) M=$(PWD) modules_install
	sudo -E depmod -a

clean:
ifeq "$(architecture)" "amd64"
	make -C $(KERNELDIR) M=$(PWD) ARCH=x86 clean
else
	make -C $(KERNELDIR) M=$(PWD) clean
endif

# Kernel 3 is bad, 5 is good, 4 needs a closer look.
versioncheck:
	@[ $V -lt ${VMIN} ] && echo ${VFAIL} && exit 1; \
	 [ $V -gt ${VMIN} ] && exit 0; \
	 [ $P -ge ${PMIN} ] && exit 0; \
	 [ "${MAYBE}" ] && exit 0; \
	 echo ${VFAILNOBACK}; exit 1

