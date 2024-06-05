/*
 *  ------------------------------------------------------------------------
 *  stm_nand_emi.c STMicroelectronics NAND Flash driver: "EMI bit-banging"
 *  ------------------------------------------------------------------------
 *
 *  Copyright (c) 2008-2009 STMicroelectronics Limited
 *  Author: Angus Clark <Angus.Clark@st.com>
 *
 *  ------------------------------------------------------------------------
 *  May be copied or modified under the terms of the GNU General Public
 *  License Version 2.0 only.  See linux/COPYING for more information.
 *  ------------------------------------------------------------------------
 *
 *  Changelog:
 *      2009-03-09 Angus Clark <Angus.Clark@st.com>
 *              - moved EMI configuration from SoC setup to device probe
 *              - updated timing specification
 *	2008-02-19 Angus Clark <Angus.Clark@st.com>
 *		- Added FDMA support for Data IO
 *	2008-03-04 Angus Clark <Angus.Clark@st.com>
 *		- Support for Data IO though cache line to minimise impact
 *                of STBus latency
 */

#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/completion.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/dma-mapping.h>
#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/semaphore.h>
#include <linux/stm/dma.h>
#include <linux/stm/emi.h>
#include <linux/stm/platform.h>
#include <linux/stm/nand.h>
#include <asm/cacheflush.h>
#include <asm/dma.h>

#include "stm_nand_bbt.h"
#include "stm_nand_dt.h"

#define NAME	"stm-nand-emi"

/*
 * Private data for stm_emi_nand driver.  Concurrency and device locking
 * handled by MTD layers.
 */
struct stm_nand_emi {
	struct nand_chip	chip;
	struct mtd_info		mtd;

	struct clk		*clk;
	unsigned int		emi_bank;
	unsigned int		emi_base;
	unsigned int		emi_size;

	int			rbn_gpio;	/* Ready not busy pin         */

	void __iomem		*io_base;	/* EMI base for NAND chip     */
	void __iomem		*io_data;	/* Address for data IO        */
						/*        (possibly cached)   */
	void __iomem		*io_cmd;	/* CMD output (emi_addr(17))  */
	void __iomem		*io_addr;	/* ADDR output (emi_addr(17)) */

	spinlock_t		lock; /* save/restore IRQ flags */
	int			nr_parts;	/* Partition Table	      */
	struct mtd_partition	*parts;

#ifdef CONFIG_STM_NAND_EMI_FDMA
	unsigned long		nand_phys_addr;
	unsigned long		init_fdma_jiffies;	/* Rate limit init    */
	struct dma_chan		*dma_chan;		/* FDMA channel       */
	struct completion	dma_rd_comp;		/* FDMA read done     */
	struct completion	dma_wr_comp;		/* FDMA write done    */
#endif
};

/*
 * A group of NAND devices which hang off a single platform device, and
 * share some hardware (normally a single Ready/notBusy signal).
 */
struct stm_nand_emi_group {
	int rbn_gpio;
	int nr_banks;
	struct stm_nand_emi *banks[0];
};

/*
 * Routines for FDMA transfers.
 */
#if defined(CONFIG_STM_NAND_EMI_FDMA)
static bool fdma_filter_fn(struct dma_chan *chan, void *fn_param)
{
	/* Grant the first channel we come to */
	return true;
}

static int init_fdma_nand(struct stm_nand_emi *data)
{
	struct dma_slave_config config;
	dma_cap_mask_t mask;

	/* Set the DMA channel capabilities we want */
	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);

	/* Request a matching DMA channel for NAND transactions */
	data->dma_chan = dma_request_channel(mask, fdma_filter_fn, NULL);
	if (!data->dma_chan) {
		printk(KERN_ERR NAME ": dma_request_channel failed!\n");
		return -EBUSY;
	}

	/* Set configuration (free-running only needs direction & FIFO addr) */
	config.direction = DMA_DEV_TO_MEM;
	config.src_addr = data->nand_phys_addr;

	if (dmaengine_slave_config(data->dma_chan, &config)) {
		printk(KERN_ERR NAME ": dmaengine_slave_config failed!\n");
		dma_release_channel(data->dma_chan);
		return -EBUSY;
	}

	/* Initiallise semaphores for read/write to wait on */
	init_completion(&data->dma_rd_comp);
	init_completion(&data->dma_wr_comp);

	printk(KERN_INFO NAME ": %s assigned %s(%d)\n",
	       data->mtd.name, dev_name(data->dma_chan->device->dev),
	       data->dma_chan->chan_id);

	return 0;
}

