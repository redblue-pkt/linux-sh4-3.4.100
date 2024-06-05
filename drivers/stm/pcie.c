/*
 * drivers/stm/pcie.c
 *
 * PCI express driver
 *
 * Copyright 2010 ST Microelectronics (R&D) Ltd.
 * Author: David J. McKay (david.mckay@st.com)
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the top level directory for more details.
 *
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/pci.h>
#include <linux/pci_regs.h>
#include <linux/msi.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/stm/platform.h>
#include <linux/stm/pci-glue.h>
#include <linux/stm/miphy.h>
#include <linux/gpio.h>
#include <linux/cache.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include "pcie-regs.h"
#include "abort.h"

#ifdef CONFIG_ARM
/* To get chained_irq_enter/exit */
#include <asm/mach/irq.h>
#endif


struct stm_msi_info;

struct stm_pcie_dev_data {
	void __iomem *cntrl; /* Main PCIe control registers */
	void __iomem *ahb; /* Amba->stbus convertor registers */
	void __iomem *config_area; /* Config read/write */
	struct stm_msi_info *msi;
	struct stm_miphy *miphy_dev; /* Handle for associated miphy */
};

/* Routines to access the DBI port of the synopsys IP. This contains the
 * standard config registers, as well as some other registers. Unfortunately,
 * byte and half word accesses are totally broken, as the synopsys IP doesn't
 * support them. Which is a pain.
 *
 * We do not have a spinlock for these, so be careful of your usage. Relies on
 * the config spinlock for config cycles.
 */

/* Little helper function to build up correct data */
static inline u32 shift_data_read(int where, int size, u32 data)
{
	data >>= (8 * (where & 0x3));

	switch (size) {
	case 1:
		data &= 0xff;
		break;
	case 2:
		BUG_ON(where & 1);
		data &= 0xffff;
		break;
	case 4:
		BUG_ON(where & 3);
		break;
	default:
		BUG();
	}

	return data;
}

static u32 dbi_read(struct stm_pcie_dev_data *priv, int where, int size)
{
	u32 data;

	/* Read the dword aligned data */
	data = readl(priv->cntrl + (where & ~0x3));

	return shift_data_read(where, size, data);
}

static inline u8 dbi_readb(struct stm_pcie_dev_data *priv, unsigned addr)
{
	return (u8) dbi_read(priv, addr, 1);
}

static inline u16 dbi_readw(struct stm_pcie_dev_data *priv, unsigned addr)
{
	return (u16) dbi_read(priv, addr, 2);
}

static inline u32 dbi_readl(struct stm_pcie_dev_data *priv, unsigned addr)
{
	return dbi_read(priv, addr, 4);
}

static inline u32 shift_data_write(int where, int size, u32 val, u32 data)
{
	int shift_bits = (where & 0x3) * 8;

	switch (size) {
	case 1:
		data &= ~(0xff << shift_bits);
		data |= ((val & 0xff) << shift_bits);
		break;
	case 2:
		data &= ~(0xffff << shift_bits);
		data |= ((val & 0xffff) << shift_bits);
		BUG_ON(where & 1);
		break;
	case 4:
		data = val;
		BUG_ON(where & 3);
		break;
	default:
		BUG();
	}

	return data;
}

static void dbi_write(struct stm_pcie_dev_data *priv,
		      u32 val, int where, int size)
{
	u32 uninitialized_var(data);
	int aligned_addr = where & ~0x3;

	/* Read the dword aligned data if we have to */
	if (size != 4)
		data = readl(priv->cntrl + aligned_addr);

	data = shift_data_write(where, size, val, data);

	writel(data, priv->cntrl + aligned_addr);
}

static inline void dbi_writeb(struct stm_pcie_dev_data *priv,
			      u8 val, unsigned addr)
{
	dbi_write(priv, (u32) val, addr, 1);
}

static inline void dbi_writew(struct stm_pcie_dev_data *priv,
			      u16 val, unsigned addr)
{
	dbi_write(priv, (u32) val, addr, 2);
}

static inline void dbi_writel(struct stm_pcie_dev_data *priv,
			      u32 val, unsigned addr)
{
	dbi_write(priv, (u32) val, addr, 4);
}


static inline void pcie_cap_writew(struct stm_pcie_dev_data *priv,
				   u16 val, unsigned cap)
{
	dbi_writew(priv, val, CFG_PCIE_CAP + cap);
}

static inline u16 pcie_cap_readw(struct stm_pcie_dev_data *priv,
				 unsigned cap)
{
	return dbi_readw(priv, CFG_PCIE_CAP + cap);
}

