/*
 * Copyright (C) 2012 STMicroelectronics Limited
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * Code to handle the clock aliases on the STIH407.
 */

#include <linux/init.h>
#include <linux/stm/clk.h>

int __init stih407_plat_clk_alias_init(void)
{
	clk_add_alias("cpu_clk", NULL, "CLK_A9", NULL);
	clk_add_alias("sbc_comms_clk", NULL, "CLK_SYSIN", NULL);
	clk_add_alias("comms_clk", NULL, "CLK_EXT2F_A9", NULL);
	clk_add_alias(NULL, "smp_gt", "CLK_A9_PERIPHS", NULL);
	clk_add_alias(NULL, "smp_twd", "CLK_A9_PERIPHS", NULL);

	/* FDMA clocks */
	clk_add_alias("fdma_slim_clk", "stm-fdma.0", "CLK_FDMA", NULL);
	clk_add_alias("fdma_slim_clk", "stm-fdma.1", "CLK_FDMA", NULL);
	clk_add_alias("fdma_slim_clk", "stm-fdma.2", "CLK_FDMA", NULL);
	clk_add_alias("fdma_hi_clk", "stm-fdma.0", "CLK_EXT2F_A9", NULL);
	clk_add_alias("fdma_hi_clk", "stm-fdma.1", "CLK_TX_ICN_DMU", NULL);
	clk_add_alias("fdma_hi_clk", "stm-fdma.2", "CLK_EXT2F_A9", NULL);
	clk_add_alias("fdma_low_clk", "stm-fdma.0", "CLK_EXT2F_A9", NULL);
	clk_add_alias("fdma_low_clk", "stm-fdma.1", "CLK_TX_ICN_DMU", NULL);
	clk_add_alias("fdma_low_clk", "stm-fdma.2", "CLK_TX_ICN_DISP_0", NULL);
	clk_add_alias("fdma_ic_clk", "stm-fdma.0", "CLK_EXT2F_A9", NULL);
	clk_add_alias("fdma_ic_clk", "stm-fdma.1", "CLK_EXT2F_A9", NULL);
	clk_add_alias("fdma_ic_clk", "stm-fdma.2", "CLK_EXT2F_A9", NULL);

	/* Uniperipheral player clocks */
	clk_add_alias("uni_player_clk", "snd_uni_player.0", "CLK_PCM_0",
			NULL);
	clk_add_alias("uni_player_clk", "snd_uni_player.1", "CLK_PCM_1",
			NULL);
	clk_add_alias("uni_player_clk", "snd_uni_player.2", "CLK_PCM_2",
			NULL);
	clk_add_alias("uni_player_clk", "snd_uni_player.3", "CLK_SPDIFF",
			NULL);
	/* EMI clk */
	clk_add_alias("emi_clk", NULL, "CLK_FLASH_PROMIP", NULL);
	/* NAND BCH clk */
	clk_add_alias("bch_clk", NULL, "CLK_NAND", NULL);
	/* sdhci */
	clk_add_alias(NULL, "sdhci-stm.0", "CLK_MMC_0",  NULL);
	clk_add_alias(NULL, "sdhci-stm.1", "CLK_MMC_1",  NULL);

	/* ETH */
	clk_add_alias(NULL, "stmmaceth.0", "CLK_ICN_SBC", NULL);
	/* gpu */
	clk_add_alias("gpu_clk", NULL, "CLK_ICN_GPU", NULL);

	/* JPEG-Decoder */
	clk_add_alias("c8jpg_dec", NULL, "CLK_JPEGDEC", NULL);
	clk_add_alias("c8jpg_icn", NULL, "CLK_TX_ICN_DMU", NULL);
	clk_add_alias("c8jpg_targ", NULL, "CLK_EXT2F_A9", NULL);

	/* AHCI */
	clk_add_alias("ahci_clk", NULL, "CLK_EXT2F_A9", NULL);

	/* Keyscan */
	clk_add_alias(NULL, "94b0000.keyscan", "CLK_SYSIN", NULL);

	return 0;
}
