# Makefile for prototype Gen-Z subsystem

VERBOSE=0

CONFIG_GENZ ?= m

KERNELBASE = /lib/modules/$(shell uname -r)
KERNELDIR ?= $(KERNELBASE)/build
SHELL=/bin/bash
PWD:=$(shell /bin/pwd)

# struct wait_queue_head, among others, didn't show up until 4.13, but
# was backported into SLES15 (kernel 4.12).  It's a gross test but
# better than nothing.
VMIN:=4
PMIN:=13
V:=$(shell make --no-print-directory -C ${KERNELDIR} kernelversion | cut -d. -f1)
P:=$(shell make --no-print-directory -C ${KERNELDIR} kernelversion | cut -d. -f2)
VFAIL:=Kernel headers are $V.$P, need \>= ${VMIN}.${PMIN}
VFAILNOBACK:=Kernel headers are $V.$P, no backport from \>= ${VMIN}.${PMIN}

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

# Kernel 3 is bad, 5 is good, 4 needs a closer look.
versioncheck:
	@[ $V -lt ${VMIN} ] && echo ${VFAIL} && exit 1; \
	 [ $V -gt ${VMIN} ] && exit 0; \
	 [ $P -ge ${PMIN} ] && exit 0; \
	 grep -q 'struct wait_queue_head' ${KERNELDIR}/include/linux/wait.h && exit 0; \
	 echo ${VFAILNOBACK}; exit 1

