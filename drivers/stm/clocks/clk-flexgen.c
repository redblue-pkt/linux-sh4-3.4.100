/*****************************************************************************
 *
 * File name   : clk-flexgen.c
 * Description : Clock Low Level API - Flexiclockgen support
 *
 * COPYRIGHT (C) 2013 STMicroelectronics - All Rights Reserved
 * May be copied or modified under the terms of the GNU General Public
 * License V2 __ONLY__.  See linux/COPYING for more information.
 *
 *****************************************************************************/

 /* Flexiclockgen lib current limitations:
    - Does not support high speed direct connection to final div
    - Assume one to one mapping between prediv and final div
    - Does not support OR2 combinations at outputs
  */

/* ----- Modification history (most recent first)----
14/dec/13 fabrice.charpentier@st.com
	  Debug & error infos enhanced. Fixed issue with 'jein' clock type.
14/nov/13 carmelo.amoroso@st.com
	  Fix for checkpatch compliance
10/oct/13 fabrice.charpentier@st.com
	  FS660C32 set rate sequence revisited.
19/sep/13 fabrice.charpentier@st.com
	  PLL3200 is now bypassed before VCO reprogrammed.
17/sep/13 fabrice.charpentier@st.com
	  flexgen_div_set() revisited for semi-sync mode.
11/sep/13 fabrice.charpentier@st.com
	  flexgen_fs660c32_xable() revisited.
07/sep/13 fabrice.charpentier@st.com
	  Added support for semi-sync clocks
06/sep/13 fabrice.charpentier@st.com
	  Added 'clk_je_input' observation capability
03/sep/13 fabrice.charpentier@st.com
	  flexgen struct modified to add nb_refin. Clock obs fixed for erefout.
23/aug/13 fabrice.charpentier@st.com
	  Several bug fixes + debug & error messages.
19/aug/13 fabrice.charpentier@st.com
	  flexgen_div_set() revisited.
06/aug/13 fabrice.charpentier@st.com
	  Some rework on "struct flexgen".
01/aug/13 fabrice.charpentier@st.com
	  Lib now fully independant thanks to flexgen_init().
24/jul/13 fabrice.charpentier@st.com
	  Initial flexiclockgen lib. Must be SOC independant.
*/

#include "clk-flexgen.h"
#define CLKLLA_SYSCONF_LINUX	1 /* Required for oslayer */
#include "clock-oslayer.h"
#include "clk-common.h"

/* SOC clocks & flexiclockgens declarations.
   Filled thru flexgen_init() call */
static struct flexgen *flexgens;
static struct clk *clk_clocks;

/*
 * Local functions
 */

/* ========================================================================
   Name:        flexgen_get_src
   Description: Look for corresponding 'struct flexgen_src' in 'fg'
   Returns:     struct flexgen_src, or NULL
   ======================================================================== */

static struct flexgen_src *flexgen_get_src(int clkid, struct flexgen *fg)
{
	int i;

	/* Source is GFG out ? */
	for (i = 0; (fg->gfgout[i].clkid != -1) &&
	    (fg->gfgout[i].clkid != clkid); i++)
		;
	if (fg->gfgout[i].clkid != -1)
		return &(fg->gfgout[i]);

	/* Source is 'refclkin' ? */
	for (i = 0; (fg->refin[i].clkid != -1) &&
	    (fg->refin[i].clkid != clkid); i++)
		;
	if (fg->refin[i].clkid != -1)
		return &(fg->refin[i]);

	/* Source is 'srcclkin' ? */
	for (i = 0; (fg->srcin[i].clkid != -1) &&
	    (fg->srcin[i].clkid != clkid); i++)
		;
	if (fg->srcin[i].clkid != -1)
		return &(fg->srcin[i]);

	/* Source is 'clk_je_input' ? */
	for (i = 0; (fg->jein[i].clkid != -1) &&
	    (fg->jein[i].clkid != clkid);
	    i++);
	if (fg->jein[i].clkid != -1)
		return &(fg->jein[i]);

	pr_err("stm-clk ERROR: Unrecognized clock type (%s, %d)\n",
	       clk_clocks[clkid].name, clkid);
	return NULL;
}

/* ========================================================================
Name:        flexgen_get_fcg
Description: Update FCG_CONFIG reg offset & shift for given 'idx'
Returns:     Nothing
======================================================================== */

static void flexgen_get_fcg(int idx, unsigned long *regoffs, int *shift)
{
	*regoffs = (idx / 4) * 4;
	*shift = (idx % 4) * 8;
}

/* ========================================================================
   Name:        flexgen_xbar_set
   Description: Configure XBAR
     out_idx = output index
     fg_base = flexiclockgen virtual base address
     in_idx  = input index
   Returns:     0=OK
   ======================================================================== */

static int flexgen_xbar_set(int out_idx, void *fg_base, int in_idx)
{
	unsigned long data;
	void *addr;
	unsigned long regoffs;
	int shift;

	flexgen_get_fcg(out_idx, &regoffs, &shift);
	addr = fg_base + FCG_CONFIG(6) + regoffs;
	data = CLK_READ(addr);
	data &= ~(0x3f << shift);
	data |= (((1 << 6) | in_idx) << shift);
	CLK_WRITE(addr, data);

	return 0;
}

/* ========================================================================
   Name:        flexgen_xbar_bypass
   Description: Reconfigure Xbar so that input 'in_idx' is no longer
		used, using first 'refclkin' instead. Or restore.
     fg      = flexgen
     in0, in1= inputs to avoid (in0=0 to 31, in1=32 to 63)
     restore = if 1, restore previous bypass
   Returns:     0=OK
   ======================================================================== */

static int flexgen_xbar_bypass(struct flexgen *fg, unsigned long long in,
			       int restore)
{
	int o, i, change;
	static unsigned long long rerouted = 0ll; /* Rerouted outputs */
	static unsigned char input[64];

	if (!restore) {
		int ref_idx;

		/* What is xbar input of first 'refclkin' ? */
		for (ref_idx = 0; fg->xbar_src[ref_idx].clkid != -1; ref_idx++)
			if (fg->xbar_src[ref_idx].src->flags & FG_REFIN)
				break;
		clk_debug("  refclkin=xbar input %d\n", ref_idx);

		/* Routing matching childs to first 'refclkin' */
		for (o = 0; o < (fg->divlast - fg->div0 + 1); o++) {
			if (!clk_clocks[fg->div0 + o].parent) {
				pr_err("flexgen_xbar_bypass() ERROR: no parent for clock @ xbar in %d\n", o);
				continue;
			}
			for (i = 0; i < 64; i++) {
				change = (in >> i) & 1;
				if (!change)
					continue;
				if (clk_clocks[fg->div0 + o].parent->id == fg->xbar_src[i].clkid)
					break;
			}
			if (i == 64)
				continue;
			clk_debug("  '%s' (out %d) must be rerouted\n", clk_clocks[fg->div0 + o].name, o);
			flexgen_xbar_set(o, fg->virt_addr, ref_idx);

			/* Storing route restore infos */
			rerouted |= (1 << o);
			input[o] = i;
		}
		clk_debug("  Clocks rerouted=0x%llx\n", rerouted);
	} else {
		/* Restoring childs routing */
		for (o = 0; o < (fg->divlast - fg->div0 + 1); o++) {
			change = (rerouted >> o) & 1;
			if (!change)
				continue;
			clk_debug("  Restoring '%s' route\n", clk_clocks[fg->div0 + o].name);
			flexgen_xbar_set(o, fg->virt_addr, input[o]);
		}
	}

	return 0;
}

