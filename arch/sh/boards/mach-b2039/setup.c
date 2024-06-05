/*
 * arch/sh/boards/mach-b2039/setup.c
 *
 * Copyright (C) 2011 STMicroelectronics Limited
 * Author: Stuart Menefy (stuart.menefy@st.com)
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
#include <linux/leds.h>
#include <linux/i2c.h>
#include <linux/i2c-gpio.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/nand.h>
#include <linux/spi/spi.h>
#include <linux/spi/flash.h>
#include <linux/stm/platform.h>
#include <linux/stm/nand_devices.h>
#include <linux/stm/stxh205.h>
#include <linux/stm/sysconf.h>
#include <asm/irq-ilc.h>

#define B2039_MII1_NOTRESET		stm_gpio(3, 0)
#define B2039_MII1_TXER			stm_gpio(0, 4)

static void __init b2039_setup(char **cmdline_p)
{
	printk(KERN_INFO "STMicroelectronics B2039 board initialisation\n");

	stxh205_early_device_init();

	/*
	 * UART0: CN43
	 * UART1: CN41
	 * UART2: CN29
	 * UART10: CN22 -> b2001 CN3 (COM1)
	 * UART11: CN21 -> b2001 CN2 (COM0)
	 */
	stxh205_configure_asc(STXH205_ASC(10), &(struct stxh205_asc_config) {
			/*
			 * Enabling hw flow control conflicts with IRB_IN,
			 * keyscan and PWR_ENABLE GPIO pin.
			 */
			.hw_flow_control = 0,
			.is_console = 1, });
	/*
	 * We disable UART11 as the muxing with MII1_notRESET can cause
	 * problems even when flow control is disabled.
	 *
	 * Enabling hw flow control conflicts with FP_LED,
	 * keyscan and PWM10.
	 *
	 * stxh205_configure_asc(STXH205_ASC(11), &(struct stxh205_asc_config) {
	 * 		.hw_flow_control = 0,
	 * 		.is_console = 1, });
	 */
}

static struct platform_device b2039_gpio_i2c_hdmi = {
	.name = "i2c-gpio",
	.id = 3,
	.dev.platform_data = &(struct i2c_gpio_platform_data) {
		.sda_pin = stm_gpio(12, 1),
		.scl_pin = stm_gpio(12, 0),
		.sda_is_open_drain = 0,
		.scl_is_open_drain = 0,
		.scl_is_output_only = 1,
        },
};

static struct platform_device b2039_leds = {
	.name = "leds-gpio",
	.id = -1,
	.dev.platform_data = &(struct gpio_led_platform_data) {
		.num_leds = 1,
		.leds = (struct gpio_led[]) {
			{
				/* Need to fit J29 1-2 and disable UART11 RTS */
				.name = "FP_LED",
				.default_trigger = "heartbeat",
				.gpio = stm_gpio(3, 1),
			},
		},
	},
};

/* Serial Flash */
static struct stm_plat_spifsm_data b2039_serial_flash =  {
	.name		= "mx25l25635e",
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
		.reset_signal = 1, /* SoC reset routed to device reset pad */
	},
};

/*
 * NAND Flash
 *	b2006a/b2007a VPMEM module
 *	J66 should be 1-2, or open (i.e. 'VPMEM_notWP' not connected)
 */
static struct stm_nand_bank_data b2039_nand_flash = {
	.csn		= 0,
	.options        = NAND_NO_AUTOINCR,
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
	/*
	 * Timing parameters specified for the Samsung K9F2G08U0C device.
	 * Please update according to mounted device (numerous population
	 * options available).
	 */
	.timing_spec	= &NAND_TSPEC_SAMSUNG_K9F2G08U0C,
};


/*
 * Need to fit J35 1-2 for this, disconnect CN21 to prevert the
 * level converter trying to drive UART11 CTS and disable keyscan
 * and sysclkin.
 */