/* Ratelimit attempts to initialise FDMA */
static int init_fdma_nand_ratelimit(struct stm_nand_emi *data)
{
	if (printk_timed_ratelimit(&data->init_fdma_jiffies,  500))
		return init_fdma_nand(data);
	return -EBUSY;
}

static void exit_fdma_nand(struct stm_nand_emi *data)
{
	/* Release DMA channel */
	if (data->dma_chan)
		dma_release_channel(data->dma_chan);
}

static void nand_callback_dma(void *param)
{
	complete(param);
}

/*
 * Setup FDMA transfer. Add 'oob' to list, if present.  Assumes FDMA channel
 * has been initialised, and data areas are suitably aligned.
 */
static int nand_read_dma(struct mtd_info *mtd, uint8_t *buf, int buf_len,
			      uint8_t *oob, int oob_len)
{
	struct nand_chip *chip = mtd->priv;
	struct stm_nand_emi *data = chip->priv;
	struct scatterlist sg[2];
	struct dma_async_tx_descriptor *dma_desc;

	/* Map DMA addresses */
	sg_init_table(sg, 2);
	sg_dma_address(&sg[0]) = dma_map_single(NULL, buf, buf_len,
						DMA_FROM_DEVICE);
	sg_dma_len(&sg[0]) = buf_len;

	/* Are we doing data+oob transfer? */
	if (oob) {
		sg_dma_address(&sg[1]) = dma_map_single(NULL, oob, oob_len,
						DMA_FROM_DEVICE);
		sg_dma_len(&sg[1]) = oob_len;
	}

	/* Prepare the DMA transfer (on completion buffers will be unmapped) */
	dma_desc = data->dma_chan->device->device_prep_slave_sg(
			data->dma_chan, sg, oob ? 2 : 1, DMA_DEV_TO_MEM,
			DMA_CTRL_ACK | DMA_COMPL_SKIP_SRC_UNMAP |
			DMA_COMPL_DEST_UNMAP_SINGLE, NULL);

	dma_desc->callback = nand_callback_dma;
	dma_desc->callback_param = &data->dma_rd_comp;

	/* Submit the DMA transfer */
	(void) dma_desc->tx_submit(dma_desc);

	/* Wait for completion... */
	wait_for_completion(&data->dma_rd_comp);

	return 0;
}

/*
 * Setup FDMA transfer. Add 'oob' to list, if present.  Assumes FDMA channel
 * has been initialised, and data areas are suitably aligned.
 */
static int nand_write_dma(struct mtd_info *mtd, const uint8_t *buf, int buf_len,
			  uint8_t *oob, int oob_len)
{
	struct nand_chip *chip = mtd->priv;
	struct stm_nand_emi *data = chip->priv;
	struct scatterlist sg[2];
	struct dma_async_tx_descriptor *dma_desc;

	/* Map DMA addresses */
	sg_init_table(sg, 2);
	sg_dma_address(&sg[0]) = dma_map_single(NULL, (void *) buf, buf_len,
						DMA_TO_DEVICE);
	sg_dma_len(&sg[0]) = buf_len;

	/* Are we doing data+oob linked transfer? */
	if (oob) {
		sg_dma_address(&sg[1]) = dma_map_single(NULL, oob, oob_len,
						DMA_TO_DEVICE);
		sg_dma_len(&sg[1]) = oob_len;
	}

	/* Prepare the DMA transfer (on completion buffers will be unmapped) */
	dma_desc = data->dma_chan->device->device_prep_slave_sg(
			data->dma_chan, sg, oob ? 2 : 1, DMA_MEM_TO_DEV,
			DMA_CTRL_ACK | DMA_COMPL_SRC_UNMAP_SINGLE |
			DMA_COMPL_SKIP_DEST_UNMAP, NULL);

	dma_desc->callback = nand_callback_dma;
	dma_desc->callback_param = &data->dma_wr_comp;

	/* Submit the DMA transfer */
	(void) dma_desc->tx_submit(dma_desc);

	/* Wait for completion... */
	wait_for_completion(&data->dma_wr_comp);

	return 0;
}

/*
 * Write buf to NAND chip.  Attempt DMA write for 'large' bufs.  Fall-back to
 * writesl for small bufs and if FDMA fails to initialise.
 */
