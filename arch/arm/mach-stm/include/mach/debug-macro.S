/*
 * arch/arm/mach-stm/include/mach/debug-macro.S
 *
 * Debugging macro include header
 *
 * Copyright (C) 2010-2012 STMicroelectronics Limited
 *
 * Author(s): Stuart Menefy <stuart.menefy@st.com>
 *            Carmelo Amoroso <carmelo.amoroso@st.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifdef CONFIG_MACH_STM_STIH415
#include <linux/stm/stih415-periphs.h>
#ifdef CONFIG_MACH_STM_B2000
#define DEBUG_LL_UART_BASE	STIH415_ASC2_BASE
#elif defined CONFIG_MACH_STM_B2020
#define DEBUG_LL_UART_BASE	STIH415_SBC_ASC1_BASE
#endif
#endif

#ifdef CONFIG_MACH_STM_STIH407
#include <linux/stm/stih407-periphs.h>
#define DEBUG_LL_UART_BASE	STIH407_SBC_ASC0_BASE
#endif

#ifndef DEBUG_LL_UART_BASE
#error "Unknown platform"
#endif

#include <mach/hardware.h>

		.macro	addruart, rp, rv, tmp
		ldr	\rp,      =DEBUG_LL_UART_BASE	@ physical base
		ldr	\rv,      =IO_ADDRESS(DEBUG_LL_UART_BASE) @ virtual base
		.endm

#include <asm/hardware/debug-stm-asc.S>
