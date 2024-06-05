/*
 * (c) 2011 STMicroelectronics Limited
 *
 *   Author: Srinivas Kandagatla <srinivas.kandagatla@st.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef CONFIG_OF
/* All the Drivers are now configured using device trees so,
 * Please start using device trees */
#warning  "This code will disappear soon, you should use device trees"


#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/ethtool.h>
#include <linux/dma-mapping.h>
#include <linux/phy.h>
#include <linux/stm/miphy.h>
#include <linux/stm/device.h>
#include <linux/clk.h>
#include <linux/stm/emi.h>
#include <linux/stm/pad.h>
#include <linux/stm/sysconf.h>
#include <linux/stm/stih415.h>
#include <linux/stm/stih415-periphs.h>
#include <linux/stm/amba_bridge.h>
#include <linux/delay.h>
#include <asm/irq-ilc.h>
#include "pio-control.h"

/* --------------------------------------------------------------------
 *	   Ethernet MAC resources (PAD and Retiming)
 * --------------------------------------------------------------------*/

#define DATA_IN(_port, _pin, _func, _retiming) \
	{ \
		.gpio = stm_gpio(_port, _pin), \
		.direction = stm_pad_gpio_direction_in, \
		.function = _func, \
		.priv = &(struct stm_pio_control_pad_config) { \
			.retime = _retiming, \
		}, \
	}

#define DATA_OUT(_port, _pin, _func, _retiming) \
	{ \
		.gpio = stm_gpio(_port, _pin), \
		.direction = stm_pad_gpio_direction_out, \
		.function = _func, \
		.priv = &(struct stm_pio_control_pad_config) { \
			.retime = _retiming, \
		}, \
	}

#define CLOCK_IN(_port, _pin, _func, _retiming) \
	{ \
		.gpio = stm_gpio(_port, _pin), \
		.direction = stm_pad_gpio_direction_in, \
		.function = _func, \
		.priv = &(struct stm_pio_control_pad_config) { \
			.retime = _retiming, \
		}, \
	}

#define CLOCK_OUT(_port, _pin, _func, _retiming) \
	{ \
		.gpio = stm_gpio(_port, _pin), \
		.direction = stm_pad_gpio_direction_out, \
		.function = _func, \
		.priv = &(struct stm_pio_control_pad_config) { \
			.retime = _retiming, \
		}, \
	}

#define PHY_CLOCK(_port, _pin, _func, _retiming) \
	{ \
		.gpio = stm_gpio(_port, _pin), \
		.direction = stm_pad_gpio_direction_unknown, \
		.function = _func, \
		.name = "PHYCLK", \
		.priv = &(struct stm_pio_control_pad_config) { \
		.retime = _retiming, \
		}, \
	}

#define MDIO(_port, _pin, _func, _retiming) \
	{ \
		.gpio = stm_gpio(_port, _pin), \
		.direction = stm_pad_gpio_direction_bidir_pull_up, \
		.function = _func, \
		.name = "MDIO", \
		.priv = &(struct stm_pio_control_pad_config) { \
			.retime = _retiming, \
		}, \
	}

#define MDC(_port, _pin, _func, _retiming) \
	{ \
		.gpio = stm_gpio(_port, _pin), \
		.direction = stm_pad_gpio_direction_out, \
		.function = _func, \
		.name = "MDC", \
		.priv = &(struct stm_pio_control_pad_config) { \
			.retime = _retiming, \
		}, \
	}


#define MAX_GMACS	2

static struct sysconf_field *gbit_sc[MAX_GMACS];
static char *gmac_clk_n[MAX_GMACS] = { "CLKS_GMAC0_PHY", "CLKS_ETH1_PHY" };
static char *phy_clk_parent_100[MAX_GMACS] = { "CLKS_A1_PLL1", "CLKS_A0_PLL1" };
static char *phy_clk_parent_10[MAX_GMACS] = { "CLKS_A1_REF", "CLKS_A0_REF" };

int stih415_gmac0_claim(struct stm_pad_state *state, void *priv)
{
	gbit_sc[0] = sysconf_claim(SYSCONF(382), 6, 8, "gmac-0");

	if (!gbit_sc[0])
		return -1;

	sysconf_write(gbit_sc[0], 0);
	return 0;
}

void stih415_gmac0_release(struct stm_pad_state *state, void *priv)
{
	sysconf_release(gbit_sc[0]);
}

int stih415_gmac1_claim(struct stm_pad_state *state, void *priv)
{

	gbit_sc[1] = sysconf_claim(SYSCONF(29), 6, 8, "gmac-1");

	if (!gbit_sc[1])
		return -1;
	sysconf_write(gbit_sc[1], 0);
	return 0;
}

void stih415_gmac1_release(struct stm_pad_state *state, void *priv)
{
	sysconf_release(gbit_sc[1]);
}


