/*
 * STMicroelectronics MiPHY 32/28LP style code
 *
 * Copyright (C) 2013 STMicroelectronics Limited
 * Author: Harsh Gupta <harsh.gupta@st.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/stm/platform.h>
#include "miphy.h"

/*
 * Versions:
 *  1.0a: first driver plus usb3 working.
 *  1.1b: New driver version: simply the procedure to write the internal
 *	  registers
 *	  Removed useless header file inclusions.
 *	  Fixed pipew ready procedure.
 *	  Fixed MiPHY / PCIe: tuned internal register settings and
 *			      fixed broken pipe wrapper configuration.
 */
#define DRIVER_VERSION "MiPHY28LP Driver Version 1.0b"

#define MIPHY_VERSION	0xfe
#define MIPHY_REVISION	0xff


static inline int miphy_is_ready(struct stm_miphy *miphy)
{
	unsigned long curr;
	unsigned long finish = jiffies + 5 * HZ;

	/* Wait for phy_ready / hfc_ready / pll_ready */
	do {
		curr = jiffies;
		if (stm_miphy_read(miphy, 0x02) & 0x03)
			cpu_relax();
		else
			return 0;
	} while (!time_after_eq(curr, finish));

	return -EBUSY;
}

static int miphy28lp_sata_start(struct stm_miphy *miphy)
{

	/* Putting Macro in reset */
	stm_miphy_write(miphy, 0x00, 0x01);
	stm_miphy_write(miphy, 0x00, 0x03);

	udelay(10);

	stm_miphy_write(miphy, 0x00, 0x01);
	stm_miphy_write(miphy, 0x04, 0x1c);

	/* PLL calibration */
	stm_miphy_write(miphy, 0xeb, 0x1d);
	stm_miphy_write(miphy, 0x0d, 0x1e);
	stm_miphy_write(miphy, 0x0f, 0x00);

	/* Writing The PLL Ratio */
	stm_miphy_write(miphy, 0xd4, 0xc8);
	stm_miphy_write(miphy, 0xd5, 0x00);
	stm_miphy_write(miphy, 0xd6, 0x00);
	stm_miphy_write(miphy, 0xd7, 0x00);
	stm_miphy_write(miphy, 0xd3, 0x00);
	stm_miphy_write(miphy, 0x0f, 0x02);
	stm_miphy_write(miphy, 0x0e, 0x0a);
	stm_miphy_write(miphy, 0x0f, 0x01);
	stm_miphy_write(miphy, 0x0e, 0x0a);
	stm_miphy_write(miphy, 0x0f, 0x00);
	stm_miphy_write(miphy, 0x0e, 0x0a);
	stm_miphy_write(miphy, 0x4e, 0xd1);
	stm_miphy_write(miphy, 0x4e, 0xd1);

	/* Rx Calibration */
	stm_miphy_write(miphy, 0x99, 0x1f);
	stm_miphy_write(miphy, 0x0a, 0x41);
	stm_miphy_write(miphy, 0x7a, 0x0d);
	stm_miphy_write(miphy, 0x7f, 0x7d);
	stm_miphy_write(miphy, 0x80, 0x56);
	stm_miphy_write(miphy, 0x81, 0x00);
	stm_miphy_write(miphy, 0x7b, 0x00);
	stm_miphy_write(miphy, 0xc1, 0x01);
	stm_miphy_write(miphy, 0xc2, 0x01);
	stm_miphy_write(miphy, 0x97, 0xf3);
	stm_miphy_write(miphy, 0xc9, 0x02);
	stm_miphy_write(miphy, 0xca, 0x02);
	stm_miphy_write(miphy, 0xcb, 0x02);
	stm_miphy_write(miphy, 0xcc, 0x0a);
	stm_miphy_write(miphy, 0x9d, 0xe5);
	stm_miphy_write(miphy, 0x0f, 0x00);
	stm_miphy_write(miphy, 0x0e, 0x02);
	stm_miphy_write(miphy, 0x0e, 0x00);
	stm_miphy_write(miphy, 0x63, 0x00);
	stm_miphy_write(miphy, 0x64, 0xaf);
	stm_miphy_write(miphy, 0x0f, 0x00);
	stm_miphy_write(miphy, 0x49, 0x07);
	stm_miphy_write(miphy, 0x0f, 0x01);
	stm_miphy_write(miphy, 0x49, 0x07);
	stm_miphy_write(miphy, 0x0f, 0x02);
	stm_miphy_write(miphy, 0x49, 0x07);
	stm_miphy_write(miphy, 0x0f, 0x03);
	stm_miphy_write(miphy, 0x49, 0x07);
	stm_miphy_write(miphy, 0x0f, 0x00);
	stm_miphy_write(miphy, 0x4a, 0x50);
	stm_miphy_write(miphy, 0x4a, 0x53);
	stm_miphy_write(miphy, 0x4b, 0x00);
	stm_miphy_write(miphy, 0x4b, 0x00);
	stm_miphy_write(miphy, 0x0f, 0x01);
	stm_miphy_write(miphy, 0x0e, 0x04);
	stm_miphy_write(miphy, 0x0e, 0x05);
	stm_miphy_write(miphy, 0x63, 0x00);
	stm_miphy_write(miphy, 0x64, 0xae);
	stm_miphy_write(miphy, 0x0f, 0x00);
	stm_miphy_write(miphy, 0x49, 0x07);
	stm_miphy_write(miphy, 0x0f, 0x01);
	stm_miphy_write(miphy, 0x49, 0x07);
	stm_miphy_write(miphy, 0x0f, 0x02);
	stm_miphy_write(miphy, 0x49, 0x07);
	stm_miphy_write(miphy, 0x0f, 0x03);
	stm_miphy_write(miphy, 0x49, 0x07);
	stm_miphy_write(miphy, 0x0f, 0x01);
	stm_miphy_write(miphy, 0x4a, 0x73);
	stm_miphy_write(miphy, 0x4a, 0x72);
	stm_miphy_write(miphy, 0x4b, 0x20);
	stm_miphy_write(miphy, 0x4b, 0x20);
	stm_miphy_write(miphy, 0x0f, 0x02);
	stm_miphy_write(miphy, 0x0e, 0x09);
	stm_miphy_write(miphy, 0x0e, 0x0a);
	stm_miphy_write(miphy, 0x63, 0x00);
	stm_miphy_write(miphy, 0x64, 0xae);
	stm_miphy_write(miphy, 0x0f, 0x00);
	stm_miphy_write(miphy, 0x49, 0x07);
	stm_miphy_write(miphy, 0x0f, 0x01);
	stm_miphy_write(miphy, 0x49, 0x07);
	stm_miphy_write(miphy, 0x0f, 0x02);
	stm_miphy_write(miphy, 0x49, 0x07);
	stm_miphy_write(miphy, 0x0f, 0x03);
	stm_miphy_write(miphy, 0x49, 0x07);
	stm_miphy_write(miphy, 0x0f, 0x02);
	stm_miphy_write(miphy, 0x4a, 0xc2);
	stm_miphy_write(miphy, 0x4a, 0xc0);
	stm_miphy_write(miphy, 0x4b, 0x20);
	stm_miphy_write(miphy, 0x4b, 0x20);
	stm_miphy_write(miphy, 0xcd, 0x21);
	stm_miphy_write(miphy, 0xcd, 0x21);
	stm_miphy_write(miphy, 0x00, 0x00);
	stm_miphy_write(miphy, 0x00, 0x01);
	stm_miphy_write(miphy, 0x00, 0x00);
	stm_miphy_write(miphy, 0x01, 0x04);
	stm_miphy_write(miphy, 0x01, 0x05);
	stm_miphy_write(miphy, 0xe9, 0x00);
	stm_miphy_write(miphy, 0x0d, 0x1e);
	stm_miphy_write(miphy, 0x3a, 0x40);
	stm_miphy_write(miphy, 0x01, 0x01);
	stm_miphy_write(miphy, 0x01, 0x00);
	stm_miphy_write(miphy, 0xe9, 0x40);
	stm_miphy_write(miphy, 0x0a, 0x41);
	stm_miphy_write(miphy, 0x0f, 0X00);
	stm_miphy_write(miphy, 0x0b, 0x00);
	stm_miphy_write(miphy, 0x0b, 0x00);
	stm_miphy_write(miphy, 0x62, 0x00);
	stm_miphy_write(miphy, 0x0f, 0x00);
	stm_miphy_write(miphy, 0xe3, 0x02);
	stm_miphy_write(miphy, 0xe3, 0x02);
	stm_miphy_write(miphy, 0x26, 0x27);
	stm_miphy_write(miphy, 0x26, 0x00);
	stm_miphy_write(miphy, 0x26, 0x62);
	stm_miphy_write(miphy, 0x26, 0x00);
	stm_miphy_write(miphy, 0x0f, 0x00);
	stm_miphy_write(miphy, 0x0f, 0x01);
	stm_miphy_write(miphy, 0x0f, 0x02);
	stm_miphy_write(miphy, 0x0f, 0x00);

	/* Originally mdelay(100), don't think it is needed */
	return miphy_is_ready(miphy);
}

