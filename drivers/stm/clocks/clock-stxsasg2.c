/*****************************************************************************
 *
 * File name   : clock-stxsasg2.c
 * Description : Low Level API - HW specific implementation
 *
 * COPYRIGHT (C) 2009 STMicroelectronics - All Rights Reserved
 * COPYRIGHT (C) 2011 STMicroelectronics - All Rights Reserved
 * May be copied or modified under the terms of the GNU General Public
 * License V2 __ONLY__.  See linux/COPYING for more information.
 *
 *****************************************************************************/

/* Compilation flags
   CLKLLA_NO_PLL     => Emulation/co-emulation case. PLL/FSYN are not present.
			The rest of the logic is there.
   CLKLLA_NO_MEASURE => disable HW measure capability. Always returns 0.
 */

/* ----- Modification history (most recent first)----
10/may/13 Fabrice Charpentier
	  Now using Linux sysconf API + emulation for non Linux.
02/may/13 fabrice.charpentier@st.com
	  Recalc functions moved to remove prototypes.
	  clockgen B: NSDIV frozen for audio clocks.
07/feb/13 fabrice.charpentier@st.com
	  Recalc bug fix for clockgens Ax.
12/nov/12 francesco.virlinzi@st.com
	  Clockgen Ax & B observations functions bug fixes.
05/nov/12 francesco.virlinzi@st.com
	  Clockgen B bug fix.
29/oct/12 fabrice.charpentier@st.com
	  FS216 & FS432 API change + NSDIV support.
25/oct/12 fabrice.charpentier@st.com
	  Clockgen C: added TMDS_FROMPHY. New fix for observation.
24/oct/12 fabrice.charpentier@st.com
	  Clockgen C obs bug fix. clkgenc_vcc_xable() fix.
22/oct/12 fabrice.charpentier@st.com
	  FS B/C/D bug fixes for Linux. Added clkgenc_vcc_xable()
24/sep/12 fabrice.charpentier@st.com
	  New clock common API applied on PLLs.
10/sep/12 francesco.virlinzi@st.com
	  Linux cleanup
08/aug/12 fabrice.charpentier@st.com
	  clkgenax_xable_pll() + clkgenax_enable() bug fixes.
	  Clockgen C NSDIV bug fix.
13/jul/12 fabrice.charpentier@st.com
	  Some fixes with "parent_rate".
11/jul/12 fabrice.charpentier@st.com, francesco.virlinzi@st.com
	  Several updates + bug fixes.
28/may/12 fabrice.charpentier@st.com
	  Several updates.
22/mar/12 fabrice.charpentier@st.com
	  Preliminary version
*/

/* Includes --------------------------------------------------------------- */

#include <linux/io.h>
#include <linux/delay.h>

#include <linux/stm/clk.h>

#include "clock-stxsasg2.h"
#include "clock-regs-stxsasg2.h"
#include "clock-oslayer.h"
#include "clk-common.h"

/* Sysconf fields declarations */
struct fsynth_cfg_sysconf {
	struct sysconf_field *nsb;
	struct sysconf_field *ndiv;
	struct sysconf_field *bw_sel;
	struct sysconf_field *nsdiv3;
	struct sysconf_field *npda;
};

static struct fsynth_cfg_sysconf clockgenb_cfg_fs[2];

struct fsynth_sysconf {
	struct sysconf_field *md;
	struct sysconf_field *pe;
	struct sysconf_field *sdiv;
	struct sysconf_field *nsdiv;
	struct sysconf_field *prog_en;
};

/* Clockgen B: Sysconf from 1558 to 1573 & 2562 to 2577 */
static struct fsynth_sysconf clockgenb_fs[2][4];
/* Clockgen C: Sysconf from 2501 to 2516 */
static struct fsynth_sysconf clockgenc_fs[4];
/* Clockgen D: Sysconf from 1505 to 1520 */
static struct fsynth_sysconf clockgend_fs[4];

SYSCONF_DECLAREU(1504, 0, 17);
SYSCONF_DECLAREU(2500, 0, 17);
SYSCONF_DECLAREU(2555, 0, 15);
SYSCONF_DECLAREU(2556, 0, 31);
SYSCONF_DECLAREU(2557, 0, 31);
SYSCONF_DECLAREU(2558, 0, 0);

static void *cga_base[2];
static void *cgb_base;
static void *cgc_base;
static void *cgd_base;

/* Prototypes */
static struct clk clk_clocks[];

/******************************************************************************
CLOCKGEN Ax clocks groups
******************************************************************************/

static inline int clkgenax_get_bank(int clk_id)
{
	return ((clk_id >= CLK_S_A1_REF) ? 1 : 0);
}

/* Returns corresponding clockgen Ax base address for 'clk_id' */
static inline void *clkgenax_get_base_address(int clk_id)
{
	return cga_base[clkgenax_get_bank(clk_id)];
}

/* ========================================================================
Name:	clkgenax_get_index
Description: Returns index of given clockgenA clock and source reg infos
Returns:     idx==-1 if error, else >=0
======================================================================== */

static int clkgenax_get_index(int clkid, unsigned long *srcreg, int *shift)
{
	int idx;

	if (clkid >= CLK_S_FDMA_0 && clkid <= CLK_S_A0_SPARE_17)
		idx = clkid - CLK_S_FDMA_0;
	else if (clkid >= CLK_S_ADP_WC_STAC && clkid <= CLK_S_TST_MVTAC_SYS)
		idx = clkid - CLK_S_ADP_WC_STAC;
	else
		return -1;

	*srcreg = CKGA_CLKOPSRC_SWITCH_CFG + (idx / 16) * 0x10;
	*shift = (idx % 16) * 2;

	return idx;
}

/* ========================================================================
   Name:	clkgenax_recalc
   Description: Get CKGA programmed clocks frequencies
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int clkgenax_recalc(struct clk *clk_p)
{
	unsigned long data, ratio;
	unsigned long srcreg, offset;
	unsigned long parent_rate;
	void *base_address;
	int shift, idx;
	struct stm_pll pll0 = {
		.type = stm_pll1600c65,
	};
	struct stm_pll pll1 = {
		.type = stm_pll800c65,
	};

	if (!clk_p)
		return CLK_ERR_BAD_PARAMETER;

	/* If no parent, assuming clock is stopped. Sometimes reset default. */
	if (!clk_p->parent) {
		clk_p->rate = 0;
		return 0;
	}
	parent_rate = clk_get_rate(clk_get_parent(clk_p));

	/* Reading clock programmed value */
	base_address = clkgenax_get_base_address(clk_p->id);
	switch (clk_p->id) {
	case CLK_S_A0_REF:  /* Clockgen A reference clock */
	case CLK_S_A1_REF:  /* Clockgen A reference clock */
		clk_p->rate = clk_p->parent->rate;
		break;

	case CLK_S_A0_PLL0HS:
	case CLK_S_A1_PLL0HS:
#if !defined(CLKLLA_NO_PLL)
		data = CLK_READ(base_address + CKGA_PLL0_CFG);
		if (data & (1 << 19)) {
			clk_p->rate = 0; /* PLL in power down state */
			return 0;
		}
		pll0.mdiv = data & 0x7;
		pll0.ndiv = (data >> 8) & 0xff;
		return stm_clk_pll_get_rate(clk_p->parent->rate,
				&pll0, &clk_p->rate);
#else
		if (clk_p->nominal_rate)
			clk_p->rate = clk_p->nominal_rate;
		else
			clk_p->rate = 12121212;
		return 0;
#endif
	case CLK_S_A0_PLL0LS:
	case CLK_S_A1_PLL0LS:
		clk_p->rate = parent_rate / 2;
		return 0;
	case CLK_S_A0_PLL1:
	case CLK_S_A1_PLL1:
#if !defined(CLKLLA_NO_PLL)
		data = CLK_READ(base_address + CKGA_PLL1_CFG);
		if (data & (1 << 19)) {
			clk_p->rate = 0; /* PLL in power down state */
			return 0;
		}
		pll1.mdiv = data & 0xff;
		pll1.ndiv = (data >> 8) & 0xff;
		pll1.pdiv = (data >> 16) & 0x7;
		return stm_clk_pll_get_rate(clk_p->parent->rate,
				&pll1, &clk_p->rate);
#else
		if (clk_p->nominal_rate)
			clk_p->rate = clk_p->nominal_rate;
		else
			clk_p->rate = 12121212;
		return 0;
#endif

	default:
		idx = clkgenax_get_index(clk_p->id, &srcreg, &shift);
		if (idx == -1)
			return CLK_ERR_BAD_PARAMETER;

		/* Now according to source, let's get divider ratio */
		if (clk_p->parent->id >= CLK_S_A1_REF)
			offset = CKGA_SOURCE_CFG(clk_p->parent->id -
					CLK_S_A1_REF);
		else
			offset = CKGA_SOURCE_CFG(clk_p->parent->id -
					CLK_S_A0_REF);
		data =  CLK_READ(base_address + offset + (4 * idx));
		ratio = (data & 0x1F) + 1;
		clk_p->rate = parent_rate / ratio;
		break;
	}

	return 0;
}

