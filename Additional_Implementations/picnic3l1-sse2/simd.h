/*
 *  This file is part of the optimized implementation of the Picnic signature scheme.
 *  See the accompanying documentation for complete details.
 *
 *  The code is provided under the MIT license, see LICENSE for
 *  more details.
 *  SPDX-License-Identifier: MIT
 */

#ifndef SIMD_H
#define SIMD_H


#include "macros.h"

#if defined(_MSC_VER)
#include <immintrin.h>
#elif defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
#include <x86intrin.h>
#elif defined(__GNUC__) && defined(__ARM_NEON)
#include <arm_neon.h>
#endif

#include "cpu.h"

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#if defined(BUILTIN_CPU_SUPPORTED)
#if !defined(BUILTIN_CPU_SUPPORTED_BROKEN_BMI2)
#define CPU_SUPPORTS_AVX2 (__builtin_cpu_supports("avx2") && __builtin_cpu_supports("bmi2"))
#else
#define CPU_SUPPORTS_AVX2 (__builtin_cpu_supports("avx2") && cpu_supports(CPU_CAP_BMI2))
#endif
#define CPU_SUPPORTS_POPCNT __builtin_cpu_supports("popcnt")
#else
#define CPU_SUPPORTS_AVX2 cpu_supports(CPU_CAP_AVX2 | CPU_CAP_BMI2)
#define CPU_SUPPORTS_POPCNT cpu_supports(CPU_CAP_POPCNT)
#endif
#endif

#if defined(__x86_64__) || defined(_M_X64)
// X86-64 CPUs always support SSE2
#define CPU_SUPPORTS_SSE2 1
#define NO_UINT64_FALLBACK
#elif defined(__i386__) || defined(_M_IX86)
#if defined(BUILTIN_CPU_SUPPORTED)
#define CPU_SUPPORTS_SSE2 __builtin_cpu_supports("sse2")
#else
#define CPU_SUPPORTS_SSE2 cpu_supports(CPU_CAP_SSE2)
#endif
#else
#define CPU_SUPPORTS_SSE2 0
#endif

#if defined(__aarch64__)
#define CPU_SUPPORTS_NEON 1
#elif defined(__arm__)
#define CPU_SUPPRTS_NEON cpu_supports(CPU_CAP_NEON)
#else
#define CPU_SUPPORTS_NEON 0
#endif

#if defined(_MSC_VER)
#define restrict __restrict
#endif

#define ATTR_TARGET_S128 ATTR_TARGET_SSE2

#define ATTR_TARGET_S256

/* backwards compatibility macros for GCC 4.8 and 4.9
 *
 * bs{l,r}i was introduced in GCC 5 and in clang as macros sometime in 2015.
 * */
#if (!defined(__clang__) && defined(__GNUC__) && __GNUC__ < 5) ||                                  \
    ((defined(__clang__) || defined(_MSC_VER)) && !defined(_mm_bslli_si128))
#define _mm_bslli_si128(a, imm) _mm_slli_si128((a), (imm))
#define _mm_bsrli_si128(a, imm) _mm_srli_si128((a), (imm))
#endif

#define apply_region(name, type, xor, attributes)                                                  \
  static inline void attributes name(type* restrict dst, type const* restrict src,                 \
                                     unsigned int count) {                                         \
    for (unsigned int i = count; i; --i, ++dst, ++src) {                                           \
      *dst = xor(*dst, *src);                                                                      \
    }                                                                                              \
  }

#define apply_mask(name, type, xor, and, attributes)                                               \
  static inline type attributes name(const type lhs, const type rhs, const type mask) {            \
    return xor(lhs, and(rhs, mask));                                                               \
  }

#define apply_mask_region(name, type, xor, and, attributes)                                        \
  static inline void attributes name(type* restrict dst, type const* restrict src,                 \
                                     type const mask, unsigned int count) {                        \
    for (unsigned int i = count; i; --i, ++dst, ++src) {                                           \
      *dst = xor(*dst, and(*src, mask));                                                           \
    }                                                                                              \
  }

#define apply_array(name, type, xor, count, attributes)                                            \
  static inline void attributes name(type dst[count], type const lhs[count],                       \
                                     type const rhs[count]) {                                      \
    type* d       = dst;                                                                           \
    const type* l = lhs;                                                                           \
    const type* r = rhs;                                                                           \
    for (unsigned int i = count; i; --i, ++d, ++l, ++r) {                                          \
      *d = xor(*l, *r);                                                                            \
    }                                                                                              \
  }


typedef __m128i word128;

