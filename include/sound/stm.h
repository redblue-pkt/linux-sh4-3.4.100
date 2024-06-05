/*
 *   STMicrolectronics SoCs audio subsystem
 *
 *   Copyright (c) 2005-2011 STMicroelectronics Limited
 *
 *   Author: Pawel Moll <pawel.moll@st.com>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#ifndef __SOUND_STM_H
#define __SOUND_STM_H

#include <linux/i2c.h>
#include <linux/stm/pad.h>
#include <linux/stm/dma.h>
#include <sound/core.h>



/*
 * Converters (DAC, ADC, I2S to SPDIF, SPDIF to I2S, etc.)
 */

/* Link type (format) description */
#define SND_STM_FORMAT__I2S              0x00000001
#define SND_STM_FORMAT__LEFT_JUSTIFIED   0x00000002
#define SND_STM_FORMAT__RIGHT_JUSTIFIED  0x00000003
#define SND_STM_FORMAT__SPDIF            0x00000004
#define SND_STM_FORMAT__MASK             0x0000000f

/* Following values are valid only for I2S, Left Justified and
 * Right justified formats and can be bit-added to format;
 * they define size of one subframe (channel) transmitted.
 * For SPDIF the frame size is fixed and defined by standard. */
#define SND_STM_FORMAT__SUBFRAME_32_BITS 0x00000010
#define SND_STM_FORMAT__SUBFRAME_16_BITS 0x00000020
#define SND_STM_FORMAT__SUBFRAME_MASK    0x000000f0

/* Converter operations
 * All converter should be disabled and muted till explicitly
 * enabled/unmuted. */

struct snd_stm_conv_converter;

struct snd_stm_conv_ops {
	/* Configuration */
	unsigned int (*get_format)(void *priv);
	int (*get_oversampling)(void *priv);

	/* Operations */
	int (*set_enabled)(int enabled, void *priv);
	int (*set_muted)(int muted, void *priv);
};

/* Registers new converter
 * Converters sharing the same group name shall use the same input format
 * and oversampling value - they usually should represent one "output";
 * when more than one group is connected to the same source, active one
 * will be selectable using "Route" ALSA control.
 * Returns negative value on error or unique converter index number (>=0) */
struct snd_stm_conv_converter *snd_stm_conv_register_converter(
		const char *group, struct snd_stm_conv_ops *ops, void *priv,
		struct bus_type *source_bus, const char *source_bus_id,
		int source_channel_from, int source_channel_to, int *index);

int snd_stm_conv_unregister_converter(struct snd_stm_conv_converter *converter);


/*
 * Generic conv implementations
 */

/* I2C-controlled DAC/ADC generic implementation
 *
 * Define a "struct i2c_board_info" with "struct snd_stm_conv_i2c_info"
 * as a platform data:
 *
 * static struct i2c_board_info external_dac __initdata = {
 * 	I2C_BOARD_INFO("snd_conv_i2c", <I2C address>),
 * 	.type = "<i.e. chip model>",
 * 	.platform_data = &(struct snd_stm_conv_i2c_info) {
 * 		<see below>
 * 	},
 * };
 *
 * and add it:
 *
 * i2c_register_board_info(<I2C bus number>, &external_dac, 1);
 *
 * If you wish to perform some actions on the device before it
 * is being used, you may define "init" callback, which will
 * be called with i2c_client pointer during driver probe.
 */
struct snd_stm_conv_i2c_info {
	const char *group;

	const char *source_bus_id;
	int channel_from, channel_to;
	unsigned int format;
	int oversampling;

	int (*init)(struct i2c_client *client, void *priv);
	void *priv;

	int enable_supported;
	const char *enable_cmd;
	int enable_cmd_len;
	const char *disable_cmd;
	int disable_cmd_len;

	int mute_supported;
	const char *mute_cmd;
	int mute_cmd_len;
	const char *unmute_cmd;
	int unmute_cmd_len;
};

/* GPIO-controlled DAC/ADC generic implementation
 *
 * Define platform device named "snd_conv_gpio", pass
 * following structure as platform_data and add it in normal way :-) */
struct snd_stm_conv_gpio_info {
	const char *group;