/* ========================================================================
   Name:	clkgenax_set_parent
   Description: Set clock source for clockgenA when possible
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int clkgenax_set_parent(struct clk *clk_p, struct clk *src_p)
{
	unsigned long clk_src, val;
	int idx, shift;
	unsigned long srcreg;
	void *base;

	if (!clk_p || !src_p)
		return CLK_ERR_BAD_PARAMETER;

	/* check if they are on the same bank */
	if (clkgenax_get_bank(clk_p->id) != clkgenax_get_bank(src_p->id))
		return CLK_ERR_BAD_PARAMETER;

	switch (src_p->id) {
	case CLK_S_A0_REF:
	case CLK_S_A1_REF:
		clk_src = 0;
		break;
	case CLK_S_A0_PLL0LS:
	case CLK_S_A0_PLL0HS:
	case CLK_S_A1_PLL0LS:
	case CLK_S_A1_PLL0HS:
		clk_src = 1;
		break;
	case CLK_S_A0_PLL1:
	case CLK_S_A1_PLL1:
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

	return clkgenax_recalc(clk_p);
}

/* ========================================================================
   Name:	clkgenax_identify_parent
   Description: Identify parent clock for clockgen A clocks.
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int clkgenax_identify_parent(struct clk *clk_p)
{
	int idx;
	unsigned long src_sel;
	unsigned long srcreg;
	unsigned long base_id;
	void *base_addr;
	int shift;

	if (!clk_p)
		return CLK_ERR_BAD_PARAMETER;

	/* Statically initialized clocks */
	if ((clk_p->id >= CLK_S_A0_REF && clk_p->id <= CLK_S_A0_PLL1) ||
	    (clk_p->id >= CLK_S_A1_REF && clk_p->id <= CLK_S_A1_PLL1))
		return 0;

	/* Which divider to setup ? */
	idx = clkgenax_get_index(clk_p->id, &srcreg, &shift);
	if (idx == -1)
		return CLK_ERR_BAD_PARAMETER;

	/* Identifying source */
	base_addr = clkgenax_get_base_address(clk_p->id);
	base_id = ((clk_p->id >= CLK_S_A1_REF) ? CLK_S_A1_REF : CLK_S_A0_REF);
	src_sel = (CLK_READ(base_addr + srcreg) >> shift) & 0x3;
	switch (src_sel) {
	case 0:
		clk_p->parent = &clk_clocks[base_id + 0];	/* CLKAx_REF */
		break;
	case 1:
		if (idx <= 3)
			/* CLKAx_PLL0HS */
			clk_p->parent = &clk_clocks[base_id + 1];
		else	/* CLKAx_PLL0LS */
			clk_p->parent = &clk_clocks[base_id + 2];
		break;
	case 2:
		clk_p->parent = &clk_clocks[base_id + 3];	/* CLKAx_PLL1 */
		break;
	case 3:
		clk_p->parent = NULL;
		clk_p->rate = 0;
		break;
	}

	return 0;
}

/* ========================================================================
   Name:	clkgenax_init
   Description: Read HW status to initialize 'clk' structure.
   Returns:     'clk_err_t' error code.
   ======================================================================== */

static int clkgenax_init(struct clk *clk_p)
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
   Name:	clkgenax_xable_pll
   Description: Enable/disable PLL
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int clkgenax_xable_pll(struct clk *clk_p, int enable)
{
	unsigned long val;
	void *base_addr;
	int bit, err = 0;

	if (!clk_p)
		return CLK_ERR_BAD_PARAMETER;

	switch (clk_p->id) {
	case CLK_S_A0_PLL0LS:
	case CLK_S_A1_PLL0LS:
		return 0; /* always on */
	case CLK_S_A0_PLL1:
	case CLK_S_A1_PLL1:
		bit = 1;
		break;
	case CLK_S_A0_PLL0HS:
	case CLK_S_A1_PLL0HS:
		bit = 0;
		break;
	default:
		return CLK_ERR_BAD_PARAMETER;
	}

#if !defined(CLKLLA_NO_PLL)

	base_addr = clkgenax_get_base_address(clk_p->id);
	val = CLK_READ(base_addr + CKGA_POWER_CFG);
	if (enable)
		val &= ~(1 << bit);
	else
		val |= (1 << bit);
	CLK_WRITE(base_addr + CKGA_POWER_CFG, val);

#endif

	if (enable)
		err = clkgenax_recalc(clk_p);
	else
		clk_p->rate = 0;

	return err;
}

/* ========================================================================
   Name:	clkgenax_enable
   Description: Enable clock
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int clkgenax_enable(struct clk *clk_p)
{
	int err;
	struct clk *parent_clk;

	if (!clk_p)
		return CLK_ERR_BAD_PARAMETER;

	switch (clk_p->id) {
	case CLK_S_A0_REF:
	case CLK_S_A1_REF:
		return 0;
	case CLK_S_A0_PLL0HS ... CLK_S_A0_PLL1:
	case CLK_S_A1_PLL0HS ... CLK_S_A1_PLL1:
		/* PLL power up */
		return clkgenax_xable_pll(clk_p, 1);
	default:
		break;
	}

	/* Enabling means there setting the parent clock instead of "off".
	   If parent is undefined, let's select oscillator as default */
	parent_clk = clk_p->parent;
	if (!parent_clk)
		parent_clk = (clk_p->id >= CLK_S_A1_REF) ?
			     &clk_clocks[CLK_S_A1_REF] : &clk_clocks[CLK_S_A0_REF];
	err = clkgenax_set_parent(clk_p, parent_clk);
	/* clkgenax_set_parent() is performing also a recalc() */

	return err;
}

/* ========================================================================
   Name:	clkgenax_disable
   Description: Disable clock
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int clkgenax_disable(struct clk *clk_p)
{
	unsigned long val;
	int idx, shift;
	unsigned long srcreg;
	void *base_address;

	if (!clk_p)
		return CLK_ERR_BAD_PARAMETER;

	switch (clk_p->id) {
	case CLK_S_A0_REF:
	case CLK_S_A1_REF:
		return 0;
	case CLK_S_A0_PLL0HS ... CLK_S_A0_PLL1:
	case CLK_S_A1_PLL0HS ... CLK_S_A1_PLL1:
		/* PLL power down */
		return clkgenax_xable_pll(clk_p, 0);
	default:
		break;
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
   Name:	clkgenax_set_div
   Description: Set divider ratio for clockgenA when possible
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int clkgenax_set_div(struct clk *clk_p, unsigned long *div_p)
{
	int idx;
	unsigned long div_cfg = 0;
	unsigned long srcreg, offset;
	int shift;
	void *base_address;

	if (!clk_p || !clk_p->parent)
		return CLK_ERR_BAD_PARAMETER;

	/* Computing divider config */
	div_cfg = (*div_p - 1) & 0x1F;

	/* Which divider to setup ? */
	idx = clkgenax_get_index(clk_p->id, &srcreg, &shift);
	if (idx == -1)
		return CLK_ERR_BAD_PARAMETER;

	/* Now according to parent, let's write divider ratio */
	if (clk_p->parent->id >= CLK_S_A1_REF)
		offset = CKGA_SOURCE_CFG(clk_p->parent->id - CLK_S_A1_REF);
	else
		offset = CKGA_SOURCE_CFG(clk_p->parent->id - CLK_S_A0_REF);
	base_address = clkgenax_get_base_address(clk_p->id);
	CLK_WRITE(base_address + offset + (4 * idx), div_cfg);

	return 0;
}

/******************************************************************************
CLOCKGEN A0 clocks groups
******************************************************************************/

