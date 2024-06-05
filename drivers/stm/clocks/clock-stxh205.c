/*****************************************************************************
 *
 * File name   : clock-stxh205.c
 * Description : Low Level API - HW specific implementation
 *
 * COPYRIGHT (C) 2009-14 STMicroelectronics - All Rights Reserved
 * May be copied or modified under the terms of the GNU General Public
 * License V2 __ONLY__.  See linux/COPYING for more information.
 *
 *****************************************************************************/

/* ----- Modification history (most recent first)----
12/jun/14 fabrice.charpentier@st.com
	  clkgenax_recalc() enhanced to support clocks OFF.
02/jun/14 fabrice.charpentier@st.com
	  Added QFS VCO failed started WA + NRST[3:0] ctrl fixes.
10/apr/14 fabrice.charpentier@st.com
	  Observation functions disabled in Linux mode + some merges.
04/jul/12 ravinder-dlh.singh@st.com
	  Some minor updates/corrections (clkgenax_get_measure,clkgenb/c set_rate & recalc)
12/jun/12 fabrice.charpentier@st.com
	  clkgenax_disable() revisited. Added CLK_ALWAYS_ENABLE flag check.
02/jan/12 ravinder-dlh.singh@st.com
	  Adding support to enable/disable VCC clocks
07/dec/11 ravinder-dlh.singh@st.com
	  Updates based on Linux delivery from Francesco Virlinzi
	  clockgenB observation function updated.
28/nov/11 ravinder-dlh.singh@st.com
	  Integrated updates from Francesco Virlinzi (linux) for clockgenA0/A1
	  Added support for clockgenB (FS0/1) and clockgenC(FS2) based on new FS660c32 algo.
28/sep/11 ravinder-dlh.singh@st.com
	  More updates for ST_H205 add and remove H205_PS1/PS2 stuff
14/jul/11 ravinder-dlh.singh@st.com
	  Updates for ST_H205 based on feedback from Francesco Virlinzi
17/jun/11 ravinder-dlh.singh@st.com
	  Updates for ST_H205 added.
02/may/11 ravinder-dlh.singh@st.com
	  Updates for H205 to align VALID_int with STAPI delivery.
28/march/11 pooja.agarwal@st.com
	  Original version
*/

/* Includes --------------------------------------------------------------- */

#include <linux/stm/clk.h>
#include <linux/stm/stxh205.h>
#include <linux/io.h>
#include <linux/delay.h>

#include "clock-stxh205.h"
#undef SYSCONF
#include "clock-regs-stxh205.h"
#define CLKLLA_SYSCONF_UNIQREGS			1
#include "clock-oslayer.h"
#include "clock-common.h"

/* Co-emulation support:
   CLKLLA_NO_PLL  => RTL (emulation or co-emulation) where only PLL/FSYN are
		     not present. The rest of the logic is there.
 */

static int clkgenax_set_parent(clk_t *clk_p, clk_t *src_p);
static int clkgena0_set_rate(clk_t *clk_p, unsigned long freq);
static int clkgena1_set_rate(clk_t *clk_p, unsigned long freq);
static int clkgenax_set_div(clk_t *clk_p, unsigned long *div_p);
static int clkgenax_identify_parent(clk_t *clk_p);
static int clkgenax_recalc(clk_t *clk_p);
static int clkgenax_enable(clk_t *clk_p);
static int clkgenax_disable(clk_t *clk_p);
static unsigned long clkgenax_get_measure(clk_t *clk_p);
static int clkgenax_init(clk_t *clk_p);

static int clkgenb_set_parent(clk_t *clk_p, clk_t *src_p);
static int clkgenb_set_rate(clk_t *clk_p, unsigned long freq);
static int clkgenc_set_rate(clk_t *clk_p, unsigned long freq);
static int clkgenb_recalc(clk_t *clk_p);
static int clkgenb_fsyn_recalc(clk_t *clk_p);
static int clkgenb_vcc_recalc(clk_t *clk_p);
static int clkgenc_recalc(clk_t *clk_p);
static int clkgenc_fsyn_recalc(clk_t *clk_p);
static int clkgenb_enable(clk_t *clk_p);
static int clkgenc_enable(clk_t *clk_p);
static int clkgenb_disable(clk_t *clk_p);
static int clkgenc_disable(clk_t *clk_p);
static int clkgenb_init(clk_t *clk_p);
static int clkgenc_init(clk_t *clk_p);

/* Boards top input clocks. */
#define SYS_CLKIN		30
#define SYS_CLKALT		30	/* ALT input. Assumed 30Mhz */

_CLK_OPS(clkgena0,
	"Clockgen A0",
	clkgenax_init,
	clkgenax_set_parent,
	clkgena0_set_rate,
	clkgenax_recalc,
	clkgenax_enable,
	clkgenax_disable,
	NULL,
	clkgenax_get_measure,
	"PIO12[0]"        /* Observation point. There is also PIO6[5:6]*/
);
_CLK_OPS(clkgena1,
	"Clockgen A1",
	clkgenax_init,
	clkgenax_set_parent,
	clkgena1_set_rate,
	clkgenax_recalc,
	clkgenax_enable,
	clkgenax_disable,
	NULL,
	clkgenax_get_measure,
	"PIO12[1]"         /* Observation point */
);
_CLK_OPS(clkgenb,
	"Clockgen B/video",
	clkgenb_init,
	clkgenb_set_parent,
	clkgenb_set_rate,
	clkgenb_recalc,
	clkgenb_enable,
	clkgenb_disable,
	NULL,
	NULL,               /* No measure function */
	"PIO10[3]"          /* Observation point */
);
_CLK_OPS(clkgenc,
	"Clockgen C/GP/audio",
	clkgenc_init,
	NULL,
	clkgenc_set_rate,
	clkgenc_recalc,
	clkgenc_enable,
	clkgenc_disable,
	NULL,
	NULL,               /* No measure function */
	NULL                /* No Observation point */
);

