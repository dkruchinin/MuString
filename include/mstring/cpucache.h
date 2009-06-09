#ifndef __CPUCACHE_H__
#define __CPUCACHE_H__

#include <mstring/stddef.h>
#include <arch/cpucache.h>

#define L1_CACHE_SIZE (1 << L1_CACHE_SHIFT)
#define L1_CACHE_ALIGN(x) align_up(x, L1_CACHE_SIZE)

#define __l1_cache_aligned__ __attribute__((__aligned__(L1_CACHE_SIZE)))

#endif /* __CPUCACHE_H__ */