/* ========================================================================
   Name:	clkgena0_set_rate
   Description: Set clock frequency
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int clkgena0_set_rate(struct clk *clk_p, unsigned long freq)
{
	unsigned long div, data;
	struct stm_pll pll0 = {
		.type = stm_pll1600c65,
	};
	struct stm_pll pll1 = {
		.type = stm_pll800c65,
	};
	int err = 0;
	void *base_addr;

	if (!clk_p)
		return CLK_ERR_BAD_PARAMETER;
	if (clk_p->id < CLK_S_A0_PLL0HS || clk_p->id > CLK_S_MII1_REF_OUT)
		return CLK_ERR_BAD_PARAMETER;

	/* We need a parent for these clocks */
	if (!clk_p->parent)
		return CLK_ERR_INTERNAL;

	base_addr = clkgenax_get_base_address(clk_p->id);
	switch (clk_p->id) {
	case CLK_S_A0_PLL0HS:
	case CLK_S_A0_PLL0LS:
		if (clk_p->id == CLK_S_A0_PLL0LS)
			freq = freq * 2;
		err = stm_clk_pll_get_params(clk_clocks[CLK_S_A0_REF].rate,
					     freq, &pll0);
		if (err != 0)
			break;
#if !defined(CLKLLA_NO_PLL)
		data = CLK_READ(base_addr + CKGA_PLL0_CFG) &
			~(0xff << 8) & ~0x7;
		data = data | ((pll0.ndiv & 0xff) << 8) | (pll0.mdiv & 0x7);
		CLK_WRITE(base_addr + CKGA_PLL0_CFG, data);
		if (clk_p->id == CLK_S_A0_PLL0LS)
			err = clkgenax_recalc(&clk_clocks[CLK_S_A0_PLL0HS]);
#endif
		break;
	case CLK_S_A0_PLL1:
		err = stm_clk_pll_get_params(clk_clocks[CLK_S_A0_REF].rate,
					     freq, &pll1);
		if (err != 0)
			break;
#if !defined(CLKLLA_NO_PLL)
		data = CLK_READ(base_addr + CKGA_PLL1_CFG)
			& 0xfff80000;
		data |= (pll1.pdiv << 16 | pll1.ndiv << 8 | pll1.mdiv);
		CLK_WRITE(base_addr + CKGA_PLL1_CFG, data);
#endif
		break;
	case CLK_S_FDMA_0 ... CLK_S_MII1_REF_OUT:
		div = clk_best_div(clk_p->parent->rate, freq);
		err = clkgenax_set_div(clk_p, &div);
		break;
	default:
		return CLK_ERR_BAD_PARAMETER;
	}

	if (!err)
		err = clkgenax_recalc(clk_p);
	return err;
}

/******************************************************************************
CLOCKGEN A1 (A right) clocks group
******************************************************************************/

/* ========================================================================
   Name:	clkgena1_set_rate
   Description: Set clock frequency
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int clkgena1_set_rate(struct clk *clk_p, unsigned long freq)
{
	unsigned long div, data;
	unsigned long parent_rate;
	struct stm_pll pll0 = {
		.type = stm_pll1600c65,
	};
	struct stm_pll pll1 = {
		.type = stm_pll800c65,
	};
	int err = 0;
	void *base_addr;

	if (!clk_p)
		return CLK_ERR_BAD_PARAMETER;
	if ((clk_p->id < CLK_S_A1_PLL0HS) || (clk_p->id > CLK_S_TST_MVTAC_SYS))
		return CLK_ERR_BAD_PARAMETER;

	/* We need a parent for these clocks */
	if (!clk_p->parent)
		return CLK_ERR_INTERNAL;

	parent_rate = clk_get_rate(clk_get_parent(clk_p));
	base_addr = clkgenax_get_base_address(clk_p->id);

	switch (clk_p->id) {
	case CLK_S_A1_PLL0HS:
	case CLK_S_A1_PLL0LS:
		if (clk_p->id == CLK_S_A1_PLL0LS)
			freq = freq * 2;
		err = stm_clk_pll_get_params(clk_clocks[CLK_S_A1_REF].rate,
					     freq, &pll0);
		if (err != 0)
			break;
#if !defined(CLKLLA_NO_PLL)
		data = CLK_READ(base_addr + CKGA_PLL0_CFG) &
			~(0xff << 8) & ~0x7;
		data = data | ((pll0.ndiv & 0xff) << 8) | (pll0.mdiv & 0x7);
		CLK_WRITE(base_addr + CKGA_PLL0_CFG, data);
		if (clk_p->id == CLK_S_A1_PLL0LS)
			err = clkgenax_recalc(&clk_clocks[CLK_S_A1_PLL0HS]);
#endif
		break;
	case CLK_S_A1_PLL1:
		err = stm_clk_pll_get_params(clk_clocks[CLK_S_A1_REF].rate,
					     freq, &pll1);
		if (err != 0)
			break;
#if !defined(CLKLLA_NO_PLL)
		data = CLK_READ(base_addr + CKGA_PLL1_CFG)
				& 0xfff80000;
		data |= (pll1.pdiv << 16 | pll1.ndiv << 8 | pll1.mdiv);
		CLK_WRITE(base_addr + CKGA_PLL1_CFG, data);
#endif
		break;
	case CLK_S_ADP_WC_STAC ... CLK_S_MII0_REF_OUT:
	case CLK_S_TST_MVTAC_SYS:
		div = clk_best_div(parent_rate, freq);
		err = clkgenax_set_div(clk_p, &div);
		break;
	default:
		return CLK_ERR_BAD_PARAMETER;
	}

	if (!err)
		err = clkgenax_recalc(clk_p);

	return err;
}

/******************************************************************************
CLOCKGEN B/USB+DSS+Audio
******************************************************************************/

/* ========================================================================
   Name:	clkgenb_fsyn_recalc
   Description: Check FSYN & channels status... active, disabled, standbye
		'clk_p->rate' is updated accordingly.
   Returns:     Error code.
   ======================================================================== */

static int clkgenb_fsyn_recalc(struct clk *clk_p)
{

	int chan, fs_num;
	struct stm_fs fs = {
		.type = stm_fs216c65,
	};

	if (!clk_p || !clk_p->parent)
		return CLK_ERR_BAD_PARAMETER;
	if (clk_p->id < CLK_S_USB48 || clk_p->id > CLK_S_PCM_3)
		return CLK_ERR_BAD_PARAMETER;

#if !defined(CLKLLA_NO_PLL)

	fs_num = (clk_p->id < CLK_S_PCM_0 ? 0 : 1);
	chan = (clk_p->id - CLK_S_USB48) % 4;


	if (sysconf_read(clockgenb_cfg_fs[fs_num].npda) == 0) {
		/* NO. Analog part is powered down */
		clk_p->rate = 0;
		return 0;
	}
	if ((sysconf_read(clockgenb_cfg_fs[fs_num].nsb) & (1 << chan)) == 0) {
		/* Digital standbye */
		clk_p->rate = 0;
		return 0;
	}

	fs.mdiv = sysconf_read(clockgenb_fs[fs_num][chan].md);
	fs.pe = sysconf_read(clockgenb_fs[fs_num][chan].pe);
	fs.sdiv = sysconf_read(clockgenb_fs[fs_num][chan].sdiv);
	fs.nsdiv = sysconf_read(clockgenb_fs[fs_num][chan].nsdiv);

	return stm_clk_fs_get_rate(clk_p->parent->rate, &fs, &clk_p->rate);

#else

	if (clk_p->nominal_rate)
		clk_p->rate = clk_p->nominal_rate;
	else
		clk_p->rate = 12121212;
	return 0;

#endif
}

/* ========================================================================
   Name:	clkgenb_recalc
   Description: Get CKGB clocks frequencies function
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int clkgenb_recalc(struct clk *clk_p)
{
	if (!clk_p)
		return CLK_ERR_BAD_PARAMETER;
	if (clk_p->id < CLK_S_B_REF || clk_p->id > CLK_S_PCM_3)
		return CLK_ERR_BAD_PARAMETER;

	if (clk_p->id == CLK_S_B_REF) {
		clk_p->rate = clk_p->parent->rate;
		return 0;
	}

	return clkgenb_fsyn_recalc(clk_p);
}

/* ========================================================================
   Name:	clkgenb_fsyn_xable
   Description: Enable/disable FSYN
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int clkgenb_fsyn_xable(struct clk *clk_p, unsigned long enable)
{
	unsigned long val, chan, npda_value;
	int fs_num;

	if (!clk_p)
		return CLK_ERR_BAD_PARAMETER;
	if (clk_p->id < CLK_S_B_REF || clk_p->id > CLK_S_PCM_3)
		return CLK_ERR_BAD_PARAMETER;

	if (clk_p->id == CLK_S_B_REF)
		return 0;

	fs_num = (clk_p->id < CLK_S_PCM_0 ? 0 : 1);
	chan = (clk_p->id - CLK_S_USB48) % 4;

	val = sysconf_read(clockgenb_cfg_fs[fs_num].nsb);
	if (enable) {
		val |= 1 << chan;
		npda_value = 1;
	} else {
		val &= ~(1 << chan);
		if (!val)
			npda_value = 0;
		else
			npda_value = sysconf_read(clockgenb_cfg_fs[fs_num].npda);
	}
	sysconf_write(clockgenb_cfg_fs[fs_num].nsb, val);
	sysconf_write(clockgenb_cfg_fs[fs_num].npda, npda_value);

	/* Freq recalc required only if a channel is enabled */
	if (enable)
		return clkgenb_fsyn_recalc(clk_p);
	else
		clk_p->rate = 0;
	return 0;
}