/*
 * Exported functions
 */

/* ========================================================================
   Name:        flexgen_init
   Description: Flexgen lib init function
   Returns:     0=OK
   ======================================================================== */

int flexgen_init(struct flexgen *soc_flexgens, struct clk *soc_clocks)
{
	struct flexgen *fg;
	int i;

	if (!soc_flexgens || !soc_clocks) {
		pr_err("stm-clk ERROR: lost in flexgen_init()\n");
		return CLK_ERR_BAD_PARAMETER;
	}

	flexgens = soc_flexgens;
	clk_clocks = soc_clocks;

	/* Updating per flexiclockgen instance, some internal infos */
	for (i = 0, fg = &soc_flexgens[0]; fg->name; fg = &soc_flexgens[i++]) {
		unsigned long type;
		int j;

		/* Registering flexiclockgen virtual base addresses */
		fg->virt_addr = ioremap_nocache(fg->phys_addr, 0x1000);

		/* Computing number of GFGs.
		   Since there is no GFG ID field, let's check when
		   GFG type is changing. Assuming cross bar input order
		   & GFG order the same.
		   This type is currently coded in 'flags[7:0]' */
		type = fg->gfgout[0].flags & 0xff;
		fg->nb_gfg = 1;
		for (j = 1; fg->gfgout[j].clkid != -1; j++)
			if ((fg->gfgout[j].flags & 0xff) != type)
				(fg->nb_gfg)++;

		/* Assigning correct source to each Xbar input */
		for (j = 0; fg->xbar_src[j].clkid != -1; j++)
			fg->xbar_src[j].src = flexgen_get_src(fg->xbar_src[j].clkid, fg);
	}

	return 0;
}

/* ========================================================================
   Name:        flexgen_get
   Description: Returns flexgen structure corresponding to given clock
   Returns:     struct flexgen *
   ======================================================================== */

int flexgen_get(struct clk *clk_p, struct flexgen_clk *fg_clk)
{
	struct flexgen *fg;
	int i;

	/* Going thru all known flexiclockgens */
	for (fg = flexgens; fg->name; fg++) {
		/* Is it a divider output clock ? */
		if (clk_p->id >= fg->div0 && clk_p->id <= fg->divlast) {
			fg_clk->clk = clk_p;
			fg_clk->fg = fg;
			fg_clk->src = NULL;
			fg_clk->div_idx = clk_p->id - fg->div0;
			return 0;
		}

		/* Is it a GFG output clock ? */
		for (i = 0; (fg->gfgout[i].clkid != -1) &&
		     (fg->gfgout[i].clkid != clk_p->id); i++)
			;
		if (fg->gfgout[i].clkid != -1) {
			fg_clk->clk = clk_p;
			fg_clk->fg = fg;
			fg_clk->src = &(fg->gfgout[i]);
			fg_clk->div_idx = -1;
			return 0;
		}

		/* Is it a 'refclkin' clock ? */
		for (i = 0; (fg->refin[i].clkid != -1) &&
		     (fg->refin[i].clkid != clk_p->id); i++)
			;
		if (fg->refin[i].clkid != -1) {
			fg_clk->clk = clk_p;
			fg_clk->fg = fg;
			fg_clk->src = &(fg->refin[i]);
			fg_clk->div_idx = -1;
			return 0;
		}

		/* Is it a 'ext_refclkout' clock ? */
		if (fg->erefout) {
			for (i = 0; (fg->erefout[i].clkid != -1) &&
				    (fg->erefout[i].clkid != clk_p->id); i++)
				;
			if (fg->erefout[i].clkid != -1) {
				fg_clk->clk = clk_p;
				fg_clk->fg = fg;
				fg_clk->src = &(fg->erefout[i]);
				fg_clk->div_idx = -1;
				return 0;
			}
		}

		/* Is it a 'srcclkin' clock ? */
		if (fg->srcin) {
			for (i = 0; (fg->srcin[i].clkid != -1) &&
			     (fg->srcin[i].clkid != clk_p->id); i++)
				;
			if (fg->srcin[i].clkid != -1) {
				fg_clk->clk = clk_p;
				fg_clk->fg = fg;
				fg_clk->src = &(fg->srcin[i]);
				fg_clk->div_idx = -1;
				return 0;
			}
		}

		/* Is it a 'clk_je_input' clock ? */
		if (fg->jein) {
			for (i = 0; (fg->jein[i].clkid != -1) &&
			     (fg->jein[i].clkid != clk_p->id); i++)
				;
			if (fg->jein[i].clkid != -1) {
				fg_clk->clk = clk_p;
				fg_clk->fg = fg;
				fg_clk->src = &(fg->jein[i]);
				fg_clk->div_idx = -1;
				return 0;
			}
		}
	}

	/* Not found in any flexiclockgen */
	pr_err("stm-clk ERROR: '%s' does not belong to a "
	       "flexiclockgen\n", clk_p->name);
	return CLK_ERR_BAD_PARAMETER;
}

/* ========================================================================
   Name:        flexgen_refout_recalc
   Description: Read HW status for 'refclkout' or 'ext_refclkout' clocks
   Returns:     0=OK, or CLK_ERR_BAD_PARAMETER
   ======================================================================== */

int flexgen_refout_recalc(struct flexgen_clk *fg_clk)
{
	int idx;
	unsigned long parent_rate;

	/* Reading 'muxdiv' ratio (1 to 16). Order is
	   MuxDiv0   => refclkout[0] => GFG0
	   ...
	   MuxDivX   => refclkout[X] => GFGX
	   MuxDivX+1 => ext_refclkout0
	   MuxDivX+2 => ext_refclkout1
	   ... etc
	 */
	if (fg_clk->src->flags & FG_REFOUT)
		idx = fg_clk->src->idx;
	else /* 'ext_refclkout' case */
		idx = fg_clk->fg->nb_gfg + fg_clk->src->idx;

	/* Any div capability ? */
	parent_rate = clk_get_rate(clk_get_parent(fg_clk->clk));
	if (fg_clk->fg->refdivmap & (1 << idx)) {
		unsigned long data;

		/* Yes */
		data = CLK_READ(fg_clk->fg->virt_addr + FCG_CONFIG(3 + (idx / 8)));
		/* Fields are: [3:0], [7:4], [11:8], ...etc */
		data = ((data >> ((idx % 8) * 4)) & 0xf) + 1;

		fg_clk->clk->rate = parent_rate / data;
	} else
		/* No. Direct connection to parent */
		fg_clk->clk->rate = parent_rate;

	return 0;
}

