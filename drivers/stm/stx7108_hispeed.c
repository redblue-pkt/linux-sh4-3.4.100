/*
 * (c) 2010 STMicroelectronics Limited
 *
 * Author: Pawel Moll <pawel.moll@st.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */



#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/ethtool.h>
#include <linux/dma-mapping.h>
#include <linux/phy.h>
#include <linux/stm/miphy.h>
#include <linux/stm/device.h>
#include <linux/stm/sysconf.h>
#include <linux/stm/emi.h>
#include <linux/stm/stx7108.h>
#include <linux/stm/amba_bridge.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <asm/irq-ilc.h>
#include "pio-control.h"

/* --------------------------------------------------------------------
 *           Ethernet MAC resources (PAD and Retiming)
 * --------------------------------------------------------------------*/

#define DATA_IN(_gmac, _port, _pin, _retiming) \
	{ \
		.gpio = stm_gpio(_port, _pin), \
		.direction = stm_pad_gpio_direction_in, \
		.function = _gmac + 1, \
		.priv = &(struct stm_pio_control_pad_config) { \
			.retime = _retiming, \
		}, \
	}

#define DATA_OUT(_gmac, _port, _pin, _retiming) \
	{ \
		.gpio = stm_gpio(_port, _pin), \
		.direction = stm_pad_gpio_direction_out, \
		.function = _gmac + 1, \
		.priv = &(struct stm_pio_control_pad_config) { \
			.retime = _retiming, \
		}, \
	}

/* On some boards MDIO line is missing Pull-up resistor, Enabling weak
 * internal PULL-UP overcomes the issue */
#define MDIO(_gmac, _port, _pin, _retiming) \
	{ \
		.gpio = stm_gpio(_port, _pin), \
		.direction = stm_pad_gpio_direction_bidir_pull_up, \
		.function = _gmac + 1, \
		.priv = &(struct stm_pio_control_pad_config) { \
			.retime = _retiming, \
		}, \
	}

#define CLOCK_IN(_gmac, _port, _pin, _retiming) \
	{ \
		.gpio = stm_gpio(_port, _pin), \
		.direction = stm_pad_gpio_direction_in, \
		.function = _gmac + 1, \
		.priv = &(struct stm_pio_control_pad_config) { \
			.retime = _retiming, \
		}, \
	}

#define CLOCK_OUT(_gmac, _port, _pin, _retiming) \
	{ \
		.gpio = stm_gpio(_port, _pin), \
		.direction = stm_pad_gpio_direction_out, \
		.function = _gmac + 1, \
		.priv = &(struct stm_pio_control_pad_config) { \
			.retime = _retiming, \
		}, \
	}

#define BYPASS_IN(_gmac, _port, _pin, _retiming) \
	{ \
		.gpio = stm_gpio(_port, _pin), \
		.direction = stm_pad_gpio_direction_in, \
		.function = _gmac + 1, \
		.priv = &(struct stm_pio_control_pad_config) { \
			.retime = &_retiming, \
		}, \
	}

#define BYPASS_OUT(_gmac, _port, _pin, _retiming) \
	{ \
		.gpio = stm_gpio(_port, _pin), \
		.direction = stm_pad_gpio_direction_out, \
		.function = _gmac + 1, \
		.priv = &(struct stm_pio_control_pad_config) { \
			.retime = &_retiming, \
		}, \
	}

#define PHY_CLOCK(_gmac, _port, _pin, _retiming) \
	{ \
		.gpio = stm_gpio(_port, _pin), \
		.direction = stm_pad_gpio_direction_unknown, \
		.name = "PHYCLK", \
		.priv = &(struct stm_pio_control_pad_config) { \
		.retime = _retiming, \
		}, \
	}

#define TX_CLOCK(_gmac, _port, _pin, _retiming) \
	{ \
		.gpio = stm_gpio(_port, _pin), \
		.direction = stm_pad_gpio_direction_in, \
		.name = "TXCLK", \
		.function = _gmac + 1, \
		.priv = &(struct stm_pio_control_pad_config) { \
			.retime = _retiming, \
		}, \
	}



static struct stm_pad_config stx7108_ethernet_mii_pad_configs[] = {
	[0] =  {
		.gpios_num = 20,
		.gpios = (struct stm_pad_gpio []) {
			DATA_OUT(0, 6, 0, RET_SE_NICLK_IO(0, 0)),/* TXD[0] */
			DATA_OUT(0, 6, 1, RET_SE_NICLK_IO(0, 0)),/* TXD[1] */
			DATA_OUT(0, 6, 2, RET_SE_NICLK_IO(0, 0)),/* TXD[2] */
			DATA_OUT(0, 6, 3, RET_SE_NICLK_IO(0, 0)),/* TXD[3] */
			DATA_OUT(0, 7, 0, RET_SE_NICLK_IO(0, 0)),/* TXER */
			DATA_OUT(0, 7, 1, RET_SE_NICLK_IO(0, 0)),/* TXEN */
			CLOCK_IN(0, 7, 2, RET_NICLK(0, 0)),/* TXCLK */
			DATA_IN(0, 7, 3, RET_BYPASS(0)),/* COL */
			MDIO(0, 7, 4, RET_BYPASS(3000)),/* MDIO*/
			CLOCK_OUT(0, 7, 5, RET_NICLK(0, 0)),/* MDC */
			DATA_IN(0, 7, 6, RET_BYPASS(0)),/* CRS */
			DATA_IN(0, 7, 7, RET_BYPASS(0)),/* MDINT */
			DATA_IN(0, 8, 0, RET_SE_NICLK_IO(1000, 0)),/* RXD[0] */
			DATA_IN(0, 8, 1, RET_SE_NICLK_IO(1000, 0)),/* RXD[1] */
			DATA_IN(0, 8, 2, RET_SE_NICLK_IO(1000, 0)),/* RXD[2] */
			DATA_IN(0, 8, 3, RET_SE_NICLK_IO(1000, 0)),/* RXD[3] */
			DATA_IN(0, 9, 0, RET_SE_NICLK_IO(1000, 0)),/* RXDV */
			DATA_IN(0, 9, 1, RET_SE_NICLK_IO(1000, 0)),/* RX_ER */
			CLOCK_IN(0, 9, 2, RET_NICLK(0, 0)),/* RXCLK */
			PHY_CLOCK(0, 9, 3, RET_NICLK(0, 0)),/* PHYCLK */
		},
		.sysconfs_num = 3,
		.sysconfs = (struct stm_pad_sysconf []) {
			/* EN_GMAC0 */
			STM_PAD_SYS_CFG_BANK(2, 53, 0, 0, 1),
			/* MIIx_PHY_SEL */
			STM_PAD_SYS_CFG_BANK(2, 27, 2, 4, 0),
			/* ENMIIx */
			STM_PAD_SYS_CFG_BANK(2, 27, 5, 5, 1),
		},
	},
	[1] =  {
		.gpios_num = 20,
		.gpios = (struct stm_pad_gpio []) {
			PHY_CLOCK(1, 15, 5, RET_NICLK(0, 0)),/* PHYCLK */
			DATA_IN(1, 15, 6, RET_BYPASS(0)),/* MDINT */
			DATA_OUT(1, 15, 7, RET_SE_NICLK_IO(0, 1)),/* TXEN */
			DATA_OUT(1, 16, 0, RET_SE_NICLK_IO(0, 1)),/* TXD[0] */
			DATA_OUT(1, 16, 1, RET_SE_NICLK_IO(0, 1)),/* TXD[1] */
			DATA_OUT(1, 16, 2, RET_SE_NICLK_IO(0, 1)),/* TXD[2] */
			DATA_OUT(1, 16, 3, RET_SE_NICLK_IO(0, 1)),/* TXD[3] */
			CLOCK_IN(1, 17, 0, RET_NICLK(0, 0)),/* TXCLK */
			DATA_OUT(1, 17, 1, RET_SE_NICLK_IO(0, 1)),/* TXER */
			DATA_IN(1, 17, 2, RET_BYPASS(0)),/* CRS */
			DATA_IN(1, 17, 3, RET_BYPASS(0)),/* COL */
			MDIO(1, 17, 4, RET_BYPASS(3000)),/* MDIO */
			CLOCK_OUT(1, 17, 5, RET_NICLK(0, 1)),/* MDC */
			DATA_IN(1, 17, 6, RET_SE_NICLK_IO(1000, 1)),/* RXDV */
			DATA_IN(1, 17, 7, RET_SE_NICLK_IO(1000, 1)),/* RX_ER */
			DATA_IN(1, 18, 0, RET_SE_NICLK_IO(1000, 1)),/* RXD[0] */
			DATA_IN(1, 18, 1, RET_SE_NICLK_IO(1000, 1)),/* RXD[1] */
			DATA_IN(1, 18, 2, RET_SE_NICLK_IO(1000, 1)),/* RXD[2] */
			DATA_IN(1, 18, 3, RET_SE_NICLK_IO(1000, 1)),/* RXD[3] */
			CLOCK_IN(1, 19, 0, RET_NICLK(0, 0)),/* RXCLK */
		},
		.sysconfs_num = 3,
		.sysconfs = (struct stm_pad_sysconf []) {
			/* EN_GMAC1 */
			STM_PAD_SYS_CFG_BANK(4, 67, 0, 0, 1),
			/* MIIx_PHY_SEL */
			STM_PAD_SYS_CFG_BANK(4, 23, 2, 4, 0),
			/* ENMIIx */
			STM_PAD_SYS_CFG_BANK(4, 23, 5, 5, 1),
		},
	},
};

