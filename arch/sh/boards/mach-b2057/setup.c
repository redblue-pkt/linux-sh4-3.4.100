/*
 * arch/sh/boards/mach-b2057/setup.c
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
#include <asm/irq-ilc.h>

#define B2057_GPIO_FLASH_WP		stm_gpio(6, 2)
#define B2057_GPIO_POWER_ON_ETH		stm_gpio(2, 5)
#define B2057_MII1_TXER			stm_gpio(0, 4)
#define B2057_POWER_ON			stm_gpio(3, 7)

static void __init b2057_setup(char **cmdline_p)
{
	printk(KERN_INFO "STMicroelectronics B2057 board initialisation\n");

	stxh205_early_device_init();

	/* Socket JM5 DB9 connector */
	stxh205_configure_asc(STXH205_ASC(10), &(struct stxh205_asc_config) {
			.hw_flow_control = 0,
			.is_console = 1, });
	/*
	 * Header JK1 to UART daughter board (no h/w flow control) or
	 * socket JB7 to MoCA (with h/w flow control).
	 */
	stxh205_configure_asc(STXH205_ASC(1), &(struct stxh205_asc_config) {
			.hw_flow_control = 0, });
}

static struct platform_device b2057_leds = {
	.name = "leds-gpio",
	.id = -1,
	.dev.platform_data = &(struct gpio_led_platform_data) {
		.num_leds = 2,
		.leds = (struct gpio_led[]) {
			{
				.name = "GREEN",
				.default_trigger = "heartbeat",
				.gpio = stm_gpio(3, 2),
			}, {
				.name = "RED",
				.gpio = stm_gpio(3, 1),
			},
		},
	},
};

static struct tm1668_key b2057_front_panel_keys[] = {
	{ 0x00001000, KEY_UP, "Up (SWF2)" },
	{ 0x00800000, KEY_DOWN, "Down (SWF7)" },
	{ 0x00008000, KEY_LEFT, "Left (SWF6)" },
	{ 0x00000010, KEY_RIGHT, "Right (SWF5)" },
	{ 0x00000080, KEY_OK, "Menu/OK (SWF1)" },
	{ 0x00100000, KEY_BACK, "Back (SWF4)" },
	{ 0x80000000, KEY_TV, "DOXTV (SWF9)" },
};

static struct tm1668_character b2057_front_panel_characters[] = {
	TM1668_7_SEG_HEX_DIGITS,
	TM1668_7_SEG_SEGMENTS,
	TM1668_7_SEG_LETTERS
};

static struct platform_device b2057_front_panel = {
	.name = "tm1668",
	.id = -1,
	.dev.platform_data = &(struct tm1668_platform_data) {
		.gpio_dio = stm_gpio(15, 4),
		.gpio_sclk = stm_gpio(14, 7),
		.gpio_stb = stm_gpio(14, 4),
		.config = tm1668_config_6_digits_12_segments,

		.keys_num = ARRAY_SIZE(b2057_front_panel_keys),
		.keys = b2057_front_panel_keys,
		.keys_poll_period = DIV_ROUND_UP(HZ, 5),

		.brightness = 8,
		.characters_num = ARRAY_SIZE(b2057_front_panel_characters),
		.characters = b2057_front_panel_characters,
		.text = "H207",
	},
};

static struct gpio_keys_button b2057_fp_gpio_keys_button = {
	.code = KEY_SUSPEND,
	.gpio = stm_gpio(15, 7),
	.desc = "Standby",
};

static struct platform_device b2057_fp_gpio_keys = {
        .name = "gpio-keys",
        .id = -1,
        .num_resources = 0,
        .dev = {
                .platform_data = &(struct gpio_keys_platform_data){
			.buttons = &b2057_fp_gpio_keys_button,
			.nbuttons = 1,
		}
        }
};

/* Serial Flash */
static struct stm_plat_spifsm_data b2057_serial_flash =  {
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
		 * Reset signal can be routed to UM7 (SO16 option) by fitting
		 * RM52 (default is DNF)
		 */
		.reset_signal = 0,
	},
};

/*
 * NAND Flash: Micron MT29F8G08ABABAWP
 *  - Requires 4-bit ECC
 *  - ONFI compliant: timing parameters retrieved during device probe
 */
static struct stm_nand_bank_data b2057_nand_flash = {
	.csn		= 0,	/* Rev A/B : set JF3 2-3 (EMI_CS0 -> NAND_CS) */
	.options        = NAND_NO_AUTOINCR,
	.bbt_options	= NAND_BBT_USE_FLASH,
	.nr_partitions	= 2,
	.partitions	= (struct mtd_partition []) {
		{
			.name	= "NAND Flash 1",
			.offset	= 0,
			.size	= 0x04000000
		}, {
			.name	= "NAND Flash 2",
			.offset = MTDPART_OFS_NXTBLK,
			.size	= MTDPART_SIZ_FULL
		},
	},
};

static int b2057_phy_reset(void *bus)
{
	/*
	 * IC+ IP101 datasheet specifies 10mS low period and device usable
	 * 2.5mS after rising edge. However experimentally it appear
	 * 10mS is required for reliable functioning.
	 */
	gpio_set_value(B2057_GPIO_POWER_ON_ETH, 0);
	mdelay(10);
	gpio_set_value(B2057_GPIO_POWER_ON_ETH, 1);
	mdelay(10);

	return 1;
}