/* Time to wait between testing the link in msecs. There is a mystery
 * here, in that if you reduce this time to say 10us, the link never
 * actually appears to come up. I cannot think of any explanation
 * for this behaviour
 *
 * I choose 12ms here because this is the frequency that the hardware
 * polls from detect.quiet -> detect.active
 */
#define LINK_LOOP_DELAY_MS 12
/* Total amount of time to wait for the link to come up in msecs */
#define LINK_WAIT_MS 120
#define LINK_LOOP_COUNT (LINK_WAIT_MS / LINK_LOOP_DELAY_MS)

/* Function to test if the link is in an operational state or not. We must
 * ensure the link is operational before we try to do a configuration access,
 * as the hardware hangs if the link is down. I think this is a bug.
 */
static int link_up(struct stm_pcie_dev_data *priv)
{
	u32 status;
	int link_up;
	int count = 0;

	/* We have to be careful here. This is used in config read/write,
	 * The higher levels switch off interrupts, so you cannot use
	 * jiffies to do a timeout, or reschedule
	 */
	do {
		/* What about L2? I think software intervention is
		 * required to get it out of L2, so in effect the link
		 * is down. Requires more work when/if we implement power
		 * management
		 */
		status = dbi_readl(priv, PORT_LOGIC_DEBUG_REG_0);
		status &= DEBUG_REG_0_LTSSM_MASK;

		link_up = (status == S_L0) || (status == S_L0S) ||
			  (status == S_L1_IDLE);

		/* It can take some considerable time for the link to actually
		 * come up, caused by the PLLs. Experiments indicate it takes
		 * about 8ms to actually bring the link up, but this can vary
		 * considerably according to the specification. This code should
		 * allow sufficient time
		 */
		if (!link_up)
			mdelay(LINK_LOOP_DELAY_MS);

	} while (!link_up  && ++count < LINK_LOOP_COUNT);

	return link_up;
}

static struct stm_pcie_dev_data *stm_pci_bus_to_dev_data(struct pci_bus *bus)
{
	struct platform_device *pdev;

	/* This must only be called for pci express type buses */
	if (stm_pci_controller_type(bus) != STM_PCI_EXPRESS)
		return NULL;

	pdev = stm_pci_bus_to_platform(bus);
	if (!pdev)
		return NULL;

	return platform_get_drvdata(pdev);
}

/*
 * The PCI express core IP expects the following arrangement on it's address
 * bus (slv_haddr) when driving config cycles.
 * bus_number		[31:24]
 * dev_number		[23:19]
 * func_number		[18:16]
 * unused		[15:12]
 * ext_reg_number	[11:8]
 * reg_number		[7:2]
 *
 * Bits [15:12] are unused.
 *
 * In the glue logic there is a 64K region of address space that can be
 * written/read to generate config cycles. The base address of this is
 * controlled by CFG_BASE_ADDRESS. There are 8 16 bit registers called
 * FUNC0_BDF_NUM to FUNC8_BDF_NUM. These split the bottom half of the 64K
 * window into 8 regions at 4K boundaries (quite what the other 32K is for is a
 * mystery). These control the bus,device and function number you are trying to
 * talk to.
 *
 * The decision on whether to generate a type 0 or type 1 access is controlled
 * by bits 15:12 of the address you write to.  If they are zero, then a type 0
 * is generated, if anything else it will be a type 1. Thus the bottom 4K
 * region controlled by FUNC0_BDF_NUM can only generate type 0, all the others
 * can only generate type 1.
 *
 * We only use FUNC0_BDF_NUM and FUNC1_BDF_NUM. Which one you use is selected
 * by bit 12 of the address you write to. The selected register is then used
 * for the top 16 bits of the slv_haddr to form the bus/dev/func, bit 15:12 are
 * wired to zero, and bits 11:2 form the address of the register you want to
 * read in config space.
 *
 * We always write FUNC0_BDF_NUM as a 32 bit write. So if we want type 1
 * accesses we have to shift by 16 so in effect we are writing to FUNC1_BDF_NUM
 *
 */

static inline u32 bdf_num(int bus, int devfn, int is_root_bus)
{
	return ((bus << 8) | devfn) << (is_root_bus ? 0 : 16);
}

static inline unsigned config_addr(int where, int is_root_bus)
{
	return (where & ~3) | (!is_root_bus << 12);
}

