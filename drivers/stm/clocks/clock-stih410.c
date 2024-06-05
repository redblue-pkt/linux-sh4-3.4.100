/*****************************************************************************
 *
 * File name   : clock-stih410.c
 * Description : Low Level API - HW specific implementation
 *
 * COPYRIGHT (C) 2013 STMicroelectronics - All Rights Reserved
 * May be copied or modified under the terms of the GNU General Public
 * License V2 __ONLY__.  See linux/COPYING for more information.
 *
 *****************************************************************************/

/* Compilation flags
   CONFIG_MACH_STM_COEMUL_MEMIN => Emulation/co-emul case.
     Allow to not access missing IPs or injected clocks.
 */

/* ----- Modification history (most recent first)----
17/jan/14 fabrice.charpentier@st.com
	  Audio clockgen bug fixes & debug info enhancements.
14/jan/14 fabrice.charpentier@st.com
	  Clockgen E support removed (handled by HDMI RX driver)
13/dec/13 fabrice.charpentier@st.com
	  D2 clocks order changed for kernel successive run fix.
12/dec/13 fabrice.charpentier@st.com
	  HADES clocks fixes.
25/nov/13 fabrice.charpentier@st.com
	  Initial H410 clock LLA.
*/

/* Includes --------------------------------------------------------------- */

#include <linux/io.h>
#include <linux/delay.h>
#include <linux/stm/stih407.h>
#include <linux/stm/clk.h>
#include <linux/stm/sysconf.h>

#include "clock-regs-stih410.h"
#include "clock-stih410.h"
#include "clock-oslayer.h"
#include "clk-common.h"
#include "clk-flexgen.h"

struct sysconf_field *stih410_sysconf_claim(int num, int lsb, int msb)
{
	return sysconf_claim(SYSCONFG_GROUP(num), SYSCONF_OFFSET(num), \
		lsb, msb, "Clk lla");
}
#define SYSCONF_CLAIMU stih410_sysconf_claim

/* Sysconf fields declarations */
SYSCONF_DECLAREU(5105, 0, 0);
SYSCONF_DECLAREU(5105, 1, 1);
SYSCONF_DECLAREU(5106, 0, 0);
SYSCONF_DECLAREU(5106, 1, 5);
SYSCONF_DECLAREU(5106, 25, 27);
SYSCONF_DECLAREU(5108, 0, 7);
SYSCONF_DECLAREU(5108, 8, 13);
SYSCONF_DECLAREU(5543, 0, 0);

/* Prototypes */
static struct clk clk_clocks[];

/* SOC top input clocks. */
#define SYS_CLKIN	30


/*	Clockgen "A0"
		GFGs
			FG_PLL3200C32
				CLK_A0_PLL0, FG_FVCOBY2 | FG_PHI, 0
		FG_REFIN, CLK_A0_REF
		FG_DIV, CLK_IC_LMI0, CLK_IC_LMI0
	Clockgen "C0"
		GFGs
			FG_PLL3200C32
				CLK_C0_PLL0, FG_FVCOBY2 | FG_PHI, 0
			FG_PLL3200C32
				CLK_C0_PLL1, FG_FVCOBY2 | FG_PHI, 0
			FG_FS660C32
				CLK_C0_FS_VCO, FG_FS660C32 | FG_FVCO, 0
				CLK_C0_FS0, FG_CHAN, 0
				CLK_C0_FS1, FG_CHAN, 1
				CLK_C0_FS2, FG_CHAN, 2
				CLK_C0_FS3, FG_CHAN, 3
		FG_REFIN, CLK_C0_REF
		FG_DIV, CLK_ICN_GPU, CLK_FC_HADES
	Clockgen "D0"
		GFGs
			FG_FS660C32
				CLK_D0_FS_VCO, FG_FS660C32 | FG_FVCO, 0
				CLK_D0_FS0, FG_CHAN, 0
				CLK_D0_FS1, FG_CHAN, 1
				CLK_D0_FS2, FG_CHAN, 2
				CLK_D0_FS3, FG_CHAN, 3
		FG_REFIN, CLK_D0_REF
		FG_DIV, CLK_PCM_0, CLK_PCMR0_MASTER
	Clockgen "D2"
		GFGs
			FG_FS660C32
				CLK_D2_FS_VCO, FG_FS660C32 | FG_FVCO, 0
				CLK_D2_FS0, FG_CHAN, 0
				CLK_D2_FS1, FG_CHAN, 1
				CLK_D2_FS2, FG_CHAN, 2
				CLK_D2_FS3, FG_CHAN, 3
		FG_REFIN, CLK_D2_REF, 0
		FG_REFIN, CLK_D2_REF1, 1
		FG_SRCIN, CLK_TMDSOUT_HDMI, 0
		FG_SRCIN, CLK_PIX_HD_P_IN, 1
		FG_DIV, CLK_PIX_MAIN_DISP, CLK_REF_HDMIPHY
	Clockgen "D3"
		GFGs
			FG_FS660C32
				CLK_D3_FS_VCO, FG_FS660C32 | FG_FVCO, 0
				CLK_D3_FS0, FG_CHAN, 0
				CLK_D3_FS1, FG_CHAN, 1
				CLK_D3_FS2, FG_CHAN, 2
				CLK_D3_FS3, FG_CHAN, 3
		FG_REFIN, CLK_D3_REF
		FG_DIV, CLK_STFE_FRC1, CLK_LPC_1
 */

/* Cross bar sources in connected order */
static struct flexgen_src gfgout_A0[] = {
	{CLK_A0_PLL0, FG_GFGOUT | FG_PLL3200C32 | FG_FVCOBY2 | FG_PHI, 0, 0},
	{-1} /* Keep this last */
};

static struct flexgen_src refin_A0[] = {
	{CLK_A0_REF, FG_REFIN, 0},	/* refclkin0 */
	{-1} /* Keep this last */
};

