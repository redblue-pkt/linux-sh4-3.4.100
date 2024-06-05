/*
 * arch/sh/boards/mach-B2146/setup.c
 *
 * Copyright (C) 2012 STMicroelectronics Limited
 * Author: Robert Yin (robert.yin@st.com) ShenZhen Board Design Team
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 */

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/phy.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/leds.h>
#include <linux/tm1668.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/nand.h>
#include <linux/spi/spi.h>
#include <linux/spi/flash.h>
#include <linux/stm/platform.h>
#include <linux/stm/stxh205.h>
#include <linux/stm/sysconf.h>
#include <linux/stm/pci-glue.h>
#include <asm/irq-ilc.h>

#define B2146_GPIO_POWER_ON_ETH		stm_gpio(3, 3)
#define B2146_MII1_TXER			stm_gpio(0, 4)
#define B2146_POWER_ON                  stm_gpio(3, 7)

static void __init B2146_setup(char **cmdline_p)
{
	printk(KERN_INFO "STMicroelectronics B2146 board initialisation\n");

	stxh205_early_device_init();

	stxh205_configure_asc(STXH205_ASC(1), &(struct stxh205_asc_config) {
			.hw_flow_control = 0,
			.is_console = 1,
		});

}

static struct platform_device B2146_leds = {
	.name = "leds-gpio",
	.id = -1,
	.dev.platform_data = &(struct gpio_led_platform_data) {
		.num_leds = 1,
		.leds = (struct gpio_led[]) {
			{
				.name = "RED",
				.default_trigger = "heartbeat",
				.gpio = stm_gpio(11, 6),
			},
		},
	},
};



/* Serial Flash */
static struct stm_plat_spifsm_data B2146_serial_flash =  {
	.name		= "n25q256",
	.nr_parts	= 2,
	.parts = (struct mtd_partition []) {
		{
			.name = "Serial Flash 1",
			.size = 0x00080000,
			.offset = 0,
		}, {
			.name = "Serial Flash 2",
			.size = MTDPART_SIZ_FULL,
			.offset = MTDPART_OFS_NXTBLK,
		},
	},
	.capabilities = {
		/* Capabilities may be overriden by SoC configuration */
		.dual_mode = 1,
		.quad_mode = 1,
		/*
		 * Reset signal can be routed to U4 and U12 by fitting
		 * RM52 (default is DNF).
		 * Note rev A boards misconnected reset and hold signals.
		 */
		.reset_signal = 0,
	},
};

/* NAND Flash */
static struct stm_nand_bank_data B2146_nand_flash = {
	.csn		= 0,	/* Controlled by JF3 */
	.options	= NAND_NO_AUTOINCR,
	.bbt_options	= NAND_BBT_USE_FLASH,
	.nr_partitions	= 2,
	.partitions	= (struct mtd_partition []) {
		{
			.name	= "NAND Flash 1",
			.offset	= 0,
			.size	= 0x00800000
		}, {
			.name	= "NAND Flash 2",
			.offset = MTDPART_OFS_NXTBLK,
			.size	= MTDPART_SIZ_FULL
		},
	},
	.timing_data	=  &(struct stm_nand_timing_data) {
		.sig_setup	= 50,		/* times in ns */
		.sig_hold	= 50,
		.CE_deassert	= 0,
		.WE_to_RBn	= 100,
		.wr_on		= 10,
		.wr_off		= 40,
		.rd_on		= 10,
		.rd_off		= 40,
		.chip_delay	= 30,		/* in us */
	},
};

static int B2146_phy_reset(void *bus)
{
	/*
	 * IC+ IP101 datasheet specifies 10mS low period and device usable
	 * 2.5mS after rising edge. However experimentally it appear
	 * 10mS is required for reliable functioning.
	 */
	gpio_set_value(B2146_GPIO_POWER_ON_ETH, 0);
	mdelay(10);
	gpio_set_value(B2146_GPIO_POWER_ON_ETH, 1);
	mdelay(10);

	return 1;
}

static struct stmmac_mdio_bus_data stmmac_mdio_bus = {
	.phy_reset = B2146_phy_reset,
	.phy_mask = 0,
	.probed_phy_irq = ILC_IRQ(25), /* MDINT */
};

static struct platform_device *B2146_devices[] __initdata = {
	&B2146_leds,
	//&B2146_front_panel,
};

