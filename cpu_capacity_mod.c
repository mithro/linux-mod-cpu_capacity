// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * CPU Capacity Module - Allows runtime modification of cpu_capacity values
 *
 * This module provides a sysfs interface to modify the cpu_scale per-CPU
 * variable, which backs the read-only cpu_capacity sysfs file.
 *
 * Usage:
 *   # At load time
 *   modprobe cpu_capacity_mod capacities="0-3:1024,4-7:512"
 *
 *   # At runtime
 *   echo "0:768" > /sys/module/cpu_capacity_mod/parameters/capacities
 *
 * Syntax:
 *   - Single CPU: "0:512"
 *   - CPU range: "0-3:1024"
 *   - Multiple: "0-3:1024,4-7:512,8:256"
 *
 * Copyright (C) 2025
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/percpu.h>
#include <linux/cpumask.h>
#include <linux/arch_topology.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/ctype.h>

/* SCHED_CAPACITY_SCALE is defined in linux/sched.h as 1024 */
#define MAX_CAPACITIES_LEN 256

/* Buffer to store current capacity settings for display */
static char capacities_buf[MAX_CAPACITIES_LEN];
static DEFINE_MUTEX(capacities_mutex);

/* Original capacities to restore on module unload */
static unsigned long *original_capacities;
static bool original_saved;

/**
 * save_original_capacities - Save original cpu_scale values
 *
 * Called once on first capacity modification to enable restoration
 * on module unload.
 */
static int save_original_capacities(void)
{
	int cpu;

	if (original_saved)
		return 0;

	original_capacities = kmalloc_array(nr_cpu_ids, sizeof(unsigned long),
					    GFP_KERNEL);
	if (!original_capacities)
		return -ENOMEM;

	for_each_possible_cpu(cpu)
		original_capacities[cpu] = per_cpu(cpu_scale, cpu);

	original_saved = true;
	return 0;
}

/**
 * restore_original_capacities - Restore original cpu_scale values
 *
 * Called on module unload to restore original capacities.
 */
static void restore_original_capacities(void)
{
	int cpu;

	if (!original_saved || !original_capacities)
		return;

	for_each_possible_cpu(cpu)
		per_cpu(cpu_scale, cpu) = original_capacities[cpu];

	kfree(original_capacities);
	original_capacities = NULL;
	original_saved = false;
}

/**
 * set_cpu_capacity - Set capacity for a single CPU
 * @cpu: CPU number
 * @capacity: Capacity value (0-1024)
 *
 * Returns 0 on success, negative error code on failure.
 */
static int set_cpu_capacity(unsigned int cpu, unsigned long capacity)
{
	if (cpu >= nr_cpu_ids || !cpu_possible(cpu)) {
		pr_err("cpu_capacity_mod: invalid CPU %u (max: %u)\n",
		       cpu, nr_cpu_ids - 1);
		return -EINVAL;
	}

	if (capacity > SCHED_CAPACITY_SCALE) {
		pr_err("cpu_capacity_mod: capacity %lu exceeds max %ld for CPU %u\n",
		       capacity, (long)SCHED_CAPACITY_SCALE, cpu);
		return -EINVAL;
	}

	per_cpu(cpu_scale, cpu) = capacity;
	pr_info("cpu_capacity_mod: set CPU %u capacity to %lu\n", cpu, capacity);

	return 0;
}

/**
 * parse_capacity_spec - Parse a single capacity specification
 * @spec: String like "0:512" or "0-3:1024"
 * @len: Length of the spec string
 *
 * Returns 0 on success, negative error code on failure.
 */
