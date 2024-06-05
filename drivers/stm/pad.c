/*
 * (c) 2010 STMicroelectronics Limited
 *
 * Author: Pawel Moll <pawel.moll@st.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */



#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/bootmem.h>
#include <linux/bug.h>
#include <linux/debugfs.h>
#include <linux/gpio.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/seq_file.h>
#include <linux/stm/pad.h>
#include <linux/stm/sysconf.h>
#include <linux/module.h>
#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/stm/pio-control.h>
#endif



/* Internal memory allocation (may be used very early, when
 * no kmalloc() is possible yet...) */

#define STM_PAD_STATIC_BUFFER_SIZE 1024

static unsigned char stm_pad_static_buffer[STM_PAD_STATIC_BUFFER_SIZE];
static unsigned char *stm_pad_static_buffer_pointer = stm_pad_static_buffer;
static int stm_pad_static_buffer_avail = sizeof(stm_pad_static_buffer);

static struct stm_pad_gpio *stm_pad_find_gpio(struct stm_pad_config *config,
		const char *name);

static void *stm_pad_alloc(int size)
{
	void *result = NULL;

	size = ALIGN(size, 4);

	if (stm_pad_static_buffer_avail >= size) {
		result = stm_pad_static_buffer_pointer;
		stm_pad_static_buffer_avail -= size;
		stm_pad_static_buffer_pointer += size;
	} else {
		static int notified;

		if (!notified) {
			pr_debug("stm_pad: Out of static buffer!\n");
			notified = 1;
		}

		result = kzalloc(size, GFP_KERNEL);
	}

	return result;
}

static void stm_pad_free(void *addr)
{
	if (addr >= (void *)stm_pad_static_buffer &&
			addr < (void *)(stm_pad_static_buffer +
			STM_PAD_STATIC_BUFFER_SIZE))
		return;

	kfree(addr);
}



/* Pads interface implementation */

static int stm_pad_initialized;

struct stm_pad_state {
	struct list_head list;
	const char *owner;
	struct stm_pad_config *config;
	struct sysconf_field *sysconf_fields[0]; /* To be expanded */
};
static LIST_HEAD(stm_pad_list);

static enum {
	stm_pad_gpio_unused = 0,
	stm_pad_gpio_normal_gpio,
	stm_pad_gpio_claimed,
	stm_pad_gpio_claimed_to_be_requested,
	stm_pad_gpio_claimed_requested,
} *stm_pad_gpios;
static int stm_pad_gpios_num;

static DEFINE_MUTEX(stm_pad_mutex);

static int stm_pad_gpio_function_in;
static int stm_pad_gpio_function_out;

static const struct stm_pad_ops *stm_pad_gpio_ops;


int __init stm_pad_init(int gpios_num,
			int gpio_function_in, int gpio_function_out,
			const struct stm_pad_ops *gpio_ops)
{
	BUG_ON(!gpio_ops);

	stm_pad_gpios = alloc_bootmem(sizeof(*stm_pad_gpios) * gpios_num);
	stm_pad_gpios_num = gpios_num;
	stm_pad_gpio_function_in = gpio_function_in;
	stm_pad_gpio_function_out = gpio_function_out;
	stm_pad_gpio_ops = gpio_ops;

	stm_pad_initialized = 1;

	return 0;
}

void stm_pad_setup(struct stm_pad_state *state)
{
	struct stm_pad_config *config = state->config;
	int i;

	mutex_lock(&stm_pad_mutex);

	for (i = 0; i < config->gpios_num; i++) {
		struct stm_pad_gpio *pad_gpio = &config->gpios[i];
		unsigned gpio = pad_gpio->gpio;

		if (pad_gpio->direction == stm_pad_gpio_direction_ignored)
			continue;

		stm_pad_gpio_ops->gpio_config(gpio, pad_gpio->direction,
			pad_gpio->function, pad_gpio->priv);
	}

	for (i = 0; i < config->sysconfs_num; i++) {
		struct stm_pad_sysconf *sysconf = &config->sysconfs[i];
		sysconf_write(state->sysconf_fields[i], sysconf->value);
	}

	mutex_unlock(&stm_pad_mutex);
}
EXPORT_SYMBOL(stm_pad_setup);

static int __stm_pad_claim(struct stm_pad_config *config,
		struct stm_pad_state *state, const char *owner)
{
	int i;

	BUG_ON(!stm_pad_initialized);
	WARN_ON(!owner);

	mutex_lock(&stm_pad_mutex);

	state->owner = owner;
	state->config = config;

