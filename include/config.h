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

/* SMP-related */
#define NR_CPUS  2
#define MAX_CPUS 8

/* debugging stuff */
#define DEBUG_GENERAL

#endif /* __CONFIG_H__ */
