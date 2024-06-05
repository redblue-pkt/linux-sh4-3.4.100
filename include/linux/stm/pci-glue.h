/*
 * (c) 2010 STMicroelectronics Limited
 *
 * Author: David Mckay <david.mckay@st.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Arch specific glue to join up the main stm pci driver in drivers/stm/
 * to the specific PCI arch code.
 *
 */

#ifndef __LINUX_STM_PCI_GLUE_H
#define __LINUX_STM_PCI_GLUE_H

#include <linux/pci.h>

/*
 * This function takes the information filled in by the arch independant code
 * and hooks it up to the arch specific stuff. The arch independant code lives
 * in drivers/stm/pcie.c or drivers/stm/pci.c
 *
 * If this function needs to allocate memory it should use devm_kzalloc()
 */

enum stm_pci_type {STM_PCI_EMISS, STM_PCI_EXPRESS};

int __devinit stm_pci_register_controller(struct platform_device *pdev,
					  struct pci_ops *config_ops,
					  enum stm_pci_type type);

/*
 * Given a pci bus, return the corresponding platform device that created it
 * in the first place. Must be called with a bus that has as it's root
 * controller an interface registered via above mechanism
 */
struct platform_device *stm_pci_bus_to_platform(struct pci_bus *bus);

/* Returns what type of bus this bus is ultimately connected to */
enum stm_pci_type stm_pci_controller_type(struct pci_bus *bus);

#ifdef CONFIG_STM_PCI_EMISS

/* IO functions for the EMISS PCI controller. Unfortunately
 * you have to actually run code to do an IO transaction,
 * rather than just dereference it, this complicates it
 * considerably
 */
u8 pci_emiss_inb(unsigned long port);
u16 pci_emiss_inw(unsigned long port);
u32 pci_emiss_inl(unsigned long port);

u8 pci_emiss_inb_p(unsigned long port);
u16 pci_emiss_inw_p(unsigned long port);
u32 pci_emiss_inl_p(unsigned long port);

void pci_emiss_insb(unsigned long port, void *dst, unsigned long count);
void pci_emiss_insw(unsigned long port, void *dst, unsigned long count);
void pci_emiss_insl(unsigned long port, void *dst, unsigned long count);

void pci_emiss_outb(u8 val, unsigned long port);
void pci_emiss_outw(u16 val, unsigned long port);
void pci_emiss_outl(u32 val, unsigned long port);

void pci_emiss_outb_p(u8 val, unsigned long port);
void pci_emiss_outw_p(u16 val, unsigned long port);
void pci_emiss_outl_p(u32 val, unsigned long port);

void pci_emiss_outsb(unsigned long port, const void *src,
		     unsigned long count);
void pci_emiss_outsw(unsigned long port, const void *src,
		     unsigned long count);
void pci_emiss_outsl(unsigned long port, const void *src,
		     unsigned long count);
#endif


#ifdef CONFIG_SUPERH
/* Later kernels have removed the indirection throught the
 * machine vec for IO. This gives us a problem as there is
 * no way to do IO on the ST without running some code, simply
 * de-referencing a memory location is not possible.
 *
 * Disable IO for now until we figure out if there is another
 * way we can support IO using some other mechanism or if we
 * have to re-introduce the machine vec, which should be avoided
 * if we can.
 *
 * Previously this macro used to fill in the above functions for
 * IO in the machine vec if PCI was enabled, now we just define it
 * to do nothing in all cases.
 */
#define STM_PCI_IO_MACHINE_VEC

#endif /* CONFIG_SUPERH */

#endif /* __LINUX_STM_PCI_GLUE_H */
