/*
 * (c) 2010 STMicroelectronics Limited
 *
 * Author: David Mckay <david.mckay@st.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * SH4 specific glue to join up the stm pci driver in drivers/stm/
 * to the sh specific PCI arch code. There will be a corresponding version
 * of this file for the ARM and ST231 as well. This allows the core code
 * to be maintained seperately rather than copying the driver.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/pci.h>
#include <linux/pci_regs.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/stm/platform.h>
#include <linux/stm/pci-glue.h>
#include <linux/gpio.h>
#include <linux/cache.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/list.h>
#include <asm/clock.h>

struct stm_pci_chan_info {
	struct pci_channel chan;
	enum stm_pci_type type;
	int legacy_irq;
	struct platform_device *pdev;
	int (*map_irq)(const struct pci_dev *dev, u8 slot, u8 pin);
};


/* Use container_of magic to get data */
static struct stm_pci_chan_info *bus_to_channel_info(struct pci_bus *bus)
{
	struct pci_channel *hose = bus->sysdata;

	return container_of(hose, struct stm_pci_chan_info, chan);
}

/* Given a pci_bus, return the corresponding platform data */
struct platform_device *stm_pci_bus_to_platform(struct pci_bus *bus)
{
	struct stm_pci_chan_info *info;

	info = bus_to_channel_info(bus);
	if (!info)
		return NULL;

	return info->pdev;
}

int __devinit stm_pci_register_controller(struct platform_device *pdev,
					  struct pci_ops *config_ops,
					  enum stm_pci_type type)
{
	struct stm_pci_chan_info *info;
	struct resource *res;
	void *platdata = dev_get_platdata(&pdev->dev);
	struct stm_pci_window_info *window;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	res = devm_kzalloc(&pdev->dev, sizeof(*res), GFP_KERNEL);
	if (!res)
		return -ENOMEM;

	info->chan.resources = res;
	info->chan.nr_resources = 1;

	res->flags = IORESOURCE_MEM;
	if (type == STM_PCI_EXPRESS) {
		struct stm_plat_pcie_config *config = platdata;
		window = &config->pcie_window;
		res->name = "pcie memory";
	} else {
		struct stm_plat_pci_config *config = platdata;
		window = &config->pci_window;
		res->name = "pci memory";
	}
	res->start = window->start;
	res->end = window->start + window->size - 1;

	/* Put some none zero value here to stop the generic code whining
	 * about not having io_map_base defined
	 */
	info->chan.io_map_base = ~0;

	if (type == STM_PCI_EXPRESS) {
		res = platform_get_resource_byname(pdev, IORESOURCE_IRQ,
						   "pcie inta");
		if (!res)
			return -ENXIO;
		info->legacy_irq = res->start;
	} else {
		struct stm_plat_pci_config *config = platdata;
		info->legacy_irq = -EINVAL;
		info->map_irq = config->pci_map_irq;
	}

	info->type = type;
	info->pdev = pdev;
	info->chan.pci_ops = config_ops;

	register_pci_controller(&info->chan);

	return 0;
}

enum stm_pci_type stm_pci_controller_type(struct pci_bus *bus)
{
	struct stm_pci_chan_info *info;

	info = bus_to_channel_info(bus);

	BUG_ON(!info);

	return info->type;
}

/* This is a GLOBAL function for ALL pci controllers in the system. The
 * assumption is that ALL controllers go through this glue layer. If you try
 * and use a.n. other controller then very bad things will happen. In the
 * incredibly unlikely event this does become a problem, then the container_of
 * technique will have to be replaced with list searching to find out if this
 * is an ST type controller. Cannot see that ever happening.
 */
int __init pcibios_map_platform_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	struct stm_pci_chan_info *info;

	info = bus_to_channel_info(dev->bus);
	if ((info->type == STM_PCI_EXPRESS) && (info->legacy_irq > 0))
		return info->legacy_irq;

	if (!info->map_irq)
		return -EINVAL;

	return  info->map_irq(dev, slot, pin);
}
