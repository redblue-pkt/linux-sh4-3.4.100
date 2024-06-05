/**
 * STMicroelectronics DWC3 Specific Glue Layer
 *
 * This is a small platform driver for the dwc3 to provide the glue logic
 * to configure the controller. Tested on STM platforms.
 *
 * Copyright (c) 2013 Stmicroelectronics Ltd.
 *
 * Author(s): Aymen Bouattay <aymen.bouattay@st.com>
 *            Carmelo Amoroso <carmelo.amoroso@st.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Inspired by dwc3-omap.c and dwc3-exynos.c.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/delay.h>
#include <linux/stm/platform.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>

#include "io.h"

/* Reg glue registers */
#define USB2_CLKRST_CTRL 0x00
#define aux_clk_en(n) ((n)<<0)
#define sw_pipew_reset_n(n) ((n)<<4)
#define ext_cfg_reset_n(n) ((n)<<8)
#define xhci_revision(n) ((n)<<12)

#define USB2_VBUS_MNGMNT_SEL1 0x2C
/*
 * 2'b00 : Override value from Reg 0x30 is selected
 * 2'b01 : utmiotg_vbusvalid from usb3_top top is selected
 * 2'b10 : pipew_powerpresent from PIPEW instance is selected
 * 2'b11 : value is 1'b0
 */
#define SEL_OVERRIDE_VBUSVALID(n) ((n)<<0)
#define SEL_OVERRIDE_POWERPRESENT(n) ((n)<<4)
#define SEL_OVERRIDE_BVALID(n) ((n)<<8)

#define USB2_VBUS_MNGMNT_VAL1 0x30
#define OVERRIDE_VBUSVALID_VAL (1 << 0)
#define OVERRIDE_POWERPRESENT_VAL (1 << 4)
#define OVERRIDE_BVALID_VAL (1 << 8)

struct dwc3_stm {
	struct device *dev;
	struct stm_device_state *device_state;
	struct stm_miphy *miphy;
	void __iomem *glue_base;
};

static inline u32 dwc3_stm_readl(void __iomem *base, u32 offset)
{
	return readl(base + offset);
}

static inline void dwc3_stm_writel(void __iomem *base, u32 offset, u32 value)
{
	writel(value, base + offset);
}

/**
 * dwc3_bus_setup - configure the AXI bridge as required
 * @dwc: pointer to our context structure
 */

void dwc3_bus_setup(struct dwc3 *dwc)
{
	/* INCR16 Burst Type Enable:
	 * DWC3_GSBUSCFG0[3] = 1 (AXI master uses INCR to do the 16-beat burst)
	 */
	dwc3_writel(dwc->regs, DWC3_GSBUSCFG0, 0x8);
	/* AXI Pipelined Transfers Burst Request Limit (PipeTransLimit):
	 * DWC3_GSBUSCFG1[11:8] = 0xf (outstanding pipelined transfers <= 16)
	 */
	dwc3_writel(dwc->regs, DWC3_GSBUSCFG1, 0xF00);
}

static struct stm_miphy *dwc3_stm_miphy_init(struct dwc3_stm *dwc3_data,
				       struct stm_plat_usb_data *pdata,
				       bool cold_boot)
{
	struct stm_miphy *miphy = NULL;
	u32 reg = dwc3_stm_readl(dwc3_data->glue_base, USB2_CLKRST_CTRL);

	reg |= aux_clk_en(1) | ext_cfg_reset_n(1) | xhci_revision(1);
	reg &= ~sw_pipew_reset_n(1);

	dwc3_stm_writel(dwc3_data->glue_base, USB2_CLKRST_CTRL, reg);

	reg = dwc3_stm_readl(dwc3_data->glue_base, USB2_VBUS_MNGMNT_SEL1);
	reg |= SEL_OVERRIDE_VBUSVALID(1) | SEL_OVERRIDE_POWERPRESENT(1) |
		SEL_OVERRIDE_BVALID(1);

	dwc3_stm_writel(dwc3_data->glue_base, USB2_VBUS_MNGMNT_SEL1, reg);
	udelay(100);

	if (cold_boot)
		miphy = stm_miphy_claim(pdata->miphy_num, USB30_MODE,
				dwc3_data->dev);
	else
		stm_miphy_start(dwc3_data->miphy);

	reg = dwc3_stm_readl(dwc3_data->glue_base, USB2_CLKRST_CTRL);
	reg |= sw_pipew_reset_n(1);

	dwc3_stm_writel(dwc3_data->glue_base, USB2_CLKRST_CTRL, reg);

	/* Use this delay to adjust for MiPHY HFCready assertion variation */
	udelay(1000);

	return miphy;

}

static void *dwc3_stm_dt_get_pdata(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct stm_plat_usb_data *pdata;

	pdata = devm_kzalloc(&pdev->dev, sizeof(struct stm_plat_usb_data),
			     GFP_KERNEL);

	BUG_ON(!pdata);

	of_property_read_u32(np, "miphy-num", (u32 *)&pdata->miphy_num);
	pdata->device_config = stm_of_get_dev_config(&pdev->dev);

	return pdata;
}

