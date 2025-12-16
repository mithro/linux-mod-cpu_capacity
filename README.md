# cpu_capacity_mod

[![Build](https://github.com/mithro/linux-mod-cpu_capacity/actions/workflows/build.yml/badge.svg)](https://github.com/mithro/linux-mod-cpu_capacity/actions/workflows/build.yml)

A Linux kernel module that allows runtime modification of CPU capacity values via sysfs.

## Overview

The Linux kernel exposes CPU capacity information via `/sys/devices/system/cpu/cpu<N>/cpu_capacity`, but this interface is read-only. This module provides a writable interface to modify these values, which can be useful for:

- Testing scheduler behavior with asymmetric CPU capacities
- Simulating heterogeneous systems (e.g., big.LITTLE) on homogeneous hardware
- Debugging and development of capacity-aware scheduling

## Technical Details

The module directly modifies the `cpu_scale` per-CPU variable, which is exported via `EXPORT_PER_CPU_SYMBOL_GPL`. This is the same variable that backs the read-only `cpu_capacity` sysfs file.

Capacity values use the kernel's standard scale of 0-1024, where 1024 (`SCHED_CAPACITY_SCALE`) represents 100% capacity.

## Requirements

- Linux kernel headers for your running kernel
- Build tools (make, gcc)
- Root access to load the module

## Building

```bash
make
```

To build for a different kernel version:

```bash
make KDIR=/path/to/kernel/build
```

**Note:** You may see a warning during build:
```
WARNING: modpost: "cpu_scale" [cpu_capacity_mod.ko] undefined!
```
This is expected. The `cpu_scale` symbol is exported by the kernel but not included in distribution kernel header packages' `Module.symvers`. The symbol will be resolved at module load time.

## Usage

### Loading the Module

```bash
# Load with initial capacity settings
sudo modprobe cpu_capacity_mod capacities="0-3:1024,4-7:512"

# Or load without initial settings
sudo insmod cpu_capacity_mod.ko
```

### Setting Capacities at Runtime

```bash
# Set a single CPU
echo "0:512" | sudo tee /sys/module/cpu_capacity_mod/parameters/capacities

# Set a range of CPUs
echo "0-3:1024" | sudo tee /sys/module/cpu_capacity_mod/parameters/capacities

# Set multiple specifications
echo "0-3:1024,4-7:512,8:256" | sudo tee /sys/module/cpu_capacity_mod/parameters/capacities
```

### Syntax

- Single CPU: `cpu:capacity` (e.g., `0:512`)
- CPU range: `start-end:capacity` (e.g., `0-3:1024`)
- Multiple: Comma-separated (e.g., `0-3:1024,4-7:512`)

### Verifying Changes

```bash
# Check all CPU capacities
for cpu in /sys/devices/system/cpu/cpu*/cpu_capacity; do
  echo "$(dirname $cpu | xargs basename): $(cat $cpu)"
done

# Or use the Makefile helper
make show-capacities
```

### Unloading

When the module is unloaded, original capacity values are automatically restored:

```bash
sudo rmmod cpu_capacity_mod
```

## Validation

To verify the scheduler is using the new capacity values:

```bash
# 1. Set asymmetric capacities
echo "0-3:1024,4-7:256" | sudo tee /sys/module/cpu_capacity_mod/parameters/capacities

# 2. Run a CPU-bound workload and observe task placement
stress-ng --cpu 4 --timeout 10s &
watch -n 0.5 'ps -eo pid,comm,psr | grep stress'

# 3. Use perf to trace task migrations
sudo perf sched record -e sched:sched_migrate_task -a -- sleep 5
sudo perf sched script
```

If the scheduler doesn't respond to changes (e.g., on systems with Energy Aware Scheduling), you may need to trigger a scheduler domain rebuild:

```bash
echo 0 | sudo tee /proc/sys/kernel/sched_energy_aware
echo 1 | sudo tee /proc/sys/kernel/sched_energy_aware
```

## Makefile Targets

- `make` or `make modules` - Build the module
- `make clean` - Clean build artifacts
- `make install` - Install module system-wide
- `make load` - Load the module (requires sudo)
- `make unload` - Unload the module (requires sudo)
- `make reload` - Reload the module (requires sudo)
- `make show-capacities` - Display current CPU capacities

## License

GPL-2.0-or-later

This module must be GPL-licensed to access the `cpu_scale` per-CPU variable, which is exported with `EXPORT_PER_CPU_SYMBOL_GPL`.
