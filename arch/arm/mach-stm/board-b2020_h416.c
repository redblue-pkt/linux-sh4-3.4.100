/*
 * arch/arm/mach-stm/board-b2020_h416.c
 *
 * Copyright (C) 2013 STMicroelectronics Limited.
 * Author: Francesco Virlinzi <francesco.virlinzi@st.com>
 *		Srinivas Kandagatla <srinivas.kandagatla@st.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *****************************************************************************
 * NOTE: THIS FILEIS AN INTERMEDIATE TRANSISSION FROM NON-DEVICE TREES
 * TO DEVICE TREES. IDEALLY THIS FILESHOULD NOT EXIST IN FULL DEVICE TREE
 * SUPPORTED KERNEL.
 *
 * WITH THE ASSUMPTION THAT SDK2 WILL MOVE TO FULL DEVICE TREES AT
 * SOME POINT, AT WHICH THIS FILEIS NOT REQUIRED ANYMORE
 *
 * ALSO BOARD SUPPORT WITH THIS APPROCH IS IS DONE IN TWO PLACES
 * 1. IN THIS FILE
 * 2. arch/arm/boot/dts/b2020_h416.dtp
 *	THIS FILECONFIGURES ALL THE DRIVERS WHICH SUPPORT DEVICE TREES.
 *
 * please do not optimize this file or try adding any level of abstraction
 * due to reasons above.
 *****************************************************************************
 */

#include <linux/init.h>
#include <linux/io.h>
#include <linux/input.h>
#include <linux/gpio.h>
#include <linux/stm/platform.h>
#include <linux/stm/stih416.h>
#include <linux/of_platform.h>
#include <linux/stm/core_of.h>
#include <linux/of_gpio.h>
#include <linux/stm/mpe41_of_devices.h>
#include <linux/stm/stm_device_of.h>
#include <linux/stm/soc.h>

#include <asm/hardware/gic.h>
#include <asm/memory.h>
#include <asm/mach/time.h>
#include <mach/common-dt.h>
#include <mach/soc-stih416.h>
#include <mach/hardware.h>

struct of_dev_auxdata stih416_auxdata_lookup[] __initdata = {
	OF_DEV_AUXDATA("st,fdma", 0xfd600000, "stm-fdma.0", NULL),
	OF_DEV_AUXDATA("st,fdma", 0xfd620000, "stm-fdma.1", NULL),
	OF_DEV_AUXDATA("st,fdma", 0xfd640000, "stm-fdma.2", NULL),
	OF_DEV_AUXDATA("st,fdma", 0xfea00000, "stm-fdma.3", NULL),
	OF_DEV_AUXDATA("st,fdma", 0xfea20000, "stm-fdma.4", NULL),
	OF_DEV_AUXDATA("st,usb", 0xfe100000, "stm-usb.0", NULL),
	OF_DEV_AUXDATA("st,usb", 0xfe200000, "stm-usb.1", NULL),
	OF_DEV_AUXDATA("st,usb", 0xfe300000, "stm-usb.2", NULL),
	OF_DEV_AUXDATA("st,usb", 0xfe340000, "stm-usb.3", NULL),
	OF_DEV_AUXDATA("st,sdhci", 0xfe81e000, "sdhci-stm.0", NULL),
	OF_DEV_AUXDATA("st,sdhci", 0xfe81f000, "sdhci-stm.1", NULL),
	OF_DEV_AUXDATA("snps,dwmac", 0xfef08000, "snps,dwmac",
		 &ethernet_platform_data),
	OF_DEV_AUXDATA("st,coproc-st40", 0, "st,coproc-st40",
		 &mpe41_st40_coproc_data),
	OF_DEV_AUXDATA("st,snd_uni_player", 0xfe002000, "snd_uni_player.0",
		 NULL),
	OF_DEV_AUXDATA("st,snd_uni_player", 0xfe003000, "snd_uni_player.1",
		 NULL),
	OF_DEV_AUXDATA("st,snd_uni_player", 0xfe004000, "snd_uni_player.2",
		 NULL),
	OF_DEV_AUXDATA("st,snd_uni_player", 0xfe006000, "snd_uni_player.3",
		 NULL),
	OF_DEV_AUXDATA("st,snd_uni_reader", 0xfe005000, "snd_uni_reader.0",
		 NULL),
	OF_DEV_AUXDATA("st,snd_uni_reader", 0xfe007000, "snd_uni_reader.1",
		 NULL),
	{}
};

static struct stm_pad_config stih416_hdmi_hp_pad_config = {
	.gpios_num = 1,
	.gpios = (struct stm_pad_gpio []) {
		STIH416_PAD_PIO_IN(2, 5, 1),	/* HDMI Hotplug */
	},
};

static void __init b2020_dt_init(void)
{
	of_platform_populate(NULL, of_default_bus_match_table,
				 stih416_auxdata_lookup, NULL);
	/* Default to HDMI HotPlug */
	if (stm_pad_claim(&stih416_hdmi_hp_pad_config, "HDMI_Hotplug") == NULL)
		pr_err("Failed to claim HDMI-Hotplug pad!\n");

	/* 1 SATA + 1 PCIe */
	stih416_configure_miphy(&(struct stih416_miphy_config) {
		.id = 0,
		.iface = UPORT_IF,
		.mode = SATA_MODE,});
	stih416_configure_sata(0);

	stih416_configure_miphy(&(struct stih416_miphy_config) {
			.id = 1,
			.mode = PCIE_MODE,
			.iface = UPORT_IF,});

	stih416_configure_pcie(&(struct stih416_pcie_config) {
			.port = 1,
			.reset_gpio = -1,});

	return;
}

/* Setup the Timer */
static void __init stih416_timer_init(void)
{
	stih416_plat_clk_init();
	stih416_plat_clk_alias_init();

	stm_of_timer_init();
}

struct sys_timer stih416_timer = {
	.init	= stih416_timer_init,
};

static const char *b2020_dt_match[] __initdata = {
	"st,stih416-b2020",
	NULL
};

DT_MACHINE_START(STM_B2020, "STM STiH416 with Flattened Device Tree")
	.map_io		= stih416_map_io,
	.init_early	= core_of_early_device_init,
	.timer		= &stih416_timer,
	.handle_irq	= gic_handle_irq,
	.init_machine	= b2020_dt_init,
	.init_irq	= stm_of_gic_init,
	.restart	= stm_of_soc_reset,
	.dt_compat	= b2020_dt_match,
MACHINE_END

#ifdef CONFIG_HIBERNATION_ON_MEMORY

#include <linux/stm/hom.h>

static struct stm_hom_board b2020_hom = {
	.lmi_retention_gpio = stih416_gpio(4, 4),
};

static int __init b2020_hom_init(void)
{
	return stm_hom_stxh416_setup(&b2020_hom);
}

module_init(b2020_hom_init);
#endif
