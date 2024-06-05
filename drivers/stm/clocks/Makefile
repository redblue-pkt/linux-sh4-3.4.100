#
# Makefile for the STMicroelectronics clocks.
#

clock-y					:= clk-common.o
clock-$(CONFIG_CPU_SUBTYPE_STX7108)	:= clock-common.o clock-stx7108.o
clock-$(CONFIG_CPU_SUBTYPE_STXH205)	:= clock-common.o clock-stxh205.o
clock-$(CONFIG_MACH_STM_STIH407)	+= clock-stih407.o clock-stih410.o clk-flexgen.o
clock-$(CONFIG_CPU_SUBTYPE_STIH415)	+= clock-stih415.o clock-stxmpe41.o clock-stxsasg1.o
clock-$(CONFIG_MACH_STM_STIH416)	+= clock-stih416.o clock-stxmpe42.o clock-stxsasg2.o
clock-$(CONFIG_MACH_STM_STID127)	+= clock-stid127.o

obj-y	+= $(clock-y)