static void nand_write_buf_dma(struct mtd_info *mtd,
			       const uint8_t *buf, int len)
{
	struct nand_chip *chip = mtd->priv;
	struct stm_nand_emi *data = chip->priv;
	unsigned long dma_align = dma_get_cache_alignment() - 1UL;
	int i;

	if (len >= 512 &&
	    virt_addr_valid(buf) &&
	    (data->dma_chan || init_fdma_nand_ratelimit(data) == 0)) {

		/* Read up to cache line boundary */
		while ((unsigned long)buf & dma_align) {
			writeb(*buf++, chip->IO_ADDR_W);
			len--;
		}

		/* Do DMA transfer, fall-back to writesl if fail */
		if (nand_write_dma(mtd, buf, len & ~dma_align, NULL, 0) != 0)
			writesl(chip->IO_ADDR_W, buf, (len & ~dma_align)/4);

		/* Mop up trailing bytes */
		for (i = (len & ~dma_align); i < len; i++)
			writeb(buf[i], chip->IO_ADDR_W);
	} else {
		/* write buf up to 4-byte boundary */
		while ((unsigned int)buf & 0x3) {
			writeb(*buf++, chip->IO_ADDR_W);
			len--;
		}

		writesl(chip->IO_ADDR_W, buf, len/4);

		/* mop up trailing bytes */
		for (i = (len & ~0x3); i < len; i++)
			writeb(buf[i], chip->IO_ADDR_W);
	}
}

/*
 * Read NAND chip to buf.  Attempt DMA read for 'large' bufs.  Fall-back to
 * readsl for small bufs and if FDMA fails to initialise.
 */
static void nand_read_buf_dma(struct mtd_info *mtd, uint8_t *buf, int len)
{
	struct nand_chip *chip = mtd->priv;
	struct stm_nand_emi *data = chip->priv;
	unsigned long dma_align = dma_get_cache_alignment() - 1UL;
	int i;

	if (len >= 512 &&
	    virt_addr_valid(buf) &&
	    (data->dma_chan || init_fdma_nand_ratelimit(data) == 0)) {

		/* Read up to cache-line boundary */
		while ((unsigned long)buf & dma_align) {
			*buf++ = readb(chip->IO_ADDR_R);
			len--;
		}

		/* Do DMA transfer, fall-back to readsl if fail */
		if (nand_read_dma(mtd, buf, len & ~dma_align, NULL, 0) != 0)
			readsl(chip->IO_ADDR_R, buf, (len & ~dma_align)/4);

		/* Mop up trailing bytes */
		for (i = (len & ~dma_align); i < len; i++)
			buf[i] = readb(chip->IO_ADDR_R);
	} else {

		/* Read buf up to 4-byte boundary */
		while ((unsigned int)buf & 0x3) {
			*buf++ = readb(chip->IO_ADDR_R);
			len--;
		}

		readsl(chip->IO_ADDR_R, buf, len/4);

		/* Mop up trailing bytes */
		for (i = (len & ~0x3); i < len; i++)
			buf[i] = readb(chip->IO_ADDR_R);
	}
}
#endif /* CONFIG_STM_NAND_EMI_FDMA */

#if defined(CONFIG_STM_NAND_EMI_LONGSL)
static void nand_readsl_buf(struct mtd_info *mtd, uint8_t *buf, int len)
{
	int i;
	struct nand_chip *chip = mtd->priv;

	/* read buf up to 4-byte boundary */
	while ((unsigned int)buf & 0x3) {
		*buf++ = readb(chip->IO_ADDR_R);
		len--;
	}

	readsl(chip->IO_ADDR_R, buf, len/4);

	/* mop up trailing bytes */
	for (i = (len & ~0x3); i < len; i++)
		buf[i] = readb(chip->IO_ADDR_R);
}
#endif /* CONFIG_STM_NAND_EMI_LONGSL */

#if defined(CONFIG_STM_NAND_EMI_LONGSL) || defined(CONFIG_STM_NAND_EMI_CACHED)
static void nand_writesl_buf(struct mtd_info *mtd, const uint8_t *buf, int len)
{
	int i;
	struct nand_chip *chip = mtd->priv;

	/* write buf up to 4-byte boundary */
	while ((unsigned int)buf & 0x3) {
		writeb(*buf++, chip->IO_ADDR_W);
		len--;
	}

	writesl(chip->IO_ADDR_W, buf, len/4);

	/* mop up trailing bytes */
	for (i = (len & ~0x3); i < len; i++)
		writeb(buf[i], chip->IO_ADDR_W);
}
#endif