/* ========================================================================
   Name:	clkgenb_fsyn_set_rate
   Description: Set FS clock
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int clkgenb_fsyn_set_rate(struct clk *clk_p, unsigned long freq)
{
	unsigned long val;
	int chan, fs_cfg;
	struct stm_fs fs = {
		.type = stm_fs216c65,
		.nsdiv = 0xff
	};

	if (!clk_p)
		return CLK_ERR_BAD_PARAMETER;
	if (!clk_p->parent)
		return CLK_ERR_INTERNAL;
	if (clk_p->id < CLK_S_USB48 || clk_p->id > CLK_S_PCM_3)
		return CLK_ERR_BAD_PARAMETER;

#if !defined(CLKLLA_NO_PLL)

	/* Even if NSDIV is programmable, freezing it to allows
	   smooth audio clocks changes without leading to
	   sound discontinuity */
	if (clk_p->id >= CLK_S_PCM_0 && clk_p->id <= CLK_S_PCM_3)
		fs.nsdiv = 0;

	/* Computing FSyn params. Should be common function with FSyn type */
	if (stm_clk_fs_get_params(clk_p->parent->rate, freq, &fs))
		return CLK_ERR_BAD_PARAMETER;

	fs_cfg = (clk_p->id < CLK_S_PCM_0 ? 1557 : 2561);
	chan = (clk_p->id - CLK_S_USB48) % 4;

	/* Analog p ower up */
	val = (clk_p->id < CLK_S_PCM_0 ? 0 : 1);
	sysconf_write(clockgenb_cfg_fs[val].npda, 1);

	sysconf_write(clockgenb_fs[val][chan].md, fs.mdiv);
	sysconf_write(clockgenb_fs[val][chan].pe, fs.pe);
	sysconf_write(clockgenb_fs[val][chan].sdiv, fs.sdiv);
	sysconf_write(clockgenb_fs[val][chan].nsdiv, fs.nsdiv);
	sysconf_write(clockgenb_fs[val][chan].prog_en, 1);
	sysconf_write(clockgenb_fs[val][chan].prog_en, 0);
#endif

	return 0;
}

/* ========================================================================
   Name:	clkgenb_enable
   Description: Enable clock or FSYN (clockgen B)
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int clkgenb_enable(struct clk *clk_p)
{
	return clkgenb_fsyn_xable(clk_p, 1);
}

/* ========================================================================
   Name:	clkgenb_disable
   Description: Disable clock
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int clkgenb_disable(struct clk *clk_p)
{
	return clkgenb_fsyn_xable(clk_p, 0);
}

/* ========================================================================
   Name:	clkgenb_set_rate
   Description: Set clock frequency
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int clkgenb_set_rate(struct clk *clk_p, unsigned long freq)
{
	int err;

	if (!clk_p)
		return CLK_ERR_BAD_PARAMETER;
	if (!clk_p->parent)
		return CLK_ERR_INTERNAL;
	if (clk_p->id < CLK_S_USB48 || clk_p->id > CLK_S_PCM_3)
		return CLK_ERR_BAD_PARAMETER;

	err = clkgenb_fsyn_set_rate(clk_p, freq);
	if (!err)
		err = clkgenb_recalc(clk_p);

	return err;
}

/* ========================================================================
   Name:	clkgenb_init
   Description: Read HW status to initialize 'clk' structure.
   Returns:     'clk_err_t' error code.
   ======================================================================== */

static int clkgenb_init(struct clk *clk_p)
{
	/* All clocks have static parent */
	return clkgenb_recalc(clk_p);
}

/******************************************************************************
CLOCKGEN C (video & transport)
Quad FSYN + Video Clock Controller
******************************************************************************/

/* ========================================================================
   Name:	clkgenc_fsyn_recalc
   Description: Get CKGC FSYN clocks frequencies function
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int clkgenc_fsyn_recalc(struct clk *clk_p)
{
	unsigned long cfg, dig_bit;
	int channel, err = 0;
	struct stm_fs fs = {
		.type = stm_fs432c65
	};

	if (!clk_p)
		return CLK_ERR_BAD_PARAMETER;
	if (clk_p->id < CLK_S_C_FS0_CH0 || clk_p->id > CLK_S_C_FS0_CH3)
		return CLK_ERR_BAD_PARAMETER;

#if !defined(CLKLLA_NO_PLL)

	/* Checking FSYN analog status */
	cfg = SYSCONF_READU(2500, 0, 17);
	if ((cfg  & (1 << 14)) != (1 << 14)) {
		/* Analog power down */
		clk_p->rate = 0;
		return 0;
	}

	channel = clk_p->id - CLK_S_C_FS0_CH0;

	/* Checking FSYN digital part */
	dig_bit = 10 + channel;
	if ((cfg & (1 << dig_bit)) == 0) {	/* digital part in standbye */
		clk_p->rate = 0;
		return 0;
	}

	/* FSYN up & running.
	   Computing frequency */
	fs.mdiv = sysconf_read(clockgenc_fs[channel].md);
	fs.pe = sysconf_read(clockgenc_fs[channel].pe);
	fs.sdiv = sysconf_read(clockgenc_fs[channel].sdiv);
	fs.nsdiv = sysconf_read(clockgenc_fs[channel].nsdiv);

	err = stm_clk_fs_get_rate(clk_p->parent->rate, &fs, &clk_p->rate);

#else

	if (clk_p->nominal_rate)
		clk_p->rate = clk_p->nominal_rate;
	else
		clk_p->rate = 12121212;

#endif

	return err;
}

/* ========================================================================
   Name:	clkgenc_fsyn_xable
   Description: Enable/Disable FSYN. If all channels OFF, FSYN is powered
		down.
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int clkgenc_fsyn_xable(struct clk *clk_p, unsigned long enable)
{
	unsigned long val, chan;

	if (!clk_p)
		return CLK_ERR_BAD_PARAMETER;
	if (clk_p->id < CLK_S_C_REF || clk_p->id > CLK_S_C_FS0_CH3)
		return CLK_ERR_BAD_PARAMETER;

	if (clk_p->id == CLK_S_C_REF)
		return 0;

	chan = clk_p->id - CLK_S_C_FS0_CH0;

	/* Powering down/up digital part */
	val = SYSCONF_READU(2500, 0, 17);
	if (enable) {
		/* Powering up digital part */
		val |= (1 << (10 + chan));
		/* Powering up analog part */
		val |= (1 << 14);
	} else {
		/* Powering down digital part */
		val &= ~(1 << (10 + chan));
		/* If all channels are off then power down FS0 */
		if ((val & 0x3c00) == 0)
			val &= ~(1 << 14);
	}
	SYSCONF_WRITEU(2500, 0, 17, val);

	/* Freq recalc required only if a channel is enabled */
	if (enable)
		return clkgenc_fsyn_recalc(clk_p);
	else
		clk_p->rate = 0;
	return 0;
}

/* ========================================================================
   Name:	clkgenc_vcc_recalc
   Description: Update Video Clock Controller outputs value
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int clkgenc_vcc_recalc(struct clk *clk_p)
{
	int chan;
	unsigned long val;
	static unsigned char tab1248[] = { 1, 2, 4, 8 };

	if (clk_p->id < CLK_S_PIX_HDMI || clk_p->id > CLK_S_THSENS)
		return CLK_ERR_BAD_PARAMETER;

	chan = clk_p->id - CLK_S_PIX_HDMI;

	/* Is the channel stopped ? */
	val = (SYSCONF_READU(2555, 0, 15) >> chan) & 1;
	if (val)	/* 1=stopped */
		clk_p->rate = 0;
	else {
		/* What is the divider ratio ? */
		val = (SYSCONF_READU(2557, 0, 31)
				>> (chan * 2)) & 3;
		clk_p->rate = clk_p->parent->rate / tab1248[val];
	}

	/* Special case: TMDS_FROMPHY is a multiple of REJECTION_PLL */
	if (clk_p->id == CLK_S_HDMI_REJECT_PLL)
		clk_clocks[CLK_S_TMDS_FROMPHY].rate = clk_p->rate;

	return 0;
}