static int dwc3_stm_remove_child(struct device *dev, void *unused)
{
	struct platform_device *pdev = to_platform_device(dev);

	platform_device_unregister(pdev);

	return 0;
}
static int dwc3_stm_probe(struct platform_device *pdev)
{
	struct stm_plat_usb_data *pdata;
	struct dwc3_stm *dwc3_data;
	struct resource *res;
	void __iomem *glue_base;
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;

	int ret = 0;

	dwc3_data = devm_kzalloc(dev, sizeof(*dwc3_data), GFP_KERNEL);
	if (!dwc3_data)
		return -ENOMEM;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "reg-glue");
	glue_base = devm_request_and_ioremap(dev, res);

	if (!glue_base)
		return -EADDRNOTAVAIL;

	dwc3_data->glue_base = glue_base;
	dwc3_data->dev = dev;

	if (node) {
		pdata = dwc3_stm_dt_get_pdata(pdev);
		dwc3_data->device_state = devm_stm_device_init(&pdev->dev,
						       pdata->device_config);
		if (!dwc3_data->device_state)
			return -EBUSY;

		/* Allocate and initialize the core */
		ret = of_platform_populate(node, NULL, NULL, dev);
		if (ret < 0) {
			dev_err(dev, "failed to add dwc3 core\n");
			return ret;
		}
	} else {
		dev_err(dev, "no device node, failed to add dwc3 core\n");
		return -ENODEV;
	}

	platform_set_drvdata(pdev, dwc3_data);

	dwc3_data->miphy = dwc3_stm_miphy_init(dwc3_data, pdata, true);
	if (!dwc3_data->miphy)
		return -EBUSY;

	dev_info(&pdev->dev, "probed successfully\n");

	return 0;
}

static int dwc3_stm_remove(struct platform_device *pdev)
{
	device_for_each_child(&pdev->dev, NULL, dwc3_stm_remove_child);
	return 0;
}

void xhci_hub_on_disconnect(struct usb_hcd *hcd)
{
	struct dwc3 *dwc3_core;
	struct dwc3_stm *dwc3_glue;
	struct stm_miphy *miphy;

	if (hcd->speed != HCD_USB3)
		return;

	dwc3_core = dev_get_drvdata(hcd->self.controller->parent);
	dwc3_glue = dev_get_drvdata(dwc3_core->dev->parent);
	miphy = dwc3_glue->miphy;

	stm_miphy_calibrate(miphy);
}

#if IS_ENABLED(CONFIG_USB_DWC3_GADGET) || IS_ENABLED(CONFIG_USB_DWC3_DUAL_ROLE)
void dwc3_gadget_on_disconnect(struct dwc3 *dwc3_core)
{
	struct dwc3_stm *dwc3_glue;
	struct stm_miphy *miphy;
	u32 reg, speed;

	reg = dwc3_readl(dwc3_core->regs, DWC3_DSTS);
	speed = reg & DWC3_DSTS_CONNECTSPD;
	if (speed != DWC3_DSTS_SUPERSPEED)
		return;

	dwc3_glue = dev_get_drvdata(dwc3_core->dev->parent);
	miphy = dwc3_glue->miphy;
	stm_miphy_calibrate(miphy);
}
#endif

#ifdef CONFIG_PM

static int dwc3_stm_suspend(struct device *dev)
{
	struct dwc3_stm *dwc3_data = dev_get_drvdata(dev);

	stm_device_power(dwc3_data->device_state, stm_device_power_off);

	return 0;
}

static int dwc3_stm_resume(struct device *dev)
{
	struct dwc3_stm *dwc3_data = dev_get_drvdata(dev);
	struct stm_plat_usb_data *pdata = dev_get_platdata(dev);

	stm_device_setup(dwc3_data->device_state);

	stm_device_power(dwc3_data->device_state, stm_device_power_on);

	dwc3_stm_miphy_init(dwc3_data, pdata, false);

	return 0;
}

SIMPLE_DEV_PM_OPS(dwc3_stm_pm, dwc3_stm_suspend, dwc3_stm_resume);
#define DW3_STM_PM_OPS		(&dwc3_stm_pm)
#else
#define DW3_STM_PM_OPS		NULL
#endif

#ifdef CONFIG_OF
static struct of_device_id dwc3_stm_match[] = {
	{
	 .compatible = "st,dwc3",
	 },
	{},
};

MODULE_DEVICE_TABLE(of, dwc3_stm_match);
#endif

static struct platform_driver dwc3_stm_driver = {
	.probe = dwc3_stm_probe,
	.remove = dwc3_stm_remove,
	.driver = {
		   .name = "usb-stm-dwc3",
		   .pm = DW3_STM_PM_OPS,
		   .owner = THIS_MODULE,
		   .of_match_table = of_match_ptr(dwc3_stm_match),
		   },
};

module_platform_driver(dwc3_stm_driver);

MODULE_ALIAS("platform:usb-stm-dwc3");
MODULE_AUTHOR("Aymen bouattay <aymen.bouattay@st.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("DesignWare USB3 STM Glue Layer");