static struct flexgen_xbar xbar_src_A0[] = {
	{CLK_A0_PLL0},		/* PLL3200 PHI0 */
	{CLK_A0_REF},		/* refclkin[0] */
	{-1} /* Keep this last */
};

static struct flexgen_src gfgout_C0[] = {
	{CLK_C0_PLL0, FG_GFGOUT | FG_PLL3200C32 | FG_FVCOBY2 | FG_PHI, 0, 0},
	{CLK_C0_PLL1, FG_GFGOUT | FG_PLL3200C32 | FG_FVCOBY2 | FG_PHI, 0, 1},
	{CLK_C0_FS_VCO, FG_GFGOUT | FG_FS660C32 | FG_FVCO, 0, 2},
	{CLK_C0_FS0, FG_GFGOUT | FG_FS660C32 | FG_CHAN, 0, 2},
	{CLK_C0_FS1, FG_GFGOUT | FG_FS660C32 | FG_CHAN, 1, 2},
	{CLK_C0_FS2, FG_GFGOUT | FG_FS660C32 | FG_CHAN, 2, 2},
	{CLK_C0_FS3, FG_GFGOUT | FG_FS660C32 | FG_CHAN, 3, 2},
	{-1} /* Keep this last */
};

static struct flexgen_src refin_C0[] = {
	{CLK_C0_REF, FG_REFIN, 0},	/* refclkin0 */
	{-1} /* Keep this last */
};

static struct flexgen_xbar xbar_src_C0[] = {
	{CLK_C0_PLL0},		/* PLL3200 PHI0 */
	{CLK_C0_PLL1},		/* PLL3200 PHI0 */
	{CLK_C0_FS0},		/* Chan 0 */
	{CLK_C0_FS1},
	{CLK_C0_FS2},
	{CLK_C0_FS3},
	{CLK_C0_REF},		/* refclkin[0] */
	{-1} /* Keep this last */
};

static struct flexgen_src gfgout_D0[] = {
	{CLK_D0_FS_VCO, FG_GFGOUT | FG_FS660C32 | FG_FVCO, 0, 0},
	{CLK_D0_FS0, FG_GFGOUT | FG_FS660C32 | FG_CHAN, 0, 0},
	{CLK_D0_FS1, FG_GFGOUT | FG_FS660C32 | FG_CHAN, 1, 0},
	{CLK_D0_FS2, FG_GFGOUT | FG_FS660C32 | FG_CHAN, 2, 0},
	{CLK_D0_FS3, FG_GFGOUT | FG_FS660C32 | FG_CHAN, 3, 0},
	{-1} /* Keep this last */
};

static struct flexgen_src refin_D0[] = {
	{CLK_D0_REF, FG_REFIN, 0},	/* refclkin0 */
	{-1} /* Keep this last */
};

static struct flexgen_src erefout_D0[] = {
	{CLK_USB2_PHY, FG_EREFOUT, 0},	/* ext_refclkout0 */
	{-1} /* Keep this last */
};

static struct flexgen_xbar xbar_src_D0[] = {
	{CLK_D0_FS0},
	{CLK_D0_FS1},
	{CLK_D0_FS2},
	{CLK_D0_FS3},
	{CLK_D0_REF},
	{-1} /* Keep this last */
};

static struct flexgen_src gfgout_D2[] = {
	{CLK_D2_FS_VCO, FG_GFGOUT | FG_FS660C32 | FG_FVCO, 0, 0},
	{CLK_D2_FS0, FG_GFGOUT | FG_FS660C32 | FG_CHAN | FG_BEAT, 0, 0},
	{CLK_D2_FS1, FG_GFGOUT | FG_FS660C32 | FG_CHAN | FG_BEAT, 1, 0},
	{CLK_D2_FS2, FG_GFGOUT | FG_FS660C32 | FG_CHAN, 2, 0},
	{CLK_D2_FS3, FG_GFGOUT | FG_FS660C32 | FG_CHAN, 3, 0},
	{-1} /* Keep this last */
};

static struct flexgen_src refin_D2[] = {
	{CLK_D2_REF, FG_REFIN, 0},			/* refclkin0 */
	{CLK_D2_REF1, FG_REFIN, 1},			/* refclkin1 */
	{-1} /* Keep this last */
};

static struct flexgen_src srcin_D2[] = {
	{CLK_TMDSOUT_HDMI, FG_SRCIN | FG_BEAT, 0},	/* srcclkin0 */
	{CLK_PIX_HD_P_IN, FG_SRCIN | FG_BEAT, 1},	/* srcclkin1 */
	{-1} /* Keep this last */
};

static struct flexgen_src je_src_D2[] = {
	{CLK_13_FROM_D0, FG_JEIN, 0},			/* clk_je_input[0] */
	{CLK_USB_480, FG_JEIN, 2},			/* clk_je_input[2] */
	{CLK_USB_UTMI, FG_JEIN, 5},			/* clk_je_input[5] */
	{-1} /* Keep this last */
};

static struct flexgen_xbar xbar_src_D2[] = {
	{CLK_D2_FS0},
	{CLK_D2_FS1},
	{CLK_D2_FS2},
	{CLK_D2_FS3},
	{CLK_D2_REF},					/* refclkin[0] */
	{CLK_D2_REF1},					/* refclkin[1], no source */
	{CLK_TMDSOUT_HDMI},				/* srcclkin[0] */
	{CLK_PIX_HD_P_IN},				/* srcclkin[1] */
	{CLK_13_FROM_D0},				/* clk_je_input[0] */
	{-1} /* Keep this last */
};

static struct flexgen_src gfgout_D3[] = {
	{CLK_D3_FS_VCO, FG_GFGOUT | FG_FS660C32 | FG_FVCO, 0, 0},
	{CLK_D3_FS0, FG_GFGOUT | FG_FS660C32 | FG_CHAN, 0, 0},
	{CLK_D3_FS1, FG_GFGOUT | FG_FS660C32 | FG_CHAN, 1, 0},
	{CLK_D3_FS2, FG_GFGOUT | FG_FS660C32 | FG_CHAN, 2, 0},
	{CLK_D3_FS3, FG_GFGOUT | FG_FS660C32 | FG_CHAN, 3, 0},
	{-1} /* Keep this last */
};

