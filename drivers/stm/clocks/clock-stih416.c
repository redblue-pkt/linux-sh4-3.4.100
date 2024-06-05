/*****************************************************************************
 *
 * File name   : clock-stxh416.c
 * Description : Low Level API - HW specific implementation
 *
 * COPYRIGHT (C) 2009 STMicroelectronics - All Rights Reserved
 * May be copied or modified under the terms of the GNU General Public
 * License V2 __ONLY__.  See linux/COPYING for more information.
 *
 *****************************************************************************/

/* ----- Modification history (most recent first)----
25/may/12 fabrice.charpentier@st.com
	  Added HDMI RX clock link + fixes
14/mar/12 fabrice.charpentier@st.com
	  Preliminary version for Orly2
*/

/* Includes --------------------------------------------------------------- */

#include <linux/io.h>
#include <linux/delay.h>

#include <linux/stm/stih416.h>
#include <linux/stm/clk.h>
#include <linux/stm/sysconf.h>

#include "clk-common.h"
#include "clock-oslayer.h"
#include "clock-stih416.h"
/*
 * This function is used in the clock-MPE file
 * and in the clock-SAS file to be able to compile
 * both the file __without__ any include chip_based
 */
struct sysconf_field *stih416_platform_sys_claim(int _nr, int _lsb, int _msb)
{
	return sysconf_claim(SYSCONFG_GROUP(_nr),
		SYSCONF_OFFSET(_nr), _lsb, _msb, "Clk lla");
}

/* External functions prototypes */
extern int mpe42_clk_init(struct sysconf_field *(*plat_sysconf_claim)
			  (int _nr, int _lsb, int _msb), struct clk *,
			  struct clk *, struct clk *, struct clk *,
			  struct clk *);
extern int sasg2_clk_init(struct sysconf_field *(*plat_sysconf_claim)
			  (int _nr, int _lsb, int _msb), struct clk *);

/* SOC top input clocks. */
#define SYS_CLKIN	30
#define SYS_CLKALTIN	30   /* MPE only alternate input */

static struct clk clk_clocks[] = {
	/* Top level clocks */
	_CLK_FIXED(CLK_SYSIN, SYS_CLKIN * 1000000, CLK_ALWAYS_ENABLED),
	_CLK_FIXED(CLK_MPEALT, SYS_CLKALTIN * 1000000, CLK_ALWAYS_ENABLED),
};

/* ========================================================================
   Name:        stih416_plat_clk_init()
   Description: SOC specific LLA initialization
   Returns:     'clk_err_t' error code.
   ======================================================================== */

int __init stih416_plat_clk_init(void)
{
	struct clk *clk_main, *clk_aux, *clk_hdmirx;

	/* Registering top level clocks */
	clk_register_table(clk_clocks, ARRAY_SIZE(clk_clocks), 0);

	/* SASG2 clocks */
	sasg2_clk_init(stih416_platform_sys_claim, &clk_clocks[0]);

	/* Connecting inter-dies clocks */
	clk_main = clk_get(NULL, "CLK_S_PIX_MAIN");
	clk_aux = clk_get(NULL, "CLK_S_PIX_AUX");
	clk_hdmirx = clk_get(NULL, "CLK_S_PIX_HDMIRX");

	/* MPE42 clocks */
	mpe42_clk_init(stih416_platform_sys_claim, &clk_clocks[0], &clk_clocks[1],
		clk_main, clk_aux, clk_hdmirx);

	return 0;
}
