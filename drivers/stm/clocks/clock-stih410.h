/*****************************************************************************
 *
 * File name   : clock-stih410.h
 * Description : Low Level API - Clocks identifiers
 *
 * COPYRIGHT (C) 2013 STMicroelectronics - All Rights Reserved
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 *****************************************************************************/

/* LLA version: YYYYMMDD */
#define LLA_VERSION "20140117"

enum {
	/* SOC input clocks */
	CLK_SYSIN,

	/* Clockgen A0 (LMI)*/
	CLK_A0_REF,		/* OSC clock */
	CLK_A0_PLL0,		/* GFG0: PLL3200 PHI0 (FVCOBY2/ODF0) */
	CLK_IC_LMI0,		/* Div0 */

	/* Clockgen C0 (main system clocks) */
	CLK_C0_REF,		/* OSC clock */
	CLK_C0_PLL0,		/* GFG0: PLL3200 PHI0 (FVCOBY2/ODF0) */
	CLK_C0_PLL1,		/* GFG1: PLL3200 PHI0 (FVCOBY2/ODF0) */
	CLK_C0_FS_VCO,		/* GFG2: FS embedded VCOCLK */
	CLK_C0_FS0,		/* FS channel 0 */
	CLK_C0_FS1,		/* FS channel 1 */
	CLK_C0_FS2,		/* FS channel 2 */
	CLK_C0_FS3,		/* FS channel 3 */

	CLK_ICN_GPU,		/* Div0 */
	CLK_FDMA,		/* Div1: FDMA 0/1/2 */
	CLK_NAND,		/* Div2 */
	CLK_HVA,
	CLK_PROC_STFE,
	CLK_TP,
	CLK_RX_ICN_DMU,		/* Div6: and RX_ICN_DISP_0/1 */
	CLK_RX_ICN_HVA,		/* Div7: and RX_ICN_TS */
	CLK_ICN_CPU,		/* Div8 */
	CLK_TX_ICN_DMU,		/* Div9: and TX_ICN_HVA/TS & ICN_COMPO */
	CLK_MMC_0,		/* Div10 */
	CLK_MMC_1,		/* Div11: and PRIV_T1/... */
	CLK_JPEGDEC,
	CLK_EXT2F_A9,		/* Div13: and ICN_REGx/TRACE_A9/PTI_STM */
	CLK_IC_BDISP_0,
	CLK_IC_BDISP_1,
	CLK_PP_DMU,
	CLK_VID_DMU,
	CLK_DSS_LPC,
	CLK_ST231_AUD_0,	/* Div19: and GP_0 */
	CLK_ST231_GP_1,
	CLK_ST231_DMU,
	CLK_ICN_LMI,
	CLK_TX_ICN_DISP_0,	/* Div23: and TX_ICN_DISP_1 */
	CLK_ICN_SBC,
	CLK_STFE_FRC2,
	CLK_ETH_PHY,		/* Div26: */
	CLK_ETH_PHYREF,		/* Div27: not connected */
	CLK_FLASH_PROMIP,	/* Div28: FLASH/PROMIP_CPU/VIDEO/DMU */
	CLK_MAIN_DISP,
	CLK_AUX_DISP,
	CLK_COMPO_DVP,		/* Div31: COMPO & DVP */
	CLK_TX_ICN_HADES,	/* Div32 */
	CLK_RX_ICN_HADES,	/* Div33 */
	CLK_ICN_REG_16,		/* Div34 */
	CLK_PP_HADES,		/* Div35 */
	CLK_CLUST_HADES,	/* Div36 */
	CLK_HWPE_HADES,		/* Div37 */
	CLK_FC_HADES,		/* Div38 */

	CLK_EXT2F_A9_DIV2,	/* CLK_EXT2F_A9 / 2 */

