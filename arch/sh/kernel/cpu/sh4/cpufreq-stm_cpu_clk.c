/*
 *
 * Generic Cpufreq driver for the ST chips
 *
 * Copyright (C) 2011 STMicroelectronics
 * Author: Francesco M. Virlinzi <francesco.virlinzi@st.com>
 *
 * This program is under the terms of the
 * General Public License version 2 ONLY
 *
 */
#include <linux/cpufreq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <linux/stm/clk.h>

#include "cpufreq-stm.h"

#define STM_MAX_NUM_RATIOS	32

static int stm_cpu_clk_update(unsigned int set);

static struct stm_cpufreq stm_cpu_clk_cpufreq = {
	.update = stm_cpu_clk_update,
};

static int stm_cpu_clk_update(unsigned int set)
{
	unsigned long rate;

	rate = clk_get_rate(stm_cpu_clk_cpufreq.cpu_clk->parent) /
		stm_cpu_clk_cpufreq.divisors[set];

	clk_set_rate(stm_cpu_clk_cpufreq.cpu_clk, rate);

	return 0;
}

static int __init stm_cpu_clk_cpufreq_init(void)
{
	unsigned long rate, parent_rate;
	unsigned long min_ratio; /* to determine the max frequency allowed */
	int i, num_divisors;
	int ret;

	stm_cpu_clk_cpufreq.cpu_clk = clk_get(NULL, "cpu_clk");

	if (IS_ERR(stm_cpu_clk_cpufreq.cpu_clk))
		return -EINVAL;

	rate = clk_get_rate(stm_cpu_clk_cpufreq.cpu_clk);
	parent_rate = clk_get_rate(stm_cpu_clk_cpufreq.cpu_clk->parent);

	min_ratio = parent_rate / rate;
	num_divisors = STM_MAX_NUM_RATIOS - (min_ratio - 1) + 1;

	stm_cpu_clk_cpufreq.divisors = kmalloc(num_divisors, GFP_KERNEL);
	if (!stm_cpu_clk_cpufreq.divisors)
		return -ENOMEM;

	for (i = 0; i < num_divisors - 1; ++i)
		stm_cpu_clk_cpufreq.divisors[i] = min_ratio++;

	/* Add a marker for the table end */
	stm_cpu_clk_cpufreq.divisors[num_divisors-1] = STM_FREQ_NOMORE_DIVISOR;

	ret = stm_cpufreq_register(&stm_cpu_clk_cpufreq);

	if (ret)
		kfree(stm_cpu_clk_cpufreq.divisors);

	return ret;
}

static void stm_cpu_clk_cpufreq_exit(void)
{
	stm_cpufreq_remove(&stm_cpu_clk_cpufreq);
}

module_init(stm_cpu_clk_cpufreq_init);
module_exit(stm_cpu_clk_cpufreq_exit);