static int parse_capacity_spec(const char *spec, size_t len)
{
	char buf[64];
	char *colon, *dash;
	unsigned int cpu_start, cpu_end, cpu;
	unsigned long capacity;
	int ret;

	if (len >= sizeof(buf)) {
		pr_err("cpu_capacity_mod: spec too long\n");
		return -EINVAL;
	}

	memcpy(buf, spec, len);
	buf[len] = '\0';

	/* Find the colon separator */
	colon = strchr(buf, ':');
	if (!colon) {
		pr_err("cpu_capacity_mod: missing ':' in spec '%s'\n", buf);
		return -EINVAL;
	}
	*colon = '\0';

	/* Parse capacity value */
	ret = kstrtoul(colon + 1, 0, &capacity);
	if (ret) {
		pr_err("cpu_capacity_mod: invalid capacity in spec '%s'\n", buf);
		return ret;
	}

	/* Check for range (dash) */
	dash = strchr(buf, '-');
	if (dash) {
		*dash = '\0';
		ret = kstrtouint(buf, 0, &cpu_start);
		if (ret) {
			pr_err("cpu_capacity_mod: invalid start CPU in spec\n");
			return ret;
		}
		ret = kstrtouint(dash + 1, 0, &cpu_end);
		if (ret) {
			pr_err("cpu_capacity_mod: invalid end CPU in spec\n");
			return ret;
		}
	} else {
		ret = kstrtouint(buf, 0, &cpu_start);
		if (ret) {
			pr_err("cpu_capacity_mod: invalid CPU in spec\n");
			return ret;
		}
		cpu_end = cpu_start;
	}

	/* Validate range */
	if (cpu_start > cpu_end) {
		pr_err("cpu_capacity_mod: invalid range %u-%u\n",
		       cpu_start, cpu_end);
		return -EINVAL;
	}

	/* Apply to all CPUs in range */
	for (cpu = cpu_start; cpu <= cpu_end; cpu++) {
		ret = set_cpu_capacity(cpu, capacity);
		if (ret)
			return ret;
	}

	return 0;
}

/**
 * parse_capacities - Parse comma-separated capacity specifications
 * @str: String like "0-3:1024,4-7:512"
 *
 * Returns 0 on success, negative error code on failure.
 */
static int parse_capacities(const char *str)
{
	const char *p = str;
	const char *end;
	int ret;

	/* Skip leading whitespace */
	while (*p && isspace(*p))
		p++;

	if (!*p)
		return 0;  /* Empty string is valid (no-op) */

	/* Save original capacities before first modification */
	ret = save_original_capacities();
	if (ret)
		return ret;

	while (*p) {
		/* Find end of this spec (comma or end of string) */
		end = strchr(p, ',');
		if (!end)
			end = p + strlen(p);

		/* Skip trailing whitespace in spec */
		while (end > p && isspace(*(end - 1)))
			end--;

		if (end > p) {
			ret = parse_capacity_spec(p, end - p);
			if (ret)
				return ret;
		}

		/* Move to next spec */
		p = (*end == ',') ? end + 1 : end;

		/* Skip leading whitespace of next spec */
		while (*p && isspace(*p))
			p++;
	}

	return 0;
}

/**
 * capacities_set - Callback for writing to module parameter
 */
static int capacities_set(const char *val, const struct kernel_param *kp)
{
	int ret;
	size_t len;

	if (!val)
		return -EINVAL;

	mutex_lock(&capacities_mutex);

	ret = parse_capacities(val);
	if (ret) {
		mutex_unlock(&capacities_mutex);
		return ret;
	}

	/* Store the setting for later retrieval */
	len = strlen(val);
	if (len >= MAX_CAPACITIES_LEN)
		len = MAX_CAPACITIES_LEN - 1;

	/* Remove trailing newline if present */
	if (len > 0 && val[len - 1] == '\n')
		len--;

	memcpy(capacities_buf, val, len);
	capacities_buf[len] = '\0';

	mutex_unlock(&capacities_mutex);

	return 0;
}

/**
 * capacities_get - Callback for reading module parameter
 */
static int capacities_get(char *buf, const struct kernel_param *kp)
{
	int ret;

	mutex_lock(&capacities_mutex);
	ret = scnprintf(buf, PAGE_SIZE, "%s\n", capacities_buf);
	mutex_unlock(&capacities_mutex);

	return ret;
}

static const struct kernel_param_ops capacities_ops = {
	.set = capacities_set,
	.get = capacities_get,
};

module_param_cb(capacities, &capacities_ops, NULL, 0644);
MODULE_PARM_DESC(capacities,
	"CPU capacity settings (e.g., '0-3:1024,4-7:512')");

static int __init cpu_capacity_mod_init(void)
{
	pr_info("cpu_capacity_mod: loaded\n");
	pr_info("cpu_capacity_mod: use 'capacities' parameter to set CPU capacities\n");
	pr_info("cpu_capacity_mod: syntax: 'cpu:value' or 'start-end:value'\n");
	return 0;
}

static void __exit cpu_capacity_mod_exit(void)
{
	restore_original_capacities();
	pr_info("cpu_capacity_mod: unloaded, original capacities restored\n");
}

module_init(cpu_capacity_mod_init);
module_exit(cpu_capacity_mod_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tim Ansell");
MODULE_DESCRIPTION("Module to modify CPU capacity values at runtime");
MODULE_VERSION("1.0");