/* Physical clocks description */
static clk_t clk_clocks[] = {
/* Top level clocks */
_CLK_FIXED(CLK_SYSIN, SYS_CLKIN * 1000000,
	CLK_RATE_PROPAGATES | CLK_ALWAYS_ENABLED),
_CLK_FIXED(CLK_SYSALT, SYS_CLKALT * 1000000,
	CLK_RATE_PROPAGATES | CLK_ALWAYS_ENABLED),

/* Clockgen A0 */
_CLK_P(CLK_A0_REF, &clkgena0, 0,
	CLK_RATE_PROPAGATES | CLK_ALWAYS_ENABLED,
	&clk_clocks[CLK_SYSIN]),
_CLK_P(CLK_A0_PLL0HS, &clkgena0, 1100000000,
	CLK_RATE_PROPAGATES, &clk_clocks[CLK_A0_REF]),
_CLK_P(CLK_A0_PLL0LS, &clkgena0, 550000000,
	CLK_RATE_PROPAGATES, &clk_clocks[CLK_A0_PLL0HS]),
_CLK_P(CLK_A0_PLL1HS, &clkgena0, 1800000000,
	CLK_RATE_PROPAGATES, &clk_clocks[CLK_A0_REF]),
_CLK_P(CLK_A0_PLL1LS, &clkgena0, 900000000,
	CLK_RATE_PROPAGATES, &clk_clocks[CLK_A0_PLL1HS]),

_CLK(CLK_A0_SH4_ICK,        &clkgena0,    550000000,    CLK_ALWAYS_ENABLED),
_CLK(CLK_A0_HQVDP_PROC,     &clkgena0,    366000000,    0),
_CLK(CLK_A0_IC_CPU,         &clkgena0,    550000000,    CLK_ALWAYS_ENABLED),
_CLK(CLK_A0_LX_DMU_CPU,     &clkgena0,    450000000,    0),
_CLK(CLK_A0_LX_AUD_CPU,     &clkgena0,    550000000,    0),
_CLK(CLK_A0_IC_STNOC,       &clkgena0,    450000000,    CLK_ALWAYS_ENABLED),
_CLK(CLK_A0_GDP_PROC,       &clkgena0,    360000000,    0),
_CLK(CLK_A0_NAND_CTRL,      &clkgena0,    200000000,    0),
_CLK(CLK_A0_IC_REG_LP_ON,   &clkgena0,    100000000,    CLK_ALWAYS_ENABLED),
_CLK(CLK_A0_SECURE,	    &clkgena0,    100000000,    CLK_ALWAYS_ENABLED),
_CLK(CLK_A0_IC_TS_DMA,      &clkgena0,    225000000,    CLK_ALWAYS_ENABLED),
_CLK(CLK_A0_TSOUT_SRC,      &clkgena0,    137000000,    0),
_CLK(CLK_A0_IC_REG_LP_OFF,  &clkgena0,    100000000,    0),
_CLK(CLK_A0_DMU_PREPROC,    &clkgena0,    180000000,    0),
_CLK(CLK_A0_THNS,           &clkgena0,     50000000,    0),
_CLK(CLK_A0_IC_IF,          &clkgena0,    225000000,    CLK_ALWAYS_ENABLED),
_CLK(CLK_A0_PMB,            &clkgena0,    100000000,    0),
_CLK(CLK_A0_SYS_EMISS,      &clkgena0,    100000000,    0),
_CLK(CLK_A0_MASTER,         &clkgena0,     50000000,    0),

/* Clockgen A1 */
_CLK_P(CLK_A1_REF, &clkgena1, 0,
	CLK_RATE_PROPAGATES | CLK_ALWAYS_ENABLED, &clk_clocks[CLK_SYSIN]),
_CLK_P(CLK_A1_PLL0HS, &clkgena1, 1000000000,
	CLK_RATE_PROPAGATES,  &clk_clocks[CLK_A1_REF]),
_CLK_P(CLK_A1_PLL0LS, &clkgena1, 500000000,
	CLK_RATE_PROPAGATES,  &clk_clocks[CLK_A1_PLL0HS]),
_CLK_P(CLK_A1_PLL1HS, &clkgena1, 1600000000,
	CLK_RATE_PROPAGATES, &clk_clocks[CLK_A1_REF]),
_CLK_P(CLK_A1_PLL1LS, &clkgena1, 800000000,
	CLK_RATE_PROPAGATES,  &clk_clocks[CLK_A1_PLL1HS]),

_CLK(CLK_A1_IC_DDRCTRL,     &clkgena1,   333000000,    CLK_ALWAYS_ENABLED),
_CLK(CLK_A1_SLIM_FDMA_0,    &clkgena1,   400000000,    0),
_CLK(CLK_A1_SLIM_FDMA_1,    &clkgena1,   400000000,    0),
_CLK(CLK_A1_BDISP_CK,       &clkgena1,   333000000,    0),
_CLK(CLK_A1_TP,             &clkgena1,   400000000,    0),
_CLK(CLK_A1_IC_COMPO_DISP,  &clkgena1,   200000000,    0),
_CLK(CLK_A1_IC_BDISP,	    &clkgena1,   250000000,    0),
_CLK(CLK_A1_ETH_PHY,        &clkgena1,    50000000,    CLK_ALWAYS_ENABLED),
_CLK(CLK_A1_IC_DMU,         &clkgena1,   266000000,    0),
_CLK(CLK_A1_IC_GMAC,        &clkgena1,   100000000,    0),
_CLK(CLK_A1_PTP_REF_CLK,    &clkgena1,   200000000,    0),
_CLK(CLK_A1_IQI,            &clkgena1,   200000000,    0),
_CLK(CLK_A1_CARD,	        &clkgena1,    50000000,    0),
_CLK(CLK_A1_VDP_PROC,       &clkgena1,   250000000,    0),
_CLK(CLK_A1_SYS_MMC,	    &clkgena1,   200000000,    0),

/* Clockgen B */
_CLK_P(CLK_B_REF, &clkgenb, 30000000,
	CLK_RATE_PROPAGATES | CLK_ALWAYS_ENABLED, &clk_clocks[CLK_SYSIN]),
_CLK_P(CLK_B_FS0_VCO, &clkgenb, 0, CLK_RATE_PROPAGATES, &clk_clocks[CLK_B_REF]),
_CLK_P(CLK_B_VID_HD_LOCAL, &clkgenb, 148500000,
		CLK_RATE_PROPAGATES, &clk_clocks[CLK_B_FS0_VCO]),
_CLK_P(CLK_B_VID_SD_LOCAL, &clkgenb, 108000000,
		CLK_RATE_PROPAGATES, &clk_clocks[CLK_B_FS0_VCO]),
_CLK_P(CLK_B_PCM0, &clkgenb, 27000000,	0, &clk_clocks[CLK_B_FS0_VCO]),
_CLK_P(CLK_B_CLK_27_0, &clkgenb, 27000000, 0, &clk_clocks[CLK_B_FS0_VCO]),

_CLK_P(CLK_B_FS1_VCO, &clkgenb, 0, CLK_RATE_PROPAGATES, &clk_clocks[CLK_B_REF]),
_CLK_P(CLK_B_VID_SD_REMOTE, &clkgenb, 108000000,
		CLK_RATE_PROPAGATES, &clk_clocks[CLK_B_FS1_VCO]),
_CLK_P(CLK_B_PCM2, &clkgenb, 27000000, 0, &clk_clocks[CLK_B_FS1_VCO]),
_CLK_P(CLK_B_CLOCK48, &clkgenb, 48000000, 0, &clk_clocks[CLK_B_FS1_VCO]),
_CLK_P(CLK_B_CLK_27_1, &clkgenb, 27000000, 0, &clk_clocks[CLK_B_FS1_VCO]),

_CLK(CLK_B_PIX_HD,     &clkgenb, 108000000, 0),   /* CLK_OUT_0 */
_CLK(CLK_B_DISP_HD,    &clkgenb, 108000000, 0),   /* CLK_OUT_1 */
_CLK(CLK_B_DISP_SD,    &clkgenb, 108000000, 0),   /* CLK_OUT_3 */
_CLK(CLK_B_DENC_MAIN,  &clkgenb, 108000000, 0),   /* CLK_OUT_4 */
_CLK(CLK_B_GDP1,       &clkgenb, 108000000, 0),   /* CLK_OUT_5 */
_CLK(CLK_B_GDP3,       &clkgenb, 108000000, 0),   /* CLK_OUT_6 */
_CLK(CLK_B_656,        &clkgenb, 108000000, 0),   /* CLK_OUT_7 */
_CLK(CLK_B_PIX_MAIN,   &clkgenb, 108000000, 0),   /* CLK_OUT_9 */
_CLK(CLK_B_PIX_SD,     &clkgenb, 108000000, 0),   /* CLK_OUT_10 */
_CLK(CLK_B_GDP4,       &clkgenb, 108000000, 0),   /* CLK_OUT_13 */
_CLK(CLK_B_PIP,        &clkgenb, 108000000, 0),   /* CLK_OUT_14 */

/* Clockgen Audio */
_CLK_P(CLK_C_REF, &clkgenc, 30000000,
	CLK_RATE_PROPAGATES | CLK_ALWAYS_ENABLED, &clk_clocks[CLK_SYSIN]),
_CLK_P(CLK_C_FS_VCO, &clkgenc, 0, CLK_RATE_PROPAGATES, &clk_clocks[CLK_C_REF]),
_CLK_P(CLK_C_SPDIF, &clkgenc, 27000000, 0, &clk_clocks[CLK_C_FS_VCO]),
_CLK_P(CLK_C_LPM, &clkgenc, 46875, 0, &clk_clocks[CLK_C_FS_VCO]),
_CLK_P(CLK_C_DSS, &clkgenc, 36864, 0, &clk_clocks[CLK_C_FS_VCO]),
_CLK_P(CLK_C_PCM1, &clkgenc, 27000000, 0, &clk_clocks[CLK_C_FS_VCO]),

};


SYSCONF(0, 406, 10, 13);
SYSCONF(0, 406, 14, 14);
SYSCONF(0, 406, 15, 17);
SYSCONF(0, 424, 10, 13);
SYSCONF(0, 424, 14, 14);
SYSCONF(0, 424, 15, 17);
SYSCONF(0, 460, 8, 8);		/* QFS GP RSTN */
SYSCONF(0, 460, 17, 17);	/* QFS VID0 RSTN */
SYSCONF(0, 460, 20, 20);	/* QFS VID1 RSTN */
SYSCONF(0, 462, 0, 3);
SYSCONF(0, 463, 0, 15);
SYSCONF(0, 464, 0, 31);
SYSCONF(0, 465, 0, 31);
SYSCONF(0, 478, 14, 14);
SYSCONF(0, 478, 15, 17);

struct sysconf_field __init
*stxh205_platform_sys_claim(int nr, int lsb, int msb)
{
	return sysconf_claim(SYSCONFG_GROUP(nr), SYSCONF_OFFSET(nr),
		lsb, msb, "clk");
}

int __init stxh205_plat_clk_init(void)
{
	int ret;
#undef call_platform_sys_claim
#define call_platform_sys_claim(_nr, _lsb, _msb)		\
	sys_0_##_nr##_##_lsb##_##_msb =				\
		stxh205_platform_sys_claim(_nr, _lsb, _msb)

	call_platform_sys_claim(406, 10, 13);
	call_platform_sys_claim(406, 14, 14);
	call_platform_sys_claim(406, 15, 17);
	call_platform_sys_claim(424, 10, 13);
	call_platform_sys_claim(424, 14, 14);
	call_platform_sys_claim(424, 15, 17);
	call_platform_sys_claim(460, 8, 8);
	call_platform_sys_claim(460, 17, 17);
	call_platform_sys_claim(460, 20, 20);
	call_platform_sys_claim(462, 0, 3);
	call_platform_sys_claim(463, 0, 15);
	call_platform_sys_claim(464, 0, 31);
	call_platform_sys_claim(465, 0, 31);
	call_platform_sys_claim(478, 14, 14);
	call_platform_sys_claim(478, 15, 17);

	ret = clk_register_table(clk_clocks, CLK_B_REF, 1);

	ret |= clk_register_table(&clk_clocks[CLK_B_REF],
		ARRAY_SIZE(clk_clocks) - CLK_B_REF, 0);

	return 0;
}

/******************************************************************************
CLOCKGEN Ax clocks groups
******************************************************************************/

static inline int clkgenax_get_bank(int clk_id)
{
	return ((clk_id >= CLK_A1_REF) ? 1 : 0);
}

