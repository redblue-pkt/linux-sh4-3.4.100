/*
 * -------------------------------------------------------------------------
 * Copyright (C) 2013  STMicroelectronics
 * Author: Francesco M. Virlinzi  <francesco.virlinzi@st.com>
 *
 * May be copied or modified under the terms of the GNU General Public
 * License V.2 ONLY.  See linux/COPYING for more information.
 *
 * ------------------------------------------------------------------------- */

#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/suspend.h>
#include <linux/errno.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/irqflags.h>
#include <linux/irq.h>
#include <linux/io.h>

#include <linux/stm/stih407.h>
#include <linux/stm/stih407-periphs.h>
#include <linux/stm/sysconf.h>
#include <linux/stm/clk.h>
#include <linux/stm/wakeup_devices.h>

#include "../suspend.h"
#include <mach/hardware.h>
#include <mach/soc-stih407.h>
#include <asm/hardware/gic.h>	/* gic offset and struct gic_chip_data */

#include <linux/stm/poke_table.h>
#include <linux/stm/synopsys_dwc_ddr32.h>

#define SYSCONF_CPU_CLK_SELECTION	(STIH407_SYSCONF_CORE_BASE + 0x1a4)
#define SYSCONF_CPU_CLK_POWER_DOWN	(STIH407_SYSCONF_CORE_BASE + 0x1a4)
#define SYSCONF_CPU_CLK_LOCK_STATUS	(STIH407_SYSCONF_CORE_BASE + 0x87c)

#define CKG_A0_IOBASE		0x090ff000

static void __iomem *clks_c0_base;
static struct stm_wakeup_devices stih407_wkd;

static long stih407_ddr_enter[] = {
synopsys_ddr32_in_self_refresh(STIH407_DDR_PCTL_BASE),
synopsys_ddr32_phy_standby_enter(STIH407_DDR_PCTL_BASE),
};

static long stih407_ddr_exit[] = {
synopsys_ddr32_phy_standby_exit(STIH407_DDR_PCTL_BASE),
synopsys_ddr32_out_of_self_refresh(STIH407_DDR_PCTL_BASE),
};


static long stih407_a9_clk_enter[] = {
/* bypass and disable the A9.PLL */
OR32(SYSCONF_CPU_CLK_SELECTION, 1 << 2),
OR32(SYSCONF_CPU_CLK_POWER_DOWN, 1),
};

static long stih407_a9_clk_exit[] = {
/* enable, wait and don't bypass the A9.PLL */
UPDATE32(SYSCONF_CPU_CLK_POWER_DOWN, ~1, 0),
WHILE_NE32(SYSCONF_CPU_CLK_LOCK_STATUS, 1, 1),
UPDATE32(SYSCONF_CPU_CLK_SELECTION, ~(1 << 2), 0),
};

static long stih407_ddr_clk_a0_enter[] = {
#if 0
	/*
	 * FIXME:
	 *
	 * Thi A0.Pll3200 should be turned-off but to do that the
	 * clock A0_IC_LMI0 should be routed under the external oscillator
	 * Soc designer confirmed it's possible... but it doesn't work
	 * and it needs investigation
	 */
	/* Bypass the A0.Pll */
	OR32(CKG_A0_IOBASE + 0x18, 0x1),
	/* turn-off A0.PLL3200 */
	OR32(CKG_A0_IOBASE + 0x2a0, 0x1 << 8),
#endif
};

static long stih407_ddr_clk_a0_exit[] = {
	/* turn-on A0.PLL3200 */
	UPDATE32(CKG_A0_IOBASE + 0x2a0, ~(0x1 << 8), 0),
	/* Wait PLL lock */
	WHILE_NE32(CKG_A0_IOBASE + 0x2a0, 1 << 24, 1 << 24),
	/* Remove Bypass the A0.Pll */
	UPDATE32(CKG_A0_IOBASE + 0x18, ~1, 0),
};

