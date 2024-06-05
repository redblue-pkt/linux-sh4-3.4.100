/* System Trace Module Platform driver.
 *
 * (c) 2012 STMicroelectronics
 *
 * This driver implements the initialization of the Systrace
 * peripheral for B20xx boards. The functionnal driver is
 * part of the MTT infrastructure
 *
 * see Documentation/mtt.txt and systrace.txt for details.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/io.h>
#include <linux/mm.h>
#include <mach/hardware.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/mtt/mtt.h>
#include <linux/stm/pad.h>
#include <linux/stm/systrace.h>
#include <linux/mtt/mtt.h>
#include <linux/debugfs.h>
#include <linux/stm/sysconf.h>
#include <linux/platform_device.h>

#ifdef CONFIG_OF
#include <linux/of_device.h>
#include <linux/of_platform.h>
#endif

MODULE_AUTHOR("Marc Titinger <marc.titinger@st.com>");
MODULE_DESCRIPTION("System Trace Modules driver.");
MODULE_LICENSE("GPL");

#ifndef __raw_writeq
#define __raw_writeq(v, a) \
	(__chk_io_ptr(a), *(volatile uint64_t __force *)(a) = (v))
#endif

struct stm_plat_systrace_data {
	struct stm_pad_config *pad_config;
	struct stm_pad_state *pad_state;
	struct resource *mem_base;
	struct resource *mem_regs;
	void __iomem *base;		/* Base address for STM trace entries */
	void __iomem *regs;		/* Base address for STM registers     */

	/*store the ptr to a per-instance structure for MTT. */
	void *private;

	/* Very basic channel pool mgt for the trace API.
	 * Will implement a bitmap if any support request,
	 */
	unsigned int last_ch_ker;
};

/**
 * Send buffer to STM Channel entry
 * Buffer contents are sent as 64bits data
 * Padding added at the end for buffer lengths not multiple of 64bits
 * Last word is written to STM entry that generates a STP "TS" event
 */
static inline void stm_write_buf(void *chan_addr, char *str, unsigned int len)
{
	register char *ptr = str;
	unsigned long long tmp = 0;

	void __iomem *dst = chan_addr;

	while (len > 8) {
		__raw_writeq(*(uint64_t *) ptr, dst);
		ptr += 8;
		len -= 8;
	}

	if (len == 8) {
		__raw_writeq(*(uint64_t *) ptr, (dst + STM_IP_TS_OFFSET));
		return;
	};
	memcpy(&tmp, ptr, len);
	__raw_writeq(tmp, (dst + STM_IP_TS_OFFSET));
}

/**
 * Map a STM-Channel-Address to a trace component
 * Called on mtt_open
 * fixup the co->private field to speedup STM access.
 * \param comp_id [in] The trace component for which a channel address
 *                     has to be found in the STM-IP
 * \param data [in] The handler
 * \return the address to use for this component in the STM-IP to send trace
 */
static void *mtt_drv_stm_comp_alloc(const uint32_t comp_id, void *data)
{
	struct stm_plat_systrace_data *drv_data =
	    (struct stm_plat_systrace_data *)data;

	/* drv_data->base is the base address for STM trace entries */
	unsigned int ret = (unsigned int)drv_data->base;

	if (comp_id & MTT_COMPID_ST) {
		/*
		 * Map a channel address to this component,
		 * using the core and id fields of component_id
		 *  33222222222211111111110000000000
		 *  10987654321098765432109876543210
		 *  ................MMMM....CCCCCCCC
		 */

		/* Convert coreID field of Component-ID to channel a offset */
		ret += (((comp_id & MTT_COMPID_CHMSK)
		    >> MTT_COMPID_CHSHIFT) << STM_IP_CH_SHIFT);

		/* Convert traceID field of Component-ID to channel a offset */
		ret += (((comp_id & MTT_COMPID_MAMSK)
		    >> MTT_COMPID_MASHIFT) << STM_IP_MA_SHIFT);
		return (void *)ret;
	}

	/* If we allocated everything, mux-down
	 *   co->private will be the pre-computed channel offset;
	 *   the core ID will be known only at runtime.
	 */
	if (drv_data->last_ch_ker == MTT_CH_LIN_KER_INVALID) {
		ret += (MTT_CH_LIN_KMUX << STM_IP_CH_SHIFT);
		return (void *)ret;
	}

	/* Some room remaning on output channels
	 *   co->private will be the pre-computed channel offset;
	 *   the core ID will be known only at runtime.
	 */
	ret += (drv_data->last_ch_ker << STM_IP_CH_SHIFT);
	drv_data->last_ch_ker++;
	return (void *)ret;
};