	const char *source_bus_id;
	int channel_from, channel_to;
	unsigned int format;
	int oversampling;

	int enable_supported;
	unsigned enable_gpio;
	int enable_value;

	int mute_supported;
	unsigned mute_gpio;
	int mute_value;
};

/* EPLD-controlled DAC/ADC generic implementation
 *
 * Define platform device named "snd_conv_epld", pass
 * following structure as platform_data and add it in normal way :-) */
struct snd_stm_conv_epld_info {
	const char *group;

	const char *source_bus_id;
	int channel_from, channel_to;
	unsigned int format;
	int oversampling;

	int enable_supported;
	unsigned enable_offset;
	unsigned enable_mask;
	unsigned enable_value;
	unsigned disable_value;

	int mute_supported;
	unsigned mute_offset;
	unsigned mute_mask;
	unsigned mute_value;
	unsigned unmute_value;
};

/* Dummy converter - use it (as a "snd_conv_dummy" platform device)
 * to define format or oversampling only */
struct snd_stm_conv_dummy_info {
	const char *group;

	const char *source_bus_id;
	int channel_from, channel_to;
	unsigned int format;
	int oversampling;
};



/*
 * Internal audio DAC descriptions (platform data)
 */

struct snd_stm_conv_dac_mem_info {
	const char *source_bus_id;
	int channel_from, channel_to;
};

struct snd_stm_conv_dac_sc_info {
	const char *source_bus_id;
	int channel_from, channel_to;

	unsigned int format;
	int oversampling;

	struct {
		int group;
		int num;
		int lsb;
		int msb;
	} nrst, mode, nsb, sb, softmute, mute_l, mute_r, sbana, npdana, pdana,
	pndbg;
};



/*
 * I2S to SPDIF converter description (platform data)
 */

struct snd_stm_conv_i2sspdif_info {
	int ver;

	const char *source_bus_id;
	int channel_from, channel_to;
};



/*
 * Bi-phase formatter converter description (platform data)
 */

struct snd_stm_conv_biphase_info {
	const char *source_bus_id;
	int channel_from, channel_to;

	unsigned int format;
	int oversampling;

	struct {
		int group;
		int num;
		int lsb;
		int msb;
	} enable, idle;

	unsigned int idle_value;
};



/*
 * SPDIF RX converter description (platform data)
 */

struct snd_stm_conv_spdif_rx_info {
	const char *source_bus_id;
	int channel_from, channel_to;

	unsigned long clock_rate;

	struct stm_pad_config *pad_config;
};



/*
 * PCM Player description (platform data)
 */

struct snd_stm_pcm_player_info {
	const char *name;
	int ver;

	int card_device;

	unsigned int channels;

	const char *fdma_name;
	unsigned char fdma_initiator;
	unsigned int fdma_direct_conn;
	unsigned int fdma_request_line;

	struct stm_pad_config *pad_config;
};



/*
 * PCM Reader description (platform data)
 */

struct snd_stm_pcm_reader_info {
	const char *name;
	int ver;

	int card_device;

	int channels;

	const char *fdma_name;
	unsigned int fdma_initiator;
	unsigned int fdma_direct_conn;
	unsigned int fdma_request_line;

	struct stm_pad_config *pad_config;
};



/*
 * SPDIF Player description (platform data)
 */

struct snd_stm_spdif_player_info {
	const char *name;
	int ver;

	int card_device;

	const char *fdma_name;
	unsigned char fdma_initiator;
	unsigned int fdma_direct_conn;
	unsigned int fdma_request_line;

	struct stm_pad_config *pad_config;
};


/*
 * Uniperipheral IP revisions
 */
enum snd_stm_uniperif_version {
	SND_STM_UNIPERIF_VERSION_UNKOWN,
	/* SASG1 (Orly), Newman */
	SND_STM_UNIPERIF_VERSION_C6AUD0_UNI_1_0,
	/* SASG2 (Orly2) */
	SND_STM_UNIPERIF_VERSION_UNI_PLR_1_0,
	/* SASG2 (Orly2), TELSS, Cannes */
	SND_STM_UNIPERIF_VERSION_UNI_RDR_1_0,
	/* TELSS */
	SND_STM_UNIPERIF_VERSION_TDM_PLR_1_0,
	/* Cannes/Monaco */
	SND_STM_UNIPERIF_VERSION_UNI_PLR_TOP_1_0
};