static struct stm_pad_config stih415_ethernet_mii_pad_configs[] = {
	[0] =  {
		.gpios_num = 20,
		.gpios = (struct stm_pad_gpio []) {
			PHY_CLOCK(13, 5, 2, RET_NICLK(0, 1)),/* PHYCLK */
			DATA_IN(13, 6, 2, RET_BYPASS(0)),/* MDINT */
			DATA_OUT(13, 7, 2, RET_SE_NICLK_IO(0, 0)),/* TXEN */

			DATA_OUT(14, 0, 2, RET_SE_NICLK_IO(0, 0)),/* TXD[0] */
			DATA_OUT(14, 1, 2, RET_SE_NICLK_IO(0, 0)),/* TXD[1] */
			DATA_OUT(14, 2, 2, RET_SE_NICLK_IO(0, 1)),/* TXD[2] */
			DATA_OUT(14, 3, 2, RET_SE_NICLK_IO(0, 1)),/* TXD[3] */

			CLOCK_IN(15, 0, 2, RET_NICLK(0, 0)),/* TXCLK */
			DATA_OUT(15, 1, 2, RET_SE_NICLK_IO(0, 0)),/* TXER */
			DATA_IN(15, 2, 2, RET_BYPASS(1000)),/* CRS */
			DATA_IN(15, 3, 2, RET_BYPASS(1000)),/* COL */

			MDIO(15, 4, 2, RET_BYPASS(3000)),/* MDIO*/
			MDC(15, 5, 2, RET_NICLK(0, 1)),/* MDC */
			DATA_IN(16, 0, 2, RET_SE_NICLK_IO(0, 0)),/* 5 RXD[0] */
			DATA_IN(16, 1, 2, RET_SE_NICLK_IO(0, 0)),/* RXD[1] */
			DATA_IN(16, 2, 2, RET_SE_NICLK_IO(0, 0)),/* RXD[2] */
			DATA_IN(16, 3, 2, RET_SE_NICLK_IO(0, 0)),/* RXD[3] */
			DATA_IN(15, 6, 2, RET_SE_NICLK_IO(0, 0)),/* RXDV */
			DATA_IN(15, 7, 2, RET_SE_NICLK_IO(0, 0)),/* RX_ER */

			CLOCK_IN(17, 0, 2, RET_NICLK(0, 0)),/* RXCLK */
		},
		.sysconfs_num = 5,
		.sysconfs = (struct stm_pad_sysconf []) {
			/* ETH0_GMAC_EN */
			STM_PAD_SYSCONF(SYSCONF(166), 0, 0, 1),
			/* MIIx_PHY_SEL */
			STM_PAD_SYSCONF(SYSCONF(382), 2, 4, 0),
			/* ENMIIx */
			STM_PAD_SYSCONF(SYSCONF(382), 5, 5, 1),
			/* TXCLK_NOT_CLK125 */
			STM_PAD_SYSCONF(SYSCONF(382), 6, 6, 1),
			/* TX_RETIMING_CLK */
			STM_PAD_SYSCONF(SYSCONF(382), 8, 8, 0),
		},
	},
	[1] =  {
		.gpios_num = 20,
		.gpios = (struct stm_pad_gpio []) {
			DATA_OUT(0, 0, 1, RET_SE_NICLK_IO(0, 0)),/* TXD[0] */
			DATA_OUT(0, 1, 1, RET_SE_NICLK_IO(0, 0)),/* TXD[1] */
			DATA_OUT(0, 2, 1, RET_SE_NICLK_IO(0, 0)),/* TXD[2] */
			DATA_OUT(0, 3, 1, RET_SE_NICLK_IO(0, 0)),/* TXD[3] */
			DATA_OUT(0, 4, 1, RET_SE_NICLK_IO(0, 0)),/* TXER */
			DATA_OUT(0, 5, 1, RET_SE_NICLK_IO(0, 0)),/* TXEN */
			CLOCK_IN(0, 6, 1, RET_NICLK(0, 0)),/* TXCLK */
			DATA_IN(0, 7, 1, RET_BYPASS(1000)),/* COL */

			MDIO(1, 0, 1, RET_BYPASS(0)),/* MDIO */
			MDC(1, 1, 1, RET_NICLK(0, 0)),/* MDC */
			DATA_IN(1, 2, 1, RET_BYPASS(1000)),/* CRS */
			DATA_IN(1, 3, 1, RET_BYPASS(0)),/* MDINT */
			DATA_IN(1, 4, 1, RET_SE_NICLK_IO(0, 0)),/* RXD[0] */
			DATA_IN(1, 5, 1, RET_SE_NICLK_IO(0, 0)),/* RXD[1] */
			DATA_IN(1, 6, 1, RET_SE_NICLK_IO(0, 0)),/* RXD[2] */
			DATA_IN(1, 7, 1, RET_SE_NICLK_IO(0, 0)),/* RXD[3] */

			DATA_IN(2, 0, 1, RET_SE_NICLK_IO(0, 0)),/* RXDV */
			DATA_IN(2, 1, 1, RET_SE_NICLK_IO(0, 0)),/* RX_ER */
			CLOCK_IN(2, 2, 1, RET_NICLK(0, 0)),/* RXCLK */
			PHY_CLOCK(2, 3, 1, RET_NICLK(0, 1)),/* PHYCLK */

		},
		.sysconfs_num = 5,
		.sysconfs = (struct stm_pad_sysconf []) {
			/* EN_GMAC1 */
			STM_PAD_SYSCONF(SYSCONF(31), 0, 0, 1),
			/* MIIx_PHY_SEL */
			STM_PAD_SYSCONF(SYSCONF(29), 2, 4, 0),
			/* ENMIIx */
			STM_PAD_SYSCONF(SYSCONF(29), 5, 5, 1),
			/* TXCLK_NOT_CLK125 */
			STM_PAD_SYSCONF(SYSCONF(29), 6, 6, 1),
			/* TX_RETIMING_CLK */
			STM_PAD_SYSCONF(SYSCONF(29), 8, 8, 0),
		},
	},
};