static int stm_pcie_config_read(struct pci_bus *bus, unsigned int devfn,
				int where, int size, u32 *val)
{
	u32 bdf;
	u32 data;
	int slot = PCI_SLOT(devfn);
	unsigned long flags;
	struct stm_pcie_dev_data *priv = stm_pci_bus_to_dev_data(bus);
	int is_root_bus = pci_is_root_bus(bus);
	int retry_count = 0;
	int ret;

	/* PCI express devices will respond to all config type 0 cycles, since
	 * they are point to point links. Thus to avoid probing for multiple
	 * devices on the root bus we simply ignore any request for anything
	 * other than slot 1 if it is on the root bus. The switch will reject
	 * requests for slots it knows do not exist.
	 *
	 * We have to check for the link being up as we will hang if we issue
	 * a config request and the link is down.
	 */
	if (!priv || (is_root_bus && slot != 1) || !link_up(priv)) {
		*val = ~0;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}

	bdf = bdf_num(bus->number, devfn, is_root_bus);

retry:
	/* Claim lock, FUNC[01]_BDF_NUM has to remain unchanged for the whole
	 * cycle
	 */
	spin_lock_irqsave(&stm_abort_lock, flags);

	/* Set the config packet devfn */
	dbi_writel(priv, bdf, FUNC0_BDF_NUM);
	/* We have to read it back here, as there is a race condition between
	 * the write to the FUNC0_BDF_NUM and the actual read from the config
	 * address space. These appear to be different stbus targets, and it
	 * looks as if the read can overtake the write sometimes. The result of
	 * this is that you end up reading from the wrong device.
	 */
	dbi_readl(priv, FUNC0_BDF_NUM);

	atomic_set(&stm_abort_flag, 0);
	ret = PCIBIOS_SUCCESSFUL;

	/* Get the addresses of where we expect the aborts to happen. These are
	 * placed as close as possible to the code due to branch length
	 * limitations on the ARM
	 */
	GET_LABEL_ADDR(config_read_start, abort_start);
	GET_LABEL_ADDR(config_read_end, abort_end);
	/* Mark start of region where we can expect bus errors. This assumes
	 * that readl is actually a macro not a real function call which is
	 * the case on our ARM platforms.
	 */
	EMIT_LABEL(config_read_start);

	/* Read the dword aligned data */
	data = readl(priv->config_area + config_addr(where, is_root_bus));

	mb(); /* Barrier to force bus error to go no further than here */

	EMIT_LABEL(config_read_end);

	/* If the trap handler has fired, then set the data to 0xffffffff */
	if (atomic_read(&stm_abort_flag)) {
		ret = PCIBIOS_DEVICE_NOT_FOUND;
		data = ~0;
	}

	spin_unlock_irqrestore(&stm_abort_lock, flags);

	/*
	 * This is truly vile, but I am unable to think of anything better.
	 * This is a hack to help with when we are probing the bus.  The
	 * problem is that the wrapper logic doesn't have any way to
	 * interrogate if the configuration request failed or not. The read
	 * will return 0 if something has gone wrong.
	 *
	 * What is actually happening here is that the ST40 is defined to
	 * return 0 for a register read that causes a bus error. On the ARM
	 * we actually get a real bus error, which is what the stm_abort_flag
	 * check above is checking for.
	 *
	 * Unfortunately this means it is impossible to tell the difference
	 * between when a device doesn't exist (the switch will return a UR
	 * completion) or the device does exist but isn't yet ready to accept
	 * configuration requests (the device will return a CRS completion)
	 *
	 * The result of this is that we will miss devices when probing.
	 *
	 * So if we are trying to read the dev/vendor id on devfn 0 and we
	 * appear to get zero back, then we retry the request.  We know that
	 * zero can never be a valid device/vendor id. The specification says
	 * we must retry for up to a second before we decide the device is
	 * dead. If we are still dead then we assume there is nothing there and
	 * return ~0
	 *
	 * The downside of this is that we incur a delay of 1s for every pci
	 * express link that doesn't have a device connected.
	 */

	if (((where&~3) == 0) && devfn == 0 && (data == 0 || data == ~0)) {
		if (retry_count++ < 1000) {
			mdelay(1);
			goto retry;
		} else {
			*val = ~0;
			return PCIBIOS_DEVICE_NOT_FOUND;
		}
	}

	*val = shift_data_read(where, size, data);

	return ret;
}