static struct flexgen_src refin_D3[] = {
	{CLK_D3_REF, FG_REFIN, 0},	/* refclkin0 */
	{-1} /* Keep this last */
};

static struct flexgen_xbar xbar_src_D3[] = {
	{CLK_D3_FS0},
	{CLK_D3_FS1},
	{CLK_D3_FS2},
	{CLK_D3_FS3},
	{CLK_D3_REF},		/* refclkin[0] */
	{-1} /* Keep this last */
};

/* Flexiclockgen hierarchical description
	Clockgen name (ex: "A0")
	Physical base address
	Number of 'refclkin' (even if not connected)
	Refclkin list
	GFG outputs list
	Refclkout config (muxmap, divmap)
	Ext refclkout list, or NULL
	Srcclkin list, or NULL
	Xbar inputs list (in cross bar input order)
	Div outputs (Div0, DivLast)
*/

static struct flexgen flexgens[] = {
	{
		"A0",
		CKG_A0_BASE_ADDRESS,
		2,		/* Nb of 'refclkin' */
		refin_A0,	/* 'refclkin' list */
		gfgout_A0,	/* GFG output clocks */
		0x7, 0x6,	/* refclkout config */
		NULL,		/* ext_refclkout clocks, or NULL */
		NULL,		/* srcclkin clocks, or NULL */
		xbar_src_A0,
		CLK_IC_LMI0, CLK_IC_LMI0
	},
	{
		"C0",
		CKG_C0_BASE_ADDRESS,
		2,		/* Nb of 'refclkin' */
		refin_C0,	/* 'refclkin' list */
		gfgout_C0,	/* GFG output clocks */
		0x1f, 0x18,	/* refclkout config */
		NULL,		/* ext_refclkout clocks, or NULL */
		NULL,		/* srcclkin clocks, or NULL */
		xbar_src_C0,
		CLK_ICN_GPU, CLK_FC_HADES
	},
	{
		"D0",
		CKG_D0_BASE_ADDRESS,
		2,		/* Nb of 'refclkin' */
		refin_D0,	/* 'refclkin' list */
		gfgout_D0,	/* GFG output clocks */
		0x7, 0x6,	/* refclkout config */
		erefout_D0,	/* ext_refclkout clocks, or NULL */
		NULL,		/* srcclkin clocks, or NULL */
		xbar_src_D0,
		CLK_PCM_0, CLK_D0_SPARE_13
	},
	{
		"D2",
		CKG_D2_BASE_ADDRESS,
		2,		/* Nb of 'refclkin' */
		refin_D2,	/* 'refclkin' list */
		gfgout_D2,	/* GFG output clocks */
		0x7, 0x6,	/* refclkout config */
		NULL,		/* ext_refclkout clocks, or NULL */
		srcin_D2,	/* srcclkin clocks, or NULL */
		xbar_src_D2,	/* Xbar inputs */
		CLK_PIX_MAIN_DISP, CLK_REF_HDMIPHY,
		0x29ff, 0,	/* Semi sync outputs:
				   0, 1, 2, 3, 4, 5, 6, 7, 8, 11, 13 */
		je_src_D2
	},
	{
		"D3",
		CKG_D3_BASE_ADDRESS,
		2,		/* Nb of 'refclkin' */
		refin_D3,	/* 'refclkin' list */
		gfgout_D3,	/* GFG output clocks */
		0x7, 0x6,	/* refclkout config */
		NULL,		/* ext_refclkout clocks, or NULL */
		NULL,		/* srcclkin clocks, or NULL */
		xbar_src_D3,
		CLK_STFE_FRC1, CLK_LPC_1
	},
	{
		NULL /* Keep this last */
	}
};

/* ========================================================================
   Name:        fclkgen_recalc
   Description: Get CKG GFG programmed clocks frequencies
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int fclkgen_recalc(struct clk *clk_p)
{
	unsigned long parent_rate = 0;

	if (!clk_p)
		return CLK_ERR_BAD_PARAMETER;

	clk_debug("fclkgen_recalc(%s)\n", clk_p->name);

	if (clk_p->parent)
		parent_rate = clk_get_rate(clk_get_parent(clk_p));

	switch (clk_p->id) {
	/* Flexiclockgen lib handled clocks */
	case CLK_A0_REF ... CLK_IC_LMI0:
	case CLK_C0_REF ... CLK_FC_HADES:
	case CLK_D0_REF ... CLK_USB2_PHY:
	case CLK_D2_REF ... CLK_D2_FS3:
	case CLK_PIX_MAIN_DISP ... CLK_REF_HDMIPHY:
	case CLK_D3_REF ... CLK_LPC_1:
		return flexgen_recalc(clk_p);

	/* Special cases */
	case CLK_13_FROM_D0:
		clk_p->rate = parent_rate;
		break;
	case CLK_EXT2F_A9_DIV2:
		clk_p->rate = parent_rate / 2;
		break;
	case CLK_TMDSOUT_HDMI:
		/* =TMDS_HDMI/some_ratio in HDMI PHY
		   but not supported by clock LLA */
	case CLK_PIX_HD_P_IN:
		/* It's a pad input. No way to get its freq */
	case CLK_USB_480 ... CLK_USB_UTMI:
		/* je_clk_input special clocks. */
		clk_p->rate = 12121212;
		break;

	/* Unexpected clocks */
	default:
		return CLK_ERR_BAD_PARAMETER;
	}

	return 0;
}

