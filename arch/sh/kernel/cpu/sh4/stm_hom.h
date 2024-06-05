/*
 * -------------------------------------------------------------------------
 * Copyright (C) 2010  STMicroelectronics
 * Author: Francesco M. Virlinzi  <francesco.virlinzi@st.com>
 *
 * May be copied or modified under the terms of the GNU General Public
 * License v2.  See linux/COPYING for more information.
 *
 * ------------------------------------------------------------------------- */
#ifndef __STM_MEM_HIBERNATION_H__
#define __STM_MEM_HIBERNATION_H__

#include <linux/hom.h>
#include <linux/stm/wakeup_devices.h>

struct stm_mem_hibernation {
	long flags;
	long tbl_addr;
	long tbl_size;
	struct platform_hom_ops ops;
	void (*early_console)(void);
};

int stm_hom_register(struct stm_mem_hibernation *platform);

void stm_hom_exec_table(unsigned int tbl, unsigned int tbl_end,
		unsigned long lpj, unsigned int flags);


void stm_defrost_kernel(void);

/*
 * HOM - board API
 */
int stm_freeze_board(struct stm_wakeup_devices *wkd);
int stm_restore_board(struct stm_wakeup_devices *wkd);

struct stm_hom_board {
	int (*freeze)(struct stm_wakeup_devices *wkd);
	int (*restore)(struct stm_wakeup_devices *wkd);
};

int stm_hom_board_register(struct stm_hom_board *board);

void hom_printk(char *buf, ...);

#endif