static int stm_pcie_config_write(struct pci_bus *bus, unsigned int devfn,
				 int where, int size, u32 val)
{
	unsigned long flags;
	u32 bdf;
	u32 data = 0;
	int slot = PCI_SLOT(devfn);
	struct stm_pcie_dev_data *priv = stm_pci_bus_to_dev_data(bus);
	int is_root_bus = pci_is_root_bus(bus);
	int ret;

	if (!priv || (is_root_bus && slot != 1) || !link_up(priv))
		return PCIBIOS_DEVICE_NOT_FOUND;

	bdf = bdf_num(bus->number, devfn, is_root_bus);

	/* Claim lock, FUNC[01]_BDF_NUM has to remain unchanged for the whole
	 * cycle
	 */
	spin_lock_irqsave(&stm_abort_lock, flags);

	/* Set the config packet devfn */
	dbi_writel(priv, bdf, FUNC0_BDF_NUM);
	/* See comment in stm_pcie_config_read */
	dbi_readl(priv, FUNC0_BDF_NUM);

	atomic_set(&stm_abort_flag, 0);

	GET_LABEL_ADDR(config_write_start, abort_start);
	GET_LABEL_ADDR(config_write_end, abort_end);
	/* We can expect bus errors for the read and the write */
	EMIT_LABEL(config_write_start);

	/* Read the dword aligned data */
	if (size != 4)
		data = readl(priv->config_area +
			     config_addr(where, is_root_bus));
	mb();

	data = shift_data_write(where, size, val, data);

	writel(data, priv->config_area + config_addr(where, is_root_bus));

	mb();

	EMIT_LABEL(config_write_end);

	ret = atomic_read(&stm_abort_flag) ? PCIBIOS_DEVICE_NOT_FOUND
				       : PCIBIOS_SUCCESSFUL;

	spin_unlock_irqrestore(&stm_abort_lock, flags);

	return ret;
}

/* The configuration read/write functions. This is exported so
 * the arch specific code can do whatever is needed to plug it
 * into its framework
 */

static struct pci_ops stm_pcie_config_ops = {
	.read = stm_pcie_config_read,
	.write = stm_pcie_config_write,
};

static void stm_pcie_board_reset(struct device *dev)
{
	struct stm_plat_pcie_config *config = dev_get_platdata(dev);

	/* We have a valid function, so call it */
	if (config->reset) {
		config->reset();
		return;
	}
	/* We haven't got a valid gpio */
	if (!gpio_is_valid(config->reset_gpio))
		return;

	if (gpio_direction_output(config->reset_gpio, 0)) {
		dev_err(dev, "Cannot set PERST# (gpio %u) to output\n",
			config->reset_gpio);
		return;
	}

	mdelay(1);		/* From PCIe spec */
	gpio_direction_output(config->reset_gpio, 1);

	/* PCIe specification states that you should not issue any config
	 * requests until 100ms after asserting reset, so we enforce that here
	 */
	mdelay(100);
}


/* Sets up any clocking, resets the controller and disables link training */
static int __devinit stm_pcie_hw_init(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct stm_plat_pcie_config *config = dev_get_platdata(dev);
	struct stm_plat_pcie_ops *ops = config->ops;

	if (!ops)
		return -EINVAL;

	config->ops_handle = ops->init(pdev);

	ops->disable_ltssm(config->ops_handle);

	return 0;
}

static int __devinit stm_pcie_hw_setup(struct device *dev,
				       phys_addr_t lmi_window_start,
				       unsigned long lmi_window_size,
				       phys_addr_t config_window_start)
{
	struct stm_plat_pcie_config *config = dev_get_platdata(dev);
	struct stm_pcie_dev_data *priv = dev_get_drvdata(dev);
	struct stm_plat_pcie_ops *ops = config->ops;

	if (!ops)
		return -EINVAL;

	ops->disable_ltssm(config->ops_handle);

	/* Don't see any point in enabling IO here */
	dbi_writew(priv, PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER, PCI_COMMAND);

	/* Set up the config window to the top of the PCI address space */
	dbi_writel(priv, config_window_start, CFG_BASE_ADDRESS);

	/* Open up all of memory to the PCI controller. We could do slightly
	 * better than this and exclude the kernel text segment and bss etc.
	 * They are base/limit registers so can be of arbitrary alignment
	 * presumably
	 */
	dbi_writel(priv, lmi_window_start, IN0_MEM_ADDR_START);
	dbi_writel(priv, lmi_window_start + lmi_window_size - 1,
		   IN0_MEM_ADDR_LIMIT);

	/* Disable the 2nd region */
	dbi_writel(priv, ~0, IN1_MEM_ADDR_START);
	dbi_writel(priv, 0, IN1_MEM_ADDR_LIMIT);

	dbi_writel(priv, RC_PASS_ADDR_RANGE, TRANSLATION_CONTROL);

	/* This will die as we will have a common method of setting the bridge
	 * settings
	 */
	if (priv->ahb) {
		writel(config->ahb_val, priv->ahb + 4);
		/* Later versions of the AMBA bridge have a merging capability, whereby
		 * reads and writes are merged into an AHB burst transaction. This
		 * breaks PCIe as it will then prefetch a maximally sized transaction.
		 * We have to disable this capability, which is controlled by bit 3 of
		 * the SD_CONFIG register. Bit 2 (the busy bit) must always be written
		 * as zero. We set the cont_on_error bit, as this is the reset state.
		 */
		writel(0x2, priv->ahb);
	}

	/* Now assert the board level reset to the other PCIe device */
	stm_pcie_board_reset(dev);

	/* Re-enable the link */
	ops->enable_ltssm(config->ops_handle);

	return 0;
}

