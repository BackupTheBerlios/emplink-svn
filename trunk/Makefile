KERNEL_VERSION	:= `uname -r`
KERNEL_DIR	:= /lib/modules/$(KERNEL_VERSION)/build
INSTALL_MOD_DIR	:= usb/media

PWD		:= $(shell pwd)

obj-m		:= emplink.o
TEST_CFLAGS	:= -Wall -g0 -O2

%.o : %.c
	gcc $(TEST_CFLAGS) -c -o $@ $<

all: emplink

emplink:
	@echo "Building EMPLINK driver..."
	@(cd $(KERNEL_DIR) && make -C $(KERNEL_DIR) SUBDIRS=$(PWD) modules)

install:
	@echo "Installing USB Video Class driver..."
	@(cd $(KERNEL_DIR) && make -C $(KERNEL_DIR) SUBDIRS=$(PWD) INSTALL_MOD_DIR=$(INSTALL_MOD_DIR) modules_install)
	depmod -ae

clean:
	-rm -f *.o *.ko .*.cmd .*.flags *.mod.c Modules.symvers
	-rm -rf .tmp_versions

