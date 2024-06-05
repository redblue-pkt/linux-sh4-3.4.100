/*
 * This driver implements a RTC using the Low Power Timer in
 * the Low Power Controller (LPC) in some STMicroelectronics devices.
 *
 * See ADCS 2001950 for more details on the hardware.
 *
 * Copyright (C) 2009 STMicroelectronics Limited
 * Copyright (C) 2010 STMicroelectronics Limited
 * Author: Stuart Menefy <stuart.menefy@@st.com>
 * Author: Francesco Virlinzi <francesco.virlinzi@st.com>
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/rtc.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/stm/platform.h>

#define DRV_NAME "stm-rtc"
#define DRV_VERSION "0.1"

/* Low Power Timer */
#define LPC_LPT_LSB_OFF		0x400
#define LPC_LPT_MSB_OFF		0x404
#define LPC_LPT_START_OFF	0x408
/* Low Power Alarm */
#define LPC_LPA_LSB_OFF		0x410
#define LPC_LPA_MSB_OFF		0x414
#define LPC_LPA_START_OFF	0x418
/* LPC as WDT */
#define LPC_WDT_OFF		0x510
#define LPC_WDT_FLAG_OFF	0x514


/*
 * LPC has _only_ one source interrupt therefore the
 * periodic-one_shot interrupts are mutually exclusive
 */
#define STM_LPC_MOVE_AVAILABLE	0x0
#define STM_LPC_MODE_PERIODIC	0x1
#define STM_LPC_MODE_ONESHOT	0x2

struct stm_rtc {
	struct rtc_device *rtc_dev;
	void __iomem *ioaddr;
	struct resource *res;
	struct clk *clk;
	unsigned char mode; /* periodic vs oneshot */
	short irq;
	int irq_enabled:1;
	unsigned long periodic_tick; /* periodic frequency in tick */
	spinlock_t lock;
	struct rtc_wkalrm alarm;
	int need_wdt_reset;
};

static void stm_rtc_set_hw_alarm(struct stm_rtc *rtc,
		unsigned long msb, unsigned long  lsb)
{

	if (rtc->need_wdt_reset)
		writel(1, rtc->ioaddr + LPC_WDT_OFF);

	writel(msb, rtc->ioaddr + LPC_LPA_MSB_OFF);
	writel(lsb, rtc->ioaddr + LPC_LPA_LSB_OFF);
	writel(1, rtc->ioaddr + LPC_LPA_START_OFF);

	if (rtc->need_wdt_reset)
		writel(0, rtc->ioaddr + LPC_WDT_OFF);

}

static irqreturn_t stm_rtc_irq(int this_irq, void *data)
{
	struct stm_rtc *rtc = (struct stm_rtc *)data;

	switch (rtc->mode) {
	case STM_LPC_MODE_PERIODIC:
		/* reload the value... */
		spin_lock(&rtc->lock);
		stm_rtc_set_hw_alarm(rtc, 0, rtc->periodic_tick);
		spin_unlock(&rtc->lock);

	case STM_LPC_MODE_ONESHOT:
		rtc_update_irq(rtc->rtc_dev, 1,
			(rtc->mode == STM_LPC_MODE_PERIODIC ? RTC_PF : RTC_AF));
		break;
	default:
		break;
	}

	return IRQ_HANDLED;
}


static int stm_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct stm_rtc *rtc = dev_get_drvdata(dev);
	unsigned long long lpt;
	unsigned long lpt_lsb, lpt_msb;

	spin_lock(&rtc->lock);

	do {
		lpt_msb = readl(rtc->ioaddr + LPC_LPT_MSB_OFF);
		lpt_lsb = readl(rtc->ioaddr + LPC_LPT_LSB_OFF);
	} while (readl(rtc->ioaddr + LPC_LPT_MSB_OFF) != lpt_msb);

	lpt = ((unsigned long long)lpt_msb << 32) | lpt_lsb;
	do_div(lpt, clk_get_rate(rtc->clk));
	rtc_time_to_tm(lpt, tm);

	spin_unlock(&rtc->lock);

	return 0;
}