	for (i = 0; i < config->gpios_num; i++) {
		struct stm_pad_gpio *pad_gpio = &config->gpios[i];
		unsigned gpio = pad_gpio->gpio;

		if (pad_gpio->direction == stm_pad_gpio_direction_ignored)
			continue;

		BUG_ON(pad_gpio->direction > stm_pad_gpio_direction_ignored ||
			pad_gpio->direction < stm_pad_gpio_direction_unknown);

		if (stm_pad_gpios[gpio] != stm_pad_gpio_unused)
			goto error_gpios;

		stm_pad_gpios[gpio] = stm_pad_gpio_claimed;

		if (stm_pad_gpio_ops->gpio_config(gpio, pad_gpio->direction,
				pad_gpio->function, pad_gpio->priv) != 0) {
			i++; /* Current GPIO must be released as well... */
			goto error_gpios;
		}
	}

	for (i = 0; i < config->sysconfs_num; i++) {
		struct stm_pad_sysconf *sysconf = &config->sysconfs[i];

		state->sysconf_fields[i] = sysconf_claim(sysconf->regtype,
				sysconf->regnum, sysconf->lsb, sysconf->msb,
				owner);
		if (!state->sysconf_fields[i])
			goto error_sysconfs;
		BUG_ON(sysconf->value < 0);
		sysconf_write(state->sysconf_fields[i], sysconf->value);
	}

	list_add(&state->list, &stm_pad_list);

	mutex_unlock(&stm_pad_mutex);

	if (config->custom_claim &&
			config->custom_claim(state, config->custom_priv) != 0)
		goto error_custom;

	return 0;

error_custom:
	mutex_lock(&stm_pad_mutex);

	list_del(&state->list);

error_sysconfs:
	for (i = 0; i < config->sysconfs_num; i++)
		if (state->sysconf_fields[i])
			sysconf_release(state->sysconf_fields[i]);
		else
			break;
	i = config->gpios_num;
error_gpios:
	while (i--)
		stm_pad_gpios[config->gpios[i].gpio] = stm_pad_gpio_unused;

	mutex_unlock(&stm_pad_mutex);

	return -EBUSY;
}

static int __stm_pad_release(struct stm_pad_state *state)
{
	struct stm_pad_config *config = state->config;
	int i;

	if (config->custom_release)
		config->custom_release(state, config->custom_priv);

	mutex_lock(&stm_pad_mutex);

	list_del(&state->list);

	for (i = 0; i < config->sysconfs_num; i++)
		sysconf_release(state->sysconf_fields[i]);

	for (i = 0; i < config->gpios_num; i++) {
		unsigned gpio = config->gpios[i].gpio;
		if (config->gpios[i].direction ==
			stm_pad_gpio_direction_ignored)
				continue;

		BUG_ON(stm_pad_gpios[gpio] != stm_pad_gpio_claimed);
		stm_pad_gpios[gpio] = stm_pad_gpio_unused;
	}

	mutex_unlock(&stm_pad_mutex);

	return 0;
}



struct stm_pad_state *stm_pad_claim(struct stm_pad_config *config,
		const char *owner)
{
	struct stm_pad_state *state = kzalloc(sizeof(*state) +
			sizeof(*state->sysconf_fields) * config->sysconfs_num,
			GFP_KERNEL);

	BUG_ON(!config);
	BUG_ON(!owner);

	if (state && __stm_pad_claim(config, state, owner) != 0) {
		kfree(state);
		state = NULL;
	}

	return state;
}
EXPORT_SYMBOL(stm_pad_claim);

void stm_pad_release(struct stm_pad_state *state)
{
	BUG_ON(!state);

	__stm_pad_release(state);

	stm_pad_free(state);
}
EXPORT_SYMBOL(stm_pad_release);




static void stm_pad_devres_release(struct device *dev, void *res)
{
	struct stm_pad_state *state = res;

	__stm_pad_release(state);
}

static int stm_pad_devres_match(struct device *dev, void *res, void *data)
{
	struct stm_pad_state *state = res, *match = data;

	return state == match;
}

struct stm_pad_state *devm_stm_pad_claim(struct device *dev,
	struct stm_pad_config *config, const char *owner)
{
	struct stm_pad_state *state = devres_alloc(stm_pad_devres_release,
			sizeof(*state) + sizeof(*state->sysconf_fields) *
			config->sysconfs_num, GFP_KERNEL);

	BUG_ON(!config);
	BUG_ON(!owner);

	if (state) {
		if (__stm_pad_claim(config, state, owner) == 0) {
			devres_add(dev, state);
		} else {
			devres_free(state);
			state = NULL;
		}
	}

	return state;
}
EXPORT_SYMBOL(devm_stm_pad_claim);