/* ========================================================================
   Name:        flexgen_pll3200c32_recalc
   Description: Read PLL3200C32 GFG HW status
   Returns:     0=OK, or CLK_ERR_BAD_PARAMETER
   ======================================================================== */

int flexgen_pll3200c32_recalc(struct flexgen_clk *fg_clk)
{
	unsigned long data;
	struct stm_pll pll = {
		.type = stm_pll3200c32,
	};
	int err, gfg_id;
	unsigned long parent_rate;

	if (!fg_clk || !fg_clk->src) {
		pr_err("stm-clk ERROR: lost in flexgen_pll3200c32_recalc()\n");
		return CLK_ERR_BAD_PARAMETER;
	}

	gfg_id = fg_clk->src->gfg;
	parent_rate = clk_get_rate(clk_get_parent(fg_clk->clk));
	clk_debug("flexgen_pll3200c32_recalc(%s), gfg_id=%d, prate=%lu\n",
		  fg_clk->clk->name, gfg_id, parent_rate);

	if (fg_clk->src->flags & FG_FVCOBY2) {
		data = CLK_READ(fg_clk->fg->virt_addr + GFG_CONFIG(gfg_id, 0));
		if (data & (1 << 8)) {
			clk_debug("  PLL is powered down\n");
			fg_clk->clk->rate = 0; /* PLL is powered DOWN */
			return 0;
		}

		/* Reading FVCOBY2 clock */
		data = CLK_READ(fg_clk->fg->virt_addr + GFG_CONFIG(gfg_id, 1));
		pll.idf = data & 0x7;
		pll.ndiv = (data >> 16) & 0xff;
		err = stm_clk_pll_get_rate(parent_rate, &pll, &fg_clk->clk->rate);
		clk_debug("  idf=0x%lx ndiv=0x%lx => rate=%lu\n",
			  pll.idf, pll.ndiv, fg_clk->clk->rate);
		if (err != 0) {
			pr_err("stm-clk ERROR: Can't compute PLL3200C32 '%s' "
			       "freq with idf/ndiv=0x%lx/%lx\n",
			       fg_clk->clk->name, pll.idf, pll.ndiv);
			return CLK_ERR_BAD_PARAMETER;
		}
		parent_rate = fg_clk->clk->rate;
	}

	if (fg_clk->src->flags & FG_PHI) {
		/* Reading ODFx ratio (1 to 63) */
		data = CLK_READ(fg_clk->fg->virt_addr
				+ GFG_CONFIG(gfg_id, 5 + fg_clk->src->idx))
				& 0x3f;
		if (!data)
			data = 1;
		clk_debug("  odf=%lu\n", data);
		fg_clk->clk->rate = parent_rate / data;
	}

	return 0;
}

/* ========================================================================
   Name:        flexgen_fs660c32_recalc
   Description: Read FS660C32 GFG HW status
   Returns:     0=OK, or CLK_ERR_BAD_PARAMETER
   ======================================================================== */

int flexgen_fs660c32_recalc(struct flexgen_clk *fg_clk)
{
	int err = 0;
	unsigned long data;
	struct stm_fs fs_vco = {
		.type = stm_fs660c32vco
	};
	struct stm_fs fs = {
		.type = stm_fs660c32
	};
	int channel;
	unsigned long parent_rate;
	int gfg_id;

	if (!fg_clk || !fg_clk->src) {
		pr_err("stm-clk ERROR: lost in flexgen_fs660c32_recalc()\n");
		return CLK_ERR_BAD_PARAMETER;
	}

	gfg_id = fg_clk->src->gfg;
	parent_rate = clk_get_rate(clk_get_parent(fg_clk->clk));
	channel = fg_clk->src->idx;
	clk_debug("flexgen_fs660c32_recalc(%s), gfg_id=%d, prate=%lu, chan=%d\n",
		  fg_clk->clk->name, gfg_id, parent_rate, channel);

	if (fg_clk->src->flags & FG_FVCO) {
		/* Checking FSYN analog part (~NPDPLL) */
		data = CLK_READ(fg_clk->fg->virt_addr + GFG_CONFIG(gfg_id, 0));
		if ((data & (1 << 12)) != 0) {
			fg_clk->clk->rate = 0;
			clk_debug("  FS is powered down (NPDPLL)\n");
			return 0;
		}

		/* FVCO is running. Computing freq */
		data = CLK_READ(fg_clk->fg->virt_addr + GFG_CONFIG(gfg_id, 1));
		fs_vco.ndiv = (data >> 16) & 0x7;
		err = stm_clk_fs_get_rate(parent_rate, &fs_vco, &fg_clk->clk->rate);
		if (err != 0) {
			pr_err("stm-clk ERROR: Can't compute FS660C32 VCO "
			       "freq with ndiv=0x%lx\n", fs_vco.ndiv);
			return CLK_ERR_BAD_PARAMETER;
		}
		parent_rate = fg_clk->clk->rate;
	}

	if (fg_clk->src->flags & FG_CHAN) {
		/* Checking FSYN digital part (~NRSTx) */
		data = CLK_READ(fg_clk->fg->virt_addr + GFG_CONFIG(gfg_id, 0));
		if ((data & (1 << channel)) != 0) {
			fg_clk->clk->rate = 0;
			clk_debug("  FS digital reset (NRSTx)\n");
			return 0;
		}
		/* Checking FSYN digital part (~NSBx) */
		data = CLK_READ(fg_clk->fg->virt_addr + GFG_CONFIG(gfg_id, 0));
		if ((data & (0x100 << channel)) != 0) {
			fg_clk->clk->rate = 0;
			clk_debug("  FS digital power down (NSBx)\n");
			return 0;
		}

		/* FSYN up & running. Computing frequency */
		data = CLK_READ(fg_clk->fg->virt_addr + GFG_CONFIG(gfg_id, (5 + channel)));
		fs.mdiv = (data >> 15) & 0x1f;		/* MD[19:15] */
		fs.pe = (data & 0x7fff);		/* PE[14:0] */
		fs.sdiv = (data >> 20) & 0xf;		/* SDIV[23:20] */
		fs.nsdiv = (data >> 24) & 0x1;		/* NSDIV[24] */
		clk_debug("  mdiv/pe/sdiv/nsdiv=0x%lx/%lx/%lx/%lx\n",
			  fs.mdiv, fs.pe, fs.sdiv, fs.nsdiv);
		err = stm_clk_fs_get_rate(parent_rate, &fs, &fg_clk->clk->rate);
		if (err != 0) {
			pr_err("stm-clk ERROR: Can't compute FS660C32 '%s' "
			       "freq with mdiv/pe/sdiv/nsdiv=0x%lx/%lx/%lx"
			       "/%lx\n", fg_clk->clk->name,
			       fs.mdiv, fs.pe, fs.sdiv, fs.nsdiv);
			return CLK_ERR_BAD_PARAMETER;
		}
	}

	return 0;
}

