/*
 *   STMicroelectronics System-on-Chips' internal (sysconf controlled)
 *   audio DAC driver
 *
 *   Copyright (c) 2005-2013 STMicroelectronics Limited
 *
 *   Author: John Boddie <john.boddie@st.com>
 *           Pawel Moll <pawel.moll@st.com>
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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/list.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/stm/sysconf.h>
#include <sound/core.h>
#include <sound/info.h>
#include <sound/stm.h>

#include "common.h"



static int snd_stm_debug_level;
module_param_named(debug, snd_stm_debug_level, int, S_IRUGO | S_IWUSR);



/*
 * Hardware-related definitions
 */

#define FORMAT (SND_STM_FORMAT__I2S | SND_STM_FORMAT__SUBFRAME_32_BITS)
#define OVERSAMPLING 256



/*
 * Internal DAC instance structure
 */

struct conv_dac_sc {
	struct device *dev;

	/* System informations */
	struct snd_stm_conv_converter *converter;
	const char *bus_id;
	const char *source_bus_id;

	int channel_to;
	int channel_from;

	unsigned int format;
	int oversampling;

	/* Control bits */
	struct sysconf_field *nrst;
	struct sysconf_field *mode;
	struct sysconf_field *nsb;
	struct sysconf_field *sb;
	struct sysconf_field *softmute;
	struct sysconf_field *mute_l;
	struct sysconf_field *mute_r;
	struct sysconf_field *sbana;
	struct sysconf_field *npdana;
	struct sysconf_field *pdana;
	struct sysconf_field *pndbg;

	snd_stm_magic_field;
};

#define FIELD_EMPTY(f) \
	((f.group == 0) && (f.num == 0) && (f.lsb == 0) && (f.msb == 0))



/*
 * Converter interface implementation
 */

static unsigned int conv_dac_sc_get_format(void *priv)
{
	struct conv_dac_sc *conv = priv;

	BUG_ON(!conv);
	BUG_ON(!snd_stm_magic_valid(conv));

	dev_dbg(conv->dev, "%s(priv=%p)", __func__, priv);

	return conv->format;
}

static int conv_dac_sc_get_oversampling(void *priv)
{
	struct conv_dac_sc *conv = priv;

	BUG_ON(!conv);
	BUG_ON(!snd_stm_magic_valid(conv));

	dev_dbg(conv->dev, "%s(priv=%p)", __func__, priv);

	return conv->oversampling;
}

static int conv_dac_sc_set_enabled(int enabled, void *priv)
{
	struct conv_dac_sc *conv = priv;

	BUG_ON(!conv);
	BUG_ON(!snd_stm_magic_valid(conv));

	dev_dbg(conv->dev, "%s(enabled=%d, priv=%p)", __func__, enabled, priv);

	dev_dbg(conv->dev, "%sabling DAC for %s",
			enabled ? "En" : "Dis", conv->bus_id);

	if (enabled) {
		/* Take the DAC out of standby */
		if (conv->nsb)
			sysconf_write(conv->nsb, 1);
		if (conv->sb)
			sysconf_write(conv->sb, 0);

		/* Take the DAC out of reset */
		if (conv->nrst)
			sysconf_write(conv->nrst, 1);
	} else {
		/* Put the DAC into reset */
		if (conv->nrst)
			sysconf_write(conv->nrst, 0);

		/* Put the DAC into standby */
		if (conv->nsb)
			sysconf_write(conv->nsb, 0);
		if (conv->sb)
			sysconf_write(conv->sb, 1);
	}

	return 0;
}

static int conv_dac_sc_set_muted(int muted, void *priv)
{
	struct conv_dac_sc *conv = priv;

	BUG_ON(!conv);
	BUG_ON(!snd_stm_magic_valid(conv));

	dev_dbg(conv->dev, "%s(muted=%d, priv=%p)", __func__, muted, priv);

	dev_dbg(conv->dev, "%suting DAC for %s", muted ? "M" : "Unm",
			conv->bus_id);

	if (conv->softmute)
		sysconf_write(conv->softmute, muted ? 1 : 0);
	if (conv->mute_l)
		sysconf_write(conv->mute_l, muted ? 1 : 0);
	if (conv->mute_r)
		sysconf_write(conv->mute_r, muted ? 1 : 0);

	return 0;
}

