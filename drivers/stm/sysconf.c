/*
 * Copyright (C) 2007 STMicroelectronics Limited
 * Author: Stuart Menefy <stuart.menefy@st.com>
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/bootmem.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/list.h>
#include <linux/platform_device.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/types.h>
#include <linux/stm/platform.h>
#include <linux/stm/sysconf.h>
#include "abort.h"

#define DRIVER_NAME "stm-sysconf"

struct sysconf_field {
	u8 group;
	u16 num;
	u8 lsb, msb;
	void __iomem *reg;
	const char *owner;
	struct list_head list;
#ifdef DEBUG
	enum { magic_good = 0x600df00d, magic_bad = 0xdeadbeef } magic;
#define MAGIC_SET(field) field->magic = magic_good
#define MAGIC_CLEAR(field) field->magic = magic_bad
#define MAGIC_CHECK(field) BUG_ON(field->magic != magic_good)
#else
#define MAGIC_SET(field)
#define MAGIC_CLEAR(field)
#define MAGIC_CHECK(field)
#endif
};

struct sysconf_group {
	void __iomem *base;
	int	start;
	const char *name;
	int (*reg_name)(char *name, int size, int group, int num);
	struct sysconf_block *block;
};

struct sysconf_block {
	void __iomem *base;
	unsigned long size;
	struct platform_device *pdev;
	struct device_node *of_node;
};

static int sysconf_blocks_num;
static struct sysconf_block *sysconf_blocks;

static int sysconf_groups_num;
static struct sysconf_group *sysconf_groups;

static LIST_HEAD(sysconf_fields);
static DEFINE_SPINLOCK(sysconf_fields_lock);

static DEFINE_SPINLOCK(sysconf_registers_lock);



/* We need a small stash of allocations before kmalloc becomes available */
# define NUM_EARLY_FIELDS	512

#define EARLY_BITS_MAPS_SIZE	DIV_ROUND_UP(NUM_EARLY_FIELDS, BITS_PER_LONG)

static struct sysconf_field early_fields[NUM_EARLY_FIELDS];
static unsigned long early_fields_map[EARLY_BITS_MAPS_SIZE];

static struct sysconf_field *field_alloc(void)
{
	int i;

	for (i = 0; i < NUM_EARLY_FIELDS; i++)
		if (test_and_set_bit(i, early_fields_map) == 0)
			return &early_fields[i];

	return kmalloc(sizeof(struct sysconf_field), GFP_KERNEL);
}

static void field_free(struct sysconf_field *field)
{
	if (field >= early_fields && field < early_fields + NUM_EARLY_FIELDS)
		clear_bit(field - early_fields, early_fields_map);
	else
		kfree(field);
}



struct sysconf_field *sysconf_claim(int group, int num, int lsb, int msb,
				    const char *devname)
{
	struct sysconf_field *field, *entry;
	enum {
		status_searching,
		status_found_register,
		status_add_field_here,
		status_conflict,
	} status = status_searching;
	int bit_avail = 0;

	pr_debug("%s(group=%d, num=%d, lsb=%d, msb=%d, devname='%s')\n",
			__func__, group, num, lsb, msb, devname);

	BUG_ON(group < 0 || group >= sysconf_groups_num);
	BUG_ON(num < 0 || num > (1 << (sizeof(entry->num) * 8 - 1)));
	BUG_ON(lsb < 0 || lsb > 32);
	BUG_ON(msb < 0 || msb > 32);
	BUG_ON(lsb > msb);

	field = field_alloc();
	if (!field)
		return NULL;

	field->group = group;
	field->num = num;
	field->lsb = lsb;
	field->msb = msb;

	field->reg = sysconf_groups[group].base + (num * 4);
	BUG_ON(field->reg >= sysconf_groups[group].block->base +
			sysconf_groups[group].block->size);
	field->owner = devname;
	MAGIC_SET(field);

	spin_lock(&sysconf_fields_lock);

	/* The list is always in group->num->lsb/msb order, so it's easy to
	 * find a place to insert a new field (and to detect conflicts) */
	list_for_each_entry(entry, &sysconf_fields, list) {
		if (entry->group == group && entry->num == num) {
			status = status_found_register;
			/* Someone already claimed a field from this
			 * register - let's try to find some space for
			 * requested bits... */
			if (bit_avail <= lsb && msb < entry->lsb) {
				status = status_add_field_here;
				break;
			}
			bit_avail = entry->msb + 1;
		} else if ((entry->group == group && entry->num > num) ||
				entry->group > group) {
			/* Ok, there is no point of looking further -
			 * the group and/or num values are bigger then
			 * the ones we are looking for */
			if ((status == status_found_register &&
					bit_avail <= lsb) ||
					status == status_searching)
				/* A remainder of the given register is not
				 * used or the register wasn't used at all */
				status = status_add_field_here;
			else
				/* Apparently some bits of the claimed field
				 * are already in use... */
				status = status_conflict;
			break;
		}
	}

	switch (status) {
	case status_searching:
	case status_found_register:
		/* Either nothing was on the list at all or the claimed
		 * field should be added as the last element... */
		list_add_tail(&field->list, &sysconf_fields);
		break;
	case status_add_field_here:
		/* So we should insert the new field between current
		 * list entry and the previous one */
		list_add(&field->list, entry->list.prev);
		break;
	case status_conflict:
		/* Apparently there was no place in the
		 * register to fulfill the request... */
		MAGIC_CLEAR(field);
		field_free(field);
		field = NULL;
		pr_debug("%s(): conflict!\n", __func__);
		break;
	default:
		BUG();
	}

	spin_unlock(&sysconf_fields_lock);

	pr_debug("%s()=0x%p\n", __func__, field);

	return field;
}
EXPORT_SYMBOL(sysconf_claim);

