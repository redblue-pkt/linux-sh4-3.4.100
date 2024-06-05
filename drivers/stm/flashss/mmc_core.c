/*
 * This is the driver to manage the FlashSS MMC sub-system and it is used for
 * configuring the Arasan eMMC host controller Core. This is mandatory
 * before installing the SDHCI driver otherwise the controller will be
 * not fully setup as, for example MMC4.5, remaining to the default setup
 * usually provided to only fix boot requirements on ST platforms.
 *
 * (c) 2013 STMicroelectronics Limited
 * Author: Giuseppe Cavallaro <peppe.cavallaro@st.com>
 * Author: Youssef Triki <youssef.triki@st.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/stm/platform.h>
#include <linux/stm/device.h>
#include <linux/stm/flashss.h>
#include <linux/clk.h>
#include <linux/of.h>
#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#endif
#include "mmc_core.h"

static inline void flashss_sd_gp_output(void __iomem *ioaddr)
{
	writel(FLASHSS_MMC_CORE_GP_OUTPUT_CD,
	       ioaddr + FLASHSS_MMC_CORE_GP_OUTPUT);
}

/**
 * flashss_start_mmc_tuning: program DLL
 * @ioaddr: base address
 * Description: this function is to start the MMC auto-tuning.
 * It calls the flashss_emmc_set_dll to program the DLL and the checks if
 * the DLL procedure is finished.
 */
int flashss_start_mmc_tuning(void __iomem *ioaddr)
{
	unsigned long curr, value;
	unsigned long finish = jiffies + HZ;

	/* Enable synamic tuning */
	flashss_emmc_set_dll();

	/* Check the status */
	do {
		curr = jiffies;
		value = readl(ioaddr + STATUS_R);
		if (value & 0x1)
			return 0;

		cpu_relax();
	} while (!time_after_eq(curr, finish));

	return -EBUSY;
}
EXPORT_SYMBOL_GPL(flashss_start_mmc_tuning);

static void flashss_mmc_dump_register(void __iomem *ioaddr)
{
	int i;

	for (i = 0; i <= 12; i++) {
		int off = FLASHSS_MMC_CORE_CONFIG_1 + (i * 4);
		pr_debug("conf_%d: addr 0x%p  val 0x%x\n", i, ioaddr + off,
			 readl(ioaddr + off));
	}
}

/**
 * flashss_mmc45_core_config / flashss_sd30_core_config: configure the Arasan HC
 * @ioaddr: base address
 * Description: this function is to configure the arasan MMC HC.
 * This should be called when the system starts in case of, on the SoC,
 * it is needed to configure the host controller.
 * This happens on some SoCs, i.e. StiH407, where the MMC0 inside the flashSS
 * needs to be configured as MMC 4.5  or SD3.0 to have full capabilities.
 * W/o these settings the SDHCI could configure and use the embedded controller
 * with limited features.
 */
static void flashss_mmc45_core_config(void __iomem *ioaddr)
{
	pr_info("flashSS: MMC_CORE MMC4.5 Core Configuration\n");

	writel(STM_FLASHSS_MMC_CORE_CONFIG_1,
	       ioaddr + FLASHSS_MMC_CORE_CONFIG_1);
	writel(STM_FLASHSS_MMC45_CORE_CONFIG_2,
	       ioaddr + FLASHSS_MMC_CORE_CONFIG_2);
	writel(STM_FLASHSS_MMC45_CORE_CONFIG_3,
	       ioaddr + FLASHSS_MMC_CORE_CONFIG_3);
	writel(STM_FLASHSS_MMC45_CORE_CONFIG_4,
	       ioaddr + FLASHSS_MMC_CORE_CONFIG_4);
	writel(STM_FLASHSS_MMC45_CORE_CONFIG_5,
	       ioaddr + FLASHSS_MMC_CORE_CONFIG_5);
}