static int miphy28lp_pcie_start(struct stm_miphy *miphy)
{

	/* Resetting the MIPHY-CPU registers and macro */
	stm_miphy_write(miphy, 0x00, 0x03);

	udelay(10);

	/* Bringing the MIPHY-CPU registers out of reset */
	stm_miphy_write(miphy, 0x00, 0x01);
	stm_miphy_write(miphy, 0x04, 0x14);

	/* Applying PLL Settings */
	stm_miphy_write(miphy, 0xEB, 0x1D);
	stm_miphy_write(miphy, 0x0D, 0x1E);

	/* Writing the PLL Ratio */
	stm_miphy_write(miphy, 0xD4, 0xA6);
	stm_miphy_write(miphy, 0xD5, 0xAA);
	stm_miphy_write(miphy, 0xD6, 0xAA);
	stm_miphy_write(miphy, 0xD7, 0x00);
	stm_miphy_write(miphy, 0xD3, 0x00);
	stm_miphy_write(miphy, 0x0A, 0x40);

	/* Writing the unbanked settings */
	stm_miphy_write(miphy, 0x4E, 0xD1);	/* TX slew */
	stm_miphy_write(miphy, 0x99, 0x5F);	/* Cal Offset */

	/*Banked Settings Bank-0 PCIE GEN-1*/

	stm_miphy_write(miphy, 0x0F, 0x00);
	stm_miphy_write(miphy, 0x0E, 0x05);
	stm_miphy_write(miphy, 0x63, 0x00);	/* PLL Settings */
	stm_miphy_write(miphy, 0x64, 0xA5);
	stm_miphy_write(miphy, 0x49, 0x07);
	/*TX buffer Settings*/
	stm_miphy_write(miphy, 0x4A, 0x71);	/* Swing Slew Settings */
	stm_miphy_write(miphy, 0x4B, 0x60);	/* Pre-emphasis */
	/*RX Buffer Settings*/
	stm_miphy_write(miphy, 0x78, 0x98);
	stm_miphy_write(miphy, 0x7A, 0x0D);
	stm_miphy_write(miphy, 0x7B, 0x00);	/* RX Equa Gain  4db */
	stm_miphy_write(miphy, 0x7F, 0x79);
	stm_miphy_write(miphy, 0x80, 0x56);	/* RX Equa Boost 6db */

	/* Banked Settings Bank-1 PCIE GEN-2 */

	stm_miphy_write(miphy, 0x0F, 0x01);
	stm_miphy_write(miphy, 0x0E, 0x0A);
	stm_miphy_write(miphy, 0x63, 0x00);	/* PLL settings */
	stm_miphy_write(miphy, 0x64, 0xA5);
	stm_miphy_write(miphy, 0x49, 0x07);
	/*TX Buffer Settings*/
	stm_miphy_write(miphy, 0x4A, 0x70);
	stm_miphy_write(miphy, 0x4B, 0x60);
	/*RX Buffer Settings*/
	stm_miphy_write(miphy, 0x78, 0x9C);
	stm_miphy_write(miphy, 0x7A, 0x0D);
	stm_miphy_write(miphy, 0x7B, 0x00);
	stm_miphy_write(miphy, 0x7F, 0x79);
	stm_miphy_write(miphy, 0x80, 0x6A);	/* Rx Equa Boost 8db */

	stm_miphy_write(miphy, 0xCD, 0x21);
	stm_miphy_write(miphy, 0x00, 0x00);	/* Macro Out of reset */
	stm_miphy_write(miphy, 0x01, 0x05);
	stm_miphy_write(miphy, 0xE9, 0x00);
	stm_miphy_write(miphy, 0x0D, 0x1E);	/* PLL Ref clock */
	stm_miphy_write(miphy, 0x3A, 0x40);

	udelay(100);

	stm_miphy_write(miphy, 0x01, 0x01);
	stm_miphy_write(miphy, 0x01, 0x00);
	stm_miphy_write(miphy, 0xE9, 0x40);
	stm_miphy_write(miphy, 0xE3, 0x02);

	/* Waiting for Compensation to complete */
	while ((stm_miphy_read(miphy, 0x3f) & 0x80) != 0x80)
		cpu_relax();



	/* PIPE Wrapper Configuration */
	stm_miphy_pipe_write(miphy, 0x104, 0X68); /* Rise_0 */
	stm_miphy_pipe_write(miphy, 0x105, 0X61); /* Rise_1 */
	stm_miphy_pipe_write(miphy, 0x108, 0X68); /* Fall_0 */
	stm_miphy_pipe_write(miphy, 0x109, 0X61); /* Fall-1 */
	stm_miphy_pipe_write(miphy, 0x10C, 0x68); /* Threshhold_0 */
	stm_miphy_pipe_write(miphy, 0x10D, 0X60); /* Threshold_1 */

	/* Wait for phy_ready */
	return miphy_is_ready(miphy);
}