static struct stm_pad_config stx7108_ethernet_gmii_pad_configs[] = {
	[0] =  {
		.gpios_num = 28,
		.gpios = (struct stm_pad_gpio []) {
			DATA_OUT(0, 6, 0, RET_SE_NICLK_IO(3000, 0)),/* TXD[0] */
			DATA_OUT(0, 6, 1, RET_SE_NICLK_IO(3000, 0)),/* TXD[1] */
			DATA_OUT(0, 6, 2, RET_SE_NICLK_IO(3000, 0)),/* TXD[2] */
			DATA_OUT(0, 6, 3, RET_SE_NICLK_IO(3000, 0)),/* TXD[3] */
			DATA_OUT(0, 6, 4, RET_SE_NICLK_IO(3000, 0)),/* TXD[4] */
			DATA_OUT(0, 6, 5, RET_SE_NICLK_IO(3000, 0)),/* TXD[5] */
			DATA_OUT(0, 6, 6, RET_SE_NICLK_IO(3000, 0)),/* TXD[6] */
			DATA_OUT(0, 6, 7, RET_SE_NICLK_IO(3000, 0)),/* TXD[7] */
			DATA_OUT(0, 7, 0, RET_SE_NICLK_IO(3000, 0)),/* TXER */
			DATA_OUT(0, 7, 1, RET_SE_NICLK_IO(3000, 0)),/* TXEN */
			CLOCK_IN(0, 7, 2, RET_NICLK(0, 0)),/* TXCLK */
			DATA_IN(0, 7, 3, RET_BYPASS(0)),/* COL */
			MDIO(0, 7, 4, RET_BYPASS(3000)),/* MDIO */
			CLOCK_OUT(0, 7, 5, RET_NICLK(0, 0)),/* MDC */
			DATA_IN(0, 7, 6, RET_BYPASS(0)),/* CRS */
			DATA_IN(0, 7, 7, RET_BYPASS(0)),/* MDINT */
			DATA_IN(0, 8, 0, RET_SE_NICLK_IO(1000, 0)),/* RXD[0] */
			DATA_IN(0, 8, 1, RET_SE_NICLK_IO(1000, 0)),/* RXD[1] */
			DATA_IN(0, 8, 2, RET_SE_NICLK_IO(1000, 0)),/* RXD[2] */
			DATA_IN(0, 8, 3, RET_SE_NICLK_IO(1000, 0)),/* RXD[3] */
			DATA_IN(0, 8, 4, RET_SE_NICLK_IO(1000, 0)),/* RXD[4] */
			DATA_IN(0, 8, 5, RET_SE_NICLK_IO(1000, 0)),/* RXD[5] */
			DATA_IN(0, 8, 6, RET_SE_NICLK_IO(1000, 0)),/* RXD[6] */
			DATA_IN(0, 8, 7, RET_SE_NICLK_IO(1000, 0)),/* RXD[7] */
			DATA_IN(0, 9, 0, RET_SE_NICLK_IO(1000, 0)),/* RXDV */
			DATA_IN(0, 9, 1, RET_SE_NICLK_IO(1000, 0)),/* RX_ER */
			CLOCK_IN(0, 9, 2, RET_NICLK(0, 0)),/* RXCLK */
			PHY_CLOCK(0, 9, 3, RET_NICLK(0, 1)),/* GTXCLK */
		},
		.sysconfs_num = 3,
		.sysconfs = (struct stm_pad_sysconf []) {
			/* EN_GMAC0 */
			STM_PAD_SYS_CFG_BANK(2, 53, 0, 0, 1),
			/* MIIx_PHY_SEL */
			STM_PAD_SYS_CFG_BANK(2, 27, 2, 4, 0),
			/* ENMIIx */
			STM_PAD_SYS_CFG_BANK(2, 27, 5, 5, 1),
		},
	},
	[1] =  {
		.gpios_num = 28,
		.gpios = (struct stm_pad_gpio []) {
			PHY_CLOCK(1, 15, 5, RET_NICLK(0, 1)), /* GTXCLK */
			DATA_IN(1, 15, 6, RET_BYPASS(0)),/* MDINT */
			DATA_OUT(1, 15, 7, RET_SE_NICLK_IO(3000, 1)),/* TXEN */
			DATA_OUT(1, 16, 0, RET_SE_NICLK_IO(3000, 1)),/* TXD[0]*/
			DATA_OUT(1, 16, 1, RET_SE_NICLK_IO(3000, 1)),/* TXD[1]*/
			DATA_OUT(1, 16, 2, RET_SE_NICLK_IO(3000, 1)),/* TXD[2]*/
			DATA_OUT(1, 16, 3, RET_SE_NICLK_IO(3000, 1)),/* TXD[3]*/
			DATA_OUT(1, 16, 4, RET_SE_NICLK_IO(3000, 1)),/* TXD[4]*/
			DATA_OUT(1, 16, 5, RET_SE_NICLK_IO(3000, 1)),/* TXD[5]*/
			DATA_OUT(1, 16, 6, RET_SE_NICLK_IO(3000, 1)),/* TXD[6]*/
			DATA_OUT(1, 16, 7, RET_SE_NICLK_IO(3000, 1)),/* TXD[7]*/
			CLOCK_IN(1, 17, 0, RET_NICLK(0, 0)),/* TXCLK */
			DATA_OUT(1, 17, 1, RET_SE_NICLK_IO(3000, 1)),/* TXER */
			DATA_IN(1, 17, 2, RET_BYPASS(0)),/* CRS */
			DATA_IN(1, 17, 3, RET_BYPASS(0)),/* COL */
			MDIO(1, 17, 4, RET_BYPASS(3000)),/* MDIO */
			CLOCK_OUT(1, 17, 5, RET_NICLK(0, 1)),/* MDC */
			DATA_IN(1, 17, 6, RET_SE_NICLK_IO(1000, 1)),/* RXDV */
			DATA_IN(1, 17, 7, RET_SE_NICLK_IO(1000, 1)),/* RX_ER */
			DATA_IN(1, 18, 0, RET_SE_NICLK_IO(1000, 1)),/* RXD[0] */
			DATA_IN(1, 18, 1, RET_SE_NICLK_IO(1000, 1)),/* RXD[1] */
			DATA_IN(1, 18, 2, RET_SE_NICLK_IO(1000, 1)),/* RXD[2] */
			DATA_IN(1, 18, 3, RET_SE_NICLK_IO(1000, 1)),/* RXD[3] */
			DATA_IN(1, 18, 4, RET_SE_NICLK_IO(1000, 1)),/* RXD[4] */
			DATA_IN(1, 18, 5, RET_SE_NICLK_IO(1000, 1)),/* RXD[5] */
			DATA_IN(1, 18, 6, RET_SE_NICLK_IO(1000, 1)),/* RXD[6] */
			DATA_IN(1, 18, 7, RET_SE_NICLK_IO(1000, 1)),/* RXD[7] */
			CLOCK_IN(1, 19, 0, RET_NICLK(0, 0)),/* RXCLK */
		},
		.sysconfs_num = 3,
		.sysconfs = (struct stm_pad_sysconf []) {
			/* EN_GMAC1 */
			STM_PAD_SYS_CFG_BANK(4, 67, 0, 0, 1),
			/* MIIx_PHY_SEL */
			STM_PAD_SYS_CFG_BANK(4, 23, 2, 4, 0),
			/* ENMIIx */
			STM_PAD_SYS_CFG_BANK(4, 23, 5, 5, 1),
		},
	},
};

