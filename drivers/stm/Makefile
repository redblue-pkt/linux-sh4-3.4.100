#
# Makefile for the STMicroelectronics device drivers.
#

obj-y					+= soc.o
obj-y					+= emi.o
obj-y					+= flashss/
obj-y					+= gpio.o
obj-y					+= pad.o
obj-y					+= sysconf.o
obj-y					+= device.o
obj-y					+= clk.o
obj-y					+= abort.o
obj-y					+= clocks/

obj-$(CONFIG_OF)		+= stm_devices_of.o
obj-$(CONFIG_OF)		+= mpe41_of_devices.o
obj-$(CONFIG_OF)		+= core_of.o

obj-$(CONFIG_STM_MIPHY)			+= miphy.o
obj-$(CONFIG_STM_MIPHY365X)		+= miphy365x.o
obj-$(CONFIG_STM_MIPHYA40X)		+= miphya40x.o
obj-$(CONFIG_STM_MIPHY28LP)		+= miphy28LP.o
obj-$(CONFIG_STM_MIPHY_TAP)		+= miphy_tap.o tap.o
obj-$(CONFIG_STM_MIPHY_PCIE_MP)		+= miphy_pcie_mp.o
obj-$(CONFIG_STM_MIPHY_DUMMY)		+= miphy_dummy.o
obj-$(CONFIG_STM_COPROCESSOR_CLASS)	+= coprocessor.o
obj-$(CONFIG_STM_COPROCESSOR_ST40)	+= coproc-st40.o
obj-$(CONFIG_STM_COPROCESSOR_ST200)	+= coproc-st200.o
obj-$(CONFIG_STM_RNG)			+= rng.o
obj-$(CONFIG_MALI400)			+= mali/
obj-$(CONFIG_STM_AMBA_BRIDGE)		+= amba_bridge.o

obj-$(CONFIG_PM)			+= wakeup_devices.o
obj-$(CONFIG_PM)			+= pm_notify.o

obj-$(CONFIG_STM_PCI_EMISS)		+= pci-emiss.o
obj-$(CONFIG_STM_PCIE)			+= pcie.o

obj-$(CONFIG_STM_IRQ_MUX)		+= irqmux.o

obj-$(CONFIG_CPU_SUBTYPE_STXH205)	+= stxh205_lpm.o
obj-$(CONFIG_STM_LPM)			+= lpm_com.o lpm_mb.o

obj-$(CONFIG_CPU_SUBTYPE_STX7108)	+= stx7108.o stx7108_clock.o stx7108_audio.o stx7108_comms.o stx7108_hispeed.o stx7108_pci.o pio-control.o
obj-$(CONFIG_CPU_SUBTYPE_STXH205)	+= stxh205.o stxh205_clock.o stxh205_audio.o stxh205_comms.o stxh205_hispeed.o stxh205_pci.o pio-control.o
obj-$(CONFIG_MACH_STM_STIH407)		+= pio-control.o stih407/
obj-$(CONFIG_CPU_SUBTYPE_STIH415)	+= stih415.o stih415_clock.o stih415_audio.o stih415_hispeed.o stih415_usb.o \
					   stih415_comms.o stih415_pci.o pio-control.o mpe41_devices.o
obj-$(CONFIG_MACH_STM_STIH416)		+= pio-control.o mpe42_devices.o stih416/
obj-$(CONFIG_MACH_STM_STID127)		+= pio-control.o stid127/