static inline unsigned long clkgenax_get_base_address(int clk_id)
{
	static const unsigned long clkgenax_base[] = {
		CKGA0_BASE_ADDRESS, CKGA1_BASE_ADDRESS };

	return clkgenax_base[clkgenax_get_bank(clk_id)];
}

/* Returns divN_cfg register offset */
static inline unsigned long clkgenax_div_cfg(int clk_src, int clk_idx)
{
	const unsigned short parent_offset[] = {
		CKGA_OSC_DIV0_CFG,	CKGA_PLL0HS_DIV0_CFG,
		CKGA_PLL0LS_DIV0_CFG,	CKGA_PLL1HS_DIV0_CFG,
		CKGA_PLL1LS_DIV0_CFG
	};

	return parent_offset[clk_src] + clk_idx * 4;
}

/* ========================================================================
Name:        clkgenax_get_index
Description: Returns index of given clockgenA clock and source reg infos
Returns:     idx==-1 if error, else >=0
======================================================================== */

static int clkgenax_get_index(int clkid, unsigned long *srcreg, int *shift)
{
	int idx;

	switch (clkid) {
	case CLK_A0_SH4_ICK ... CLK_A0_MASTER:
		idx = clkid - CLK_A0_SH4_ICK;
		break;
	case CLK_A1_IC_DDRCTRL ... CLK_A1_SYS_MMC:
		idx = clkid - CLK_A1_IC_DDRCTRL;
		break;
	default:
		return -1;
	}
	*srcreg = CKGA_CLKOPSRC_SWITCH_CFG + (idx / 16) * 0x4;
	*shift = (idx % 16) * 2;

	return idx;
}

/* ========================================================================
   Name:        clkgenax_set_parent
   Description: Set clock source for clockgenA when possible
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int clkgenax_set_parent(clk_t *clk_p, clk_t *src_p)
{
	unsigned long clk_src, val;
	int idx, shift;
	unsigned long srcreg, base;

	if (!clk_p || !src_p)
		return CLK_ERR_BAD_PARAMETER;

	if (clk_p->id < CLK_A0_SH4_ICK && clk_p->id > CLK_A0_MASTER &&
	    clk_p->id < CLK_A1_IC_DDRCTRL && clk_p->id > CLK_A1_SYS_MMC)
		return CLK_ERR_BAD_PARAMETER;

	/* check if they are on the same bank */
	if (clkgenax_get_bank(clk_p->id) != clkgenax_get_bank(src_p->id))
		return CLK_ERR_BAD_PARAMETER;

	switch (src_p->id) {
	case CLK_A0_REF:
	case CLK_A1_REF:
		clk_src = 0;
		break;
	case CLK_A0_PLL0LS:
	case CLK_A0_PLL0HS:
	case CLK_A1_PLL0LS:
	case CLK_A1_PLL0HS:
		clk_src = 1;
		break;
	case CLK_A0_PLL1LS:
	case CLK_A0_PLL1HS:
	case CLK_A1_PLL1LS:
	case CLK_A1_PLL1HS:
		clk_src = 2;
		break;
	default:
		return CLK_ERR_BAD_PARAMETER;
	}

	idx = clkgenax_get_index(clk_p->id, &srcreg, &shift);
	if (idx == -1)
		return CLK_ERR_BAD_PARAMETER;

	base = clkgenax_get_base_address(clk_p->id);
	val = CLK_READ(base + srcreg) & ~(0x3 << shift);
	val = val | (clk_src << shift);
	CLK_WRITE(base + srcreg, val);
	clk_p->parent = &clk_clocks[src_p->id];

#if defined(CLKLLA_NO_PLL)
	/* If NO PLL means emulation like platform. Then HW may be forced in
	   a specific position preventing SW change */
	clkgenax_identify_parent(clk_p);
#endif

	return clkgenax_recalc(clk_p);
}

/* ========================================================================
   Name:        clkgenax_identify_parent
   Description: Identify parent clock for clockgen A clocks.
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int clkgenax_identify_parent(clk_t *clk_p)
{
	int idx;
	unsigned long src_sel;
	unsigned long srcreg;
	unsigned long base_addr, base_id;
	int shift;

	if (!clk_p)
		return CLK_ERR_BAD_PARAMETER;

	if ((clk_p->id >= CLK_A0_REF && clk_p->id <= CLK_A0_PLL1LS) ||
	    (clk_p->id >= CLK_A1_REF && clk_p->id <= CLK_A1_PLL1LS))
		/* statically initialized */
		return 0;

	/* Which divider to setup ? */
	idx = clkgenax_get_index(clk_p->id, &srcreg, &shift);
	if (idx == -1)
		return CLK_ERR_BAD_PARAMETER;

	/* Identifying source */
	base_addr = clkgenax_get_base_address(clk_p->id);
	base_id = ((clk_p->id >= CLK_A1_REF) ? CLK_A1_REF : CLK_A0_REF);
	src_sel = (CLK_READ(base_addr + srcreg) >> shift) & 0x3;
	switch (src_sel) { /* 0=OSC, 1=PLL0 , 2=PLL1 , 3=STOP */
	case 0:
		clk_p->parent = &clk_clocks[base_id + 0];	/* CLK_Ax_REF */
		break;
	case 1:
		if (idx <= 5)
			/* CLK_Ax_PLL0HS */
			clk_p->parent = &clk_clocks[base_id + 1];
		else	/* CLK_Ax_PLL0LS */
			clk_p->parent = &clk_clocks[base_id + 2];
		break;
	case 2:
		if (idx <= 9)
			/* CLK_Ax_PLL1HS */
			clk_p->parent = &clk_clocks[base_id + 3];
		else	/* CLK_Ax_PLL1LS */
			clk_p->parent = &clk_clocks[base_id + 4];
		break;
	case 3:
		clk_p->parent = NULL;
		clk_p->rate = 0;
		break;
	}

	return 0;
}

/* ========================================================================
   Name:        clkgenax_init
   Description: Read HW status to initialize 'clk_t' structure.
   Returns:     'clk_err_t' error code.
   ======================================================================== */

static int clkgenax_init(clk_t *clk_p)
{
	int err;

	if (!clk_p)
		return CLK_ERR_BAD_PARAMETER;

	err = clkgenax_identify_parent(clk_p);
	if (!err)
		err = clkgenax_recalc(clk_p);

	return err;
}

/* ========================================================================
   Name:        clkgenax_xable_pll
   Description: Enable/disable PLL
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int clkgenax_xable_pll(clk_t *clk_p, int enable)
{
	unsigned long val, base_addr;
	int bit, err = 0;

	if (!clk_p)
		return CLK_ERR_BAD_PARAMETER;

	switch (clk_p->id) {
	case CLK_A0_PLL0LS:	/* all the PLL_LS return */
	case CLK_A0_PLL1LS:	/* CLK_ERR_FEATURE_NOT_SUPPORTED*/
	case CLK_A1_PLL0LS:
	case CLK_A1_PLL1LS:
		return 0;
	case CLK_A0_PLL1HS:
	case CLK_A1_PLL1HS:
		bit = 1;
		break;
	default:
		bit = 0;
	}

	base_addr = clkgenax_get_base_address(clk_p->id);
	val = CLK_READ(base_addr + CKGA_POWER_CFG);
	if (enable)
		val &= ~(1 << bit);
	else
		val |= (1 << bit);
	CLK_WRITE(base_addr + CKGA_POWER_CFG, val);

	if (enable)
		err = clkgenax_recalc(clk_p);
	else
		clk_p->rate = 0;

	return err;
}

/* ========================================================================
   Name:        clkgenax_enable
   Description: Enable clock
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int clkgenax_enable(clk_t *clk_p)
{
	int err;

	if (!clk_p)
		return CLK_ERR_BAD_PARAMETER;

	if (!clk_p->parent)
		/* Unsupported. Init must be called first. */
		return CLK_ERR_BAD_PARAMETER;

	switch (clk_p->id) {
	case CLK_A0_REF:
	case CLK_A1_REF:
		return 0;
	/* all the PLLs */
	case CLK_A0_PLL0HS ... CLK_A0_PLL1LS:
	case CLK_A1_PLL0HS ... CLK_A1_PLL1LS:
		return clkgenax_xable_pll(clk_p, 1);
	default:
		err = clkgenax_set_parent(clk_p, clk_p->parent);
	}
	/* clkgenax_set_parent() is performing also a recalc() */

	return err;
}

/* ========================================================================
   Name:        clkgenax_disable
   Description: Disable clock
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int clkgenax_disable(clk_t *clk_p)
{
	unsigned long val;
	int idx, shift;
	unsigned long srcreg;
	unsigned long base_address;

	if (!clk_p)
		return CLK_ERR_BAD_PARAMETER;

	/* Can this clock be disabled ? */
	if (clk_p->flags & CLK_ALWAYS_ENABLED)
		return 0;

	/* PLL power down */
	switch (clk_p->id) {
	case CLK_A0_REF:
	case CLK_A1_REF:
		return 0;
	case CLK_A0_PLL0HS ... CLK_A0_PLL1LS:
	case CLK_A1_PLL0HS ... CLK_A1_PLL1LS:
		return clkgenax_xable_pll(clk_p, 0);
	}

	idx = clkgenax_get_index(clk_p->id, &srcreg, &shift);
	if (idx == -1)
		return CLK_ERR_BAD_PARAMETER;

	/* Disabling clock */
	base_address = clkgenax_get_base_address(clk_p->id);
	val = CLK_READ(base_address + srcreg) & ~(0x3 << shift);
	val = val | (3 << shift);     /* 3 = STOP clock */
	CLK_WRITE(base_address + srcreg, val);
	clk_p->rate = 0;

	return 0;
}

