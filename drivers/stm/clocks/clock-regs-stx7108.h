/*****************************************************************************
 *
 * File name   : clock-regs-stx7108.h
 * Description : Low Level API - Base addresses & register definitions.
 *
 * COPYRIGHT (C) 2009 STMicroelectronics - All Rights Reserved
 * May be copied or modified under the terms of the GNU General Public
 * License V2 _ONLY_.  See linux/COPYING for more information.
 *
 *****************************************************************************/

#ifndef __CLOCK_LLA_REGS_H
#define __CLOCK_LLA_REGS_H

/* --- Base addresses --------------------------------------- */
/* CkgA-Left: CPU, GPU, EMI... */
#define CKGA0_BASE_ADDRESS	0xfde98000
/* CkgA-Right: Disp, tango, DMA, ... */
#define CKGA1_BASE_ADDRESS	0xfdab8000
	/* Video */
#define CKGB_BASE_ADDRESS	0xfd546000
/* Audio: WARNING, first offset at 0x60 */
#define CKGC_BASE_ADDRESS	0xfe700000


/* --- CKGA registers --- */
#define CKGA_PLL0_CFG                   0x000
#define CKGA_PLL1_CFG                   0x004
#define CKGA_POWER_CFG	                0x010
#define CKGA_CLKOPSRC_SWITCH_CFG        0x014
#define CKGA_OSC_ENABLE_FB              0x018
#define CKGA_PLL0_ENABLE_FB             0x01c
#define CKGA_PLL1_ENABLE_FB             0x020
#define CKGA_CLKOPSRC_SWITCH_CFG2       0x024

#define CKGA_CLKOBS_MUX1_CFG            0x030
#define CKGA_CLKOBS_MASTER_MAXCOUNT     0x034
#define CKGA_CLKOBS_CMD                 0x038
#define CKGA_CLKOBS_STATUS              0x03c
#define CKGA_CLKOBS_SLAVE0_COUNT        0x040
#define CKGA_OSCMUX_DEBUG               0x044
#define CKGA_CLKOBS_MUX2_CFG            0x048
#define CKGA_LOW_POWER_CTRL             0x04C

/*
 * The CKGA_SOURCE_CFG(..) replaces the
 * - CKGA_OSC_DIV0_CFG
 * - CKGA_PLL0HS_DIV0_CFG
 * - CKGA_PLL0LS_DIV0_CFG
 * - CKGA_PLL1_DIV0_CFG
 * macros.
 * The _parent_id identifies the parent as:
 * - 0: OSC
 * - 1: PLL0_HS
 * - 2: PLL0_LS
 * - 3: PLL1
 */
#define CKGA_SOURCE_CFG(_parent_id)	(0x800 + (_parent_id) * 0x100)

/* --- CKGB registers --- */
#define CKGB_LOCK                       0x010
#define CKGB_FS0_CTRL                   0x014
#define CKGB_FS1_CTRL                   0x05c
#define CKGB_FS0_CLKOUT_CTRL            0x058
#define CKGB_FS1_CLKOUT_CTRL            0x0a0
#define CKGB_DISPLAY_CFG                0x0a4
#define CKGB_FS_SELECT                  0x0a8
#define CKGB_POWER_DOWN                 0x0ac
#define CKGB_POWER_ENABLE               0x0b0
#define CKGB_OUT_CTRL                   0x0b4
#define CKGB_CRISTAL_SEL                0x0b8

/*
 * both bank and channel counts from _zero_
 */
#define CKGB_FS_MD(_bk, _chan)		\
	(0x18 + (_chan) * 0x10 + (_bk) * 0x48)
#define CKGB_FS_PE(_bk, _chan)	(0x4 + CKGB_FS_MD(_bk, _chan))
#define CKGB_FS_EN_PRG(_bk, _chan)	(0x4 + CKGB_FS_PE(_bk, _chan))
#define CKGB_FS_SDIV(_bk, _chan)	(0x4 + CKGB_FS_EN_PRG(_bk, _chan))

/* Clock recovery registers */
#define CKGB_RECOV_REF_MAX              0x000
#define CKGB_RECOV_CMD                  0x004
#define CKGB_RECOV_CPT_PCM              0x008
#define CKGB_RECOV_CPT_HD               0x00c

/* --- Audio config registers --- */
/* Note:
 * Channel 0 => PCM0 (tvout ss)
 * Channel 1 => PCM1 (analog out)
 * Channel 2 => SPDIF0
 * Channel 3 => PCM2 (digital out)
 */

#define CKGC_FS0_CFG	   0x060
#define CKGC_FS_MD(_bk, _chan)		\
		(0x64 + 0x100 * (_bk) + 0x10 * (_chan))
#define CKGC_FS_PE(_bk, _chan)	(0x4 + CKGC_FS_MD(_bk, _chan))
#define CKGC_FS_SDIV(_bk, _chan)	(0x8 + CKGC_FS_MD(_bk, _chan))
#define CKGC_FS_EN_PRG(_bk, _chan)	(0xc + CKGC_FS_MD(_bk, _chan))

#endif  /* End __CLOCK_LLA_REGS_H */