static void flashss_sd30_core_config(void __iomem *ioaddr)
{
	pr_info("MMC_CORE Core Configuration\n");
	pr_info("flashSS: MMC_CORE SD3.0 Core Configuration\n");

	writel(STM_FLASHSS_MMC_CORE_CONFIG_1,
	       ioaddr + FLASHSS_MMC_CORE_CONFIG_1);
	writel(STM_FLASHSS_SD30_CORE_CONFIG_2,
	       ioaddr + FLASHSS_MMC_CORE_CONFIG_2);
	writel(STM_FLASHSS_SD30_CORE_CONFIG_3,
	       ioaddr + FLASHSS_MMC_CORE_CONFIG_3);
	writel(STM_FLASHSS_MMC45_CORE_CONFIG_4,
	       ioaddr + FLASHSS_MMC_CORE_CONFIG_4);
	writel(STM_FLASHSS_MMC45_CORE_CONFIG_5,
	       ioaddr + FLASHSS_MMC_CORE_CONFIG_5);

	flashss_sd_gp_output(ioaddr);
}

/**
 * flashss_mmc43_core_config / flashss_sd20_core_config: configure the Arasan HC
 * @ioaddr: base address
 * Description: this function is to configure the arasan MMC HC.
 * This should be called when the system starts in case of, on the SoC,
 * it is needed to configure the host controller.
 * This happens on some SoCs, i.e. StiH407, where the MMC1
 * needs to be configured as MMC 4.3 or SD2.0 to have full capabilities.
 * W/o these settings the SDHCI could configure and use the embedded controller
 * with limited features.
 */
static void flashss_mmc43_core_config(void __iomem *ioaddr)
{
	pr_info("flashSS: MMC_CORE MMC4.3 Core Configuration\n");

	writel(STM_FLASHSS_MMC_CORE_CONFIG_1,
	       ioaddr + FLASHSS_MMC_CORE_CONFIG_1);
	writel(STM_FLASHSS_MMC43_CORE_CONFIG_2,
	       ioaddr + FLASHSS_MMC_CORE_CONFIG_2);
	writel(STM_FLASHSS_MMC43_CORE_CONFIG_3,
	       ioaddr + FLASHSS_MMC_CORE_CONFIG_3);
	writel(STM_FLASHSS_MMC43_CORE_CONFIG_4,
	       ioaddr + FLASHSS_MMC_CORE_CONFIG_4);
	writel(STM_FLASHSS_MMC43_CORE_CONFIG_5,
	       ioaddr + FLASHSS_MMC_CORE_CONFIG_5);
}

static void flashss_sd20_core_config(void __iomem *ioaddr)
{
	pr_info("flashSS: MMC_CORE SD2.0 Core Configuration\n");

	writel(STM_FLASHSS_MMC_CORE_CONFIG_1,
	       ioaddr + FLASHSS_MMC_CORE_CONFIG_1);
	writel(STM_FLASHSS_SD20_CORE_CONFIG_2,
	       ioaddr + FLASHSS_MMC_CORE_CONFIG_2);
	writel(STM_FLASHSS_SD20_CORE_CONFIG_3,
	       ioaddr + FLASHSS_MMC_CORE_CONFIG_3);
	writel(STM_FLASHSS_MMC43_CORE_CONFIG_4,
	       ioaddr + FLASHSS_MMC_CORE_CONFIG_4);
	writel(STM_FLASHSS_MMC43_CORE_CONFIG_5,
	       ioaddr + FLASHSS_MMC_CORE_CONFIG_5);

	flashss_sd_gp_output(ioaddr);
}

void flashss_mmc_core_config(void __iomem *mmc_ioaddr, enum arasan_spec spec)
{

	switch (spec) {
	case ARASAN_EMMC_4_5:
		flashss_emmc_set_static_delay();
		flashss_mmc45_core_config(mmc_ioaddr);
		break;
	case ARASAN_EMMC_4_3:
		flashss_mmc43_core_config(mmc_ioaddr);
		break;
	case ARASAN_SD_3_0:
		flashss_emmc_set_static_delay();
		flashss_sd30_core_config(mmc_ioaddr);
		break;
	case ARASAN_SD_2_0:
		flashss_sd20_core_config(mmc_ioaddr);
		break;
	default:
		/* In this case, the default settings will be used for the HC
		 * these could program the device to only work in PIO mode
		 * (w/o ADMA or SDMA) and 1bit bus etc...
		 */
		pr_warn("flashSS: do not configure eMMC core\n");
	}
	flashss_mmc_dump_register(mmc_ioaddr);
}
EXPORT_SYMBOL_GPL(flashss_mmc_core_config);