/* ========================================================================
   Name:	clkgenc_vcc_xable
   Description: Enable/disable Video Clock Controller outputs
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int clkgenc_vcc_xable(struct clk *clk_p, int enable)
{
	int chan;
	unsigned long val;

	chan = clk_p->id - CLK_S_PIX_HDMI;
	val = SYSCONF_READU(2555, 0, 15);
	if (enable)
		val &= ~(1 << chan);
	else
		val |= (1 << chan);
	SYSCONF_WRITEU(2555, 0, 15, val);

	return clkgenc_vcc_recalc(clk_p);
}

/* ========================================================================
   Name:	clkgenc_vcc_set_div
   Description: Video Clocks Controller divider setup function
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int clkgenc_vcc_set_div(struct clk *clk_p, unsigned long *div_p)
{
	int chan;
	unsigned long set, val;
	static const unsigned char div_table[] = {
		/* 1  2     3  4     5     6     7  8 */
		0, 1, 0xff, 2, 0xff, 0xff, 0xff, 3 };

	if (clk_p->id < CLK_S_PIX_HDMI || clk_p->id > CLK_S_THSENS)
		return CLK_ERR_BAD_PARAMETER;

	chan = clk_p->id - CLK_S_PIX_HDMI;
	if (*div_p < 1 || *div_p > 8)
		return CLK_ERR_BAD_PARAMETER;

	set = div_table[*div_p - 1];
	if (set == 0xff)
		return CLK_ERR_BAD_PARAMETER;

	/* Set SYSTEM_CONFIG2557: div_mode, 2bits per channel */
	val = SYSCONF_READU(2557, 0, 31);
	val &= ~(3 << (chan * 2));
	val |= set << (chan * 2);
	SYSCONF_WRITEU(2557, 0, 31, val);

	return 0;
}

/* ========================================================================
   Name:	clkgenc_recalc
   Description: Get CKGC clocks frequencies function
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int clkgenc_recalc(struct clk *clk_p)
{
	if (!clk_p)
		return CLK_ERR_BAD_PARAMETER;

	switch (clk_p->id) {
	case CLK_S_C_REF:
	case CLK_S_VCC_HD: /* Mux output */
		clk_p->rate = clk_p->parent->rate;
		break;
	case CLK_S_TMDS_FROMPHY: /* Clock from HDMI PHY */
		clk_p->rate = clk_clocks[CLK_S_HDMI_REJECT_PLL].rate;
		break;
	case CLK_S_C_FS0_CH0 ... CLK_S_C_FS0_CH3:	/* FS clocks */
		return clkgenc_fsyn_recalc(clk_p);
	case CLK_S_PIX_HDMI ... CLK_S_THSENS:		/* VCC clocks */
		return clkgenc_vcc_recalc(clk_p);
	default:
		return CLK_ERR_BAD_PARAMETER;
	}

	return 0;
}

/* ========================================================================
   Name:	clkgenc_set_rate
   Description: Set CKGC clocks frequencies
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int clkgenc_set_rate(struct clk *clk_p, unsigned long freq)
{
	unsigned long val;
	int channel, err = 0;
	struct stm_fs fs = {
		.type = stm_fs432c65,
		.nsdiv = 0xff
	};

	if (!clk_p)
		return CLK_ERR_BAD_PARAMETER;

	switch (clk_p->id) {
	case CLK_S_VCC_HD: /* Mux output */
		if (clk_p->parent->id != CLK_S_C_REF)
			err = clkgenc_set_rate(clk_p->parent, freq);
		break;
	case CLK_S_C_FS0_CH0 ... CLK_S_C_FS0_CH3:
		if (stm_clk_fs_get_params(clk_p->parent->rate, freq, &fs))
			return CLK_ERR_BAD_PARAMETER;

		channel = clk_p->id - CLK_S_C_FS0_CH0;

		val = SYSCONF_READU(2500, 0, 17);
		/* Removing reset, digit standby and analog standby */
		val |= (1 << 14) | (1 << (10 + channel));
		SYSCONF_WRITEU(2500, 0, 17, val);

		sysconf_write(clockgenc_fs[channel].md, fs.mdiv);
		sysconf_write(clockgenc_fs[channel].pe, fs.pe);
		sysconf_write(clockgenc_fs[channel].sdiv, fs.sdiv);
		sysconf_write(clockgenc_fs[channel].nsdiv, fs.nsdiv);
		sysconf_write(clockgenc_fs[channel].prog_en, 1);
		sysconf_write(clockgenc_fs[channel].prog_en, 0);

		break;
	case CLK_S_PIX_HDMI ... CLK_S_THSENS:
		/* Video Clock Controller clocks */

		val = clk_best_div(clk_p->parent->rate, freq);
		err = clkgenc_vcc_set_div(clk_p, &val);
		break;
	default:
		return CLK_ERR_BAD_PARAMETER;
	}

	/* Recomputing freq from real HW status */
	if (err)
		return err;
	return clkgenc_recalc(clk_p);
}

/* ========================================================================
   Name:	clkgenc_identify_parent
   Description: Identify parent clock
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int clkgenc_identify_parent(struct clk *clk_p)
{
	unsigned long chan, val;
	static const struct clk *vcc_parent_clocks[] = {
		&clk_clocks[CLK_S_VCC_HD],		/* clk_hd */
		&clk_clocks[CLK_S_VCC_SD],		/* clk_sd */
		&clk_clocks[CLK_S_TMDS_FROMPHY],	/* clk_hd_ext */
		&clk_clocks[CLK_S_C_FS0_CH2]		/* clk_sd_ext */
	};

	if (clk_p->id >= CLK_S_C_REF && clk_p->id <= CLK_S_C_FS0_CH3)
		return 0; /* These clocks have static parent */

	if (clk_p->id == CLK_S_VCC_HD) {
		val = SYSCONF_READU(2558, 0, 0);
		if (val)
			clk_p->parent = &clk_clocks[CLK_S_C_FS0_CH0];
		else
			clk_p->parent = &clk_clocks[CLK_S_C_REF];
	} else if (clk_p->id >= CLK_S_PIX_HDMI && clk_p->id <= CLK_S_THSENS) {
		/* Clocks from "Video Clock Controller". */
		chan = clk_p->id - CLK_S_PIX_HDMI;
		val = SYSCONF_READU(2556, 0, 31);
		val >>= (chan * 2);
		val &= 0x3;
		/* VCC inputs:
		   00 clk_hd = CLK_S_VCC_HD (mux output)
		   01 clk_sd = CLK_S_VCC_SD (FS chan 1)
		   10 clk_hd_ext = CLK_S_TMDS_FROMPHY
		   11 clk_sd_ext = CLK_S_C_FS0_CH2 (FS chan 2)
		 */
		clk_p->parent = (struct clk *) vcc_parent_clocks[val];
	}

	/* Other clockgen C clocks are statically initialized
	   thanks to _CLK_P() macro */

	return 0;
}

/* ========================================================================
   Name:	clkgenc_set_parent
   Description: Set parent clock
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int clkgenc_set_parent(struct clk *clk_p, struct clk *parent_p)
{
	unsigned long chan, val, data;

	if (!clk_p)
		return CLK_ERR_BAD_PARAMETER;

	if (clk_p->id == CLK_S_VCC_HD) {
		val = SYSCONF_READU(2558, 0, 0);
		if (parent_p->id == CLK_S_C_FS0_CH0)
			val |= 1;
		else
			val &= ~1;
		SYSCONF_WRITEU(2558, 0, 0, val);
		clk_p->parent = parent_p;
	} else if (clk_p->id >= CLK_S_PIX_HDMI &&
		   clk_p->id <= CLK_S_HDMI_REJECT_PLL) {
		/* Clocks from "Video Clock Controller". */
		/* VCC inputs:
		   00 clk_hd = CLK_S_VCC_HD (mux output)
		   01 clk_sd = CLK_S_VCC_SD (FS chan 1)
		   10 clk_hd_ext = CLK_S_TMDS_FROMPHY
		   11 clk_sd_ext = CLK_S_C_FS0_CH2 (FS chan 2)
		 */
		chan = clk_p->id - CLK_S_PIX_HDMI;
		switch (parent_p->id) {
		case CLK_S_VCC_HD:
			val = 0;
			break;
		case CLK_S_VCC_SD:
			val = 1;
			break;
		case CLK_S_TMDS_FROMPHY:
			val = 2;
			break;
		case CLK_S_C_FS0_CH2:
			val = 3;
			break;
		default:
			return CLK_ERR_BAD_PARAMETER;
		}
		data = SYSCONF_READU(2556, 0, 31);
		data &= ~(0x3 << (chan * 2));
		data |= (val << (chan * 2));
		SYSCONF_WRITEU(2556, 0, 31, data);
		clk_p->parent = parent_p;
	} else if (clk_p->id == CLK_S_THSENS) {
		/* VCC: CLK_S_THSENS must be sourced from FS chan 2
			(clk_sd_ext) */
		chan = 14;
		val = 3;
		data = SYSCONF_READU(2556, 0, 31);
		data &= ~(0x3 << (chan * 2));
		data |= (val << (chan * 2));
		SYSCONF_WRITEU(2556, 0, 31, data);
		clk_p->parent = &clk_clocks[CLK_S_C_FS0_CH2];
	} else
		return CLK_ERR_BAD_PARAMETER;

	return clkgenc_recalc(clk_p);
}