/* ========================================================================
   Name:        fclkgen_identify_parent
   Description: Identify GFG source clock
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int fclkgen_identify_parent(struct clk *clk_p)
{
	if (!clk_p)
		return CLK_ERR_BAD_PARAMETER;

	clk_debug("fclkgen_identify_parent(%s)\n", clk_p->name);

	switch (clk_p->id) {
	/* Single source clocks */
	case CLK_A0_REF ... CLK_A0_PLL0:
	case CLK_C0_REF ... CLK_C0_FS3:
	case CLK_EXT2F_A9_DIV2:
	case CLK_D0_REF ... CLK_D0_FS3:
	case CLK_D2_REF ... CLK_D2_FS3:
	case CLK_TMDSOUT_HDMI:	/* From HDMI TX PHY */
	case CLK_PIX_HD_P_IN:	/* From pad */
	case CLK_13_FROM_D0 ... CLK_USB_UTMI: /* clk_je_inputs */
	case CLK_D3_REF ... CLK_D3_FS3:
		break;

	/* Flexiclocken lib handled clocks */
	default:
		return flexgen_identify_parent(clk_p);
	}

	return 0;
}

/* ========================================================================
   Name:        fclkgen_init
   Description: Read HW status to initialize 'struct clk' structure.
   Returns:     'clk_err_t' error code.
   ======================================================================== */

static int fclkgen_init(struct clk *clk_p)
{
	int err = 0;

	if (!clk_p)
		return CLK_ERR_BAD_PARAMETER;

	clk_debug("fclkgen_init(%s)\n", clk_p->name);

	err = fclkgen_identify_parent(clk_p);
	if (!err)
		err = fclkgen_recalc(clk_p);

	return err;
}

/* ========================================================================
   Name:        fclkgen_set_parent
   Description: Set clock parent
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int fclkgen_set_parent(struct clk *clk_p, struct clk *src_p)
{
	if (!clk_p)
		return CLK_ERR_BAD_PARAMETER;

	clk_debug("fclkgen_set_parent(%s)\n", clk_p->name);

	switch (clk_p->id) {
	/* Single source clocks not handled by flexiclockgen */
	case CLK_SYSIN:
	case CLK_A0_REF:
	case CLK_C0_REF:
	case CLK_EXT2F_A9_DIV2:
	case CLK_D0_REF:
	case CLK_USB2_PHY:
	case CLK_D2_REF ... CLK_D2_REF1:
	case CLK_TMDSOUT_HDMI ... CLK_USB_UTMI: /* srcclkinX + je_inX */
	case CLK_D3_REF:
		break;

	/* Flexiclocken lib handled clocks */
	default:
		return flexgen_set_parent(clk_p, src_p);
	}

	return 0;
}

/* ========================================================================
   Name:        fclkgen_set_rate
   Description: Flexiclockgen: Set clock frequency
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int fclkgen_set_rate(struct clk *clk_p, unsigned long freq)
{
	int err;

	if (!clk_p)
		return CLK_ERR_BAD_PARAMETER;

	clk_debug("fclkgen_set_rate(%s, %lu)\n", clk_p->name, freq);

	switch (clk_p->id) {
	/* Flexiclockgen lib handled clocks */
	case CLK_A0_REF ... CLK_IC_LMI0:
	case CLK_C0_REF ... CLK_FC_HADES:
	case CLK_D0_REF ... CLK_D0_FS3:
	case CLK_USB2_PHY:
	case CLK_D2_REF ... CLK_REF_HDMIPHY:
	case CLK_D3_REF ... CLK_LPC_1:
		return flexgen_set_rate(clk_p, freq);

	/* Special treatment for audio clockgen (D0).
	   Changing clock at FS output, keeping div in bypass */
	case CLK_PCM_0 ... CLK_PCMR0_MASTER:
		err = flexgen_set_rate(clk_p->parent, freq);
		if (!err)
			err = flexgen_recalc(clk_p);
		return err;

	/* Special cases: no programmable rate */
	case CLK_EXT2F_A9_DIV2:
		break;

	/* Unexpected clocks */
	default:
		return CLK_ERR_BAD_PARAMETER;
	}

	return 0;
}

/* ========================================================================
   Name:        fclkgen_enable
   Description: Enable clock
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int fclkgen_enable(struct clk *clk_p)
{
	int err;

	if (!clk_p)
		return CLK_ERR_BAD_PARAMETER;

	clk_debug("fclkgen_enable(%s)\n", clk_p->name);

	switch (clk_p->id) {
	/* Flexiclockgen lib handled clocks */
	case CLK_A0_REF ... CLK_IC_LMI0:
	case CLK_C0_REF ... CLK_FC_HADES:
	case CLK_D0_REF ... CLK_D0_FS3:
	case CLK_USB2_PHY:
	case CLK_D2_REF ... CLK_REF_HDMIPHY:
	case CLK_D3_REF ... CLK_LPC_1:
		err = flexgen_xable(clk_p, 1);
		break;

	/* Special treatment for audio clockgen (D0).
       Enabling source (FS) then audio clock */
	case CLK_PCM_0 ... CLK_PCMR0_MASTER:
		err = flexgen_xable(clk_p->parent, 1);
		if (!err)
			err = flexgen_xable(clk_p, 1);
		break;

	/* Special cases: no enable feature */
	case CLK_EXT2F_A9_DIV2:
		return fclkgen_recalc(clk_p);

	/* Unexpected clocks */
	default:
		return CLK_ERR_BAD_PARAMETER;
	}

	/* flexgen_xable() is performing the required recalc() */

	return err;
}