static struct stm_pad_config stih415_ethernet_gmii_pad_configs[] = {
	[0] =  {
		.gpios_num = 29,
		.gpios = (struct stm_pad_gpio []) {
			PHY_CLOCK(13, 5, 4, RET_NICLK(0, 1)),/* GTXCLK */
			DATA_IN(13, 6, 2, RET_BYPASS(0)),/* MDINT */
			DATA_OUT(13, 7, 2, RET_SE_NICLK_IO(3000, 0)),/* TXEN */

			DATA_OUT(14, 0, 2, RET_SE_NICLK_IO(3000, 0)),/* TXD[0]*/
			DATA_OUT(14, 1, 2, RET_SE_NICLK_IO(3000, 0)),/* TXD[1]*/
			DATA_OUT(14, 2, 2, RET_SE_NICLK_IO(3000, 1)),/* TXD[2]*/
			DATA_OUT(14, 3, 2, RET_SE_NICLK_IO(3000, 1)),/* TXD[3]*/
			DATA_OUT(14, 4, 2, RET_SE_NICLK_IO(3000, 1)),/* TXD[4]*/
			DATA_OUT(14, 5, 2, RET_SE_NICLK_IO(3000, 1)),/* TXD[5]*/
			DATA_OUT(14, 6, 2, RET_SE_NICLK_IO(3000, 1)),/* TXD[6]*/
			DATA_OUT(14, 7, 2, RET_SE_NICLK_IO(3000, 1)),/* TXD[7]*/

			CLOCK_IN(15, 0, 2, RET_NICLK(0, 0)),/* TXCLK */
			DATA_OUT(15, 1, 2, RET_SE_NICLK_IO(3000, 0)),/* TXER */
			DATA_IN(15, 2, 2, RET_BYPASS(1000)),/* CRS */
			DATA_IN(15, 3, 2, RET_BYPASS(1000)),/* COL */
			MDIO(15, 4, 2, RET_BYPASS(3000)),/* MDIO */
			MDC(15, 5, 2, RET_NICLK(0, 1)),/* MDC */
			DATA_IN(15, 6, 2, RET_SE_NICLK_IO(1500, 0)),/* RXDV */
			DATA_IN(15, 7, 2, RET_SE_NICLK_IO(1500, 0)),/* RX_ER */

			DATA_IN(16, 0, 2, RET_SE_NICLK_IO(1500, 0)),/* RXD[0] */
			DATA_IN(16, 1, 2, RET_SE_NICLK_IO(1500, 0)),/* RXD[1] */
			DATA_IN(16, 2, 2, RET_SE_NICLK_IO(1500, 0)),/* RXD[2] */
			DATA_IN(16, 3, 2, RET_SE_NICLK_IO(1500, 0)),/* RXD[3] */
			DATA_IN(16, 4, 2, RET_SE_NICLK_IO(1500, 0)),/* RXD[4] */
			DATA_IN(16, 5, 2, RET_SE_NICLK_IO(1500, 0)),/* RXD[5] */
			DATA_IN(16, 6, 2, RET_SE_NICLK_IO(1500, 0)),/* RXD[6] */
			DATA_IN(16, 7, 2, RET_SE_NICLK_IO(1500, 0)),/* RXD[7] */

			CLOCK_IN(17, 0, 2, RET_NICLK(0, 0)),/* RXCLK */
			CLOCK_IN(17, 6, 1, RET_NICLK(0, 0)),/* CLK_125 */
		},
		.sysconfs_num = 3,
		.sysconfs = (struct stm_pad_sysconf []) {
			/* EN_GMAC0 */
			STM_PAD_SYSCONF(SYSCONF(166), 0, 0, 1),
			/* MIIx_PHY_SEL */
			STM_PAD_SYSCONF(SYSCONF(382), 2, 4, 0),
			/* ENMIIx */
			STM_PAD_SYSCONF(SYSCONF(382), 5, 5, 1),
		},
		.custom_claim	= stih415_gmac0_claim,
		.custom_release	= stih415_gmac0_release,
	},
	[1] =  {
		.gpios_num = 29,
		.gpios = (struct stm_pad_gpio []) {
			DATA_OUT(0, 0, 1, RET_SE_NICLK_IO(3000, 0)),/* TXD[0] */
			DATA_OUT(0, 1, 1, RET_SE_NICLK_IO(3000, 0)),/* TXD[1] */
			DATA_OUT(0, 2, 1, RET_SE_NICLK_IO(3000, 0)),/* TXD[2] */
			DATA_OUT(0, 3, 1, RET_SE_NICLK_IO(3000, 0)),/* TXD[3] */
			DATA_OUT(0, 4, 1, RET_SE_NICLK_IO(3000, 0)),/* TXER */
			DATA_OUT(0, 5, 1, RET_SE_NICLK_IO(3000, 0)),/* TXEN */
			CLOCK_IN(0, 6, 1, RET_NICLK(0, 0)),/* TXCLK */
			DATA_IN(0, 7, 1, RET_BYPASS(1000)),/* COL */

			MDIO(1, 0, 1, RET_BYPASS(3000)),/* MDIO */
			MDC(1, 1, 1, RET_NICLK(0, 0)),/* MDC */

			DATA_IN(1, 2, 1, RET_BYPASS(1000)),/* CRS */
			DATA_IN(1, 3, 1, RET_BYPASS(0)),/* MDINT */
			DATA_IN(1, 4, 1, RET_SE_NICLK_IO(1500, 0)),/* RXD[0] */
			DATA_IN(1, 5, 1, RET_SE_NICLK_IO(1500, 0)),/* RXD[1] */
			DATA_IN(1, 6, 1, RET_SE_NICLK_IO(1500, 0)),/* RXD[2] */
			DATA_IN(1, 7, 1, RET_SE_NICLK_IO(1500, 0)),/* RXD[3] */

			DATA_IN(2, 0, 1, RET_SE_NICLK_IO(1500, 0)),/* RXDV */
			DATA_IN(2, 1, 1, RET_SE_NICLK_IO(1500, 0)),/* RX_ER */
			CLOCK_IN(2, 2, 1, RET_NICLK(0, 0)),/* RXCLK */

			PHY_CLOCK(2, 3, 4, RET_NICLK(0, 1)), /* GTXCLK */

			DATA_OUT(2, 6, 4, RET_SE_NICLK_IO(3000, 0)),/* TXD[4] */
			DATA_OUT(2, 7, 4, RET_SE_NICLK_IO(3000, 0)),/* TXD[5] */

			DATA_IN(3, 0, 4, RET_SE_NICLK_IO(1500, 0)),/* RXD[4] */
			DATA_IN(3, 1, 4, RET_SE_NICLK_IO(1500, 0)),/* RXD[5] */
			DATA_IN(3, 2, 4, RET_SE_NICLK_IO(1500, 0)),/* RXD[6] */
			DATA_IN(3, 3, 4, RET_SE_NICLK_IO(1500, 0)),/* RXD[7] */

			CLOCK_IN(3, 7, 4, RET_NICLK(0, 0)),/* CLK_125 */

			DATA_OUT(4, 1, 4, RET_SE_NICLK_IO(3000, 0)),/* TXD[6] */
			DATA_OUT(4, 2, 4, RET_SE_NICLK_IO(3000, 0)),/* TXD[7] */
		},
		.sysconfs_num = 3,
		.sysconfs = (struct stm_pad_sysconf []) {
			/* EN_GMAC1 */
			STM_PAD_SYSCONF(SYSCONF(31), 0, 0, 1),
			/* MIIx_PHY_SEL */
			STM_PAD_SYSCONF(SYSCONF(29), 2, 4, 0),
			/* ENMIIx */
			STM_PAD_SYSCONF(SYSCONF(29), 5, 5, 1),
		},
		.custom_claim	= stih415_gmac1_claim,
		.custom_release	= stih415_gmac1_release,
	},
};

