/*
 * Copyright (C) 2006 STMicroelectronics Limited
 * Author: Stuart Menefy <stuart.menefy@st.com>
 *
 * Contains code copyright (C) Echostar Technologies Corporation
 * Author: Anthony Jackson <anthony.jackson@echostar.com>
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/semaphore.h>
#include <linux/platform_device.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/stm/platform.h>
#include <asm/io.h>

struct stm_capt_data {
	u32 cnt[3];
	int index;
	struct semaphore lock;
	wait_queue_head_t wait;
};

struct stm_pwm {
	struct resource *mem;
	void* base;
	struct device *hwmon_dev;
	struct stm_plat_pwm_data *platform_data;
	struct stm_pad_state *out_pad_state[STM_PLAT_PWM_NUM_CHANNELS];
	struct stm_pad_state *capt_pad_state[STM_PLAT_PWM_NUM_CHANNELS];
	struct stm_capt_data *capt_data[STM_PLAT_PWM_NUM_CHANNELS];
	struct clk *ipclk;
#ifdef CONFIG_PM_SLEEP
	u8 suspend_value[STM_PLAT_PWM_NUM_CHANNELS];
#endif
};

/* Each capture input can be programmed to detect rising-edge, falling-edge,
 * either edge or neither egde
 */
enum stm_capt_edge {
	capt_edge_disabled,
	capt_edge_rising,
	capt_edge_falling,
	capt_edge_both,
};

/* PWM registers */
#define PWM_OUT_VAL(x)			(0x00 + (4 * (x)))
#define PWM_CPT_VAL(x)			(0x10 + (4 * (x)))

#define PWM_CTRL			0x50
#define PWM_CTRL_OUT_EN			(1<<9)
#define PWM_CTRL_CPT_EN			(1<<10)
#define PWM_CTRL_OUT_CLK_VAL0_SHIFT	0
#define PWM_CTRL_OUT_CLK_VAL4_SHIFT	11
#define PWM_CTRL_OUT_CLK_VAL_SPLIT	4
#define PWM_CTRL_OUT_CLK_VAL_MASK	0xff
#define PWM_CTRL_OUT_CLK_VAL0_MASK	\
	((1 << PWM_CTRL_OUT_CLK_VAL_SPLIT) - 1)
#define PWM_CTRL_OUT_CLK_VAL4_MASK	\
	(PWM_CTRL_OUT_CLK_VAL_MASK & ~PWM_CTRL_OUT_CLK_VAL0_MASK)
#define PWM_CTRL_CPT_CLK_VAL_SHIFT	4
#define PWM_CTRL_CPT_CLK_VAL_MASK	0x1f

#define PWM_INT_EN			0x54
#define PWM_INT_STA			0x58
#define PWM_INT_ACK			0x5c
#define PWM_INT_ACK_MASK		0x1ff
#define PWM_INT_OUT_MASK		0x01
#define PWM_INT_OUT_SHIFT		0
#define PWM_INT_CPT_MASK		0x0f
#define PWM_INT_CPT_SHIFT		1

#define PWM_CPT_EDGE(x)			(0x30 + (4 * (x)))
#define PWM_CPT_EDGE_MASK		0x3

#define CPT_DC_MAX			255

/* Prescale value (clock dividor): */
#define PWM_CLK_VAL		0

static unsigned long stm_pwm_get_base_freq(struct device *dev)
{
	struct stm_pwm *pwm = dev_get_drvdata(dev);

	if (IS_ERR(pwm->ipclk))
		return 30000000UL;
	else
		return clk_get_rate(pwm->ipclk);
}

static ssize_t show_pwm_out(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	int channel = to_sensor_dev_attr(attr)->index;
	struct stm_pwm *pwm = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n", readb(pwm->base + PWM_OUT_VAL(channel)));
}

static ssize_t store_pwm_out(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf,
			     size_t count)
{
	int channel = to_sensor_dev_attr(attr)->index;
	struct stm_pwm *pwm = dev_get_drvdata(dev);
	char* p;
	long val = simple_strtol(buf, &p, 10);

	if (p != buf) {
		val &= 0xff;
		writeb(val, pwm->base + PWM_OUT_VAL(channel));
		return p-buf;
	}
	return -EINVAL;
}

