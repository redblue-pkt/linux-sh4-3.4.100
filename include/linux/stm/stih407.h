/*
 * Copyright (c) 2012-2013 STMicroelectronics Limited
 * Author: Nunzio Raciti <nunzio.raciti@st.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_STM_STIH407_H
#define __LINUX_STM_STIH407_H

#include <linux/device.h>
#include <linux/stm/platform.h>


/*
 * GPIO bank re-mapping on STiH407:
 *
 *           Hw_GPIO_Port_Nr    Linux GPIO
 * ----------------------------------------
 * PIO_SBC:     [ 0 : 5 ]      [  0 : 5  ]
 * PIO_FRONT:   [10 : 20]      [  6 : 16 ]
 * PIO_REAR:    [30 : 35]      [ 17 : 22 ]
 * PIO_FLASH:   [40 : 42]      [ 23 : 25 ]
 */

#define SYSCONFG_GROUP(x)	(((x) < 1000) ? (0) : ((x) / 1000))
#define SYSCONF_OFFSET(x)	(((x) < 1000) ? (x) : ((x) % 1000))
#define SYSCONF(x)		SYSCONFG_GROUP(x), SYSCONF_OFFSET(x)

#define STIH407_GPIO(x)	(((x) < 6) ? (x) : \
			((x) < 21) ? (x) - 4 : \
			((x) < 36) ? (x) - 13 : (x) - 17)

/* Clk */
int stih407_plat_clk_init(void);
int stih407_plat_clk_alias_init(void);

/* Clk LLA for Cannes-2 & Monaco-2 SoCs */
int stih410_plat_clk_init(void);
#endif
