/*
 * MMC definitions for STMicroelectronics
 *
 * Copyright (C) 2012 STMicroelectronics
 *
 * Author: Giuseppe Cavallaro <peppe.cavallaro@st.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __STM_MMC_H
#define __STM_MMC_H

#include <linux/types.h>
#include <linux/device.h>
#include <linux/mmc/sdhci.h>
#include <linux/stm/amba_bridge.h>
#include <linux/stm/flashss.h>

#define MMC_AHB2STBUS_BASE	 (0x104 - 4)

struct stm_mmc_platform_data {
	void *custom_cfg;
	void *custom_data;

	/* AMBA bridge structure */
	struct stm_amba_bridge_config *amba_config;
	struct stm_amba_bridge *amba_bridge;

	/* Non removable e.g. eMMC */
	unsigned nonremovable;

	/* to call specific STM code for super-speed auto-tuning */
	int stm_mmc_auto_tuning;
	/* to call the mmc core configuration from the flashSS */
	int hc_core_init;
	/* to switch among different voltages */
	int stm_mmc_has_vsense;
	/* card detect irq via gpio line */
	int gpio_cd;
	/* Do not have HISPD bit field in HI-SPEED SD card */
	int stm_no_hispd_bit;
};

#endif /* __STM_MMC_H */