#if defined(CONFIG_STM_NAND_EMI_CACHED)
static void nand_read_buf_cached(struct mtd_info *mtd, uint8_t *buf, int len)
{
	struct nand_chip *chip = mtd->priv;
	struct stm_nand_emi *data = chip->priv;
	unsigned long irq_flags;

	int lenaligned = len & ~(L1_CACHE_BYTES-1);
	int lenleft = len & (L1_CACHE_BYTES-1);

	while (lenaligned > 0) {
		spin_lock_irqsave(&(data->lock), irq_flags);
		invalidate_ioremap_region(data->emi_base,
					  data->io_data, 0, L1_CACHE_BYTES);
		memcpy_fromio(buf, data->io_data, L1_CACHE_BYTES);
		spin_unlock_irqrestore(&(data->lock), irq_flags);

		buf += L1_CACHE_BYTES;
		lenaligned -= L1_CACHE_BYTES;
	}

#ifdef CONFIG_STM_L2_CACHE
	/* If L2 cache is enabled, we must ensure the cacheline is evicted prior
	 * to non-cached accesses..
	 */
	invalidate_ioremap_region(data->emi_base,
				  data->io_data, 0, L1_CACHE_BYTES);
#endif
	/* Mop up any remaining bytes */
	while (lenleft > 0) {
		*buf++ = readb(chip->IO_ADDR_R);
		lenleft--;
	}
}
#endif /* CONFIG_STM_NAND_EMI_CACHED */

/* Command control function for EMI 'bit-banging'.  AL and CL signals are
 * toggled via EMI address lines emi_addr_17, emi_addr_18.
 */
static void nand_cmd_ctrl_emi(struct mtd_info *mtd, int cmd, unsigned int ctrl)
{
	struct nand_chip *this = mtd->priv;
	struct stm_nand_emi *data = this->priv;

	if (ctrl & NAND_CTRL_CHANGE) {
		if (ctrl & NAND_CLE) {
			this->IO_ADDR_W = data->io_cmd;
		} else if (ctrl & NAND_ALE) {
			this->IO_ADDR_W = data->io_addr;
		} else {
			this->IO_ADDR_W = data->io_base;
		}
	}

	if (cmd != NAND_CMD_NONE)
		writeb(cmd, this->IO_ADDR_W);
}

/* Device ready functio, for boards where nand_rbn is available via GPIO pin */
static int nand_device_ready(struct mtd_info *mtd)
{
	struct nand_chip *this = mtd->priv;
	struct stm_nand_emi *data = this->priv;

	return gpio_get_value(data->rbn_gpio);
}

#define GET_CLK_CYCLES(X, T)	(((X) + (T) - 1) / (T))

/* Configure EMI Bank according to 'stm_nand_timing_data'
 *
 * [DEPRECATED in favour of nand_config_emi() based on 'struct nand_timing_spec'
 * data.]
 */
static void nand_config_emi_legacy(struct clk *clk,
	int bank, struct stm_nand_timing_data *td)
{
	uint32_t emi_t_ns;
	unsigned long config[4];
	uint32_t rd_cycle, wr_cycle;
	uint32_t iord_start, iord_end;
	uint32_t iowr_start, iowr_end;
	uint32_t rd_latch;
	uint32_t bus_release;
	uint32_t wait_active_low;

	printk(KERN_INFO NAME ": Configuring EMI Bank %d for NAND access\n",
	       bank);

	if (!td) {
		printk(KERN_ERR NAME "No timing data specified in platform "
		       "data\n");
		return;
	}

	if (!clk) {
		pr_warning(NAME
			": No EMI clock available. Using default 100MHz.\n");
		emi_t_ns = 10;
	} else
		emi_t_ns = 1000000000UL / clk_get_rate(clk);

	/* Convert nand timings to EMI compatible values */
	rd_cycle = GET_CLK_CYCLES(td->rd_on + td->rd_off, emi_t_ns) + 3;
	iord_start = 0;
	iord_end = GET_CLK_CYCLES(td->rd_on, emi_t_ns) + 2;
	rd_latch = GET_CLK_CYCLES(td->rd_on, emi_t_ns) + 2;
	bus_release = GET_CLK_CYCLES(50, emi_t_ns);
	wait_active_low = 0;
	wr_cycle = GET_CLK_CYCLES(td->wr_on + td->wr_off, emi_t_ns) + 3;
	iowr_start = 0;
	iowr_end = GET_CLK_CYCLES(td->wr_on, emi_t_ns) + 2;

	/* Set up EMI configuration data */
	config[0] = 0x04000699 |
		((bus_release & 0xf) << 11) |
		((rd_latch & 0x1f) << 20) |
		((wait_active_low & 0x1) << 25);

	config[1] =
		((rd_cycle & 0x7f) << 24) |
		((iord_start & 0xf) << 12) |
		((iord_end & 0xf) << 8);

	config[2] =
		((wr_cycle & 0x7f) << 24) |
		((iowr_start & 0xf) << 12) |
		((iowr_end & 0xf) << 8);

	config[3] = 0x00;

	pr_debug("EMI Configuration Data: {0x%08x, 0x%08x, 0x%08x, 0x%08x}\n",
		 (unsigned int)config[0], (unsigned int)config[1],
		 (unsigned int)config[2], (unsigned int)config[3]);

	/* Configure Bank */
	emi_bank_configure(bank, config);

	/* Disable PC mode */
	emi_config_pcmode(bank, 0);
}