static struct snd_stm_conv_ops conv_dac_sc_ops = {
	.get_format	  = conv_dac_sc_get_format,
	.get_oversampling = conv_dac_sc_get_oversampling,
	.set_enabled	  = conv_dac_sc_set_enabled,
	.set_muted	  = conv_dac_sc_set_muted,
};


/*
 * ALSA low-level device implementation
 */

static int conv_dac_sc_register(struct snd_device *snd_device)
{
	struct conv_dac_sc *conv = snd_device->device_data;

	BUG_ON(!conv);
	BUG_ON(!snd_stm_magic_valid(conv));

	dev_dbg(conv->dev, "%s(snd_device=%p)", __func__, snd_device);

	/* Put the DAC into reset */
	if (conv->nrst)
		sysconf_write(conv->nrst, 0);

	/* Put the DAC into standby */
	if (conv->nsb)
		sysconf_write(conv->nsb, 0);
	if (conv->sb)
		sysconf_write(conv->sb, 1);

	/* Mute the DAC */
	if (conv->softmute)
		sysconf_write(conv->softmute, 1);
	if (conv->mute_l)
		sysconf_write(conv->mute_l, 1);
	if (conv->mute_r)
		sysconf_write(conv->mute_r, 1);

	/* Take the DAC analog bits out of standby */
	if (conv->mode)
		sysconf_write(conv->mode, 0);
	if (conv->npdana)
		sysconf_write(conv->npdana, 0);
	if (conv->sbana)
		sysconf_write(conv->sbana, 0);
	if (conv->pdana)
		sysconf_write(conv->pdana, 1);
	if (conv->pndbg)
		sysconf_write(conv->pndbg, 1);

	return 0;
}

static int conv_dac_sc_disconnect(struct snd_device *snd_device)
{
	struct conv_dac_sc *conv = snd_device->device_data;

	BUG_ON(!conv);
	BUG_ON(!snd_stm_magic_valid(conv));

	dev_dbg(conv->dev, "%s(snd_device=%p)", __func__, snd_device);

	/* Put the DAC into reset */
	if (conv->nrst)
		sysconf_write(conv->nrst, 0);

	/* Put the DAC into standby */
	if (conv->nsb)
		sysconf_write(conv->nsb, 0);
	if (conv->sb)
		sysconf_write(conv->sb, 1);

	/* Mute the DAC */
	if (conv->softmute)
		sysconf_write(conv->softmute, 1);
	if (conv->mute_l)
		sysconf_write(conv->mute_l, 1);
	if (conv->mute_r)
		sysconf_write(conv->mute_r, 1);

	/* Put the DAC analog bits into standby */
	if (conv->mode)
		sysconf_write(conv->mode, 0);
	if (conv->npdana)
		sysconf_write(conv->npdana, 1);
	if (conv->sbana)
		sysconf_write(conv->sbana, 1);
	if (conv->pdana)
		sysconf_write(conv->pdana, 0);
	if (conv->pndbg)
		sysconf_write(conv->pndbg, 0);

	return 0;
}

static struct snd_device_ops conv_dac_sc_snd_device_ops = {
	.dev_register	= conv_dac_sc_register,
	.dev_disconnect	= conv_dac_sc_disconnect,
};



/*
 * Platform driver routines
 */

static void conv_dac_sc_parse_dt(struct platform_device *pdev,
		struct conv_dac_sc *conv)
{
	struct device_node *pnode = pdev->dev.of_node;

	/* Read the device bus and channel properties */
	of_property_read_string(pnode, "source-bus-id", &conv->source_bus_id);
	of_property_read_u32(pnode, "channel-to", &conv->channel_to);
	of_property_read_u32(pnode, "channel-from", &conv->channel_from);
	of_property_read_u32(pnode, "format", &conv->format);
	of_property_read_u32(pnode, "oversampling", &conv->oversampling);

	/* Read the device reset sysconf property */
	conv->nrst = stm_of_sysconf_claim(pnode, "not-reset");

	/* Read the device standby sysconf properties */
	conv->nsb = stm_of_sysconf_claim(pnode, "not-standby");
	conv->sb = stm_of_sysconf_claim(pnode, "standby");

	/* Read the device mute sysconf properties */
	conv->softmute = stm_of_sysconf_claim(pnode, "softmute");
	conv->mute_l = stm_of_sysconf_claim(pnode, "mute-left");
	conv->mute_r = stm_of_sysconf_claim(pnode, "mute-right");

	/* Read the device mode sysconf property */
	conv->mode = stm_of_sysconf_claim(pnode, "mode");

