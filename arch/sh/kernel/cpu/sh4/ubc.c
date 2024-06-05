/*
 * arch/sh/kernel/cpu/sh4/ubc.c
 *
 * On-chip UBC support for SH-4 CPUs.
 *
 * Copyright (C) 2014 STMicroelectronics Limited
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <asm/hw_breakpoint.h>

#define UBC_BARA		0xff200000
#define UBC_BAMRA		0xff200004
#define UBC_BBRA		0xff200008
#define UBC_BASRA		0xff000014
#define UBC_BARB		0xff20000c
#define UBC_BAMRB		0xff200010
#define UBC_BBRB		0xff200014
#define UBC_BASRB		0xff000018
#define UBC_BDRB		0xff200018
#define UBC_BDMRB		0xff20001c
#define UBC_BRCR		0xff200020

#define BAMR_ASID		(1 << 2)
#define BAMR_NONE		0
#define BAMR_10			0x1
#define BAMR_12			0x2
#define BAMR_ALL		0x3
#define BAMR_16			0x8
#define BAMR_20			0x9

#define BBR_INST		(1 << 4)
#define BBR_DATA		(2 << 4)
#define BBR_READ		(1 << 2)
#define BBR_WRITE		(2 << 2)
#define BBR_BYTE		0x1
#define BBR_HALF		0x2
#define BBR_LONG		0x3
#define BBR_QUAD		(1 << 6)

#define BRCR_CMFA		(1 << 15)
#define BRCR_CMFB		(1 << 14)

#define BRCR_PCTE		(1 << 11)
#define BRCR_PCBA		(1 << 10)	/* 1: after execution */
#define BRCR_DBEB		(1 << 7)
#define BRCR_PCBB		(1 << 6)
#define BRCR_SEQ		(1 << 3)
#define BRCR_UBDE		(1 << 0)

static struct sh_ubc sh4_ubc;

static void sh4_ubc_enable(struct arch_hw_breakpoint *info, int idx)
{
	switch (idx) {
	case 0:
		__raw_writel(info->address, UBC_BARA);
		__raw_writeb(BAMR_ASID, UBC_BAMRA);
		__raw_writew(__raw_readw(UBC_BRCR) | BRCR_PCBA, UBC_BRCR);
		__raw_writew(BBR_INST | BBR_READ, UBC_BBRA);
		break;
	case 1:
		__raw_writel(info->address, UBC_BARB);
		__raw_writeb(BAMR_ASID, UBC_BAMRB);
		__raw_writew(__raw_readw(UBC_BRCR) | BRCR_PCBB, UBC_BRCR);
		__raw_writew(BBR_INST | BBR_READ, UBC_BBRB);
		break;
	default:
		BUG();
	}
}

static void sh4_ubc_disable(struct arch_hw_breakpoint *info, int idx)
{
	__raw_writew(0, (idx == 0) ? UBC_BBRA : UBC_BBRB);
}

static void sh4_ubc_enable_all(unsigned long mask)
{
	__raw_writew(BBR_INST | BBR_READ, UBC_BBRA);
	__raw_writew(BBR_INST | BBR_READ, UBC_BBRB);
}

static void sh4_ubc_disable_all(void)
{
	__raw_writew(0, UBC_BBRA);
	__raw_writew(0, UBC_BBRB);
}

static unsigned long sh4_ubc_active_mask(void)
{
	unsigned long active = 0;

	if (__raw_readw(UBC_BBRA))
		active |= 1;
	if (__raw_readw(UBC_BBRB))
		active |= 2;

	return active;
}

static unsigned long sh4_ubc_triggered_mask(void)
{
	u16 brcr = __raw_readw(UBC_BRCR);
	unsigned long val;

	val =  ((brcr & BRCR_CMFA) ? 1 : 0) | ((brcr & BRCR_CMFB) ? 2 : 0);

	return val;
}

static void sh4_ubc_clear_triggered_mask(unsigned long mask)
{
	u16 brcr = __raw_readw(UBC_BRCR);

	if (mask & 1)
		brcr &= ~BRCR_CMFA;
	if (mask & 2)
		brcr &= ~BRCR_CMFB;

	__raw_writew(brcr, UBC_BRCR);
}

static struct sh_ubc sh4_ubc = {
	.name			= "SH-4",
	.num_events		= 2,
	.trap_nr		= 0x1e0,
	.enable			= sh4_ubc_enable,
	.disable		= sh4_ubc_disable,
	.enable_all		= sh4_ubc_enable_all,
	.disable_all		= sh4_ubc_disable_all,
	.active_mask		= sh4_ubc_active_mask,
	.triggered_mask		= sh4_ubc_triggered_mask,
	.clear_triggered_mask	= sh4_ubc_clear_triggered_mask,
};

static int __init sh4_ubc_init(void)
{
	__raw_writew(0, UBC_BRCR);
	__raw_writew(0, UBC_BBRA);
	__raw_writew(0, UBC_BBRB);

	return register_sh_ubc(&sh4_ubc);
}
arch_initcall(sh4_ubc_init);
