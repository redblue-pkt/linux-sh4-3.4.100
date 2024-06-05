/*****************************************************************************
 *
 * File name   : clock-regs-stih410.h
 * Description : Low Level API - Base addresses & register definitions.
 *
 * COPYRIGHT (C) 2013 STMicroelectronics - All Rights Reserved
 * May be copied or modified under the terms of the GNU General Public
 * License V2 _ONLY_.  See linux/COPYING for more information.
 *
 *****************************************************************************/

#ifndef __CLOCK_LLA_REGS_H
#define __CLOCK_LLA_REGS_H

/* --- Base addresses --------------------------------------- */
#define CKG_A0_BASE_ADDRESS		0x90ff000
#define CKG_C0_BASE_ADDRESS		0x9103000
#define CKG_D0_BASE_ADDRESS		0x9104000
#define CKG_D2_BASE_ADDRESS		0x9106000
#define CKG_D3_BASE_ADDRESS		0x9107000

#ifdef ST_OS21
#define SYSCFG_PIO_FRONT_BASE		0x9280000
#define SYSCFG_PIO_REAR_BASE		0x9290000
#define SYSCFG_5000_5999_BASE		0x92B0000
#endif

#endif  /* End __CLOCK_LLA_REGS_H */