/* ========================================================================
   Name:        clkgenax_set_div
   Description: Set divider ratio for clockgenA when possible
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int clkgenax_set_div(clk_t *clk_p, unsigned long *div_p)
{
	int idx;
	unsigned long div_cfg = 0;
	unsigned long srcreg, offset;
	int shift;
	unsigned long base_address;

	if (!clk_p || !clk_p->parent)
		return CLK_ERR_BAD_PARAMETER;

	/* Which divider to setup ? */
	idx = clkgenax_get_index(clk_p->id, &srcreg, &shift);
	if (idx == -1)
		return CLK_ERR_BAD_PARAMETER;

	base_address = clkgenax_get_base_address(clk_p->id);

	/* Now according to parent, let's write divider ratio */
	if (clk_p->parent->id >= CLK_A1_REF)
		offset = clkgenax_div_cfg(clk_p->parent->id - CLK_A1_REF, idx);
	else
		offset = clkgenax_div_cfg(clk_p->parent->id - CLK_A0_REF, idx);

	/* Computing divider config */
	div_cfg = (*div_p - 1) & 0x1F;
	CLK_WRITE(base_address + offset, div_cfg);

	return 0;
}

/* ========================================================================
   Name:	clkgenax_recalc
   Description:	Get CKGA programmed clocks frequencies
   Returns:	'clk_err_t' error code
   ======================================================================== */

static int clkgenax_recalc(clk_t *clk_p)
{
	unsigned long data, ratio;
	unsigned long srcreg, offset, base_addr;
	int shift, err, idx;
#if !defined(CLKLLA_NO_PLL)
	unsigned long idf, ndiv;
#endif

	if (!clk_p)
		return CLK_ERR_BAD_PARAMETER;

	/* If no parent, assuming clock is stopped. Sometimes reset default. */
	if (!clk_p->parent) {
		clk_p->rate = 0;
		return 0;
	}

	/* Reading clock programmed value */
	base_addr = clkgenax_get_base_address(clk_p->id);
	switch (clk_p->id) {
	case CLK_A0_REF:  /* Clockgen A reference clock */
	case CLK_A1_REF:  /* Clockgen A reference clock */
		clk_p->rate = clk_p->parent->rate;
		break;
	case CLK_A0_PLL0HS:
	case CLK_A1_PLL0HS:
#if !defined(CLKLLA_NO_PLL)
		ndiv = CLK_READ(base_addr + CKGA_PLL0_REG0_CFG) & 0xff;
		idf = CLK_READ(base_addr + CKGA_PLL0_REG1_CFG) & 0x7;
		return clk_pll1600c45_get_rate(clk_p->parent->rate,
				idf, ndiv, &clk_p->rate);
#else
		if (clk_p->nominal_rate)
			clk_p->rate = clk_p->nominal_rate;
		else
			clk_p->rate = 12121212;
		return 0;
#endif
		return err;
	case CLK_A0_PLL1HS:
	case CLK_A1_PLL1HS:
#if !defined(CLKLLA_NO_PLL)
		ndiv = CLK_READ(base_addr + CKGA_PLL1_REG0_CFG) & 0xff;
		idf = CLK_READ(base_addr + CKGA_PLL1_REG1_CFG) & 0x7;
		return clk_pll1600c45_get_rate(clk_p->parent->rate,
				idf, ndiv, &clk_p->rate);
#else
		if (clk_p->nominal_rate)
			clk_p->rate = clk_p->nominal_rate;
		else
			clk_p->rate = 12121212;
		return 0;
#endif
		return err;
	case CLK_A0_PLL0LS:
	case CLK_A1_PLL0LS:
	case CLK_A0_PLL1LS:
	case CLK_A1_PLL1LS:
		clk_p->rate = clk_p->parent->rate / 2;
		return 0;

	default:
		idx = clkgenax_get_index(clk_p->id, &srcreg, &shift);
		if (idx == -1)
			return CLK_ERR_BAD_PARAMETER;

		/* Now according to parent, let's write divider ratio */
		if (clk_p->parent->id >= CLK_A1_REF)
			offset = clkgenax_div_cfg(clk_p->parent->id -
				CLK_A1_REF, idx);
		else
			offset = clkgenax_div_cfg(clk_p->parent->id -
				CLK_A0_REF, idx);

		data =  CLK_READ(base_addr + offset);
		ratio = (data & 0x1F) + 1;
		clk_p->rate = clk_p->parent->rate / ratio;
		return 0;
	}

#if defined(CLKLLA_NO_PLL)
	if (clk_p->nominal_rate)
		clk_p->rate = clk_p->nominal_rate;
	else
		clk_p->rate = 12121212;
#endif

	return 0;
}

/* ========================================================================
   Name:        clkgena0_set_rate
   Description: Set clock frequency
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int clkgena0_set_rate(clk_t *clk_p, unsigned long freq)
{
	unsigned long div, idf, ndiv, cp;
	int err = 0;
	long deviation, new_deviation;
#if !defined(CLKLLA_NO_PLL)
	unsigned long data;
#endif

	if (!clk_p)
		return CLK_ERR_BAD_PARAMETER;
	if (clk_p->id < CLK_A0_PLL0HS || clk_p->id > CLK_A0_MASTER)
		return CLK_ERR_BAD_PARAMETER;

	/* We need a parent for these clocks */
	if (!clk_p->parent)
		return CLK_ERR_INTERNAL;

	switch (clk_p->id) {
	case CLK_A0_PLL0HS:
		err = clk_pll1600c45_get_params(clk_p->parent->rate,
			freq, &idf, &ndiv, &cp);
		if (err != 0)
			break;
#if !defined(CLKLLA_NO_PLL)
		data = CLK_READ(CKGA0_BASE_ADDRESS + CKGA_PLL0_REG0_CFG)
				& 0xffffff00;
		data |= ndiv;
		CLK_WRITE(CKGA0_BASE_ADDRESS + CKGA_PLL0_REG0_CFG, data);
		data = CLK_READ(CKGA0_BASE_ADDRESS + CKGA_PLL0_REG1_CFG)
				& 0xfffffff8;
		data |= idf;
		CLK_WRITE(CKGA0_BASE_ADDRESS + CKGA_PLL0_REG1_CFG, data);
#endif
		break;
	case CLK_A0_PLL1HS:
		err = clk_pll1600c45_get_params(clk_p->parent->rate, freq,
			&idf, &ndiv, &cp);
		if (err != 0)
			break;
#if !defined(CLKLLA_NO_PLL)
		data = CLK_READ(CKGA0_BASE_ADDRESS + CKGA_PLL1_REG0_CFG)
				& 0xffffff80;
		data |= ndiv;
		CLK_WRITE(CKGA0_BASE_ADDRESS + CKGA_PLL1_REG0_CFG, data);
		data = CLK_READ(CKGA0_BASE_ADDRESS + CKGA_PLL1_REG1_CFG)
				& 0xfffffff8;
		data |= idf;
		CLK_WRITE(CKGA0_BASE_ADDRESS + CKGA_PLL1_REG1_CFG, data);
#endif
		break;
	case CLK_A0_PLL0LS:
	case CLK_A0_PLL1LS:
		return CLK_ERR_FEATURE_NOT_SUPPORTED;
	case CLK_A0_SH4_ICK ... CLK_A0_MASTER:
		div = clk_p->parent->rate / freq;
		deviation = (clk_p->parent->rate / div) - freq;
		new_deviation = (clk_p->parent->rate / (div + 1)) - freq;
		if (new_deviation < 0)
			new_deviation = -new_deviation;
		if (new_deviation < deviation)
			div++;
		err = clkgenax_set_div(clk_p, &div);
		break;
	default:
		return CLK_ERR_BAD_PARAMETER;
	}

	if (!err)
		err = clkgenax_recalc(clk_p);
	return err;
}