void sysconf_release(struct sysconf_field *field)
{
	pr_debug("%s(field=0x%p)\n", __func__, field);

	BUG_ON(!field);
	MAGIC_CHECK(field);

	spin_lock(&sysconf_fields_lock);
	list_del(&field->list);
	spin_unlock(&sysconf_fields_lock);

	MAGIC_CLEAR(field);
	field_free(field);
}
EXPORT_SYMBOL(sysconf_release);

void sysconf_write(struct sysconf_field *field, unsigned long value)
{
	int field_bits;

	pr_debug("%s(field=0x%p (%s %d[%d:%d]) = 0x%08lx)\n", __func__, field,
		 sysconf_groups[field->group].name, field->num,
		 field->msb, field->lsb, value);

	BUG_ON(!field);
	MAGIC_CHECK(field);

	field_bits = field->msb - field->lsb + 1;
	BUG_ON(field_bits < 1 || field_bits > 32);

	if (field_bits == 32) {
		/* Operating on the whole register, nice and easy */
		writel(value, field->reg);
	} else {
		unsigned long flags;
		u32 reg_mask;
		u32 tmp;

		reg_mask = ~(((1 << field_bits) - 1) << field->lsb);

		spin_lock_irqsave(&sysconf_registers_lock, flags);
		tmp = readl(field->reg);
		tmp &= reg_mask;
		tmp |= value << field->lsb;
		writel(tmp, field->reg);
		spin_unlock_irqrestore(&sysconf_registers_lock, flags);
	}
}
EXPORT_SYMBOL(sysconf_write);

unsigned long sysconf_read(struct sysconf_field *field)
{
	int field_bits;
	u32 result;

	pr_debug("%s(field=0x%p (%s %d[%d:%d]))\n", __func__, field,
		 sysconf_groups[field->group].name, field->num,
		 field->msb, field->lsb);

	BUG_ON(!field);
	MAGIC_CHECK(field);

	field_bits = field->msb - field->lsb + 1;
	BUG_ON(field_bits < 1 || field_bits > 32);

	result = readl(field->reg);

	if (field_bits != 32) {
		result >>= field->lsb;
		result &= (1 << field_bits) - 1;
	}

	pr_debug("%s()=0x%u\n", __func__, result);

	return result;
}
EXPORT_SYMBOL(sysconf_read);