static mode_t stm_pwm_out_is_visible(struct kobject *kobj,
				     struct attribute *attr,
				     int n)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct stm_pwm *pwm = dev_get_drvdata(dev);
	struct device_attribute *devattr;
	int channel;

	devattr = container_of(attr, struct device_attribute, attr);
	channel = to_sensor_dev_attr(devattr)->index;

	if (!pwm->platform_data->out_channel_config[channel].enabled)
		return 0;

	if (pwm->platform_data->out_channel_config[channel].locked)
		return S_IRUGO;

	return S_IRUGO | S_IWUSR;
}

static SENSOR_DEVICE_ATTR(pwm_out0, 0, show_pwm_out, store_pwm_out, 0);
static SENSOR_DEVICE_ATTR(pwm_out1, 0, show_pwm_out, store_pwm_out, 1);
static SENSOR_DEVICE_ATTR(pwm_out2, 0, show_pwm_out, store_pwm_out, 2);
static SENSOR_DEVICE_ATTR(pwm_out3, 0, show_pwm_out, store_pwm_out, 3);

static struct attribute *stm_pwm_out_attributes[] = {
	&sensor_dev_attr_pwm_out0.dev_attr.attr,
	&sensor_dev_attr_pwm_out1.dev_attr.attr,
	&sensor_dev_attr_pwm_out2.dev_attr.attr,
	&sensor_dev_attr_pwm_out3.dev_attr.attr,
	NULL
};

static struct attribute_group stm_pwm_out_attr_group = {
	.is_visible = stm_pwm_out_is_visible,
	.attrs = stm_pwm_out_attributes,
};

static ssize_t show_pwm_capt(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	int channel = to_sensor_dev_attr(attr)->index;
	struct stm_pwm *pwm = dev_get_drvdata(dev);
	struct stm_capt_data *d =  pwm->capt_data[channel];
	int ret;

	if (down_interruptible(&(d->lock)))
		return -ERESTARTSYS;

	/* Prepare capture measurement */
	d->index = 0;
	writeb(capt_edge_rising, pwm->base + PWM_CPT_EDGE(channel));

	ret = wait_event_interruptible_timeout(d->wait, d->index > 2, HZ);

	/*
	 * In case we woke up for another reason than completion
	 * make sure to disable the capture.
	 */
	writeb(capt_edge_disabled, pwm->base + PWM_CPT_EDGE(channel));

	if (ret != -ERESTARTSYS) {
		if (d->index < 3) {
			/*
			 * Getting here could mean :
			 *  - input signal is constant of less than 1Hz
			 *  - there is not input signal at all
			 *
			 * In such case the frequency is rounded down to 0
			 * level of the supposed constant signal is reported
			 * using duty cycle min and max values.
			 */
			bool level = gpio_get_value(pwm->platform_data->
				capt_pad_config[channel]->gpios->gpio);

			ret = sprintf(buf, "0:%u\n", level ? CPT_DC_MAX : 0);

		} else {
			/* We got what we need, hoorraaaa ! */
			u16 prescaler;
			u32 high = d->cnt[1] - d->cnt[0];
			u32 low  = d->cnt[2] - d->cnt[1];
			unsigned dc;
			unsigned f;

			dc = CPT_DC_MAX * high / (high + low);

			prescaler = ((readw(pwm->base + PWM_CTRL)
						  & PWM_CTRL_CPT_CLK_VAL_SHIFT)
						 >> PWM_CTRL_CPT_CLK_VAL_MASK);

			/* get the frequency in Hz */
			f = stm_pwm_get_base_freq(dev)
				/ ((1 + prescaler) * (high + low));

			ret = sprintf(buf, "%u:%u\n", f, dc);
		}
	}

	up(&d->lock);

	return ret;
}