static int __devinit remap_named_resource(struct platform_device *pdev,
					  const char *name,
					  void __iomem **io_ptr)
{
	struct resource *res;
	resource_size_t size;
	void __iomem *p;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, name);
	if (!res)
		return -ENXIO;

	size = resource_size(res);

	if (!devm_request_mem_region(&pdev->dev,
				     res->start, size, name))
		return -EBUSY;

	p = devm_ioremap_nocache(&pdev->dev, res->start, size);
	if (!p)
		return -ENOMEM;

	*io_ptr = p;

	return 0;
}

static irqreturn_t stm_pcie_sys_err(int irq, void *dev_data)
{
	panic("PCI express serious error raised\n");
}


#ifdef CONFIG_PCI_MSI
static int __devinit stm_msi_probe(struct platform_device *pdev);
#else
static int __devinit stm_msi_probe(struct platform_device *pdev) { return 0; }
#endif

static void  stm_of_get_window(struct device_node *np, const char *name,
				phys_addr_t *start, unsigned long *size)
{
	int len;
	const __be32 *prop;

	prop = of_get_property(np, name, &len);
	if (!prop)
		return;
	len /= 4;
	*start = be32_to_cpup(prop++);
	*size = be32_to_cpup(prop++);
	return;
}
static void *stm_pcie_get_pdata(struct platform_device *pdev)
{
	struct stm_plat_pcie_config *data = dev_get_platdata(&pdev->dev);
	struct device_node *np = pdev->dev.of_node;
	if (!np)
		return data;

	if (!data)
		data  = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);

	stm_of_get_window(np, "st,pcie-window",
			&data->pcie_window.start, &data->pcie_window.size);
	stm_of_get_window(np, "st,io-window",
		&data->pcie_window.io_start, &data->pcie_window.io_size);
	stm_of_get_window(np, "st,mem-window",
		&data->pcie_window.lmi_start, &data->pcie_window.lmi_size);

	data->reset_gpio = of_get_named_gpio(np, "st,reset-gpio", 0);
	of_property_read_u32(np, "st,ahb-fixup", (u32 *)&data->ahb_val);
	of_property_read_u32(np, "st,miphy-num", &data->miphy_num);
	return data;
}
/* Probe function for PCI data When we get here, we can assume that the PCI
 * block is powered up and ready to rock, and that all sysconfigs have been
 * set correctly.
 */
static int __devinit stm_pcie_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct stm_plat_pcie_config *config;
	unsigned long config_window_start;
	struct stm_pcie_dev_data *priv;
	int err, serr_irq;

	config = stm_pcie_get_pdata(pdev);
	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	platform_set_drvdata(pdev, priv);

	err = remap_named_resource(pdev, "pcie cntrl", &priv->cntrl);
	if (err)
		return err;

	/* It is not mandatory to have from the pltfm the Amba Bus settings */
	err = remap_named_resource(pdev, "pcie ahb", &priv->ahb);
	if (err)
		dev_warn(&pdev->dev, "no amba resource available\n");

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					   "pcie config");
	if (!res)
		return -ENXIO;

	/* Check that this has sensible values */
	BUG_ON(resource_size(res) != CFG_REGION_SIZE);
	BUG_ON(res->start & (CFG_REGION_SIZE - 1));
	config_window_start = res->start;

	err = remap_named_resource(pdev, "pcie config", &priv->config_area);
	if (err)
		return err;

	serr_irq = platform_get_irq_byname(pdev, "pcie syserr");
	if (serr_irq < 0)
		return serr_irq;

	err = devm_request_irq(&pdev->dev, serr_irq, stm_pcie_sys_err,
			      IRQF_DISABLED, "pcie syserr", priv);
	if (err)
		return err;

	/* We have to initialise the PCIe cell on some hardware before we can
	 * talk to the phy
	 */
	err = stm_pcie_hw_init(pdev);
	if (err)
		return err;

	/* Now claim the associated miphy */
	priv->miphy_dev = stm_miphy_claim(config->miphy_num, PCIE_MODE,
					  &pdev->dev);
	if (!priv->miphy_dev) {
		dev_err(&pdev->dev, "Cannot claim MiPHY %d!\n",
				config->miphy_num);
		return -EBUSY;
	}

	/* Claim the GPIO for PRST# if available */
	if (!config->reset && gpio_is_valid(config->reset_gpio) &&
	    gpio_request(config->reset_gpio, "PERST#")) {
		dev_err(&pdev->dev, "Cannot request reset PIO %d\n",
				config->reset_gpio);
		config->reset_gpio = -EINVAL;
	}

	/* Now do all the register poking */
	err = stm_pcie_hw_setup(&pdev->dev, config->pcie_window.lmi_start,
				config->pcie_window.lmi_size,
				config_window_start);
	if (err)
		return err;

	/* MSI configuration */
	err = stm_msi_probe(pdev);
	if (err)
		return err;

	stm_abort_init();

	/* And now hook this into the generic driver */
	err = stm_pci_register_controller(pdev, &stm_pcie_config_ops,
					  STM_PCI_EXPRESS);
	return err;
}

