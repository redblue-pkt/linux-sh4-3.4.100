#
# Makefile for the Linux/SuperH SH-4 backends.
#

obj-y	:= probe.o common.o
common-y	+= entry.o
obj-$(CONFIG_COUNT_EXCEPTIONS)		+= count_exceptions.o

swsusp-y				:= $(addprefix ../sh3/, swsusp.o)
obj-$(CONFIG_HIBERNATION_ON_DISK)	+= swsusp.o

obj-$(CONFIG_SH_FPU)			+= fpu.o softfloat.o
obj-$(CONFIG_SH_STORE_QUEUES)		+= sq.o

# Perf events
perf-$(CONFIG_CPU_SUBTYPE_SH7750)	:= perf_event.o
perf-$(CONFIG_CPU_SUBTYPE_SH7750S)	:= perf_event.o
perf-$(CONFIG_CPU_SUBTYPE_SH7091)	:= perf_event.o

# CPU subtype setup
obj-$(CONFIG_CPU_SUBTYPE_SH7750)	+= setup-sh7750.o
obj-$(CONFIG_CPU_SUBTYPE_SH7750R)	+= setup-sh7750.o
obj-$(CONFIG_CPU_SUBTYPE_SH7750S)	+= setup-sh7750.o
obj-$(CONFIG_CPU_SUBTYPE_SH7091)	+= setup-sh7750.o
obj-$(CONFIG_CPU_SUBTYPE_SH7751)	+= setup-sh7750.o
obj-$(CONFIG_CPU_SUBTYPE_SH7751R)	+= setup-sh7750.o
obj-$(CONFIG_CPU_SUBTYPE_SH7760)	+= setup-sh7760.o
obj-$(CONFIG_CPU_SUBTYPE_SH4_202)	+= setup-sh4-202.o
obj-$(CONFIG_CPU_SUBTYPE_STXH205)	+= setup-stxh205.o
obj-$(CONFIG_CPU_SUBTYPE_STIH415)	+= setup-stih415.o
obj-$(CONFIG_CPU_SUBTYPE_STX7108)	+= setup-stx7108.o

# Primary on-chip clocks (common)
ifndef CONFIG_CPU_SH4A
clock-$(CONFIG_CPU_SH4)			:= clock-sh4.o
clock-$(CONFIG_CPU_SUBTYPE_STXH205)	:= clock-stxh205.o
clock-$(CONFIG_CPU_SUBTYPE_STIH415)	:= clock-stih415.o
clock-$(CONFIG_CPU_SUBTYPE_STX7108)	:= clock-stx7108.o
endif

# Additional clocks by subtype
clock-$(CONFIG_CPU_SUBTYPE_SH4_202)	+= clock-sh4-202.o

obj-y	+= $(clock-y)
obj-$(CONFIG_PERF_EVENTS)		+= $(perf-y)
obj-$(CONFIG_HAVE_HW_BREAKPOINT)	+= ubc.o

obj-$(CONFIG_CPU_SUBTYPE_ST40)		+= stm-tmu.o

cpufreq-y				:= cpufreq-stm_cpu_clk.o cpufreq-stm.o
obj-$(CONFIG_STM_CPU_FREQ)		+= $(cpufreq-y)

suspend-y				:= stm_suspend.o stm_suspend-core.o
suspend-$(CONFIG_CPU_SUBTYPE_STXH205)	+= suspend-stxh205.o
suspend-$(CONFIG_CPU_SUBTYPE_STX7108)	+= suspend-stx7108.o
obj-$(CONFIG_SUSPEND)			+= $(suspend-y)

hom-y					:= stm_hom.o stm_hom-core.o
hom-$(CONFIG_CPU_SUBTYPE_STXH205)	+= hom-stxh205.o
hom-$(CONFIG_CPU_SUBTYPE_STX7108)	+= hom-stx7108.o
obj-$(CONFIG_HIBERNATION_ON_MEMORY)	+= $(hom-y)