static struct stm_pad_config stx7108_ethernet_rgmii_pad_configs[] = {
	[0] =  {
		.gpios_num = 17,
		.gpios = (struct stm_pad_gpio []) {
			DATA_OUT(0, 6, 0, RET_DE_IO(0, 0)),/* TXD[0] */
			DATA_OUT(0, 6, 1, RET_DE_IO(0, 0)),/* TXD[1] */
			DATA_OUT(0, 6, 2, RET_DE_IO(0, 0)),/* TXD[2] */
			DATA_OUT(0, 6, 3, RET_DE_IO(0, 0)),/* TXD[3] */
			DATA_OUT(0, 7, 1, RET_DE_IO(0, 0)),/* TXEN */
			/* TX Clock inversion is not set for 1000Mbps */
			TX_CLOCK(0, 7, 2, RET_NICLK(0, 0)),/* TXCLK */
			DATA_IN(0, 7, 3, RET_BYPASS(0)),/* COL */
			MDIO(0, 7, 4, RET_BYPASS(3000)),/* MDIO */
			CLOCK_OUT(0, 7, 5, RET_NICLK(0, 0)),/* MDC */
			DATA_IN(0, 7, 7, RET_BYPASS(0)), /* MDINT */
			DATA_IN(0, 8, 0, RET_DE_IO(0, 0)),/* RXD[0] */
			DATA_IN(0, 8, 1, RET_DE_IO(0, 0)),/* RXD[1] */
			DATA_IN(0, 8, 2, RET_DE_IO(0, 0)),/* RXD[2] */
			DATA_IN(0, 8, 3, RET_DE_IO(0, 0)),/* RXD[3] */
			DATA_IN(0, 9, 0, RET_DE_IO(0, 0)),/* RXDV */
			CLOCK_IN(0, 9, 2, RET_NICLK(0, 0)),/* RXCLK */
			PHY_CLOCK(0, 9, 3, RET_NICLK(0, 1)),/* PHYCLK */
		},
		.sysconfs_num = 3,
		.sysconfs = (struct stm_pad_sysconf []) {
			/* EN_GMAC0 */
			STM_PAD_SYS_CFG_BANK(2, 53, 0, 0, 1),
			/* MIIx_PHY_SEL */
			STM_PAD_SYS_CFG_BANK(2, 27, 2, 4, 1),
			/* ENMIIx */
			STM_PAD_SYS_CFG_BANK(2, 27, 5, 5, 1),
		},
	},
	[1] =  {
		.gpios_num = 17,
		.gpios = (struct stm_pad_gpio []) {
			PHY_CLOCK(1, 15, 5, RET_NICLK(0, 1)), /* GTXCLK*/
			DATA_IN(1, 15, 6, RET_BYPASS(0)),/* MDINT */
			DATA_OUT(1, 15, 7, RET_DE_IO(0, 1)),/* TXEN */
			DATA_OUT(1, 16, 0, RET_DE_IO(0, 1)),/* TXD[0] */
			DATA_OUT(1, 16, 1, RET_DE_IO(0, 1)),/* TXD[1] */
			DATA_OUT(1, 16, 2, RET_DE_IO(0, 1)),/* TXD[2] */
			DATA_OUT(1, 16, 3, RET_DE_IO(0, 1)),/* TXD[3] */
			/* TX Clock inversion is not set for 1000Mbps */
			TX_CLOCK(1, 17, 0, RET_NICLK(0, 0)),/* TXCLK */
			DATA_IN(1, 17, 3, RET_BYPASS(0)),/* COL */
			MDIO(1, 17, 4, RET_BYPASS(3000)),/* MDIO */
			CLOCK_OUT(1, 17, 5, RET_NICLK(0, 1)),/* MDC */
			DATA_IN(1, 17, 6, RET_DE_IO(0, 1)),/* RXDV */
			DATA_IN(1, 18, 0, RET_DE_IO(0, 1)),/* RXD[0] */
			DATA_IN(1, 18, 1, RET_DE_IO(0, 1)),/* RXD[1] */
			DATA_IN(1, 18, 2, RET_DE_IO(0, 1)),/* RXD[2] */
			DATA_IN(1, 18, 3, RET_DE_IO(0, 1)),/* RXD[3] */
			CLOCK_IN(1, 19, 0, RET_NICLK(0, 0)),/* RXCLK */
		},
		.sysconfs_num = 3,
		.sysconfs = (struct stm_pad_sysconf []) {
			/* EN_GMAC1 */
			STM_PAD_SYS_CFG_BANK(4, 67, 0, 0, 1),
			/* MIIx_PHY_SEL */
			STM_PAD_SYS_CFG_BANK(4, 23, 2, 4, 1),
			/* ENMIIx */
			STM_PAD_SYS_CFG_BANK(4, 23, 5, 5, 1),
		},
	},
};
static struct stm_pad_config stx7108_ethernet_rmii_pad_configs[] = {
	[0] = {
		.gpios_num = 14,
		.gpios = (struct stm_pad_gpio []) {
			DATA_OUT(0, 6, 0, RET_SE_NICLK_IO(0, 1)),/* TXD[0] */
			DATA_OUT(0, 6, 1, RET_SE_NICLK_IO(0, 1)),/* TXD[1] */
			DATA_OUT(0, 7, 0, RET_SE_NICLK_IO(0, 1)),/* TXER */
			DATA_OUT(0, 7, 1, RET_SE_NICLK_IO(0, 1)),/* TXEN */
			DATA_OUT(0, 7, 3, RET_BYPASS(0)),/* COL */
			MDIO(0, 7, 4, RET_BYPASS(3000)),/* MDIO */
			CLOCK_OUT(0, 7, 5, RET_NICLK(0, 0)),/* MDC */
			DATA_IN(0, 7, 6, RET_BYPASS(0)),/* CRS */
			DATA_IN(0, 7, 7, RET_BYPASS(0)),/* MDINT */
			DATA_IN(0, 8, 0, RET_SE_NICLK_IO(1000, 1)),/* RXD.0 */
			DATA_IN(0, 8, 1, RET_SE_NICLK_IO(1000, 1)),/* RXD.1 */
			DATA_IN(0, 9, 0, RET_SE_NICLK_IO(1000, 1)),/* RXDV */
			DATA_IN(0, 9, 1, RET_SE_NICLK_IO(1000, 1)),/* RX_ER */
			PHY_CLOCK(0, 9, 3, RET_NICLK(0, 0)),/* PHYCLK */
		},
		.sysconfs_num = 3,
		.sysconfs = (struct stm_pad_sysconf []) {
			/* EN_GMAC0 */
			STM_PAD_SYS_CFG_BANK(2, 53, 0, 0, 1),
			/* MIIx_PHY_SEL */
			STM_PAD_SYS_CFG_BANK(2, 27, 2, 4, 4),
			/* ENMIIx */
			STM_PAD_SYS_CFG_BANK(2, 27, 5, 5, 1),
		},
	},
	[1] =  {
		.gpios_num = 14,
		.gpios = (struct stm_pad_gpio []) {
			PHY_CLOCK(1, 15, 5, RET_NICLK(0, 0)),/* PHYCLK */
			DATA_IN(1, 15, 6, RET_BYPASS(0)),/* MDINT */
			DATA_OUT(1, 15, 7, RET_SE_NICLK_IO(0, 0)),/* TXEN */
			DATA_OUT(1, 16, 0, RET_SE_NICLK_IO(0, 0)),/* TXD[0] */
			DATA_OUT(1, 16, 1, RET_SE_NICLK_IO(0, 0)),/* TXD[1] */
			DATA_OUT(1, 17, 1, RET_SE_NICLK_IO(0, 0)),/* TXER */
			DATA_OUT(1, 17, 2, RET_BYPASS(0)),/* CRS */
			DATA_OUT(1, 17, 3, RET_BYPASS(0)),/* COL */
			MDIO(1, 17, 4, RET_BYPASS(3000)),/* MDIO */
			CLOCK_OUT(1, 17, 5, RET_NICLK(0, 1)),/* MDC */
			DATA_IN(1, 17, 6, RET_SE_NICLK_IO(1000, 0)),/* RXDV */
			DATA_IN(1, 17, 7, RET_SE_NICLK_IO(1000, 0)),/* RX_ER */
			DATA_IN(1, 18, 0, RET_SE_NICLK_IO(1000, 0)),/* RXD[0] */
			DATA_IN(1, 18, 1, RET_SE_NICLK_IO(1000, 0)),/* RXD[1] */
		},
		.sysconfs_num = 3,
		.sysconfs = (struct stm_pad_sysconf []) {
			/* EN_GMAC1 */
			STM_PAD_SYS_CFG_BANK(4, 67, 0, 0, 1),
			/* MIIx_PHY_SEL */
			STM_PAD_SYS_CFG_BANK(4, 23, 2, 4, 4),
			/* ENMIIx */
			STM_PAD_SYS_CFG_BANK(4, 23, 5, 5, 1),
		},
	},
};

