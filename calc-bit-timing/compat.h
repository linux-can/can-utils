/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _COMPAT_H
#define _COMPAT_H

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>

#include <linux/can/netlink.h>
#include <linux/types.h>

/* imported from kernel */

/* define in-kernel-types */
typedef __u64 u64;
typedef __u32 u32;

#define NSEC_PER_SEC	1000000000L

#define CAN_CALC_MAX_ERROR 50 /* in one-tenth of a percent */
#define CAN_CALC_SYNC_SEG 1
#define CAN_SYNC_SEG 1
#define CAN_KBPS 1000
#define KILO 1000UL

/**
 * abs - return absolute value of an argument
 * @x: the value.  If it is unsigned type, it is converted to signed type first.
 *     char is treated as if it was signed (regardless of whether it really is)
 *     but the macro's return type is preserved as char.
 *
 * Return: an absolute value of x.
 */
#define abs(x)	__abs_choose_expr(x, long long,				\
		__abs_choose_expr(x, long,				\
		__abs_choose_expr(x, int,				\
		__abs_choose_expr(x, short,				\
		__abs_choose_expr(x, char,				\
		__builtin_choose_expr(					\
			__builtin_types_compatible_p(typeof(x), char),	\
			(char)({ signed char __x = (x); __x < 0 ? -__x:__x; }), \
			((void)0)))))))

#define __abs_choose_expr(x, type, other) __builtin_choose_expr(	\
	__builtin_types_compatible_p(typeof(x),   signed type) ||	\
	__builtin_types_compatible_p(typeof(x), unsigned type),		\
	({ signed type __x = (x); __x < 0 ? -__x : __x; }), other)

/*
 * min()/max()/clamp() macros that also do
 * strict type-checking.. See the
 * "unnecessary" pointer comparison.
 */
#define min(x, y) ({				\
	typeof(x) _min1 = (x);			\
	typeof(y) _min2 = (y);			\
	(void) (&_min1 == &_min2);		\
	_min1 < _min2 ? _min1 : _min2; })

#define max(x, y) ({				\
	typeof(x) _max1 = (x);			\
	typeof(y) _max2 = (y);			\
	(void) (&_max1 == &_max2);		\
	_max1 > _max2 ? _max1 : _max2; })

/**
 * clamp - return a value clamped to a given range with strict typechecking
 * @val: current value
 * @lo: lowest allowable value
 * @hi: highest allowable value
 *
 * This macro does strict typechecking of lo/hi to make sure they are of the
 * same type as val.  See the unnecessary pointer comparisons.
 */
#define clamp(val, lo, hi) min((typeof(val))max(val, lo), hi)

#define do_div(n, base) ({					\
	uint32_t __base = (base);				\
	uint32_t __rem;						\
	__rem = ((uint64_t)(n)) % __base;			\
	(n) = ((uint64_t)(n)) / __base;				\
	__rem;							\
})


/**
 * DIV_U64_ROUND_CLOSEST - unsigned 64bit divide with 32bit divisor rounded to nearest integer
 * @dividend: unsigned 64bit dividend
 * @divisor: unsigned 32bit divisor
 *
 * Divide unsigned 64bit dividend by unsigned 32bit divisor
 * and round to closest integer.
 *
 * Return: dividend / divisor rounded to nearest integer
 */
#define DIV_U64_ROUND_CLOSEST(dividend, divisor)	\
	({ u32 _tmp = (divisor); div_u64((u64)(dividend) + _tmp / 2, _tmp); })

/**
 * div_u64_rem - unsigned 64bit divide with 32bit divisor with remainder
 * @dividend: unsigned 64bit dividend
 * @divisor: unsigned 32bit divisor
 * @remainder: pointer to unsigned 32bit remainder
 *
 * Return: sets ``*remainder``, then returns dividend / divisor
 *
 * This is commonly provided by 32bit archs to provide an optimized 64bit
 * divide.
 */
static inline u64 div_u64_rem(u64 dividend, u32 divisor, u32 *remainder)
{
	*remainder = dividend % divisor;
	return dividend / divisor;
}

static inline u64 div_u64(u64 dividend, u32 divisor)
{
	u32 remainder;
	return div_u64_rem(dividend, divisor, &remainder);
}

static inline u64 mul_u32_u32(u32 a, u32 b)
{
	return (u64)a * b;
}

/* */

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

/* we don't want to see these prints */
#define netdev_err(dev, format, arg...) do { } while (0)
#define netdev_warn(dev, format, arg...) do { } while (0)
#define NL_SET_ERR_MSG_FMT(dev, format, arg...) do { } while (0)

struct calc_ref_clk {
	__u32 clk;	/* CAN system clock frequency in Hz */
	const char *name;
};

/*
 * minimal structs, just enough to be source level compatible
 */
struct can_priv {
	struct can_clock clock;
};

struct net_device {
	struct can_priv	priv;
};

struct netlink_ext_ack {
	u32 __dummy;
};

static inline void *netdev_priv(const struct net_device *dev)
{
	return (void *)&dev->priv;
}

#endif	/*_COMPAT_H */
