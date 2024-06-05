/*
 *   STMicrolectronics audio glue driver
 *
 *   Copyright (c) 2013 STMicroelectronics Limited
 *
 *   Authors: John Boddie <john.boddie@st.com>
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

#include <linux/of.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/stm/sysconf.h>
#include <linux/stm/platform.h>

#include "common.h"


/*
 * Driver structure.
 */

struct audio_glue {
	struct stm_device_config *dev_config;
	struct stm_device_state *dev_state;
};


/*
 * Driver functions.
 */

static int __devinit audio_glue_probe(struct platform_device *pdev)
{
	struct audio_glue *glue;
	int result;

	dev_dbg(&pdev->dev, "%s()", __func__);

	/* Allocate driver structure */
	glue = devm_kzalloc(&pdev->dev, sizeof(*glue), GFP_KERNEL);
	if (!glue) {
		dev_err(&pdev->dev, "Failed to allocate driver structure");
		return -ENOMEM;
	}

	/* Get the device configuration */
	if (!pdev->dev.of_node)
		glue->dev_config = pdev->dev.platform_data;
	else
		glue->dev_config = stm_of_get_dev_config(&pdev->dev);

	/* Set the device configuration */
	glue->dev_state = devm_stm_device_init(&pdev->dev, glue->dev_config);
	if (!glue->dev_state) {
		dev_err(&pdev->dev, "Failed to initialise device");
		return -ENOMEM;
	}

	/* Register the audio sound card */
	result = snd_stm_card_register(SND_STM_CARD_TYPE_AUDIO);
	if (result) {
		dev_err(&pdev->dev, "Failed to register ALSA audio card!");
		return result;
	}

	/* Save the dev config as driver data */
	dev_set_drvdata(&pdev->dev, glue);

	return 0;
}

static int __devexit audio_glue_remove(struct platform_device *pdev)
{
	struct audio_glue *glue = dev_get_drvdata(&pdev->dev);

	dev_dbg(&pdev->dev, "%s()", __func__);

	/* Issue the device power off sequence */
	stm_device_power(glue->dev_state, stm_device_power_off);

	return 0;
}


/*
 * Power management.
 */

#ifdef CONFIG_PM
static int audio_glue_suspend(struct device *dev)
{
	struct audio_glue *glue = dev_get_drvdata(dev);

	dev_dbg(dev, "%s()", __func__);

	/* Issue the device power off sequence */
	stm_device_power(glue->dev_state, stm_device_power_off);

	return 0;
}

static int audio_glue_resume(struct device *dev)
{
	struct audio_glue *glue = dev_get_drvdata(dev);

	dev_dbg(dev, "%s()", __func__);

	/* Issue the device power on sequence */
	stm_device_power(glue->dev_state, stm_device_power_on);

	return 0;
}
#else
#define audio_glue_suspend NULL
#define audio_glue_resume NULL
#endif

static const struct dev_pm_ops audio_glue_pm_ops = {
	.suspend	= audio_glue_suspend,
	.resume		= audio_glue_resume,

	.freeze		= audio_glue_suspend,
	.thaw		= audio_glue_resume,
	.restore	= audio_glue_resume,
};


/*
 * Module initialistaion.
 */

#ifdef CONFIG_OF
static struct of_device_id audio_glue[] = {
	{
		.compatible = "st,snd_audio_glue",
	},
	{},
};

MODULE_DEVICE_TABLE(of, audio_glue);
#endif

static struct platform_driver audio_glue_platform_driver = {
	.driver.name	= "snd_audio_glue",
	.driver.of_match_table = of_match_ptr(audio_glue),
	.driver.pm	= &audio_glue_pm_ops,
	.probe		= audio_glue_probe,
	.remove		= audio_glue_remove,
};

module_platform_driver(audio_glue_platform_driver);

MODULE_AUTHOR("John Boddie <john.boddie@st.com>");
MODULE_DESCRIPTION("STMicroelectronics audio glue driver");
MODULE_LICENSE("GPL");
