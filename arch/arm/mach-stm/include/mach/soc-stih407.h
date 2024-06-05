/*
 * arch/arm/mach-stm/include/mach/soc-stih407.h
 *
 * Copyright (C) 2012 STMicroelectronics Limited.
 * Author: Nunzio Raciti <nunzio.raciti@st.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_SOC_STIH407_H
#define __ASM_ARCH_SOC_STIH407_H

#include <linux/stm/stih407-periphs.h>

/* Cortex A9 private memory region */
/* Size 2*4K, base address set by PERIPHBASE[31:13] pins */
/* See table 1-3 in Cortex-A9 MPCore TRM (ARM DDI 0407) */

#define STIH407_A9_CONFIG_10		0x08760000

/* Snoop Control Unit */
#define STIH407_SCU_BASE		(STIH407_A9_CONFIG_10 + 0x0000)
/* Private Generic Interrupt Controller */
#define STIH407_GIC_CPU_BASE		(STIH407_A9_CONFIG_10 + 0x0100)
/* Global timer */
#define STIH407_GLOBAL_TIMER_BASE	(STIH407_A9_CONFIG_10 + 0x0200)
/* Private Timer Watchdog */
#define STIH407_TWD_BASE		(STIH407_A9_CONFIG_10 + 0x0600)
#define STIH407_TWD_SIZE		0x100
/* Interrupt distributor */
#define STIH407_GIC_DIST_BASE		(STIH407_A9_CONFIG_10 + 0x1000)

/* PL310 (Cache Controller) control registers */
/* defined by REGFILEBASE[31:12] pins, size 0x1000 */
/* See table 301 in PrimeCell Level 2 Cache Controller (PL310)
 * TRM (ARM DDI 0246) */
#define STIH407_PL310_BASE		(STIH407_A9_CONFIG_10 + 0x2000)

#ifndef __ASSEMBLER__

void stih407_map_io(void);

#endif /* __ASSEMBLER__ */

#endif  /* __ASM_ARCH_SOC_STIH407_H */