/* ========================================================================
   Name:	clkgenc_init
   Description: Read HW status to initialize 'clk' structure.
   Returns:     'clk_err_t' error code.
   ======================================================================== */

static int clkgenc_init(struct clk *clk_p)
{
	int err;

	if (!clk_p)
		return CLK_ERR_BAD_PARAMETER;
	if (clk_p->id < CLK_S_C_REF || clk_p->id > CLK_S_THSENS)
		return CLK_ERR_BAD_PARAMETER;

	err = clkgenc_identify_parent(clk_p);
	if (!err)
		err = clkgenc_recalc(clk_p);

	return err;
}

/* ========================================================================
   Name:	clkgenc_enable
   Description: Enable clock
   Returns:     'clk_err_t' error code.
   ======================================================================== */

static int clkgenc_enable(struct clk *clk_p)
{
	switch (clk_p->id) {
	case CLK_S_C_REF:
		return 0;
	case CLK_S_C_FS0_CH0 ... CLK_S_C_FS0_CH3:
		return clkgenc_fsyn_xable(clk_p, 1);
	case CLK_S_PIX_HDMI ... CLK_S_THSENS:
		return clkgenc_vcc_xable(clk_p, 1);
	}

	return 0;
}

/* ========================================================================
   Name:	clkgenc_disable
   Description: Disable clock
   Returns:     'clk_err_t' error code.
   ======================================================================== */

static int clkgenc_disable(struct clk *clk_p)
{
	switch (clk_p->id) {
	case CLK_S_C_REF:
		return 0;
	case CLK_S_C_FS0_CH0 ... CLK_S_C_FS0_CH3:
		return clkgenc_fsyn_xable(clk_p, 0);
	case CLK_S_PIX_HDMI ... CLK_S_THSENS:
		return clkgenc_vcc_xable(clk_p, 0);
	}

	return 0;
}

/******************************************************************************
CLOCKGEN D (CCSC, MCHI, TSout src, ref clock for MMCRU)
******************************************************************************/

/* ========================================================================
   Name:	clkgend_fsyn_recalc
   Description: Get CKGD FSYN clocks frequencies function
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int clkgend_fsyn_recalc(struct clk *clk_p)
{
	unsigned long cfg, dig_bit;
	int channel, err = 0;
	struct stm_fs fs = {
		.type = stm_fs216c65,
	};

	if (!clk_p)
		return CLK_ERR_BAD_PARAMETER;
	if (clk_p->id < CLK_S_CCSC || clk_p->id > CLK_S_MCHI)
		return CLK_ERR_BAD_PARAMETER;

	/* Checking FSYN analog status */
	cfg = SYSCONF_READU(1504, 0, 17);
	if ((cfg & (1 << 14)) == 0) {   /* Analog power down */
		clk_p->rate = 0;
		return 0;
	}

	/* Checking FSYN digital part */
	dig_bit = 10 + clk_p->id - CLK_S_CCSC;
	if ((cfg & (1 << dig_bit)) == 0) { /* digital part in standbye */
		clk_p->rate = 0;
		return 0;
	}

	/* FSYN up & running.
	   Computing frequency */
	channel = clk_p->id - CLK_S_CCSC;

	fs.mdiv = sysconf_read(clockgend_fs[channel].md);
	fs.pe = sysconf_read(clockgend_fs[channel].pe);
	fs.sdiv = sysconf_read(clockgend_fs[channel].sdiv);
	fs.nsdiv = sysconf_read(clockgend_fs[channel].nsdiv);

	err = stm_clk_fs_get_rate(clk_p->parent->rate, &fs,
		&clk_p->rate);

	return err;
}

/* ========================================================================
   Name:	clkgend_recalc
   Description: Get CKGD clocks frequencies function
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int clkgend_recalc(struct clk *clk_p)
{
	if (!clk_p)
		return CLK_ERR_BAD_PARAMETER;

	if (clk_p->id == CLK_S_D_REF)
		clk_p->rate = clk_p->parent->rate;
	else if (clk_p->id >= CLK_S_CCSC && clk_p->id <= CLK_S_MCHI)
		return clkgend_fsyn_recalc(clk_p);
	else
		return CLK_ERR_BAD_PARAMETER;

	return 0;
}

/* ========================================================================
   Name:	clkgend_fsyn_set_rate
   Description: Set FS clock
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int clkgend_fsyn_set_rate(struct clk *clk_p, unsigned long freq)
{
	int channel;
	unsigned long val;
	struct stm_fs fs = {
		.type = stm_fs216c65,
		.nsdiv = 0xff
	};

	if (!clk_p)
		return CLK_ERR_BAD_PARAMETER;
	if (!clk_p->parent)
		return CLK_ERR_INTERNAL;
	if (clk_p->id < CLK_S_CCSC || clk_p->id > CLK_S_MCHI)
		return CLK_ERR_BAD_PARAMETER;

	/* Computing FSyn params. Should be common function with FSyn type */
	if (stm_clk_fs_get_params(clk_p->parent->rate, freq, &fs))
		return CLK_ERR_BAD_PARAMETER;

	channel = clk_p->id - CLK_S_CCSC;
	val = SYSCONF_READU(1504, 0, 17);
	/* Power up, release digit reset & FS reset */
	val |= (1 << 14) | (1 << (10 + channel));
	SYSCONF_WRITEU(1504, 0, 17, val);

	sysconf_write(clockgend_fs[channel].md, fs.mdiv);
	sysconf_write(clockgend_fs[channel].pe, fs.pe);
	sysconf_write(clockgend_fs[channel].sdiv, fs.sdiv);
	sysconf_write(clockgend_fs[channel].nsdiv, fs.nsdiv);
	sysconf_write(clockgend_fs[channel].prog_en, 1);
	sysconf_write(clockgend_fs[channel].prog_en, 0);

	return clkgend_recalc(clk_p);
}

/* ========================================================================
   Name:	clkgend_fsyn_xable
   Description: Enable/Disable FSYN. If all channels OFF, FSYN is powered
		down.
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int clkgend_fsyn_xable(struct clk *clk_p, unsigned long enable)
{
	unsigned long val;
	int chan;

	if (!clk_p)
		return CLK_ERR_BAD_PARAMETER;
	if (clk_p->id < CLK_S_D_REF || clk_p->id > CLK_S_MCHI)
		return CLK_ERR_BAD_PARAMETER;

	if (clk_p->id == CLK_S_D_REF)
		return 0;

	chan = clk_p->id - CLK_S_CCSC;

	/* Powering down/up digital part */
	val = SYSCONF_READU(1504, 0, 17);
	if (enable) {
		val |= (1 << (10 + chan));
		val |= (1 << 14);
	} else {
		val &= ~(1 << (10 + chan));
		if ((val & 0x3c00) == 0)
			val &= ~(1 << 14);
	}
	SYSCONF_WRITEU(1504, 0, 17, val);

	/* Freq recalc required only if a channel is enabled */
	if (enable)
		return clkgend_fsyn_recalc(clk_p);
	else
		clk_p->rate = 0;
	return 0;
}

/* ========================================================================
   Name:	clkgend_init
   Description: Read HW status to initialize 'clk' structure.
   Returns:     'clk_err_t' error code.
   ======================================================================== */

static int clkgend_init(struct clk *clk_p)
{
	if (!clk_p)
		return CLK_ERR_BAD_PARAMETER;

	/* Parents are static. No idenfication required */
	return clkgend_recalc(clk_p);
}

/* ========================================================================
   Name:	clkgend_enable
   Description: Enable clock
   Returns:     'clk_err_t' error code.
   ======================================================================== */

static int clkgend_enable(struct clk *clk_p)
{
	return clkgend_fsyn_xable(clk_p, 1);
}