static struct of_device_id stm_pcie_match[] = {
	{
		.compatible = "st,pcie",
	},
	{},
};

MODULE_DEVICE_TABLE(of, stm_pcie_match);

static struct platform_driver stm_pcie_driver = {
	.driver.name = "pcie_stm",
	.driver.owner = THIS_MODULE,
	.driver.of_match_table = of_match_ptr(stm_pcie_match),
	.probe = stm_pcie_probe,
};

static int __init stm_pcie_init(void)
{
	return platform_driver_register(&stm_pcie_driver);
}

subsys_initcall(stm_pcie_init);

#ifdef CONFIG_PCI_MSI

/* When allocating interrupts to endpoints, we have to have a lock, but
 * we can use a global one for everybody as there is not going to be
 * any contention
 */
static DEFINE_SPINLOCK(msi_alloc_lock);

struct stm_msi_info {
	void __iomem *regs;
	int first_irq, last_irq, mux_irq;
	int num_irqs_per_ep;
	int num_eps;
	int ep_allocated[MSI_NUM_ENDPOINTS];
	spinlock_t reg_lock;
};

static inline int irq_to_ep(struct stm_msi_info *msi, int irq)
{
	return (irq - msi->first_irq) / msi->num_irqs_per_ep;
}

/* Which bit in the irq, 0 means bit 0 etc */
static inline int irq_to_bitnum(struct stm_msi_info *msi, int irq)
{
	return (irq - msi->first_irq) % msi->num_irqs_per_ep;
}

static inline int irq_num(struct stm_msi_info *msi, int ep_num, int bitnum)
{
	return (ep_num * msi->num_irqs_per_ep) + bitnum + msi->first_irq;
}

static void msi_irq_demux(unsigned int mux_irq, struct irq_desc *mux_desc)
{
	int ep;
	int bitnum;
	u32 status;
	u32 mask;
	int irq;
	struct stm_msi_info *msi = irq_desc_get_handler_data(mux_desc);
	struct irq_chip __maybe_unused *chip = irq_get_chip(mux_irq);

#ifdef CONFIG_ARM
	/* These functions exist on the ARM only, chained_irq_enter/exit() are
	 * needed because the interrupts are fast_eoi rather than level. So
	 * somehow we have to call the eoi function. These arm specific
	 * functions will do this.
	 */
	chained_irq_enter(chip, mux_desc);
#endif

	/* Run down the status registers looking for which one to take.
	 * No need for any locks, we only ever read stuff here
	 */
	for (ep = 0; ep < msi->num_eps; ep++) {
		do {
			status = readl(msi->regs + MSI_INTERRUPT_STATUS(ep));
			mask = readl(msi->regs + MSI_INTERRUPT_MASK(ep));
			status &= ~mask;
			if (status) {
				/* ffs return 1 if bit zero set */
				bitnum = ffs(status) - 1 ;
				irq = irq_num(msi, ep, bitnum);
				generic_handle_irq(irq);
			}
		} while (status);
	}

#ifdef CONFIG_ARM
	chained_irq_exit(chip, mux_desc);
#endif

}

static inline void set_msi_bit(struct irq_data *data, unsigned int reg_base)
{
	struct stm_msi_info *msi = irq_data_get_irq_handler_data(data);
	int irq = data->irq;
	int ep = irq_to_ep(msi, irq);
	int val;
	unsigned long flags;

	spin_lock_irqsave(&msi->reg_lock, flags);

	val = readl(msi->regs + reg_base + MSI_OFFSET_REG(ep));
	val |= 1 << irq_to_bitnum(msi, irq);
	writel(val, msi->regs + reg_base + MSI_OFFSET_REG(ep));

	/* Read back for write posting */
	readl(msi->regs + reg_base + MSI_OFFSET_REG(ep));

	spin_unlock_irqrestore(&msi->reg_lock, flags);
}