void devm_stm_pad_release(struct device *dev, struct stm_pad_state *state)
{
	int err;

	__stm_pad_release(state);

	err = devres_destroy(dev, stm_pad_devres_release,
			stm_pad_devres_match, state);
	WARN_ON(err);
}
EXPORT_SYMBOL(devm_stm_pad_release);



int stm_pad_update_gpio(struct stm_pad_state *state, const char* name,
		enum stm_pad_gpio_direction direction,
		int out_value, int function, void *priv)
{
	struct stm_pad_gpio *pad_gpio = stm_pad_find_gpio(state->config, name);
	int result;

	if (!pad_gpio)
		return -EINVAL;

	mutex_lock(&stm_pad_mutex);

	if ((stm_pad_gpios[pad_gpio->gpio] != stm_pad_gpio_claimed) ||
	    (pad_gpio->direction == stm_pad_gpio_direction_ignored)) {
		result = -EINVAL;
		goto out_unlock;
	}

	if (direction == stm_pad_gpio_direction_ignored)
		direction = pad_gpio->direction;

	if (function < 0)
		function = pad_gpio->function;

	if (!priv)
		priv = pad_gpio->priv;

	result = stm_pad_gpio_ops->gpio_config(pad_gpio->gpio, direction,
			function, priv);

	if (result == 0) {
		pad_gpio->direction = direction;
		pad_gpio->out_value = out_value;
		pad_gpio->function = function;
		pad_gpio->priv = priv;
	}

out_unlock:
	mutex_unlock(&stm_pad_mutex);

	return result;
}
EXPORT_SYMBOL(stm_pad_update_gpio);



/* Private interface for GPIO driver */

int stm_pad_claim_gpio(unsigned gpio)
{
	int result = 0;

	BUG_ON(!stm_pad_initialized);
	BUG_ON(gpio > stm_pad_gpios_num);

	mutex_lock(&stm_pad_mutex);

	switch (stm_pad_gpios[gpio]) {
	case stm_pad_gpio_unused:
		stm_pad_gpios[gpio] = stm_pad_gpio_normal_gpio;
		break;
	case stm_pad_gpio_claimed_to_be_requested:
		stm_pad_gpios[gpio] = stm_pad_gpio_claimed_requested;
		break;
	case stm_pad_gpio_normal_gpio:
	case stm_pad_gpio_claimed:
	case stm_pad_gpio_claimed_requested:
		result = -EBUSY;
		break;
	default:
		result = -EINVAL;
		BUG();
		break;
	}

	mutex_unlock(&stm_pad_mutex);

	return result;
}

void stm_pad_configure_gpio(unsigned gpio, unsigned direction)
{
	switch (direction) {
	case STM_GPIO_DIRECTION_IN:
		stm_pad_gpio_ops->gpio_config(gpio, stm_pad_gpio_direction_in,
				    stm_pad_gpio_function_in, NULL);
		break;
	case STM_GPIO_DIRECTION_OUT:
		stm_pad_gpio_ops->gpio_config(gpio, stm_pad_gpio_direction_out,
				    stm_pad_gpio_function_out, NULL);
		break;
	default:
		BUG();
		break;
	}
}

void stm_pad_release_gpio(unsigned gpio)
{
	BUG_ON(gpio > stm_pad_gpios_num);

	mutex_lock(&stm_pad_mutex);

	switch (stm_pad_gpios[gpio]) {
	case stm_pad_gpio_normal_gpio:
		stm_pad_gpios[gpio] = stm_pad_gpio_unused;
		break;
	case stm_pad_gpio_claimed_requested:
		stm_pad_gpios[gpio] = stm_pad_gpio_claimed;
		break;
	default:
		BUG();
		break;

	}

	mutex_unlock(&stm_pad_mutex);
}

static int stm_pad_find_gpio_struct(unsigned gpio,
	struct stm_pad_state **state_p,
	struct stm_pad_gpio **pad_gpio_p)
{
	struct stm_pad_state *state;

	BUG_ON(gpio > stm_pad_gpios_num);

	list_for_each_entry(state, &stm_pad_list, list) {
		struct stm_pad_config *config = state->config;
		int i;

		for (i = 0; i < config->gpios_num; i++) {
			struct stm_pad_gpio *pad_gpio = &config->gpios[i];

			if (pad_gpio->direction ==
			    stm_pad_gpio_direction_ignored)
				continue;

			if (pad_gpio->gpio == gpio) {
				*state_p = state;
				*pad_gpio_p = pad_gpio;
				return 0;
			}
		}
	}

	return -ENOENT;
}

