/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _LINUX_KERNEL_H
#define _LINUX_KERNEL_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

#include <linux/can.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint32_t __le32;

struct mcp251xfd_mem;

struct regmap {
	struct mcp251xfd_mem *mem;
};

#define pr_info(...) fprintf(stdout, ## __VA_ARGS__)
#define pr_cont(...) fprintf(stdout, ## __VA_ARGS__)
#define netdev_info(ndev, ...) fprintf(stdout, ## __VA_ARGS__)
#define BUILD_BUG_ON(...)

#define BITS_PER_LONG (sizeof(long) * 8)

#define ____cacheline_aligned

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

int regmap_bulk_read(struct regmap *map, unsigned int reg,
		     void *val, size_t val_count);

#define SZ_2K				0x00000800

#define __packed                        __attribute__((__packed__))

#define sizeof_field(TYPE, MEMBER) sizeof((((TYPE *)0)->MEMBER))

#define BIT(nr)			(UL(1) << (nr))

#define __stringify_1(x...)	#x
#define __stringify(x...)	__stringify_1(x)

#ifdef __ASSEMBLY__
#define _AC(X,Y)	X
#define _AT(T,X)	X
#else
#define __AC(X,Y)	(X##Y)
#define _AC(X,Y)	__AC(X,Y)
#define _AT(T,X)	((T)(X))
#endif

#define _UL(x)		(_AC(x, UL))
#define _ULL(x)		(_AC(x, ULL))

#define UL(x)		(_UL(x))
#define ULL(x)		(_ULL(x))

#define GENMASK(h, l) \
	(((~UL(0)) - (UL(1) << (l)) + 1) & \
	 (~UL(0) >> (BITS_PER_LONG - 1 - (h))))

#define __bf_shf(x) (__builtin_ffsll(x) - 1)

#define FIELD_PREP(_mask, _val)						\
	({								\
		((typeof(_mask))(_val) << __bf_shf(_mask)) & (_mask);	\
	})

#define FIELD_GET(_mask, _reg)						\
	({								\
		(typeof(_mask))(((_reg) & (_mask)) >> __bf_shf(_mask));	\
	})

#define min_t(type, x, y) ({			\
	type __min1 = (x);			\
	type __min2 = (y);			\
	__min1 < __min2 ? __min1 : __min2; })

#define get_canfd_dlc(i)	(min_t(__u8, (i), CANFD_MAX_DLC))

static const u8 dlc2len[] = {0, 1, 2, 3, 4, 5, 6, 7,
			     8, 12, 16, 20, 24, 32, 48, 64};

/* get data length from can_dlc with sanitized can_dlc */
static inline u8 can_dlc2len(u8 can_dlc)
{
	return dlc2len[can_dlc & 0x0F];
}

#endif /* _LINUX_KERNEL_H */
