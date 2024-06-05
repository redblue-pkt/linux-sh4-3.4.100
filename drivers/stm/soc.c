/*
 * (c) 2012 STMicroelectronics Limited
 * Author: Stuart Menefy <stuart.menefy@st.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/export.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/stm/soc.h>
#include <linux/stm/sysconf.h>
#include <asm/processor.h>

unsigned long stm_soc_devid;
EXPORT_SYMBOL(stm_soc_devid);

long stm_soc_version_major_id = -1;
EXPORT_SYMBOL(stm_soc_version_major_id);

long stm_soc_version_minor_id = -1;
EXPORT_SYMBOL(stm_soc_version_minor_id);

void __init stm_soc_set(unsigned long devid, long major, long minor)
{
	stm_soc_devid = devid;
	if (major != -1)
		stm_soc_version_major_id = major;
	else
		stm_soc_version_major_id = (devid >> 28) & 0xF;
	stm_soc_version_minor_id = minor;

#ifdef CONFIG_SUPERH
	cpu_data->cut_major = stm_soc_version_major_id;
	cpu_data->cut_minor = stm_soc_version_minor_id;
#endif
}

const char *stm_soc(void)
{
	if (stm_soc_is_stid127())
		return "STiD127";
	if (stm_soc_is_stxh205())
		return "STxH205";
	if (stm_soc_is_stih407())
		return "STiH407";
	if (stm_soc_is_stih415())
		return "STiH415";
	if (stm_soc_is_stx7108())
		return "STx7108";
	if (stm_soc_is_stih416())
		return "STiH416";
	return NULL;
}
EXPORT_SYMBOL(stm_soc);

#ifdef CONFIG_OF
/* Common implementation for all STM SoC DT enabled */
void stm_of_soc_reset(char mode, const char *cmd)
{
	struct device_node *np;
	struct sysconf_field *sc, *sc_mask;
	np = of_find_node_by_path("/soc");
	if (np) {
		sc = stm_of_sysconf_claim(np, "reset");
		sc_mask = stm_of_sysconf_claim(np, "reset_mask");

		/* If there is a reset mask, we need to enable the reset bit */
		if (sc_mask)
			sysconf_write(sc_mask, 1);

		/* Reset is active low */
		sysconf_write(sc, 0);
		/* Release the mask bit */
		if (sc_mask)
			sysconf_write(sc_mask, 0);

		sysconf_release(sc);
		if (sc_mask)
			sysconf_release(sc_mask);
		of_node_put(np);
	}
}
#endif