/* ========================================================================
   Name:        clkgena1_set_rate
   Description: Set clock frequency
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int clkgena1_set_rate(clk_t *clk_p, unsigned long freq)
{
	unsigned long div, idf, ndiv, cp;
	int err = 0;
	long deviation, new_deviation;
	unsigned long data;

	if (!clk_p)
		return CLK_ERR_BAD_PARAMETER;
	if ((clk_p->id < CLK_A1_PLL0HS) || (clk_p->id > CLK_A1_SYS_MMC))
		return CLK_ERR_BAD_PARAMETER;

	/* We need a parent for these clocks */
	if (!clk_p->parent)
		return CLK_ERR_INTERNAL;

	switch (clk_p->id) {
	case CLK_A1_PLL0HS:
		err = clk_pll1600c45_get_params(clk_p->parent->rate,
			freq, &idf, &ndiv, &cp);
		if (err != 0)
			break;
#if !defined(CLKLLA_NO_PLL)
		data = CLK_READ(CKGA1_BASE_ADDRESS + CKGA_PLL0_REG0_CFG)
				& 0xffffff00;
		data |= ndiv;
		CLK_WRITE(CKGA1_BASE_ADDRESS + CKGA_PLL0_REG0_CFG, data);
		data = CLK_READ(CKGA1_BASE_ADDRESS + CKGA_PLL0_REG1_CFG)
				& 0xfffffff8;
		data |= idf;
		CLK_WRITE(CKGA1_BASE_ADDRESS + CKGA_PLL0_REG1_CFG, data);
#endif
		break;
	case CLK_A1_PLL1HS:
		err = clk_pll1600c45_get_params(clk_p->parent->rate,
			freq, &idf, &ndiv, &cp);
		if (err != 0)
			break;
#if !defined(CLKLLA_NO_PLL)
		data = CLK_READ(CKGA1_BASE_ADDRESS + CKGA_PLL1_REG0_CFG)
				& 0xffffff80;
		data |= ndiv;
		CLK_WRITE(CKGA1_BASE_ADDRESS + CKGA_PLL1_REG0_CFG, data);
		data = CLK_READ(CKGA1_BASE_ADDRESS + CKGA_PLL1_REG1_CFG)
				& 0xfffffff8;
		data |= idf;
		CLK_WRITE(CKGA1_BASE_ADDRESS + CKGA_PLL1_REG1_CFG, data);
#endif
		break;
	case CLK_A1_PLL0LS:
	case CLK_A1_PLL1LS:
		return CLK_ERR_FEATURE_NOT_SUPPORTED;
	case CLK_A1_IC_DDRCTRL ... CLK_A1_SYS_MMC:
		div = clk_p->parent->rate / freq;
		deviation = (clk_p->parent->rate / div) - freq;
		new_deviation = (clk_p->parent->rate / (div + 1)) - freq;
		if (new_deviation < 0)
			new_deviation = -new_deviation;
		if (new_deviation < deviation)
			div++;
		err = clkgenax_set_div(clk_p, &div);
		break;
	default:
		return CLK_ERR_BAD_PARAMETER;
	}

	if (!err)
		err = clkgenax_recalc(clk_p);

	return err;
}

/* ========================================================================
   Name:        clkgenax_get_measure
   Description: Use internal HW feature (when avail.) to measure clock
   Returns:     'clk_err_t' error code.
   ======================================================================== */

static unsigned long clkgenax_get_measure(clk_t *clk_p)
{
	unsigned long src, data, base;
	unsigned long measure;
	int i;

	if (!clk_p)
		return 0;

	switch (clk_p->id) {
	case CLK_A0_SH4_ICK ... CLK_A0_MASTER:
		src = clk_p->id - CLK_A0_SH4_ICK;
		break;
	case CLK_A1_IC_DDRCTRL ... CLK_A1_SYS_MMC:
		src = clk_p->id - CLK_A1_IC_DDRCTRL;
		break;
	default:
		return 0;
	}

	measure = 0;
	base = clkgenax_get_base_address(clk_p->id);

	/* Loading the MAX Count 1000 in 30MHz Oscillator Counter */
	CLK_WRITE(base + CKGA_CLKOBS_MASTER_MAXCOUNT, 0x3E8);
	CLK_WRITE(base + CKGA_CLKOBS_CMD, 3);

	/* Selecting clock to observe */
	CLK_WRITE(base + CKGA_CLKOBS_MUX0_CFG, (1 << 7) | src);

	/* Start counting */
	CLK_WRITE(base + CKGA_CLKOBS_CMD, 0);

	for (i = 0; i < 10; i++) {
		CLK_DELAYMS(10);
		data = CLK_READ(base + CKGA_CLKOBS_STATUS);
		if (data & 1)
			break;	/* IT */
	}
	if (i == 10)
		return 0;

	/* Reading value */
	data = CLK_READ(base + CKGA_CLKOBS_SLAVE0_COUNT);
	measure = 30 * data * 1000;

	CLK_WRITE(base + CKGA_CLKOBS_CMD, 3);

	return measure;
}

/******************************************************************************
QFS VCO checker: checks that VCO is at expected freq
Params: fs, reqFreq (in Mhz)
    fs=0 => VID0 QFS
    fs=1 => VID1 QFS
    fs=2 => GP QFS
Returns: 0=OK, -1=mismatch
******************************************************************************/

static int qfs_check(int fs, unsigned long reqFreq)
{
	unsigned long regVal;
	unsigned long freqReg;
	unsigned long freq;

	/* Program clock measurement */
	CLK_WRITE(CKGB_BASE_ADDRESS + 0xD8, 0x0000001F);
	CLK_WRITE(CKGB_BASE_ADDRESS + 0xD0, 0x00040FC0);
	CLK_WRITE(CKGB_BASE_ADDRESS + 0xD4, 0x0235022B);
	CLK_WRITE(CKGB_BASE_ADDRESS + 0xCC, 0x00019100);
	switch (fs) {
	case 0:
		CLK_WRITE(CKGB_BASE_ADDRESS + 0xDC, 0x00840081);
		break;
	case 1:
		CLK_WRITE(CKGB_BASE_ADDRESS + 0xDC, 0x00880081);
		break;
	case 2:
		CLK_WRITE(CKGB_BASE_ADDRESS + 0xDC, 0x00800081);
		break;
	default:
		return 0;
	}
	CLK_WRITE(CKGB_BASE_ADDRESS + 0xE0, 0x00001000);
	CLK_WRITE(CKGB_BASE_ADDRESS + 0xE4, 0x00000000);

	CLK_WRITE(CKGB_BASE_ADDRESS + 0xCC, 0x00A19101);
	do {
		regVal = CLK_READ(CKGB_BASE_ADDRESS + 0x11C);
	} while ((regVal & 0x80000) == 0);

	CLK_WRITE(CKGB_BASE_ADDRESS + 0xCC, 0x00819101);
	CLK_WRITE(CKGB_BASE_ADDRESS + 0xCC, 0x0081910C);
	freqReg = CLK_READ(CKGB_BASE_ADDRESS + 0x11C) & 0xffff;

	/* Measured freq = freqReg * 30 MHz / 511
	   Tolerance is 1MHz */
	freq = freqReg * 30 / (unsigned long)511;
	if (freq < (reqFreq - 1))
		return -1;
	if (freq > (reqFreq + 1))
		return -1;

	return 0; /* OK: freq is the expected one */
}

/******************************************************************************
CLOCKGEN B (video)
******************************************************************************/

/* ========================================================================
   Name:        clkgenb_fsyn_recalc
   Description: Get CKGC FSYN clocks frequencies function
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int clkgenb_fsyn_recalc(clk_t *clk_p)
{
	int err = 0;
#if !defined(CLKLLA_NO_PLL)
	unsigned long cfg, dig_bit;
	unsigned long pe, md, sdiv, ndiv, nsdiv;
	int channel, bank;
#endif

	if (!clk_p)
		return CLK_ERR_BAD_PARAMETER;

#if !defined(CLKLLA_NO_PLL)
	/* Which FSYN control registers to use ? */
	switch (clk_p->id) {
	case CLK_B_FS0_VCO ... CLK_B_CLK_27_0:
		bank = 0;
		channel = clk_p->id - CLK_B_VID_HD_LOCAL;
		break;
	case CLK_B_FS1_VCO ... CLK_B_CLK_27_1:
		bank = 1;
		channel = clk_p->id - CLK_B_VID_SD_REMOTE;
		break;
	default:
		return CLK_ERR_BAD_PARAMETER;
	}

	dig_bit = 10 + channel;

	/* Checking FSYN analog status */
	cfg = CLK_READ(CKGB_BASE_ADDRESS + CKGB_FS_CFG(bank));
	if ((cfg  & (1 << 14)) != (1 << 14)) {
		/* Analog power down (NPDA) */
		clk_p->rate = 0;
		return 0;
	}

	/* At least analog part (PLL660) is running */
	ndiv = (cfg >> 15) & 0x7;
	if (clk_p->id == CLK_B_FS0_VCO || clk_p->id == CLK_B_FS1_VCO)
		return clk_fs660c32_vco_get_rate(clk_p->parent->rate, ndiv,
					      &clk_p->rate);

	/* Checking FSYN digital part (standby/NSB) */
	if ((cfg & (1 << dig_bit)) == 0) {	/* digital part in standbye */
		clk_p->rate = 0;
		return 0;
	}

	/* FSYN up & running.
	   Computing frequency */
	pe = CLK_READ(CKGB_BASE_ADDRESS + CKGB_FS_PE(bank, channel));
	md = CLK_READ(CKGB_BASE_ADDRESS + CKGB_FS_MD(bank, channel));
	sdiv = CLK_READ(CKGB_BASE_ADDRESS + CKGB_FS_SDIV(bank, channel));
	nsdiv = (CLK_READ(CKGB_BASE_ADDRESS + CKGB_FS_NSDIV(bank)) >>
			(1 + channel)) & 1;
	err = clk_fs660c32_get_rate(clk_p->parent->rate,
				nsdiv, md, pe, sdiv, &clk_p->rate);
#else
	if (clk_p->nominal_rate)
		clk_p->rate = clk_p->nominal_rate;
	else
		clk_p->rate = 12121212;
#endif

	return err;
}