static int mtt_drv_stm_mmap(struct file *filp, struct vm_area_struct *vma,
			    void *data)
{
	struct stm_plat_systrace_data *drv_data =
	    (struct stm_plat_systrace_data *)data;

	u64 off = (u64) (vma->vm_pgoff) << PAGE_SHIFT;
	u64 physical = drv_data->mem_base->start + off;
	u64 vsize = (vma->vm_end - vma->vm_start);
	u64 psize = (drv_data->mem_base->end - drv_data->mem_base->start) - off;

	/* Only map up to the aperture we have. */
	if (vsize > psize)
		vsize = psize;

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	vma->vm_ops = NULL;

	mtt_printk(KERN_DEBUG "mtt_drv_stm_mmap: " \
		   "vm_start=%lx, vm_end=%lx, vm_pgoff=%lx, "
		   "physical=%llx (size=%llx)\n",
		   vma->vm_start, vma->vm_end, vma->vm_pgoff, physical, vsize);

	if (remap_pfn_range(vma, vma->vm_start, physical >> PAGE_SHIFT,
			    vsize, vma->vm_page_prot)) {
		printk(KERN_ERR " Failure returning from stm_mmap\n");
		return -EAGAIN;
	}
	return 0;
}

/* Fast write routine, offsets are prefetched in the component's
 * private field.
 */
static void mtt_drv_stm_write(mtt_packet_t *p, int lock)
{
	struct mtt_component_obj *co = (struct mtt_component_obj *)p->comp;

	/* Kptrace and printks have the 'ST' bit set
	 * so, they already did set the core offset in the
	 * component ID (see mtt_drv_stm_comp_alloc).
	 *
	 * API calls need to append the core offset.
	 * We store a component ID, that
	 */
	if (unlikely(((unsigned int)(co->private) & MTT_COMPID_ST) == 0))
		co->private = (void *)
		  ((unsigned int)(co->private) +
		   (raw_smp_processor_id() << STM_IP_MA_SHIFT));

	/* When lock is DRVLOCK, we already made sure that we have a
	 * dedicated channel and hence we do not need to manage locking here.
	 */
	stm_write_buf(co->private, p->u.buf, p->length);
}

static int systrace_remove(struct platform_device *pdev)
{
	struct stm_plat_systrace_data *drv_data;

	drv_data = (struct stm_plat_systrace_data *)pdev->dev.platform_data;

	if (drv_data->private) {
		mtt_unregister_output_driver(drv_data->private);
		kfree(drv_data->private);
	}

	return 0;
}

#ifdef CONFIG_OF
static struct of_device_id stm_systrace_match[] = {
	{
		.compatible = "st,systrace",
	},
	{},
};
MODULE_DEVICE_TABLE(of, stm_systrace_match);

static void *stm_systrace_of_get_pdata(struct platform_device *pdev)
{
	struct stm_plat_systrace_data *data;
	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data) {
		dev_err(&pdev->dev, "Unable to allocate platform data\n");
		return ERR_PTR(-ENOMEM);
	}

	data->pad_config = stm_of_get_pad_config(&pdev->dev);
	return data;
}
#else
static void *stm_systrace_of_get_pdata(struct platform_device *pdev)
{
	return NULL;
}
#endif

/**
 * Check STP IP version
 */
static void systrace_of_phyver(struct device_node *topnode,
			       struct stm_plat_systrace_data *drv_data)
{
	uint32_t stm_pid;
	uint32_t stm_cid;
	stm_pid =
		((readl(drv_data->regs + STM_IP_PID3_OFF)) << 24) +
		((readl(drv_data->regs + STM_IP_PID2_OFF)) << 16) +
		((readl(drv_data->regs + STM_IP_PID1_OFF)) << 8) +
		((readl(drv_data->regs + STM_IP_PID0_OFF))) ;
	stm_cid =
		((readl(drv_data->regs + STM_IP_CID3_OFF)) << 24) +
		((readl(drv_data->regs + STM_IP_CID2_OFF)) << 16) +
		((readl(drv_data->regs + STM_IP_CID1_OFF)) << 8) +
		((readl(drv_data->regs + STM_IP_CID0_OFF))) ;
	printk(KERN_NOTICE "Systrace: STM IP revision is V%d\n",
				STM_IP_VERSION);
	printk(KERN_NOTICE "Systrace: STM_PID=0x%08x(0x%08x)\n",
				stm_pid, STM_PERI_ID);
	printk(KERN_NOTICE "Systrace: STM_CID=0x%08x(0x%08x)\n",
				stm_cid, STM_CELL_ID);
}

