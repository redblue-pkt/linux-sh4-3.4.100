/*
 * (c) 2011 STMicroelectronics Limited
 *
 * Author: Stuart Menefy <stuart.menefy@st.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/ata_platform.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/stm/emi.h>
#include <linux/stm/device.h>
#include <linux/stm/sysconf.h>
#include <linux/stm/stxh205.h>
#include <asm/irq-ilc.h>
#include "pio-control.h"

/* PIO ports resources ---------------------------------------------------- */

#define STXH205_PIO_ENTRY(_num, _base, _irq)				\
	[_num] = {							\
		.name = "stm-gpio",					\
		.id = _num,						\
		.num_resources = 2,					\
		.resource = (struct resource[]) {			\
			STM_PLAT_RESOURCE_MEM(_base, 0x100),		\
			STM_PLAT_RESOURCE_IRQ(ILC_IRQ(_irq), -1),	\
		},							\
	}

static struct platform_device stxh205_pio_devices[16] = {
	/* PIO_SBC: 0-3 */
	STXH205_PIO_ENTRY(0,  0xfe610000, 129),
	STXH205_PIO_ENTRY(1,  0xfe611000, 130),
	STXH205_PIO_ENTRY(2,  0xfe612000, 131),
	STXH205_PIO_ENTRY(3,  0xfe613000, 132),

	/* PIO_BANK_1_0: 4-8 */
	STXH205_PIO_ENTRY(4,  0xfda60000, 133),
	STXH205_PIO_ENTRY(5,  0xfda61000, 134),
	STXH205_PIO_ENTRY(6,  0xfda62000, 135),
	STXH205_PIO_ENTRY(7,  0xfda63000, 136),
	STXH205_PIO_ENTRY(8,  0xfda64000, 137),

	/* PIO_BANK_1_1: 9-12 */
	STXH205_PIO_ENTRY(9,  0xfda70000, 138),
	STXH205_PIO_ENTRY(10, 0xfda71000, 139),
	STXH205_PIO_ENTRY(11, 0xfda72000, 140),
	STXH205_PIO_ENTRY(12, 0xfda73000, 141),

	/* PIO_BANK_2: 13-15 */
	STXH205_PIO_ENTRY(13, 0xfde20000, 142),
	STXH205_PIO_ENTRY(14, 0xfde21000, 143),
	STXH205_PIO_ENTRY(15, 0xfde22000, 144),
};

static const struct stm_pio_control_retime_offset stxh205_pio_retime_offset = {
	.clk1notclk0_offset	= 0,
	.delay_lsb_offset	= 2,
	.delay_msb_offset	= 3,
	.invertclk_offset	= 4,
	.retime_offset		= 5,
	.clknotdata_offset	= 6,
	.double_edge_offset	= 7,
};

static unsigned int stxh205_pio_control_delays_in[] = {
	0,	/* 00: 0.0ns */
	500,	/* 01: 0.5ns */
	1000,	/* 10: 1.0ns */
	1500,	/* 11: 1.5ns */
};

static unsigned int stxh205_pio_control_delays_out[] = {
	0,	/* 00: 0.0ns */
	1000,	/* 01: 1.0ns */
	2000,	/* 10: 2.0ns */
	3000,	/* 11: 3.0ns */
};

static const struct stm_pio_control_retime_params stxh205_retime_params = {
	.retime_offset = &stxh205_pio_retime_offset,
	.delay_times_in = stxh205_pio_control_delays_in,
	.num_delay_times_in = ARRAY_SIZE(stxh205_pio_control_delays_in),
	.delay_times_out = stxh205_pio_control_delays_out,
	.num_delay_times_out = ARRAY_SIZE(stxh205_pio_control_delays_out),
};

#define STXH205_PIO_ENTRY_CONTROL(_num, _alt_num,			\
		_oe_num, _pu_num, _od_num, _lsb, _msb,			\
		_rt)							\
	[_num] = {							\
		.alt = { SYSCONF(_alt_num) },				\
		.oe = { SYSCONF(_oe_num), _lsb, _msb },			\
		.pu = { SYSCONF(_pu_num), _lsb, _msb },			\
		.od = { SYSCONF(_od_num), _lsb, _msb },			\
		.retime_style = stm_pio_control_retime_style_packed,	\
		.retime_pin_mask = 0xff,				\
		.retime_params = &stxh205_retime_params,		\
		.retiming = {						\
			{ SYSCONF(_rt) },				\
			{ SYSCONF(_rt+1) },				\
		},							\
	}