/* ========================================================================
   Name:        clkgenb_vcc_recalc
   Description: Update Video Clock Controller outputs value
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int clkgenb_vcc_recalc(clk_t *clk_p)
{
	int chan;
	unsigned long val;
	static const unsigned char tab1248[] = { 1, 2, 4, 8 };

	if (clk_p->id < CLK_B_PIX_HD || clk_p->id > CLK_B_PIP)
		return CLK_ERR_BAD_PARAMETER;

	chan = clk_p->id - CLK_B_PIX_HD;
	/* Is the channel stopped ? */
	val = (SYSCONF_READ(0, 463, 0, 15) >> chan) & 1;
	if (val)
		clk_p->rate = 0;
	else {
		/* What is the divider ratio ? */
		val = (SYSCONF_READ(0, 465, 0, 31)
				>> (chan * 2)) & 3;
		clk_p->rate = clk_p->parent->rate / tab1248[val];
	}
	return 0;
}

/* ========================================================================
   Name:        clkgenb_recalc
   Description: Get CKGB clocks frequencies function
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int clkgenb_recalc(clk_t *clk_p)
{
	if (!clk_p)
		return CLK_ERR_BAD_PARAMETER;

	switch (clk_p->id) {
	case CLK_B_REF:
		clk_p->rate = clk_p->parent->rate;
		break;
	case CLK_B_FS0_VCO ... CLK_B_CLK_27_1:  /* FS0/FS1 clocks */
		return clkgenb_fsyn_recalc(clk_p);
	case CLK_B_PIX_HD ... CLK_B_PIP:  /* VCC clocks */
		return clkgenb_vcc_recalc(clk_p);
	default:
		return CLK_ERR_BAD_PARAMETER;
	}

	return 0;
}

/* ========================================================================
   Name:        clkgenb_vcc_set_div
   Description: Get Video Clocks Controller clocks divider function
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int clkgenb_vcc_set_div(clk_t *clk_p, unsigned long *div_p)
{
	int chan;
	unsigned long set, val;
	static const unsigned char div_table[] = {
		/* 1  2     3  4     5     6     7  8 */
		   0, 1, 0xff, 2, 0xff, 0xff, 0xff, 3 };

	if (clk_p->id < CLK_B_PIX_HD || clk_p->id > CLK_B_PIP)
		return CLK_ERR_BAD_PARAMETER;

	chan = clk_p->id - CLK_B_PIX_HD;
	if (*div_p < 1 || *div_p > 8)
		return CLK_ERR_BAD_PARAMETER;

	set = div_table[*div_p - 1];
	if (set == 0xff)
		return CLK_ERR_BAD_PARAMETER;

	/* Set SYSTEM_CONFIG465: div_mode, 2bits per channel */
	val = SYSCONF_READ(0, 465, 0, 31);
	val &= ~(3 << (chan * 2));
	val |= set << (chan * 2);
	SYSCONF_WRITE(0, 465, 0, 31, val);

	return 0;
}
/* ========================================================================
   Name:        clkgenb_set_rate
   Description: Set CKGB clocks frequencies
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int clkgenb_set_rate(clk_t *clk_p, unsigned long freq)
{
	unsigned long div;
#if !defined(CLKLLA_NO_PLL)
	unsigned long md, pe, sdiv, ndiv, nsdiv = -1;
	unsigned long val = 0;
	int bank, channel;
	unsigned long save_rate;
#endif

	if (!clk_p)
		return CLK_ERR_BAD_PARAMETER;

#if !defined(CLKLLA_NO_PLL)
	switch (clk_p->id) {
	case CLK_B_FS0_VCO:
		if (clk_fs660c32_vco_get_params(clk_p->parent->rate,
			freq, &ndiv))
			return CLK_ERR_BAD_PARAMETER;
		save_rate = clk_clocks[CLK_B_VID_HD_LOCAL].rate;
		while (1) {
			SYSCONF_WRITE(0, 460, 17, 17, 0); /* Assert RSTN[3:0] */
			SYSCONF_WRITE(0, 424, 14, 14, 0); /* PLL power down */
			SYSCONF_WRITE(0, 424, 15, 17, ndiv);
			SYSCONF_WRITE(0, 424, 14, 14, 1); /* PLL power up */

			/* No PLL lock capability: waiting at least 20us */
			CLK_DELAYMS(1);
			clkgenb_recalc(clk_p);

			/* WA: In some cases the VCO may not be at correct freq.
			   Setting chan0 FS to 148.5Mhz.
			   Checking freq with jitter estimator & looping if
			   required.
			 */
			clkgenb_set_rate(&clk_clocks[CLK_B_VID_HD_LOCAL], 148500000);
			if (!qfs_check(0, 148))
				break;
		}

		/* Program back CLK_B_VID_HD_LOCAL with rate known by
		 * clk framework
		 */
		if (save_rate == 0)
			clk_clocks[CLK_B_VID_HD_LOCAL].rate = 0;
		else
			clkgenc_set_rate(&clk_clocks[CLK_B_VID_HD_LOCAL],
					save_rate);

		break;
	case CLK_B_FS1_VCO:
		if (clk_fs660c32_vco_get_params(clk_p->parent->rate,
			freq, &ndiv))
			return CLK_ERR_BAD_PARAMETER;
		save_rate = clk_clocks[CLK_B_VID_SD_REMOTE].rate;
		while (1) {
			SYSCONF_WRITE(0, 460, 20, 20, 0); /* Assert RSTN[3:0] */
			SYSCONF_WRITE(0, 478, 14, 14, 0); /* PLL power down */
			SYSCONF_WRITE(0, 478, 15, 17, ndiv);
			SYSCONF_WRITE(0, 478, 14, 14, 1); /* PLL power up */

			/* No PLL lock capability: waiting at least 20us */
			CLK_DELAYMS(1);
			clkgenb_recalc(clk_p);

			/* WA: In some cases the VCO may not be at correct freq.
			   Setting chan0 FS to 108Mhz.
			   Checking freq with jitter estimator & looping if
			   required.
			 */
			clkgenb_set_rate(&clk_clocks[CLK_B_VID_SD_REMOTE], 108000000);
			if (!qfs_check(1, 108))
				break;
		}

		/* Program back CLK_B_VID_SD_REMOTE with rate known by
		 * clk framework
		 */
		if (save_rate == 0)
			clk_clocks[CLK_B_VID_SD_REMOTE].rate = 0;
		else
			clkgenb_set_rate(&clk_clocks[CLK_B_VID_SD_REMOTE],
					save_rate);

		break;
	case CLK_B_VID_HD_LOCAL ... CLK_B_CLK_27_0:
	case CLK_B_VID_SD_REMOTE ... CLK_B_CLK_27_1:
		if (clk_fs660c32_dig_get_params(clk_p->parent->rate,
			freq, &nsdiv, &md, &pe, &sdiv))
			return CLK_ERR_BAD_PARAMETER;

		bank = (clk_p->id <= CLK_B_FS1_VCO) ? 0 : 1;
		if (clk_p->id <= CLK_B_FS1_VCO)
			channel = clk_p->id - CLK_B_VID_HD_LOCAL;
		else
			channel = clk_p->id - CLK_B_VID_SD_REMOTE;

		/* Removing digit standby (NSB) */
		val = CLK_READ(CKGB_BASE_ADDRESS + CKGB_FS_CFG(bank));
		val |= 1 << (10 + channel);
		CLK_WRITE(CKGB_BASE_ADDRESS + CKGB_FS_CFG(bank), val);

		/* Removing digit reset (NRST) */
		if (clk_p->id < CLK_B_FS1_VCO)
			SYSCONF_WRITE(0, 460, 17, 17, 1);
		else
			SYSCONF_WRITE(0, 460, 20, 20, 1);

		val = CLK_READ(CKGB_BASE_ADDRESS + CKGB_FS_EN_PRG(bank, channel));
		CLK_WRITE(CKGB_BASE_ADDRESS + CKGB_FS_EN_PRG(bank, channel), val & ~0x1);
		CLK_WRITE(CKGB_BASE_ADDRESS + CKGB_FS_PE(bank, channel), pe);
		CLK_WRITE(CKGB_BASE_ADDRESS + CKGB_FS_MD(bank, channel), md);
		CLK_WRITE(CKGB_BASE_ADDRESS + CKGB_FS_SDIV(bank, channel),
				sdiv);
		val = CLK_READ(CKGB_BASE_ADDRESS + CKGB_FS_NSDIV(bank));
		if (nsdiv)
			val |= (1 << (channel + 1));
		else
			val &= ~(1 << (channel + 1));
		CLK_WRITE(CKGB_BASE_ADDRESS + CKGB_FS_NSDIV(bank), val);
		/* prog enable */
		val = CLK_READ(CKGB_BASE_ADDRESS + CKGB_FS_EN_PRG(bank, channel));
		CLK_WRITE(CKGB_BASE_ADDRESS + CKGB_FS_EN_PRG(bank, channel), val | 0x1);
		CLK_WRITE(CKGB_BASE_ADDRESS + CKGB_FS_EN_PRG(bank, channel), val & ~0x1);
		break;
	case CLK_B_PIX_HD ... CLK_B_SPARE_15:
		/* Video Clock Controller clocks */
		div = clk_best_div(clk_p->parent->rate, freq);
		clkgenb_vcc_set_div(clk_p, &div);
		break;
	default:
		return CLK_ERR_BAD_PARAMETER;
	}