static int sysconf_read_safe(struct sysconf_field *field, unsigned long *valuep)
{
	int field_bits;
	int result;
	u32 value;
	unsigned long flags;

	pr_debug("%s(field=0x%p (%s %d[%d:%d]))\n", __func__, field,
		 sysconf_groups[field->group].name, field->num,
		 field->msb, field->lsb);

	BUG_ON(!field);
	MAGIC_CHECK(field);

	field_bits = field->msb - field->lsb + 1;
	BUG_ON(field_bits < 1 || field_bits > 32);

	spin_lock_irqsave(&stm_abort_lock, flags);
	atomic_set(&stm_abort_flag, 0);

	GET_LABEL_ADDR(config_read_start, abort_start);
	GET_LABEL_ADDR(config_read_end, abort_end);
	EMIT_LABEL(config_read_start);

	value = readl(field->reg);
	mb();

	EMIT_LABEL(config_read_end);
	result = atomic_read(&stm_abort_flag) ? -EINTR : 0;
	spin_unlock_irqrestore(&stm_abort_lock, flags);

	if (!result) {
		if (field_bits != 32) {
			value >>= field->lsb;
			value &= (1 << field_bits) - 1;
		}

		*valuep = value;
	}

	pr_debug("%s()=%d (value 0x%u)\n", __func__, result, value);
	return result;
}

int sysconf_reg_name(char *name, int size, int group, int num)
{
	BUG_ON(group < 0 || group >= sysconf_groups_num);

	if (sysconf_groups[group].reg_name)
		return sysconf_groups[group].reg_name(name, size, group, num);
	else
		return snprintf(name, size, "%s%d", sysconf_groups[group].name, num);
}
EXPORT_SYMBOL(sysconf_reg_name);

#ifdef CONFIG_DEBUG_FS

enum sysconf_seq_state { state_blocks, state_groups, state_fields, state_last };

static void *sysconf_seq_start(struct seq_file *s, loff_t *pos)
{
	if (*pos >= state_last)
		return NULL;

	return pos;
}

static void *sysconf_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	(*pos)++;

	if (*pos >= state_last)
		return NULL;

	return pos;
}

static void sysconf_seq_stop(struct seq_file *s, void *v)
{
}

static int sysconf_seq_show_blocks(struct seq_file *s)
{
	int i;

	seq_printf(s, "blocks:\n");

	for (i = 0; i < sysconf_blocks_num; i++) {
		struct sysconf_block *block = &sysconf_blocks[i];
		struct resource *mem = platform_get_resource(block->pdev,
				IORESOURCE_MEM, 0);

		seq_printf(s, "- %s: 0x%08x (0x%p), 0x%lxb\n",
			   dev_name(&block->pdev->dev), mem->start,
			   block->base, block->size);
	}

	seq_printf(s, "\n");

	return 0;
}

static int sysconf_seq_show_groups(struct seq_file *s)
{
	int i;

	seq_printf(s, "groups:\n");

	for (i = 0; i < sysconf_groups_num; i++) {
		struct sysconf_group *group = &sysconf_groups[i];

		seq_printf(s, "- %s: 0x%p (%s)\n",
			   group->name, group->base,
			   dev_name(&group->block->pdev->dev));
	}

	seq_printf(s, "\n");

	return 0;
}

static int sysconf_seq_show_fields(struct seq_file *s)
{
	struct sysconf_field *field;

	seq_printf(s, "claimed fields:\n");

	spin_lock(&sysconf_fields_lock);

	list_for_each_entry(field, &sysconf_fields, list) {
		char name[20];
		long value;

		sysconf_reg_name(name, sizeof(name), field->group, field->num);
		seq_printf(s, "- %s[", name);

		if (field->msb == field->lsb)
			seq_printf(s, "%d", field->msb);
		else
			seq_printf(s, "%d:%d", field->msb, field->lsb);

		if (!sysconf_read_safe(field, &value))
			seq_printf(s, "] = 0x%0*lx (0x%p, %s)\n",
				   (field->msb - field->lsb + 4) / 4,
				   value,
				   field->reg, field->owner);
		else
			seq_printf(s, "] = RESERVED (0x%p, %s)\n",
				   field->reg, field->owner);

	}

	spin_unlock(&sysconf_fields_lock);

	seq_printf(s, "\n");

	return 0;
}

static int sysconf_seq_show(struct seq_file *s, void *v)
{
	enum sysconf_seq_state state = *((loff_t *)v);

	switch (state) {
	case state_blocks:
		return sysconf_seq_show_blocks(s);
	case state_groups:
		return sysconf_seq_show_groups(s);
	case state_fields:
		return sysconf_seq_show_fields(s);
	default:
		BUG();
		return -EINVAL;
	}
}

static struct seq_operations sysconf_seq_ops = {
	.start = sysconf_seq_start,
	.next = sysconf_seq_next,
	.stop = sysconf_seq_stop,
	.show = sysconf_seq_show,
};

static int sysconf_debugfs_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &sysconf_seq_ops);
}