/* ========================================================================
   Name:	clkgend_disable
   Description: Disable clock
   Returns:     'clk_err_t' error code.
   ======================================================================== */

static int clkgend_disable(struct clk *clk_p)
{
	return clkgend_fsyn_xable(clk_p, 0);
}

/******************************************************************************
CLOCKGEN HDMI RX
******************************************************************************/

/* ========================================================================
   Name:	clkgenhdmirx_recalc
   Description: Update clock structure from HW
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int clkgenhdmirx_recalc(struct clk *clk_p)
{
	if (!clk_p)
		return CLK_ERR_BAD_PARAMETER;
	if (clk_p->id != CLK_S_PIX_HDMIRX)
		return CLK_ERR_BAD_PARAMETER;

	clk_p->rate = 121212; /* TO BE COMPLETED */

	return 0;
}

/* ========================================================================
   Name:	clkgenhdmirx_init
   Description: Read HW status to initialize 'clk' structure.
   Returns:     'clk_err_t' error code.
   ======================================================================== */

static int clkgenhdmirx_init(struct clk *clk_p)
{
	if (!clk_p)
		return CLK_ERR_BAD_PARAMETER;

	/* Parents are static. No idenfication required */
	return clkgenhdmirx_recalc(clk_p);
}

/* ========================================================================
   Name:	clkgenhdmirx_enable
   Description: Enable clock
   Returns:     'clk_err_t' error code.
   ======================================================================== */

static int clkgenhdmirx_enable(struct clk *clk_p)
{
	return 0;
}

/* ========================================================================
   Clocks groups declaration
   ======================================================================== */

_CLK_OPS(clkgena0,
	"A0",
	clkgenax_init,
	clkgenax_set_parent,
	clkgena0_set_rate,
	clkgenax_recalc,
	clkgenax_enable,
	clkgenax_disable,
	NULL,
	NULL,
	"PIO12[0]"       /* Observation point */
);
_CLK_OPS(clkgena1,
	"A1",
	clkgenax_init,
	clkgenax_set_parent,
	clkgena1_set_rate,
	clkgenax_recalc,
	clkgenax_enable,
	clkgenax_disable,
	NULL,
	NULL,
	"PIO12[1]"       /* Observation point */
);
_CLK_OPS(clkgenb,
	"B",
	clkgenb_init,
	NULL,
	clkgenb_set_rate,
	clkgenb_recalc,
	clkgenb_enable,
	clkgenb_disable,
	NULL,
	NULL,		/* No measure function */
	"PIO16[6] or PIO10[2]"	/* Observation point */
);
_CLK_OPS(clkgenc,
	"C",
	clkgenc_init,
	clkgenc_set_parent,
	clkgenc_set_rate,
	clkgenc_recalc,
	clkgenc_enable,
	clkgenc_disable,
	NULL,
	NULL,		/* No measure function */
	"PIO16[6] or PIO10[3]"	/* Observation point */
);
_CLK_OPS(clkgend,
	"D",
	clkgend_init,
	NULL,
	clkgend_fsyn_set_rate,
	clkgend_recalc,
	clkgend_enable,
	clkgend_disable,
	NULL,
	NULL,	       /* No measure function */
	"?"		 /* Observation point */
);
_CLK_OPS(clkgenhdmirx,
	"HDMIRX",
	clkgenhdmirx_init,
	NULL,
	NULL,
	clkgenhdmirx_recalc,
	clkgenhdmirx_enable,
	NULL,
	NULL,
	NULL,	       /* No measure function */
	"?"		 /* Observation point */
);

/* Physical clocks description */
static struct clk clk_clocks[] = {
/* Clockgen A0 */
_CLK(CLK_S_A0_REF, &clkgena0, 0,
	  CLK_RATE_PROPAGATES | CLK_ALWAYS_ENABLED),
_CLK_P(CLK_S_A0_PLL0HS, &clkgena0, 1000000000,
	CLK_RATE_PROPAGATES, &clk_clocks[CLK_S_A0_REF]),
_CLK_P(CLK_S_A0_PLL0LS, &clkgena0, 500000000,
	CLK_RATE_PROPAGATES, &clk_clocks[CLK_S_A0_PLL0HS]),
_CLK_P(CLK_S_A0_PLL1, &clkgena0, 800000000,
	CLK_RATE_PROPAGATES, &clk_clocks[CLK_S_A0_REF]),

_CLK(CLK_S_FDMA_0,	&clkgena0,    400000000,    0),
_CLK(CLK_S_FDMA_1,	&clkgena0,    400000000,    0),
_CLK(CLK_S_JIT_SENSE,	&clkgena0,    0,    0),
_CLK(CLK_S_ICN_REG_0,	&clkgena0,    100000000, 0),
_CLK(CLK_S_ICN_IF_0,	&clkgena0,    200000000,    0),
_CLK(CLK_S_ICN_REG_LP_0,	&clkgena0,    100000000,    0),
_CLK(CLK_S_EMISS,	&clkgena0,    100000000,    0),
_CLK(CLK_S_ETH1_PHY,	&clkgena0,    50000000,    0),
_CLK(CLK_S_MII1_REF_OUT,	&clkgena0,    25000000,    0),

/* Clockgen A1 */
_CLK(CLK_S_A1_REF, &clkgena1, 0,
	CLK_RATE_PROPAGATES | CLK_ALWAYS_ENABLED),
_CLK_P(CLK_S_A1_PLL0HS, &clkgena1, 1800000000,
	CLK_RATE_PROPAGATES,  &clk_clocks[CLK_S_A1_REF]),
_CLK_P(CLK_S_A1_PLL0LS, &clkgena1, 900000000,
	CLK_RATE_PROPAGATES,  &clk_clocks[CLK_S_A1_PLL0HS]),
_CLK_P(CLK_S_A1_PLL1, &clkgena1, 800000000,
	CLK_RATE_PROPAGATES, &clk_clocks[CLK_S_A1_REF]),

_CLK(CLK_S_ADP_WC_STAC,	&clkgena1,   0,    0),
_CLK(CLK_S_ADP_WC_VTAC,	&clkgena1,   0,    0),
_CLK(CLK_S_STAC_TX_CLK_PLL,	&clkgena1,   900000000,    0),
_CLK(CLK_S_STAC,		&clkgena1,   400000000,    0),
_CLK(CLK_S_ICN_IF_2,	&clkgena1,   200000000,    0),
_CLK(CLK_S_CARD_MMC_0,	&clkgena1,   50000000,    0),
_CLK(CLK_S_ICN_IF_1,	&clkgena1,   200000000,    0),
_CLK(CLK_S_GMAC0_PHY,	&clkgena1,   50000000,    0),
_CLK(CLK_S_NAND_CTRL,	&clkgena1,   200000000,    0),
_CLK(CLK_S_DCEIMPD_CTRL,	&clkgena1,   0,    0),
_CLK(CLK_S_MII0_REF_OUT,	&clkgena1,   25000000,    0),
_CLK(CLK_S_TST_MVTAC_SYS,	&clkgena1,   0,    0),
_CLK(CLK_S_CARD_MMC_1,	&clkgena1,   50000000,    0),

/* Clockgen B */
_CLK(CLK_S_B_REF, &clkgenb, 0,
		  CLK_RATE_PROPAGATES | CLK_ALWAYS_ENABLED),

_CLK_P(CLK_S_USB48, &clkgenb, 48000000, 0, &clk_clocks[CLK_S_B_REF]),
_CLK_P(CLK_S_DSS, &clkgenb, 36864000,	0, &clk_clocks[CLK_S_B_REF]),
_CLK_P(CLK_S_STFE_FRC_2, &clkgenb, 32768000,	0, &clk_clocks[CLK_S_B_REF]),
_CLK_P(CLK_S_THSENS_SCARD, &clkgenb, 27000000, 0, &clk_clocks[CLK_S_B_REF]),

_CLK_P(CLK_S_PCM_0, &clkgenb, 0, 0, &clk_clocks[CLK_S_B_REF]),
_CLK_P(CLK_S_PCM_1, &clkgenb, 0, 0, &clk_clocks[CLK_S_B_REF]),
_CLK_P(CLK_S_PCM_2, &clkgenb, 0, 0, &clk_clocks[CLK_S_B_REF]),
_CLK_P(CLK_S_PCM_3, &clkgenb, 0, 0, &clk_clocks[CLK_S_B_REF]),

/* Clockgen C: FS outputs */
_CLK(CLK_S_C_REF, &clkgenc, 0,
		  CLK_RATE_PROPAGATES | CLK_ALWAYS_ENABLED),
_CLK_P(CLK_S_C_FS0_CH0, &clkgenc, 148500000,
		CLK_RATE_PROPAGATES, &clk_clocks[CLK_S_C_REF]),
_CLK_P(CLK_S_VCC_SD, &clkgenc, 108000000,
		CLK_RATE_PROPAGATES, &clk_clocks[CLK_S_C_REF]),
_CLK_P(CLK_S_C_FS0_CH2, &clkgenc, 625000,
		CLK_RATE_PROPAGATES, &clk_clocks[CLK_S_C_REF]),

/* Clockgen C: mux output */
_CLK(CLK_S_VCC_HD, &clkgenc, 0, CLK_RATE_PROPAGATES),

/* Clockgen C: clock from HDMI PHY */
_CLK(CLK_S_TMDS_FROMPHY, &clkgenc, 0, CLK_RATE_PROPAGATES),

/* Clockgen C: video clock controller */
_CLK(CLK_S_PIX_HDMI, &clkgenc, 148500000, 0),
_CLK(CLK_S_PIX_DVO, &clkgenc, 148500000, 0),
_CLK(CLK_S_OUT_DVO, &clkgenc, 148500000, 0),
_CLK(CLK_S_PIX_HD, &clkgenc, 148500000, 0),
_CLK(CLK_S_HDDAC, &clkgenc, 148500000, 0),
_CLK(CLK_S_DENC, &clkgenc, 27000000, 0),
_CLK(CLK_S_SDDAC, &clkgenc, 108000000, 0),
_CLK(CLK_S_PIX_MAIN, &clkgenc, 148500000, CLK_RATE_PROPAGATES),	/* To MPE */
_CLK(CLK_S_PIX_AUX, &clkgenc, 148500000, CLK_RATE_PROPAGATES),	/* To MPE */
_CLK(CLK_S_STFE_FRC_0, &clkgenc, 27000000, 0),
_CLK(CLK_S_REF_MCRU, &clkgenc, 27000000, 0),
_CLK(CLK_S_SLAVE_MCRU, &clkgenc, 27000000, 0),
_CLK(CLK_S_TMDS_HDMI, &clkgenc, 297000000, 0),
_CLK(CLK_S_HDMI_REJECT_PLL, &clkgenc, 297000000, 0),
_CLK(CLK_S_THSENS, &clkgenc, 156250, 0),

/* Clockgen D: Generic quad FS (CCSC, MCHI, TSOUT, MMCRU ref clock) */
_CLK(CLK_S_D_REF, &clkgend, 30000000,
	CLK_RATE_PROPAGATES | CLK_ALWAYS_ENABLED),
_CLK_P(CLK_S_CCSC, &clkgend, 0,
	0, &clk_clocks[CLK_S_D_REF]),
_CLK_P(CLK_S_STFE_FRC_1, &clkgend, 0,
	0, &clk_clocks[CLK_S_D_REF]),
_CLK_P(CLK_S_TSOUT_1, &clkgend, 0,
	0, &clk_clocks[CLK_S_D_REF]),
_CLK_P(CLK_S_MCHI, &clkgend, 0,
	0, &clk_clocks[CLK_S_D_REF]),

/* HDMI RX */
_CLK(CLK_S_PIX_HDMIRX, &clkgenhdmirx, 0,
	CLK_RATE_PROPAGATES | CLK_ALWAYS_ENABLED),	/* To MPE */
};