static struct stm_pad_config stx7108_ethernet_reverse_mii_pad_configs[] = {
	[0] = {
		.gpios_num = 20,
		.gpios = (struct stm_pad_gpio []) {
			DATA_OUT(0, 6, 0, RET_SE_NICLK_IO(0, 0)),/* TXD[0] */
			DATA_OUT(0, 6, 1, RET_SE_NICLK_IO(0, 0)),/* TXD[1] */
			DATA_OUT(0, 6, 2, RET_SE_NICLK_IO(0, 0)),/* TXD[2] */
			DATA_OUT(0, 6, 3, RET_SE_NICLK_IO(0, 0)),/* TXD[3] */
			DATA_OUT(0, 7, 0, RET_SE_NICLK_IO(0, 0)),/* TXER */
			DATA_OUT(0, 7, 1, RET_SE_NICLK_IO(0, 0)),/* TXEN */
			CLOCK_IN(0, 7, 2, RET_NICLK(0, 0)),/* TXCLK */
			DATA_OUT(0, 7, 3, RET_BYPASS(0)),/* COL */
			MDIO(0, 7, 4, RET_BYPASS(3000)),/* MDIO*/
			CLOCK_IN(0, 7, 5, RET_NICLK(0, 0)),/* MDC */
			DATA_OUT(0, 7, 6, RET_BYPASS(0)),/* CRS */
			DATA_IN(0, 7, 7, RET_BYPASS(0)),/* MDINT */
			DATA_IN(0, 8, 0, RET_SE_NICLK_IO(1000, 0)),/* RXD[0] */
			DATA_IN(0, 8, 1, RET_SE_NICLK_IO(1000, 0)),/* RXD[1] */
			DATA_IN(0, 8, 2, RET_SE_NICLK_IO(1000, 0)),/* RXD[2] */
			DATA_IN(0, 8, 3, RET_SE_NICLK_IO(1000, 0)),/* RXD[3] */
			DATA_IN(0, 9, 0, RET_SE_NICLK_IO(1000, 0)),/* RXDV */
			DATA_IN(0, 9, 1, RET_SE_NICLK_IO(1000, 0)),/* RX_ER */
			CLOCK_IN(0, 9, 2, RET_NICLK(0, 0)),/* RXCLK */
			PHY_CLOCK(0, 9, 3, RET_NICLK(0, 0)),/* PHYCLK */
		},
		.sysconfs_num = 3,
		.sysconfs = (struct stm_pad_sysconf []) {
			/* EN_GMAC0 */
			STM_PAD_SYS_CFG_BANK(2, 53, 0, 0, 1),
			/* MIIx_PHY_SEL */
			STM_PAD_SYS_CFG_BANK(2, 27, 2, 4, 0),
			/* ENMIIx */
			STM_PAD_SYS_CFG_BANK(2, 27, 5, 5, 0),
		},
	},
	[1] =  {
		.gpios_num = 20,
		.gpios = (struct stm_pad_gpio []) {
			PHY_CLOCK(1, 15, 5, RET_NICLK(0, 0)),/* PHYCLK */
			DATA_IN(1, 15, 6, RET_BYPASS(0)),/* MDINT */
			DATA_OUT(1, 15, 7, RET_SE_NICLK_IO(0, 1)),/* TXEN */
			DATA_OUT(1, 16, 0, RET_SE_NICLK_IO(0, 1)),/* TXD[0] */
			DATA_OUT(1, 16, 1, RET_SE_NICLK_IO(0, 1)),/* TXD[1] */
			DATA_OUT(1, 16, 2, RET_SE_NICLK_IO(0, 1)),/* TXD[2] */
			DATA_OUT(1, 16, 3, RET_SE_NICLK_IO(0, 1)),/* TXD[3] */
			CLOCK_IN(1, 17, 0, RET_NICLK(0, 0)),/* TXCLK */
			DATA_OUT(1, 17, 1, RET_SE_NICLK_IO(0, 1)),/* TXER */
			DATA_OUT(1, 17, 2, RET_BYPASS(0)),/* CRS */
			DATA_OUT(1, 17, 3, RET_BYPASS(0)),/* COL */
			MDIO(1, 17, 4, RET_BYPASS(3000)),/* MDIO */
			CLOCK_IN(1, 17, 5, RET_NICLK(0, 1)),/* MDC */
			DATA_IN(1, 17, 6, RET_SE_NICLK_IO(1000, 1)),/* RXDV */
			DATA_IN(1, 17, 7, RET_SE_NICLK_IO(1000, 1)),/* RX_ER */
			DATA_IN(1, 18, 0, RET_SE_NICLK_IO(1000, 1)),/* RXD[0] */
			DATA_IN(1, 18, 1, RET_SE_NICLK_IO(1000, 1)),/* RXD[1] */
			DATA_IN(1, 18, 2, RET_SE_NICLK_IO(1000, 1)),/* RXD[2] */
			DATA_IN(1, 18, 3, RET_SE_NICLK_IO(1000, 1)),/* RXD[3] */
			CLOCK_IN(1, 19, 0, RET_NICLK(0, 0)),/* RXCLK */
		},
		.sysconfs_num = 3,
		.sysconfs = (struct stm_pad_sysconf []) {
			/* EN_GMAC1 */
			STM_PAD_SYS_CFG_BANK(4, 67, 0, 0, 1),
			/* MIIx_PHY_SEL */
			STM_PAD_SYS_CFG_BANK(4, 23, 2, 4, 0),
			/* ENMIIx */
			STM_PAD_SYS_CFG_BANK(4, 23, 5, 5, 1),
		},
	},
};

struct stx7108_stmmac_priv {
	struct sysconf_field *mac_speed_sel;
	void (*txclk_select)(int txclk_250_not_25_mhz);
	struct stm_pio_control_pad_config pio_config;
} stx7108_stmmac_priv_data[2];

static void stx7108_ethernet_rmii_speed(void *priv, unsigned int speed)
{
	struct stx7108_stmmac_priv *stmmac_priv = priv;
	struct sysconf_field *mac_speed_sel = stmmac_priv->mac_speed_sel;

	sysconf_write(mac_speed_sel, (speed == SPEED_100) ? 1 : 0);
}

static void stx7108_ethernet_gtx_speed(void *priv, unsigned int speed)
{
	struct stx7108_stmmac_priv *stmmac_priv = priv;
	void (*txclk_select)(int txclk_250_not_25_mhz) = stmmac_priv->txclk_select;

	if (txclk_select)
		txclk_select(speed == SPEED_1000);
}