static int stm_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct stm_rtc *rtc = dev_get_drvdata(dev);
	unsigned long secs, flags;
	int ret;
	unsigned long long lpt;

	ret = rtc_tm_to_time(tm, &secs);
	if (ret != 0)
		return ret;

	lpt = (unsigned long long)secs * clk_get_rate(rtc->clk);
	spin_lock_irqsave(&rtc->lock, flags);
	writel(lpt >> 32, rtc->ioaddr + LPC_LPT_MSB_OFF);
	writel(lpt, rtc->ioaddr + LPC_LPT_LSB_OFF);
	writel(1, rtc->ioaddr + LPC_LPT_START_OFF);
	spin_unlock_irqrestore(&rtc->lock, flags);

	return 0;
}

static int stm_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *wkalrm)
{
	struct stm_rtc *rtc = dev_get_drvdata(dev);
	spin_lock(&rtc->lock);
	memcpy(wkalrm, &rtc->alarm, sizeof(struct rtc_wkalrm));
	spin_unlock(&rtc->lock);
	return 0;
}

static int stm_rtc_ioctl(struct device *dev,
			unsigned int cmd, unsigned long arg)
{
	struct stm_rtc *rtc = dev_get_drvdata(dev);
	unsigned int ret = 0;

	spin_lock(&rtc->lock);
	switch (cmd) {
	case RTC_AIE_OFF:
	case RTC_PIE_OFF:
		if ((rtc->mode == STM_LPC_MODE_PERIODIC &&
		     cmd == RTC_AIE_OFF) ||
		    (rtc->mode == STM_LPC_MODE_ONESHOT &&
		     cmd == RTC_PIE_OFF)) {
			ret = -EBUSY;
			break;
		}
		if (rtc->irq_enabled)
			disable_irq(rtc->irq);
		rtc->irq_enabled = 0;
		rtc->mode = STM_LPC_MOVE_AVAILABLE;
		if (cmd == RTC_AIE_OFF)
			device_set_wakeup_enable(dev, 0);
		break;
	case RTC_AIE_ON:
	case RTC_PIE_ON:
		if (rtc->mode != STM_LPC_MOVE_AVAILABLE) {
			ret = -EBUSY;
			break;
		}
		rtc->mode = (cmd == RTC_PIE_ON ? STM_LPC_MODE_PERIODIC :
						 STM_LPC_MODE_ONESHOT);

		device_set_wakeup_enable(dev, cmd == RTC_PIE_ON ? 0 : 1);

		if (cmd == RTC_PIE_ON)
			stm_rtc_set_hw_alarm(rtc, 0, rtc->periodic_tick);

		if (!rtc->irq_enabled)
			enable_irq(rtc->irq);
		rtc->irq_enabled = 1;

		break;
	default:
		ret = -ENOIOCTLCMD;
	}
	spin_unlock(&rtc->lock);

	return ret;
}

static int stm_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *t)
{
	struct stm_rtc *rtc = dev_get_drvdata(dev);
	struct rtc_time now;
	unsigned long now_secs;
	unsigned long alarm_secs;
	unsigned long long lpa;
	int ret = 0;

	stm_rtc_read_time(dev, &now);
	rtc_tm_to_time(&now, &now_secs);
	rtc_tm_to_time(&t->time, &alarm_secs);

	if (now_secs > alarm_secs)
		return -EINVAL; /* invalid alarm time */

	memcpy(&rtc->alarm, t, sizeof(struct rtc_wkalrm));

	alarm_secs -= now_secs; /* now many secs to fire */
	lpa = (unsigned long long)alarm_secs * clk_get_rate(rtc->clk);

	spin_lock(&rtc->lock);
	stm_rtc_set_hw_alarm(rtc, lpa >> 32, lpa);
	spin_unlock(&rtc->lock);

	if (t->enabled)
		ret = stm_rtc_ioctl(dev, RTC_AIE_ON, 0);

	return ret;
}


