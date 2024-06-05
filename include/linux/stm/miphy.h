/*
 * (c) 2010 STMicroelectronics Limited
 *
 * Author: Pawel Moll <pawel.moll@st.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_STM_MIPHY_H
#define __LINUX_STM_MIPHY_H

#define ID_MIPHY28LP	"MiPHY28LP"
#define ID_MIPHYA40X	"MiPHYA40X"
#define ID_MIPHY365X	"MiPHY3-65"
#define ID_MIPHYDUMMY	"MiPHY-DUMMY"

enum miphy_if_type { TAP_IF, UPORT_IF, DUMMY_IF };
enum miphy_mode { UNUSED_MODE, SATA_MODE, PCIE_MODE, USB30_MODE};
/* Allows soc layer to explicitly ask for a specifed link speed */
enum miphy_sata_gen {SATA_GEN_DEFAULT, SATA_GEN1, SATA_GEN2, SATA_GEN3};
struct stm_miphy;

/*
 * MiPHY API for SATA and PCIe
 */

struct stm_miphy *stm_miphy_claim(int port, enum miphy_mode mode,
	struct device *dev);
void stm_miphy_release(struct stm_miphy *miphy);

void stm_miphy_force_interface(int port, enum miphy_if_type interface);

int stm_miphy_start(struct stm_miphy *miphy);

int stm_miphy_sata_status(struct stm_miphy *miphy);

void stm_miphy_assert_deserializer(struct stm_miphy *miphy, int assert);

void stm_miphy_freeze(struct stm_miphy *miphy);
void stm_miphy_thaw(struct stm_miphy *miphy);
void stm_miphy_calibrate(struct stm_miphy *miphy);

#endif