static void stx7108_ethernet_rgmii_gtx_speed(void *priv, unsigned int speed)
{
	struct stx7108_stmmac_priv *stmmac_priv = priv;
	struct stm_pio_control_pad_config *config = &stmmac_priv->pio_config;
	struct plat_stmmacenet_data *plat_data =
		container_of(priv, struct plat_stmmacenet_data, bsp_priv);

	/* TX Clock inversion is not set for 1000Mbps */
	if (speed == SPEED_1000)
		config->retime = RET_NICLK(0, 0);
	else
		config->retime = RET_ICLK(0, 0);

	stm_pad_update_gpio(plat_data->custom_data, "TXCLK",
		stm_pad_gpio_direction_ignored, -1, -1, config);

	stx7108_ethernet_gtx_speed(priv, speed);
}

static struct stmmac_dma_cfg gmac_dma_setting = {
        .pbl = 32,
};

/* STBus Convertor config */
static struct stm_amba_bridge_config stx7108_amba_stmmac_config = {
	.type =	stm_amba_type2,
	.chunks_in_msg = 1,
	.packets_in_chunk = 2,
	.write_posting = stm_amba_write_posting_disabled,
	.max_opcode = stm_amba_opc_LD64_ST64,
	.type2.threshold = 128,
	.type2.sd_config_missing = 1,
	.type2.trigger_mode = stm_amba_stbus_threshold_based,
	.type2.read_ahead = stm_amba_read_ahead_enabled,
};

#define GMAC_AHB2STBUS_BASE         (0x7000 - 4)
static void *stx7108_ethernet_bus_setup(void __iomem *ioaddr,
					struct device *dev, void *data)
{
	struct stm_amba_bridge *amba;

	if (!data) {
		static struct stm_amba_bridge_config *bridge_config;

		bridge_config = &stx7108_amba_stmmac_config;

		if (unlikely(boot_cpu_data.cut_major == 1)) {
			bridge_config->packets_in_chunk = 1;
			bridge_config->type2.threshold = 64;
		}

		amba = stm_amba_bridge_create(dev, ioaddr + GMAC_AHB2STBUS_BASE,
					      bridge_config);
		if (IS_ERR(amba)) {
			dev_err(dev, " Unable to create amba plug\n");
			return NULL;
		}
	} else
		amba = (struct stm_amba_bridge *) data;

	stm_amba_bridge_init(amba);

	return (void *) amba;
}

static struct plat_stmmacenet_data stx7108_ethernet_platform_data[] = {
	{
		.dma_cfg = &gmac_dma_setting,
		.has_gmac = 1,
		.enh_desc = 1,
		.tx_coe = 1,
		.rx_coe = STMMAC_RX_COE_TYPE2,
		.bugged_jumbo =1,
		.pmt = 1,
		.init = &stmmac_claim_resource,
		.exit = &stmmac_release_resource,
		.bsp_priv = &stx7108_stmmac_priv_data[0],
		.bus_setup = &stx7108_ethernet_bus_setup,
	}, {
		.dma_cfg = &gmac_dma_setting,
		.has_gmac = 1,
		.enh_desc = 1,
		.tx_coe = 1,
		.rx_coe = STMMAC_RX_COE_TYPE2,
		.bugged_jumbo =1,
		.pmt = 1,
		.init = &stmmac_claim_resource,
		.exit = &stmmac_release_resource,
		.bsp_priv = &stx7108_stmmac_priv_data[1],
		.bus_setup = &stx7108_ethernet_bus_setup,
	}
};

static struct platform_device stx7108_ethernet_devices[] = {
	{
		.name = "stmmaceth",
		.id = 0,
		.num_resources = 2,
		.resource = (struct resource[]) {
			STM_PLAT_RESOURCE_MEM(0xfda88000, 0x8000),
			STM_PLAT_RESOURCE_IRQ_NAMED("macirq", ILC_IRQ(21), -1),
		},
		.dev.platform_data = &stx7108_ethernet_platform_data[0],
	}, {
		.name = "stmmaceth",
		.id = 1,
		.num_resources = 2,
		.resource = (struct resource[]) {
			STM_PLAT_RESOURCE_MEM(0xfe730000, 0x8000),
			STM_PLAT_RESOURCE_IRQ_NAMED("macirq", ILC_IRQ(23), -1),
		},
		.dev.platform_data = &stx7108_ethernet_platform_data[1],
	}
};

void __init stx7108_configure_ethernet(int port,
		struct stx7108_ethernet_config *config)
{
	static int configured[ARRAY_SIZE(stx7108_ethernet_devices)];
	struct stx7108_ethernet_config default_config;
	struct plat_stmmacenet_data *plat_data;
	struct stx7108_stmmac_priv *priv_data;
	struct stm_pad_config *pad_config;

	BUG_ON(port < 0 || port >= ARRAY_SIZE(stx7108_ethernet_devices));
	BUG_ON(configured[port]++);

	if (!config)
		config = &default_config;

	plat_data = &stx7108_ethernet_platform_data[port];
	priv_data = &stx7108_stmmac_priv_data[port];

	switch (config->interface) {
	case PHY_INTERFACE_MODE_MII:
		pad_config = &stx7108_ethernet_mii_pad_configs[port];
		if (config->ext_clk)
			stm_pad_set_pio_ignored(pad_config, "PHYCLK");
		else
			stm_pad_set_pio_out(pad_config, "PHYCLK", 1 + port);
		break;
	case PHY_INTERFACE_MODE_GMII:
		pad_config = &stx7108_ethernet_gmii_pad_configs[port];
		stm_pad_set_pio_out(pad_config, "PHYCLK", 1 + port);
		plat_data->fix_mac_speed = stx7108_ethernet_gtx_speed;
		priv_data->txclk_select = config->txclk_select;
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
		pad_config = &stx7108_ethernet_rgmii_pad_configs[port];
		stm_pad_set_pio_out(pad_config, "PHYCLK", 1 + port);
		plat_data->fix_mac_speed = stx7108_ethernet_rgmii_gtx_speed;
		priv_data->txclk_select = config->txclk_select;
		break;
	case PHY_INTERFACE_MODE_RMII: /* GMAC1 only tested */
		pad_config = &stx7108_ethernet_rmii_pad_configs[port];
		if (config->ext_clk)
			stm_pad_set_pio_in(pad_config, "PHYCLK", 2 + port);
		else {
			unsigned long phy_clk_rate;
			struct clk *phy_clk = clk_get(NULL, "CLKA_ETH_PHY_1");

			BUG_ON(!phy_clk);
			stm_pad_set_pio_out(pad_config, "PHYCLK", 1 + port);

			phy_clk_rate = 50000000;
			clk_set_rate(phy_clk, phy_clk_rate);
		}
		plat_data->fix_mac_speed = stx7108_ethernet_rmii_speed;
		/* MIIx_MAC_SPEED_SEL */
		if (port == 0)
			priv_data->mac_speed_sel = sysconf_claim(SYS_CFG_BANK2,
					27, 1, 1, "stmmac");
		else
			priv_data->mac_speed_sel = sysconf_claim(SYS_CFG_BANK4,
					23, 1, 1, "stmmac");
		break;
	case PHY_INTERFACE_MODE_REV_MII:
		pad_config = &stx7108_ethernet_reverse_mii_pad_configs[port];
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
	plat_data->phy_addr = config->phy_addr;
	plat_data->mdio_bus_data = config->mdio_bus_data;

	platform_device_register(&stx7108_ethernet_devices[port]);
}



/* USB resources ---------------------------------------------------------- */

static u64 stx7108_usb_dma_mask = DMA_BIT_MASK(32);

#define USB_HOST_PWR		"USB_HOST_PWR"
#define USB_PHY_INDCSHIFT	"USB_PHY_INDCSHIFT"
#define USB_PHY_INEDGECTL	"USB_PHY_INEDGECTL"
#define USB_PWR_ACK		"USB_PWR_ACK"

static int stx7108_usb_init(struct stm_device_state *device_state)
{
	/* USB edge rise and DC shift - STLinux Bugzilla 10991 */
	stm_device_sysconf_write(device_state, USB_PHY_INDCSHIFT, 0);
	stm_device_sysconf_write(device_state, USB_PHY_INEDGECTL, 1);

	return 0;
}

static void stx7108_usb_power(struct stm_device_state *device_state,
		enum stm_device_power_state power)
{
	int i;
	int value = (power == stm_device_power_on) ? 0 : 1;