#define STXH205_PIO_ENTRY_CONTROL4(_num, _alt_num,		\
		_oe_num, _pu_num, _od_num, _rt)			\
	STXH205_PIO_ENTRY_CONTROL(_num,   _alt_num,		\
		_oe_num, _pu_num, _od_num,  0,  7,		\
		_rt),						\
	STXH205_PIO_ENTRY_CONTROL(_num+1, _alt_num+1,		\
		_oe_num, _pu_num, _od_num,  8, 15,		\
		_rt+2),						\
	STXH205_PIO_ENTRY_CONTROL(_num+2, _alt_num+2,		\
		_oe_num, _pu_num, _od_num, 16, 23,		\
		_rt+4),						\
	STXH205_PIO_ENTRY_CONTROL(_num+3, _alt_num+3,		\
		_oe_num, _pu_num, _od_num, 24, 31,		\
		_rt+6)

static const struct stm_pio_control_config stxh205_pio_control_configs[16] = {
	/*                        pio, alt,  oe,  pu,  od,lsb,msb, rt */
	/* 0-3: SBC */
	STXH205_PIO_ENTRY_CONTROL4(0,    0,   4,   5,   6,           7),
	/* 4-12: BANK 1 */
	STXH205_PIO_ENTRY_CONTROL4(4,  100, 109, 112, 115,         118),
	STXH205_PIO_ENTRY_CONTROL4(8,  104, 110, 113, 116,         126),
	STXH205_PIO_ENTRY_CONTROL(12,  108, 111, 114, 117,  0,  7, 134),
	/* 13-15: BANK 2 */
	STXH205_PIO_ENTRY_CONTROL(13,  200, 203, 204, 205,  0,  7, 206),
	STXH205_PIO_ENTRY_CONTROL(14,  201, 203, 204, 205,  8, 15, 208),
	STXH205_PIO_ENTRY_CONTROL(15,  202, 203, 204, 205, 16, 23, 210),
};

static struct stm_pio_control stxh205_pio_controls[16];

static int stxh205_pio_config(unsigned gpio,
		enum stm_pad_gpio_direction direction, int function, void *priv)
{
	struct stm_pio_control_pad_config *config = priv;

	return stm_pio_control_config_all(gpio, direction, function, config,
			stxh205_pio_controls,
			ARRAY_SIZE(stxh205_pio_devices), 7);
}

#ifdef CONFIG_DEBUG_FS
static void stxh205_pio_report(unsigned gpio, char *buf, int len)
{
	stm_pio_control_report_all(gpio, stxh205_pio_controls,
			buf, len);
}
#else
#define stxh205_pio_report NULL
#endif

static const struct stm_pad_ops stxh205_pad_ops = {
	.gpio_config = stxh205_pio_config,
	.gpio_report = stxh205_pio_report,
};

static void __init stxh205_pio_init(void)
{
	stm_pio_control_init(stxh205_pio_control_configs, stxh205_pio_controls,
			     ARRAY_SIZE(stxh205_pio_control_configs));
}

/* sysconf resources ------------------------------------------------------ */

static int stxh205_sysconf_reg_name(char *name, int size, int group, int num)
{
	if (group >= 3)
		group++;
	return snprintf(name, size, "SYSCONF%d", (group * 100) + num);
}