/**
 * Default STM IP configuration registers setting
 */
static void systrace_stmip_default_cfg(struct stm_plat_systrace_data *drv_data)
{
#if defined(CONFIG_MACH_STM_STIH407) || defined(CONFIG_MACH_STM_STID127)
	/* STM3 configuration */
	__raw_writel(0x0200, drv_data->regs + STM_IPv3_CR_OFF);
	__raw_writel(0x0000, drv_data->regs + STM_IPv3_MCR_OFF);
	__raw_writel(0x03ff, drv_data->regs + STM_IPv3_TER_OFF);
	__raw_writel(0x0001, drv_data->regs + STM_IPv3_FTR_OFF);
	__raw_writel(0x0001, drv_data->regs + STM_IPv3_CTR_OFF);
#else	/* STM1 configuration: H415 / H416 */
	__raw_writel(0x00C0, drv_data->regs + STM_IPv1_CR_OFF);
	__raw_writel(0x0000, drv_data->regs + STM_IPv1_MMC_OFF);
	__raw_writel(0x023d, drv_data->regs + STM_IPv1_TER_OFF);
#endif
}

/**
 * Configure the STM IP registers using the device tree
 * Device tree is the ".dtsi" file in "arch/arm/boot/dts/"
 * Device tree structure is as follows:
 * systrace-phy-config {
 *	stm_reg0 { nam="STM_CR";  ofs=<0x0000>; val=<0x0600>; };
 *	stm_reg1 { nam="STM_TER"; ofs=<0x0010>; val=<0x03ff>; };
 *      etc.
 * Node keywords are "systrace-phy-config", "stm_reg<num>"
 * Parameter keywords are "nam" "ofs" "val"
 * Return 0 if Nodes are found in the tree
 */
static int systrace_of_phyconf(struct device_node *topnode,
			       struct stm_plat_systrace_data *drv_data)
#ifdef CONFIG_OF
{
	unsigned int          num_sysc = 0;
	struct device_node   *cnfbank;
	struct device_node   *cnfregs;

	cnfbank = of_get_child_by_name(topnode, "systrace-phy-config");
	if (cnfbank == NULL) {
		systrace_stmip_default_cfg(drv_data);
		return -1;
	}
	do {
		size_t                phyconfofs;
		uint32_t              phyconfval;
		const char           *phyregname;
		char                  bufname[16];
		uint32_t              temp;

		snprintf(bufname, 16, "stm_reg%d", num_sysc);
		cnfregs = of_get_child_by_name(cnfbank, bufname);
		if (cnfregs == NULL)
			return -1;

		of_property_read_string(cnfregs, "nam", &phyregname);
		of_property_read_u32(cnfregs, "ofs", &phyconfofs);
		of_property_read_u32(cnfregs, "val", &phyconfval);

		temp = (uint32_t)readl(drv_data->regs + phyconfofs);
		__raw_writel(phyconfval, drv_data->regs + phyconfofs);

		printk(KERN_NOTICE "Systrace: %s[0x%04x] = 0x%08x (prev. 0x%08x)\n",
		    phyregname,
		    (uint32_t)(phyconfofs),
		    (uint32_t)readl(drv_data->regs + phyconfofs),
		    temp);
		num_sysc++;
	} while (cnfregs != NULL);
	return 0;
}
#else
{
	systrace_stmip_default_cfg(drv_data);
	return 0;
}
#endif

/**
 * Configure the Sysconf registers using the device tree
 * Device tree is the ".dtsi" file in "arch/arm/boot/dts/"
 * Device tree structure is as follows:
 * systrace-sys-config {
 *	r_sysconfs {
 *          sysconf0 = <&sysconf 5190 0 31>;
 *          sysconf1 = <&sysconf 5191 0 31>;
 *          etc.
 *      };
 *      v_sysconfs {
 *          sysconf0 = <0x003f0000>;
 *          sysconf1 = <0x00580058>;
 *          etc.
 *      };
 * };
 * Node keywords are "systrace-sys-config", "r_sysconfs", "v_sysconfs"
 * Parameter keywords are "sysconf<num>" for sysconfs and values to set
 * Return 0 if Nodes are found in the tree
 */