/* ========================================================================
   Name:        flexgen_div_recalc
   Description: Read dividers HW status
   Returns:     0=OK, or CLK_ERR_BAD_PARAMETER
   ======================================================================== */

int flexgen_div_recalc(struct flexgen_clk *fg_clk)
{
	unsigned long regoffs, data, ratio;
	int shift;
	unsigned long parent_rate;

	if (!fg_clk || (fg_clk->div_idx == -1)) {
		pr_err("stm-clk ERROR: lost in flexgen_div_recalc()\n");
		return CLK_ERR_BAD_PARAMETER;
	}

	clk_debug("flexgen_div_recalc(%s)\n", fg_clk->clk->name);
	flexgen_get_fcg(fg_clk->div_idx, &regoffs, &shift);

	/* Is clock enabled at crossbar output ?
	   Note that this disable feature should normally not be used
	   prefering disable feature of final divider instead. */
	data = CLK_READ(fg_clk->fg->virt_addr + FCG_CONFIG(6) + regoffs) >> (shift + 6);
	if ((data & 1) == 0) { /* Bit 6 = enable */
		clk_debug("  cross bar output DISABLED\n");
		fg_clk->clk->rate = 0;
		return 0;
	}

	/* Reading predivider */
	data =  CLK_READ(fg_clk->fg->virt_addr + FCG_CONFIG(22 + fg_clk->div_idx));
	ratio = (data & 0x3FF) + 1;
	parent_rate = clk_get_rate(clk_get_parent(fg_clk->clk));
	clk_debug("  prediv=%lu, prate=%lu\n", ratio, parent_rate);
	fg_clk->clk->rate = parent_rate / ratio;

	/* Reading final divider */
	data = CLK_READ(fg_clk->fg->virt_addr + FCG_CONFIG(89 + fg_clk->div_idx));
	if ((data & (1 << 6)) == 0) {
		clk_debug("  final div output DISABLED\n");
		fg_clk->clk->rate = 0; /* Disabled */
	} else {
		ratio = (data & 0x3F) + 1;
		clk_debug("  final div ratio=%lu\n", ratio);
		fg_clk->clk->rate /= ratio;
	}

	return 0;
}

/* ========================================================================
   Name:        flexgen_recalc
   Description: Read flexiclockgen HW status to fill in 'clk_p->rate'
   Returns:     0=OK, or CLK_ERR_BAD_PARAMETER
   ======================================================================== */

int flexgen_recalc(struct clk *clk_p)
{
	struct flexgen_clk fg_clk;
	unsigned long parent_rate;

	/* Associating clock with flexiclockgen */
	if (flexgen_get(clk_p, &fg_clk) != 0) {
		pr_err("stm-clk ERROR: lost in flexgen_recalc()\n");
		return CLK_ERR_BAD_PARAMETER;
	}

	/* Is it a divider output clock ? */
	if (fg_clk.div_idx != -1)
		return flexgen_div_recalc(&fg_clk);

	if (!fg_clk.src) {
		pr_err("stm-clk ERROR: flexgen_recalc(%s). No source\n",
		       clk_p->name);
		return CLK_ERR_BAD_PARAMETER;
	}

	/* Is it a GFG output clock ? */
	if (fg_clk.src->flags & FG_GFGOUT) {
		/* PLL3200C32 clock ? */
		if (fg_clk.src->flags & FG_PLL3200C32)
			return flexgen_pll3200c32_recalc(&fg_clk);
		/* FS660C32 clock ? */
		if (fg_clk.src->flags & FG_FS660C32)
			return flexgen_fs660c32_recalc(&fg_clk);

		/* Unsupported case */
		pr_err("stm-clk ERROR: '%s', unsupported flexiclockgen GFG "
		       "type for recalc\n", clk_p->name);
		return CLK_ERR_BAD_PARAMETER;
	}

	/* Is it a 'refclkout' or 'ext_refclkout' clock ? */
	if ((fg_clk.src->flags & FG_REFOUT) || (fg_clk.src->flags & FG_EREFOUT))
		return flexgen_refout_recalc(&fg_clk);

	/* Other clock types are copy of their parent */
	if (clk_p->parent)
		parent_rate = clk_get_rate(clk_get_parent(clk_p));
	else
		parent_rate = 12121212;
	clk_p->rate = parent_rate;

	return 0;
}

/* ========================================================================
   Name:        flexgen_identify_parent
   Description: Identify cross bar source clock (GFG clk, REFCLK, SRCCLK)
   Returns:     0=OK
   ======================================================================== */

int flexgen_identify_parent(struct clk *clk_p)
{
	unsigned long src_sel, regoffs;
	int shift;
	int xbar_size;
	struct flexgen_clk fg_clk;

	/* Associating clock with flexiclockgen */
	if (flexgen_get(clk_p, &fg_clk) != 0) {
		pr_err("stm-clk ERROR: lost in flexgen_identify_parent()\n");
		return CLK_ERR_BAD_PARAMETER;
	}

	clk_debug("flexgen_identify_parent(%s)\n", clk_p->name);

	/* Div output ? */
	if (fg_clk.div_idx != -1) {
		/* How many xbar sources ? */
		for (xbar_size = 0; (fg_clk.fg)->xbar_src[xbar_size].clkid != -1;
			xbar_size++)
			;

		/* Getting FCG_CONFIG proper offset & shift */
		flexgen_get_fcg(fg_clk.div_idx, &regoffs, &shift);

		/* Identifying crossbar source */
		src_sel = (CLK_READ((fg_clk.fg)->virt_addr + FCG_CONFIG(6) + regoffs) >> shift) & 0x3f;
		if (src_sel > xbar_size)
			/* Source currently on unsupported clock input */
			return CLK_ERR_BAD_PARAMETER;
		clk_p->parent = &clk_clocks[(fg_clk.fg)->xbar_src[src_sel].clkid];
		clk_debug("  Final div output => parent='%s'\n", clk_p->parent->name);

		return 0;
	}

	if (!fg_clk.src) {
		pr_err("stm-clk ERROR: flexgen_identify_parent(). No source "
		       "for '%s'\n", fg_clk.clk->name);
		return CLK_ERR_BAD_PARAMETER;
	}

	/* Is it a 'refclkout' or 'ext_refclkout' clock ? */
	if ((fg_clk.src->flags & FG_REFOUT) || (fg_clk.src->flags & FG_EREFOUT)) {
		int idx;

		/* Reading 'muxdiv' mux select. Order is
		   MuxDiv0 => refclkout0 => GFG0
		   MuxDivX => refclkoutX => GFGX
		   MuxDivX+1 = ext_refclkout0
		   MuxDivX+2 = ext_refclkout1
		   MuxDivX+3 = srcclkin0
		   MuxDivX+4 = srcclkin1
		   ... etc
		 */
		if (fg_clk.src->flags & FG_REFOUT)
			idx = fg_clk.src->idx;
		else if (fg_clk.src->flags & FG_EREFOUT)
			idx = fg_clk.fg->nb_gfg + fg_clk.src->idx;
		else
			idx = fg_clk.fg->nb_gfg + fg_clk.fg->nb_refin + fg_clk.src->idx;

		/* Mux capability for this clock ? */
		if (fg_clk.fg->refmuxmap & (1 << idx)) {
			/* Yes. Identifying parent */
			src_sel = CLK_READ(fg_clk.fg->virt_addr + FCG_CONFIG(0 + (idx / 8)));
			/* Fields are: [3:0], [7:4], [11:8], ...etc */
			src_sel = (src_sel >> ((idx % 8) * 4)) & 0xf;
		} else
			src_sel = 0; /* No. Direct connection to refclkin[0] */
		clk_p->parent = &clk_clocks[(fg_clk.fg)->refin[src_sel].clkid];

		return 0;
	}

	return 0;
}