/* ========================================================================
   Name:        fclkgen_disable
   Description: Disable clock
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int fclkgen_disable(struct clk *clk_p)
{
	int err;

	if (!clk_p)
		return CLK_ERR_BAD_PARAMETER;

	clk_debug("fclkgen_disable(%s)\n", clk_p->name);

	switch (clk_p->id) {
	/* Flexiclockgen lib handled clocks */
	case CLK_A0_REF ... CLK_IC_LMI0:
	/* case CLK_C0_REF ... CLK_FC_HADES: BUG in clk.c */
	case CLK_C0_REF ... CLK_COMPO_DVP:
	case CLK_D0_REF ... CLK_D0_FS3:
	case CLK_USB2_PHY:
	case CLK_D2_REF ... CLK_REF_HDMIPHY:
	case CLK_D3_REF ... CLK_LPC_1:
		err = flexgen_xable(clk_p, 0);
		break;

	/* Special treatment for audio clockgen (D0).
       Disabling audio clock then its source (FS). */
	case CLK_PCM_0 ... CLK_PCMR0_MASTER:
		err = flexgen_xable(clk_p, 0);
		if (!err)
			err = flexgen_xable(clk_p->parent, 0);
		break;

	/* Special cases: no disable feature */
	case CLK_EXT2F_A9_DIV2:
		return fclkgen_recalc(clk_p);

	/* Unexpected clocks */
	default:
		return CLK_ERR_BAD_PARAMETER;
	}

	/* flexgen_xable() is performing the required recalc() */

	return err;
}

/******************************************************************************
CA9 PLL
******************************************************************************/

/* ========================================================================
   Name:        clkgena9_recalc
   Description: Get clocks frequencies (in Hz)
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int clkgena9_recalc(struct clk *clk_p)
{
	if (!clk_p)
		return CLK_ERR_BAD_PARAMETER;

	clk_debug("clkgena9_recalc(%s)\n", clk_p->name);

	switch (clk_p->id) {
	case CLK_A9_REF:
	case CLK_A9:
		clk_p->rate = clk_p->parent->rate;
		break;
	case CLK_A9_PERIPHS:
		clk_p->rate = clk_p->parent->rate / 2;
		break;
	case CLK_A9_PHI0:
		{
			unsigned long vcoby2_rate, odf;
			struct stm_pll pll = {
				.type = stm_pll3200c32,
			};
			int err;

			pll.idf = SYSCONF_READU(5106, 25, 27);
			pll.ndiv = SYSCONF_READU(5108, 0, 7);
			if (SYSCONF_READU(5106, 0, 0))
				/* A9_PLL_PD=1 => PLL disabled */
				clk_p->rate = 0;
			else {
				err = stm_clk_pll_get_rate(clk_p->parent->rate,
					&pll, &vcoby2_rate);
				if (err)
					return CLK_ERR_BAD_PARAMETER;
				odf = SYSCONF_READU(5108, 8, 13);
				if (odf == 0)
					odf = 1;
				clk_p->rate = vcoby2_rate / odf;
			}
		}
		break;
	default:
		return CLK_ERR_BAD_PARAMETER;	/* Unknown clock */
	}

	return 0;
}

/* ========================================================================
   Name:        clkgena9_identify_parent
   Description: Identify parent clock for clockgen A9 clocks.
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int clkgena9_identify_parent(struct clk *clk_p)
{
	clk_debug("clkgena9_identify_parent(%s)\n", clk_p->name);

	if (clk_p->id != CLK_A9) /* Other clocks have static parent */
		return 0;

	/* Is CA9 clock sourced from PLL or C0-13 ? */
	if (SYSCONF_READU(5105, 1, 1)) {
		/* Yes. Div by 1 or div by 2 ? */
		if (SYSCONF_READU(5105, 0, 0))
			clk_p->parent = &clk_clocks[CLK_EXT2F_A9];
		else
			clk_p->parent = &clk_clocks[CLK_EXT2F_A9_DIV2];
	} else
		clk_p->parent = &clk_clocks[CLK_A9_PHI0];

	return 0;
}

/* ========================================================================
   Name:        clkgena9_init
   Description: Read HW status to initialize 'struct clk' structure.
   Returns:     'clk_err_t' error code.
   ======================================================================== */

static int clkgena9_init(struct clk *clk_p)
{
	int err;

	if (!clk_p)
		return CLK_ERR_BAD_PARAMETER;

	clk_debug("clkgena9_init(%s)\n", clk_p->name);

	err = clkgena9_identify_parent(clk_p);
	if (!err)
		err = clkgena9_recalc(clk_p);

	return err;
}

/* ========================================================================
   Name:        clkgena9_set_rate
   Description: Set clock frequency
   Returns:     'clk_err_t' error code
   ======================================================================== */

static int clkgena9_set_rate(struct clk *clk_p, unsigned long freq)
{
	unsigned long odf, vcoby2_rate;
	struct stm_pll pll = {
		.type = stm_pll3200c32,
	};
	int err = 0;

	if (!clk_p)
		return CLK_ERR_BAD_PARAMETER;
	if (!clk_p->parent)
		return CLK_ERR_INTERNAL;

	clk_debug("clkgena9_set_rate(%s)\n", clk_p->name);

	if (clk_p->id == CLK_A9_PERIPHS)
		return CLK_ERR_BAD_PARAMETER;
	if (clk_p->id == CLK_A9)
		return clkgena9_set_rate(clk_p->parent, freq);
	if (clk_p->id != CLK_A9_PHI0)
		return CLK_ERR_BAD_PARAMETER;

	/* Computing ODF: min FVCOBY2=800Mhz. Using ODF to go below */
	if (freq < 800000000) {
		odf = 800000000 / freq;
		if (800000000 % freq)
			odf = odf + 1;
	} else
		odf = 1;
	vcoby2_rate = freq * odf;
	err = stm_clk_pll_get_params(clk_p->parent->rate, vcoby2_rate, &pll);
	if (err != 0)
		return err;

	SYSCONF_WRITEU(5105, 1, 1, 1);		/* CLK_CA9=C0-13 clock */

	SYSCONF_WRITEU(5106, 0, 0, 1);		/* Disabling PLL */
	SYSCONF_WRITEU(5106, 25, 27, pll.idf);	/* IDF */
	SYSCONF_WRITEU(5108, 0, 7, pll.ndiv);	/* NDIV */
	SYSCONF_WRITEU(5108, 8, 13, odf);	/* ODF */
	SYSCONF_WRITEU(5106, 1, 5, pll.cp);	/* Charge Pump */

	SYSCONF_WRITEU(5106, 0, 0, 0);		/* Reenabling PLL */

	while (!SYSCONF_READU(5543, 0, 0))	/* Wait for lock */
		;
	/* Can't put any delay because may rely on a clock that is currently
	   changing (running from CA9 case). */

	SYSCONF_WRITEU(5105, 1, 1, 0);		/* CLK_CA9=PLL */

	return clkgena9_recalc(clk_p);
}

