/*
 * Copyright (C) 2013 STMicroelectronics Limited
 *
 * Author(s): Giuseppe Condorelli <giuseppe.condorelli@st.com>
 *            Carmelo Amoroso <carmelo.amoroso@st.com>
 *            Giuseppe Cavallaro <peppe.cavallaro@st.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/stm/stih407.h>
#include <linux/stm/stm_device_of.h>
#include <linux/stm/soc.h>
#include <linux/stm/core_of.h>
#include <asm/hardware/gic.h>
#include <asm/mach/time.h>
#include <mach/common-dt.h>
#include <mach/soc-stih407.h>
#include <asm/mach/arch.h>
#include <linux/phy.h>
#include <linux/phy_fixed.h>
#include <linux/tm1668.h>
#include <linux/delay.h>
#include <linux/clk.h>

struct of_dev_auxdata stih407_auxdata_lookup[] __initdata = {
	OF_DEV_AUXDATA("st,fdma", 0x8e20000, "stm-fdma.0", NULL),
	OF_DEV_AUXDATA("st,fdma", 0x8e40000, "stm-fdma.1", NULL),
	OF_DEV_AUXDATA("st,fdma", 0x8e60000, "stm-fdma.2", NULL),
	OF_DEV_AUXDATA("st,fdma-xbar", 0x8f00000, "stm-fdma-xbar.0", NULL),
	OF_DEV_AUXDATA("st,pcie", 0x09b00000, "st,pcie.0", &stm_pcie_config),
	OF_DEV_AUXDATA("st,pcie", 0x09b10000, "st,pcie.1", &stm_pcie_config),
	OF_DEV_AUXDATA("snps,dwmac", 0x9630000, "stmmaceth.0",
		 &ethernet_platform_data),
	OF_DEV_AUXDATA("st,sdhci", 0x9060000, "sdhci-stm.0", NULL),
	OF_DEV_AUXDATA("st,sdhci", 0x9080000, "sdhci-stm.1", NULL),
	OF_DEV_AUXDATA("st,snd_uni_player", 0x08d80000, "snd_uni_player.0",
		 NULL),
	OF_DEV_AUXDATA("st,snd_uni_player", 0x08d81000, "snd_uni_player.1",
		 NULL),
	OF_DEV_AUXDATA("st,snd_uni_player", 0x08d82000, "snd_uni_player.2",
		 NULL),
	OF_DEV_AUXDATA("st,snd_uni_player", 0x08d85000, "snd_uni_player.3",
		 NULL),
	OF_DEV_AUXDATA("st,snd_uni_reader", 0x08d83000, "snd_uni_reader.0",
		 NULL),
	OF_DEV_AUXDATA("st,snd_uni_reader", 0x08d84000, "snd_uni_reader.1",
		 NULL),
	{}
};

static struct fixed_phy_status stmmac1_fixed_phy_status = {
	.link = 1,
	.speed = 100,
	.duplex = 1,
};

static struct tm1668_key b2120_front_panel_keys[] = {
	{ 0x00001000, KEY_UP, "Up (SWF2)" },
	{ 0x00800000, KEY_DOWN, "Down (SWF7)" },
	{ 0x00008000, KEY_LEFT, "Left (SWF6)" },
	{ 0x00000010, KEY_RIGHT, "Right (SWF5)" },
	{ 0x00000080, KEY_OK, "Menu/OK (SWF1)" },
	{ 0x00100000, KEY_BACK, "Back (SWF4)" },
	{ 0x80000000, KEY_TV, "DOXTV (SWF9)" },
};

static struct tm1668_character b2120_front_panel_characters[] = {
	TM1668_7_SEG_HEX_DIGITS,
	TM1668_7_SEG_SEGMENTS,
	TM1668_7_SEG_LETTERS
};

static struct platform_device b2120_front_panel = {
	.name = "tm1668",
	.id = -1,
	.dev.platform_data = &(struct tm1668_platform_data) {
		.gpio_dio = stm_gpio(STIH407_GPIO(20), 5),
		.gpio_sclk = stm_gpio(STIH407_GPIO(20), 7),
		.gpio_stb = stm_gpio(STIH407_GPIO(20), 6),
		.config = tm1668_config_6_digits_12_segments,

		.keys_num = ARRAY_SIZE(b2120_front_panel_keys),
		.keys = b2120_front_panel_keys,
		.keys_poll_period = DIV_ROUND_UP(HZ, 5),

		.brightness = 8,
		.characters_num = ARRAY_SIZE(b2120_front_panel_characters),
		.characters = b2120_front_panel_characters,
		.text = "H407",
	},
};

/* Useful to set the PHY clk to 25MHz for testing 100 speed mode and
 * to 125MHz (although the default from stmtp) for giga.
 */