	/* Read the device analog sysconf properties */
	conv->npdana = stm_of_sysconf_claim(pnode, "analog-not-pwr-dwn");
	conv->sbana = stm_of_sysconf_claim(pnode, "analog-standby");
	conv->pdana = stm_of_sysconf_claim(pnode, "analog-pwr-dwn");
	conv->pndbg = stm_of_sysconf_claim(pnode, "analog-not-pwr-dwn-bg");
}

static void conv_dac_sc_parse_pdata(struct platform_device *pdev,
		struct conv_dac_sc *conv)
{
	struct snd_stm_conv_dac_sc_info *info = pdev->dev.platform_data;

	BUG_ON(!info);

	/* Set the device bus and channel data */
	conv->source_bus_id = info->source_bus_id;
	conv->channel_to = info->channel_to;
	conv->channel_from = info->channel_from;
	conv->format = info->format;
	conv->oversampling = info->oversampling;

	/* Claim the device reset sysconf */
	if (!FIELD_EMPTY(info->nrst)) {
		conv->nrst = sysconf_claim(info->nrst.group,
				info->nrst.num,info->nrst.lsb, info->nrst.msb,
				"NRST");
		WARN_ON(!conv->nrst);
	}

	/* Claim the device standby sysconfs */
	if (!FIELD_EMPTY(info->nsb))
		conv->nsb = sysconf_claim(info->nsb.group, info->nsb.num,
				info->nsb.lsb, info->nsb.msb, "NSB");
	if (!FIELD_EMPTY(info->sb))
		conv->sb = sysconf_claim(info->sb.group, info->sb.num,
				info->sb.lsb, info->sb.msb, "SB");

	/* Claim the device mute sysconfs */
	if (!FIELD_EMPTY(info->softmute))
		conv->softmute = sysconf_claim(info->softmute.group,
				info->softmute.num, info->softmute.lsb,
				info->softmute.msb, "SOFTMUTE");
	if (!FIELD_EMPTY(info->mute_l))
		conv->mute_l = sysconf_claim(info->mute_l.group,
				info->mute_l.num, info->mute_l.lsb,
				info->mute_l.msb, "MUTE_L");
	if (!FIELD_EMPTY(info->mute_r))
		conv->mute_r = sysconf_claim(info->mute_r.group,
				info->mute_r.num, info->mute_r.lsb,
				info->mute_r.msb, "MUTE_R");

	/* Claim the device mode sysconf */
	if (!FIELD_EMPTY(info->mode)) {
		conv->mode = sysconf_claim(info->mode.group,
				info->mode.num, info->mode.lsb,
				info->mode.msb, "MODE");
		WARN_ON(!conv->mode);
	}

	/* Claim the device analog sysconfs */
	if (!FIELD_EMPTY(info->npdana)) {
		conv->npdana = sysconf_claim(info->npdana.group,
				info->npdana.num, info->npdana.lsb,
				info->npdana.msb, "NPDANA");
		WARN_ON(!conv->npdana);
	}

	if (!FIELD_EMPTY(info->sbana)) {
		conv->sbana = sysconf_claim(info->sbana.group,
				info->sbana.num, info->sbana.lsb,
				info->sbana.msb, "SBANA");
		WARN_ON(!conv->sbana);
	}

	if (!FIELD_EMPTY(info->pdana)) {
		conv->pdana = sysconf_claim(info->pdana.group,
				info->pdana.num, info->pdana.lsb,
				info->pdana.msb, "PDANA");
		WARN_ON(!conv->pdana);
	}

	if (!FIELD_EMPTY(info->pndbg)) {
		conv->pndbg = sysconf_claim(info->pndbg.group,
				info->pndbg.num, info->pndbg.lsb,
				info->pndbg.msb, "PNDBG");
		WARN_ON(!conv->pndbg);
	}
}