/* Configure EMI Bank according to 'nand_timing_spec' */
static void nand_config_emi(struct clk *clk, int bank,
	struct nand_timing_spec *spec, int relax)
{
	int tCLK;
	unsigned long config[4];
	uint32_t rd_cycle, rd_oee1, rd_oee2, rd_latch;
	uint32_t wr_cycle, wr_wee1, wr_wee2;
	uint32_t bus_release;
	uint32_t tMAX_SETUP, tMAX_HOLD;

	printk(KERN_INFO NAME ": Configuring EMI Bank %d for NAND access\n",
	       bank);

	/* Get EMI clock (default 100MHz) */
	if (!clk) {
		pr_warning(NAME
		       ": No EMI clock available. Assuming default 100MHz\n");
		tCLK = 10;
	} else
		tCLK = 1000000000 / clk_get_rate(clk);

	rd_cycle = (spec->tRC + tCLK - 1)/tCLK + 1 + relax;
	rd_oee1 = 0;
	rd_oee2 = (spec->tREH + tCLK - 1)/tCLK + relax;
	rd_latch = (spec->tREH + tCLK - 1)/tCLK + relax;

	bus_release = (spec->tCHZ + tCLK - 1)/tCLK + relax;

	tMAX_SETUP = spec->tCLS;
	if (spec->tCS > tMAX_SETUP)
		tMAX_SETUP = spec->tCS;
	if (spec->tALS > tMAX_SETUP)
		tMAX_SETUP = spec->tALS;
	if (spec->tDS > tMAX_SETUP)
		tMAX_SETUP = spec->tDS;
	if (spec->tWP > tMAX_SETUP)
		tMAX_SETUP = spec->tWP;

	tMAX_HOLD = spec->tCLH;
	if (spec->tCH > tMAX_HOLD)
		tMAX_HOLD = spec->tCH;
	if (spec->tALH > tMAX_HOLD)
		tMAX_HOLD = spec->tALH;
	if (spec->tDH > tMAX_HOLD)
		tMAX_HOLD = spec->tDH;
	if (spec->tWH > tMAX_HOLD)
		tMAX_HOLD = spec->tWH;

	if (spec->tWC > (tMAX_SETUP + tMAX_HOLD))
		wr_cycle = (spec->tWC + tCLK - 1)/tCLK + 1 + relax;
	else
		wr_cycle = (tMAX_SETUP + tMAX_HOLD + tCLK - 1)/tCLK + 1 + relax;
	wr_wee1 = 0;
	wr_wee2 = (tMAX_HOLD + tCLK - 1)/tCLK + relax;

	config[0] = (EMI_CFG0_WE_USE_OE_CFG |
		     EMI_CFG0_LATCH_POINT(rd_latch) |
		     EMI_CFG0_BUS_RELEASE(bus_release) |
		     EMI_CFG0_CS_ACTIVE(ACTIVE_CODE_RDWR) |
		     EMI_CFG0_OE_ACTIVE(ACTIVE_CODE_RD) |
		     EMI_CFG0_BE_ACTIVE(ACTIVE_CODE_OFF) |
		     EMI_CFG0_PORTSIZE_8BIT |
		     EMI_CFG0_DEVICE_NORMAL);

	config[1] = (EMI_CFG1_READ_CYCLESNOTPHASE |
		     EMI_CFG1_READ_CYCLES(rd_cycle) |
		     EMI_CFG1_READ_OEE1(rd_oee1) |
		     EMI_CFG1_READ_OEE2(rd_oee2));

	config[2] = (EMI_CFG2_WRITE_CYCLESNOTPHASE |
		     EMI_CFG2_WRITE_CYCLES(wr_cycle) |
		     EMI_CFG2_WRITE_OEE1(wr_wee1) |
		     EMI_CFG2_WRITE_OEE2(wr_wee2));

	config[3] = 0;

	pr_debug("EMI Configuration Data: {0x%08x, 0x%08x, 0x%08x, 0x%08x}\n",
		 (unsigned int)config[0], (unsigned int)config[1],
		 (unsigned int)config[2], (unsigned int)config[3]);

	/* Configure Bank */
	emi_bank_configure(bank, config);