const char *stm_pad_get_gpio_owner(unsigned gpio)
{
	struct stm_pad_state *state;
	struct stm_pad_gpio *pad_gpio;
	int result;

	mutex_lock(&stm_pad_mutex);
	result = stm_pad_find_gpio_struct(gpio, &state, &pad_gpio);
	mutex_unlock(&stm_pad_mutex);

	if (!result)
		return state->owner;
	else
		return "?";
}


/* GPIO interface */

static int __stm_pad_gpio_request(struct stm_pad_gpio *pad_gpio,
		const char *owner)
{
	int result = -EBUSY;
	unsigned gpio = pad_gpio->gpio;

	BUG_ON(!stm_pad_initialized);

	mutex_lock(&stm_pad_mutex);

	if (stm_pad_gpios[gpio] == stm_pad_gpio_claimed) {
		int err;

		stm_pad_gpios[gpio] = stm_pad_gpio_claimed_to_be_requested;

		mutex_unlock(&stm_pad_mutex);

		err = gpio_request(gpio, owner);

		mutex_lock(&stm_pad_mutex);

		if (err != 0)
			stm_pad_gpios[gpio] = stm_pad_gpio_claimed;

		result = err;
	}

	mutex_unlock(&stm_pad_mutex);

	return result;
}

unsigned stm_pad_gpio_request_input(struct stm_pad_state *state,
		const char *name)
{
	unsigned result = STM_GPIO_INVALID;
	struct stm_pad_gpio *pad_gpio = stm_pad_find_gpio(state->config, name);

	if (pad_gpio && __stm_pad_gpio_request(pad_gpio, state->owner) == 0) {
		gpio_direction_input(pad_gpio->gpio);
		stm_pad_gpio_ops->gpio_config(pad_gpio->gpio,
				    stm_pad_gpio_direction_in,
				    stm_pad_gpio_function_in, pad_gpio->priv);

		result = pad_gpio->gpio;
	}

	return result;
}
EXPORT_SYMBOL(stm_pad_gpio_request_input);

unsigned stm_pad_gpio_request_output(struct stm_pad_state *state,
		const char *name, int value)
{
	unsigned result = STM_GPIO_INVALID;
	struct stm_pad_gpio *pad_gpio = stm_pad_find_gpio(state->config, name);

	if (pad_gpio && __stm_pad_gpio_request(pad_gpio, state->owner) == 0) {
		BUG_ON(value < 0);
		BUG_ON(value > 1);
		gpio_direction_output(pad_gpio->gpio, value);
		stm_pad_gpio_ops->gpio_config(pad_gpio->gpio,
				    stm_pad_gpio_direction_out,
				    stm_pad_gpio_function_out, pad_gpio->priv);

		result = pad_gpio->gpio;
	}

	return result;
}
EXPORT_SYMBOL(stm_pad_gpio_request_output);

void stm_pad_gpio_free(struct stm_pad_state *state, unsigned gpio)
{
	struct stm_pad_config *config;
	int i;

	BUG_ON(!state);
	config = state->config;

	for (i = 0; i < config->gpios_num; i++) {
		struct stm_pad_gpio *pad_gpio = &config->gpios[i];

		if (pad_gpio->gpio == gpio) {
			gpio_free(gpio);

			stm_pad_gpio_ops->gpio_config(gpio, pad_gpio->direction,
					pad_gpio->function, pad_gpio->priv);

			return;
		}
	}

	/* Should never be here... */
	BUG();
}
EXPORT_SYMBOL(stm_pad_gpio_free);


/* debugfs view of used pads */

#ifdef CONFIG_DEBUG_FS

static void *stm_pad_seq_start(struct seq_file *s, loff_t *pos)
{
	mutex_lock(&stm_pad_mutex);

	return seq_list_start(&stm_pad_list, *pos);
}

static void *stm_pad_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	return seq_list_next(v, &stm_pad_list, pos);
}

static void stm_pad_seq_stop(struct seq_file *s, void *v)
{
	mutex_unlock(&stm_pad_mutex);
}