static mode_t stm_pwm_capt_is_visible(struct kobject *kobj,
				     struct attribute *attr,
				     int n)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct stm_pwm *pwm = dev_get_drvdata(dev);
	struct device_attribute *devattr;
	int channel;

	devattr = container_of(attr, struct device_attribute, attr);
	channel = to_sensor_dev_attr(devattr)->index;

	if (!pwm->platform_data->capt_channel_config[channel].enabled)
		return 0;

	return S_IRUGO;
}

static SENSOR_DEVICE_ATTR(pwm_capt0, 0, show_pwm_capt, NULL, 0);
static SENSOR_DEVICE_ATTR(pwm_capt1, 0, show_pwm_capt, NULL, 1);
static SENSOR_DEVICE_ATTR(pwm_capt2, 0, show_pwm_capt, NULL, 2);
static SENSOR_DEVICE_ATTR(pwm_capt3, 0, show_pwm_capt, NULL, 3);

static struct attribute *stm_pwm_capt_attributes[] = {
	&sensor_dev_attr_pwm_capt0.dev_attr.attr,
	&sensor_dev_attr_pwm_capt1.dev_attr.attr,
	&sensor_dev_attr_pwm_capt2.dev_attr.attr,
	&sensor_dev_attr_pwm_capt3.dev_attr.attr,
	NULL
};

static struct attribute_group stm_pwm_capt_attr_group = {
	.is_visible = stm_pwm_capt_is_visible,
	.attrs = stm_pwm_capt_attributes,
};


static void stm_pwm_enable(struct stm_pwm *pwm)
{
	int channel;
	u32 reg = 0;

	/* disable PWM & CPT if currently running */
	reg = readw(pwm->base + PWM_CTRL);
	reg &= ~(PWM_CTRL_OUT_EN | PWM_CTRL_CPT_EN);
	writew(reg, pwm->base + PWM_CTRL);

	/* disable edge capture */
	for (channel = 0;  channel < STM_PLAT_PWM_NUM_CHANNELS; channel++)
		writeb(capt_edge_disabled, pwm->base + PWM_CPT_EDGE(channel));

	/* enable capture interrupts */
	reg = PWM_INT_CPT_MASK << PWM_INT_CPT_SHIFT;
	writew(reg, pwm->base + PWM_INT_EN);

	/* Set global PWM state: */
	reg = 0;

	/* set prescale values... */
	reg |= (PWM_CLK_VAL & PWM_CTRL_OUT_CLK_VAL0_MASK)
		<< PWM_CTRL_OUT_CLK_VAL0_SHIFT;
	reg |= ((PWM_CLK_VAL & PWM_CTRL_OUT_CLK_VAL4_MASK)
		>> PWM_CTRL_OUT_CLK_VAL_SPLIT)
		<< PWM_CTRL_OUT_CLK_VAL4_SHIFT;
	reg |= (PWM_CLK_VAL & PWM_CTRL_CPT_CLK_VAL_MASK)
		<< PWM_CTRL_CPT_CLK_VAL_SHIFT;

	/* enable */
	reg |= (PWM_CTRL_OUT_EN | PWM_CTRL_CPT_EN);
	writew(reg, pwm->base + PWM_CTRL);
}