#else
#endif

	/* Recomputing freq from real HW status */
	return clkgenb_recalc(clk_p);
}

/* ========================================================================
   Name:        clkgenb_identify_parent
   Description: Identify parent clock
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int clkgenb_identify_parent(clk_t *clk_p)
{
	unsigned long chan, val;

	if (clk_p->id >= CLK_B_REF && clk_p->id <= CLK_B_CLK_27_1)
		return 0; /* These clocks have static parent */

		/* Clocks from "Video Clock Controller". */
	if ((clk_p->id >= CLK_B_PIX_HD) && (clk_p->id <= CLK_B_PIP)) {
		chan = clk_p->id - CLK_B_PIX_HD;
		val = SYSCONF_READ(0, 464, 0, 31);
		val >>= (chan * 2);
		val &= 0x3;
		/* sel : 00 clk_hd_local,
		 *	 01 clk_sd_local,
		 *	 10 clk_sd_remote,
		 *	 11 not used
		 */
		switch (val) {
		case 0:
			clk_p->parent = &clk_clocks[CLK_B_VID_HD_LOCAL];
			break;
		case 1:
			clk_p->parent = &clk_clocks[CLK_B_VID_SD_LOCAL];
			break;
		case 3:
			clk_p->parent = &clk_clocks[CLK_B_VID_SD_REMOTE];
			break;
		default:
			break;
		}
	}

	/* Other clockgen B clocks are statically initialized
	   thanks to _CLK_P() macro */

	return 0;
}

/* ========================================================================
   Name:        clkgenb_set_parent
   Description: Set parent clock
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int clkgenb_set_parent(clk_t *clk_p, clk_t *parent_p)
{
	unsigned long chan, val, data;

	if (!clk_p)
		return CLK_ERR_BAD_PARAMETER;
	if (clk_p->id < CLK_B_PIX_HD || clk_p->id > CLK_B_PIP)
		return CLK_ERR_BAD_PARAMETER;

	/* Clocks from "Video Clock Controller". */
	/* sel : 00 clk_hd_local,
	 *	 01 clk_sd_local,
	 *	 10 not used,
	 *	 11 clk_sd_remote
	 */
	chan = clk_p->id - CLK_B_PIX_HD;

	switch (parent_p->id) {
	case CLK_B_VID_HD_LOCAL:
		val = 0;
		break;
	case CLK_B_VID_SD_LOCAL:
		val = 1;
		break;
	case CLK_B_VID_SD_REMOTE:
		val = 3;
		break;
	default:
		return CLK_ERR_BAD_PARAMETER;
	}

	data = SYSCONF_READ(0, 464, 0, 31);
	data &= ~(0x3 << (chan * 2));
	data |= (val << (chan * 2));
	SYSCONF_WRITE(0, 464, 0, 31, data);
	clk_p->parent = parent_p;

	return clkgenb_recalc(clk_p);
}

/* ========================================================================
   Name:        clkgenb_init
   Description: Read HW status to initialize 'clk_t' structure.
   Returns:     'clk_err_t' error code.
   ======================================================================== */

static int clkgenb_init(clk_t *clk_p)
{
	int err;

	if (!clk_p)
		return CLK_ERR_BAD_PARAMETER;
	if (clk_p->id < CLK_B_REF || clk_p->id > CLK_B_PIP)
		return CLK_ERR_BAD_PARAMETER;

	err = clkgenb_identify_parent(clk_p);
	if (!err)
		err = clkgenb_recalc(clk_p);

	return err;
}

/* ========================================================================
   Name:        clkgenb_xable_fsyn
   Description: Enable/Disable FSYN. If all channels OFF, FSYN is powered
		down.
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int clkgenb_xable_fsyn(clk_t *clk_p, unsigned long enable)
{
	unsigned long val;
	int bank, channel;

	if (!clk_p)
		return CLK_ERR_BAD_PARAMETER;
	if (clk_p->id < CLK_B_FS0_VCO || clk_p->id > CLK_B_CLK_27_1)
		return CLK_ERR_BAD_PARAMETER;

	/* Which FSYN control registers to use ? */
	bank = (clk_p->id <= CLK_B_CLK_27_0) ? 0 : 1;
	if (clk_p->id <= CLK_B_FS1_VCO)
		channel = clk_p->id - CLK_B_VID_HD_LOCAL;
	else
		channel = clk_p->id - CLK_B_VID_SD_REMOTE;

	val = CLK_READ(CKGB_BASE_ADDRESS + CKGB_FS_CFG(bank));

	switch (clk_p->id) {
	case CLK_B_FS0_VCO:
	case CLK_B_FS1_VCO:
		if (enable) {
			/* Powering up analog part */
			val |= (1 << 14);
			val |= (1 << 0);
		} else {
			/* Powering down analog part */
			val &= ~(1 << 14);
		}
		break;
	case CLK_B_VID_HD_LOCAL ... CLK_B_CLK_27_0:
	case CLK_B_VID_SD_REMOTE ... CLK_B_CLK_27_1:
		if (enable)
			/* Powering up digital part */
			val |= (1 << (10 + channel));
		else
			/* Powering down digital part */
			val &= ~(1 << (10 + channel));
		break;
	default:
		break;
	}

	CLK_WRITE(CKGB_BASE_ADDRESS + CKGB_FS_CFG(bank), val);

	/* Freq recalc required only if a channel is enabled */
	if (enable)
		return clkgenb_fsyn_recalc(clk_p);
	else
		clk_p->rate = 0;
	return 0;
}

/* ========================================================================
   Name:        clkgenb_xable_clock
   Description: Enable/disable clock (Clockgen B)
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int clkgenb_xable_clock(clk_t *clk_p, unsigned long enable)
{
	unsigned long bit;
	int err = 0;

	if (!clk_p)
		return CLK_ERR_BAD_PARAMETER;

	if (clk_p->id >= CLK_B_PIX_HD && clk_p->id <= CLK_B_PIP) {
		/* Clocks from "Video Clock Controller".
		STOP clock controlled thru SYSTEM_CONFIG 463 of bank3 */
		unsigned long tmp = SYSCONF_READ(0, 463, 0, 15);
		bit = clk_p->id - CLK_B_PIX_HD;
		if (enable)
			tmp &= ~(1 << bit);
		else
			tmp |= (1 << bit);
		SYSCONF_WRITE(0, 463, 0, 15, tmp);
	} else
		return CLK_ERR_BAD_PARAMETER;

	if (enable)
		err = clkgenb_recalc(clk_p);
	else
		clk_p->rate = 0;

	return err;
}

/* ========================================================================
   Name:        clkgenb_enable
   Description: Enable clock
   Returns:     'clk_err_t' error code.
   ======================================================================== */

static int clkgenb_enable(clk_t *clk_p)
{
	if (!clk_p)
		return CLK_ERR_BAD_PARAMETER;

	switch (clk_p->id) {
	case CLK_B_REF:
		return 0;
	case CLK_B_FS0_VCO ... CLK_B_CLK_27_0:
	case CLK_B_FS1_VCO ... CLK_B_CLK_27_1:
		return clkgenb_xable_fsyn(clk_p, 1);
	case CLK_B_PIX_HD ... CLK_B_SPARE_15:
		return clkgenb_xable_clock(clk_p, 1);
	default:
		break;
	}

	return CLK_ERR_BAD_PARAMETER;
}

/* ========================================================================
   Name:        clkgenb_disable
   Description: Disable clock
   Returns:     'clk_err_t' error code.
   ======================================================================== */

static int clkgenb_disable(clk_t *clk_p)
{
	if (!clk_p)
		return CLK_ERR_BAD_PARAMETER;

	switch (clk_p->id) {
	case CLK_B_REF:
	case CLK_B_FS0_VCO:
	case CLK_B_FS1_VCO:
		return 0;
	case CLK_B_VID_HD_LOCAL ... CLK_B_CLK_27_0:
	case CLK_B_VID_SD_REMOTE ... CLK_B_CLK_27_1:
		return clkgenb_xable_fsyn(clk_p, 0);
	case CLK_B_PIX_HD ... CLK_B_SPARE_15:
		return clkgenb_xable_clock(clk_p, 0);
	default:
		break;
	}

	return CLK_ERR_BAD_PARAMETER;
}

/******************************************************************************
CLOCKGEN C (audio)
******************************************************************************/