/* ========================================================================
   Name:        flexgen_set_parent
   Description: Set cross bar source clock
   Returns:     'clk_err_t' error code
   ======================================================================== */

int flexgen_set_parent(struct clk *clk_p, struct clk *src_p)
{
	unsigned long clk_src;
	int xbar_size;
	struct flexgen_clk fg_clk, fg_src;

	if (!clk_p || !src_p) {
		pr_err("stm-clk ERROR: lost in flexgen_set_parent()\n");
		return CLK_ERR_BAD_PARAMETER;
	}

	clk_debug("flexgen_set_parent(%s, %s)\n", clk_p->name, src_p->name);

	/* Associating clocks with flexiclockgen */
	if (flexgen_get(clk_p, &fg_clk) != 0 ||
	    flexgen_get(src_p, &fg_src) != 0)
		return CLK_ERR_BAD_PARAMETER;

	/* Is it div output ? */
	if (fg_clk.div_idx != -1) {
		/* How many xbar sources ? */
		for (xbar_size = 0; (fg_clk.fg)->xbar_src[xbar_size].clkid != -1;
			xbar_size++)
			;

		/* Which index is requested source ? */
		for (clk_src = 0; (clk_src < xbar_size) &&
		     (src_p->id != (fg_clk.fg)->xbar_src[clk_src].clkid); clk_src++)
			;
		if (clk_src == xbar_size)
			/* Invalid source */
			return CLK_ERR_BAD_PARAMETER;

		/* Configuring xbar */
		flexgen_xbar_set(fg_clk.div_idx, (fg_clk.fg)->virt_addr, clk_src);

		clk_p->parent = &clk_clocks[src_p->id];
		return flexgen_div_recalc(&fg_clk);
	}

	if (!fg_clk.src)
		return CLK_ERR_BAD_PARAMETER;
#if 0
	/* refclkout or ext_refclkout ? */
	/* FIXME: to be completed */
	if (fg_clk.src->flags & FG_REFOUT || fg_clk.src->flags & FG_EREFOUT) {
	}
#endif

	/* Other clocks do not have any source mux capability */
	return CLK_ERR_BAD_PARAMETER;
}

/* ========================================================================
   Name:        flexgen_pll3200c32_set_rate
   Description: Set PLL3200C32 GFG clock & update status
		freq = PHI0 output freq if FG_PHI is set, FVCOBY2 freq else
   Returns:     'clk_err_t' error code
   ======================================================================== */

int flexgen_pll3200c32_set_rate(struct flexgen_clk *fg_clk,
				unsigned long freq)
{
	int err = 0;
	int gfg_id;
	unsigned long parent_rate, data;
	struct stm_pll pll = {
		.type = stm_pll3200c32,
	};
	unsigned long flags, odf = 0;
	unsigned long vcoby2_rate = 0;
	struct flexgen *fg;

	if (!fg_clk) {
		pr_err("stm-clk ERROR: lost in flexgen_pll3200c32_set_rate()\n");
		return CLK_ERR_BAD_PARAMETER;
	}

	gfg_id = fg_clk->src->gfg;
	parent_rate = clk_get_rate(clk_get_parent(fg_clk->clk));
	flags = fg_clk->src->flags;
	fg = fg_clk->fg;

	/* Computing parameters.
	   Either FVCO and/or ODF */

	if (flags & FG_PHI) {
		if (flags & FG_FVCOBY2) {
			/* Computing ODF: min FVCOBY2=800Mhz.
			   Using ODF to go below. */
			if (freq < 800000000) {
				odf = 800000000 / freq;
				if (800000000 % freq)
					odf += 1;
			} else
				odf = 1;
			vcoby2_rate = freq * odf;
		} else {
			/* Computing params of PHIx only*/
			odf = clk_best_div(parent_rate, freq);
		}
	} else
		vcoby2_rate = freq;
	if (flags & FG_FVCOBY2) {
		/* Computing params for FVCOBY2 */
		err = stm_clk_pll_get_params(parent_rate, vcoby2_rate, &pll);
		if (err != 0)
			return CLK_ERR_BAD_PARAMETER;
	}

	/* Parameters computed.
	   Let's program FVCOBY2 and/or PHIx */

	if (flags & FG_FVCOBY2) {
		int i;
		unsigned long long in;

		/* Before shutting down VCO, need to reroute all childs
		   clocks (those for same GFG) to first 'refclkin' */
		for (i = 0, in = 0ll; fg->xbar_src[i].clkid != -1; i++) {
			if (fg->xbar_src[i].src->gfg != fg_clk->src->gfg)
				continue;
			in |= (1 << i);
		}
		flexgen_xbar_bypass(fg, in, 0);

		/* PLL power down */
		data = CLK_READ(fg_clk->fg->virt_addr + GFG_CONFIG(gfg_id, 0));
		CLK_WRITE(fg_clk->fg->virt_addr + GFG_CONFIG(gfg_id, 0), data | (1 << 8));

		/* Configure ndiv and idf */
		data = CLK_READ(fg_clk->fg->virt_addr + GFG_CONFIG(gfg_id, 1)) & 0xff00fff8;
		CLK_WRITE(fg_clk->fg->virt_addr + GFG_CONFIG(gfg_id, 1),
			(data | (pll.ndiv << 16) | pll.idf));
	}

	if (flags & FG_PHI) {
		/* Configure ODF */
		data = CLK_READ(fg_clk->fg->virt_addr + GFG_CONFIG(gfg_id, 5)) & ~(0x3f);
		CLK_WRITE(fg_clk->fg->virt_addr + GFG_CONFIG(gfg_id, 5), data | odf);
	}

	if (flags & FG_FVCOBY2) {
		/* PLL power up */
		data = CLK_READ(fg_clk->fg->virt_addr + GFG_CONFIG(gfg_id, 0));
		CLK_WRITE(fg_clk->fg->virt_addr + GFG_CONFIG(gfg_id, 0), data & ~(1 << 8));

		/* Check PLL Lock */
		while ((data  & (1 << 24)) != (1 << 24)) {
			data = CLK_READ(fg_clk->fg->virt_addr + GFG_CONFIG(gfg_id, 0));
		}

		/* Rerouting child clocks to original parent */
		flexgen_xbar_bypass(fg, 0ll, 1);
	}

	return flexgen_pll3200c32_recalc(fg_clk);
}