static struct rtc_class_ops stm_rtc_ops = {
	.ioctl = stm_rtc_ioctl,
	.read_time = stm_rtc_read_time,
	.set_time = stm_rtc_set_time,
	.read_alarm = stm_rtc_read_alarm,
	.set_alarm = stm_rtc_set_alarm,
};

#ifdef CONFIG_PM
static int stm_rtc_suspend(struct device *dev)
{
	struct stm_rtc *rtc = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
		return 0;

	writel(1, rtc->ioaddr + LPC_WDT_OFF);
	writel(0, rtc->ioaddr + LPC_LPA_START_OFF);
	writel(0, rtc->ioaddr + LPC_WDT_OFF);

	return 0;
}

static int stm_rtc_resume(struct device *dev)
{
	struct stm_rtc *rtc = dev_get_drvdata(dev);

	rtc_alarm_irq_enable(rtc->rtc_dev, 0);

	 if (device_may_wakeup(dev))
		rtc_update_irq(rtc->rtc_dev, 1, RTC_AF);

	if (rtc->need_wdt_reset) {
		writel(0, rtc->ioaddr + LPC_LPA_MSB_OFF);
		writel(0, rtc->ioaddr + LPC_LPA_LSB_OFF);
		writel(1, rtc->ioaddr + LPC_WDT_OFF);
		writel(1, rtc->ioaddr + LPC_LPA_START_OFF);
		writel(0, rtc->ioaddr + LPC_WDT_OFF);
	}

	return 0;
}

static struct dev_pm_ops stm_rtc_pm_ops = {
	.suspend = stm_rtc_suspend,  /* on standby/memstandby */
	.resume = stm_rtc_resume,
};
#else
static struct dev_pm_ops stm_rtc_pm_ops;
#endif

static void *stm_rtc_get_pdata(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct stm_plat_rtc_lpc *data;
	if (!np)
		return pdev->dev.platform_data;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	of_property_read_string(np, "clk-id", (const char **)&data->clk_id);
	of_property_read_u32(np, "clk-rate", &data->force_clk_rate);

	data->no_hw_req = of_property_read_bool(np, "no-hw-req");
	data->need_wdt_reset = of_property_read_bool(np, "need-wdt-reset");
	if (of_property_read_bool(np, "irq-edge-rising"))
		data->irq_edge_level = IRQ_TYPE_EDGE_RISING;
	if (of_property_read_bool(np, "irq-edge-falling"))
		data->irq_edge_level = IRQ_TYPE_EDGE_FALLING;

	return data;
}