static struct stm_pad_config stih415_ethernet_rgmii_pad_configs[] = {
	[0] =  {
		.gpios_num = 16,
		.gpios = (struct stm_pad_gpio []) {
			PHY_CLOCK(13, 5, 4, RET_NICLK(0, 1)),/* GTXCLK */

			DATA_OUT(13, 7, 2, RET_DE_IO(0, 0)),/* TXEN */

			DATA_OUT(14, 0, 2, RET_DE_IO(1000, 0)),/* TXD[0] */
			DATA_OUT(14, 1, 2, RET_DE_IO(1000, 0)),/* TXD[1] */
			DATA_OUT(14, 2, 2, RET_DE_IO(1000, 1)),/* TXD[2] */
			DATA_OUT(14, 3, 2, RET_DE_IO(1000, 1)),/* TXD[3] */

			/* TX Clock inversion is not set for 1000Mbps */
			CLOCK_IN(15, 0, 2, RET_NICLK(0, 0)),/* TXCLK */
			MDIO(15, 4, 2, RET_BYPASS(0)),/* MDIO */
			MDC(15, 5, 2, RET_NICLK(0, 1)),/* MDC */
			DATA_IN(15, 6, 2, RET_DE_IO(500, 0)),/* RXDV */

			DATA_IN(16, 0, 2, RET_DE_IO(500, 0)),/* RXD[0] */
			DATA_IN(16, 1, 2, RET_DE_IO(500, 0)),/* RXD[1] */
			DATA_IN(16, 2, 2, RET_DE_IO(500, 0)),/* RXD[2] */
			DATA_IN(16, 3, 2, RET_DE_IO(500, 0)),/* RXD[3] */

			CLOCK_IN(17, 0, 2, RET_NICLK(0, 0)),/* RXCLK */
			CLOCK_IN(17, 6, 1, RET_NICLK(0, 0)),/* CLK_125 */
		},
		.sysconfs_num = 3,
		.sysconfs = (struct stm_pad_sysconf []) {
			/* EN_GMAC0 */
			STM_PAD_SYSCONF(SYSCONF(166), 0, 0, 1),
			/* MIIx_PHY_SEL */
			STM_PAD_SYSCONF(SYSCONF(382), 2, 4, 1),
			/* ENMIIx */
			STM_PAD_SYSCONF(SYSCONF(382), 5, 5, 1),
			/* Extra system config based on speed */

		},
		.custom_claim	= stih415_gmac0_claim,
		.custom_release = stih415_gmac0_release,
	},
	[1] =  {
		.gpios_num = 16,
		.gpios = (struct stm_pad_gpio []) {
			DATA_OUT(0, 0, 1, RET_DE_IO(1000, 0)),/* TXD[0] */
			DATA_OUT(0, 1, 1, RET_DE_IO(1000, 0)),/* TXD[1] */
			DATA_OUT(0, 2, 1, RET_DE_IO(1000, 0)),/* TXD[2] */
			DATA_OUT(0, 3, 1, RET_DE_IO(1000, 0)),/* TXD[3] */
			DATA_OUT(0, 5, 1, RET_DE_IO(0, 0)),/* TXEN */
			CLOCK_IN(0, 6, 1, RET_NICLK(0, 0)),/* TXCLK */

			MDIO(1, 0, 1, RET_BYPASS(0)),/* MDIO */
			MDC(1, 1, 1, RET_NICLK(0, 0)),/* MDC */
			DATA_IN(1, 4, 1, RET_DE_IO(0, 0)),/* RXD[0] */
			DATA_IN(1, 5, 1, RET_DE_IO(0, 0)),/* RXD[1] */
			DATA_IN(1, 6, 1, RET_DE_IO(0, 0)),/* RXD[2] */
			DATA_IN(1, 7, 1, RET_DE_IO(0, 0)),/* RXD[3] */

			DATA_IN(2, 0, 1, RET_DE_IO(500, 0)),/* RXDV */
			CLOCK_IN(2, 2, 1, RET_NICLK(0, 0)),/* RXCLK */
			PHY_CLOCK(2, 3, 4, RET_NICLK(0, 1)), /* GTXCLK */

			CLOCK_IN(3, 7, 4, RET_NICLK(0, 0)),/* CLK_125 */
		},
		.sysconfs_num = 3,
		.sysconfs = (struct stm_pad_sysconf []) {
			/* EN_GMAC1 */
			STM_PAD_SYSCONF(SYSCONF(31), 0, 0, 1),
			/* MIIx_PHY_SEL */
			STM_PAD_SYSCONF(SYSCONF(29), 2, 4, 1),
			/* ENMIIx */
			STM_PAD_SYSCONF(SYSCONF(29), 5, 5, 1),
			/* Extra retime config base on speed */
		},
		.custom_claim	= stih415_gmac1_claim,
		.custom_release = stih415_gmac1_release,
	},
};
static struct stm_pad_config stih415_ethernet_rmii_pad_configs[] = {
	[0] = {
		.gpios_num = 11,
		.gpios = (struct stm_pad_gpio []) {
			PHY_CLOCK(13, 5, 2, RET_NICLK(0, 1)),/* PHYCLK */
			DATA_IN(13, 6, 2, RET_BYPASS(0)),/* MDINT */
			DATA_OUT(13, 7, 2, RET_SE_NICLK_IO(0, 0)),/* TXEN */
			DATA_OUT(14, 0, 2, RET_SE_NICLK_IO(0, 0)),/* TXD[0] */
			DATA_OUT(14, 1, 2, RET_SE_NICLK_IO(0, 0)),/* TXD[1] */
			MDIO(15, 4, 2, RET_BYPASS(3000)),/* MDIO */
			MDC(15, 5, 2, RET_NICLK(0, 1)),/* MDC */
			/* NOTE:
			 * retime settings for RX pins is
			 * incorrect in App note */
			DATA_IN(15, 6, 2, RET_SE_NICLK_IO(0, 1)),/* RXDV */
			DATA_IN(15, 7, 2, RET_SE_NICLK_IO(0, 0)),/* RX_ER */
			DATA_IN(16, 0, 2, RET_SE_NICLK_IO(0, 1)),/* RXD.0 */
			DATA_IN(16, 1, 2, RET_SE_NICLK_IO(0, 1)),/* RXD.1 */
		},
		.sysconfs_num = 4,
		.sysconfs = (struct stm_pad_sysconf []) {
			/* EN_GMAC0 */
			STM_PAD_SYSCONF(SYSCONF(166), 0, 0, 1),
			/* MIIx_PHY_SEL */
			STM_PAD_SYSCONF(SYSCONF(382), 2, 4, 4),
			/* ENMIIx */
			STM_PAD_SYSCONF(SYSCONF(382), 5, 5, 1),
			/* TX_RETIMING_CLK */
			STM_PAD_SYSCONF(SYSCONF(382), 8, 8, 1),
		},
	},
	[1] =  {
		.gpios_num = 11,
		.gpios = (struct stm_pad_gpio []) {
			DATA_OUT(0, 0, 1, RET_SE_NICLK_IO(0, 0)),/* TXD[0] */
			DATA_OUT(0, 1, 1, RET_SE_NICLK_IO(0, 0)),/* TXD[1] */
			DATA_OUT(0, 5, 1, RET_SE_NICLK_IO(0, 0)),/* TXEN */
			MDIO(1, 0, 1, RET_BYPASS(3000)),/* MDIO */
			MDC(1, 1, 1, RET_NICLK(0, 0)),/* MDC */
			DATA_IN(1, 3, 1, RET_BYPASS(0)),/* MDINT */
			DATA_IN(1, 4, 1, RET_SE_NICLK_IO(1000, 1)),/* RXD[0] */
			DATA_IN(1, 5, 1, RET_SE_NICLK_IO(1000, 1)),/* RXD[1] */
			DATA_IN(2, 0, 1, RET_SE_NICLK_IO(1000, 1)),/* RXDV */
			DATA_IN(2, 1, 1, RET_SE_NICLK_IO(1000, 0)),/* RX_ER */
			PHY_CLOCK(2, 3, 1, RET_NICLK(0, 1)),/* PHYCLK */
		},
		.sysconfs_num = 4,
		.sysconfs = (struct stm_pad_sysconf []) {
			/* EN_GMAC1 */
			STM_PAD_SYSCONF(SYSCONF(31), 0, 0, 1),
			/* MIIx_PHY_SEL */
			STM_PAD_SYSCONF(SYSCONF(29), 2, 4, 4),
			/* ENMIIx */
			STM_PAD_SYSCONF(SYSCONF(29), 5, 5, 1),
			/* TX_RETIMING_CLK */
			STM_PAD_SYSCONF(SYSCONF(29), 8, 8, 1),
		},
	},
};