	/* Clockgen D0 (Audio) */
	CLK_D0_REF,
	CLK_D0_FS_VCO,		/* FS embedded VCO clock: not connected */
	CLK_D0_FS0,		/* FS channel 0 */
	CLK_D0_FS1,		/* FS channel 1 */
	CLK_D0_FS2,		/* FS channel 2 */
	CLK_D0_FS3,		/* FS channel 3 */

	CLK_PCM_0,		/* Div0 */
	CLK_PCM_1,		/* Div1 */
	CLK_PCM_2,		/* Div2 */
	CLK_SPDIFF,		/* Div3 */
	CLK_PCMR0_MASTER,	/* Div4: PCM READER */
	CLK_D0_SPARE_5,
	CLK_D0_SPARE_6,
	CLK_D0_SPARE_7,
	CLK_D0_SPARE_8,
	CLK_D0_SPARE_9,
	CLK_D0_SPARE_10,
	CLK_D0_SPARE_11,
	CLK_D0_SPARE_12,
	CLK_D0_SPARE_13,	/* Div13 */

	CLK_USB2_PHY,		/* 'ext_refclkout0' to picophy */

	/* Clockgen D2 (Video display and output stage) */
	CLK_D2_REF,
	CLK_D2_REF1,		/* refclkin[1] but no connection */
	CLK_D2_FS_VCO,		/* FS embedded VCO clock: not connected */
	CLK_D2_FS0,		/* FS channel 0 */
	CLK_D2_FS1,		/* FS channel 1 */
	CLK_D2_FS2,		/* FS channel 2 */
	CLK_D2_FS3,		/* FS channel 3 */

	CLK_TMDSOUT_HDMI,	/* srcclkin0: From HDMI TX PHY 'TMDSCK_OUT' */
	CLK_PIX_HD_P_IN,	/* srcclkin1: From PIX_HD_P pad */

	CLK_13_FROM_D0,		/* je_in[0]: from D0-13 */
	CLK_USB_480,		/* je_in[2]: from 480Mhz USB PLL */
	CLK_USB_UTMI,		/* je_in[5]: from UTMI clock */

	CLK_PIX_MAIN_DISP,	/* Div0 */
	CLK_PIX_PIP,
	CLK_PIX_GDP1,		/* Div2 */
	CLK_PIX_GDP2,
	CLK_PIX_GDP3,		/* Div4 */
	CLK_PIX_GDP4,
	CLK_PIX_AUX_DISP,	/* Div6 */
	CLK_DENC,		/* Div7: DENC & FRC0 */
	CLK_PIX_HDDAC,		/* Div8 */
	CLK_HDDAC,
	CLK_SDDAC,		/* Div10 */
	CLK_PIX_DVO,
	CLK_DVO,		/* Div12 */
	CLK_PIX_HDMI,
	CLK_TMDS_HDMI,		/* Div14: to HDMI TX PHY 'TMDSCK' in */
	CLK_REF_HDMIPHY,	/* Div15: to HDMI TX PHY 'CkPxPLL' */

	/* Clockgen D3 (TransportSS and MCHI) */
	CLK_D3_REF,
	CLK_D3_FS_VCO,		/* FS embedded VCO clock: not connected */
	CLK_D3_FS0,		/* FS channel 0 */
	CLK_D3_FS1,		/* FS channel 1 */
	CLK_D3_FS2,		/* FS channel 2 */
	CLK_D3_FS3,		/* FS channel 3 */

	CLK_STFE_FRC1,		/* Div0 */
	CLK_TSOUT_0,
	CLK_TSOUT_1,		/* Div2 */
	CLK_MCHI,
	CLK_VSENS_COMPO,	/* Div4 */
	CLK_FRC1_REMOTE,
	CLK_LPC_0,		/* Div6 */
	CLK_LPC_1,

	/* CA9 PLL3200 */
	CLK_A9_REF,
	CLK_A9_PHI0,		/* PLL3200 PHI0 (FVCOBY2/ODF0) */
	CLK_A9,			/* CA9 clock */
	CLK_A9_PERIPHS		/* CA9.gt CA9.twd clock */
};