static struct platform_device stxh205_sysconf_devices[] = {
	{
		/* SBC system configuration bank 0 registers */
		/* SYSCFG_BANK0 (aka SYSCFG_SBC, Sapphire): 0-42 */
		.name		= "stm-sysconf",
		.id		= 0,
		.num_resources	= 1,
		.resource	= (struct resource[]) {
			STM_PLAT_RESOURCE_MEM(0xfe600000, 0xac),
		},
		.dev.platform_data = &(struct stm_plat_sysconf_data) {
			.groups_num = 1,
			.groups = (struct stm_plat_sysconf_group []) {
				{
					.group = 0,
					.offset = 0,
					.name = "SYSCFG_SBC",
					.reg_name = stxh205_sysconf_reg_name,
				}
			},
		}
	}, {
		/* SYSCFG_BANK1 (aka Coral): 100-176 */
		.name		= "stm-sysconf",
		.id		= 1,
		.num_resources	= 1,
		.resource	= (struct resource[]) {
			STM_PLAT_RESOURCE_MEM(0xfda50000, 0x134),
		},
		.dev.platform_data = &(struct stm_plat_sysconf_data) {
			.groups_num = 1,
			.groups = (struct stm_plat_sysconf_group []) {
				{
					.group = 1,
					.offset = 0,
					.name = "SYSCFG_BANK1",
					.reg_name = stxh205_sysconf_reg_name,
				}
			},
		}
	}, {
		/* SYSCFG_BANK2 (aka Perl): 200-243 */
		.name		= "stm-sysconf",
		.id		= 2,
		.num_resources	= 1,
		.resource	= (struct resource[]) {
			STM_PLAT_RESOURCE_MEM(0xfd500000, 0xb0),
		},
		.dev.platform_data = &(struct stm_plat_sysconf_data) {
			.groups_num = 1,
			.groups = (struct stm_plat_sysconf_group []) {
				{
					.group = 2,
					.offset = 0,
					.name = "SYSCFG_BANK2",
					.reg_name = stxh205_sysconf_reg_name,
				}
			},
		}
	}, {
		/* SYSCFG_BANK3 (aka Opal): 400-510 */
		.name		= "stm-sysconf",
		.id		= 3,
		.num_resources	= 1,
		.resource	= (struct resource[]) {
			STM_PLAT_RESOURCE_MEM(0xfd541000, 0x1bc),
		},
		.dev.platform_data = &(struct stm_plat_sysconf_data) {
			.groups_num = 1,
			.groups = (struct stm_plat_sysconf_group []) {
				{
					.group = 3,
					.offset = 0,
					.name = "SYSCFG_BANK3",
					.reg_name = stxh205_sysconf_reg_name,
				}
			},
		}
	}, {
		/* LPM Configuration registers */
		.name		= "stm-sysconf",
		.id		= 4,
		.num_resources	= 1,
		.resource	= (struct resource[]) {
			STM_PLAT_RESOURCE_MEM(0xfe4b5100, 0x54),
		},
		.dev.platform_data = &(struct stm_plat_sysconf_data) {
			.groups_num = 1,
			.groups = (struct stm_plat_sysconf_group []) {
				{
					.group = 4,
					.offset = 0,
					.name = "LPM_CFG",
				}
			},
		}
	},
};



/* Early initialisation-----------------------------------------------------*/

/* Initialise devices which are required early in the boot process. */
void __init stxh205_early_device_init(void)
{
	struct sysconf_field *sc;
	unsigned long devid;
	unsigned long chip_revision;
	const char* variant;

	/* Initialise PIO and sysconf drivers */

	sysconf_early_init(stxh205_sysconf_devices,
			ARRAY_SIZE(stxh205_sysconf_devices));
	stxh205_pio_init();
	stm_gpio_early_init(stxh205_pio_devices,
			ARRAY_SIZE(stxh205_pio_devices),
			ILC_FIRST_IRQ + ILC_NR_IRQS);
	stm_pad_init(ARRAY_SIZE(stxh205_pio_devices) * STM_GPIO_PINS_PER_PORT,
		     0, 0, &stxh205_pad_ops);

	sc = sysconf_claim(SYSCONF(41), 0, 31, "devid");
	devid = sysconf_read(sc);
	chip_revision = (devid >> 28) + 1;
	boot_cpu_data.cut_major = chip_revision;
	switch ((devid >> 12) & 0x3ff) {
	case 0x04a:
		/* STxH205 (Lille): devid 0x0d44a041 */
		variant = "205";
		break;
	case 0x057:
		/* STxH273 (Palma 2): devid 0x0d457041 */
		boot_cpu_data.type = CPU_STXH273;
		variant = "273";
		break;
	case 0x58:
		/* STxH237 (Cardiff 2): devid 0x0d458041 */
		boot_cpu_data.type = CPU_STXH237;
		variant = "237";
		break;
	default:
		printk(KERN_WARNING "Unknown STxH205 variant: %08lx\n", devid);
		variant = "???";
		break;
	}

	printk(KERN_INFO "STxH%s version %ld.x\n", variant, chip_revision);

	/* We haven't configured the LPC, so the sleep instruction may
	 * do bad things. Thus we disable it here. */
	disable_hlt();
}

/*
 * EMI
 */
static void stxh205_emi_power(struct stm_device_state *device_state,
		enum stm_device_power_state power)
{
	int i;
	int value = (power == stm_device_power_on) ? 0 : 1;