static int stm_pad_seq_show(struct seq_file *s, void *v)
{
	struct stm_pad_state *state = list_entry(v, struct stm_pad_state,
			list);
	struct stm_pad_config *config = state->config;
	int i;

	seq_printf(s, "*** %s ***\n", state->owner);

	for (i = 0; i < config->gpios_num; i++) {
		struct stm_pad_gpio *pad_gpio = &config->gpios[i];
		char name[20];
		static const char *directions[] = {
			[stm_pad_gpio_direction_unknown] = "unknown",
			[stm_pad_gpio_direction_in] = "input",
			[stm_pad_gpio_direction_in_pull_up] = "input pull up",
			[stm_pad_gpio_direction_out] = "output",
			[stm_pad_gpio_direction_bidir] = "bidirectional",
			[stm_pad_gpio_direction_bidir_pull_up] =
						 "bidirectional pull up",
			[stm_pad_gpio_direction_ignored] = "ignored",
		};

		if (i == 0)
			seq_printf(s, "- GPIOs:\n");

		if (pad_gpio->direction == stm_pad_gpio_direction_ignored)
			continue;

		BUG_ON(pad_gpio->direction >= ARRAY_SIZE(directions));

		stm_gpio_get_name(pad_gpio->gpio, name, sizeof(name));
		seq_printf(s, "  - %d: %s: %s, function %d",
				pad_gpio->gpio, name,
				directions[pad_gpio->direction],
				pad_gpio->function);
		if (pad_gpio->name)
			seq_printf(s, ", '%s'", pad_gpio->name);
		seq_printf(s, "\n");
	}

	for (i = 0; i < config->sysconfs_num; i++) {
		struct stm_pad_sysconf *sysconf = &config->sysconfs[i];
		char name[20];

		if (i == 0)
			seq_printf(s, "- System Configuration values:\n");

		sysconf_reg_name(name, sizeof(name), sysconf->regtype,
				sysconf->regnum);
		seq_printf(s, "  - %s", name);

		if (sysconf->msb == sysconf->lsb)
			seq_printf(s, "[%d]", sysconf->lsb);
		else
			seq_printf(s, "[%d:%d]", sysconf->msb, sysconf->lsb);

		seq_printf(s, " = 0x%0*lx",
				(sysconf->msb - sysconf->lsb + 4) / 4,
				sysconf_read(state->sysconf_fields[i]));
		if (sysconf->name)
			seq_printf(s, " '%s'", sysconf->name);
		seq_printf(s, "\n");
	}

	if (config->custom_claim) {
		seq_printf(s, "- custom claim");
		if (!config->custom_release)
			seq_printf(s, " and release");
		seq_printf(s, "\n");
	}

	seq_printf(s, "\n");

	return 0;
}

static const struct seq_operations stm_pad_seq_ops = {
	.start = stm_pad_seq_start,
	.next = stm_pad_seq_next,
	.stop = stm_pad_seq_stop,
	.show = stm_pad_seq_show,
};

static int stm_pad_debugfs_open(struct inode *inode, struct file *file)
{
	BUG_ON(!stm_pad_initialized);

	return seq_open(file, &stm_pad_seq_ops);
}