static int
stm_pwm_init(struct platform_device  *pdev, struct stm_pwm *pwm)
{
	int channel;

	pwm->ipclk = clk_get(NULL, pwm->platform_data->clk);
	if (IS_ERR(pwm->ipclk))
		dev_warn(&pdev->dev, "Failed to find %s clock. Using default 30MHz.\n",
			 pwm->platform_data->clk);

	/* Initialize PWM out */
	for (channel = 0; channel < STM_PLAT_PWM_NUM_CHANNELS; channel++) {
		struct stm_plat_pwm_out_channel_config *channel_config;
		channel_config =
			&pwm->platform_data->out_channel_config[channel];
		if (channel_config->enabled) {
			if (!channel_config->retain_hw_value)
				writeb(channel_config->initial_value,
				       pwm->base + PWM_OUT_VAL(channel));

			pwm->out_pad_state[channel] =
				devm_stm_pad_claim(&pdev->dev,
				pwm->platform_data->out_pad_config[channel],
				dev_name(&pdev->dev));

			if (!pwm->out_pad_state[channel]) {
				dev_err(&pdev->dev, "cannot claim pwm pad\n");
				return -ENODEV;
			}
		}
	}

	/* Initialize PWM Capture IN */
	for (channel = 0; channel < STM_PLAT_PWM_NUM_CHANNELS; channel++) {
		struct stm_plat_pwm_capt_channel_config *channel_config;
		struct stm_capt_data *data = NULL;
		channel_config =
			&pwm->platform_data->capt_channel_config[channel];
		if (channel_config->enabled) {
			pwm->capt_pad_state[channel] =
				devm_stm_pad_claim(&pdev->dev,
				pwm->platform_data->capt_pad_config[channel],
				dev_name(&pdev->dev));
			if (!pwm->capt_pad_state[channel]) {
				dev_err(&pdev->dev, "cannot claim capt pad\n");
				return -ENODEV;
			}

			/* Init capture data if needed */
			data = devm_kzalloc(&pdev->dev,
					    sizeof(struct stm_capt_data),
					    GFP_KERNEL);
			if (!data)
				return -ENOMEM;

			init_waitqueue_head(&(data->wait));
			sema_init(&(data->lock), 1);
		}

		pwm->capt_data[channel] = data;
	}

	stm_pwm_enable(pwm);

	return 0;
}

static irqreturn_t stm_pwm_interrupt(int irq, void *data)
{
	struct stm_pwm *pwm = data;
	int channel;
	u16 status = readw(pwm->base + PWM_INT_STA);
	u16 status_capt = (status >> PWM_INT_CPT_SHIFT) & PWM_INT_CPT_MASK;

	for (channel = 0; status_capt != 0; channel++, status_capt >>= 1) {
		struct stm_capt_data *d =  pwm->capt_data[channel];

		/* Check if there a capt interrupt on */
		if (!(status_capt & 1))
			continue;

		/*
		 * Capture input:
		 *    _______                   _______
		 *   |       |                 |       |
		 * __|       |_________________|       |________
		 *   ^0      ^1                ^2
		 *
		 * Capture start by the first available rising edge
		 * When a capture event occurs, capture value (CPT_VALx)
		 * is stored, index incremented, capture edge changed.
		 *
		 * After the capture, if the index > 2, we have collected
		 * the necessary data so we signal the thread waiting for it
		 * and disable the capture by setting capture edge to none
		 *
		 */

		d->cnt[d->index] = readl(pwm->base + PWM_CPT_VAL(channel));
		(d->index)++;

		if (d->index > 2) {
			writeb(capt_edge_disabled,
			       pwm->base + PWM_CPT_EDGE(channel));
			wake_up(&(d->wait));
		} else {
			u16 reg = readb(pwm->base + PWM_CPT_EDGE(channel));
			reg = (reg ^ PWM_CPT_EDGE_MASK);
			writeb(reg, pwm->base + PWM_CPT_EDGE(channel));
		}
	}

	/* Just ACK everything */
	writew(PWM_INT_ACK_MASK, pwm->base + PWM_INT_ACK);

	return IRQ_HANDLED;
}

#ifdef CONFIG_OF
static void
stm_pwm_out_dt_get_pdata(struct platform_device *pdev,
			 struct device_node *pwm_out,
			 struct stm_plat_pwm_data  *data)
{
	struct device_node *node;

	for_each_child_of_node(pwm_out, node) {
		u32 channel;
		struct stm_plat_pwm_out_channel_config *channel_config;
		u32 initial_value;
		bool retain_hw_value;
		bool locked;

		if (!of_device_is_available(node))
			continue;

		if (of_property_read_u32(node, "reg", &channel)) {
			dev_err(&pdev->dev, "unable to read \"reg\" for %s\n",
				node->full_name);
			continue;
		}

		if (channel >= STM_PLAT_PWM_NUM_CHANNELS) {
			dev_err(&pdev->dev,
				"invalid channel index %d on %s\n",
				(unsigned int)channel, node->full_name);
			continue;
		}

		initial_value = 0;
		of_property_read_u32(node, "st,initial-value", &initial_value);
		retain_hw_value = of_property_read_bool(node,
							"st,retain-hw-value");
		locked = of_property_read_bool(node, "st,locked");

		channel_config = &data->out_channel_config[channel];

		channel_config->enabled = true;
		channel_config->initial_value = initial_value;
		channel_config->retain_hw_value = retain_hw_value;
		channel_config->locked = locked;

		data->out_pad_config[channel] =
			stm_of_get_pad_config_from_node(&pdev->dev,
							pwm_out,
							channel);
	}
}

