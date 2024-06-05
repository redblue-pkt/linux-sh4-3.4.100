/*
 * (c) 2010 STMicroelectronics Limited
 *
 * Author: Pawel Moll <pawel.moll@st.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

struct stm_miphy_style_ops {
	int (*miphy_start)(struct stm_miphy *miphy);
	void (*miphy_assert_deserializer)(struct stm_miphy *miphy, int assert);
	int (*miphy_sata_status)(struct stm_miphy *miphy);
	void (*miphy_calibrate)(struct stm_miphy *miphy);
};

struct stm_miphy_style {
	char *style_id;
	int (*probe)(struct stm_miphy *miphy);
	int (*remove)(void);
	struct list_head node;
	struct stm_miphy_style_ops *miphy_ops;
};

struct stm_miphy;

struct stm_miphy_device {
	struct mutex mutex;
	struct stm_miphy *dev;
	struct device *parent;
	int miphy_first;
	int miphy_count;
	unsigned int tx_pol_inv:1;
	unsigned int rx_pol_inv:1;
	unsigned int ten_bit_symbols:1; /* Apply 10 bit sequence */
	int ten_bit_done:1; /* Apply to one port only */
	enum miphy_sata_gen sata_gen;
	char *style_id;
	enum miphy_if_type type;
	enum miphy_mode *modes;
	void (*reg_write)(struct stm_miphy *, u8 addr, u8 data);
	u8 (*reg_read)(struct stm_miphy *, u8 addr);
	void (*pipe_write)(struct stm_miphy *, u32 addr, u32 data);
	u32 (*pipe_read)(struct stm_miphy *, u32 addr);
};

struct stm_miphy {
	struct stm_miphy_device *dev;
	struct stm_miphy_style *style;
	int port;
	enum miphy_mode mode;
	struct list_head node;
	struct device *owner;
	struct device *device;
	u8 miphy_version;
	u8 miphy_revision;
};

void stm_miphy_write(struct stm_miphy *miphy, u8 addr, u8 data);

u8 stm_miphy_read(struct stm_miphy *miphy, u8 addr);

/* 
 * Pipe interface, which implements the standard PIPE interface between
 * the PCIE express controller and the actual PCIe phy
 */
void stm_miphy_pipe_write(struct stm_miphy *miphy, u32 addr, u32 data);

u32 stm_miphy_pipe_read(struct stm_miphy *miphy, u32 addr);

/* Returns stm_miphy with that specified port number, NULL on failure */
struct stm_miphy *stm_miphy_find_port(int port);

/* MiPHY style registration for diff versions of MiPHY */
int miphy_register_style(struct stm_miphy_style *drv);

int miphy_unregister_style(struct stm_miphy_style *drv);

/* MiPHY register Device with Read/Write interfaces and device info*/
int miphy_register_device(struct stm_miphy_device *dev);

/* MiPHY unregister Device with dev info */
int miphy_unregister_device(struct stm_miphy_device *dev);