	stm_device_sysconf_write(device_state, "EMI_PWR", value);
	for (i = 5; i; --i) {
		if (stm_device_sysconf_read(device_state, "EMI_ACK")
			== value)
			break;
		mdelay(10);
	}

	return;
}

static struct platform_device stxh205_emi = {
	.name = "emi",
	.id = -1,
	.num_resources = 3,
	.resource = (struct resource[]) {
		STM_PLAT_RESOURCE_MEM_NAMED("emi memory", 0, 256 * 1024 * 1024),
		STM_PLAT_RESOURCE_MEM_NAMED("emi4 config", 0xfe900000, 0x874),
		STM_PLAT_RESOURCE_MEM_NAMED("emiss config", 0xfdaa9000 , 0x80),
	},
	.dev.platform_data = &(struct stm_device_config){
		.sysconfs_num = 2,
		.sysconfs = (struct stm_device_sysconf []){
			STM_DEVICE_SYSCONF(SYSCONF(401), 0, 0, "EMI_PWR"),
			STM_DEVICE_SYSCONF(SYSCONF(423), 0, 0, "EMI_ACK"),
		},
		.power = stxh205_emi_power,
	}
};

/*
 * Temperature sensor
 */
static int stxh205_temp_init(struct stm_device_state *device_state)
{
	struct clk *clk = clk_get(NULL, "CLK_A0_THNS");
	struct clk *osc;

	if (IS_ERR(clk))
		return -ENODEV;

	osc = clk_get(NULL, "CLK_A0_REF");
	if (IS_ERR(osc)) {
		clk_put(clk);
		return -ENODEV;
	}

	clk_prepare_enable(clk);
	clk_set_parent(clk, osc);
	clk_set_rate(clk, clk_get_rate(osc) / 30);

	return 0;
}

static void stxh205_temp_power(struct stm_device_state *device_state,
		enum stm_device_power_state power)
{
	int value = (power == stm_device_power_on) ? 1 : 0;

	stm_device_sysconf_write(device_state, "TEMP_PWR", value);
}

static struct platform_device sth205_temp = {
	.name	= "stm-temp",
	.id	= 0,
	.dev.platform_data = &(struct plat_stm_temp_data) {
		.correction_factor = -103,
		.device_config = &(struct stm_device_config) {
			.sysconfs_num = 4,
			.init = stxh205_temp_init,
			.power = stxh205_temp_power,
			.sysconfs = (struct stm_device_sysconf []){
				STM_DEVICE_SYSCONF(SYSCONF(140),
					9, 9, "TEMP_PWR"),
				STM_DEVICE_SYSCONF(SYSCONF(140),
					4, 8, "DCORRECT"),
				STM_DEVICE_SYSCONF(SYSCONF(148),
					9, 9, "OVERFLOW"),
				STM_DEVICE_SYSCONF(SYSCONF(148),
					11, 18, "DATA"),
			},
		},
	},
};

/* SPI FSM setup ---------------------------------------------------------- */

static struct stm_pad_config stxh205_spifsm_pad_config = {
	.gpios_num = 6,
	.gpios = (struct stm_pad_gpio[]) {
		STM_PAD_PIO_OUT_NAMED(12, 2, 1, "spi-fsm-clk"),
		STM_PAD_PIO_OUT_NAMED(12, 3, 1, "spi-fsm-cs"),
		/* To support QUAD mode operations, each of the following pads
		 * may be used by the IP as an input or an output.  Here we
		 * specify either PIO_OUT or PIO_IN, which sets pu = 0 && od =
		 * 0. 'oe' is taken from a signal generated by the SPI-FSM IP
		 * itself.
		 */
		STM_PAD_PIO_OUT_NAMED(12, 4, 1, "spi-fsm-mosi"),
		STM_PAD_PIO_IN_NAMED(12, 5, 1, "spi-fsm-miso"),
		STM_PAD_PIO_OUT_NAMED(12, 6, 1, "spi-fsm-hold"),
		STM_PAD_PIO_OUT_NAMED(12, 7, 1, "spi-fsm-wp"),
	}
};

static struct platform_device stxh205_spifsm_device = {
	.name		= "stm-spi-fsm",
	.id		= 0,
	.num_resources	= 1,
	.resource	= (struct resource[]) {
		{
			.start	= 0xfe902000,
			.end	= 0xfe9024ff,
			.flags	= IORESOURCE_MEM,
		},
	},
};