static struct stm_pad_config stih415_ethernet_reverse_mii_pad_configs[] = {
	[0] = {
		.gpios_num = 20,
		.gpios = (struct stm_pad_gpio []) {
			DATA_OUT(14, 0, 2, RET_BYPASS(0)),/* TXD[0] */
			DATA_OUT(14, 1, 2, RET_BYPASS(0)),/* TXD[1] */
			DATA_OUT(14, 2, 2, RET_BYPASS(0)),/* TXD[2] */
			DATA_OUT(14, 3, 2, RET_BYPASS(0)),/* TXD[3] */
			DATA_OUT(15, 1, 2, RET_SE_NICLK_IO(0, 0)),/* TXER */
			DATA_OUT(13, 7, 2, RET_SE_NICLK_IO(0, 0)),/* TXEN */
			CLOCK_IN(15, 0, 2, RET_NICLK(0, 0)),/* TXCLK */

			DATA_OUT(15, 3, 3, RET_BYPASS(0)),/* COL */
			MDIO(15, 4, 2, RET_BYPASS(2000)),/* MDIO*/
			MDC(15, 5, 3, RET_NICLK(0, 0)),/* MDC */
			DATA_OUT(15, 2, 3, RET_BYPASS(0)),/* CRS */

			DATA_IN(13, 6, 2, RET_BYPASS(0)),/* MDINT */
			DATA_IN(16, 0, 2, RET_SE_NICLK_IO(1000, 0)),/* RXD[0] */
			DATA_IN(16, 1, 2, RET_SE_NICLK_IO(1000, 0)),/* RXD[1] */
			DATA_IN(16, 2, 2, RET_SE_NICLK_IO(1000, 0)),/* RXD[2] */
			DATA_IN(16, 3, 2, RET_SE_NICLK_IO(1000, 0)),/* RXD[3] */
			DATA_IN(15, 6, 2, RET_SE_NICLK_IO(1000, 0)),/* RXDV */
			DATA_IN(15, 7, 2, RET_SE_NICLK_IO(1000, 0)),/* RX_ER */
			CLOCK_IN(17, 0, 2, RET_NICLK(0, 0)),/* RXCLK */
			PHY_CLOCK(13, 5, 2, RET_NICLK(0, 0)),/* PHYCLK */
		},
		.sysconfs_num = 5,
		.sysconfs = (struct stm_pad_sysconf []) {
			/* EN_GMAC0 */
			STM_PAD_SYSCONF(SYSCONF(166), 0, 0, 1),
			/* MIIx_PHY_SEL */
			STM_PAD_SYSCONF(SYSCONF(382), 2, 4, 0),
			/* ENMIIx */
			STM_PAD_SYSCONF(SYSCONF(382), 5, 5, 0),
			/* TXCLK_NOT_CLK125 */
			STM_PAD_SYSCONF(SYSCONF(382), 6, 6, 1),
			/* TX_RETIMING_CLK */
			STM_PAD_SYSCONF(SYSCONF(382), 8, 8, 0),
		},
	},
	[1] =  {
		.gpios_num = 20,
		.gpios = (struct stm_pad_gpio []) {
			DATA_OUT(0, 0, 1, RET_SE_NICLK_IO(0, 1)),/* TXD[0] */
			DATA_OUT(0, 1, 1, RET_SE_NICLK_IO(0, 1)),/* TXD[1] */
			DATA_OUT(0, 2, 1, RET_SE_NICLK_IO(0, 1)),/* TXD[2] */
			DATA_OUT(0, 3, 1, RET_SE_NICLK_IO(0, 1)),/* TXD[3] */
			DATA_OUT(0, 4, 1, RET_SE_NICLK_IO(0, 1)),/* TXER */
			DATA_OUT(0, 5, 1, RET_SE_NICLK_IO(0, 1)),/* TXEN */
			CLOCK_IN(0, 6, 1, RET_NICLK(0, 0)),/* TXCLK */
			DATA_OUT(0, 7, 2, RET_BYPASS(0)),/* COL */


			DATA_IN(2, 0, 1, RET_SE_NICLK_IO(1000, 1)),/* RXDV */
			DATA_IN(2, 1, 1, RET_SE_NICLK_IO(1000, 1)),/* RX_ER */
			CLOCK_IN(2, 2, 1, RET_NICLK(0, 0)),/* RXCLK */
			PHY_CLOCK(2, 3, 1, RET_NICLK(0, 0)),/* PHYCLK */

			MDIO(1, 0, 1, RET_BYPASS(2000)),/* MDIO */
			MDC(1, 1, 2, RET_NICLK(0, 1)),/* MDC */
			DATA_OUT(1, 2, 2, RET_BYPASS(0)),/* CRS */
			DATA_IN(1, 3, 1, RET_BYPASS(0)),/* MDINT */
			DATA_IN(1, 4, 1, RET_SE_NICLK_IO(1000, 1)),/* RXD[0] */
			DATA_IN(1, 5, 1, RET_SE_NICLK_IO(1000, 1)),/* RXD[1] */
			DATA_IN(1, 6, 1, RET_SE_NICLK_IO(1000, 1)),/* RXD[2] */
			DATA_IN(1, 7, 1, RET_SE_NICLK_IO(1000, 1)),/* RXD[3] */
		},
		.sysconfs_num = 5,
		.sysconfs = (struct stm_pad_sysconf []) {
			/* EN_GMAC1 */
			STM_PAD_SYSCONF(SYSCONF(31), 0, 0, 1),
			/* MIIx_PHY_SEL */
			STM_PAD_SYSCONF(SYSCONF(29), 2, 4, 0),
			/* ENMIIx */
			STM_PAD_SYSCONF(SYSCONF(29), 5, 5, 1),
			/* TXCLK_NOT_CLK125 */
			STM_PAD_SYSCONF(SYSCONF(29), 6, 6, 1),
			/* TX_RETIMING_CLK */
			STM_PAD_SYSCONF(SYSCONF(29), 8, 8, 0),
		},
	},
};


static void stih415_ethernet_gtx_speed(void *priv, unsigned int speed)
{
	void (*txclk_select)(int txclk_250_not_25_mhz) = priv;
	if (txclk_select)
		txclk_select(speed == SPEED_1000);
}
static void stih415_ethernet_gmii_speed(int port, void *priv,
						unsigned int speed)
{
	/* TX Clock inversion is not set for 1000Mbps */
	if (speed == SPEED_1000)
		sysconf_write(gbit_sc[port], 0);
	else
		sysconf_write(gbit_sc[port], 1);
	stih415_ethernet_gtx_speed(priv, speed);
}

static void stih415_ethernet_gmii0_speed(void *priv, unsigned int speed)
{
	stih415_ethernet_gmii_speed(0, priv, speed);
}
static void stih415_ethernet_gmii1_speed(void *priv, unsigned int speed)
{
	stih415_ethernet_gmii_speed(1, priv, speed);
}


static void stih415_ethernet_rgmii_speed(int port, void *priv,
					 unsigned int speed)
{
	/* TX Clock inversion is not set for 1000Mbps */
	if (speed == SPEED_1000) {
		/* output clock driver by MII_TXCLK
		 * 125Mhz Clock from PHY is used for retiming
		 * and also to drive GTXCLK
		 * */
		sysconf_write(gbit_sc[port], 0);
	} else {
		int ret;
		static struct clk *phy_clk, *clk_parent;
		unsigned long phy_clk_rate;
		/* output clock driver by Clockgen
		 * 125MHz clock provided by PHY is not suitable for retiming.
		 * So TXPIO retiming must therefore be clocked by an
		 * internal 2.5/25Mhz clock generated by Clockgen.
		 * */
		phy_clk = clk_get(NULL, gmac_clk_n[port]);
		sysconf_write(gbit_sc[port], 6);
		if (speed  == SPEED_100) {
			clk_parent = clk_get(NULL, phy_clk_parent_100[port]);
			phy_clk_rate = 25000000;
		} else {
			/*
			 * We have to route the phy clk to a parent
			 * (e.g. 30MHz quartz) to get 2.5MHz for RGMII.
			 */
			clk_parent = clk_get(NULL, phy_clk_parent_10[port]);
			phy_clk_rate = 2500000;
		}
		ret = clk_set_parent(phy_clk, clk_parent);
		if (ret)
			pr_err("%s: error setting the parent clk\n", __func__);

		clk_set_rate(phy_clk, phy_clk_rate);

		pr_debug("%s: %d Speed - phy clk %lu\n", __func__,
			 speed, clk_get_rate(phy_clk));
	}
	stih415_ethernet_gtx_speed(priv, speed);
}
static void stih415_ethernet_rgmii0_gtx_speed(void *priv, unsigned int speed)
{
	stih415_ethernet_rgmii_speed(0, priv, speed);
}

static void stih415_ethernet_rgmii1_gtx_speed(void *priv, unsigned int speed)
{
	stih415_ethernet_rgmii_speed(1, priv, speed);
}

static struct stmmac_dma_cfg gmac_dma_setting = {
        .pbl = 32,
	.mixed_burst = 1,
};