static int conv_dac_sc_probe(struct platform_device *pdev)
{
	int result = 0;
	struct conv_dac_sc *conv;
	struct snd_card *card = snd_stm_card_get(SND_STM_CARD_TYPE_AUDIO);

	BUG_ON(!card);

	snd_stm_printd(0, "%s('%s')\n", __func__, dev_name(&pdev->dev));

	/* Allocate driver structure */
	conv = devm_kzalloc(&pdev->dev, sizeof(*conv), GFP_KERNEL);
	if (!conv) {
		dev_err(&pdev->dev, "Failed to allocate driver structure");
		return -ENOMEM;
	}

	snd_stm_magic_set(conv);
	conv->dev = &pdev->dev;
	conv->bus_id = dev_name(&pdev->dev);

	/* Get resources */
	if (!pdev->dev.of_node)
		conv_dac_sc_parse_pdata(pdev, conv);
	else
		conv_dac_sc_parse_dt(pdev, conv);

	/* Ensure valid sysconf and bus id */
	if (!conv->source_bus_id) {
		dev_err(conv->dev, "Invalid source bus id!");
		return -EINVAL;
	}

	/* Check if we need to use the default format */
	if (conv->format == 0)
		conv->format = FORMAT;

	/* Check if we need to use the default oversampling */
	if (conv->oversampling == 0)
		conv->oversampling = OVERSAMPLING;

	/* SoC should have 'not-reset' sysconf */
	dev_warn(&pdev->dev, "not-reset sysconf not configured");

	/* SoC must have either 'not-standby' or 'standby' sysconf */
	if (!conv->nsb && !conv->sb) {
		dev_err(conv->dev, "Failed to claim any standby sysconf!");
		return -EINVAL;
	}

	/* SoC must have either 'softmute' or 'mute-left' & 'mute-right' */
	if (!conv->softmute && !conv->mute_l && !conv->mute_r) {
		dev_err(conv->dev, "Failed to claim any mute sysconf!");
		return -EINVAL;
	}

	dev_dbg(&pdev->dev, "Attached to %s", conv->source_bus_id);

	/* Register the converter */
	conv->converter = snd_stm_conv_register_converter(
			"DAC SC", &conv_dac_sc_ops, conv,
			&platform_bus_type, conv->source_bus_id,
			conv->channel_from, conv->channel_to, NULL);
	if (!conv->converter) {
		dev_err(conv->dev, "Can't attach to player!");
		result = -ENODEV;
		goto error_attach;
	}

	/* Create ALSA low-level device */
	result = snd_device_new(card, SNDRV_DEV_LOWLEVEL, conv,
			&conv_dac_sc_snd_device_ops);
	if (result < 0) {
		dev_err(conv->dev, "ALSA low-level device creation failed!");
		goto error_device;
	}

	/* Save converter in driver data */
	platform_set_drvdata(pdev, conv);

	return 0;

error_device:
	snd_stm_conv_unregister_converter(conv->converter);
error_attach:
	snd_stm_magic_clear(conv);
	return result;
}

static int conv_dac_sc_remove(struct platform_device *pdev)
{
	struct conv_dac_sc *conv = platform_get_drvdata(pdev);

	BUG_ON(!conv);
	BUG_ON(!snd_stm_magic_valid(conv));

	snd_stm_conv_unregister_converter(conv->converter);

	sysconf_release(conv->nrst);

	if (conv->nrst)
		sysconf_release(conv->nrst);
	if (conv->nsb)
		sysconf_release(conv->nsb);
	if (conv->sb)
		sysconf_release(conv->sb);
	if (conv->softmute)
		sysconf_release(conv->softmute);
	if (conv->mute_l)
		sysconf_release(conv->mute_l);
	if (conv->mute_r)
		sysconf_release(conv->mute_r);
	if (conv->mode)
		sysconf_release(conv->mode);
	if (conv->sbana)
		sysconf_release(conv->sbana);
	if (conv->npdana)
		sysconf_release(conv->npdana);
	if (conv->pdana)
		sysconf_release(conv->pdana);
	if (conv->pndbg)
		sysconf_release(conv->pndbg);

	snd_stm_magic_clear(conv);

	return 0;
}


/*
 * Module initialization.
 */

#ifdef CONFIG_OF
static struct of_device_id conv_dac_sc_match[] = {
	{
		.compatible = "st,snd_conv_dac_sc",
	},
	{},
};

MODULE_DEVICE_TABLE(of, conv_dac_sc_match);
#endif

static struct platform_driver conv_dac_sc_platform_driver = {
	.driver.name	= "snd_conv_dac_sc",
	.driver.of_match_table = of_match_ptr(conv_dac_sc_match),
	.probe		= conv_dac_sc_probe,
	.remove		= conv_dac_sc_remove,
};

module_platform_driver(conv_dac_sc_platform_driver);

MODULE_AUTHOR("John Boddie <john.boddie@st.com>");
MODULE_DESCRIPTION("STMicroelectronics sysconf-based DAC converter driver");
MODULE_LICENSE("GPL");