static const struct file_operations stm_pad_debugfs_ops = {
	.owner = THIS_MODULE,
	.open = stm_pad_debugfs_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

static int stm_pad_state_debugfs_show(struct seq_file *s, void *unused)
{
	int gpio;

	mutex_lock(&stm_pad_mutex);

	for (gpio = 0; gpio < stm_pad_gpios_num; gpio++) {
		char name[20];
		char buf[80];
		const char *desc;
		const char *states[] = {
			[stm_pad_gpio_unused] =  "unused",
			[stm_pad_gpio_normal_gpio] = "gpio",
			[stm_pad_gpio_claimed] = "claimed",
			[stm_pad_gpio_claimed_to_be_requested] = "claimed-gpio",
			[stm_pad_gpio_claimed_requested] = "claimed-gpio",
		};
		struct stm_pad_state *state;
		struct stm_pad_gpio *pad_gpio;

		stm_gpio_get_name(gpio, name, sizeof(name));

		if (stm_pad_gpio_ops->gpio_report) {
			stm_pad_gpio_ops->gpio_report(gpio, buf, sizeof(buf));
			desc = buf;
		} else {
			desc = stm_gpio_get_direction(gpio);
		}

		seq_printf(s, "%3d: %8s: %s %s",
			   gpio, name, desc, states[stm_pad_gpios[gpio]]);

		if (!stm_pad_find_gpio_struct(gpio, &state, &pad_gpio)) {
			seq_printf(s, ": %s", state->owner);
			if (pad_gpio->name)
				seq_printf(s, " (%s)", pad_gpio->name);
		}

		seq_printf(s, "\n");
	}

	mutex_unlock(&stm_pad_mutex);

	return 0;
}

static int stm_pad_state_debugfs_open(struct inode *inode, struct file *file)
{
	return single_open(file, stm_pad_state_debugfs_show, NULL);
}

static const struct file_operations stm_pad_state_debugfs_ops = {
	.open		= stm_pad_state_debugfs_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init stm_pad_debugfs_init(void)
{
	debugfs_create_file("pads", S_IFREG | S_IRUGO,
			NULL, NULL, &stm_pad_debugfs_ops);

	debugfs_create_file("pad-state", S_IFREG | S_IRUGO,
			NULL, NULL, &stm_pad_state_debugfs_ops);

	return 0;
}
subsys_initcall(stm_pad_debugfs_init);

#endif /* CONFIG_DEBUG_FS */



/* Configuration structure helpers */

static struct stm_pad_gpio *stm_pad_find_gpio(struct stm_pad_config *config,
		const char *name)
{
	struct stm_pad_gpio *result = NULL;
	int i;

	BUG_ON(!name);

	for (i = 0; i < config->gpios_num; i++) {
		struct stm_pad_gpio *pad_gpio = &config->gpios[i];

		if (pad_gpio->name && strcmp(name, pad_gpio->name) == 0) {
			result = pad_gpio;
			break;
		}
	}

	return result;
}

static struct stm_pad_sysconf *stm_pad_find_sysconf(
		struct stm_pad_config *config,
		const char *name)
{
	struct stm_pad_sysconf *result = NULL;
	int i;

	BUG_ON(!name);

	for (i = 0; i < config->sysconfs_num; i++) {
		struct stm_pad_sysconf *pad_sysconf = &config->sysconfs[i];

		if (pad_sysconf->name && strcmp(name, pad_sysconf->name) == 0) {
			result = pad_sysconf;
			break;
		}
	}

	return result;
}

int __init stm_pad_set_gpio(struct stm_pad_config *config, const char *name,
		unsigned gpio)
{
	int result = -EINVAL;
	struct stm_pad_gpio *pad_gpio = stm_pad_find_gpio(config, name);

	WARN_ON(!pad_gpio);
	if (pad_gpio) {
		pad_gpio->gpio = gpio;
		result = 0;
	}

	return result;
}

int __init stm_pad_set_priv(struct stm_pad_config *config, const char *name,
		void *priv)
{
	int result = -EINVAL;
	struct stm_pad_gpio *pad_gpio = stm_pad_find_gpio(config, name);

	WARN_ON(!pad_gpio);
	if (pad_gpio) {
		pad_gpio->priv = priv;
		result = 0;
	}

	return result;
}

int __init stm_pad_set_direction_function(struct stm_pad_config *config,
		const char *name, enum stm_pad_gpio_direction direction,
		int out_value, int function)
{
	int result = -EINVAL;
	struct stm_pad_gpio *pad_gpio = stm_pad_find_gpio(config, name);

	WARN_ON(!pad_gpio);
	if (pad_gpio) {
		pad_gpio->direction = direction;
		pad_gpio->out_value = out_value;
		pad_gpio->function = function;
		result = 0;
	}

	return result;
}

int __init stm_pad_set_sysconf(struct stm_pad_config *config,
			       const char *name, int value)
{
	int result = -EINVAL;
	struct stm_pad_sysconf *pad_sysconf;

	pad_sysconf = stm_pad_find_sysconf(config, name);
	if (!WARN_ON(!pad_sysconf)) {
		pad_sysconf->value = value;
		result = 0;
	}

	return result;
}


/* Dynamic configuration routines */

#define to_dynamic(config) \
		container_of(config, struct stm_pad_config_dynamic, config)

#if defined(CONFIG_BUG)
#define MAGIC_FIELD enum { magic_good = 0x600df00d } magic;
#define MAGIC_SET(d) d->magic = magic_good
#define MAGIC_CHECK(d) BUG_ON(d->magic != magic_good)
#else
#define MAGIC_FIELD
#define MAGIC_SET(d)
#define MAGIC_CHECK(d)
#endif

struct stm_pad_config_dynamic {
	MAGIC_FIELD
	int sysconfs_allocated;
	int gpios_allocated;
	struct stm_pad_config config;
};

struct stm_pad_config * __init stm_pad_config_alloc(int gpios_num,
		int sysconfs_num)
{
	struct stm_pad_config *config;
	struct stm_pad_config_dynamic *dynamic;

	dynamic = stm_pad_alloc(sizeof(struct stm_pad_config_dynamic) +
			sizeof(struct stm_pad_gpio) * gpios_num +
			sizeof(struct stm_pad_sysconf) * sysconfs_num);
	if (!dynamic)
		return NULL;
	MAGIC_SET(dynamic);

	/* +------------+
	 * | dynamic    | sizeof(struct stm_pad_config_dynamic)
	 * | { config } |
	 * +------------+
	 * | gpios      | sizeof(struct stm_pad_gpio) * gpios_num
	 * +------------+
	 * | sysconfs   | sizeof(struct stm_pad_sysconf) * sysconfs_num
	 * +------------+
	 */

	config = &dynamic->config;

	dynamic->gpios_allocated = gpios_num;
	config->gpios = (void *)dynamic + sizeof(struct stm_pad_config_dynamic);

	dynamic->sysconfs_allocated = sysconfs_num;
	config->sysconfs = (void *)config->gpios +
			sizeof(struct stm_pad_gpio) * gpios_num;

	return config;
}

int __init stm_pad_config_add_sysconf(struct stm_pad_config *config,
		int regtype, int regnum, int lsb, int msb, int value)
{
	int result = -ENOMEM;
	struct stm_pad_config_dynamic *dynamic;
	struct stm_pad_sysconf *sysconf;

	BUG_ON(!config);
	dynamic = to_dynamic(config);
	MAGIC_CHECK(dynamic);

	WARN_ON(config->sysconfs_num == dynamic->sysconfs_allocated);
	if (config->sysconfs_num < dynamic->sysconfs_allocated) {
		sysconf = config->sysconfs + config->sysconfs_num++;

		sysconf->regtype = regtype;
		sysconf->regnum = regnum;
		sysconf->lsb = lsb;
		sysconf->msb = msb;
		sysconf->value = value;

		result = 0;
	}

	return result;
}

int __init stm_pad_config_add_gpio_named(struct stm_pad_config *config,
		unsigned gpio, enum stm_pad_gpio_direction direction,
		int out_value, int function, const char *name)
{
	int result = -ENOMEM;
	struct stm_pad_config_dynamic *dynamic;
	struct stm_pad_gpio *pad_gpio;

	BUG_ON(!config);
	dynamic = to_dynamic(config);
	MAGIC_CHECK(dynamic);

	WARN_ON(config->gpios_num == dynamic->gpios_allocated);
	if (config->gpios_num < dynamic->gpios_allocated) {
		pad_gpio = config->gpios + config->gpios_num++;

		pad_gpio->gpio = gpio;
		pad_gpio->direction = direction;
		pad_gpio->out_value = out_value;
		pad_gpio->function = function;
		pad_gpio->name = name;

		result = 0;
	}

	return result;
}
#ifdef CONFIG_OF

#define OF_STM_GPIO_ARGS_MIN	(4)
static int stm_pinctrl_is_pin_property(struct property *pp)
{
	if (pp  && (pp->length/sizeof(u32)) >= OF_STM_GPIO_ARGS_MIN)
		return 1;
	return 0;
}

static struct stm_pio_control_pad_config *stm_of_get_pad_retime(
		struct device *dev, u32 config)
{
	struct stm_pio_control_pad_config *pad_config;
	struct stm_pio_control_retime_config *rt;


	pad_config = devm_kzalloc(dev, sizeof(*pad_config),
					GFP_KERNEL);
	pad_config->retime = devm_kzalloc(dev, sizeof(*rt), GFP_KERNEL);
	rt = pad_config->retime;

	rt->retime = STM_PINCONF_UNPACK_RT(config);
	rt->clknotdata = STM_PINCONF_UNPACK_RT_CLKNOTDATA(config);
	rt->double_edge = STM_PINCONF_UNPACK_RT_DOUBLE_EDGE(config);
	rt->invertclk = STM_PINCONF_UNPACK_RT_INVERTCLK(config);

	rt->delay = STM_PINCONF_UNPACK_RT_DELAY(config);
	rt->clk = STM_PINCONF_UNPACK_RT_CLK(config);

	rt->force_delay = STM_PINCONF_UNPACK_FORCE_DELAY(config);
	rt->force_delay_innotout =
		STM_PINCONF_UNPACK_FORCE_DELAY_INNOTOUT(config);

	return pad_config;
}

/**
 *	stm_of_get_pad_config_from_node - parse stm pad config from
 *					a padconfig device node.
 *	@np:	Node which is a pad config device node.
 *
 *	returns a pointer to newly allocated stm_pad_config.
 *	User is responsible to freeing the pointer.
 *
 */
struct stm_pad_config *stm_of_get_pad_config_from_node(struct device *dev,
			struct device_node *np, int index)
{
	struct stm_pad_config *pad_config;
	struct device_node *gpios, *pc_np;
	struct stm_pad_gpio *gpio;
	struct property *pp;
	const __be32 *list, *pc_list, *old_pc_list;
	int i = 0, k = 0, nr_gpios = 0, nr_props, size;
	char padconf_name[16];
	phandle phandle;

	if (!np)
		return NULL;

	/*
	 * Also padconf-names property available to read user friendly name.
	 */
	sprintf(padconf_name, "padcfg-%d", index);

	pp = of_find_property(np, padconf_name, &size);
	if (!pp)
		return NULL;
	pc_list = pp->value;
	size =  pp->length/sizeof(u32);
	old_pc_list = pc_list;

	for (k = 0; k < size; k++) {
		phandle = be32_to_cpup(pc_list++);

		/* Look up the pin configuration node */
		pc_np = of_find_node_by_phandle(phandle);
		if (!pc_np)
			continue;

		gpios = of_get_child_by_name(pc_np, "st,pins");
		if (!gpios)
			continue;

		for_each_property_of_node(gpios, pp) {
			if (stm_pinctrl_is_pin_property(pp))
				nr_gpios++;
		}
		of_node_put(pc_np);
		of_node_put(gpios);
	}

	pad_config = devm_kzalloc(dev, sizeof(*pad_config), GFP_KERNEL);
	pad_config->gpios_num = nr_gpios;
	pad_config->gpios = devm_kzalloc(dev, (nr_gpios) * sizeof(*gpio),
					GFP_KERNEL);
	pc_list = old_pc_list;

	for (k = 0; k < size; k++) {
		phandle = be32_to_cpup(pc_list++);

		/* Look up the pin configuration node */
		pc_np = of_find_node_by_phandle(phandle);
		if (!pc_np)
			continue;

		gpios = of_get_child_by_name(pc_np, "st,pins");
		if (!gpios)
			continue;

		/* BANK OFFSET DIR ALTFUNC [ RT-TYPE CLK FORCE DELAY ] */
		for_each_property_of_node(gpios, pp) {
			if (!stm_pinctrl_is_pin_property(pp))
				continue;

			nr_props = pp->length/sizeof(u32);
			list = pp->value;
			gpio = &pad_config->gpios[i];

			/* bank & pin */
			list++;
			list++;
			gpio->gpio = of_get_named_gpio(gpios, pp->name, 0);
			gpio->name = pp->name;

			/* direction */
			gpio->direction = be32_to_cpup(list++);
			/* altfun_np */
			gpio->function = be32_to_cpup(list++);
			/* RT-TYPE, RT-DELAY, RT-CLK, FORCE */
			if (nr_props > OF_STM_GPIO_ARGS_MIN) {
				u32 rt_config = 0;
				while (nr_props-- > OF_STM_GPIO_ARGS_MIN)
					rt_config |= be32_to_cpup(list++);
				gpio->priv = stm_of_get_pad_retime(dev,
							rt_config);
			}
			i++;
		}
		of_node_put(gpios);
		of_node_put(pc_np);
	}

	return pad_config;
}
EXPORT_SYMBOL(stm_of_get_pad_config_from_node);

/**
 *	stm_of_get_pad_config_index - parse stm pad config at
 *			particular index from a device.
 *
 *	returns a pointer to newly allocated stm_pad_config.
 *	User is responsible to freeing the pointer.
 *
 */
struct stm_pad_config *stm_of_get_pad_config_index(struct device *dev,
		int index)
{
	return stm_of_get_pad_config_from_node(dev, dev->of_node, index);
}
EXPORT_SYMBOL(stm_of_get_pad_config_index);
/**
 *	stm_of_get_pad_config - parse stm pad config from a device.
 *
 *	returns a pointer to newly allocated stm_pad_config.
 *	User is responsible to freeing the pointer.
 *
 */
struct stm_pad_config *stm_of_get_pad_config(struct device *dev)
{
	return stm_of_get_pad_config_from_node(dev, dev->of_node, 0);
}
EXPORT_SYMBOL(stm_of_get_pad_config);

void stm_of_pad_config_fixup(struct device *dev,
		struct device_node *fixup_np, struct stm_pad_state *state)
{
	struct stm_pio_control_pad_config *priv;
	struct property *pp;
	const __be32 *list;
	struct device_node *gpios;
	int dir, altfun, nr_props;

	if (!fixup_np)
		return;

	gpios = of_get_child_by_name(fixup_np, "gpios");

	if (gpios) {
		for_each_property_of_node(gpios, pp) {
			nr_props = pp->length/sizeof(u32);
			if (nr_props < 2)
				continue;

			list = pp->value;
			/* direction */
			dir = be32_to_cpup(list++);
			/* altfun_np */
			altfun = be32_to_cpup(list++);

			priv = NULL;
			if (nr_props > (OF_STM_GPIO_ARGS_MIN - 2)) {
				u32 rt_config = 0;
				while (nr_props-- > (OF_STM_GPIO_ARGS_MIN - 2))
					rt_config |= be32_to_cpup(list++);

				priv = stm_of_get_pad_retime(dev, rt_config);
			}
			stm_pad_update_gpio(state, pp->name, dir, -1,
						altfun, priv);
		}
	}
	of_node_put(gpios);
	return;
}
EXPORT_SYMBOL(stm_of_pad_config_fixup);

#endif