static int __init device_init(void)
{
	/* The "POWER_ON_ETH" line should be rather called "PHY_RESET",
	 * but it isn't... ;-) */
	gpio_request(B2146_GPIO_POWER_ON_ETH, "POWER_ON_ETH");
	gpio_direction_output(B2146_GPIO_POWER_ON_ETH, 0);

	stxh205_configure_ethernet(&(struct stxh205_ethernet_config) {
			.interface = PHY_INTERFACE_MODE_MII,
			.ext_clk = 1,
			.phy_bus = 0,
			.phy_addr = -1,
			.mdio_bus_data = &stmmac_mdio_bus,
		});

	/* PHY IRQ has to be triggered LOW */
	irq_set_irq_type(ILC_IRQ(25), IRQ_TYPE_LEVEL_LOW);

	stxh205_configure_miphy(&(struct stxh205_miphy_config){
			.mode = PCIE_MODE,
			.iface = UPORT_IF,
			});

	stxh205_configure_pcie(&(struct stxh205_pcie_config) {
			.reset_gpio = stm_gpio(4, 6),
			});

	stxh205_configure_usb(0);

	stxh205_configure_usb(1);

	/*
	 * SSC1: FE/DEMO
	 * U14: LNBH26PQR, STxH238: J_SCL/SDA (internal demod), JB6
	 */
	stxh205_configure_ssc_i2c(STXH205_SSC(1), &(struct stxh205_ssc_config) {
			.routing.ssc1.sclk = stxh205_ssc1_sclk_pio12_0,
			.routing.ssc1.mtsr = stxh205_ssc1_mtsr_pio12_1, });
	/*
	 * SSC3: SYS
	 * UQ1: STV6440, UK2: M24256 (EEPROM), JQ3
	 * Also JK2 (front panel) on rev A only
	 */
	stxh205_configure_ssc_i2c(STXH205_SSC(3), &(struct stxh205_ssc_config) {
			.routing.ssc3.sclk = stxh205_ssc3_sclk_pio15_5,
			.routing.ssc3.mtsr = stxh205_ssc3_mtsr_pio15_6, });
	/*
	 * SSC11: HDMI
	 * UG1: HDMI2C1 -> HDMI
	 */
	stxh205_configure_ssc_i2c(STXH205_SSC(11), NULL);

	stxh205_configure_lirc(&(struct stxh205_lirc_config) {
#ifdef CONFIG_LIRC_STM_UHF
			.rx_mode = stxh205_lirc_rx_mode_uhf, });
#else
			.rx_mode = stxh205_lirc_rx_mode_ir, });
#endif

	stxh205_configure_pwm(&(struct stxh205_pwm_config) {
			/*
			 * PWM10 is connected to 12V->1.2V power supply
			 * for "debug purposes". Enable at your own risk!
			 */
			.out_channel_config[0].enabled = 0,
		});

	stxh205_configure_mmc(&(struct stxh205_mmc_config) {
			.emmc = 0,
			.no_mmc_boot_data_error = 1,
		});

	stxh205_configure_nand(&(struct stm_nand_config) {
			.driver = stm_nand_bch,
			.nr_banks = 1,
			.banks = &B2146_nand_flash,
			.bch_ecc_cfg = BCH_ECC_CFG_18BIT,});

	stxh205_configure_spifsm(&B2146_serial_flash);

	return platform_add_devices(B2146_devices,
			ARRAY_SIZE(B2146_devices));
}
arch_initcall(device_init);

struct sh_machine_vector mv_B2146 __initmv = {
	.mv_name = "B2146",
	.mv_setup = B2146_setup,
	.mv_nr_irqs = NR_IRQS,
};

#if defined(CONFIG_HIBERNATION_ON_MEMORY)

#include "../../kernel/cpu/sh4/stm_hom.h"

static int B2146_board_freeze(struct stm_wakeup_devices *wkd)
{
	if (!wkd->stm_mac0_can_wakeup)
		gpio_set_value(B2146_GPIO_POWER_ON_ETH, 0);
	return 0;
}

static int B2146_board_defrost(struct stm_wakeup_devices *wkd)
{
	B2146_phy_reset(NULL);
	return 0;
}

static struct stm_hom_board B2146_hom = {
	.freeze = B2146_board_freeze,
	.restore = B2146_board_defrost,
};

static int __init B2146_hom_register(void)
{
	return stm_hom_board_register(&B2146_hom);
}

module_init(B2146_hom_register);
#endif