static int b2039_phy_reset(void *bus)
{
#ifdef CONFIG_STM_B2039_J35_PHY_RESET
	/*
	 * IC+ IP101 datasheet specifies 10mS low period and device
	 * usable 2.5mS after rising edge of notReset. However
	 * experimentally it appear 10mS is required for reliable
	 * functioning.
	 */
	gpio_set_value(B2039_MII1_NOTRESET, 0);
	mdelay(10);
	gpio_set_value(B2039_MII1_NOTRESET, 1);
	mdelay(10);
#endif

	return 1;
}

static struct stmmac_mdio_bus_data stmmac_mdio_bus = {
	.phy_reset = b2039_phy_reset,
	.phy_mask = 0,
	.probed_phy_irq = ILC_IRQ(25), /* MDINT */
};

static struct platform_device *b2039_devices[] __initdata = {
	&b2039_leds,
};

static int __init device_init(void)
{
#ifdef CONFIG_STM_B2039_J35_PHY_RESET
	/* This conflicts with PWM10, keyscan and ASC11 */
	gpio_request(B2039_MII1_NOTRESET, "MII1_NORESET");
	gpio_direction_output(B2039_MII1_NOTRESET, 0);
#endif

#ifdef CONFIG_STM_B2039_INT_PHY_IC101A
	/*
	 * This is a work around for the problem that on parts with an
	 * IC+101A, the pin marked as TX_ER_FXSD (AA23) is actually
	 * ISOL, which appears to be driven high at boot time despite the
	 * internal pull down in the IC+101A.
	 * In MII mode this doesn't appear to be a problem because the
	 * STxH207 is driving the pin, and so it remains low, however
	 * just in case the GMAC were to assert this sgnal for whatever
	 * reason, we still drive treat it as a gpio pin.
	 */
	gpio_request(B2039_MII1_TXER, "MII1_TXER");
	gpio_direction_output(B2039_MII1_TXER, 0);
#endif

#if !defined(CONFIG_STM_B2039_INT_PHY_NONE) || \
    !defined(CONFIG_STM_B2039_CN14_NONE)
	/* Configure GMII MDINT for active low */
	irq_set_irq_type(ILC_IRQ(25), IRQ_TYPE_LEVEL_LOW);

	stxh205_configure_ethernet(&(struct stxh205_ethernet_config) {
#if defined(CONFIG_STM_B2039_IC101_MII) || \
    defined(CONFIG_STM_B2039_CN14_B2032)
			.interface = PHY_INTERFACE_MODE_MII,
			.ext_clk = 1,
#elif defined(CONFIG_STM_B2039_IC101_RMII_EXTCLK)
			.interface = PHY_INTERFACE_MODE_RMII,
			.ext_clk = 1,
#elif defined(CONFIG_STM_B2039_IC101_RMII_NO_EXTCLK) || \
      defined(CONFIG_STM_B2039_CN14_B2035)
			.interface = PHY_INTERFACE_MODE_RMII,
			.ext_clk = 0,
#else
#error Unknown PHY configuration
#endif
#if defined(CONFIG_STM_B2039_INT_PHY_IC101A)
			.no_txer = 1,
#endif
			.phy_bus = 0,
			.phy_addr = -1,
			.mdio_bus_data = &stmmac_mdio_bus,
		});
#endif
	/* PHY IRQ has to be triggered LOW */
	irq_set_irq_type(ILC_IRQ(25), IRQ_TYPE_LEVEL_LOW);

	stxh205_configure_miphy(&(struct stxh205_miphy_config){
			.mode = SATA_MODE,
			.iface = UPORT_IF,
			.tx_pol_inv = 1,
			});
	stxh205_configure_sata();
	/* Need to set J17 1-2 and J19 1-2 */
	stxh205_configure_usb(0);

	/* Need to set J12 1-2 and J22 1-2 */
	stxh205_configure_usb(1);

	/* 1: FRONTEND CN18 (NIM), CN19 */
	stxh205_configure_ssc_i2c(1, &(struct stxh205_ssc_config) {
			.i2c_speed = 100,
			.routing.ssc1.sclk = stxh205_ssc1_sclk_pio4_6,
			.routing.ssc1.mtsr = stxh205_ssc1_mtsr_pio4_7, });

	/* 2: FRONTEND_EXT (VPAV), CN28 */
	stxh205_configure_ssc_i2c(2, &(struct stxh205_ssc_config) {
			.i2c_speed = 100,
			.routing.ssc1.sclk = stxh205_ssc2_sclk_pio9_4,
			.routing.ssc1.mtsr = stxh205_ssc2_mtsr_pio9_5, });
	/* 3: BACKEND (GMII (CN14), CN10, CN37), CN27 */
	/* Fit jumpers J45 2-3, J46 2-3, J47 2-3 */

#if !defined(CONFIG_STM_B2039_PIO15_AUDIO)
	stxh205_configure_ssc_i2c(3, &(struct stxh205_ssc_config) {
			.i2c_speed = 100,
			.routing.ssc1.sclk = stxh205_ssc3_sclk_pio15_0,
			.routing.ssc1.mtsr = stxh205_ssc3_mtsr_pio15_1, });
#endif

	/*
	 * 1: PIO_HDMI_TX: U4 (HDMI2C1), CN9
	 * Fit jumpers J8 and J9
	 * This usage of SSC1 can't be used concurrently with the FRONTEND
	 * bus above. So drive this bus using gpio i2c.
	 *
	 * stxh205_configure_ssc_i2c(1, &(struct stxh205_ssc_config) {
	 * 		.i2c_speed = 100,
	 *		.routing.ssc1.sclk = stxh205_ssc1_sclk_pio12_0,
	 *		.routing.ssc1.mtsr = stxh205_ssc1_mtsr_pio12_1, });
	 */
	BUG_ON(platform_device_register(&b2039_gpio_i2c_hdmi));

	stxh205_configure_lirc(&(struct stxh205_lirc_config) {
#ifdef CONFIG_LIRC_STM_UHF
			.rx_mode = stxh205_lirc_rx_mode_uhf, });
#else
			.rx_mode = stxh205_lirc_rx_mode_ir, });