/******************************************************************************
Clock structure description & platform init function
******************************************************************************/

#define fclkgen_observe NULL

#ifdef CONFIG_PM
_CLK_OPS(fclkgena0,
	"A0",
	fclkgen_init,
	NULL,
	NULL,
	fclkgen_recalc,
	NULL,
	NULL,
	fclkgen_observe,
	NULL,
	"PIO16[0]"
);
#else
_CLK_OPS(fclkgena0,
	"A0",
	fclkgen_init,
	fclkgen_set_parent,
	fclkgen_set_rate,
	fclkgen_recalc,
	fclkgen_enable,
	fclkgen_disable,
	fclkgen_observe,
	NULL,		/* No measure function */
	"PIO16[0]"	/* Observation point */
);
#endif

_CLK_OPS(fclkgenc0,
	"C0",
	fclkgen_init,
	fclkgen_set_parent,
	fclkgen_set_rate,
	fclkgen_recalc,
	fclkgen_enable,
	fclkgen_disable,
	fclkgen_observe,
	NULL,		/* No measure function */
	"PIO16[6]"	/* Observation point */
);
_CLK_OPS(fclkgend0,
	"D0",
	fclkgen_init,
	fclkgen_set_parent,
	fclkgen_set_rate,
	fclkgen_recalc,
	fclkgen_enable,
	fclkgen_disable,
	fclkgen_observe,
	NULL,		/* No measure function */
	"PIO33[4]"	/* Observation point */
);
_CLK_OPS(fclkgend2,
	"D2",
	fclkgen_init,
	fclkgen_set_parent,
	fclkgen_set_rate,
	fclkgen_recalc,
	fclkgen_enable,
	fclkgen_disable,
	fclkgen_observe,
	NULL,		/* No measure function */
	"PIO35[3]"	/* Observation point */
);
_CLK_OPS(fclkgend3,
	"D3",
	fclkgen_init,
	fclkgen_set_parent,
	fclkgen_set_rate,
	fclkgen_recalc,
	fclkgen_enable,
	fclkgen_disable,
	fclkgen_observe,
	NULL,		/* No measure function */
	"PIO17[0]"	/* Observation point to be checked */
);
_CLK_OPS(clkgena9,
	"CA9",
	clkgena9_init,
	NULL,
	clkgena9_set_rate,
	clkgena9_recalc,
	NULL,
	NULL,
	NULL,
	NULL,		/* No measure function */
	NULL		/* No observation point */
);