#define SUSPEND_TBL(_name) {					\
	.enter = _name##_enter,					\
	.enter_size = ARRAY_SIZE(_name##_enter) * sizeof(long),	\
	.exit = _name##_exit,					\
	.exit_size = ARRAY_SIZE(_name##_exit) * sizeof(long),	\
}

static struct stm_suspend_table stih407_suspend_tables[] = {
	SUSPEND_TBL(stih407_ddr),
	SUSPEND_TBL(stih407_a9_clk),
	SUSPEND_TBL(stih407_ddr_clk_a0),
};

#define clk_gen_c_gfg_nr		3
#define clk_gen_c_xbar_output_nr	32
#define clk_gen_c_xbar_osc_input	6

static void flex_xbar_mux(void *base, int id, int mux)
{
	unsigned long offset = 0x18 + (id / 4) * 0x4;
	unsigned long shift = 8 * (id % 4);
	unsigned long mask = (0x40 << shift) | ~(0xff << shift);
	unsigned long data;

	mux &= 0x3f; /* mux IS 6 bits */

	data = readl(base + offset);
	data &= mask;			/* clear only the mux-id field */
	data |= (mux << shift);		/* apply the new mux */
	writel(data, base + offset);
}

static char flex_xbar_get_cfg(void *base, int id)
{
	unsigned long offset = 0x18 + (id / 4) * 0x4;
	unsigned long shift = 8 * (id % 4);
	unsigned long data;

	data = readl(base + offset);

	return (char)((data >> shift) & 0xff);
}

static void flex_xbar_set_cfg(void *base, int id, char value)
{
	unsigned long offset = 0x18 + (id / 4) * 0x4;
	unsigned long shift = 8 * (id % 4);
	unsigned long data, nvalue = value;

	data = readl(base + offset);

	data &= ~(0xff << shift);
	data |= (nvalue << shift);

	writel(data, base + offset);
}

static void flex_gfg_enable(void *base, int id, int enable)
{
	unsigned long data;
	unsigned long offset = 0x2a0 + id * 0x28;

	data = readl(base + offset);

	if (enable) {
		/* enable */
		writel(data & ~(1 << 8), base + offset);
		/* wait for stable signal */
		while (!(readl(base + offset) & (1 << 24)))
			cpu_relax();
		return;
	}

	/* disable */
	writel(data | (1 << 8), base + offset);
}

static int stih407_suspend_core(suspend_state_t state, int resume)
{
	int i;
	static char *xbar_cfg;

	if (resume)
		goto on_resume;

	xbar_cfg = kmalloc(sizeof(char) * clk_gen_c_xbar_output_nr,
		GFP_ATOMIC);

	if (!xbar_cfg)
		return -ENOMEM;

	/* save the current xbar configuration */
	for (i = 0; i < clk_gen_c_xbar_output_nr; ++i)
		xbar_cfg[i] = flex_xbar_get_cfg(clks_c0_base, i);

	/* route all the clks on the external oscillator */
	for (i = 0; i < clk_gen_c_xbar_output_nr; ++i)
		flex_xbar_mux(clks_c0_base, i, clk_gen_c_xbar_osc_input);

	/* Turned-off GFG_PLLs, GFG_FSs... */
	for (i = 0; i < clk_gen_c_gfg_nr; ++i)
		flex_gfg_enable(clks_c0_base, i, 0);

	pr_debug("stm: pm: stih407: FlexClockGens C: saved\n");

	return 0;

on_resume:
	/* Turned-on GFG_PLLs, GFG_FSs... */
	for (i = 0; i < clk_gen_c_gfg_nr; ++i)
		flex_gfg_enable(clks_c0_base, i, 1);

	/* restore the original xbar setting */
	for (i = 0; i < clk_gen_c_xbar_output_nr; ++i)
		flex_xbar_set_cfg(clks_c0_base, i, xbar_cfg[i]);

	kfree(xbar_cfg);
	xbar_cfg = NULL;

	pr_debug("stm: pm: stih407: FlexClockGens C: restored\n");
	return 0;
}

static int stih407_suspend_begin(suspend_state_t state)
{
	pr_info("stm: pm: Analyzing the wakeup devices\n");

	stm_check_wakeup_devices(&stih407_wkd);

	return 0;
}

static int stih407_suspend_pre_enter(suspend_state_t state)
{
	return stih407_suspend_core(state, 0);
}

static void stih407_suspend_post_enter(suspend_state_t state)
{
	stih407_suspend_core(state, 1);
}

static int stih407_get_wake_irq(void)
{
	int irq = 0;
	struct irq_data *d;
	void *gic_cpu = __io_address(STIH407_GIC_CPU_BASE);

	irq = readl(gic_cpu + GIC_CPU_INTACK);
	d = irq_get_irq_data(irq);
	writel(d->hwirq, gic_cpu + GIC_CPU_EOI);

	return irq;
}

static struct stm_platform_suspend stih407_suspend = {
	.ops.begin = stih407_suspend_begin,

	.pre_enter = stih407_suspend_pre_enter,
	.post_enter = stih407_suspend_post_enter,

	.eram_iomem = (void *)0x06000000,
	.get_wake_irq = stih407_get_wake_irq,

};

static int __init stih407_suspend_setup(void)
{
	int i;

	clks_c0_base = ioremap_nocache(0x09103000, 0x1000);

	if (!clks_c0_base)
		goto err_0;

	INIT_LIST_HEAD(&stih407_suspend.mem_tables);

	 for (i = 0; i < ARRAY_SIZE(stih407_suspend_tables); ++i)
		list_add_tail(&stih407_suspend_tables[i].node,
			&stih407_suspend.mem_tables);

	return stm_suspend_register(&stih407_suspend);

err_0:
	return -EINVAL;
}

module_init(stih407_suspend_setup);