/* ========================================================================
   Name:        flexgen_fs660c32_set_rate
   Description: Set FS660C32 GFG clock & update status
		freq = channel freq if FG_CHAN set, else FVCO freq
   Returns:     'clk_err_t' error code
   ======================================================================== */

int flexgen_fs660c32_set_rate(struct flexgen_clk *fg_clk, unsigned long freq)
{
	int err;
	struct stm_fs fs_vco = {
		.type = stm_fs660c32vco
	};
	struct stm_fs fs = {
		.type = stm_fs660c32,
		.nsdiv = 0xff
	};
	unsigned long channel, parent_rate, data, flags;
	int gfg_id;

	if (!fg_clk) {
		pr_err("stm-clk ERROR: lost in flexgen_fs660c32_set_rate()\n");
		return CLK_ERR_BAD_PARAMETER;
	}

	parent_rate = clk_get_rate(clk_get_parent(fg_clk->clk));
	flags = fg_clk->src->flags;
	if (flags & FG_FVCO)
		err = stm_clk_fs_get_params(parent_rate, freq, &fs_vco);
	else
		err = stm_clk_fs_get_params(parent_rate, freq, &fs);
	if (err != 0) {
		pr_err("stm-clk ERROR: Can't compute FS660 params for "
		       "'%s'=%lu Hz with prate=%lu\n", fg_clk->clk->name,
		       freq, parent_rate);
		return err;
	}

	gfg_id = fg_clk->src->gfg;
	channel = fg_clk->src->idx;

	if (flags & FG_FVCO) { /* FVCO prog required */
		clk_debug("  Programming FVCO with ndiv=0x%lx\n", fs_vco.ndiv);

		/* Digital reset (reset[3:0]) */
		data = CLK_READ(fg_clk->fg->virt_addr + GFG_CONFIG(gfg_id, 0));
		data |= 0xf;
		CLK_WRITE(fg_clk->fg->virt_addr + GFG_CONFIG(gfg_id, 0), data);

		/* PLL power down  */
		data = CLK_READ(fg_clk->fg->virt_addr + GFG_CONFIG(gfg_id, 0));
		CLK_WRITE(fg_clk->fg->virt_addr + GFG_CONFIG(gfg_id, 0), data | (1 << 8));

		/* 'ndiv' programmation */
		data = CLK_READ(fg_clk->fg->virt_addr + GFG_CONFIG(gfg_id, 1)) & 0xfff8ffff;
		CLK_WRITE(fg_clk->fg->virt_addr + GFG_CONFIG(gfg_id, 1),
			  data | (fs_vco.ndiv << 16));

		/* PLL power up */
		data = CLK_READ(fg_clk->fg->virt_addr + GFG_CONFIG(gfg_id, 0));
		CLK_WRITE(fg_clk->fg->virt_addr + GFG_CONFIG(gfg_id, 0), data & ~(1 << 8));

		/* Check PLL Lock */
		while ((data  & (1 << 24)) != (1 << 24)) {
			data = CLK_READ(fg_clk->fg->virt_addr + GFG_CONFIG(gfg_id, 0));
		}

		/* Release digital resets */
		data = CLK_READ(fg_clk->fg->virt_addr + GFG_CONFIG(gfg_id, 0));
		CLK_WRITE(fg_clk->fg->virt_addr + GFG_CONFIG(gfg_id, 0), (data & ~0xf));
	}

	if (flags & FG_CHAN) { /* Channel prog required */
		clk_debug("  Programming chan %d with md/pe/sd/nsd="
			  "0x%lx/%lx/%lx/%lx\n",
			  channel, fs.mdiv, fs.pe, fs.sdiv, fs.nsdiv);

		/* Release standby (/NSBi): note that this means forcing
		   clock enable but mandatory to allow EN_PROGx */
		data = CLK_READ(fg_clk->fg->virt_addr + GFG_CONFIG(gfg_id, 0));
		CLK_WRITE(fg_clk->fg->virt_addr + GFG_CONFIG(gfg_id, 0), (data & ~(0x100 << channel)));

		/* FS programming */
		data = CLK_READ(fg_clk->fg->virt_addr + GFG_CONFIG(gfg_id, (channel + 5)));
		data &= ~(0x1 << 24 | 0xf << 20 | 0x1f << 15 | 0x7fff);
		data |= (fs.nsdiv << 24 | fs.sdiv << 20 | fs.mdiv << 15 | fs.pe);
		CLK_WRITE(fg_clk->fg->virt_addr + GFG_CONFIG(gfg_id, (channel + 5)), data);

		/* Enable channel EN_PROGx */
		data = CLK_READ(fg_clk->fg->virt_addr + GFG_CONFIG(gfg_id, 3));
		CLK_WRITE(fg_clk->fg->virt_addr + GFG_CONFIG(gfg_id, 3), (data | (1 << channel)));

		/* Disable channel EN_PROGx to stop further changes */
		data = CLK_READ(fg_clk->fg->virt_addr + GFG_CONFIG(gfg_id, 3));
		CLK_WRITE(fg_clk->fg->virt_addr + GFG_CONFIG(gfg_id, 3), (data & ~(1 << channel)));
	}

	return flexgen_fs660c32_recalc(fg_clk);
}

/* ========================================================================
   Name:        flexgen_div_set
   Description: Set divider ratio & update status
   Returns:     'clk_err_t' error code
   ======================================================================== */