static struct clk clk_clocks[] = {
/* Top level clocks */
_CLK_FIXED(CLK_SYSIN, SYS_CLKIN * 1000000, CLK_ALWAYS_ENABLED),

/* Clockgen A0 */
_CLK_P(CLK_A0_REF,	&fclkgena0, 30000000, CLK_ALWAYS_ENABLED,
	&clk_clocks[CLK_SYSIN]),
_CLK_P(CLK_A0_PLL0,	&fclkgena0, 932000000, 0,
	&clk_clocks[CLK_A0_REF]),
_CLK(CLK_IC_LMI0,	&fclkgena0,   466000000, CLK_ALWAYS_ENABLED),

/* Clockgen C0 */
_CLK_P(CLK_C0_REF,	&fclkgenc0, 30000000, CLK_ALWAYS_ENABLED,
	&clk_clocks[CLK_SYSIN]),
_CLK_P(CLK_C0_PLL0,	&fclkgenc0, 1066000000,	0,
	&clk_clocks[CLK_C0_REF]),
_CLK_P(CLK_C0_PLL1,	&fclkgenc0, 1200000000,	0,
	&clk_clocks[CLK_C0_REF]),

_CLK_P(CLK_C0_FS_VCO,	&fclkgenc0, 660000000,	0,
	&clk_clocks[CLK_C0_REF]),
_CLK_P(CLK_C0_FS0,	&fclkgenc0, 333000000,	0,
	&clk_clocks[CLK_C0_FS_VCO]),
_CLK_P(CLK_C0_FS1,	&fclkgenc0, 450000000,	0,
	&clk_clocks[CLK_C0_FS_VCO]),
_CLK_P(CLK_C0_FS2,	&fclkgenc0, 36864000,	0,
	&clk_clocks[CLK_C0_FS_VCO]),
_CLK_P(CLK_C0_FS3,	&fclkgenc0, 125000000,  0,
	&clk_clocks[CLK_C0_FS_VCO]),

_CLK(CLK_ICN_GPU,	&fclkgenc0,   355300000,    0),
_CLK(CLK_FDMA,		&fclkgenc0,   400000000,    0),
_CLK(CLK_NAND,		&fclkgenc0,   266500000,    0),
_CLK(CLK_HVA,		&fclkgenc0,   300000000,    0),
_CLK(CLK_PROC_STFE,	&fclkgenc0,   250000000,    0),
_CLK(CLK_TP,		&fclkgenc0,   400000000,    0),
_CLK(CLK_RX_ICN_DMU,	&fclkgenc0,   450000000,    CLK_ALWAYS_ENABLED),
_CLK(CLK_RX_ICN_HVA,	&fclkgenc0,   450000000,    CLK_ALWAYS_ENABLED),
_CLK(CLK_ICN_CPU,	&fclkgenc0,   533000000,    CLK_ALWAYS_ENABLED),
_CLK(CLK_TX_ICN_DMU,	&fclkgenc0,   333000000,    CLK_ALWAYS_ENABLED),
_CLK(CLK_MMC_0,		&fclkgenc0,   200000000,    0),
_CLK(CLK_MMC_1,		&fclkgenc0,    50000000,    0),
_CLK(CLK_JPEGDEC,	&fclkgenc0,   333000000,    0),
_CLK(CLK_EXT2F_A9,	&fclkgenc0,   200000000,    0),
_CLK(CLK_IC_BDISP_0,	&fclkgenc0,   400000000,    0),
_CLK(CLK_IC_BDISP_1,	&fclkgenc0,   400000000,    0),
_CLK(CLK_PP_DMU,	&fclkgenc0,   266500000,    0),
_CLK(CLK_VID_DMU,	&fclkgenc0,   533000000,    0),
_CLK(CLK_DSS_LPC,	&fclkgenc0,    36864000,    0),
_CLK(CLK_ST231_AUD_0,	&fclkgenc0,   600000000,    0),
_CLK(CLK_ST231_GP_1,	&fclkgenc0,   600000000,    0),
_CLK(CLK_ST231_DMU,	&fclkgenc0,   600000000,    0),
_CLK(CLK_ICN_LMI,	&fclkgenc0,   600000000,    CLK_ALWAYS_ENABLED),
_CLK(CLK_TX_ICN_DISP_0,	&fclkgenc0,   300000000,    0),
_CLK(CLK_ICN_SBC,	&fclkgenc0,   200000000,    CLK_ALWAYS_ENABLED),
_CLK(CLK_STFE_FRC2,	&fclkgenc0,    50000000,    0),
_CLK(CLK_ETH_PHY,	&fclkgenc0,   125000000,    CLK_ALWAYS_ENABLED),
_CLK(CLK_ETH_PHYREF,	&fclkgenc0,   125000000,    CLK_ALWAYS_ENABLED),
_CLK(CLK_FLASH_PROMIP,	&fclkgenc0,   100000000,    0),
_CLK(CLK_MAIN_DISP,	&fclkgenc0,   400000000,    0),
_CLK(CLK_AUX_DISP,	&fclkgenc0,   400000000,    0),
_CLK(CLK_COMPO_DVP,	&fclkgenc0,   400000000,    0),

_CLK(CLK_TX_ICN_HADES,	&fclkgenc0,   0,    0),
_CLK(CLK_RX_ICN_HADES,	&fclkgenc0,   0,    0),
_CLK(CLK_ICN_REG_16,	&fclkgenc0,   0,    0),
_CLK(CLK_PP_HADES,	&fclkgenc0,   0,    0),
_CLK(CLK_CLUST_HADES,	&fclkgenc0,   0,    0),
_CLK(CLK_HWPE_HADES,	&fclkgenc0,   0,    0),
_CLK(CLK_FC_HADES,	&fclkgenc0,   0,    0),

_CLK_P(CLK_EXT2F_A9_DIV2,	&fclkgenc0,    100000000,
	CLK_ALWAYS_ENABLED, &clk_clocks[CLK_EXT2F_A9]),

/* Clockgen D0 */
_CLK_P(CLK_D0_REF, &fclkgend0, 30000000, CLK_ALWAYS_ENABLED,
	&clk_clocks[CLK_SYSIN]),
_CLK_P(CLK_D0_FS_VCO,	&fclkgend0, 660000000, 0,
	&clk_clocks[CLK_D0_REF]),
_CLK_P(CLK_D0_FS0,	&fclkgend0, 50000000, 0,
	&clk_clocks[CLK_D0_FS_VCO]),
_CLK_P(CLK_D0_FS1,	&fclkgend0, 50000000, 0,
	&clk_clocks[CLK_D0_FS_VCO]),
_CLK_P(CLK_D0_FS2,	&fclkgend0, 50000000, 0,
	&clk_clocks[CLK_D0_FS_VCO]),
_CLK_P(CLK_D0_FS3,	&fclkgend0, 50000000,  0,
	&clk_clocks[CLK_D0_FS_VCO]),

_CLK(CLK_PCM_0,		&fclkgend0,	50000000,	0),
_CLK(CLK_PCM_1,		&fclkgend0,	50000000,	0),
_CLK(CLK_PCM_2,		&fclkgend0,	50000000,	0),
_CLK(CLK_SPDIFF,	&fclkgend0,	50000000,	0),
_CLK(CLK_D0_SPARE_13,	&fclkgend0,	0,	0),

_CLK(CLK_USB2_PHY,	&fclkgend0,	10000000,	0),

/* Clockgen D2 */
_CLK_P(CLK_D2_REF, &fclkgend2, 30000000, CLK_ALWAYS_ENABLED,
	&clk_clocks[CLK_SYSIN]),
_CLK(CLK_D2_REF1,	&fclkgend2, 0, 0),
_CLK_P(CLK_D2_FS_VCO,	&fclkgend2, 660000000, 0,
	&clk_clocks[CLK_D2_REF]),
_CLK_P(CLK_D2_FS0,	&fclkgend2, 297000000,	0,
	&clk_clocks[CLK_D2_FS_VCO]),
_CLK_P(CLK_D2_FS1,	&fclkgend2, 297000000,	0,
	&clk_clocks[CLK_D2_FS_VCO]),
_CLK_P(CLK_D2_FS2,	&fclkgend2, 400000000,	0,
	&clk_clocks[CLK_D2_FS_VCO]),
_CLK_P(CLK_D2_FS3,	&fclkgend2, 450000000,	0,
	&clk_clocks[CLK_D2_FS_VCO]),

_CLK(CLK_PIX_MAIN_DISP,	&fclkgend2,   0,    0),
_CLK(CLK_PIX_PIP,	&fclkgend2,   0,    0),
_CLK(CLK_PIX_GDP1,	&fclkgend2,   0,    0),
_CLK(CLK_PIX_GDP2,	&fclkgend2,   0,    0),
_CLK(CLK_PIX_GDP3,	&fclkgend2,   0,    0),
_CLK(CLK_PIX_GDP4,	&fclkgend2,   0,    0),
_CLK(CLK_PIX_AUX_DISP,	&fclkgend2,   0,    0),
_CLK(CLK_DENC,		&fclkgend2,   0,    0),
_CLK(CLK_PIX_HDDAC,	&fclkgend2,   0,    0),
_CLK(CLK_HDDAC,		&fclkgend2,   0,    0),
_CLK(CLK_SDDAC,		&fclkgend2,   0,    0),
_CLK(CLK_PIX_DVO,	&fclkgend2,   0,    0),
_CLK(CLK_DVO,		&fclkgend2,   0,    0),
_CLK(CLK_PIX_HDMI,	&fclkgend2,   0,    0),
_CLK(CLK_TMDS_HDMI,	&fclkgend2,   0,    0),
_CLK(CLK_REF_HDMIPHY,	&fclkgend2,   0,    0),

_CLK_P(CLK_13_FROM_D0,	&fclkgend2, 0,	0,
	&clk_clocks[CLK_D0_SPARE_13]),		/* jein[0] */
_CLK(CLK_USB_480,	&fclkgend2,   0,    0),	/* jein[2] */
_CLK(CLK_USB_UTMI,	&fclkgend2,   0,    0),	/* jein[5] */

_CLK(CLK_TMDSOUT_HDMI,	&fclkgend2,   0,    0),	/* srcclkin0 */
_CLK(CLK_PIX_HD_P_IN,	&fclkgend2,   0,    0),	/* srcclkin1 */

/* Clockgen D3 */
_CLK_P(CLK_D3_REF,	&fclkgend3, 30000000, CLK_ALWAYS_ENABLED,
	&clk_clocks[CLK_SYSIN]),
_CLK_P(CLK_D3_FS_VCO,	&fclkgend3, 660000000, 0,
	&clk_clocks[CLK_D3_REF]),
_CLK_P(CLK_D3_FS0,	&fclkgend3, 27000000,	0,
	&clk_clocks[CLK_D3_FS_VCO]),
_CLK_P(CLK_D3_FS1,	&fclkgend3, 108000000,	0,
	&clk_clocks[CLK_D3_FS_VCO]),
_CLK_P(CLK_D3_FS2,	&fclkgend3, 450000000,	0,
	&clk_clocks[CLK_D3_FS_VCO]),
_CLK_P(CLK_D3_FS3,	&fclkgend3, 450000000,	0,
	&clk_clocks[CLK_D3_FS_VCO]),

_CLK(CLK_STFE_FRC1,	&fclkgend3,   27000000,    0),
_CLK(CLK_TSOUT_0,	&fclkgend3,   108000000,    0),
_CLK(CLK_TSOUT_1,	&fclkgend3,   108000000,    0),
_CLK(CLK_MCHI,		&fclkgend3,   27000000,    0),
_CLK(CLK_VSENS_COMPO,	&fclkgend3,   468000,    0),
_CLK(CLK_FRC1_REMOTE,	&fclkgend3,   27000000,    0),
_CLK(CLK_LPC_0,		&fclkgend3,   30000000,    0),
_CLK(CLK_LPC_1,		&fclkgend3,   30000000,    0),

/* CA9 PLL */
_CLK_P(CLK_A9_REF,	&clkgena9, 30000000, CLK_ALWAYS_ENABLED,
	&clk_clocks[CLK_SYSIN]),
_CLK_P(CLK_A9_PHI0,	&clkgena9, 1500000000, 0, &clk_clocks[CLK_A9_REF]),
_CLK(CLK_A9,		&clkgena9, 1500000000, CLK_ALWAYS_ENABLED),
_CLK_P(CLK_A9_PERIPHS,	&clkgena9, 750000000, 0, &clk_clocks[CLK_A9]),
};