static int systrace_of_sysconf(struct device_node *topnode)
#ifdef CONFIG_OF
{
	char bufname[16];
	unsigned int          num_sysc = 0;
	struct device_node   *cnfbank;
	struct device_node   *cnfregs;
	struct device_node   *cnfvals;
	struct sysconf_field *sysconfreg;

	cnfbank = of_get_child_by_name(topnode, "systrace-sys-config");
	if (cnfbank == NULL)
		return -1;
	cnfregs = of_get_child_by_name(cnfbank, "r_sysconfs");
	if (cnfregs == NULL)
		return -1;
	cnfvals = of_get_child_by_name(cnfbank, "v_sysconfs");
	if (cnfvals == NULL)
		return -1;

	do {
		uint32_t sysconfval;
		uint32_t sysconfvalo;
		snprintf(bufname, 16, "sysconf%d", num_sysc);

		sysconfreg = stm_of_sysconf_claim(cnfregs, bufname);
		if (sysconfreg == NULL)
			return -1;

		of_property_read_u32(cnfvals, bufname, &sysconfval);
		sysconfvalo = sysconf_read(sysconfreg);
		sysconf_write(sysconfreg, sysconfval);
		sysconfval = sysconf_read(sysconfreg);
		printk(KERN_NOTICE "Systrace: %s=0x%08x (prev: 0x%08x)\n",
			bufname, sysconfval, sysconfvalo);
		num_sysc++;
	} while (sysconfreg != NULL);
	return 0;
}
#else
{
/**
 * Configure required Sysconf registers to route Cores on initiators
 * Required only for H407
 */
#ifdef CONFIG_MACH_STM_STIH407
	void __iomem *add   = NULL;
	/* uint32_t               temp  = 0; */
	uint32_t               tempo = 0;
	add   = ioremap_nocache(0x092B02F8, 28);
	/* temp  = readl(add+24); */
	tempo = 0x03ff03ff;
	writel(tempo, add+24);
	/* printk(KERN_NOTICE "Systrace: 0x%08x --> 0x%08x\n", temp, tempo); */
	iounmap(add);
#endif
	return 0;
}
#endif

/**
 * Send some blank data to the STM FIFOs to flush them and the probe
 */
static void systrace_flush(struct stm_plat_systrace_data *drv_data)
{
	int iloop = 0;
	for (iloop = 0; iloop < 20; iloop++) {
		void __iomem *base = drv_data->base +
			(iloop*STM_IP_CH_OFFSET) + STM_IP_TS_OFFSET;
		__raw_writeq(0x0LL, base);
	}
}

#ifdef SYSTRACE_DRV_DEBUG
static void systrace_test_pattern(struct stm_plat_systrace_data *drv_data)
{
	unsigned long long value_to_write64[8];
	unsigned long     *value_to_write  =
				(unsigned long *)value_to_write64;
	char              *value_to_string =
				(unsigned char *)(&(value_to_write[5]));
	unsigned int ux = 0;
	unsigned int ix = 0;
	value_to_write[ix++] = 0xeee00300; /* Synchro */
	value_to_write[ix++] = 0x00000000; /* Attributes */
	/*printk("0x%016llx\n",value_to_write64[ux++]);*/
	value_to_write[ix++] = 0x00000000; /* ComponentID */
	value_to_write[ix++] = 0x00000000; /* TraceID */
	/*printk("0x%016llx\n",value_to_write64[ux++]);*/
	value_to_write[ix++] = 0x01020018; /* Array of chars */
	value_to_write[ix++] = 0x70747473;
	/*printk("0x%016llx\n",value_to_write64[ux++]);*/
	value_to_write[ix++] = 0x5453203A;
	value_to_write[ix++] = 0x4e49204d;
	/*printk("0x%016llx\n",value_to_write64[ux++]);*/
	value_to_write[ix++] = 0x41495449;
	value_to_write[ix++] = 0x455A494C;
	/*printk("0x%016llx\n",value_to_write64[ux++]);*/
	value_to_write[ix++] = 0x00000044;
	value_to_write[ix++] = 0x0;
	/*printk("0x%016llx\n",value_to_write64[ux++]);*/

	stm_write_buf(drv_data->base, (char *)value_to_write, ix*4);

	for (ux = 0; ux < (256*STM_IP_CH_OFFSET); ux += STM_IP_CH_OFFSET) {
		sprintf(value_to_string, "Hello World 1 from  %3d ",
			ux/0x100);
		stm_write_buf(drv_data->base+ux, (char *)value_to_write,
			ix*4);
		sprintf(value_to_string, "Hello World 2 from  %3d ",
			ux/0x100);
		stm_write_buf(drv_data->base+ux, (char *)value_to_write,
			ix*4);
		sprintf(value_to_string, "Hello World 3 from  %3d ",
			ux/0x100);
		stm_write_buf(drv_data->base+ux, (char *)value_to_write,
			ix*4);
		sprintf(value_to_string, "Hello World 4 from  %3d ",
			ux/0x100);
		stm_write_buf(drv_data->base+ux, (char *)value_to_write,
			ix*4);
	}
}
#endif