int flexgen_div_set(struct flexgen_clk *fg_clk, unsigned long *div_p)
{
	unsigned long pdiv = 0, fdiv = 0; /* Pre & final divs */
	int sync = 0, beatnb = -1;
	struct flexgen *fg;
	int i;
	unsigned long data;

	if (!fg_clk || (fg_clk->div_idx == -1)) {
		pr_err("stm-clk ERROR: lost in flexgen_div_set()\n");
		return CLK_ERR_BAD_PARAMETER;
	}

	fg = fg_clk->fg;

	/* Async or sync ? */
	sync = (fg_clk->div_idx < 32) ? (fg->syncl >> fg_clk->div_idx) & 1 :
		(fg->synch >> (fg_clk->div_idx - 32)) & 1;
	clk_debug("flexgen_div_set(%s, div_idx=%d, div=%lu) => sync=%d\n",
		  fg_clk->clk->name, fg_clk->div_idx, *div_p, sync);

	/* Computing predivider & final divider ratios */
	if (*div_p <= 64) {
		pdiv = 0; /* bypass prediv */
		fdiv = *div_p - 1;
	} else if (*div_p <= (1024 * 64)) {
		unsigned long dev = 0xffffffff, new_dev; /* deviation */
		unsigned long f, p, new_div;

		for (f = 64; f; f--) {
			p = *div_p / f;
			if (!p || (p > 1024))
				continue;
			new_div = f * p;
			if (new_div > *div_p)
			    new_dev = new_div - *div_p;
			else
			    new_dev = *div_p - new_div;
			if (new_dev < dev) {
				/* Best case */
				fdiv = f - 1;
				pdiv = p - 1;
				dev = new_dev;
			}
		}
		if (dev == 0xffffffff) /* No solution found */
			return CLK_ERR_BAD_PARAMETER;
	} else
		return CLK_ERR_BAD_PARAMETER;

	/* If sync mode, may need to reprogram beat counter */
	if (sync) {
		/* Which beat counter number for this clock ? */
		for (i = 0, beatnb = -1; fg->xbar_src[i].clkid != -1; i++) {
			/* A beatcounter on this source ? */
			if (fg->xbar_src[i].src && (fg->xbar_src[i].src->flags & FG_BEAT))
				beatnb++;
			if (fg->xbar_src[i].clkid == fg_clk->clk->parent->id)
				break; /* Source found */
		}
		if (fg->xbar_src[i].clkid == -1 || !fg->xbar_src[i].src ||
		    !(fg->xbar_src[i].src->flags & FG_BEAT)) {
			clk_debug("  '%s' has NO source with beatcounter => ASYNC\n", fg_clk->clk->name);
			sync = 0;
		} else
			clk_debug("  '%s' is sourced thru beatcounter %d\n", fg_clk->clk->name, beatnb);
	}

	clk_debug("  requested div=%lu => prediv=%lu, findiv=%lu, sync=%d\n",
		  *div_p, pdiv, fdiv, sync);

	/* Pre divider config */
	CLK_WRITE(fg_clk->fg->virt_addr + FCG_CONFIG(22) + (fg_clk->div_idx * 4), pdiv);
	/* Final divider config */
	data = CLK_READ(fg_clk->fg->virt_addr + FCG_CONFIG(89) + (fg_clk->div_idx * 4));
	data &= ~((1 << 7) | 0x3f);
	CLK_WRITE(fg_clk->fg->virt_addr + FCG_CONFIG(89) + (fg_clk->div_idx * 4), data | (!sync << 7) | fdiv);

	if (sync) {
		unsigned long beat, div, max = 0;
		void *addr;
		int bc_changed = 0, j;

		/* Beat counter value ? */
		beat = CLK_READ(fg->virt_addr + FCG_CONFIG(155 + (beatnb / 4))) & 0xff;

		/* Computing new beat counter value taking highest final ratio
		   applied on all sync clocks.
		   WARNING: This makes sense only for 1, 2, 4, 8, 16.. div series */
		for (i = 0; i < (fg->divlast - fg->div0 + 1); i++) {
			/* If not same source, skip */
			if (clk_clocks[fg->div0 + i].parent !=
			    clk_clocks[fg->div0 + fg_clk->div_idx].parent)
				continue;
			/* If not sync requested, skip */
			sync = (i < 32) ? (fg->syncl >> i) & 1 :
				(fg->synch >> (i - 32)) & 1;
			if (!sync)
				continue;
			/* Taking max ratio */
			div = CLK_READ(fg_clk->fg->virt_addr + FCG_CONFIG(89) + (i * 4)) & 0x3f;
			if (div > max)
				max = div;
		}
		clk_debug("  beat count=0x%x, max ratio=0x%x\n", beat, max);

		/* If beat counter != max ratio, need to reprogram it */
		if (beat != max) {
			beat = CLK_READ(fg->virt_addr + FCG_CONFIG(155 + (beatnb / 4))) & ~0xff;
			beat |= max;
			CLK_WRITE(fg->virt_addr + FCG_CONFIG(155 + (beatnb / 4)), beat);
			bc_changed = 1;
		} else
			bc_changed = 0;

		/* To force resync, restarting
		   - all SYNC clocks with same source if beat counter has changed
		   - or only current clock */
		if (bc_changed) {
			i = 0;
			j = fg->divlast - fg->div0 + 1;
		} else {
			i = fg_clk->div_idx;
			j = i + 1;
		}
		for (; i < j; i++) {
			/* If not same source, skip */
			if (clk_clocks[fg->div0 + i].parent !=
			    clk_clocks[fg->div0 + fg_clk->div_idx].parent)
				continue;
			/* If not sync requested, skip */
			sync = (i < 32) ? (fg->syncl >> i) & 1 :
				(fg->synch >> (i - 32)) & 1;
			if (!sync)
				continue;
			/* If clock not active, skip */
			addr = fg->virt_addr + FCG_CONFIG(89) + (i * 4);
			data = CLK_READ(addr);
			if ((data & (1 << 6)) == 0)
				continue;	/* OFF */
			clk_debug("  Restarting '%s' (idx %d)\n", clk_clocks[fg->div0 + i].name, i);
			/* Disable, then reenable */
			CLK_WRITE(addr, data & ~(1 << 6));
			CLK_WRITE(addr, data | (1 << 6));
		}
	}

	return flexgen_div_recalc(fg_clk);
}

/* ========================================================================
   Name:        flexgen_set_rate
   Description: Set Flexiclockgen clock & update status
   Returns:     0=OK
   ======================================================================== */

int flexgen_set_rate(struct clk *clk_p, unsigned long freq)
{
	struct flexgen_clk fg_clk;

	if (!clk_p || !freq) {
		pr_err("stm-clk ERROR: lost in flexgen_set_rate()\n");
		return CLK_ERR_BAD_PARAMETER;
	}

	clk_debug("flexgen_set_rate(%s, %lu)\n", clk_p->name, freq);

	/* Associating clock with flexiclockgen */
	if (flexgen_get(clk_p, &fg_clk) != 0)
		return CLK_ERR_BAD_PARAMETER;

	/* Is it a divider output clock ? */
	if (fg_clk.div_idx != -1) {
		unsigned long parent_rate, div;

		parent_rate = clk_get_rate(clk_get_parent(clk_p));
		div = clk_best_div(parent_rate, freq);
		return flexgen_div_set(&fg_clk, &div);
	}

	/* Is it a GFG output clock ? */
	if (fg_clk.src->flags & FG_GFGOUT) {
		/* PLL3200C32 clock ? */
		if (fg_clk.src->flags & FG_PLL3200C32)
			return flexgen_pll3200c32_set_rate(&fg_clk, freq);
		/* FS660C32 clock ? */
		if (fg_clk.src->flags & FG_FS660C32)
			return flexgen_fs660c32_set_rate(&fg_clk, freq);

		/* Unsupported case */
		pr_err("stm-clk ERROR: '%s', unsupported flexiclockgen GFG "
		       "type for set_rate\n", clk_p->name);
		return CLK_ERR_BAD_PARAMETER;
	}

	/* Other clock types are considered not having any
	   rate programmation feature */

	return 0;
}

/* ========================================================================
   Name:        flexgen_pll3200c32_xable
   Description: Enable/disable clocks from PLL3200C32
		type = 'OR' of FG_PHI/FG_FVCOBY2.
   Returns:     'clk_err_t' error code
   ======================================================================== */