/* ========================================================================
   Name:        clkgenc_fsyn_recalc
   Description: Get CKGC FSYN clocks frequencies function
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int clkgenc_fsyn_recalc(clk_t *clk_p)
{
	int err = 0;
	unsigned long cfg, dig_bit;
	unsigned long pe, md, sdiv, ndiv, nsdiv;
	int channel;

	if (!clk_p)
		return CLK_ERR_BAD_PARAMETER;

#if !defined(CLKLLA_NO_PLL)
	/* Checking FSYN analog status */
	cfg = CLK_READ(CKGC_BASE_ADDRESS + CKGC_FS_CFG);
	if ((cfg  & (1 << 14)) != (1 << 14)) {
		/* Analog power down */
		clk_p->rate = 0;
		return 0;
	}

	/* At least analog part (PLL660) is running */
	ndiv = SYSCONF_READ(0, 406, 15, 17);
	if (clk_p->id == CLK_C_FS_VCO)
		return clk_fs660c32_vco_get_rate(clk_p->parent->rate, ndiv,
					      &clk_p->rate);

	/* Checking FSYN digital status */
	dig_bit = (clk_p->id - CLK_C_SPDIF) + 10;
	if ((cfg & (1 << dig_bit)) == 0 || (cfg & (1 << 0)) == 0) {
		/* digital part in standby or reset */
		clk_p->rate = 0;
		return 0;
	}

	/* FSYN up & running.
	   Computing frequency */
	channel = (clk_p->id - CLK_C_SPDIF);
	pe = CLK_READ(CKGC_BASE_ADDRESS + CKGC_FS_PE(channel));
	md = CLK_READ(CKGC_BASE_ADDRESS + CKGC_FS_MD(channel));
	sdiv = CLK_READ(CKGC_BASE_ADDRESS + CKGC_FS_SDIV(channel));
	nsdiv = (CLK_READ(CKGC_BASE_ADDRESS + CKGC_FS_NSDIV) >>
			(1 + channel)) & 1;
	err = clk_fs660c32_get_rate(clk_p->parent->rate,
				nsdiv, md, pe, sdiv, &clk_p->rate);
#else
	if (clk_p->nominal_rate)
		clk_p->rate = clk_p->nominal_rate;
	else
		clk_p->rate = 12121212;
#endif

	return err;
}

/* ========================================================================
   Name:        clkgenc_recalc
   Description: Get CKGC clocks frequencies function
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int clkgenc_recalc(clk_t *clk_p)
{
	if (!clk_p)
		return CLK_ERR_BAD_PARAMETER;

	switch (clk_p->id) {
	case CLK_C_REF:
		clk_p->rate = clk_p->parent->rate;
		break;
	case CLK_C_FS_VCO ... CLK_C_PCM1:
		return clkgenc_fsyn_recalc(clk_p);
	default:
		return CLK_ERR_BAD_PARAMETER;
	}
	return 0;
}

/* ========================================================================
   Name:        clkgenc_init
   Description: Read HW status to initialize 'clk_t' structure.
   Returns:     'clk_err_t' error code.
   ======================================================================== */

static int clkgenc_init(clk_t *clk_p)
{
	int err;

	if (!clk_p)
		return CLK_ERR_BAD_PARAMETER;

	/* Parents are static. No idenfication required */
	err = clkgenc_recalc(clk_p);

	return err;
}

/* ========================================================================
   Name:        clkgenc_set_rate
   Description: Set CKGC clocks frequencies
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int clkgenc_set_rate(clk_t *clk_p, unsigned long freq)
{
	unsigned long val = 0;
	unsigned long pe, md, sdiv, ndiv, nsdiv = -1;
	int channel;
	unsigned long save_rate;

	if (!clk_p)
		return CLK_ERR_BAD_PARAMETER;

#if !defined(CLKLLA_NO_PLL)
	if (clk_p->id == CLK_C_FS_VCO) {
		if (clk_fs660c32_vco_get_params(clk_p->parent->rate,
			freq, &ndiv))
			return CLK_ERR_BAD_PARAMETER;
		save_rate = clk_clocks[CLK_C_SPDIF].rate;
		while (1) {
			SYSCONF_WRITE(0, 460, 8, 8, 0);		/* Assert RSTN[3:0] */
			SYSCONF_WRITE(0, 406, 14, 14, 0);	/* PLL power down */
			SYSCONF_WRITE(0, 406, 15, 17, ndiv);
			SYSCONF_WRITE(0, 406, 14, 14, 1);	/* PLL power up */

			/* No PLL lock capability: waiting at least 20us */
			CLK_DELAYMS(1);
			clkgenc_recalc(clk_p);

			/* WA: In some cases the VCO may not be at correct freq.
			   Setting chan0 FS to 50Mhz.
			   Checking freq with jitter estimator & looping if
			   required.
			 */
			clkgenc_set_rate(&clk_clocks[CLK_C_SPDIF], 50000000);
			if (!qfs_check(2, 50))
				break;
		}

		/* Program back CLK_C_SPDIF with rate known by
		 * clk framework
		 */
		if (save_rate == 0)
			clk_clocks[CLK_C_SPDIF].rate = 0;
		else
			clkgenc_set_rate(&clk_clocks[CLK_C_SPDIF], save_rate);

		return clkgenc_recalc(clk_p);
	}

	/* Computing FSyn params. Should be common function with FSyn type */
	if (clk_fs660c32_dig_get_params(clk_p->parent->rate,
		freq, &nsdiv, &md, &pe, &sdiv))
		return CLK_ERR_BAD_PARAMETER;

	channel = (clk_p->id - CLK_C_SPDIF);

	/* Removing digit standby (NSB) */
	val = CLK_READ(CKGC_BASE_ADDRESS + CKGC_FS_CFG);
	val |= (1 << (10 + channel));
	CLK_WRITE(CKGC_BASE_ADDRESS + CKGC_FS_CFG, val);

	/* Removing digit reset (RSTN[3:0]) */
	SYSCONF_WRITE(0, 460, 8, 8, 1);

	val = CLK_READ(CKGC_BASE_ADDRESS + CKGC_FS_EN_PRG(channel));
	CLK_WRITE(CKGC_BASE_ADDRESS + CKGC_FS_EN_PRG(channel), val & ~0x1);
	CLK_WRITE(CKGC_BASE_ADDRESS + CKGC_FS_PE(channel), pe);
	CLK_WRITE(CKGC_BASE_ADDRESS + CKGC_FS_MD(channel), md);
	CLK_WRITE(CKGC_BASE_ADDRESS + CKGC_FS_SDIV(channel), sdiv);
	val = CLK_READ(CKGC_BASE_ADDRESS + CKGC_FS_NSDIV);
	if (nsdiv)
		val |= (1 << (channel + 1));
	else
		val &= ~(1 << (channel + 1));
	CLK_WRITE(CKGC_BASE_ADDRESS + CKGC_FS_NSDIV, val);
	/* prog enable */
	val = CLK_READ(CKGC_BASE_ADDRESS + CKGC_FS_EN_PRG(channel));
	CLK_WRITE(CKGC_BASE_ADDRESS + CKGC_FS_EN_PRG(channel), val | 0x1);
	CLK_WRITE(CKGC_BASE_ADDRESS + CKGC_FS_EN_PRG(channel), val & ~0x1);
#endif

	return clkgenc_recalc(clk_p);
}

/* ========================================================================
   Name:        clkgenc_xable_fsyn
   Description: Enable/Disable FSYN. If all channels OFF, FSYN is powered
		down.
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int clkgenc_xable_fsyn(clk_t *clk_p, unsigned long enable)
{
	unsigned long val;
	int channel;

	if (!clk_p)
		return CLK_ERR_BAD_PARAMETER;
	if (clk_p->id < CLK_C_FS_VCO || clk_p->id > CLK_C_PCM1)
		return CLK_ERR_BAD_PARAMETER;

	val = CLK_READ(CKGC_BASE_ADDRESS + CKGC_FS_CFG);

	channel = clk_p->id - CLK_C_SPDIF;

	/* Powering down/up digital part */
	if (enable) {
		switch (clk_p->id) {
		case CLK_C_FS_VCO:
			/* PLL power-up (NPDA) */
			val |= (1 << 14);
			break;
		case CLK_C_SPDIF ... CLK_C_PCM1:
			val |= (1 << (10 + channel));
			val |= (1 << 14) | (1 << 0);
			break;
		}
	} else {
		switch (clk_p->id) {
		case CLK_C_FS_VCO:
			/* PLL power-down (NPDA) */
			val &= ~(1 << 14);
			break;
		case CLK_C_SPDIF ... CLK_C_PCM1:
			val &= ~(1 << (10 + channel));

			if((val & 0x3c00) == 0)
			val &=~(1<<14);
			break;
		}
	}

	CLK_WRITE(CKGC_BASE_ADDRESS + CKGC_FS_CFG, val);

	/* Freq recalc required only if a channel is enabled */
	if (enable)
		return clkgenc_fsyn_recalc(clk_p);
	else
		clk_p->rate = 0;
	return 0;
}

/* ========================================================================
   Name:        clkgenc_enable
   Description: Enable clock
   Returns:     'clk_err_t' error code.
   ======================================================================== */

static int clkgenc_enable(clk_t *clk_p)
{
	switch (clk_p->id) {
	case CLK_C_REF:
		return 0;
	case CLK_C_FS_VCO ... CLK_C_PCM1:
		return clkgenc_xable_fsyn(clk_p, 1);
	default:
		break;
	}

	return CLK_ERR_BAD_PARAMETER;
}

/* ========================================================================
   Name:        clkgenc_disable
   Description: Disable clock
   Returns:     'clk_err_t' error code.
   ======================================================================== */

static int clkgenc_disable(clk_t *clk_p)
{
	switch (clk_p->id) {
	case CLK_C_REF:
		return 0;
	case CLK_C_FS_VCO ... CLK_C_PCM1:
		return clkgenc_xable_fsyn(clk_p, 0);
	default:
		break;
	}

	return CLK_ERR_BAD_PARAMETER;
}