void __init stxh205_configure_spifsm(struct stm_plat_spifsm_data *data)
{
	struct sysconf_field *sc;

	stxh205_spifsm_device.dev.platform_data = data;

	data->pads = &stxh205_spifsm_pad_config;

	sc = sysconf_claim(SYSCONF(40), 2, 6, "mode-pins");

	/* SoC/IP Capabilities */
	data->capabilities.no_read_repeat = 1;
	data->capabilities.no_write_repeat = 1;
	data->capabilities.no_sw_reset = 0;
	data->capabilities.read_status_bug = spifsm_read_status_clkdiv4;
	data->capabilities.no_poll_mode_change = 0;
	data->capabilities.boot_from_spi = (sysconf_read(sc) == 0x1a) ? 1 : 0;

	sysconf_release(sc);
	platform_device_register(&stxh205_spifsm_device);
}


/* NAND Resources --------------------------------------------------------- */

static struct stm_plat_nand_flex_data stxh205_nand_flex_data;
static struct stm_plat_nand_bch_data stxh205_nand_bch_data;

static struct platform_device stxh205_nandi_device = {
	.id			= -1,
	.num_resources		= 3,
	.resource		= (struct resource[]) {
		STM_PLAT_RESOURCE_MEM_NAMED("nand_mem", 0xFE901000, 0x1000),
		STM_PLAT_RESOURCE_MEM_NAMED("nand_dma", 0xFDAA8800, 0x0800),
		STM_PLAT_RESOURCE_IRQ_NAMED("nand_irq", ILC_IRQ(121), -1),
	},
};

void __init stxh205_configure_nand(struct stm_nand_config *config)
{
	switch (config->driver) {
	case stm_nand_flex:
	case stm_nand_afm:
		/* Configure device for stm-nand-flex/afm driver */
		emiss_nandi_select(STM_NANDI_HAMMING);
		stxh205_nand_flex_data.nr_banks = config->nr_banks;
		stxh205_nand_flex_data.banks = config->banks;
		stxh205_nand_flex_data.flex_rbn_connected =
			config->rbn.flex_connected;
		stxh205_nandi_device.dev.platform_data =
			&stxh205_nand_flex_data;
		stxh205_nandi_device.name =
			(config->driver == stm_nand_flex) ?
			"stm-nand-flex" : "stm-nand-afm";
		platform_device_register(&stxh205_nandi_device);
		break;
	case stm_nand_bch:
		/* Configure device for stm-nand-bch driver */
		BUG_ON(config->nr_banks > 1);
		emiss_nandi_select(STM_NANDI_BCH);
		stxh205_nand_bch_data.bank = config->banks;
		stxh205_nand_bch_data.bch_ecc_cfg = config->bch_ecc_cfg;
		stxh205_nand_bch_data.bch_bitflip_threshold =
			config->bch_bitflip_threshold;
		stxh205_nandi_device.dev.platform_data =
			&stxh205_nand_bch_data;
		stxh205_nandi_device.name = "stm-nand-bch";
		platform_device_register(&stxh205_nandi_device);
		break;
	default:
		BUG();
		return;
	}
}


/* FDMA resources --------------------------------------------------------- */

static struct stm_plat_fdma_fw_regs stxh205_fdma_fw = {
	.rev_id    = 0x10000,
	.cmd_statn = 0x10200,
	.req_ctln  = 0x10240,
	.ptrn      = 0x10800,
	.cntn      = 0x10808,
	.saddrn    = 0x1080c,
	.daddrn    = 0x10810,
	.node_size = 128,
};

static struct stm_plat_fdma_hw stxh205_fdma_hw = {
	.slim_regs = {
		.id       = 0x0000 + (0x000 << 2), /* 0x0000 */
		.ver      = 0x0000 + (0x001 << 2), /* 0x0004 */
		.en       = 0x0000 + (0x002 << 2), /* 0x0008 */
		.clk_gate = 0x0000 + (0x003 << 2), /* 0x000c */
	},
	.dmem = {
		.offset = 0x10000,
		.size   = 0xc00 << 2, /* 3072 * 4 = 12K */
	},
	.periph_regs = {
		.sync_reg = 0x17f88,
		.cmd_sta  = 0x17fc0,
		.cmd_set  = 0x17fc4,
		.cmd_clr  = 0x17fc8,
		.cmd_mask = 0x17fcc,
		.int_sta  = 0x17fd0,
		.int_set  = 0x17fd4,
		.int_clr  = 0x17fd8,
		.int_mask = 0x17fdc,
	},
	.imem = {
		.offset = 0x18000,
		.size   = 0x2000 << 2, /* 8192 * 4 = 32K (24K populated) */
	},
};