static int __devinit stm_rtc_probe(struct platform_device *pdev)
{
	struct stm_plat_rtc_lpc *plat_data;
	struct stm_rtc *rtc;
	struct resource *res;
	int size;
	int ret = 0;
	struct rtc_time tm_check;

	rtc = kzalloc(sizeof(struct stm_rtc), GFP_KERNEL);
	if (unlikely(!rtc))
		return -ENOMEM;

	spin_lock_init(&rtc->lock);
	plat_data = stm_rtc_get_pdata(pdev);
	if (unlikely(plat_data == NULL)) {
		dev_err(&pdev->dev, "No platform data\n");
		ret = -ENOENT;
		goto err_badres;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (unlikely(res == NULL)) {
		dev_err(&pdev->dev, "No IO resource\n");
		ret = -ENOENT;
		goto err_badres;
	}

	size = res->end - res->start + 1;
	rtc->res = request_mem_region(res->start, size, pdev->name);
	if (!rtc->res) {
		ret = -EBUSY;
		goto err_badres;
	}

	rtc->ioaddr = ioremap_nocache(res->start, size);
	if (!rtc->ioaddr) {
		ret = -EINVAL;
		goto err_badmap;
	}

	if (plat_data->clk_id)
		rtc->clk = clk_get(&pdev->dev, plat_data->clk_id);
	else
		rtc->clk = clk_get(&pdev->dev, "lpc_clk");
	if (IS_ERR(rtc->clk)) {
		pr_err("clk lpc_clk not found\n");
		ret = PTR_ERR(rtc->clk);
		goto err_badreg;
	}
	clk_prepare_enable(rtc->clk);

	if (plat_data->force_clk_rate)
		clk_set_rate(rtc->clk, plat_data->force_clk_rate);

	pr_debug("%s: clk @ %lu\n",
		DRV_NAME, clk_get_rate(rtc->clk));

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		pr_err("%s Request irq %d not done\n",
			__func__, res->start);
		return -ENODEV;
	}

	rtc->need_wdt_reset = plat_data->need_wdt_reset;
	rtc->irq = res->start;
	irq_set_irq_type(rtc->irq, plat_data->irq_edge_level);
	enable_irq_wake(rtc->irq);
	if (devm_request_irq(&pdev->dev, rtc->irq, stm_rtc_irq,
		IRQF_DISABLED, DRV_NAME, rtc) < 0){
		pr_err("%s: Request irq not done\n", __func__);
		return -ENODEV;
	}
	disable_irq(rtc->irq);

	device_set_wakeup_capable(&pdev->dev, 1);

	platform_set_drvdata(pdev, rtc);

	/*
	 * The RTC-LPC is able to manage date.year > 2038
	 * but currently the kernel can not manage this date!
	 * If the RTC-LPC has a date.year > 2038 then
	 * it's set to the epoch "Jan 1st 2000"
	 */
	stm_rtc_read_time(&pdev->dev, &tm_check);

	if (tm_check.tm_year >=  (2038 - 1900)) {
		memset(&tm_check, 0, sizeof(tm_check));
		tm_check.tm_year = 100;
		/*
		 * FIXME:
		 *   the 'tm_check.tm_mday' should be set to zero but the func-
		 *   tions rtc_tm_to_time and rtc_time_to_time aren't coherent.
		 */
		tm_check.tm_mday = 1;
		stm_rtc_set_time(&pdev->dev, &tm_check);
	}

	rtc->rtc_dev = rtc_device_register(DRV_NAME, &pdev->dev,
					   &stm_rtc_ops, THIS_MODULE);
	if (IS_ERR(rtc->rtc_dev)) {
		ret = PTR_ERR(rtc->rtc_dev);
		goto err_badreg;
	}

	return ret;

err_badreg:
	iounmap(rtc->ioaddr);
err_badmap:
	release_resource(rtc->res);
err_badres:
	kfree(rtc);

	platform_set_drvdata(pdev, NULL);
	return ret;
}

static int __devexit stm_rtc_remove(struct platform_device *pdev)
{
	struct stm_rtc *rtc = platform_get_drvdata(pdev);

	if (likely(rtc->rtc_dev))
		rtc_device_unregister(rtc->rtc_dev);

	iounmap(rtc->ioaddr);
	release_resource(rtc->res);
	platform_set_drvdata(pdev, NULL);
	kfree(rtc);

	return 0;
}

#ifdef CONFIG_OF
static struct of_device_id stm_rtc_match[] = {
	{
		.compatible = "st,rtc",
	},
	{},
};

MODULE_DEVICE_TABLE(of, stm_rtc_match);
#endif

static struct platform_driver stm_rtc_platform_driver = {
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
		.pm = &stm_rtc_pm_ops,
		.of_match_table = of_match_ptr(stm_rtc_match),
	},
	.probe = stm_rtc_probe,
	.remove = __devexit_p(stm_rtc_remove),
};

static int __init stm_rtc_init(void)
{
	return platform_driver_register(&stm_rtc_platform_driver);
}

static void __exit stm_rtc_exit(void)
{
	platform_driver_unregister(&stm_rtc_platform_driver);
}

module_init(stm_rtc_init);
module_exit(stm_rtc_exit);

MODULE_DESCRIPTION("STMicroelectronics LPC RTC driver");
MODULE_VERSION(DRV_VERSION);
MODULE_AUTHOR("Stuart Menefy <stuart.menefy@st.com>");
MODULE_LICENSE("GPL");