int flexgen_pll3200c32_xable(struct flexgen_clk *fg_clk, int enable)
{
	int gfg_id;
	unsigned long data, flags;

	if (!fg_clk || !fg_clk->src) {
		pr_err("stm-clk ERROR: lost in flexgen_pll3200c32_xable()\n");
		return CLK_ERR_BAD_PARAMETER;
	}

	clk_debug("flexgen_pll3200c32_xable(%s, %d)\n", fg_clk->clk->name, enable);

	gfg_id = fg_clk->src->gfg;
	flags = fg_clk->src->flags;

	if (enable) {
		if (flags & FG_FVCOBY2) {
			/* PLL power up */
			data = CLK_READ(fg_clk->fg->virt_addr + GFG_CONFIG(gfg_id, 0));
			CLK_WRITE(fg_clk->fg->virt_addr + GFG_CONFIG(gfg_id, 0), data & ~(1 << 8));

			/* Wait for lock */
			while ((data  & (1 << 24)) != (1 << 24)) {
				data = CLK_READ(fg_clk->fg->virt_addr + GFG_CONFIG(gfg_id, 0));
			}
		}

		if (flags & FG_PHI) {
			/* ODF enable */
			data = CLK_READ(fg_clk->fg->virt_addr + GFG_CONFIG(gfg_id, 5));
			CLK_WRITE(fg_clk->fg->virt_addr + GFG_CONFIG(gfg_id, 5), data & ~(1 << 6));
		}

		return flexgen_pll3200c32_recalc(fg_clk);
	} else {
		if (flags & FG_PHI) {
			/* ODF disable */
			data = CLK_READ(fg_clk->fg->virt_addr + GFG_CONFIG(gfg_id, 5));
			CLK_WRITE(fg_clk->fg->virt_addr + GFG_CONFIG(gfg_id, 5), data | (1 << 6));
		}

		if (flags & FG_FVCOBY2) {
			/* PLL power down */
			data = CLK_READ(fg_clk->fg->virt_addr + GFG_CONFIG(gfg_id, 0));
			CLK_WRITE(fg_clk->fg->virt_addr + GFG_CONFIG(gfg_id, 0), data | (1 << 8));
		}

		fg_clk->clk->rate = 0;
	}

	return 0;
}

/* ========================================================================
   Name:        flexgen_fs660c32_xable
   Description: Enable/disable clocks from FS660C32
   Returns:     'clk_err_t' error code
   ======================================================================== */

int flexgen_fs660c32_xable(struct flexgen_clk *fg_clk, int enable)
{
	int gfg_id, channel;
	unsigned long data;
	void *addr;

	if (!fg_clk || !fg_clk->src) {
		pr_err("stm-clk ERROR: lost in flexgen_fs660c32_xable()\n");
		return CLK_ERR_BAD_PARAMETER;
	}

	clk_debug("flexgen_fs660c32_xable(%s, %d)\n", fg_clk->clk->name, enable);

	gfg_id = fg_clk->src->gfg;
	channel = fg_clk->src->idx;
	/* GFGx_CONFIG0 */
	addr = fg_clk->fg->virt_addr + GFG_CONFIG(gfg_id, 0);

	if (enable) {
		data = CLK_READ(addr);

		/* PLL power up */
		data &= ~(1 << 12);
		CLK_WRITE(addr, data);

		/* Wait for lock */
		while ((data  & (1 << 24)) != (1 << 24)) {
			data = CLK_READ(addr);
		}

		/* Deactivating all NRSTi */
		data &= ~(0xf);
		CLK_WRITE(addr, data);

		/* If channel, deactivating NSBi */
		if (channel != -1) {
			CLK_WRITE(addr, data & ~(1 << (8 + channel)));
		}

		return flexgen_fs660c32_recalc(fg_clk);
	} else {
		data = CLK_READ(addr);

		/* If channel, activating NSBi */
		if (channel != -1) {
			data |= (1 << (8 + channel));
			CLK_WRITE(addr, data);
		}

		/* PLL power down if no channel=VCO, or all channels OFFs */
		if (channel == -1 || ((data & (0xf << 8)) == (0xf << 8))) {
			CLK_WRITE(addr, data | (1 << 12));
		}

		fg_clk->clk->rate = 0;
	}

	return 0;
}

/* ========================================================================
   Name:        flexgen_div_xable
   Description: Enable/disable clocks at final divider stage
   Returns:     'clk_err_t' error code
   ======================================================================== */

int flexgen_div_xable(struct flexgen_clk *fg_clk, int enable)
{
	unsigned long regoffs, data;
	int shift;
	void *addr;

	if (!fg_clk || (fg_clk->div_idx == -1)) {
		pr_err("stm-clk ERROR: lost in flexgen_div_xable()\n");
		return CLK_ERR_BAD_PARAMETER;
	}

	flexgen_get_fcg(fg_clk->div_idx, &regoffs, &shift);

	if (enable) {
		/* Just in case because not supposed to be OFF,
		   enabling clock at cross bar stage. */
		addr = fg_clk->fg->virt_addr + FCG_CONFIG(6) + regoffs;
		data = CLK_READ(addr);
		CLK_WRITE(addr, data | (1 << (shift + 6)));

		/* Enabling clock at final divider stage */
		addr = fg_clk->fg->virt_addr + FCG_CONFIG(89) + (fg_clk->div_idx * 4);
		data = CLK_READ(addr);
		CLK_WRITE(addr, data | (1 << 6));

		return flexgen_div_recalc(fg_clk);
	} else {
		/* Disabling clock oat final divider stage */
		addr = fg_clk->fg->virt_addr + FCG_CONFIG(89) + (fg_clk->div_idx * 4);
		data = CLK_READ(addr);
		CLK_WRITE(addr, data & ~(1 << 6));

		fg_clk->clk->rate = 0;
	}

	return 0;
}

/* ========================================================================
   Name:        flexgen_xable
   Description: Enable/disable clock & update status
   Returns:     0=OK
   ======================================================================== */

int flexgen_xable(struct clk *clk_p, unsigned long enable)
{
	struct flexgen_clk fg_clk;

	/* Associating clock with flexiclockgen */
	if (flexgen_get(clk_p, &fg_clk) != 0) {
		pr_err("stm-clk ERROR: lost in flexgen_xable()\n");
		return CLK_ERR_BAD_PARAMETER;
	}

	/* Is it a divider output clock ? */
	if (fg_clk.div_idx != -1)
		return flexgen_div_xable(&fg_clk, enable);

	/* Is it a GFG output clock ? */
	if (fg_clk.src->flags & FG_GFGOUT) {
		/* PLL3200C32 clock ? */
		if (fg_clk.src->flags & FG_PLL3200C32)
			return flexgen_pll3200c32_xable(&fg_clk, enable);
		/* FS660C32 clock ? */
		if (fg_clk.src->flags & FG_FS660C32)
			return flexgen_fs660c32_xable(&fg_clk, enable);

		/* Unsupported case */
		pr_err("stm-clk ERROR: '%s', unsupported flexiclockgen GFG "
		       "type for xable\n", clk_p->name);
		return CLK_ERR_BAD_PARAMETER;
	}

	/* Other clock types have no xable feature */

	return 0;
}
