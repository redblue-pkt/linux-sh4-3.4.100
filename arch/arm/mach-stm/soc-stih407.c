/*
 * Copyright (C) 2012-2013 STMicroelectronics Limited.
 * Author: Nunzio Raciti <nunzio.raciti@st.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/stm/stih407.h>

#include <asm/mach/map.h>

#include <mach/soc-stih407.h>
#include <mach/hardware.h>

#include "core.h"

static struct map_desc stih407_io_desc[] __initdata = {
	{
		.virtual	= IO_ADDRESS(STIH407_SCU_BASE),
		.pfn		= __phys_to_pfn(STIH407_SCU_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= IO_ADDRESS(STIH407_GIC_DIST_BASE),
		.pfn		= __phys_to_pfn(STIH407_GIC_DIST_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual        = IO_ADDRESS(STIH407_PL310_BASE),
		.pfn            = __phys_to_pfn(STIH407_PL310_BASE),
		.length         = SZ_16K,
		.type           = MT_DEVICE,
	}, {

	/* --------- PIO ----------- */
		/* PIO 0-5 */
		.virtual	= IO_ADDRESS(STIH407_PIO_SBC_BASE),
		.pfn		= __phys_to_pfn(STIH407_PIO_SBC_BASE),
		.length		= 0x6000,
		.type		= MT_DEVICE,
	}, {
		/* PIO 10-19 */
		.virtual	= IO_ADDRESS(STIH407_PIO_FRONT0_BASE),
		.pfn		= __phys_to_pfn(STIH407_PIO_FRONT0_BASE),
		.length		= SZ_64K,
		.type		= MT_DEVICE,
	}, {
		/* PIO 20 */
		.virtual	= IO_ADDRESS(STIH407_PIO_FRONT1_BASE),
		.pfn		= __phys_to_pfn(STIH407_PIO_FRONT1_BASE),
		.length		= 0x100,
		.type		= MT_DEVICE,
	}, {
		/* PIO 30-35 */
		.virtual	= IO_ADDRESS(STIH407_PIO_REAR_BASE),
		.pfn		= __phys_to_pfn(STIH407_PIO_REAR_BASE),
		.length		= 0x6000,
		.type		= MT_DEVICE,
	}, {
		/* 40-42: PIO_FLASH */
		.virtual	= IO_ADDRESS(STIH407_PIO_FLASH_BASE),
		.pfn		= __phys_to_pfn(STIH407_PIO_FLASH_BASE),
		.length		= 0x3000,
		.type		= MT_DEVICE,
	}, {

	/* --------- SYSCONF ----------- */
		.virtual	= IO_ADDRESS(STIH407_SYSCONF_PIO_SBC_BASE),
		.pfn		= __phys_to_pfn(STIH407_SYSCONF_PIO_SBC_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= IO_ADDRESS(STIH407_SYSCONF_PIO_FRONT_BASE),
		.pfn		= __phys_to_pfn(STIH407_SYSCONF_PIO_FRONT_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= IO_ADDRESS(STIH407_SYSCONF_PIO_REAR_BASE),
		.pfn		= __phys_to_pfn(STIH407_SYSCONF_PIO_REAR_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= IO_ADDRESS(STIH407_SYSCONF_PIO_FLASH_BASE),
		.pfn		= __phys_to_pfn(STIH407_SYSCONF_PIO_FLASH_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= IO_ADDRESS(STIH407_SYSCONF_SBC_BASE),
		.pfn		= __phys_to_pfn(STIH407_SYSCONF_SBC_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= IO_ADDRESS(STIH407_SYSCONF_CORE_BASE),
		.pfn		= __phys_to_pfn(STIH407_SYSCONF_CORE_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= IO_ADDRESS(STIH407_SBC_LPM_CONF_BASE),
		.pfn		= __phys_to_pfn(STIH407_SBC_LPM_CONF_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {

	/* ---------  SBC COMMS ----------- */
		.virtual	= IO_ADDRESS(STIH407_SBC_ASC0_BASE),
		.pfn		= __phys_to_pfn(STIH407_SBC_ASC0_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= IO_ADDRESS(STIH407_SBC_ASC1_BASE),
		.pfn		= __phys_to_pfn(STIH407_SBC_ASC1_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {

	/* ---------  COMMS ----------- */
		.virtual	= IO_ADDRESS(STIH407_ASC0_BASE),
		.pfn		= __phys_to_pfn(STIH407_ASC0_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= IO_ADDRESS(STIH407_ASC1_BASE),
		.pfn		= __phys_to_pfn(STIH407_ASC1_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= IO_ADDRESS(STIH407_ASC2_BASE),
		.pfn		= __phys_to_pfn(STIH407_ASC2_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= IO_ADDRESS(STIH407_ASC3_BASE),
		.pfn		= __phys_to_pfn(STIH407_ASC3_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	},
};

void __init stih407_map_io(void)
{
	iotable_init(stih407_io_desc, ARRAY_SIZE(stih407_io_desc));
#ifdef CONFIG_SMP
	scu_base_addr = ((void __iomem *) IO_ADDRESS(STIH407_SCU_BASE));
#endif
}
