#ifndef STM_SOC_H
#define STM_SOC_H

extern unsigned long stm_soc_devid;
extern long stm_soc_version_major_id;
extern long stm_soc_version_minor_id;

void stm_soc_set(unsigned long devid, long major, long minor);
const char *stm_soc(void);

static inline long stm_soc_version_major(void)
{
	return stm_soc_version_major_id;
}

static inline long stm_soc_version_minor(void)
{
	return stm_soc_version_minor_id;
}

#ifdef CONFIG_OF
/* Common implementation for machine restart callback */
void stm_of_soc_reset(char mode, const char *cmd);
#endif

/*
 * When we get round to supporting multi-SoC binary kernels these will need
 * to be modified to perform a run time test of which SoC we are actually
 * running on.
 */

#ifdef CONFIG_CPU_SUBTYPE_FLI7610
#define stm_soc_is_fli7610()    (1)
#else
#define stm_soc_is_fli7610()    (0)
#endif

#ifdef CONFIG_MACH_STM_STID127
#define stm_soc_is_stid127()    (1)
#else
#define stm_soc_is_stid127()    (0)
#endif

#ifdef CONFIG_CPU_SUBTYPE_STXH205
#define stm_soc_is_stxh205()	(1)
#else
#define stm_soc_is_stxh205()	(0)
#endif

#ifdef CONFIG_MACH_STM_STIH407
#define stm_soc_is_stih407()	(1)
#else
#define stm_soc_is_stih407()	(0)
#endif

#ifdef CONFIG_CPU_SUBTYPE_STIH415
#define stm_soc_is_stih415()	(1)
#else
#define stm_soc_is_stih415()	(0)
#endif

#ifdef CONFIG_CPU_SUBTYPE_STX7108
#define stm_soc_is_stx7108()	(1)
#else
#define stm_soc_is_stx7108()	(0)
#endif

#ifdef CONFIG_MACH_STM_STIH416
#define stm_soc_is_stih416()	(1)
#else
#define stm_soc_is_stih416()	(0)
#endif

#endif /* STM_SOC_H */