#define mm128_zero _mm_setzero_si128()
#define mm128_xor(l, r) _mm_xor_si128((l), (r))
#define mm128_and(l, r) _mm_and_si128((l), (r))
/* !l & r */
#define mm128_nand(l, r) _mm_andnot_si128((l), (r))
#define mm128_broadcast_u64(x) _mm_set1_epi64x((x))
#define mm128_sl_u64(x, s) _mm_slli_epi64((x), (s))
#define mm128_sr_u64(x, s) _mm_srli_epi64((x), (s))

apply_region(mm128_xor_region, word128, mm128_xor, FN_ATTRIBUTES_SSE2)
apply_mask_region(mm128_xor_mask_region, word128, mm128_xor, mm128_and, FN_ATTRIBUTES_SSE2)
apply_mask(mm128_xor_mask, word128, mm128_xor, mm128_and, FN_ATTRIBUTES_SSE2_CONST)
apply_array(mm128_xor_256, word128, mm128_xor, 2, FN_ATTRIBUTES_SSE2)
apply_array(mm128_and_256, word128, mm128_and, 2, FN_ATTRIBUTES_SSE2)

#define mm128_shift_left(data, count)                                                              \
  _mm_or_si128(_mm_slli_epi64(data, count), _mm_srli_epi64(_mm_bslli_si128(data, 8), 64 - count))

#define mm128_shift_right(data, count)                                                             \
  _mm_or_si128(_mm_srli_epi64(data, count), _mm_slli_epi64(_mm_bsrli_si128(data, 8), 64 - count))

#define mm128_rotate_left(data, count)                                                             \
  _mm_or_si128(_mm_slli_epi64(data, count),                                                        \
               _mm_shuffle_epi32(_mm_srli_epi64(data, 64 - count), _MM_SHUFFLE(1, 0, 3, 2)))

#define mm128_rotate_right(data, count)                                                            \
  _mm_or_si128(_mm_srli_epi64(data, count),                                                        \
               _mm_shuffle_epi32(_mm_slli_epi64(data, 64 - count), _MM_SHUFFLE(1, 0, 3, 2)))

static inline void FN_ATTRIBUTES_SSE2 mm128_shift_right_256(__m128i res[2], __m128i const data[2],
                                                            const unsigned int count) {
  __m128i total_carry = _mm_bslli_si128(data[1], 8);
  total_carry         = _mm_slli_epi64(total_carry, 64 - count);
  for (unsigned int i = 0; i < 2; ++i) {
    __m128i carry = _mm_bsrli_si128(data[i], 8);
    carry         = _mm_slli_epi64(carry, 64 - count);
    res[i]        = _mm_srli_epi64(data[i], count);
    res[i]        = _mm_or_si128(res[i], carry);
  }
  res[0] = _mm_or_si128(res[0], total_carry);
}

static inline void FN_ATTRIBUTES_SSE2 mm128_shift_left_256(__m128i res[2], __m128i const data[2],
                                                           const unsigned int count) {
  __m128i total_carry = _mm_bsrli_si128(data[0], 8);
  total_carry         = _mm_srli_epi64(total_carry, 64 - count);

  for (unsigned int i = 0; i < 2; ++i) {
    __m128i carry = _mm_bslli_si128(data[i], 8);

    carry  = _mm_srli_epi64(carry, 64 - count);
    res[i] = _mm_slli_epi64(data[i], count);
    res[i] = _mm_or_si128(res[i], carry);
  }
  res[1] = _mm_or_si128(res[1], total_carry);
}

/* shift left by 64 to 127 bits */
#define mm128_shift_left_64_127(data, count) _mm_slli_epi64(_mm_bslli_si128(data, 8), count - 64)
/* shift right by 64 to 127 bits */
#define mm128_shift_right_64_127(data, count) _mm_srli_epi64(_mm_bsrli_si128(data, 8), count - 64)

static inline void FN_ATTRIBUTES_SSE2 mm128_rotate_left_256(__m128i res[2], __m128i const data[2],
                                                            const unsigned int count) {
  const __m128i carry = mm128_shift_right_64_127(data[0], 128 - count);

  res[0] = _mm_or_si128(mm128_shift_left(data[0], count),
                        mm128_shift_right_64_127(data[1], 128 - count));
  res[1] = _mm_or_si128(mm128_shift_left(data[1], count), carry);
}

static inline void FN_ATTRIBUTES_SSE2 mm128_rotate_right_256(__m128i res[2], __m128i const data[2],
                                                             const unsigned int count) {
  const __m128i carry = mm128_shift_left_64_127(data[0], 128 - count);

  res[0] = _mm_or_si128(mm128_shift_right(data[0], count),
                        mm128_shift_left_64_127(data[1], 128 - count));
  res[1] = _mm_or_si128(mm128_shift_right(data[1], count), carry);
}


#if defined(_MSC_VER)
#undef restrict
#endif

#undef apply_region
#undef apply_mask_region
#undef apply_array
#undef BUILTIN_CPU_SUPPORTED

#endif