static inline void clear_msi_bit(struct irq_data *data, unsigned int reg_base)
{
	struct stm_msi_info *msi = irq_data_get_irq_handler_data(data);
	int irq = data->irq;
	int ep = irq_to_ep(msi, irq);
	int val;
	unsigned long flags;

	spin_lock_irqsave(&msi->reg_lock, flags);

	val = readl(msi->regs + reg_base + MSI_OFFSET_REG(ep));
	val &= ~(1 << irq_to_bitnum(msi, irq));
	writel(val, msi->regs + reg_base + MSI_OFFSET_REG(ep));

	/* Read back for write posting */
	readl(msi->regs + reg_base + MSI_OFFSET_REG(ep));

	spin_unlock_irqrestore(&msi->reg_lock, flags);
}

static void stm_enable_msi_irq(struct irq_data *data)
{
	set_msi_bit(data, MSI_INTERRUPT_ENABLE(0));
	/* The generic code will have masked all interrupts for device that
	 * support the optional Mask capability. Therefore  we have to unmask
	 * interrupts on the device if the device supports the Mask capability.
	 *
	 * We do not have to this in the irq mask/unmask functions, as we can
	 * mask at the MSI interrupt controller itself.
	 */
	unmask_msi_irq(data);
}

static void stm_disable_msi_irq(struct irq_data *data)
{
	/* Disable the msi irq on the device */
	mask_msi_irq(data);

	clear_msi_bit(data, MSI_INTERRUPT_ENABLE(0));
}

static void stm_mask_msi_irq(struct irq_data *data)
{
	set_msi_bit(data, MSI_INTERRUPT_MASK(0));
}

static void stm_unmask_msi_irq(struct irq_data *data)
{
	clear_msi_bit(data, MSI_INTERRUPT_MASK(0));
}

static void stm_ack_msi_irq(struct irq_data *data)
{
	struct stm_msi_info *msi = irq_data_get_irq_handler_data(data);
	int ep = irq_to_ep(msi, data->irq);
	int val;

	/* Don't need to do a RMW cycle here, as
	 * the hardware is sane
	 */
	val = (1 << irq_to_bitnum(msi, data->irq));

	writel(val, msi->regs + MSI_INTERRUPT_STATUS(ep));
	/* Read back for write posting */
	readl(msi->regs + MSI_INTERRUPT_STATUS(ep));
}

static struct irq_chip msi_chip = {
	.name = "pcie",
	.irq_enable = stm_enable_msi_irq,
	.irq_disable = stm_disable_msi_irq,
	.irq_mask = stm_mask_msi_irq,
	.irq_unmask = stm_unmask_msi_irq,
	.irq_ack = stm_ack_msi_irq,
};


static void __devinit msi_init_one(struct stm_msi_info *msi)
{
	int ep;

	/* Set the magic address the hardware responds to. This has to be in
	 * the range the PCI controller can write to. We just use the value
	 * of the stm_msi_info data structure, but anything will do
	 */
	writel(0, msi->regs + MSI_UPPER_ADDRESS);
	writel(virt_to_phys(msi), msi->regs + MSI_ADDRESS);

	/* Disable everything to start with */
	for (ep = 0; ep < MSI_NUM_ENDPOINTS; ep++) {
		writel(0, msi->regs + MSI_INTERRUPT_ENABLE(ep));
		writel(0, msi->regs + MSI_INTERRUPT_MASK(ep));
		writel(~0, msi->regs + MSI_INTERRUPT_STATUS(ep));
	}
}