/*
 * Uniperipheral player description (platform data)
 */

enum snd_stm_uniperif_player_type {
	SND_STM_UNIPERIF_PLAYER_TYPE_NONE,
	SND_STM_UNIPERIF_PLAYER_TYPE_HDMI,
	SND_STM_UNIPERIF_PLAYER_TYPE_PCM,
	SND_STM_UNIPERIF_PLAYER_TYPE_SPDIF
};

struct snd_stm_uniperif_player_info {
	const char *name;
	int ver;

	int card_device;
	enum snd_stm_uniperif_player_type player_type;

	unsigned int iec958_lr_pol;		/* HDMI/SPDIF LR polarity */
	unsigned int iec958_i2s_mode;		/* HDMI/SPDIF I2S mode */

	unsigned int channels;

	int s16_swap_lr;			/* S16LE: swap left/right */
	int parking_enabled;
	int standby_enabled;
	int underflow_enabled;			/* Underflow recovery mode */

	const char *fdma_name;
	unsigned int fdma_initiator;
	unsigned int fdma_direct_conn;
	unsigned int fdma_request_line;

	struct stm_pad_config *pad_config;
};


/*
 * Uniperipheral TDM description (platform data)
 */

struct snd_stm_telss_handset_info {
	unsigned int fsync;
	unsigned int slot1;
	unsigned int slot2;
	unsigned int slot2_valid;
	unsigned int duplicate;
	unsigned int data16;
	unsigned int cnb;
	unsigned int lnb;
	unsigned int cwb;
	unsigned int lwb;
};

#define SND_STM_TELSS_HANDSET_INFO(fs, s1, s2, v, dup, dat, cn, ln, cw, lw)\
	{ \
		.fsync = fs, \
		.slot1 = s1, \
		.slot2 = s2, \
		.slot2_valid = v, \
		.duplicate = dup, \
		.data16 = dat, \
		.cnb = cn, \
		.lnb = ln, \
		.cwb = cw, \
		.lwb = lw, \
	}

struct snd_stm_telss_word_pos_info {
	unsigned int msb;
	unsigned int lsb;
};

struct snd_stm_uniperif_tdm_info {
	const char *name;
	int ver;
	int card_device;

	const char *fdma_name;
	unsigned int fdma_channel;
	unsigned int fdma_initiator;
	enum dma_transfer_direction fdma_direction;
	unsigned int fdma_direct_conn;
	unsigned int fdma_request_line;

	struct stm_pad_config *pad_config;

	unsigned int rising_edge;		/* Data on rising edge */
	unsigned int clk_rate;			/* Clock rate in Hz */
	unsigned int pclk_rate;			/* PCLK rate in Hz */
	unsigned int fs_rate;			/* fs ref rate in Hz */
	unsigned int timeslots;			/* Time slots per fs ref */
	unsigned int fs01_rate;			/* fs01 rate in Hz */
	unsigned int fs02_rate;			/* fs02 rate in Hz */
	unsigned int fs02_delay_clock;		/* fs02 delay from fs01 */
	unsigned int fs02_delay_timeslot;	/* fs02 delay from fs01 */
	unsigned int msbit_start;		/* Timeslot start position */

	unsigned int frame_size;		/* In 32-bit words */
	unsigned int frame_count;		/* Frames per period */
	unsigned int max_periods;		/* Max audio periods */
	unsigned int handset_count;		/* One per I2S: 2/4/6/8/10 */
	struct snd_stm_telss_handset_info *handset_info;
};


/*
 * TELSS Glue description (platform data)
 */

enum snd_stm_telss_glue_mode {
	SND_STM_TELSS_GLUE_MODE_DECT,
	SND_STM_TELSS_GLUE_MODE_LANTIQ,
	SND_STM_TELSS_GLUE_MODE_ZSI
};

struct snd_stm_telss_glue_info {
	const char *name;			/* Driver name */
	int ver;				/* IP version */

	enum snd_stm_telss_glue_mode mode;	/* TELSS operating mode */
	unsigned int loopback;			/* Loop back UniP into UniR */
};


#endif