#endif

	stxh205_configure_pwm(&(struct stxh205_pwm_config) {
			/*
			 * 10: conflicts with SBC_SYS_CLKINALT, UART11 CTS
			 *    keyscan and MII1 notReset.
			 * 11: conflicts with ETH_RXCLK
			 */
			.out_channel_config[0].enabled = 0,
			.out_channel_config[1].enabled = 0,
		});

	/* NAND Flash via b2006/b2007 VPMEM daughter board. */
	stxh205_configure_nand(&(struct stm_nand_config) {
			.driver = stm_nand_flex,
			.nr_banks = 1,
			.banks = &b2039_nand_flash,
			.rbn.flex_connected = 1,});

	stxh205_configure_spifsm(&b2039_serial_flash);

	/* PCM Player #2 (PIO) and PCM Reader are not enabled by default */
	stxh205_configure_audio(&(struct stxh205_audio_config) {
#ifdef CONFIG_STM_B2039_PIO15_AUDIO
			.pcm_player_2_output_enabled = 1,
			.pcm_reader_input_enabled    = 1,
#endif
			.spdif_player_output_enabled = 1 });

	stxh205_configure_mmc(&(struct stxh205_mmc_config) {
#ifdef CONFIG_STM_B2039_B2048A_MMC_EMMC
			.emmc = 1,
#endif
		});
	return platform_add_devices(b2039_devices,
			ARRAY_SIZE(b2039_devices));
}
arch_initcall(device_init);

struct sh_machine_vector mv_b2039 __initmv = {
	.mv_name = "b2039",
	.mv_setup = b2039_setup,
	.mv_nr_irqs = NR_IRQS,
};