	/* Disable PC mode */
	emi_config_pcmode(bank, 0);
}

/*
 * Probe for the NAND device.
 */
static struct stm_nand_emi * __init nand_probe_bank(
	struct stm_nand_bank_data *bank, int bank_nr, int rbn_gpio,
	struct device *dev)
{
	struct stm_nand_emi *data;
	struct mtd_part_parser_data ppdata;
	/* Default EMI config data, for device probing */
	unsigned long emi_cfg_probe[] = {
		0x04402e99,
		0x0a000400,
		0x0a000400,
		0x00000000};
	int res = 0;

	if (dev->of_node)
		ppdata.of_node = stm_of_get_partitions_node(
						dev->of_node, bank_nr);

	/* Allocate memory for the driver structure (and zero it) */
	data = kzalloc(sizeof(struct stm_nand_emi), GFP_KERNEL);
	if (!data) {
		printk(KERN_ERR NAME
		       ": Failed to allocate device structure.\n");
		return ERR_PTR(-ENOMEM);
	}

	/* Get EMI clk */
	data->clk = clk_get(dev, "emi_clk");
	if (!data->clk || IS_ERR(data->clk)) {
		pr_warning(NAME ": Failed to find EMI clock. ");
		data->clk = NULL;
	} else if (clk_prepare_enable(data->clk)) {
		pr_warning(NAME ": Failed to enable EMI clock. ");
		clk_put(data->clk);
		data->clk = NULL;
	}

	/* Get EMI Bank base address */
	data->emi_bank = bank->csn;
	data->emi_base = emi_bank_base(data->emi_bank) +
		bank->emi_withinbankoffset;
	data->emi_size = (1 << 18) + 1;

	/* Configure EMI Bank for device probe */
	emi_bank_configure(data->emi_bank, emi_cfg_probe);
	emi_config_pcmode(data->emi_bank, 0);

	/* Request IO Memory */
	if (!request_mem_region(data->emi_base, data->emi_size, NAME)) {
		printk(KERN_ERR NAME ": Request mem 0x%x region failed\n",
		       data->emi_base);
		res = -ENODEV;
		goto out1;
	}

	/* Map base address */
	data->io_base = ioremap_nocache(data->emi_base, 4096);
	if (!data->io_base) {
		printk(KERN_ERR NAME ": ioremap failed for io_base 0x%08x\n",
		       data->emi_base);
		res = -ENODEV;
		goto out2;
	}

#ifdef CONFIG_STM_NAND_EMI_CACHED
	/* Map data address through cache line */
	data->io_data = ioremap_cache(data->emi_base, 4096);
	if (!data->io_data) {
		printk(KERN_ERR NAME ": ioremap failed for io_data 0x%08x\n",
		       data->emi_base);
		res = -ENOMEM;
		goto out3;
	}
#else
	data->io_data = data->io_base;
#endif
	/* Map cmd and addr addresses (emi_addr_17 and emi_addr_18) */
	data->io_cmd = ioremap_nocache(data->emi_base | (1 << 17), 1);
	if (!data->io_cmd) {
		printk(KERN_ERR NAME ": ioremap failed for io_cmd 0x%08x\n",
		       data->emi_base | (1 << 17));
		res = -ENOMEM;
		goto out4;
	}

	data->io_addr = ioremap_nocache(data->emi_base | (1 << 18), 1);
	if (!data->io_addr) {
		printk(KERN_ERR NAME ": ioremap failed for io_addr 0x%08x\n",
		       data->emi_base | (1 << 18));
		res = -ENOMEM;
		goto out5;
	}

	data->chip.priv = data;
	data->mtd.priv = &data->chip;
	data->mtd.owner = THIS_MODULE;
	data->mtd.dev.parent = dev;

	/* Assign more sensible name (default is string from nand_ids.c!) */
	data->mtd.name = dev_name(dev);

	data->chip.IO_ADDR_R = data->io_base;
	data->chip.IO_ADDR_W = data->io_base;
	data->rbn_gpio = -1;
	data->chip.chip_delay = 50;
	data->chip.cmd_ctrl = nand_cmd_ctrl_emi;

	/* Do we have access to NAND_RBn? */
	if (gpio_is_valid(rbn_gpio)) {
		data->rbn_gpio = rbn_gpio;
		data->chip.dev_ready = nand_device_ready;
	}

