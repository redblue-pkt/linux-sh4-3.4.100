/*******************************************************************************
 *
 * File name   : clock-oslayer.h
 * Description : Low Level API - OS Specifics
 *
 * COPYRIGHT (C) 2009 STMicroelectronics - All Rights Reserved
 * This file is under the GPL 2 License.
 *
 ******************************************************************************/


#ifndef __CLKLLA_OSLAYER_H
#define __CLKLLA_OSLAYER_H

#include <linux/io.h>
#include <linux/stm/sysconf.h>
#include <linux/stm/gpio.h>
#include <linux/stm/pio.h>
#include <linux/stm/platform.h>
#include <linux/stm/soc.h>
#include <asm-generic/errno-base.h>

#define clk_t	struct clk

#define CLK_RATE_PROPAGATES		0
#define CLK_ALWAYS_ENABLED		(1 << 0)

/* Register access macros */
#define CLK_READ(addr)	  		ioread32((void *)addr)
#define CLK_WRITE(addr, val)		iowrite32(val, (void *)addr)

/*
 * SYSCONF registers management
 */

/* Sysconf UNIQUE numbering scheme */
#define SYSCONF_DECLAREU(num, lsb, msb)			\
	static struct sysconf_field *sys_##num##_##lsb##_##msb
#define SYSCONF_ASSIGNU(num, lsb, msb)		\
	sys_##num##_##lsb##_##msb = SYSCONF_CLAIMU(num, lsb, msb)
#define SYSCONF_READU(num, lsb, msb)		\
	sysconf_read(sys_##num##_##lsb##_##msb)
#define SYSCONF_WRITEU(num, lsb, msb, value)	\
	sysconf_write(sys_##num##_##lsb##_##msb, value)

/* OBSOLETE !! Use SYSCONF_DECLAREU instead */
#undef SYSCONF	/* Clash with Linux standard */
#define SYSCONF(type, num, lsb, msb)			\
	static struct sysconf_field *sys_##type##_##num##_##lsb##_##msb

/* OBSOLETE !! Use SYSCONF_CLAIMU instead */
#define SYSCONF_CLAIM(type, num, lsb, msb)		\
	sys_##type##_##num##_##lsb##_##msb =		\
		sysconf_claim(type, num, lsb, msb, "Clk lla")

/* OBSOLETE !! Use SYSCONF_CLAIMU instead */
extern struct sysconf_field *(*platform_sys_claim)(int nr, int lsb, int msb);

/* OBSOLETE !! Use SYSCONF_ASSIGNU instead */
#define call_platform_sys_claim(_nr, _lsb, _msb)		\
	sys_0_##_nr##_##_lsb##_##_msb =				\
		platform_sys_claim(_nr, _lsb, _msb)

#define SYSCONF_READ(type, num, lsb, msb)		\
	sysconf_read(sys_##type##_##num##_##lsb##_##msb)

#define SYSCONF_WRITE(type, num, lsb, msb, value)	\
	sysconf_write(sys_##type##_##num##_##lsb##_##msb, value)


static inline
void PIO_SET_MODE(unsigned long bank, unsigned long line, long mode)
{
	static int pin = -ENOSYS;

	if (pin == -ENOSYS)
		gpio_request(stm_gpio(bank, line), "Clk Observer");

	if (pin >= 0)
		stm_gpio_direction(pin, mode);
}

#define _CLK_OPS(_name, _desc, _init, _setparent, _setfreq, _recalc,	\
		     _enable, _disable, _observe, _measure, _obspoint)	\
static struct clk_ops  _name = {					\
	.init = _init,							\
	.set_parent = _setparent,					\
	.set_rate = _setfreq,						\
	.recalc = _recalc,						\
	.enable = _enable,						\
	.disable = _disable,						\
	.observe = _observe,						\
	.get_measure = _measure,					\
}

/* Clock operation registration macro (used by clock-xxxx.c) */
#define _CLK_OPS2(_name, _desc, _init, _setparent, _setfreq, _recalc,	\
	_enable, _disable, _observe, _measure, _obspoint, \
	_obspoint2, _observe2)		\
static struct clk_ops  _name = {					\
	.init = _init,							\
	.set_parent = _setparent,					\
	.set_rate = _setfreq,						\
	.recalc = _recalc,						\
	.enable = _enable,						\
	.disable = _disable,						\
	.observe = _observe,						\
	.get_measure = _measure,					\
}

#define _CLK(_id, _ops, _nominal, _flags) 				\
[_id] = (clk_t){ .name = #_id,						\
		 .id = (_id),						\
		 .ops = (_ops),						\
		 .flags = (_flags),					\
}

#define _CLK_P(_id, _ops, _nominal, _flags, _parent) 			\
[_id] = (clk_t){ .name = #_id,  \
		 .id = (_id),						\
		 .ops = (_ops), \
		 .flags = (_flags),					\
		 .parent = (_parent),					\
}

#define _CLK_F(_id, _rate)	 					\
[_id] = (clk_t) {							\
		.name = #_id,  						\
		.id = (_id),						\
		.rate = (_rate),					\
}

#define _CLK_FIXED(_id, _rate, _flags)					\
[_id] = (clk_t) {							\
		.name = #_id,						\
		.id = (_id),						\
		.rate = (_rate),					\
		.flags = (_flags),					\
}

/* Low level API errors */
enum clk_err {
	CLK_ERR_NONE = 0,
	CLK_ERR_FEATURE_NOT_SUPPORTED = -EPERM,
	CLK_ERR_BAD_PARAMETER = -EINVAL,
	CLK_ERR_INTERNAL = -EFAULT /* Internal & fatal error */
};

/* Retrieving chip cut (major) */
static inline unsigned long chip_major_version(void)
{
	return stm_soc_version_major();
}

/* Produce a delay in ms */
#define CLK_DELAYMS(delay)	mdelay(delay)
#define __mdelay(x)		mdelay(x)

/*
 * Runtime & debug infos
 */

/* Clock debug info at runtime ONLY */
/* #define DEBUG */

/* Clock debug info at early boot stage ONLY.
 * As early_printk uses the bootconsole that will be disable as soon as the
 * system console will be configured, all messages printed through early_printk
 * will not be visible.
 * It is possible to avoid de-registering the bootconsole by kernel parameter
 * 'keep_bootcon'
 * This should be avoid indeed, and DEBUG_EARLY should be enabled just if
 * something is going wrong in clk-lla in the early stage.
 */

/* #define DEBUG_EARLY */

#ifdef DEBUG
/* DEBUG take over DEBUG_EARLY in case both are defined */
#define clk_debug(fmt, args ...) pr_info("stm-clk: " fmt, ## args)
#elif defined DEBUG_EARLY && defined CONFIG_EARLY_PRINTK
#define clk_debug(fmt, args ...) early_printk("stm-clk: " fmt, ## args)
#else
#define clk_debug(...)
#endif

#endif /* #ifndef __CLKLLA_OSLAYER_H */