static void
stm_pwm_capt_dt_get_pdata(struct platform_device *pdev,
			 struct device_node *capt_in,
			 struct stm_plat_pwm_data  *data)
{
	struct device_node *node;

	for_each_child_of_node(capt_in, node) {
		u32 channel;
		struct stm_plat_pwm_capt_channel_config *channel_config;

		if (!of_device_is_available(node))
			continue;

		if (of_property_read_u32(node, "reg", &channel)) {
			dev_err(&pdev->dev, "unable to read \"reg\" for %s\n",
				node->full_name);
			continue;
		}

		if (channel >= STM_PLAT_PWM_NUM_CHANNELS) {
			dev_err(&pdev->dev,
				"invalid channel index %d on %s\n",
				(unsigned int)channel, node->full_name);
			continue;
		}

		channel_config = &data->capt_channel_config[channel];
		channel_config->enabled = true;
		data->capt_pad_config[channel] =
			stm_of_get_pad_config_from_node(&pdev->dev,
							capt_in,
							channel);
	}
}

static struct stm_plat_pwm_data *
stm_pwm_dt_get_pdata(struct platform_device *pdev)
{
	struct stm_plat_pwm_data  *data;
	struct device_node *child;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return NULL;

	if (of_property_read_string(pdev->dev.of_node, "clk", &(data->clk)))
		dev_err(&pdev->dev, "unable to read \"clk\" for %s\n",
			pdev->dev.of_node->full_name);

	/* Get Platform data for PWM out */
	child = of_get_child_by_name(pdev->dev.of_node, "out");
	if (child && of_device_is_available(child))
		stm_pwm_out_dt_get_pdata(pdev, child, data);

	/* Get Platform data for PWM capture */
	child = of_get_child_by_name(pdev->dev.of_node, "capt");
	if (child && of_device_is_available(child))
		stm_pwm_capt_dt_get_pdata(pdev, child, data);

	return data;
}
#else
static void *stm_pwm_dt_get_pdata(struct platform_device *pdev)
{
	return NULL;
}
#endif

static int stm_pwm_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct stm_pwm *pwm;
	int irq;
	int err;

	pwm = devm_kzalloc(&pdev->dev, sizeof(*pwm), GFP_KERNEL);
	if (!pwm)
		return -ENOMEM;

	if (pdev->dev.of_node)
		pwm->platform_data = stm_pwm_dt_get_pdata(pdev);
	else
		pwm->platform_data = pdev->dev.platform_data;

	if (!pwm->platform_data) {
		dev_err(&pdev->dev, "No platform data found\n");
		return -ENODEV;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "irq configuration not found\n");
		return -ENODEV;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "No memory region in device\n");
		return -ENODEV;
	}

	pwm->mem = devm_request_mem_region(&pdev->dev,
			res->start, resource_size(res), "stm-pwm");
	if (pwm->mem == NULL) {
		dev_err(&pdev->dev, "failed to claim memory region\n");
		return -EBUSY;
	}

	pwm->base = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (pwm->base == NULL) {
		dev_err(&pdev->dev, "failed ioremap");
		return -EINVAL;
	}

	pwm->hwmon_dev = hwmon_device_register(&pdev->dev);
	if (IS_ERR(pwm->hwmon_dev)) {
		err = PTR_ERR(pwm->hwmon_dev);
		dev_err(&pdev->dev, "Class registration failed (%d)\n", err);
		return err;
	}

	platform_set_drvdata(pdev, pwm);
	dev_info(&pdev->dev, "registers at 0x%x, mapped to 0x%p\n",
		 res->start, pwm->base);

	err = stm_pwm_init(pdev, pwm);
	if (err)
		goto init_pwm_failed;

	err = sysfs_create_group(&pdev->dev.kobj, &stm_pwm_out_attr_group);
	if (err)
		goto sysfs_pwm_failed;

	err = sysfs_create_group(&pdev->dev.kobj, &stm_pwm_capt_attr_group);
	if (err)
		goto sysfs_capt_failed;

	err = devm_request_irq(&pdev->dev, irq, stm_pwm_interrupt,
			       0, pdev->name, (void *) pwm);
	if (err < 0)
		goto irq_pwm_failed;

	return err;