static struct stmmac_mdio_bus_data stmmac_mdio_bus = {
	.phy_reset = b2057_phy_reset,
	.phy_mask = 0,
	.probed_phy_irq = ILC_IRQ(25), /* MDINT */
};

static struct platform_device *b2057_devices[] __initdata = {
	&b2057_leds,
	&b2057_front_panel,
	&b2057_fp_gpio_keys,
};

static int __init device_init(void)
{
	/* The "POWER_ON_ETH" line should be rather called "PHY_RESET",
	 * but it isn't... ;-) */
	gpio_request(B2057_GPIO_POWER_ON_ETH, "POWER_ON_ETH");
	gpio_direction_output(B2057_GPIO_POWER_ON_ETH, 0);

	/* This PIO controls power of board */
	gpio_request(B2057_POWER_ON, "POWER_ON");
	gpio_direction_output(B2057_POWER_ON, 0);

#ifdef CONFIG_STM_B2057_INT_PHY_IC101A
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
	gpio_request(B2057_MII1_TXER, "MII1_TXER");
	gpio_direction_output(B2057_MII1_TXER, 0);
#endif

#if !defined(CONFIG_STM_B2057_INT_PHY_NONE) || \
    !defined(CONFIG_STM_B2057_JP1_NONE)
	/* Configure GMII MDINT for active low */
	irq_set_irq_type(ILC_IRQ(25), IRQ_TYPE_LEVEL_LOW);

	stxh205_configure_ethernet(&(struct stxh205_ethernet_config) {
#if defined(CONFIG_STM_B2057_IC101_MII) || \
    defined(CONFIG_STM_B2057_JP1_B2032)
			.interface = PHY_INTERFACE_MODE_MII,
			.ext_clk = 1,
#elif defined(CONFIG_STM_B2057_IC101_RMII)
			.interface = PHY_INTERFACE_MODE_RMII,
			.ext_clk = 1,
#elif defined(CONFIG_STM_B2057_JP1_B2035)
			.interface = PHY_INTERFACE_MODE_RMII,
			.ext_clk = 0,
#else
#error Unknown PHY configuration
#endif
#if defined(CONFIG_STM_B2057_INT_PHY_IC101A)
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
			});
	stxh205_configure_sata();

	stxh205_configure_usb(0);
	stxh205_configure_usb(1);

	/* SSC1: NIM */
	stxh205_configure_ssc_i2c(1, &(struct stxh205_ssc_config) {
			.i2c_speed = 100,
			.routing.ssc1.sclk = stxh205_ssc1_sclk_pio12_0,
			.routing.ssc1.mtsr = stxh205_ssc1_mtsr_pio12_1, });
	/* SSC3: SYS - STV6440, EEPROM, Front panel, MII and JQ3 */
	stxh205_configure_ssc_i2c(3, &(struct stxh205_ssc_config) {
			.i2c_speed = 100,
			.routing.ssc3.sclk = stxh205_ssc3_sclk_pio15_5,
			.routing.ssc3.mtsr = stxh205_ssc3_mtsr_pio15_6, });
	/* SSC11: HDMI */
	stxh205_configure_ssc_i2c(STXH205_SSC(11),  &(struct stxh205_ssc_config) {
			.i2c_speed = 100, });

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
		});

	/*
	 * NAND MTD has no concept of write-protect, so permanently disable WP
	 */
	gpio_request(B2057_GPIO_FLASH_WP, "FLASH_WP");
	gpio_direction_output(B2057_GPIO_FLASH_WP, 1);

	/*
	 * NAND MTD has no concept of write-protect, so permanently disable WP
	 */
	gpio_request(B2057_GPIO_FLASH_WP, "FLASH_WP");
	gpio_direction_output(B2057_GPIO_FLASH_WP, 1);

	stxh205_configure_nand(&(struct stm_nand_config) {
			.driver = stm_nand_bch,
			.nr_banks = 1,
			.banks = &b2057_nand_flash,
			.rbn.flex_connected = 1,
			.bch_ecc_cfg = BCH_ECC_CFG_AUTO});

	stxh205_configure_spifsm(&b2057_serial_flash);

	/* PCM Player #2 (PIO) and PCM Reader are not enabled */
	stxh205_configure_audio(&(struct stxh205_audio_config) {
			.spdif_player_output_enabled = 1 });

	return platform_add_devices(b2057_devices,
			ARRAY_SIZE(b2057_devices));
}
arch_initcall(device_init);

struct sh_machine_vector mv_b2057 __initmv = {
	.mv_name = "b2057",
	.mv_setup = b2057_setup,
	.mv_nr_irqs = NR_IRQS,
};

#if defined(CONFIG_HIBERNATION_ON_MEMORY)

#include "../../kernel/cpu/sh4/stm_hom.h"

static int b2057_board_freeze(struct stm_wakeup_devices *wkd)
{
	if (!wkd->stm_mac0_can_wakeup)
		gpio_set_value(B2057_GPIO_POWER_ON_ETH, 0);
	return 0;
}

static struct stm_hom_board b2057_hom = {
	.freeze = b2057_board_freeze,
};

static int __init b2057_hom_register(void)
{
	return stm_hom_board_register(&b2057_hom);
}

module_init(b2057_hom_register);
#endif