/* STBus Convertor config */
static struct stm_amba_bridge_config stih415_amba_stmmac_config = {
	.type =	stm_amba_type2,
	.chunks_in_msg = 1,
	.packets_in_chunk = 2,
	.write_posting = stm_amba_write_posting_enabled,
	.max_opcode = stm_amba_opc_LD64_ST64,
	.type2.threshold = 512,
	.type2.sd_config_missing = 1,
	.type2.trigger_mode = stm_amba_stbus_threshold_based,
	.type2.read_ahead = stm_amba_read_ahead_enabled,
};

#define GMAC_AHB2STBUS_BASE	 (0x2000 - 4)
static void *stih415_ethernet_bus_setup(void __iomem *ioaddr,
					struct device *dev, void *data)
{
	struct stm_amba_bridge *amba;

	if (!data) {
		amba = stm_amba_bridge_create(dev, ioaddr + GMAC_AHB2STBUS_BASE,
					      &stih415_amba_stmmac_config);
		if (IS_ERR(amba)) {
			dev_err(dev, " Unable to create amba plug\n");
			return NULL;
		}
	} else
		amba = (struct stm_amba_bridge *) data;

	stm_amba_bridge_init(amba);

	return (void *) amba;
}

static struct plat_stmmacenet_data stih415_ethernet_platform_data[] = {
	{
		.dma_cfg = &gmac_dma_setting,
		.has_gmac = 1,
		.enh_desc = 1,
		.force_sf_dma_mode = 1,
		.bugged_jumbo = 1,
		.pmt = 1,
		.init = &stmmac_claim_resource,
		.exit = &stmmac_release_resource,
		.bus_setup = &stih415_ethernet_bus_setup,
	}, {
		.dma_cfg = &gmac_dma_setting,
		.has_gmac = 1,
		.enh_desc = 1,
		.force_sf_dma_mode = 1,
		.bugged_jumbo = 1,
		.pmt = 1,
		.init = &stmmac_claim_resource,
		.exit = &stmmac_release_resource,
		.bus_setup = &stih415_ethernet_bus_setup,
	}
};

static u64 stih415_eth_dma_mask = DMA_BIT_MASK(32);
static struct platform_device stih415_ethernet_devices[] = {
	{
		.name = "stmmaceth",
		.id = 0,
		.num_resources = 2,
		.resource = (struct resource[]) {
			STM_PLAT_RESOURCE_MEM(0xFE810000, 0x8000),
			STIH415_RESOURCE_IRQ_NAMED("macirq", 147),
		},
		.dev = {
			.dma_mask = &stih415_eth_dma_mask,
			.coherent_dma_mask = DMA_BIT_MASK(32),
			.platform_data = &stih415_ethernet_platform_data[0],
		},
	}, {
		.name = "stmmaceth",
		.id = 1,
		.num_resources = 2,
		.resource = (struct resource[]) {
			STM_PLAT_RESOURCE_MEM(0xFEF08000, 0x8000),
			STIH415_RESOURCE_IRQ_NAMED("macirq", 150),
		},

		.dev = {
			.dma_mask = &stih415_eth_dma_mask,
			.coherent_dma_mask = DMA_BIT_MASK(32),
			.platform_data = &stih415_ethernet_platform_data[1],
		},
	}
};


/* ONLY MII mode on GMAC1 is tested */
void __init stih415_configure_ethernet(int port,
		struct stih415_ethernet_config *config)
{
	static int configured[ARRAY_SIZE(stih415_ethernet_devices)];
	struct stih415_ethernet_config default_config;
	struct plat_stmmacenet_data *plat_data;
	struct stm_pad_config *pad_config;

	BUG_ON(port < 0 || port >= ARRAY_SIZE(stih415_ethernet_devices));
	BUG_ON(configured[port]++);

	if (!config)
		config = &default_config;

	plat_data = &stih415_ethernet_platform_data[port];

	switch (config->interface) {
	case PHY_INTERFACE_MODE_MII:
		pad_config = &stih415_ethernet_mii_pad_configs[port];
		if (config->ext_clk)
			stm_pad_set_pio_ignored(pad_config, "PHYCLK");
		else
			stm_pad_set_pio_out(pad_config, "PHYCLK", 1 + port);
		break;
	case PHY_INTERFACE_MODE_GMII:
		pad_config = &stih415_ethernet_gmii_pad_configs[port];
		stm_pad_set_pio_out(pad_config, "PHYCLK", 4);
		if (port == 0)
			plat_data->fix_mac_speed =
					stih415_ethernet_gmii0_speed;
		else
			plat_data->fix_mac_speed =
					stih415_ethernet_gmii1_speed;

		plat_data->bsp_priv = config->txclk_select;
		break;
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
		/* This mode is similar to GMII (GTX) except the data
		 * buses are reduced down to 4 bits and the 2 error
		 * signals are removed. The data rate is maintained by
		 * using both edges of the clock. This also explains
		 * the different retiming configuration for this mode.
		 */
		pad_config = &stih415_ethernet_rgmii_pad_configs[port];
		stm_pad_set_pio_out(pad_config, "PHYCLK", 4);

		if (port == 0)
			plat_data->fix_mac_speed =
				stih415_ethernet_rgmii0_gtx_speed;
		else
			plat_data->fix_mac_speed =
				stih415_ethernet_rgmii1_gtx_speed;

		/* Configure so that stmmac gets clock */
		plat_data->bsp_priv = config->txclk_select;
		break;
	case PHY_INTERFACE_MODE_RMII: {
		struct sysconf_field *sc;
		pad_config = &stih415_ethernet_rmii_pad_configs[port];

		/* SEL_INTERNAL_NO_EXT_PHYCLK */
		if (!port)
			sc  = sysconf_claim(2, 82, 7, 7, "rmii");
		else
			sc  = sysconf_claim(0, 29, 7, 7, "rmii");

		if (config->ext_clk) {
			stm_pad_set_pio_in(pad_config, "PHYCLK", 3 - port);
			/* SEL_INTERNAL_NO_EXT_PHYCLK */
			sysconf_write(sc, 0);
		} else {
			unsigned long phy_clk_rate;
			struct clk *phy_clk = clk_get(NULL, gmac_clk_n[port]);
			BUG_ON(!phy_clk);
			stm_pad_set_pio_out(pad_config, "PHYCLK", 2 - port);
			phy_clk_rate = 50000000;
			clk_set_rate(phy_clk, phy_clk_rate);
			/* SEL_INTERNAL_NO_EXT_PHYCLK */
			sysconf_write(sc, 1);
		}
	} break;
	case PHY_INTERFACE_MODE_REV_MII:
		pad_config = &stih415_ethernet_reverse_mii_pad_configs[port];
		if (config->ext_clk)
			stm_pad_set_pio_ignored(pad_config, "PHYCLK");
		else
			stm_pad_set_pio_out(pad_config, "PHYCLK", 1 + port);
		break;
	default:
		BUG();
		return;
	}

	plat_data->custom_cfg = (void *) pad_config;
	plat_data->interface = config->interface;
	plat_data->bus_id = config->phy_bus;
	plat_data->phy_bus_name = config->phy_bus_name;
	plat_data->phy_addr = config->phy_addr;
	plat_data->mdio_bus_data = config->mdio_bus_data;
	if (!config->mdio_bus_data) {
		stm_pad_set_pio_ignored(pad_config, "MDC");
		stm_pad_set_pio_ignored(pad_config, "MDIO");
	}