static struct stm_plat_fdma_data stxh205_fdma_platform_data = {
	.hw = &stxh205_fdma_hw,
	.fw = &stxh205_fdma_fw,
};

static struct platform_device stxh205_fdma_devices[] = {
	{
		.name = "stm-fdma",
		.id = 0,
		.num_resources = 2,
		.resource = (struct resource[]) {
			STM_PLAT_RESOURCE_MEM(0xfda00000, 0x20000),
			STM_PLAT_RESOURCE_IRQ(ILC_IRQ(27), -1),
		},
		.dev.platform_data = &stxh205_fdma_platform_data,
	}, {
		.name = "stm-fdma",
		.id = 1,
		.num_resources = 2,
		.resource = (struct resource[2]) {
			STM_PLAT_RESOURCE_MEM(0xfd9e0000, 0x20000),
			STM_PLAT_RESOURCE_IRQ(ILC_IRQ(29), -1),
		},
		.dev.platform_data = &stxh205_fdma_platform_data,
	}
};

static struct platform_device stxh205_fdma_xbar_device = {
	.name = "stm-fdma-xbar",
	.id = -1,
	.num_resources = 1,
	.resource = (struct resource[]) {
		STM_PLAT_RESOURCE_MEM(0xfdabb000, 0x1000),
	},
};


/* st231 coprocessor resources -------------------------------------------- */

static struct platform_device stxh205_st231_coprocessor_devices[2] = {
	{
		.name = "stm-coproc-st200",
		.id = 0,
		.dev.platform_data = &(struct plat_stm_st231_coproc_data) {
			.name = "video",
			.id = 0,
			.device_config = &(struct stm_device_config) {
				.sysconfs_num = 2,
				.sysconfs = (struct stm_device_sysconf []) {
					STM_DEVICE_SYSCONF(SYSCONF(175),
							6, 31, "BOOT_ADDR"),
					STM_DEVICE_SYSCONF(SYSCONF(460),
							27, 27, "RESET"),
				},
			},
			.boot_shift = 6,
			.not_reset = 1,
		},
	}, {
		.name = "stm-coproc-st200",
		.id = 1,
		.dev.platform_data = &(struct plat_stm_st231_coproc_data) {
			.name = "audio",
			.id = 0,
			.device_config = &(struct stm_device_config) {
				.sysconfs_num = 2,
				.sysconfs = (struct stm_device_sysconf []) {
					STM_DEVICE_SYSCONF(SYSCONF(176),
							6, 31, "BOOT_ADDR"),
					STM_DEVICE_SYSCONF(SYSCONF(460),
							26, 26, "RESET"),
				},
			},
			.boot_shift = 6,
			.not_reset = 1,
		},
	},
};


/* Hardware RNG resources ------------------------------------------------- */

static struct platform_device stxh205_devhwrandom_device = {
	.name		= "stm-hwrandom",
	.id		= -1,
	.num_resources	= 1,
	.resource	= (struct resource []) {
		STM_PLAT_RESOURCE_MEM(0xfdabd000, 0x1000),
	}
};

/* Pre-arch initialisation ------------------------------------------------ */

static int __init stxh205_postcore_setup(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(stxh205_pio_devices); i++)
		platform_device_register(&stxh205_pio_devices[i]);

	platform_device_register(&stxh205_emi);

	return 0;
}
postcore_initcall(stxh205_postcore_setup);

/* Late initialisation ---------------------------------------------------- */

static struct platform_device *stxh205_devices[] __initdata = {
	&stxh205_st231_coprocessor_devices[0],
	&stxh205_st231_coprocessor_devices[1],
	&stxh205_sysconf_devices[0],
	&stxh205_sysconf_devices[1],
	&stxh205_sysconf_devices[2],
	&stxh205_sysconf_devices[3],
	&stxh205_sysconf_devices[4],
	&sth205_temp,
	&stxh205_fdma_devices[0],
	&stxh205_fdma_devices[1],
	&stxh205_fdma_xbar_device,
	&stxh205_devhwrandom_device,
};

static int __init stxh205_devices_setup(void)
{
	return platform_add_devices(stxh205_devices,
			ARRAY_SIZE(stxh205_devices));
}
device_initcall(stxh205_devices_setup);
