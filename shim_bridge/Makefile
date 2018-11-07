# Makefile for linux-genz Fabric Emulation Environment and bridge

VERBOSE=0

CONFIG_GENZ_FEE ?= m

KERNELBASE = /lib/modules/$(shell uname -r)
KERNELDIR ?= $(KERNELBASE)/build
SHELL=/bin/bash

# wait_queue_head(), among others, didn't show up until 4.13
VMUST:=4
PMIN:=13
V:=$(shell make --no-print-directory -C ${KERNELDIR} kernelversion | cut -d. -f1)
P:=$(shell make --no-print-directory -C ${KERNELDIR} kernelversion | cut -d. -f2)

obj-$(CONFIG_GENZ_FEE) += genz_fee.o genz_fee_bridge.o

# fee_pci.c has the MODULE declarations

genz_fee-objs := fee_pci.o fee_adapter.o fee_IVSHMSG.o \
	fee_register.o fee_MSI-X.o fee_link.o

genz_fee_bridge-objs := gf_bridge.o

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

versioncheck:
	@[ $V -eq ${VMUST} -a $P -ge ${PMIN} ] && exit 0; \
	 echo Kernel headers are $V.$P, need \>= ${VMUST}.${PMIN}; exit 1