static int miphy28lp_usb3_start(struct stm_miphy *miphy)
{

	/* Putting Macro in reset */
	stm_miphy_write(miphy, 0x00, 0x01);
	stm_miphy_write(miphy, 0x00, 0x03);

	/* Wait for a while */
	stm_miphy_write(miphy, 0x00, 0x01);
	stm_miphy_write(miphy, 0x04, 0x1C);

	/* PLL calibration */
	stm_miphy_write(miphy, 0xEB, 0x1D);
	stm_miphy_write(miphy, 0x0D, 0x1E);
	stm_miphy_write(miphy, 0x0F, 0x00);
	stm_miphy_write(miphy, 0xC4, 0x70);
	stm_miphy_write(miphy, 0xC9, 0x02);
	stm_miphy_write(miphy, 0xCA, 0x02);
	stm_miphy_write(miphy, 0xCB, 0x02);
	stm_miphy_write(miphy, 0xCC, 0x0A);

	/* Writing The PLL Ratio */
	stm_miphy_write(miphy, 0xD4, 0xA6);
	stm_miphy_write(miphy, 0xD5, 0xAA);
	stm_miphy_write(miphy, 0xD6, 0xAA);
	stm_miphy_write(miphy, 0xD7, 0x04);
	stm_miphy_write(miphy, 0xD3, 0x00);

	/* Writing The Speed Rate */
	stm_miphy_write(miphy, 0x0F, 0x00); /* Write to register bank 1 */
	stm_miphy_write(miphy, 0x0E, 0x0A); /* TX_SPDSEL=0b10 and RX_SPDSEL=0b10 */

	/* Rx Channel compensation and calibration */
	stm_miphy_write(miphy, 0xC2, 0x1C); /* Disable AUto Calibration for DC gain */
	stm_miphy_write(miphy, 0x97, 0x51); /* Enable Fine Cal */
	stm_miphy_write(miphy, 0x98, 0x70); /* Enable Fine Cal */
	stm_miphy_write(miphy, 0x99, 0x5F); /* Enable VGA Cal for 64 bits */
	stm_miphy_write(miphy, 0x9A, 0x22);
	stm_miphy_write(miphy, 0x9F, 0x0E); /* Enable Fine Cal */

	stm_miphy_write(miphy, 0x7A, 0x05); /* VGA GAIN, EQ GAIN set manaually */
	stm_miphy_write(miphy, 0x7F, 0x78); /* EQU_GAIN_MAN = +5dB  */
	stm_miphy_write(miphy, 0x30, 0x1B); /* SYNC_CHAR_EN to be programmed for 10 bit */

	/* Enable GENSEL_SEL and SSC */
	stm_miphy_write(miphy, 0x0A, 0x11); /* SSC_SEL=1, GENSEL_SEL=1, TX_SEL=0 swing preemp forced by pipe registres */

	/* MIPHY Bias boost */
	stm_miphy_write(miphy, 0x63, 0x00);
	stm_miphy_write(miphy, 0x64, 0xA7);

	/* TX compensation offset to re-center TX impedance */
	stm_miphy_write(miphy, 0x42, 0x02);

	/* SSC modulation */
	stm_miphy_write(miphy, 0x0C, 0x04); /* SSC_EN_SW=1 */

	stm_miphy_write(miphy, 0x0F, 0x00);
	stm_miphy_write(miphy, 0xE5, 0x5A);
	stm_miphy_write(miphy, 0xE6, 0xA0);
	stm_miphy_write(miphy, 0xE4, 0x3C);
	stm_miphy_write(miphy, 0xE6, 0xA1);
	stm_miphy_write(miphy, 0xE3, 0x00);
	stm_miphy_write(miphy, 0xE3, 0x02);
	stm_miphy_write(miphy, 0xE3, 0x00);

	/* Rx PI controller settings */
	stm_miphy_write(miphy, 0x78, 0xCA); /* autogain=1, Ki=4, Kp=A */

	/* MIPHY RX input bridge control*/
	stm_miphy_write(miphy, 0xCD, 0x21); /* INPUT_BRIDGE_EN_SW=1, manual input bridge control[0]=1 */

	/* Offset Calibration */
	stm_miphy_write(miphy, 0xCD, 0x29);  /* INPUT_BRIDGE_EN_SW=1,  manual threshold control=1 */
	stm_miphy_write(miphy, 0xCE, 0x1A);

	/* MIPHY Reset*/
	stm_miphy_write(miphy, 0x00, 0x01); /* RST_APPLI_SW=1 */
	stm_miphy_write(miphy, 0x00, 0x00); /* RST_APPLI_SW=0 */
	stm_miphy_write(miphy, 0x01, 0x04); /* RST_COMP_SW = 1 */
	stm_miphy_write(miphy, 0x01, 0x05); /* RST_PLL_SW = 1 */

	stm_miphy_write(miphy, 0xE9, 0x00);
	stm_miphy_write(miphy, 0x0D, 0x1E);
	stm_miphy_write(miphy, 0x3A, 0x40);
	stm_miphy_write(miphy, 0x01, 0x01);
	stm_miphy_write(miphy, 0x01, 0x00);
	stm_miphy_write(miphy, 0xE9, 0x40);
	stm_miphy_write(miphy, 0x0F, 0x00);
	stm_miphy_write(miphy, 0x0B, 0x00);
	stm_miphy_write(miphy, 0x62, 0x00);
	stm_miphy_write(miphy, 0x0F, 0x00);
	stm_miphy_write(miphy, 0xE3, 0x02);
	stm_miphy_write(miphy, 0x26, 0xa5);
	stm_miphy_write(miphy, 0x0F, 0x00);

	/* PIPE Wrapper Configuration */
	stm_miphy_pipe_write(miphy, 0x23, 0X68);
	stm_miphy_pipe_write(miphy, 0x24, 0X61);
	stm_miphy_pipe_write(miphy, 0x26, 0X68);
	stm_miphy_pipe_write(miphy, 0x27, 0X61);
	stm_miphy_pipe_write(miphy, 0x29, 0X18);
	stm_miphy_pipe_write(miphy, 0x2A, 0X61);

	/*pipe Wrapper usb3 TX swing de-emph margin PREEMPH[7:4], SWING[3:0] */
	stm_miphy_pipe_write(miphy, 0x68, 0x67); /* margin 0 */
	stm_miphy_pipe_write(miphy, 0x69, 0x0D); /* margin 1 */
	stm_miphy_pipe_write(miphy, 0x6A, 0x67); /* margin 2 */
	stm_miphy_pipe_write(miphy, 0x6B, 0x0D); /* margin 3 */
	stm_miphy_pipe_write(miphy, 0x6C, 0x67); /* margin 4 */
	stm_miphy_pipe_write(miphy, 0x6D, 0x0D); /* margin 5 */
	stm_miphy_pipe_write(miphy, 0x6E, 0x67); /* margin 6 */
	stm_miphy_pipe_write(miphy, 0x6F, 0x0D); /* margin 7 */

	return 0;
}

