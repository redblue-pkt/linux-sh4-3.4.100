/*
 * -------------------------------------------------------------------------
 * Copyright (C) 2013  STMicroelectronics
 * Author: Francesco M. Virlinzi  <francesco.virlinzi@st.com>
 *
 * May be copied or modified under the terms of the GNU General Public
 * License V.2 ONLY.  See linux/COPYING for more information.
 *
 * -------------------------------------------------------------------------
 */

#include "../hom.h"
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/suspend.h>
#include <linux/errno.h>
#include <linux/irqflags.h>
#include <linux/io.h>

#include <linux/stm/stih407.h>
#include <linux/stm/sysconf.h>
#include <linux/stm/platform.h>
#include <linux/stm/gpio.h>
#include <linux/stm/stih407-periphs.h>

#include <asm/hardware/gic.h>

#include <linux/stm/hom.h>
#include <linux/stm/poke_table.h>
#include <linux/stm/synopsys_dwc_ddr32.h>

#include <mach/soc-stih407.h>

#define SBC_MBX_WRITE_STATUS(x)	(STIH407_SBC_MAILBOX + 0x4 + 0x4 * (x))

static const unsigned long __stih407_hom_ddr[] = {
synopsys_ddr32_in_hom(STIH407_DDR_PCTL_BASE),
};

static unsigned long __stih407_hom_lmi_retention[] = {
/*
 * Enable retention mode gpio
 * Address and value set in stm_setup_lmi_retention_gpio.
 */
POKE32(0x0, 0x0), /* dummy value just to guarantee the required space */
};

static const unsigned long __stih407_hom_enter_passive[] = {
/*
 * Send message 'ENTER_PASSIVE' (0x5)
 */
POKE32(SBC_MBX_WRITE_STATUS(0), 0x5),
};

#define HOM_TBL(name) {					\
		.addr = name,				\
		.size = ARRAY_SIZE(name) * sizeof(long),\
	}

static struct hom_table stih407_hom_table[] = {
	HOM_TBL(__stih407_hom_ddr),
	HOM_TBL(__stih407_hom_lmi_retention),
	HOM_TBL(__stih407_hom_enter_passive),
};

static struct stm_wakeup_devices stih407_wkd;

static int stih407_hom_prepare(void)
{
	stm_check_wakeup_devices(&stih407_wkd);
	stm_freeze_board(&stih407_wkd);

	return 0;
}

static int stih407_hom_complete(void)
{
	stm_restore_board(&stih407_wkd);

	return 0;
}

static struct stm_mem_hibernation stih407_hom = {
	.eram_iomem = (void *)0x06000000,
	.gpio_iomem = (void *)STIH407_PIO_SBC_BASE,

	.ops.prepare = stih407_hom_prepare,
	.ops.complete = stih407_hom_complete,
};

int __init stm_hom_stih407_setup(struct stm_hom_board *hom_board)
{
	int ret, i;

	stih407_hom.board = hom_board;

	ret = stm_setup_lmi_retention_gpio(&stih407_hom,
		__stih407_hom_lmi_retention);

	if (ret)
		return ret;

	INIT_LIST_HEAD(&stih407_hom.table);

	for (i = 0; i < ARRAY_SIZE(stih407_hom_table); ++i)
		list_add_tail(&stih407_hom_table[i].node, &stih407_hom.table);

	ret =  stm_hom_register(&stih407_hom);
	if (ret) {
		gpio_free(hom_board->lmi_retention_gpio);
		pr_err("stm pm hom: Error: on stm_hom_register\n");
	}
	return ret;
}
