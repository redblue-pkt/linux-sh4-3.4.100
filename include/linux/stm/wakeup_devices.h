/*
 * Copyright (C) 2010  STMicroelectronics
 * Author: Francesco M. Virlinzi  <francesco.virlinzi@st.com>
 *
 * May be copied or modified under the terms of the GNU General Public
 * License V.2 ONLY.  See linux/COPYING for more information.
 */

#ifndef __LINUX_STM_WAKEUP_DEVICES__
#define __LINUX_STM_WAKEUP_DEVICES__

/*
 * the stm_wakeup_devices tracks __ONLY__ the special devices which have
 * a constraint to wakeup; i.e.:
 *
 * - irb.scd needs at least 1 MHz
 *
 * - hdmi_cec needs 100 MHz.
 *
 * - eth_phy needs 25 MHz (125 MHz in gphy)
 *
 * In some SOC these IPs share the same clock therefore a fine tuning
 * has to be done to set the clock according
 * the currently enabled wakeup devices.
 *
 * Currently the other IPs (i.e.: ASC, PIO) didn't raise any particular issues.
 */

struct stm_wakeup_devices {
	unsigned int lirc_can_wakeup:1;		/* lirc_scd_clk >= 1 MHz */
	unsigned int hdmi_can_wakeup:1;		/* hdmi_clk == 100 MHz	 */
	unsigned int stm_mac0_can_wakeup:1;	/* eth_phy_clk ~= 25 MHz */
	unsigned int stm_mac1_can_wakeup:1;
	unsigned int stm_phy_can_wakeup:1;
	unsigned int hdmi_cec:1;
	unsigned int hdmi_hotplug:1;
	unsigned int kscan:1;
	unsigned int asc:1;
	unsigned int rtc:1;
	unsigned int rtc_sbc:1;
};

int stm_check_wakeup_devices(struct stm_wakeup_devices *dev_wk);
#endif
