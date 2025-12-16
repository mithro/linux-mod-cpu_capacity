# SPDX-License-Identifier: GPL-2.0-or-later
#
# Makefile for cpu_capacity_mod - out-of-tree kernel module build
#

obj-m := cpu_capacity_mod.o

# Kernel build directory
KDIR ?= /lib/modules/$(shell uname -r)/build

# Current directory
PWD := $(shell pwd)

# Allow unresolved symbols during build - cpu_scale is exported by the kernel
# but not included in Ubuntu's headers Module.symvers. The symbol will be
# resolved at module load time.
KBUILD_MODPOST_WARN := 1

# Default target
all: modules

modules:
	$(MAKE) -C $(KDIR) M=$(PWD) KBUILD_MODPOST_WARN=$(KBUILD_MODPOST_WARN) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f Module.symvers modules.order

# Install module to system
install: modules
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install
	depmod -a

# Load module
load: modules
	sudo insmod cpu_capacity_mod.ko

# Unload module
unload:
	sudo rmmod cpu_capacity_mod

# Reload module
reload: unload load

# Show current cpu_capacity values
show-capacities:
	@echo "Current CPU capacities:"
	@for cpu in /sys/devices/system/cpu/cpu*/cpu_capacity; do \
		echo "  $$(dirname $$cpu | xargs basename): $$(cat $$cpu)"; \
	done

.PHONY: all modules clean install load unload reload show-capacities
