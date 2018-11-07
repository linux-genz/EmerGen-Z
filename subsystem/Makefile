# Makefile for genz

VERBOSE=0

CONFIG_GENZ ?= m

KERNELBASE = /lib/modules/$(shell uname -r)
KERNELDIR ?= $(KERNELBASE)/build
SHELL=/bin/bash
PWD:=$(shell /bin/pwd)

# wait_queue_head(), among others, didn't show up until 4.13
VMUST:=4
PMIN:=13
V:=$(shell make --no-print-directory -C ${KERNELDIR} kernelversion | cut -d. -f1)
P:=$(shell make --no-print-directory -C ${KERNELDIR} kernelversion | cut -d. -f2)

obj-$(CONFIG_GENZ) += genz.o

genz-objs := genz_bus.o genz_class.o genz_device.o

RUNNING_ARCH := $(shell dpkg-architecture -qDEB_BUILD_ARCH_CPU 2>/dev/null)

all:	versioncheck
ifeq "$(RUNNING_ARCH)" "amd64"
	make $(CFLAGS) V=$(VERBOSE) -C $(KERNELDIR) M=$(PWD) ARCH=x86 modules
else
	make $(CFLAGS) V=$(VERBOSE) -C $(KERNELDIR) M=$(PWD) modules
endif

modules_install:	all
	INSTALL_MOD_DIR=genz sudo -E make V=$(VERBOSE) -C $(KERNELDIR) M=$(PWD) modules_install
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