static const struct file_operations sysconf_debugfs_ops = {
	.owner = THIS_MODULE,
	.open = sysconf_debugfs_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

static int __init sysconf_debugfs_init(void)
{
	debugfs_create_file("sysconf", S_IFREG | S_IRUGO,
			NULL, NULL, &sysconf_debugfs_ops);

	return 0;
}
subsys_initcall(sysconf_debugfs_init);

#endif

#ifdef CONFIG_OF

static struct of_device_id stm_sysconf_match[] = {
	{
		.compatible = "st,sysconf",
	},
	{},
};
MODULE_DEVICE_TABLE(of, stm_sysconf_match);

static int of_get_sysconf_group(struct device_node *sysconf_groups,
			int sysconf_num, int *group, int *num)
{
	const __be32 *ip, *list;
	int k, group_cells;
	struct property *sysconf_group;
	int nr_groups = 0;

	ip = of_get_property(sysconf_groups,
				"#sysconf-group-cells", NULL);
	group_cells = be32_to_cpup(ip);
	sysconf_group = of_find_property(sysconf_groups,
				"sysconf-groups", NULL);
	nr_groups = sysconf_group->length/(sizeof(u32) * group_cells);
	list = sysconf_group->value;
	for (k = 0; k < nr_groups; k++) {
		int start = be32_to_cpup(list++);
		int end = be32_to_cpup(list++);
		*group = be32_to_cpup(list++);
		if (sysconf_num >= start && sysconf_num <= end) {
			*num = sysconf_num - start;
			return 0;
		}
	}
	return -ENODATA;
}

#define STM_OF_SYCONF_CELLS	(5)
struct sysconf_field *stm_of_sysconf_claim(struct device_node *np,
				const char *prop)
{
	const __be32 *list;
	const struct property *pp;
	phandle phandle;
	struct device_node *sysconf_groups;
	int lsb, msb, group = 0, num = 0, sysconf_num;

	pp = of_find_property(np, prop, NULL);
	if (!pp)
		return NULL;

	if (pp->length != ((STM_OF_SYCONF_CELLS - 1) * sizeof(u32)))
		return NULL;

	list = pp ? pp->value : NULL;

	/* sysconf group */
	phandle = be32_to_cpup(list++);
	sysconf_groups = of_find_node_by_phandle(phandle);
	sysconf_num = be32_to_cpup(list++);
	if (of_get_sysconf_group(sysconf_groups, sysconf_num, &group, &num))
		return NULL;
	lsb = be32_to_cpup(list++);
	msb = be32_to_cpup(list++);

	return sysconf_claim(group, num, lsb, msb, pp->name);
}
EXPORT_SYMBOL(stm_of_sysconf_claim);


static int of_sysconf_reg_name(char *name, int size, int group, int num)
{
	return snprintf(name, size, "SYSCONF%d",
		 sysconf_groups[group].start + num);
}

void __init of_sysconf_early_init()
{
	struct device_node *np, *child = NULL;
	int i;
	np = of_find_node_by_path("/sysconfs");
	if (!np)
		return;

	for_each_child_of_node(np, child) {
		if (of_match_node(stm_sysconf_match, child)) {
			struct device_node *grp = NULL;
			for_each_child_of_node(child, grp)
				sysconf_groups_num++;
			sysconf_blocks_num++;
		}
	}

	sysconf_blocks = alloc_bootmem(sizeof(*sysconf_blocks) *
			sysconf_blocks_num);
	if (!sysconf_blocks)
		panic("Failed to allocate memory for sysconf blocks!");

	sysconf_groups = alloc_bootmem(sizeof(*sysconf_groups)
							* sysconf_groups_num);
	if (!sysconf_groups)
		panic("Failed to allocate memory for sysconf groups!\n");

	i = 0;
	child = NULL;
	for_each_child_of_node(np, child) {
		struct resource res;
		struct sysconf_block *block;
		struct device_node *grp = NULL;

		if (!of_match_node(stm_sysconf_match, child))
			continue;

		block = &sysconf_blocks[i++];
		block->of_node = child;
		of_address_to_resource(child, 0, &res);
		block->size = resource_size(&res);
		block->base = ioremap(res.start, block->size);
		if (!block->base)
			panic("Unable to ioremap %s registers!",
			      dev_name(&block->pdev->dev));

		for_each_child_of_node(child, grp) {
			struct sysconf_group *group;
			u32 group_nr, offset;
			of_property_read_u32(grp, "group", &group_nr);
			BUG_ON(group_nr < 0 || group_nr >= sysconf_groups_num);

			of_property_read_u32(grp, "offset", &offset);

			group = &sysconf_groups[group_nr];
			BUG_ON(group->base != NULL);
			group->base = block->base + offset;
			of_property_read_u32(grp, "start",
					&group->start);
			of_property_read_string(grp, "group-name",
					&group->name);
			group->reg_name = of_sysconf_reg_name;
			group->block = block;
		}

	}
}
#endif

/* This is called early to allow board start up code to use sysconf
 * registers (in particular console devices). */
void __init sysconf_early_init(struct platform_device *pdevs, int pdevs_num)
{
	int i;

	pr_debug("%s(pdevs=%p, pdevs_num=%d)\n", __func__, pdevs, pdevs_num);

	/* I don't like panicing, but if we failed here, we probably
	 * don't have any way to report things have gone wrong.
	 * So a panic here at least gives some hope of being
	 * able to debug the problem. */

	sysconf_blocks_num = pdevs_num;
	sysconf_blocks = alloc_bootmem(sizeof(*sysconf_blocks) *
			sysconf_blocks_num);
	if (!sysconf_blocks)
		panic("Failed to allocate memory for sysconf blocks!");

	for (i = 0; i < sysconf_blocks_num; i++) {
		struct sysconf_block *block = &sysconf_blocks[i];
		struct stm_plat_sysconf_data *data = pdevs[i].dev.platform_data;
		struct resource *mem;

		block->pdev = &pdevs[i];

		mem = platform_get_resource(&pdevs[i], IORESOURCE_MEM, 0);
		BUG_ON(!mem);

		block->size = mem->end - mem->start + 1;
		if (data->regs)
			block->base = data->regs;
		else {
			block->base = ioremap(mem->start, block->size);
			if (!block->base)
				panic("Unable to ioremap %s registers!",
				      dev_name(&block->pdev->dev));
		}

		sysconf_groups_num += data->groups_num;
	}

	sysconf_groups = alloc_bootmem(sizeof(*sysconf_groups) *
			sysconf_groups_num);
	if (!sysconf_groups)
		panic("Failed to allocate memory for sysconf groups!\n");

	for (i = 0; i < sysconf_blocks_num; i++) {
		struct stm_plat_sysconf_data *data = pdevs[i].dev.platform_data;
		struct sysconf_block *block = &sysconf_blocks[i];
		int j;

		for (j = 0; j < data->groups_num; j++) {
			struct stm_plat_sysconf_group *info = &data->groups[j];
			struct sysconf_group *group;

			BUG_ON(info->group < 0 ||
					info->group >= sysconf_groups_num);

			group = &sysconf_groups[info->group];

			BUG_ON(group->base != NULL);

			group->base = block->base + info->offset;
			group->name = info->name;
			group->reg_name = info->reg_name;
			group->block = block;
		}

	}
}



static int sysconf_probe(struct platform_device *pdev)
{
	int result = -EINVAL;
	int i;

	pr_debug("%s(pdev=%p)\n", __func__, pdev);

	/* Confirm that the device has been initialized earlier */
	for (i = 0; i < sysconf_blocks_num; i++) {
#ifdef CONFIG_OF
		if (sysconf_blocks[i].of_node == pdev->dev.of_node) {
			sysconf_blocks[i].pdev = pdev;
			result = 0;
			break;
		}
#else
		if (sysconf_blocks[i].pdev == pdev) {
			result = 0;
			break;
		}
#endif
	}

	if (result == 0) {
		struct resource *mem = platform_get_resource(pdev,
				IORESOURCE_MEM, 0);

		BUG_ON(!mem);

		if (request_mem_region(mem->start, mem->end -
				mem->start + 1, pdev->name) == NULL) {
			pr_err("Memory region request failed for %s!\n",
			       dev_name(&pdev->dev));
			result = -EBUSY;
		}
	}

	pr_debug("%s()=%d\n", __func__, result);

	return result;
}

static struct platform_driver sysconf_driver = {
	.probe		= sysconf_probe,
	.driver	= {
		.name	= DRIVER_NAME,
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(stm_sysconf_match),
	},
};

static int __init sysconf_init(void)
{
	stm_abort_init();

	return platform_driver_register(&sysconf_driver);
}
arch_initcall(sysconf_init);