	platform_device_register(&stih415_ethernet_devices[port]);
}
/* MiPHY resources -------------------------------------------------------- */

#define MAX_PORTS		2
#define PCIE_BASE		0xfe800000
#define PCIE_UPORT_BASE		(PCIE_BASE + 0x4000)
#define PCIE_UPORT_REG_SIZE	(0xFF)

static enum miphy_mode stih415_miphy_modes[MAX_PORTS];
static struct sysconf_field *sc_pcie_mp_select;
static struct sysconf_field *sc_miphy_reset[MAX_PORTS];


static void stih415_pcie_mp_select(void *data, int port)
{
	BUG_ON(port < 0 || port > 1);
	sysconf_write(sc_pcie_mp_select, port);
}

struct stm_plat_pcie_mp_data stih415_pcie_mp_platform_data = {
	.style_id = ID_MIPHY365X,
	.miphy_first = 0,
	.miphy_count = MAX_PORTS,
	.miphy_modes = stih415_miphy_modes,
	.mp_select = stih415_pcie_mp_select,
};
static struct platform_device stih415_pcie_mp_device = {
	.name   = "pcie-mp",
	.id     = 0,
	.num_resources = 1,
	.resource = (struct resource[]) {
		STM_PLAT_RESOURCE_MEM_NAMED("pcie-uport", PCIE_UPORT_BASE,
					    PCIE_UPORT_REG_SIZE),
	},
	.dev = {
		.platform_data = &stih415_pcie_mp_platform_data,
	}
};

static int __init stih415_configure_miphy_uport(void)
{
	struct sysconf_field *sc_miphy1_ref_clk,
			*sc_sata1_hc_reset, *sc_sata_pcie_sel,
			*sc_sata0_hc_reset;

	sc_pcie_mp_select = sysconf_claim(SYSCONF(335),
					  0, 0, "pcie-mp");
	BUG_ON(!sc_pcie_mp_select);

	/* SATA0_SOFT_RST_N_SATA: sata0_soft_rst_n_sata. */
	sc_sata0_hc_reset = sysconf_claim(SYSCONF(377), 7, 7, "MiPHY");
	BUG_ON(!sc_sata0_hc_reset);

	/* SELECT_SATA: select_sata. */
	sc_sata_pcie_sel = sysconf_claim(SYSCONF(333), 1, 1, "MiPHY");
	BUG_ON(!sc_sata_pcie_sel);

	/* SATAPHY1_OSC_FORCE_EXT: SATAphy1_osc_force_ext. */
	sc_miphy1_ref_clk = sysconf_claim(SYSCONF(333), 2, 2, "MiPHY");
	BUG_ON(!sc_miphy1_ref_clk);

	/*SATA1_SOFT_RST_N_SATA: sata1_soft_rst_n_sata  */
	sc_sata1_hc_reset = sysconf_claim(SYSCONF(377), 3, 3, "SATA");
	BUG_ON(!sc_sata1_hc_reset);
	/* Deassert Soft Reset to SATA0 */
	sysconf_write(sc_sata0_hc_reset, 1);
	/* If the 100MHz xtal for PCIe is present, then the microport interface
	* will already have a clock, so there is no need to flip to the 30MHz
	* clock here. If it isn't then we have to switch miphy lane 1 to use
	* the 30MHz clock, as otherwise we will not be able to talk to lane 0
	* since the uport interface itself is clocked from lane1
	*/
	if (stih415_miphy_modes[1] != PCIE_MODE) {
		/* Put MiPHY1 in reset - rst_per_n[32] */
		sysconf_write(sc_miphy_reset[1], 0);
		/* Put SATA1 HC in reset - rst_per_n[30] */
		sysconf_write(sc_sata1_hc_reset, 0);
		/* Now switch to Phy interface to SATA HC not PCIe HC */
		sysconf_write(sc_sata_pcie_sel, 1);
		/* Select the Uport to use MiPHY1 */
		stih415_pcie_mp_select(NULL, 1);
		/* Take SATA1 HC out of reset - rst_per_n[30] */
		sysconf_write(sc_sata1_hc_reset, 1);
		/* MiPHY1 needs to be using the MiPHY0 reference clock */
		sysconf_write(sc_miphy1_ref_clk, 1);
		/* Take MiPHY1 out of reset - rst_per_n[32] */
		sysconf_write(sc_miphy_reset[1], 1);
	}

	return platform_device_register(&stih415_pcie_mp_device);
}

void __init stih415_configure_miphy(struct stih415_miphy_config *config)
{
	int err = 0;

	memcpy(stih415_miphy_modes, config->modes, sizeof(stih415_miphy_modes));

	/*Reset to MIPHY0 */
	sc_miphy_reset[0]  = sysconf_claim(SYSCONF(376), 18, 18, "SATA");
	BUG_ON(!sc_miphy_reset[0]);

	/* Reset to MIPHY1 */
	sc_miphy_reset[1]  = sysconf_claim(SYSCONF(376), 19, 19, "SATA");
	BUG_ON(!sc_miphy_reset[1]);

	err = stih415_configure_miphy_uport();
}
/* SATA resources --------------------------------------------------------- */

/* STBus Convertor config */
static struct stm_amba_bridge_config stih415_amba_sata_config = {
	.type			=	stm_amba_type2,
	.chunks_in_msg		=	1,
	.packets_in_chunk	=	2,
	.write_posting		=	stm_amba_write_posting_disabled,
	.max_opcode		=	stm_amba_opc_LD64_ST64,
	.type2.threshold	=	256,
	.type2.sd_config_missing =	1,
	.type2.trigger_mode	=	stm_amba_stbus_threshold_based,
	.type2.read_ahead	=	stm_amba_read_ahead_enabled,
};

static struct sysconf_field *sc_sata_hc_pwr[MAX_PORTS];

static void stih415_restart_sata(int port)
{
	/* This part of the code is executed for ESD recovery.*/
	/* Reset the SATA Host and MiPHY */
	sysconf_write(sc_sata_hc_pwr[port], 1);
	sysconf_write(sc_miphy_reset[port], 0);

	if (port == 1)
		stih415_pcie_mp_select(NULL, 1);
	msleep(1);
	sysconf_write(sc_sata_hc_pwr[port], 0);
	sysconf_write(sc_miphy_reset[port], 1);
}

static void stih415_sata_power(struct stm_device_state *device_state,
		int port, enum stm_device_power_state power)
{
	int value = (power == stm_device_power_on) ? 0 : 1;
	int i;

	sysconf_write(sc_sata_hc_pwr[port], value);

	for (i = 100; i; --i) {
		if (stm_device_sysconf_read(device_state, "SATA_ACK")
				== value)
			break;
		mdelay(10);
	}
}
static void stih415_sata0_power(struct stm_device_state *device_state,
		enum stm_device_power_state power)
{
	stih415_sata_power(device_state, 0, power);
}
static void stih415_sata1_power(struct stm_device_state *device_state,
		enum stm_device_power_state power)
{
	stih415_sata_power(device_state, 1, power);
}

