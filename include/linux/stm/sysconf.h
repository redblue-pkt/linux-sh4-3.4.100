/*
 * Copyright (C) 2007 STMicroelectronics Limited
 * Author: Stuart Menefy <stuart.menefy@st.com>
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 */

#ifndef __LINUX_STM_SYSCONF_H
#define __LINUX_STM_SYSCONF_H

#include <linux/types.h>
#include <linux/platform_device.h>

struct sysconf_field;

/**
 * sysconf_claim - Claim ownership of a field of a sysconfig register
 * @group: register group (ie. SYS_CFG, SYS_STA); SOC-specific
 * @num: register number
 * @lsb: the LSB of the register we are claiming
 * @msb: the MSB of the register we are claiming
 * @devname: device claiming the field
 *
 * This function claims ownership of a field from a sysconfig register.
 * The part of the sysconfig register being claimed is from bit @lsb
 * through to bit @msb inclusive. To claim the whole register, @lsb
 * should be 0, @msb 31 (or 63 for systems with 64 bit sysconfig registers).
 *
 * It returns a &struct sysconf_field which can be used in subsequent
 * operations on this field.
 */
struct sysconf_field *sysconf_claim(int group, int num, int lsb, int msb,
		const char *devname);

/**
 * sysconf_release - Release ownership of a field of a sysconfig register
 * @field: the sysconfig field to write to
 *
 * Release ownership of a field from a sysconf register.
 * @field must have been claimed using sysconf_claim().
 */
void sysconf_release(struct sysconf_field *field);

/**
 * sysconf_write - Write a value into a field of a sysconfig register
 * @field: the sysconfig field to write to
 * @value: the value to write into the field
 *
 * This writes @value into the field of the sysconfig register @field.
 * @field must have been claimed using sysconf_claim().
 */
void sysconf_write(struct sysconf_field *field, unsigned long value);

/**
 * sysconf_read - Read a field of a sysconfig register
 * @field: the sysconfig field to read
 *
 * This reads a field of the sysconfig register @field.
 * @field must have been claimed using sysconf_claim().
 */
unsigned long sysconf_read(struct sysconf_field *field);

/**
 * sysconf_early_init - Used by board initialization code
 */
void sysconf_early_init(struct platform_device *pdevs, int pdevs_num);

void of_sysconf_early_init(void);

/**
 * sysconf_reg_name - Return register name
 * @name: buffer into which to write the name
 * @size: size of buffer
 * @group: register group (ie. SYS_CFG, SYS_STA); SOC-specific
 * @num: register number
 */
int sysconf_reg_name(char *name, int size, int group, int num);
#ifdef CONFIG_OF
struct sysconf_field *stm_of_sysconf_claim(struct device_node *np,
			const char *prop);
#else
static inline struct sysconf_field *stm_of_sysconf_claim(struct device_node *np,
			const char *prop){
	return NULL;
}
#endif

#endif