	/* Set IO routines for acessing NAND pages */
#if defined(CONFIG_STM_NAND_EMI_FDMA)
	data->chip.read_buf = nand_read_buf_dma;
	data->chip.write_buf = nand_write_buf_dma;
	data->dma_chan = NULL;
	data->init_fdma_jiffies = 0;
	init_fdma_nand_ratelimit(data);
	data->nand_phys_addr = data->emi_base;

#elif defined(CONFIG_STM_NAND_EMI_LONGSL)
	data->chip.read_buf = nand_readsl_buf;
	data->chip.write_buf = nand_writesl_buf;

#elif defined(CONFIG_STM_NAND_EMI_CACHED)
	data->chip.read_buf = nand_read_buf_cached;
	data->chip.write_buf = nand_writesl_buf;

#elif defined(CONFIG_STM_NAND_EMI_BYTE)
	/* Default byte orientated routines */
#else
#error "Must specify CONFIG_STM_NAND_EMI_xxxx mode"
#endif

	data->chip.ecc.mode = NAND_ECC_SOFT;

	/* Copy chip options from platform data */
	data->chip.options = bank->options;
	data->chip.bbt_options = bank->bbt_options;

	data->chip.scan_bbt = stmnand_scan_bbt;

	/* Scan to find existence of device */
	if (nand_scan_ident(&data->mtd, 1, NULL) != 0) {
		res = -ENODEV;
		goto out6;
	}

	/*
	 * Configure timing registers
	 */
	if (bank->timing_spec) {
		printk(KERN_INFO NAME ": Using platform timing data\n");
		nand_config_emi(data->clk,
			data->emi_bank, bank->timing_spec,
				bank->timing_relax);
		data->chip.chip_delay = bank->timing_spec->tR;
	} else if (bank->timing_data) {
		printk(KERN_INFO NAME ": Using legacy platform timing data\n");
		nand_config_emi_legacy(data->clk,
			data->emi_bank, bank->timing_data);
		data->chip.chip_delay = bank->timing_data->chip_delay;
	} else if (data->chip.onfi_version) {
		struct nand_onfi_params *onfi = &data->chip.onfi_params;
		int mode;

		mode = fls(le16_to_cpu(onfi->async_timing_mode)) - 1;
		/* Modes 4 and 5 (EDO) are not supported on our H/W */
		if (mode > 3)
			mode = 3;

		printk(KERN_INFO NAME ": Using ONFI Timing Mode %d\n", mode);
		nand_config_emi(data->clk,
				data->emi_bank, &nand_onfi_timing_specs[mode],
				bank->timing_relax);
		data->chip.chip_delay = le16_to_cpu(data->chip.onfi_params.t_r);
	} else {
		printk(KERN_WARNING NAME ": No timing data available\n");
	}

	/* Complete scan */
	if (nand_scan_tail(&data->mtd) != 0) {
		res = -ENXIO;
		goto out6;
	}

	/* If all blocks are marked bad, mount as "recovery" partition */
	if (stmnand_blocks_all_bad(&data->mtd)) {
		printk(KERN_ERR NAME ": initiating NAND Recovery Mode\n");
		data->mtd.name = "NAND RECOVERY MODE";
		res = mtd_device_register(&data->mtd, NULL, 0);
		if (res)
			goto out6;

		return data;
	}

	res = mtd_device_parse_register(&data->mtd,
			NULL, &ppdata,
			bank->partitions, bank->nr_partitions);
	if (!res)
		return data;

	/* Release resources on error */
 out6:

	nand_release(&data->mtd);
	iounmap(data->io_addr);
 out5:
	iounmap(data->io_cmd);
 out4:
#ifdef CONFIG_STM_NAND_EMI_CACHED
	iounmap(data->io_data);
 out3:
#endif
	iounmap(data->io_base);
 out2:
	release_mem_region(data->emi_base, data->emi_size);
 out1:
	kfree(data);
	return ERR_PTR(res);
}

static void nand_remove_bank(struct stm_nand_emi *emi)
{
	nand_release(&emi->mtd);

	iounmap(emi->io_addr);
	iounmap(emi->io_cmd);
#ifdef CONFIG_STM_NAND_EMI_CACHED
	iounmap(emi->io_data);
#endif
	if (emi->clk)
		clk_disable_unprepare(emi->clk);

	iounmap(emi->io_base);
	release_mem_region(emi->emi_base, emi->emi_size);
#ifdef CONFIG_STM_NAND_EMI_FDMA
	exit_fdma_nand(emi);
#endif
	kfree(emi);
}

#ifdef CONFIG_OF
static void *stm_emi_dt_get_pdata(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct stm_plat_nand_emi_data *data;
	if (!np)
		return NULL;

	data = kzalloc(sizeof(*data), GFP_KERNEL);

	data->emi_rbn_gpio = of_get_named_gpio(np, "st,emi-rbn-gpio", 0);
	data->nr_banks = stm_of_get_nand_banks(&pdev->dev, np, &data->banks);
	return data;
}
#else
static void *stm_emi_dt_get_pdata(struct platform_device *pdev)
{
	return NULL;
}
#endif