static u64 stih415_sata_dma_mask = DMA_BIT_MASK(32);
static struct platform_device stih415_sata_devices[] = {
	[0] = {
		.name = "sata-stm",
		.id = 0,
		.dev.dma_mask = &stih415_sata_dma_mask,
		.dev.coherent_dma_mask = DMA_BIT_MASK(32),
		.dev.platform_data = &(struct stm_plat_sata_data) {
			.phy_init = 0,
			.pc_glue_logic_init = 0,
			.only_32bit = 0,
			.oob_wa = 1,
			.port_num = 0,
			.miphy_num = 0,
			.amba_config = &stih415_amba_sata_config,
			.device_config = &(struct stm_device_config){
				.power = stih415_sata0_power,
				.sysconfs_num = 1,
				.sysconfs = (struct stm_device_sysconf []) {
					STM_DEVICE_SYSCONF(SYSCONF(384), 3, 3,
						"SATA_ACK"),
				},
			},
		},
		.num_resources = 3,
		.resource = (struct resource[]) {
			STM_PLAT_RESOURCE_MEM(0xfe818000, 0x1000),
			STIH415_RESOURCE_IRQ_NAMED("hostc", 161),
			STIH415_RESOURCE_IRQ_NAMED("dmac", 162),
			},
	},
	[1] = {
		.name = "sata-stm",
		.id = 1,
		.dev.dma_mask = &stih415_sata_dma_mask,
		.dev.coherent_dma_mask = DMA_BIT_MASK(32),
		.dev.platform_data = &(struct stm_plat_sata_data) {
			.phy_init = 0,
			.pc_glue_logic_init = 0,
			.only_32bit = 0,
			.oob_wa = 1,
			.port_num = 1,
			.miphy_num = 1,
			.amba_config = &stih415_amba_sata_config,
			.device_config = &(struct stm_device_config){
				.power = stih415_sata1_power,
				.sysconfs_num = 1,
				.sysconfs = (struct stm_device_sysconf []) {
					STM_DEVICE_SYSCONF(SYSCONF(384), 4, 4,
						"SATA_ACK"),
				},
			},
		},
		.num_resources = 3,
		.resource = (struct resource[]) {
			STM_PLAT_RESOURCE_MEM(0xfe819000, 0x1000),
			STIH415_RESOURCE_IRQ_NAMED("hostc", 164),
			STIH415_RESOURCE_IRQ_NAMED("dmac", 165),
		},
	}
};

void __init stih415_configure_sata(int port, struct stih415_sata_config *config)
{
	struct stm_plat_sata_data *sata_data;
	static int initialized[2];

	sata_data = stih415_sata_devices[port].dev.platform_data;

	BUG_ON(initialized[port]);

	sc_sata_hc_pwr[port] = sysconf_claim(SYSCONF(336),
					     3 + port, 3 + port, "SATA");
	if (!sc_sata_hc_pwr[port]) {
		BUG();
		return;
	}

	sata_data->host_restart = stih415_restart_sata;
	platform_device_register(&stih415_sata_devices[port]);
}
/* MMC/SD resources ------------------------------------------------------ */
/* Custom PAD configuration for the MMC Host controller */
#define STIH415_PIO_MMC_CLK_OUT(_port, _pin) \
	{ \
		.gpio = stm_gpio(_port, _pin), \
		.direction = stm_pad_gpio_direction_bidir_pull_up, \
		.function = 4, \
		.name = "MMCCLK", \
		.priv = &(struct stm_pio_control_pad_config) {	\
			.retime = RET_NICLK(0, 1), \
		}, \
	}

#define STIH415_PIO_MMC_OUT(_port, _pin) \
	{ \
		.gpio = stm_gpio(_port, _pin), \
		.direction = stm_pad_gpio_direction_out, \
		.function = 4, \
	}

#define STIH415_PIO_MMC_BIDIR(_port, _pin) \
	{ \
		.gpio = stm_gpio(_port, _pin), \
		.direction = stm_pad_gpio_direction_bidir_pull_up, \
		.function = 4, \
		.priv = &(struct stm_pio_control_pad_config) {	\
			.retime = RET_BYPASS(0), \
		}, \
	}

#define STIH415_PIO_MMC_IN(_port, _pin) \
	{ \
		.gpio = stm_gpio(_port, _pin), \
		.direction = stm_pad_gpio_direction_in, \
		.function = 4, \
	}

static struct stm_pad_config stih415_mmc_pad_config = {
	.gpios_num = 14,
	.gpios = (struct stm_pad_gpio []) {
		STIH415_PIO_MMC_CLK_OUT(13, 4),
		STIH415_PIO_MMC_BIDIR(14, 4),	/* MMC Data[0]*/
		STIH415_PIO_MMC_BIDIR(14, 5),	/* MMC Data[1]*/
		STIH415_PIO_MMC_BIDIR(14, 6),	/* MMC Data[2]*/
		STIH415_PIO_MMC_BIDIR(14, 7),	/* MMC Data[3]*/

		STIH415_PIO_MMC_BIDIR(15, 1),	/* MMC command */
		STIH415_PIO_MMC_IN(15, 3),	/* MMC Write Protection */

		STIH415_PIO_MMC_BIDIR(16, 4),	/* MMC Data[4]*/
		STIH415_PIO_MMC_BIDIR(16, 5),	/* MMC Data[5]*/
		STIH415_PIO_MMC_BIDIR(16, 6),	/* MMC Data[6]*/
		STIH415_PIO_MMC_BIDIR(16, 7),	/* MMC Data[7]*/

		STIH415_PIO_MMC_OUT(17, 1),	/* MMC Card PWR */
		STIH415_PIO_MMC_IN(17, 2),	/* MMC Card Detect */
		STIH415_PIO_MMC_OUT(17, 3),	/* MMC LED on */
	},
};

static struct stm_amba_bridge_config stih415_amba_mmc_config = {
	.type =	stm_amba_type2,
	.chunks_in_msg = 1,
	.packets_in_chunk = 2,
	.write_posting = stm_amba_write_posting_enabled,
	.max_opcode = stm_amba_opc_LD64_ST64,
	.type2.threshold = 128,
	.type2.sd_config_missing = 1,
	.type2.trigger_mode = stm_amba_stbus_threshold_based,
	.type2.read_ahead = stm_amba_read_ahead_enabled,
};

static struct stm_mmc_platform_data stih415_mmc_platform_data = {
	.amba_config = &stih415_amba_mmc_config,
	.custom_cfg = &stih415_mmc_pad_config,
	.nonremovable = false,
	.hc_core_init = -EOPNOTSUPP,
	.gpio_cd = -EINVAL,
};

static struct platform_device stih415_mmc_device = {
	.name = "sdhci-stm",
	.id = 0,
	.num_resources = 2,
	.resource = (struct resource[]) {
		STM_PLAT_RESOURCE_MEM(0xfe81e000, 0x1000),
		STIH415_RESOURCE_IRQ_NAMED("mmcirq", 145),
	},
	.dev = {
		.platform_data = &stih415_mmc_platform_data,
	}
};

void __init stih415_configure_mmc(int emmc)
{
	struct stm_mmc_platform_data *plat_data;

	plat_data = &stih415_mmc_platform_data;

	if (emmc)
		plat_data->nonremovable = true;

	platform_device_register(&stih415_mmc_device);
}
#endif /* CONFIG_OF */