static int systrace_probe(struct platform_device *pdev)
{
	struct mtt_output_driver *mtt_drv;
	struct stm_plat_systrace_data *drv_data;
	struct resource *res;
	int err = 0;

	if (pdev->dev.of_node)
		drv_data = stm_systrace_of_get_pdata(pdev);
	else
		drv_data = pdev->dev.platform_data;

	if (!drv_data || IS_ERR(drv_data)) {
		dev_err(&pdev->dev, "No platform data found\n");
		return -ENODEV;
	}

	/* Get STM Channel memory area */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	drv_data->base = devm_request_and_ioremap(&pdev->dev, res);
	if (!drv_data->base) {
		err = -EADDRNOTAVAIL;
		goto _failed_exit;
	};
	/* remember for mmap */
	drv_data->mem_base = res;

	/* Get STM registers area */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	drv_data->regs = devm_request_and_ioremap(&pdev->dev, res);
	if (!drv_data->regs) {
		err = -EADDRNOTAVAIL;
		goto _failed_exit;
	};
	drv_data->mem_regs = res;

	/* Claim the Pads */
	drv_data->pad_state = devm_stm_pad_claim(&pdev->dev,
						drv_data->pad_config,
						dev_name(&pdev->dev));
	if (!drv_data->pad_state) {
		err = -EIO;
		goto _failed_exit;
	}

	/* Configure sysconf (if any) */
	systrace_of_sysconf(pdev->dev.of_node);

	/* Configure STM registers */
	systrace_of_phyconf(pdev->dev.of_node, drv_data);

	/* Check STM IP version */
	systrace_of_phyver(pdev->dev.of_node, drv_data);

	printk(KERN_NOTICE "Systrace: driver probed.\n");

	/* Allocate an MTT output descriptor for this instance */
	mtt_drv = kzalloc(sizeof(struct mtt_output_driver), GFP_KERNEL);
	if (!mtt_drv) {
		err = -ENOMEM;
		devm_stm_pad_release(&pdev->dev, drv_data->pad_state);
		goto _failed_exit;
	}

	/* MTT layer output medium declaration structure. */
	mtt_drv->write_func = mtt_drv_stm_write;
	mtt_drv->mmap_func = mtt_drv_stm_mmap;
	mtt_drv->comp_alloc_func = mtt_drv_stm_comp_alloc;
	mtt_drv->guid = MTT_DRV_GUID_STM;
	mtt_drv->private = drv_data;
	mtt_drv->last_error = 0;
	mtt_drv->devid = STM_IP_VERSION;
	drv_data->last_ch_ker = MTT_CH_LIN_KER_FIRST;
	drv_data->private = mtt_drv;

	/* Register the MTT output device */
	mtt_register_output_driver(mtt_drv);

	/* Flush dummy data / send an async in case of STM3 */
	/* Not mandatory */
	systrace_flush(drv_data);

	/* This is only for test */
	/* systrace_test_pattern(drv_data);*/

	return 0;

_failed_exit:
	dev_err(&pdev->dev, "Systrace: driver probing failed!");
	return err;
}

static struct platform_driver systrace_driver = {
	.probe = systrace_probe,
	.remove = systrace_remove,
	.driver = {
		.owner = THIS_MODULE,
		.name = "stm-systrace",
		.of_match_table = of_match_ptr(stm_systrace_match),
	},
};

/* Platform driver init and exit. */
int __init systrace_init(void)
{
	static char banner[] =
	    KERN_INFO "Systrace: platform driver registered.\n";

	printk(banner);

	return platform_driver_register(&systrace_driver);
}

void __exit systrace_exit(void)
{
	platform_driver_unregister(&systrace_driver);
}

module_init(systrace_init);
module_exit(systrace_exit);