/* ========================================================================
   Name:        stih410_plat_clk_init()
   Description: SOC specific LLA initialization
   Returns:     'clk_err_t' error code.
   ======================================================================== */

int __init stih410_plat_clk_init(void)
{
	int err;

	pr_info("H410 clock LLA %s\n", LLA_VERSION);

	/* Flexiclockgen lib init */
	if (flexgen_init(flexgens, clk_clocks) != 0)
		return CLK_ERR_BAD_PARAMETER;

	SYSCONF_ASSIGNU(5105, 0, 0);
	SYSCONF_ASSIGNU(5105, 1, 1);
	SYSCONF_ASSIGNU(5106, 0, 0);
	SYSCONF_ASSIGNU(5106, 1, 5);
	SYSCONF_ASSIGNU(5106, 25, 27);
	SYSCONF_ASSIGNU(5108, 0, 7);
	SYSCONF_ASSIGNU(5108, 8, 13);
	SYSCONF_ASSIGNU(5543, 0, 0);

	/* Registering clocks */
	err = clk_register_table(clk_clocks, ARRAY_SIZE(clk_clocks), 0);

	clk_prepare_enable(&clk_clocks[CLK_D0_FS_VCO]);
	clk_set_rate(&clk_clocks[CLK_D0_FS_VCO], 600000000);
	clk_disable_unprepare(&clk_clocks[CLK_D0_FS_VCO]);

	clk_set_parent(&clk_clocks[CLK_PCM_0], &clk_clocks[CLK_D0_FS0]);
	clk_set_parent(&clk_clocks[CLK_PCM_1], &clk_clocks[CLK_D0_FS1]);
	clk_set_parent(&clk_clocks[CLK_PCM_2], &clk_clocks[CLK_D0_FS2]);
	clk_set_parent(&clk_clocks[CLK_SPDIFF], &clk_clocks[CLK_D0_FS3]);

	return err;
}

