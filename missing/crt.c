/*
 * Our own CRT replacement for Clang
 *
 * This is used to demonstrates how it is possible to create UEFI
 * applications WITHOUT actually linking to any external libraries.
 */

/* This CRT replacement is only needed for ARM compilation using Visual Studio's Clang/C2 */
#if defined(_M_ARM) && defined(__clang__)

#if !defined(__STDC_VERSION__) || (__STDC_VERSION__ < 199901L )
/* ANSI C 1999/2000 stdint.h integer width declarations */
typedef unsigned long long  uint64_t;
typedef long long           int64_t;
typedef unsigned int        uint32_t;
typedef int                 int32_t;
typedef unsigned short      uint16_t;
typedef short               int16_t;
typedef unsigned char       uint8_t;
typedef signed char         int8_t;   // unqualified 'char' is unsigned on ARM
#else
#include <stdint.h>
#endif

int _fltused = 0x9875;

typedef struct {
	uint32_t quotient;
	uint32_t modulus;
} udiv_result_t;

typedef struct {
	int32_t quotient;
	int32_t modulus;
} sdiv_result_t;

typedef struct {
	uint64_t quotient;
	uint64_t modulus;
} udiv64_result_t;

typedef struct {
	uint64_t quotient;
	uint64_t modulus;
} sdiv64_result_t;

static const uint8_t debruijn32[32] = {
	0, 31, 9, 30, 3, 8, 13, 29, 2, 5, 7, 21, 12, 24, 28, 19,
	1, 10, 4, 14, 6, 22, 25, 20, 11, 15, 23, 26, 16, 27, 17, 18
};

__inline uint32_t _CountLeadingZeros(uint32_t x)
{
	if (x == 0) return 32;
	x |= x >> 1;
	x |= x >> 2;
	x |= x >> 4;
	x |= x >> 8;
	x |= x >> 16;
	x++;
	return debruijn32[x * 0x076be629 >> 27];
}

__inline uint32_t _CountLeadingZeros64(uint64_t x)
{
	if ((x >> 32) == 0) {
		return 32 + _CountLeadingZeros((uint32_t)x);
	} else {
		return _CountLeadingZeros((uint32_t)(x >> 32));
	}
}

/* No inline or standalone assembly for halt on div by 0 -> do it ourselves */
typedef void(*__brkdiv0_t)(void);
static const uint32_t ___brkdiv0 = 0xe7f00ff9;	// udf #249
static __brkdiv0_t __brkdiv0 = (__brkdiv0_t)&___brkdiv0;

/* Modified from ReactOS' sdk/lib/crt/math/arm/... */
__inline void __rt_udiv_internal(udiv_result_t *result, uint32_t divisor, uint32_t dividend)
{
	uint32_t shift;
	uint32_t mask;
	uint32_t quotient;

	if (divisor == 0) {
		/* Raise divide by zero error */
		__brkdiv0();
	}

	if (divisor > dividend) {
		result->quotient = 0;
		result->modulus = dividend;
		return;
	}

	/* Get the difference in count of leading zeros between dividend and divisor */
	shift = _CountLeadingZeros(divisor);
	shift -= _CountLeadingZeros(dividend);

	/* Shift the divisor to the left, so that it's highest bit is the same as the
	   highest bit of the dividend */
	divisor <<= shift;

	mask = 1 << shift;

	quotient = 0;
	do {
		if (dividend >= divisor) {
			quotient |= mask;
			dividend -= divisor;
		}
		divisor >>= 1;
		mask >>= 1;
	} while (mask);

	result->quotient = quotient;
	result->modulus = dividend;
	return;
}

uint64_t __rt_sdiv(int32_t divisor, int32_t dividend)
{
	sdiv_result_t result;
	int32_t divisor_sign, dividend_sign;

	dividend_sign = divisor & 0x80000000;
	if (dividend_sign) {
		dividend = -dividend;
	}

	divisor_sign = divisor & 0x80000000;
	if (divisor_sign) {
		divisor = -divisor;
	}

	__rt_udiv_internal((udiv_result_t*)&result, divisor, dividend);

	if (dividend_sign ^ divisor_sign) {
		result.quotient = -result.quotient;
	}

	if (dividend_sign) {
		result.modulus = -result.modulus;
	}

	return (((uint64_t)result.modulus) << 32) | ((uint32_t)result.quotient);
}

uint64_t __rt_udiv(uint32_t divisor, uint32_t dividend)
{
	udiv_result_t result;

	__rt_udiv_internal(&result, divisor, dividend);

	return (((uint64_t)result.modulus) << 32) | ((uint32_t)result.quotient);
}

static __inline void __rt_udiv64_internal(udiv64_result_t *result,
	uint64_t divisor, uint64_t dividend)
{
	uint32_t shift;
	uint64_t mask;
	uint64_t quotient;

	if (divisor == 0) {
		/* Raise divide by zero error */
		__brkdiv0();
	}

	if (divisor > dividend) {
		result->quotient = 0;
		result->modulus = dividend;
		return;
	}

	/* Get the difference in count of leading zeros between dividend and divisor */
	shift = _CountLeadingZeros64(divisor);
	shift -= _CountLeadingZeros64(dividend);

	/* Shift the divisor to the left, so that it's highest bit is the same as the
	   highest bit of the dividend */
	divisor <<= shift;

	mask = 1LL << shift;

	quotient = 0;
	do {
		if (dividend >= divisor) {
			quotient |= mask;
			dividend -= divisor;
		}
		divisor >>= 1;
		mask >>= 1;
	} while (mask);

	result->quotient = quotient;
	result->modulus = dividend;
	return;
}

/*
 * Soooo, what do you do when you have an UNCOOPERATIVE compiler (Microsoft's
 * crippled version of Clang), compiling into some weird intermediate language
 * (C2 or LLVM or whatever), with NO FRIGGING ASSEMBLY, be it inline or
 * standalone, and you want to provide your own version of __rt_udiv64(), that
 * MUST return the result in ARM registers r0/r1/r2/r3?
 * Why, you "just" define an (uint64_t, uint64_t) function call, that points
 * to the binary code for 'mov pc, lr' (i.e. ARM's return from call instruction)
 * since you can then use this call to set r0/r1 to the first call parameter and
 * r2/r3 to the second.
 */
typedef void(*set_return_registers_t)(uint64_t, uint64_t);
static const uint32_t _set_return_registers = 0xe1a0f00e;	// mov pc, lr
static set_return_registers_t set_return_registers = (set_return_registers_t)&_set_return_registers;

void __rt_udiv64(uint64_t divisor, uint64_t dividend)
{
	udiv64_result_t result;

	__rt_udiv64_internal(&result, divisor, dividend);

	set_return_registers(result.quotient, result.modulus);
}

/* Not sure if this one is also needed, but just in case... */
void __rt_sdiv64(int64_t divisor, int64_t dividend)
{
	udiv64_result_t result;
	int64_t divisor_sign, dividend_sign, quotient, modulus;

	dividend_sign = divisor & 0x8000000000000000LL;
	if (dividend_sign) {
		dividend = -dividend;
	}

	divisor_sign = divisor & 0x8000000000000000LL;
	if (divisor_sign) {
		divisor = -divisor;
	}

	__rt_udiv64_internal(&result, divisor, dividend);

	quotient = (dividend_sign ^ divisor_sign) ? -result.quotient : result.quotient;
	modulus = (dividend_sign) ? -result.modulus : result.modulus;
	set_return_registers((uint64_t)quotient, (uint64_t)modulus);
}

#endif