static int __devinit stm_msi_probe(struct platform_device *pdev)
{
	int irq;
	struct resource *res;
	struct stm_pcie_dev_data *priv = platform_get_drvdata(pdev);
	struct stm_msi_info *msi;
	int num_irqs;

	msi = devm_kzalloc(&pdev->dev, sizeof(*msi), GFP_KERNEL);
	if (!msi)
		return -ENOMEM;

	spin_lock_init(&msi->reg_lock);

	/* Copy over the register pointer for convenience */
	msi->regs = priv->cntrl;

	res = platform_get_resource_byname(pdev, IORESOURCE_IRQ, "msi mux");
	if (!res)
		return -ENODEV;

	msi->mux_irq = res->start;

	/*
	 * We ask for 8 irqs in total. Since we don't support
	 * arch_setup_msi_irqs(), there is not a lot of point in allocating
	 * more than one irq per ep.  If we can't get that number, we keep
	 * halving until we can get something. The code is robust enough to
	 * work even if we only manage to get one irq, although obviously more
	 * is better.
	 *
	 * Perhaps this parameter should be tunable, or keyed on SPARSE_IRQ?
	 *
	 * The code starts searching from the mux irq rather than zero. This is
	 * because on the ARM there are some free interrupt slots below 32.
	 * There is nothing wrong with using them, but it causes confusion. If
	 * you have two PCI buses present, then the interrupts will appear
	 * below 32 for one bus and at high numbers for the other. It is also
	 * a bad idea to have irqs starting at zero, this will cause problems
	 * since some drivers will assume zero cannot be a valid irq.
	 */
	for (num_irqs = 8; num_irqs > 0; num_irqs /= 2) {
		irq = irq_alloc_descs(-1, msi->mux_irq, num_irqs, 0);
		if (irq >= 0)
			break;
	}

	if (irq < 0) {
		dev_err(&pdev->dev, "Unable to allocate any irqs for MSI\n");
		return irq;
	}

	msi->first_irq = irq;
	msi->num_eps = min(num_irqs, MSI_NUM_ENDPOINTS);
	msi->num_irqs_per_ep = rounddown_pow_of_two(num_irqs / msi->num_eps);

	/* Maximum of 32 irqs per endpoint */
	if (msi->num_irqs_per_ep > 32)
		msi->num_irqs_per_ep = 32;

	num_irqs = msi->num_irqs_per_ep * msi->num_eps;
	msi->last_irq = msi->first_irq + num_irqs - 1;

	msi_init_one(msi);

	/* Hook up the multiplexor */
	irq_set_chained_handler(msi->mux_irq, msi_irq_demux);
	irq_set_handler_data(msi->mux_irq, msi);

	for (irq = msi->first_irq; irq <= msi->last_irq; irq++) {
		irq_set_chip_and_handler_name(irq, &msi_chip,
					      handle_level_irq, "msi");
		irq_set_handler_data(irq, msi);
#ifdef CONFIG_ARM
		/* Horrible, why is the ARM it's own universe? */
		set_irq_flags(irq, IRQF_VALID);
#endif
	}

	/* Set the private data, arch_setup_msi_irq() will always fail
	 * if we haven't got as far as this
	 */
	priv->msi = msi;

	return 0;
}

int arch_setup_msi_irq(struct pci_dev *dev, struct msi_desc *desc)
{
	int ep;
	struct msi_msg msg;
	int irq;
	struct stm_pcie_dev_data *priv;
	struct stm_msi_info *msi;

	/* It is legitimate for this function to  be called with a non-express
	 * bus device, as it is a global function. This check ensures that it
	 * will fail in this case, as the stm_pci_bus_to_dev_data() checks
	 * that it is being called on an express bus and will return NULL
	 * otherwise
	 */
	priv = stm_pci_bus_to_dev_data(dev->bus);
	if (!priv)
		return -ENXIO;

	msi = priv->msi;
	if (!msi)
		return -ENXIO;

	/* We only support one interrupt, although the hardware can do 32.
	 * The reason is that we currently do not implement
	 * arch_setup_msi_irqs() (plural) and things will go wrong if allocate
	 * more than one interrupt here. If there is a need we can implement
	 * it in the future.
	 */
	spin_lock(&msi_alloc_lock);

	/* Now we have to find a free EP to use */
	for (ep = 0; ep < msi->num_eps; ep++)
		if (!msi->ep_allocated[ep])
			break;

	/* Hell, they are all in use. We are stuffed */
	if (ep == msi->num_eps) {
		spin_unlock(&msi_alloc_lock);
		return -ENOSPC;
	}

	irq = irq_num(msi, ep, 0);

	msi->ep_allocated[ep] = irq;

	spin_unlock(&msi_alloc_lock);

	/* Set up the data the card needs in order to raise
	 * interrupt. The format of the data word is
	 * [7:5] Selects endpoint register
	 * [4:0] Interrupt number to raise
	 */
	msg.data = (ep << 5);
	msg.address_hi = 0;
	msg.address_lo = virt_to_phys(msi);

	/* We have to update this, it will be written back to the chip by
	 * write_msi_msg()
	 */
	desc->msi_attrib.multiple = 0;

	irq_set_msi_desc(irq, desc);
	write_msi_msg(irq, &msg);

	return 0;
}

void arch_teardown_msi_irq(unsigned int irq)
{
	struct stm_msi_info *msi = irq_get_handler_data(irq);
	int ep = irq_to_ep(msi, irq);

	spin_lock(&msi_alloc_lock);

	msi->ep_allocated[ep] = 0;

	spin_unlock(&msi_alloc_lock);
}

#endif /* CONFIG_PCI_MSI */