static int miphy28lp_start(struct stm_miphy *miphy)
{
	int rval = -ENODEV;

	switch (miphy->mode) {
	case SATA_MODE:
		rval = miphy28lp_sata_start(miphy);
		break;
	case PCIE_MODE:
		rval = miphy28lp_pcie_start(miphy);
		break;
	case USB30_MODE:
		rval = miphy28lp_usb3_start(miphy);
		break;
	default:
		BUG();
	}

	/* Pass the device version and revision to the main */
	miphy->miphy_version = stm_miphy_read(miphy, MIPHY_VERSION);
	miphy->miphy_revision = stm_miphy_read(miphy, MIPHY_REVISION);

	return rval;
}

static void miphy28lp_calibrate(struct stm_miphy *miphy)
{
	stm_miphy_write(miphy, 0x01, 0x40);
	stm_miphy_write(miphy, 0x01, 0x00);

	dev_info(miphy->dev->parent, "miphy autocalibration done\n");

	/* check status */
	if ((stm_miphy_read(miphy, 0x83)) ||
	    (stm_miphy_read(miphy, 0x84)))
		dev_warn(miphy->dev->parent, "miphy autocalibration failed!\n");
}

/* MiPHY28lp Ops */
static struct stm_miphy_style_ops miphy28lp_ops = {
	.miphy_start = miphy28lp_start,
	.miphy_calibrate = miphy28lp_calibrate,
};

static int miphy28lp_probe(struct stm_miphy *miphy)
{
	pr_info("MiPHY driver style %s probed successfully\n", ID_MIPHY28LP);

	return 0;
}

static int miphy28lp_remove(void)
{
	return 0;
}

static struct stm_miphy_style miphy28lp_style = {
	.style_id	= ID_MIPHY28LP,
	.probe		= miphy28lp_probe,
	.remove		= miphy28lp_remove,
	.miphy_ops	= &miphy28lp_ops,
};

static int __init miphy28lp_init(void)
{
	int result;

	result = miphy_register_style(&miphy28lp_style);
	if (result)
		pr_err("MiPHY driver style %s register failed (%d)",
				ID_MIPHY28LP, result);

	return result;
}
postcore_initcall(miphy28lp_init);

MODULE_AUTHOR("Harsh Gupta <harsh.gupta@st.com>");
MODULE_DESCRIPTION(DRIVER_VERSION);
MODULE_LICENSE("GPL");
