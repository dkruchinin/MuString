/*
 * Feel free to add here all you need.
 * NOTE: all configuration macros that enable or disable something should hav
 * prefix CONFIG_
 * NOTE: all configuration macros that enable or disable some kind of debugging
 * should have prefix DEBUG
 */

#ifndef __CONFIG_H__
#define __CONFIG_H__

/* GENERAL config */
#define CONFIG_SMP
#define CONFIG_ALWAYS_INLINE

/* MM-related */
#define CONFIG_IOMMU
#define IDALLOC_PAGES 2

/* SMP-related */
#define NR_CPUS  2
#define MAX_CPUS 8

/* debugging stuff */
#define DEBUG_GENERAL
#define DEBUG_MM

#define KERNEL_CORE_STACK_PAGES 2

#endif /* __CONFIG_H__ */