static void stmmac1_clk_setting(void)
{
	struct clk *clk;
	unsigned int eth1_frq = 25000000;

#ifdef CONFIG_MACH_STM_STIH407_FIXED_1000_SPEED
	eth1_frq = 125000000;
#endif
	clk = clk_get(NULL, "CLK_ETH_PHY");
	clk_set_rate(clk, eth1_frq);
}

#ifdef CONFIG_HIBERNATION_ON_MEMORY

#include <linux/stm/hom.h>

static struct stm_hom_board stih407_board_dt_hom;
static int __init stih407_board_dt_hom_init(void)
{
	struct device_node *np = of_find_node_by_path("/soc");
	int lmi_retention_gpio;

	if (np) {
		lmi_retention_gpio = of_get_named_gpio(np,
			"lmi-retention-gpio", 0);
		if (lmi_retention_gpio > 0) {
			stih407_board_dt_hom.lmi_retention_gpio =
				lmi_retention_gpio;
			stm_hom_stih407_setup(&stih407_board_dt_hom);
			return 0;
		} else
			pr_warning("stm: hom: LMI retention gpio NOT found\n");
		of_node_put(np);
	}

	pr_warning("stm: hom: HoM not supported\n");
	return -EINVAL;
}

#else
static inline int stih407_board_dt_hom_init(void)
{
	return 0;
}
#endif

static void __init stih407_board_dt_init(void)
{
	of_platform_populate(NULL, of_default_bus_match_table,
			     stih407_auxdata_lookup, NULL);

#ifdef CONFIG_MACH_STM_STIH407_FIXED_1000_SPEED
	stmmac1_fixed_phy_status.speed = 1000;
#endif
	BUG_ON(fixed_phy_add(PHY_POLL, 1, &stmmac1_fixed_phy_status));

	stmmac1_clk_setting();

	if ((of_machine_is_compatible("st,stih407-b2120")) ||
	   (of_machine_is_compatible("st,stih410-b2120")) ||
	   (of_machine_is_compatible("st,stih305-b2120")) ||
	   (of_machine_is_compatible("st,stih310-b2120")) ||
	   (of_machine_is_compatible("st,stih407-b2148")) ||
	   (of_machine_is_compatible("st,stih410-b2148")) ||
	   (of_machine_is_compatible("st,stih305-b2148")) ||
	   (of_machine_is_compatible("st,stih310-b2148")))
		platform_device_register(&b2120_front_panel);

	stih407_board_dt_hom_init();

	return;
}

/* Setup the Timer */
static void __init stih407_of_timer_init(void)
{
	if (of_machine_is_compatible("st,stih407") ||
	   of_machine_is_compatible("st,stih305")) {
		/* Monaco SoC family */
		stih407_plat_clk_init();
	} else {
		/* Monaco-2 SoC family */
		stih410_plat_clk_init();
	}
	stih407_plat_clk_alias_init();
	stm_of_timer_init();
	return;
}

struct sys_timer stih407_of_timer = {
	.init	= stih407_of_timer_init,
};

static const char *stih407_dt_match[] __initdata = {
	"st,stih407",
	"st,stih410",
	"st,stih305",
	"st,stih310",
	NULL
};

DT_MACHINE_START(STM, "StiH407 SoC with Flattened Device Tree")
	.map_io		= stih407_map_io,
	.init_early	= core_of_early_device_init,
	.timer		= &stih407_of_timer,
	.handle_irq	= gic_handle_irq,
	.init_machine	= stih407_board_dt_init,
	.init_irq	= stm_of_gic_init,
	.dt_compat	= stih407_dt_match,
	.restart	= stm_of_soc_reset,
MACHINE_END
