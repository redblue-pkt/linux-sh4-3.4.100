/*****************************************************************************
 *
 * File name   : clk-flexgen.h
 * Description : Clock Low Level API - Flexiclockgen support
 *
 * COPYRIGHT (C) 2013 STMicroelectronics - All Rights Reserved
 * May be copied or modified under the terms of the GNU General Public
 * License V2 __ONLY__.  See linux/COPYING for more information.
 *
 *****************************************************************************/

#include <linux/stm/clk.h>

/*
 * Flexiclockgen constants & types
 */

/* Clocks flags. Some can be "ORed".
    Examples:
      FS660C32 single CHAN output => FG_GFGOUT|FG_FS660C32|FG_FVCO|FG_CHAN
	This means there is no FVCO dedicated clock. Changing this clock
	rate will also change FVCO accordingly.
      PLL3200C32 FVCOBY2 => FG_GFGOUT|FG_PLL3200C32|FG_FVCOBY2
	This is a VCOby2 clock only
      PLL3200C32 PHI => FG_GFGOUT|FG_PLL3200C32|FG_FVCOBY2|FG_PHI
	No FVCO dedicated clock. Changing PHI will update FVCO
	accordingly.
*/
enum {
	/* GFG types */
	FG_PLL3200C32 = 1 << 0,		/* PLL3200C32 */
	FG_FS660C32 = 1 << 1,		/* FS660C32 */
	/* RESERVED TYPES = [7:2] */

	/* GFG outputs types */
	FG_FVCO = 1 << 8,		/* VCO freq (fs660c32 case) */
	FG_FVCOBY2 = 1 << 9,		/* VCO freq by 2 (pll3200c32 case) */
	FG_PHI = 1 << 10,		/* PHIx freq (pll3200c32 case) */
	FG_CHAN = 1 << 11,		/* CHANNELx freq (fs660c32 case) */
	/* RESERVED TYPES = [15:12] */

	/* Clock types */
	FG_SRCIN = 1 << 16,		/* Source clock input (srcclkin) */
	FG_REFIN = 1 << 17,		/* Ref clock input (refclkin) */
	FG_REFOUT = 1 << 18,		/* Ref clock output (refclkout) */
	FG_EREFOUT = 1 << 19,		/* External ref clock output
					  (ext_refclkout) */
	FG_GFGOUT = 1 << 20,		/* GFG output */
	FG_DIV = 1 << 21,		/* Final div output */
	FG_JEIN = 1 << 22,		/* clk_je_input */

	/* Misc */
	FG_BEAT = 1 << 30		/* Beat counter on this source */
};

/* 'refclkin', 'srcclkin', 'ext_refclkout' and GFG clocks
   declarations */
struct flexgen_src {
	int clkid;			/* Ex: CLK_A0_PLL0 */
	unsigned long flags;		/* Ex: FG_PLL3200C32|FG_FVCOBY2 */
	char idx;			/* Index: channel/PHIx/ref/src nb */
	char gfg;			/* GFG ID (for FG_GFGOUT types). */
};

/* Xbar inputs declarations */
struct flexgen_xbar {
	int clkid;			/* Ex: CLK_A0_PLL0 */
	struct flexgen_src *src;	/* Corresponding source */
};

/* Flexiclockgen infos */
struct flexgen {
	/* Flexiclockgen general infos */
	const char *name;		/* Clockgen name (ex: "A0") */
	unsigned long phys_addr;	/* Physical base addr */
	int nb_refin;			/* Number of refclkin */

	/* Ref clocks (refclkin) list */
	struct flexgen_src *refin;	/* Terminated with -1 */

	/* GFG outputs list */
	struct flexgen_src *gfgout;	/* Terminated with -1 */

	/* 'refclkout' path config */
	unsigned short refmuxmap;	/* 1=mux present, 0=bypass */
	unsigned short refdivmap;	/* 1=div present, 0=bypass */

	/* Ext ref clock out (ext_refclkout) list, or NULL */
	struct flexgen_src *erefout;	/* Terminated with -1 */

	/* Src clock (srclkin) list */
	struct flexgen_src *srcin;	/* Terminated with -1 */

	/* Cross bar inputs listed in 0 to MSB order. */
	struct flexgen_xbar *xbar_src;	/* Terminated with -1 */

	/* Final div outputs (0 to 63) */
	int div0;			/* First clock ID */
	int divlast;			/* Last clock ID */
	unsigned long syncl;		/* [31:0]: bitX=1 => semi-sync out */
	unsigned long synch;		/* [63:32]: bitX=1 => semi-sync out */

	/* Extra 'clk_je_input[]' sources for ref/obs */
	struct flexgen_src *jein;	/* Terminated with -1 */

	/* For code internal use */
	void *virt_addr;		/* Virtual base addr */
	int nb_gfg;			/* Number of GFGs = nb of refclkout */
};

/* Grouping 'struct clk' & flexiclockgen infos */
struct flexgen_clk {
	struct clk *clk;		/* Clock */
	struct flexgen *fg;		/* Flexgen the clock belongs to */
	struct flexgen_src *src;	/* Clock source if not div out */
	int div_idx;			/* Output div index, or -1 */
};

#define FCG_CONFIG(_num)		(0x0 + (_num) * 0x4)
#define GFG_CONFIG(_gfg_id, _num)	(0x2a0 + (_num) * 0x4 + (_gfg_id) * 0x28)

/*
 * Flexiclockgen generic functions
 */

/* ========================================================================
   Name:        flexgen_init
   Description: Flexgen lib init function
   Returns:     0=OK
   ======================================================================== */

int flexgen_init(struct flexgen *soc_flexgens, struct clk *soc_clocks);

/* ========================================================================
   Name:        flexgen_recalc
   Description: Read flexiclockgen HW status to fill in 'clk_p->rate'
   Returns:     0=OK, or CLK_ERR_BAD_PARAMETER
   ======================================================================== */

int flexgen_recalc(struct clk *clk_p);

/* ========================================================================
   Name:        flexgen_identify_parent
   Description: Identify cross bar source clock (GFG clk, REFCLK, SRCCLK)
   Returns:     0=OK
   ======================================================================== */

int flexgen_identify_parent(struct clk *clk_p);

/* ========================================================================
   Name:        flexgen_set_parent
   Description: Set clock source for clockgenA when possible
   Returns:     'clk_err_t' error code
   ======================================================================== */

int flexgen_set_parent(struct clk *clk_p, struct clk *src_p);

/* ========================================================================
   Name:        flexgen_set_rate
   Description: Set Flexiclockgen clock & update status
   Returns:     0=OK
   ======================================================================== */

int flexgen_set_rate(struct clk *clk_p, unsigned long freq);

/* ========================================================================
   Name:        flexgen_xable
   Description: Enable/disable clock & update status
   Returns:     0=OK
   ======================================================================== */

int flexgen_xable(struct clk *clk_p, unsigned long enable);

/* ========================================================================
   Name:        flexgen_observe
   Description: Configure JESS to observe flexiclockgen clocks
   Returns:     0=OK
   ======================================================================== */

int flexgen_observe(struct clk *clk_p, unsigned long *div_p);