/* ========================================================================
   Name:	sasg2_clk_init()
   Description: SOC specific LLA initialization
   Returns:     'clk_err_t' error code.
   ======================================================================== */

/* To statisfy SYSCONF_ASSIGNU macro */
#define SYSCONF_CLAIMU plat_sysconf_claimu

int __init sasg2_clk_init(struct sysconf_field *(*plat_sysconf_claimu)
			 (int _nr, int _lsb, int _msb), struct clk *_sys_clk_in)
{
	int ret, i;

	for (i = 0; i < 2; ++i) {
		unsigned long reg_num = (i ? 2561 : 1557);
		clockgenb_cfg_fs[i].nsb =
			plat_sysconf_claimu(reg_num, 10, 13);
		clockgenb_cfg_fs[i].ndiv =
			plat_sysconf_claimu(reg_num, 15, 15);
		clockgenb_cfg_fs[i].bw_sel =
			plat_sysconf_claimu(reg_num, 16, 17);
		clockgenb_cfg_fs[i].npda =
			plat_sysconf_claimu(reg_num, 14, 14);
	};

	for (i = 0; i < 4; ++i) {
		/* Clockgen B-0 */
		clockgenb_fs[0][i].md =
			plat_sysconf_claimu(1558 + i * 4, 0, 4);
		clockgenb_fs[0][i].pe =
			plat_sysconf_claimu(1559 + i * 4, 0, 15);
		clockgenb_fs[0][i].sdiv =
			plat_sysconf_claimu(1560 + i * 4, 0, 2);
		clockgenb_fs[0][i].nsdiv =
			plat_sysconf_claimu(1557, 18 + i, 18 + i);
		clockgenb_fs[0][i].prog_en =
			plat_sysconf_claimu(1561 + i * 4, 0, 0);
		/* Clockgen B-1 */
		clockgenb_fs[1][i].md =
			plat_sysconf_claimu(2562 + i * 4, 0, 4);
		clockgenb_fs[1][i].pe =
			plat_sysconf_claimu(2563 + i * 4, 0, 15);
		clockgenb_fs[1][i].sdiv =
			plat_sysconf_claimu(2564 + i * 4, 0, 2);
		clockgenb_fs[1][i].nsdiv =
			plat_sysconf_claimu(2561, 18 + i, 18 + i);
		clockgenb_fs[1][i].prog_en =
			plat_sysconf_claimu(2565 + i * 4, 0, 0);

		/* Clockgen C */
		clockgenc_fs[i].md =
			plat_sysconf_claimu(2501 + i * 4, 0, 4);
		clockgenc_fs[i].pe =
			plat_sysconf_claimu(2502 + i * 4, 0, 15);
		clockgenc_fs[i].sdiv =
			plat_sysconf_claimu(2503 + i * 4, 0, 2);
		clockgenc_fs[i].nsdiv =
			plat_sysconf_claimu(2500, 18 + i, 18 + i);
		clockgenc_fs[i].prog_en =
			plat_sysconf_claimu(2504 + i * 4, 0, 0);

		/* Clockgen D */
		clockgend_fs[i].md =
			plat_sysconf_claimu(1505 + i * 4, 0, 4);
		clockgend_fs[i].pe =
			plat_sysconf_claimu(1506 + i * 4, 0, 15);
		clockgend_fs[i].sdiv =
			plat_sysconf_claimu(1507 + i * 4, 0, 2);
		clockgend_fs[i].nsdiv =
			plat_sysconf_claimu(1504, 18 + i, 18 + i);
		clockgend_fs[i].prog_en =
			plat_sysconf_claimu(1508 + i * 4, 0, 0);
	}

	SYSCONF_ASSIGNU(1504, 0, 17); /* nsdiv @ 18:21 */
	SYSCONF_ASSIGNU(2500, 0, 17); /* nsdiv @ 18:21 */
	SYSCONF_ASSIGNU(2555, 0, 15);
	SYSCONF_ASSIGNU(2556, 0, 31);
	SYSCONF_ASSIGNU(2557, 0, 31);
	SYSCONF_ASSIGNU(2558, 0, 0);

	clk_clocks[CLK_S_A0_REF].parent = _sys_clk_in;
	clk_clocks[CLK_S_A1_REF].parent = _sys_clk_in;
	clk_clocks[CLK_S_B_REF].parent = _sys_clk_in;
	clk_clocks[CLK_S_C_REF].parent = _sys_clk_in;
	clk_clocks[CLK_S_D_REF].parent = _sys_clk_in;

	cga_base[0] = ioremap_nocache(CKGA0_BASE_ADDRESS, 0x1000);
	cga_base[1] = ioremap_nocache(CKGA1_BASE_ADDRESS, 0x1000);
	cgb_base = ioremap_nocache(CKGB_BASE_ADDRESS, 0x1000);
	cgc_base = ioremap_nocache(CKGC_BASE_ADDRESS, 0x1000);
	cgd_base = ioremap_nocache(CKGD_BASE_ADDRESS, 0x1000);

	ret = clk_register_table(clk_clocks, CLK_S_B_REF, 1);

	ret |= clk_register_table(&clk_clocks[CLK_S_B_REF],
				ARRAY_SIZE(clk_clocks) - CLK_S_B_REF, 0);
	return ret;
}