	stm_device_sysconf_write(device_state, USB_HOST_PWR, value);

	for (i = 5; i; --i) {
		if (stm_device_sysconf_read(device_state, USB_PWR_ACK)
			== value)
			break;
		mdelay(10);
	}
}

/* STBus Convertor config */
static struct stm_amba_bridge_config stx7108_amba_usb_config = {
	STM_DEFAULT_USB_AMBA_PLUG_CONFIG_OLD
};

static struct stm_plat_usb_data stx7108_usb_platform_data[] = {
	[0] = {
		.flags = STM_PLAT_USB_FLAGS_STRAP_8BIT,
		.amba_config = &stx7108_amba_usb_config,
		.device_config = &(struct stm_device_config){
			.init = stx7108_usb_init,
			.power = stx7108_usb_power,
			.sysconfs_num = 4,
			.sysconfs = (struct stm_device_sysconf []) {
				STM_DEVICE_SYS_CFG_BANK(4, 46, 0, 0,
					USB_HOST_PWR),
				STM_DEVICE_SYS_CFG_BANK(4, 44, 0, 0,
					USB_PHY_INDCSHIFT),
				STM_DEVICE_SYS_CFG_BANK(4, 44, 3, 3,
					USB_PHY_INEDGECTL),
				STM_DEVICE_SYS_STA_BANK(4,  2, 0, 0,
					USB_PWR_ACK),
			},
			.pad_config = &(struct stm_pad_config) {
				.gpios_num = 2,
				.gpios = (struct stm_pad_gpio []) {
					/* Overcurrent detection */
					STM_PAD_PIO_IN(23, 6, 1),
					/* USB power enable */
					STM_PAD_PIO_OUT(23, 7, 1),
				},
			},
		},
	},
	[1] = {
		.flags = STM_PLAT_USB_FLAGS_STRAP_8BIT,
		.amba_config = &stx7108_amba_usb_config,
		.device_config = &(struct stm_device_config){
			.init = stx7108_usb_init,
			.power = stx7108_usb_power,
			.sysconfs_num = 4,
			.sysconfs = (struct stm_device_sysconf []) {
				STM_DEVICE_SYS_CFG_BANK(4, 46, 1, 1,
					USB_HOST_PWR),
				STM_DEVICE_SYS_CFG_BANK(4, 44, 1, 1,
					USB_PHY_INDCSHIFT),
				STM_DEVICE_SYS_CFG_BANK(4, 44, 4, 4,
					USB_PHY_INEDGECTL),
				STM_DEVICE_SYS_STA_BANK(4,  2, 1, 1,
					USB_PWR_ACK),
			},
			.pad_config = &(struct stm_pad_config) {
				.gpios_num = 2,
				.gpios = (struct stm_pad_gpio []) {
					/* Overcurrent detection */
					STM_PAD_PIO_IN(24, 0, 1),
					/* USB power enable */
					STM_PAD_PIO_OUT(24, 1, 1),
				},
			},
		},
	},
	[2] = {
		.flags = STM_PLAT_USB_FLAGS_STRAP_8BIT,
		.amba_config = &stx7108_amba_usb_config,
		.device_config = &(struct stm_device_config){
			.init = stx7108_usb_init,
			.power = stx7108_usb_power,
			.sysconfs_num = 4,
			.sysconfs = (struct stm_device_sysconf []) {
				STM_DEVICE_SYS_CFG_BANK(4, 46, 2, 2,
					USB_HOST_PWR),
				STM_DEVICE_SYS_CFG_BANK(4, 44, 2, 2,
					USB_PHY_INDCSHIFT),
				STM_DEVICE_SYS_CFG_BANK(4, 44, 5, 5,
					USB_PHY_INEDGECTL),
				STM_DEVICE_SYS_STA_BANK(4,  2, 2, 2,
					USB_PWR_ACK),
			},
			.pad_config = &(struct stm_pad_config) {
				.gpios_num = 2,
				.gpios = (struct stm_pad_gpio []) {
					/* Overcurrent detection */
					STM_PAD_PIO_IN(24, 2, 1),
					/* USB power enable */
					STM_PAD_PIO_OUT(24, 3, 1),
				},
			},
		},
	},
};

static struct platform_device stx7108_usb_devices[] = {
	[0] = {
		.name = "stm-usb",
		.id = 0,
		.dev = {
			.dma_mask = &stx7108_usb_dma_mask,
			.coherent_dma_mask = DMA_BIT_MASK(32),
			.platform_data = &stx7108_usb_platform_data[0],
		},
		.num_resources = 6,
		.resource = (struct resource[]) {
			STM_PLAT_RESOURCE_MEM_NAMED("wrapper",
					0xfe000000, 0x100),
			STM_PLAT_RESOURCE_MEM_NAMED("ohci",
					0xfe0ffc00, 0x100),
			STM_PLAT_RESOURCE_MEM_NAMED("ehci",
					0xfe0ffe00, 0x100),
			STM_PLAT_RESOURCE_MEM_NAMED("protocol",
					0xfe0fff00, 0x100),
			STM_PLAT_RESOURCE_IRQ_NAMED("ehci", ILC_IRQ(59), -1),
			STM_PLAT_RESOURCE_IRQ_NAMED("ohci", ILC_IRQ(62), -1),
		},
	},
	[1] = {
		.name = "stm-usb",
		.id = 1,
		.dev = {
			.dma_mask = &stx7108_usb_dma_mask,
			.coherent_dma_mask = DMA_BIT_MASK(32),
			.platform_data = &stx7108_usb_platform_data[1],
		},
		.num_resources = 6,
		.resource = (struct resource[]) {
			STM_PLAT_RESOURCE_MEM_NAMED("wrapper",
					0xfe100000, 0x100),
			STM_PLAT_RESOURCE_MEM_NAMED("ohci",
					0xfe1ffc00, 0x100),
			STM_PLAT_RESOURCE_MEM_NAMED("ehci",
					0xfe1ffe00, 0x100),
			STM_PLAT_RESOURCE_MEM_NAMED("protocol",
					0xfe1fff00, 0x100),
			STM_PLAT_RESOURCE_IRQ_NAMED("ehci", ILC_IRQ(60), -1),
			STM_PLAT_RESOURCE_IRQ_NAMED("ohci", ILC_IRQ(63), -1),
		},
	},
	[2] = {
		.name = "stm-usb",
		.id = 2,
		.dev = {
			.dma_mask = &stx7108_usb_dma_mask,
			.coherent_dma_mask = DMA_BIT_MASK(32),
			.platform_data = &stx7108_usb_platform_data[2],
		},
		.num_resources = 6,
		.resource = (struct resource[]) {
			STM_PLAT_RESOURCE_MEM_NAMED("wrapper",
					0xfe200000, 0x100),
			STM_PLAT_RESOURCE_MEM_NAMED("ohci",
					0xfe2ffc00, 0x100),
			STM_PLAT_RESOURCE_MEM_NAMED("ehci",
					0xfe2ffe00, 0x100),
			STM_PLAT_RESOURCE_MEM_NAMED("protocol",
					0xfe2fff00, 0x100),
			STM_PLAT_RESOURCE_IRQ_NAMED("ehci", ILC_IRQ(61), -1),
			STM_PLAT_RESOURCE_IRQ_NAMED("ohci", ILC_IRQ(64), -1),
		},
	},
};

void __init stx7108_configure_usb(int port)
{
	static int osc_initialized;
	static int configured[ARRAY_SIZE(stx7108_usb_devices)];
	struct sysconf_field *sc;
	static void *lmi16reg;
	static int lmi_is_16;

	BUG_ON(port < 0 || port >= ARRAY_SIZE(stx7108_usb_devices));

	BUG_ON(configured[port]++);

	if (!lmi16reg) {
		/* Look at PPCFG ENABLE bit */
		lmi16reg = ioremap(0xfde50084, 4);
		if (lmi16reg) {
			/* Check lmi0 */
			lmi_is_16 = readl(lmi16reg) & 0x1;
			iounmap(lmi16reg);
			/* And now lmi1 */
			lmi16reg = ioremap(0xfde70084, 4);
			if (lmi16reg) {
				lmi_is_16 |= readl(lmi16reg) & 0x1;
				iounmap(lmi16reg);
			}
		}
	}

	if (lmi_is_16)
		stx7108_amba_usb_config.type2.threshold = 16;

	if (!osc_initialized++) {
		/* USB2TRIPPHY_OSCIOK */
		sc = sysconf_claim(SYS_CFG_BANK4, 44, 6, 6, "USB");
		sysconf_write(sc, 1);
	}

	platform_device_register(&stx7108_usb_devices[port]);
}



/* MiPHY resources -------------------------------------------------------- */

#define MAX_PORTS	2

/*
 * NOTE: In 7108 SYSCONF BANK0 the register numbering jumps from 8 to
 * 12, which breaks the sysconf drivers. So when accessing registers
 * 12, 13, 14 we need to manually substract 3, as generic sysconf
 * infrastructure does not takecare of such deviations.
 */
#define BANK0_REG(reg)	(reg - 3)

#define PCIE_BASE		0xfe780000
#define PCIE_UPORT_BASE		(PCIE_BASE + 0x4000)
#define PCIE_UPORT_REG_SIZE	(0xFF)

static __initdata int stx7108_using_uport;
static enum miphy_mode stx7108_miphy_modes[2];
static struct sysconf_field *sc_pcie_mp_select;
static struct sysconf_field *sc_miphy_reset[MAX_PORTS];

static void stx7108_pcie_mp_select(void *data, int port)
{
	BUG_ON(port < 0 || port > 1);
	sysconf_write(sc_pcie_mp_select, port);
}

struct stm_plat_pcie_mp_data stx7108_pcie_mp_platform_data = {
	.style_id = ID_MIPHY365X,
	.miphy_first = 0,
	.miphy_count = 2,
	.miphy_modes = stx7108_miphy_modes,
	.mp_select = stx7108_pcie_mp_select,
};

static struct platform_device stx7108_pcie_mp_device = {
	.name	= "pcie-mp",
	.id	= 0,
	.num_resources = 1,
	.resource = (struct resource[]) {
		STM_PLAT_RESOURCE_MEM_NAMED("pcie-uport", PCIE_UPORT_BASE,
					    PCIE_UPORT_REG_SIZE),
	},
	.dev = {
		.platform_data = &stx7108_pcie_mp_platform_data,
	}
};

/* TAP private data */

static struct stm_tap_sysconf tap_sysconf = {
	.tck = { SYS_CFG_BANK1, 3, 20, 20},
	.tms = { SYS_CFG_BANK1, 3, 23, 23},
	.tdi = { SYS_CFG_BANK1, 3, 22, 22},
	.tdo = { SYS_STA_BANK1, 4, 1, 1},
	.tap_en = { SYS_CFG_BANK1, 3, 13, 13},
	.trstn = { SYS_CFG_BANK1, 3, 21, 21},
};


static struct stm_plat_tap_data stx7108_tap_platform_data = {
	.style_id = ID_MIPHY365X,
	.miphy_first = 0,
	.miphy_count = 2,
	.miphy_modes = stx7108_miphy_modes,
	.tap_sysconf = &tap_sysconf,
};

static struct platform_device stx7108_tap_device = {
	.name	= "stm-miphy-tap",
	.id	= 0,
	.num_resources = 0,
	.dev = {
		.platform_data = &stx7108_tap_platform_data,
	}
};

static int __init stx7108_configure_miphy_uport(void)
{
	struct sysconf_field *sc_miphy1_ref_clk,
			*sc_sata1_hc_reset, *sc_sata_pcie_sel,
			*sc_sata1_hc_srst;

	sc_pcie_mp_select = sysconf_claim(SYS_CFG_BANK4, 70,
					  0, 0, "pcie-mp");
	BUG_ON(!sc_pcie_mp_select);

	/* SATA0_SOFT_RST_N_SATA: sata0_soft_rst_n_sata. */
	/* Useless documentation R us */
	sc_sata1_hc_srst = sysconf_claim(SYS_CFG_BANK4, 45, 4, 4, "MiPHY");
	BUG_ON(!sc_sata1_hc_srst);

	/* SELECT_SATA: select_sata. */
	sc_sata_pcie_sel = sysconf_claim(SYS_CFG_BANK4, 68, 1, 1, "MiPHY");
	BUG_ON(!sc_sata_pcie_sel);

	/* SATAPHY1_OSC_FORCE_EXT: SATAphy1_osc_force_ext. */
	sc_miphy1_ref_clk = sysconf_claim(SYS_CFG_BANK4, 68, 2, 2, "MiPHY");
	BUG_ON(!sc_miphy1_ref_clk);

	/* RESETGEN_CONF0_1: Active low Software Reset for peripherals <31:0>.
	 * NOTE documenation appears to be wrong!
	 * RST_PER_N_30: SATA_2XHOST  */
	sc_sata1_hc_reset = sysconf_claim(SYS_CFG_BANK0,
					  BANK0_REG(12), 30, 30, "SATA");
	BUG_ON(!sc_sata1_hc_reset);

	/* Deassert Soft Reset to SATA0 */
	sysconf_write(sc_sata1_hc_srst, 1);

	/* If the 100MHz xtal for PCIe is present, then the microport interface
	 * will already have a clock, so there is no need to flip to the 30MHz
	 * clock here. If it isn't then we have to switch miphy lane 1 to use
	 * the 30MHz clock, as otherwise we will not be able to talk to lane 0
	 * since the uport interface itself is clocked from lane1
	 */
	if (stx7108_miphy_modes[1] != PCIE_MODE) {
		/* Put MiPHY1 in reset - rst_per_n[32] */
		sysconf_write(sc_miphy_reset[1], 0);
		/* Put SATA1 HC in reset - rst_per_n[30] */
		sysconf_write(sc_sata1_hc_reset, 0);
		/* Now switch to Phy interface to SATA HC not PCIe HC */
		sysconf_write(sc_sata_pcie_sel, 1);
		/* Select the Uport to use MiPHY1 */
		stx7108_pcie_mp_select(NULL, 1);
		/* Take SATA1 HC out of reset - rst_per_n[30] */
		sysconf_write(sc_sata1_hc_reset, 1);
		/* MiPHY1 needs to be using the MiPHY0 reference clock */
		sysconf_write(sc_miphy1_ref_clk, 1);
		/* Take MiPHY1 out of reset - rst_per_n[32] */
		sysconf_write(sc_miphy_reset[1], 1);
	}

	stx7108_using_uport = 1;

	return platform_device_register(&stx7108_pcie_mp_device);
}

static int __init stx7108_configure_miphy_tap(void)
{
	return platform_device_register(&stx7108_tap_device);
}

void __init stx7108_configure_miphy(struct stx7108_miphy_config *config)
{
	int err = 0;

	memcpy(stx7108_miphy_modes, config->modes, sizeof(stx7108_miphy_modes));

	/* RESETGEN_CONF0_1: Active low Software Reset for peripherals <31:0>.
	 * NOTE documenation appears to be wrong!
	 * RST_PER_N_31: SATA PHI 0 */
	sc_miphy_reset[0]  = sysconf_claim(SYS_CFG_BANK0,
					   BANK0_REG(12), 31, 31, "SATA");
	BUG_ON(!sc_miphy_reset[0]);

	/* RESETGEN_CONF0_2: Active low Software Reset for peripherals <63:32>.
	 * NOTE documenation appears to be wrong!
	 * RST_PER_N_32: SATA PHI 1 */
	sc_miphy_reset[1]  = sysconf_claim(SYS_CFG_BANK0,
					   BANK0_REG(13), 0, 0, "SATA");
	BUG_ON(!sc_miphy_reset[1]);

	if (cpu_data->cut_major >= 2 && !config->force_jtag)
		err = stx7108_configure_miphy_uport();
	else
		err = stx7108_configure_miphy_tap();
}



/* SATA resources --------------------------------------------------------- */

static struct sysconf_field *sc_sata_hc_pwr[MAX_PORTS];

static void stx7108_restart_sata(int port)
{
/* This part of the code is executed for ESD recovery.
 * However...
 * It's Not supported on CUT1.0 As we have to reset both Lanes if there
 * is a problem with single lane. As the MiPHY Code for JTAG_IF is
 * not independent of lanes which will potentially results in a
 * recursive resets among lane-0 and lane-1. This behaviour might
 * make both disks unavailable.
 */
	BUG_ON(cpu_data->cut_major < 2);

	/* Reset the SATA Host and MiPHY */
	sysconf_write(sc_sata_hc_pwr[port], 1);
	sysconf_write(sc_miphy_reset[port], 0);

	if (port == 1)
		stx7108_pcie_mp_select(NULL, 1);

	msleep(1);

	sysconf_write(sc_sata_hc_pwr[port], 0);
	sysconf_write(sc_miphy_reset[port], 1);
}

static void stx7108_sata_power(struct stm_device_state *device_state,
		int port, enum stm_device_power_state power)
{
	int value = (power == stm_device_power_on) ? 0 : 1;
	int i;

	sysconf_write(sc_sata_hc_pwr[port], value);

	for (i = 5; i; --i) {
		if (stm_device_sysconf_read(device_state, "SATA_ACK")
				== value)
			break;
		mdelay(10);
	}
}
static void stx7108_sata0_power(struct stm_device_state *device_state,
		enum stm_device_power_state power)
{
	stx7108_sata_power(device_state, 0, power);
}

static void stx7108_sata1_power(struct stm_device_state *device_state,
		enum stm_device_power_state power)
{
	stx7108_sata_power(device_state, 1, power);
}
static struct stm_amba_bridge_config amba_config_sata = {
	STM_DEFAULT_SATA_AMBA_PLUG_CONFIG
};

static struct platform_device stx7108_sata_devices[] = {
	[0] = {
		.name = "sata-stm",
		.id = 0,
		.dev.platform_data = &(struct stm_plat_sata_data) {
			.phy_init = 0,
			.pc_glue_logic_init = 0,
			.only_32bit = 0,
			.oob_wa = 1,
			.port_num = 0,
			.miphy_num = 0,
			.amba_config = &amba_config_sata,
			.device_config = &(struct stm_device_config){
				.power = stx7108_sata0_power,
				.sysconfs_num = 1,
				.sysconfs = (struct stm_device_sysconf []) {
					STM_DEVICE_SYS_STA_BANK(4, 2, 3, 3,
						"SATA_ACK"),
				},
			},
		},
		.num_resources = 3,
		.resource = (struct resource[]) {
			STM_PLAT_RESOURCE_MEM(0xfe768000, 0x1000),
			STM_PLAT_RESOURCE_IRQ_NAMED("hostc", ILC_IRQ(57), -1),
			STM_PLAT_RESOURCE_IRQ_NAMED("dmac", ILC_IRQ(55), -1),
		},
	},
	[1] = {
		.name = "sata-stm",
		.id = 1,
		.dev.platform_data = &(struct stm_plat_sata_data) {
			.phy_init = 0,
			.pc_glue_logic_init = 0,
			.only_32bit = 0,
			.oob_wa = 1,
			.port_num = 1,
			.miphy_num = 1,
			.amba_config = &amba_config_sata,
			.device_config = &(struct stm_device_config){
				.power = stx7108_sata1_power,
				.sysconfs_num = 1,
				.sysconfs = (struct stm_device_sysconf []) {
					STM_DEVICE_SYS_STA_BANK(4, 2, 4, 4,
						"SATA_ACK"),
				},
			},
		},
		.num_resources = 3,
		.resource = (struct resource[]) {
			STM_PLAT_RESOURCE_MEM(0xfe769000, 0x1000),
			STM_PLAT_RESOURCE_IRQ_NAMED("hostc", ILC_IRQ(58), -1),
			STM_PLAT_RESOURCE_IRQ_NAMED("dmac", ILC_IRQ(56), -1),
		},
	}
};

void __init stx7108_configure_sata(int port, struct stx7108_sata_config *config)
{
	struct stm_plat_sata_data *sata_data;
	static int initialized[ARRAY_SIZE(stx7108_sata_devices)];

	sata_data = stx7108_sata_devices[port].dev.platform_data;

	BUG_ON(initialized[port]++);

	sc_sata_hc_pwr[port] = sysconf_claim(SYS_CFG_BANK4, 46,
					     3+port, 3+port, "SATA");
	if (!sc_sata_hc_pwr[port]) {
		BUG();
		return;
	}

	/* If we're using the uPort then we can reset the two lanes
	 * independently. */
	if (stx7108_using_uport)
		sata_data->host_restart = stx7108_restart_sata;

	platform_device_register(&stx7108_sata_devices[port]);
}

/* MMC/SD resources ------------------------------------------------------ */
/* Custom PAD configuration for the MMC Host controller */
#define STX7108_PIO_MMC_CLK_OUT(_port, _pin) \
	{ \
		.gpio = stm_gpio(_port, _pin), \
		.direction = stm_pad_gpio_direction_bidir_pull_up, \
		.function = 1, \
		.name = "MMCCLK", \
		.priv = &(struct stm_pio_control_pad_config) {	\
			.retime = RET_NICLK(0, 0), \
		}, \
	}

#define STX7108_PIO_MMC_OUT(_port, _pin) \
	{ \
		.gpio = stm_gpio(_port, _pin), \
		.direction = stm_pad_gpio_direction_bidir_pull_up, \
		.function = 1, \
	}
#define STX7108_PIO_MMC_BIDIR(_port, _pin) \
	{ \
		.gpio = stm_gpio(_port, _pin), \
		.direction = stm_pad_gpio_direction_out, \
		.function = 1, \
	}
#define STX7108_PIO_MMC_IN(_port, _pin) \
	{ \
		.gpio = stm_gpio(_port, _pin), \
		.direction = stm_pad_gpio_direction_in, \
		.function = 1, \
	}


static struct stm_pad_config stx7108_mmc_pad_config = {
	.gpios_num = 14,
	.gpios = (struct stm_pad_gpio []) {
		STX7108_PIO_MMC_CLK_OUT(1, 0),
		STX7108_PIO_MMC_OUT(1, 1),	/* MMC command */
		STX7108_PIO_MMC_IN(1, 2),	/* MMC Write Protection */
		STX7108_PIO_MMC_IN(1, 3),	/* MMC Card Detect */
		STX7108_PIO_MMC_OUT(1, 4),	/* MMC LED on */
		STX7108_PIO_MMC_OUT(1, 5),	/* MMC Card PWR */
		STX7108_PIO_MMC_BIDIR(0, 0),	/* MMC Data[0]*/
		STX7108_PIO_MMC_BIDIR(0, 1),	/* MMC Data[1]*/
		STX7108_PIO_MMC_BIDIR(0, 2),	/* MMC Data[2]*/
		STX7108_PIO_MMC_BIDIR(0, 3),	/* MMC Data[3]*/
		STX7108_PIO_MMC_BIDIR(0, 4),	/* MMC Data[4]*/
		STX7108_PIO_MMC_BIDIR(0, 5),	/* MMC Data[5]*/
		STX7108_PIO_MMC_BIDIR(0, 6),	/* MMC Data[6]*/
		STX7108_PIO_MMC_BIDIR(0, 7),	/* MMC Data[7]*/
	},
};

static struct stm_mmc_platform_data stx7108_mmc_platform_data = {
	.custom_cfg = &stx7108_mmc_pad_config,
	.nonremovable = false,
	.hc_core_init = -EOPNOTSUPP,
	.gpio_cd = -EINVAL,
};

static struct platform_device stx7108_mmc_device = {
		.name = "sdhci-stm",
		.id = 0,
		.num_resources = 2,
		.resource = (struct resource[]) {
			STM_PLAT_RESOURCE_MEM(0xfdaba000, 0x1000),
			STM_PLAT_RESOURCE_IRQ_NAMED("mmcirq",
						    ILC_IRQ(120), -1),
		},
		.dev = {
			.platform_data = &stx7108_mmc_platform_data,
		}
};

void __init stx7108_configure_mmc(int emmc)
{
	struct stm_mmc_platform_data *plat_data;

	plat_data = &stx7108_mmc_platform_data;

	if (unlikely(emmc))
		plat_data->nonremovable = true;

	platform_device_register(&stx7108_mmc_device);
}