irq_pwm_failed:
	sysfs_remove_group(&pdev->dev.kobj, &stm_pwm_capt_attr_group);
sysfs_capt_failed:
	sysfs_remove_group(&pdev->dev.kobj, &stm_pwm_out_attr_group);
sysfs_pwm_failed:
init_pwm_failed:
	hwmon_device_unregister(pwm->hwmon_dev);
	return err;
}

static int stm_pwm_remove(struct platform_device *pdev)
{
	struct stm_pwm *pwm = platform_get_drvdata(pdev);

	if (pwm) {
		hwmon_device_unregister(pwm->hwmon_dev);
		sysfs_remove_group(&pdev->dev.kobj, &stm_pwm_out_attr_group);
		sysfs_remove_group(&pdev->dev.kobj, &stm_pwm_capt_attr_group);
		platform_set_drvdata(pdev, NULL);
	}
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int stm_pwm_suspend(struct device *dev)
{
	struct stm_pwm *pwm = dev_get_drvdata(dev);
	int channel;

	for (channel = 0; channel < STM_PLAT_PWM_NUM_CHANNELS; channel++) {
		if (!pwm->platform_data->out_channel_config[channel].enabled)
			continue;
		pwm->suspend_value[channel] =
			readb(pwm->base + PWM_OUT_VAL(channel));
	}

	return 0;
}

static int stm_pwm_restore(struct device *dev)
{
	struct stm_pwm *pwm = dev_get_drvdata(dev);
	int channel;

	stm_pwm_enable(pwm);

	for (channel = 0; channel < STM_PLAT_PWM_NUM_CHANNELS; channel++) {
		if (!pwm->platform_data->out_channel_config[channel].enabled)
			continue;
		writeb(pwm->suspend_value[channel],
		       pwm->base + PWM_OUT_VAL(channel));

		stm_pad_setup(pwm->out_pad_state[channel]);
	}

	for (channel = 0; channel < STM_PLAT_PWM_NUM_CHANNELS; channel++) {
		if (!pwm->platform_data->capt_channel_config[channel].enabled)
			continue;
		stm_pad_setup(pwm->capt_pad_state[channel]);
	}

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(stm_pwm_pm_ops, stm_pwm_suspend, stm_pwm_restore);

#ifdef CONFIG_OF
static struct of_device_id stm_pwm_match[] = {
	{
		.compatible = "st,pwm",
	},
	{},
};
MODULE_DEVICE_TABLE(of, stm_pwm_match);
#endif

static struct platform_driver stm_pwm_driver = {
	.driver = {
		.name		= "stm-pwm",
		.of_match_table = of_match_ptr(stm_pwm_match),
		.pm		= &stm_pwm_pm_ops,
	},
	.probe		= stm_pwm_probe,
	.remove		= stm_pwm_remove,
};

static int __init stm_pwm_module_init(void)
{
	return platform_driver_register(&stm_pwm_driver);
}

static void __exit stm_pwm_module_exit(void)
{
	platform_driver_unregister(&stm_pwm_driver);
}

module_init(stm_pwm_module_init);
module_exit(stm_pwm_module_exit);

MODULE_AUTHOR("Stuart Menefy <stuart.menefy@st.com>");
MODULE_DESCRIPTION("STMicroelectronics simple PWM driver");
MODULE_LICENSE("GPL");