static int __devinit stm_nand_emi_probe(struct platform_device *pdev)
{
	struct stm_plat_nand_emi_data *pdata ;
	struct stm_nand_emi_group *group;
	struct stm_nand_emi *emi;
	int err;
	int n;
	int rbn_gpio;

	if (pdev->dev.of_node)
		pdev->dev.platform_data = stm_emi_dt_get_pdata(pdev);

	pdata = pdev->dev.platform_data;
	group = kzalloc(sizeof(struct stm_nand_emi_group) +
			(sizeof(struct stm_nand_emi *) * pdata->nr_banks),
			GFP_KERNEL);
	if (!group)
		return -ENOMEM;

	rbn_gpio = pdata->emi_rbn_gpio;
	if (gpio_is_valid(rbn_gpio)) {
		err = gpio_request(rbn_gpio, "nand_RBn");
		if (err == 0) {
			gpio_direction_input(rbn_gpio);
		} else {
			dev_err(&pdev->dev, "nand_rbn unavailable. "
				"Falling back to chip_delay\n");
			rbn_gpio = -1;
		}
	}

	group->rbn_gpio = rbn_gpio;
	group->nr_banks = pdata->nr_banks;

	for (n = 0; n < pdata->nr_banks; n++) {
		emi = nand_probe_bank(&pdata->banks[n], n, rbn_gpio,
			&pdev->dev);

		if (IS_ERR(emi)) {
			err = PTR_ERR(emi);
			goto err1;
		}

		group->banks[n] = emi;
	}

	platform_set_drvdata(pdev, group);

	return 0;

 err1:
	while (--n > 0)
		nand_remove_bank(group->banks[n]);

	if (gpio_is_valid(group->rbn_gpio))
		gpio_free(group->rbn_gpio);

	platform_set_drvdata(pdev, NULL);
	kfree(group);

	return err;
}

/*
 * Remove a NAND device.
 */
static int __devexit stm_nand_emi_remove(struct platform_device *pdev)
{
	struct stm_nand_emi_group *group = platform_get_drvdata(pdev);
	int n;

	for (n = 0; n < group->nr_banks; n++)
		nand_remove_bank(group->banks[n]);

	if (gpio_is_valid(group->rbn_gpio))
		gpio_free(group->rbn_gpio);

	platform_set_drvdata(pdev, NULL);
	kfree(group);

	return 0;
}

#ifdef CONFIG_OF
static struct of_device_id nand_emi_match[] = {
	{
		.compatible = "st,nand-emi",
	},
	{},
};

MODULE_DEVICE_TABLE(of, nand_emi_match);
#endif

#ifdef CONFIG_PM
static int stm_nand_emi_suspend(struct device *dev)
{
	struct stm_nand_emi_group *group = dev_get_drvdata(dev);
	int n;

	for (n = 0; n < group->nr_banks; n++)
		if (group->banks[n]->clk)
			clk_disable_unprepare(group->banks[n]->clk);

	return 0;
}

static int stm_nand_emi_resume(struct device *dev)
{
	struct stm_nand_emi_group *group = dev_get_drvdata(dev);
	int n;

	for (n = 0; n < group->nr_banks; n++)
		if (group->banks[n]->clk)
			clk_prepare_enable(group->banks[n]->clk);
	return 0;
}

static const struct dev_pm_ops stm_nand_emi_pm_ops = {
	.suspend = stm_nand_emi_suspend,
	.resume = stm_nand_emi_resume,
};
#else
static const struct dev_pm_ops stm_nand_emi_pm_ops;
#endif

static struct platform_driver plat_nand_driver = {
	.probe		= stm_nand_emi_probe,
	.remove		= stm_nand_emi_remove,
	.driver		= {
		.name	= NAME,
		.owner	= THIS_MODULE,
		.pm	= &stm_nand_emi_pm_ops,
		.of_match_table = of_match_ptr(nand_emi_match),
	},
};

static int __init stm_nand_emi_init(void)
{
	return platform_driver_register(&plat_nand_driver);
}

static void __exit stm_nand_emi_exit(void)
{
	platform_driver_unregister(&plat_nand_driver);
}

module_init(stm_nand_emi_init);
module_exit(stm_nand_emi_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Angus Clark");
MODULE_DESCRIPTION("STMicroelectronics NAND driver: 'EMI bit-banging'");
