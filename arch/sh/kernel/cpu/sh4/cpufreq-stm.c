/*
 *
 * Copyright (C) 2008-2011 STMicroelectronics
 * Author: Francesco M. Virlinzi <francesco.virlinzi@st.com>
 *
 * This program is under the terms of the
 * General Public License version 2 ONLY
 *
 */
#include <linux/types.h>
#include <linux/cpufreq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/time.h>
#include <linux/delay.h>	/* loops_per_jiffy */
#include <linux/cpumask.h>
#include <linux/smp.h>
#include <linux/sched.h>	/* set_cpus_allowed() */
#include <linux/slab.h>
#include <linux/stm/clk.h>
#include <linux/io.h>

#include "cpufreq-stm.h"

static struct cpufreq_frequency_table *cpu_freqs;
static struct stm_cpufreq *stm_cpufreq;

/*
 * Here we notify other drivers of the proposed change and the final change.
 */
static int stm_cpufreq_setstate(unsigned int cpu, unsigned int set)
{
	cpumask_t cpus_allowed;
	struct cpufreq_freqs freqs = {
		.cpu = cpu,
		.old = clk_get_rate(stm_cpufreq->cpu_clk) / 1000,
		.new = cpu_freqs[set].frequency,
		.flags = 0,
	};

	pr_debug("%s\n", __func__);

	if (!cpu_online(cpu)) {
		pr_debug("%s: cpu not online\n", __func__);
		return -ENODEV;
	}
	cpus_allowed = current->cpus_allowed;
	set_cpus_allowed(current, cpumask_of_cpu(cpu));
	BUG_ON(smp_processor_id() != cpu);

	cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);

	stm_cpufreq->update(set);

	stm_cpufreq->cpu_clk->rate = (cpu_freqs[set].frequency << 3) * 125;

	set_cpus_allowed(current, cpus_allowed);

	/* updates the loops_per_jiffies */
	cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);

	return 0;
}

static int stm_cpufreq_init(struct cpufreq_policy *policy)
{
	if (!cpu_online(policy->cpu))
		return -ENODEV;

	pr_debug("%s\n", __func__);
	/* cpuinfo and default policy values */
	policy->governor = CPUFREQ_DEFAULT_GOVERNOR;
	policy->cur = clk_get_rate(stm_cpufreq->cpu_clk) / 1000;
	policy->cpuinfo.transition_latency = 10;

	return cpufreq_frequency_table_cpuinfo(policy, cpu_freqs);
}

static int stm_cpufreq_verify(struct cpufreq_policy *policy)
{
	int ret = cpufreq_frequency_table_verify(policy, cpu_freqs);
	pr_debug("%s: ret %d\n", __func__, ret);
	return ret;
}

static int stm_cpufreq_target(struct cpufreq_policy *policy,
			     unsigned int target_freq, unsigned int relation)
{
	unsigned int idx = 0;
	pr_debug("%s\n", __func__);
	if (cpufreq_frequency_table_target(policy,
					   &cpu_freqs[0], target_freq, relation,
					   &idx))
		return -EINVAL;

	stm_cpufreq_setstate(policy->cpu, idx);
	return 0;
}

static unsigned int stm_cpufreq_get(unsigned int cpu)
{
	pr_debug("%s\n", __func__);
	return clk_get_rate(stm_cpufreq->cpu_clk) / 1000;
}

#ifdef CONFIG_PM
static unsigned long pm_old_freq;
static int stm_cpufreq_suspend(struct cpufreq_policy *policy)
{
	pr_debug("%s\n", __func__);
	pm_old_freq = stm_cpufreq_get(0);/* save current frequency */
	stm_cpufreq->update(0);
	return 0;
}

static int stm_cpufreq_resume(struct cpufreq_policy *policy)
{
	int i;
	pr_debug("%s\n", __func__);
	for (i = 0; cpu_freqs[i].frequency != CPUFREQ_TABLE_END; ++i)
		if (cpu_freqs[i].frequency == pm_old_freq)
			break;
	stm_cpufreq->update(i);
	return 0;
}
#else
#define stm_cpufreq_suspend      NULL
#define stm_cpufreq_resume       NULL
#endif

static struct cpufreq_driver stm_cpufreq_driver = {
	.owner = THIS_MODULE,
	.name = "stm-cpufreq",
	.init = stm_cpufreq_init,
	.verify = stm_cpufreq_verify,
	.get = stm_cpufreq_get,
	.target = stm_cpufreq_target,
	.suspend = stm_cpufreq_suspend,
	.resume = stm_cpufreq_resume,
	.flags = CPUFREQ_PM_NO_WARN,
};

int stm_cpufreq_register(struct stm_cpufreq *soc_cpufreq)
{
	int idx, num_divisors;
	unsigned char *divisors;

	pr_debug("%s\n", __func__);

	if (stm_cpufreq)
		return -EINVAL;

	if (!soc_cpufreq->divisors || !soc_cpufreq->update ||
		!soc_cpufreq->cpu_clk)
		return -EINVAL;

	stm_cpufreq = soc_cpufreq;
	divisors = stm_cpufreq->divisors;

	for (num_divisors = 0;
	     divisors[num_divisors] != STM_FREQ_NOMORE_DIVISOR;
	     ++num_divisors)
		;

	pr_debug("%s: num_divisors %d\n", __func__, num_divisors);

	cpu_freqs = kmalloc((num_divisors + 1) * sizeof(*cpu_freqs),
			GFP_KERNEL);

	if (!cpu_freqs)
		return -ENOMEM;

	for (idx = 0; idx < num_divisors; ++idx) {
		cpu_freqs[idx].index = idx;
		cpu_freqs[idx].frequency =
		    (clk_get_rate(stm_cpufreq->cpu_clk->parent) / 1000)
		    / divisors[idx];

		pr_debug("%s: initialize ids %u @%uKHz\n", __func__, idx,
			 cpu_freqs[idx].frequency);
	}

	cpu_freqs[num_divisors].frequency = CPUFREQ_TABLE_END;

	if (cpufreq_register_driver(&stm_cpufreq_driver)) {
		kfree(cpu_freqs);
		stm_cpufreq = NULL;
		return -EINVAL;
	}
	pr_info("stm: cpufreq: Registered\n");

	return 0;
}
EXPORT_SYMBOL(stm_cpufreq_register);

int stm_cpufreq_remove(struct stm_cpufreq *soc_cpufreq)
{
	if (!soc_cpufreq || soc_cpufreq != stm_cpufreq)
		return -EINVAL;

	pr_debug("%s\n", __func__);
	stm_cpufreq->update(0);	/* switch to the highest frequency */
	cpufreq_unregister_driver(&stm_cpufreq_driver);
	kfree(cpu_freqs);
	cpu_freqs = NULL;
	stm_cpufreq = NULL;
	return 0;
}
EXPORT_SYMBOL(stm_cpufreq_remove);

MODULE_DESCRIPTION("cpufreq driver for ST Platform");
MODULE_LICENSE("GPL");
