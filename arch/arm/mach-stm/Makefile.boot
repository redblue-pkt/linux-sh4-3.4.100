   zreladdr-y	:= 0x40008000
params_phys-y	:= 0x40000100
initrd_phys-y	:= 0x40800000

# STiH407 (Monaco family) kernel supports all boards in same binary
dtb-$(CONFIG_MACH_STM_STIH407) += stih407-b2120.dtb stih305-b2120.dtb \
				  stih410-b2120.dtb stih310-b2120.dtb \
				  stih407-b2089.dtb stih305-b2089.dtb \
				  stih410-b2089.dtb stih310-b2089.dtb \
				  stih407-b2148.dtb stih305-b2148.dtb \
				  stih410-b2148.dtb stih310-b2148.dtb

# STiD127 (Alicante cut 1) kernel supports all boards in same binary
dtb-$(CONFIG_MACH_STM_STID127) += stid127-b2112.dtb stid127-b2110.dtb \
				  stid127-b2147.dtb stid127-b2147bc.dtb

# STiH415 (Orly-1) boards
dtb-$(CONFIG_MACH_STM_B2000_STIH415) += stih415-b2000.dtb
dtb-$(CONFIG_MACH_STM_B2020_STIH415) += stih415-b2020.dtb

# STiH416 (Orly-2) boards
dtb-$(CONFIG_MACH_STM_B2000_STIH416) += stih416-b2000.dtb
dtb-$(CONFIG_MACH_STM_B2020_STIH416) += stih416-b2020.dtb stih416-b2020-reve.dtsp
dtb-$(CONFIG_MACH_STM_B2105) += stih416-b2105.dtb
dtb-$(CONFIG_MACH_STM_B2116) += stih315-b2116.dtb
dtb-$(CONFIG_MACH_STM_B2092) += stih416-b2092.dtb
