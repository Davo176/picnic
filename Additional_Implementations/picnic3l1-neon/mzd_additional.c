/*
 *  This file is part of the optimized implementation of the Picnic signature scheme.
 *  See the accompanying documentation for complete details.
 *
 *  The code is provided under the MIT license, see LICENSE for
 *  more details.
 *  SPDX-License-Identifier: MIT
 */


#if !defined(_MSC_VER)
#include <stdalign.h>
#endif
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "compat.h"
#include "mzd_additional.h"

#if !defined(_MSC_VER) && !defined(static_assert)
#define static_assert _Static_assert
#endif

static_assert(((sizeof(mzd_local_t) + 0x1f) & ~0x1f) == 32, "sizeof mzd_local_t not supported");

#include "simd.h"
static const unsigned int align_bound = 128 / (8 * sizeof(word));

static size_t calculate_rowstride(size_t width) {
  // As soon as we hit the AVX bound, use 32 byte alignment. Otherwise use 16
  // byte alignment for SSE2 and 128 bit vectors.
  if (width > align_bound) {
    return ((width * sizeof(word) + 31) & ~31) / sizeof(word);
  } else {
    return ((width * sizeof(word) + 15) & ~15) / sizeof(word);
  }
}

static size_t calculate_width(size_t c) {
  return (c + sizeof(word) * 8 - 1) / (sizeof(word) * 8);
}

// Notes on the memory layout: mzd_init allocates multiple memory blocks (one
// for mzd_local_t, one for rows and multiple for the buffers). We use one memory
// block for mzd_local_t, rows and the buffer. This improves memory locality and
// requires less calls to malloc.
//
// In mzd_local_init_multiple we do the same, but store n mzd_local_t instances in one
// memory block.

mzd_local_t* mzd_local_init_ex(unsigned int r, unsigned int c, bool clear) {
  const size_t rowstride = calculate_rowstride(calculate_width(c));

  const size_t buffer_size = r * rowstride * sizeof(word);
  const size_t alloc_size  = (buffer_size + 31) & ~31;

  /* We always align mzd_local_ts to 32 bytes. Thus the first row is always
   * aligned to 32 bytes as well. For 128 bit and SSE all other rows are then
   * aligned to 16 bytes. */
  unsigned char* buffer = aligned_alloc(32, alloc_size);
  if (clear) {
    memset(buffer, 0, alloc_size);
  }

  return (mzd_local_t*)buffer;
}

void mzd_local_free(mzd_local_t* v) {
  aligned_free(v);
}

void mzd_local_init_multiple_ex(mzd_local_t** dst, size_t n, unsigned int r, unsigned int c, bool clear) {
  const size_t rowstride = calculate_rowstride(calculate_width(c));

  const size_t buffer_size   = r * rowstride * sizeof(word);
  const size_t size_per_elem = (buffer_size + 31) & ~31;

  unsigned char* full_buffer = aligned_alloc(32, size_per_elem * n);
  if (clear) {
    memset(full_buffer, 0, size_per_elem * n);
  }

  for (size_t s = 0; s < n; ++s, full_buffer += size_per_elem) {
    dst[s] = (mzd_local_t*)full_buffer;
  }
}

void mzd_local_free_multiple(mzd_local_t** vs) {
  if (vs) {
    aligned_free(vs[0]);
  }
}

/* implementation of copy */

void mzd_copy_uint64_128(mzd_local_t* dst, mzd_local_t const* src) {
  const block_t* sblock = CONST_BLOCK(src, 0);
  block_t* dblock       = BLOCK(dst, 0);

  for (unsigned int i = 0; i < 2; ++i) {
    dblock->w64[i] = sblock->w64[i];
  }
}

void mzd_copy_uint64_192(mzd_local_t* dst, mzd_local_t const* src) {
  const block_t* sblock = CONST_BLOCK(src, 0);
  block_t* dblock       = BLOCK(dst, 0);

  for (unsigned int i = 0; i < 3; ++i) {
    dblock->w64[i] = sblock->w64[i];
  }
}

void mzd_copy_uint64_256(mzd_local_t* dst, mzd_local_t const* src) {
  const block_t* sblock = CONST_BLOCK(src, 0);
  block_t* dblock       = BLOCK(dst, 0);

  for (unsigned int i = 0; i < 4; ++i) {
    dblock->w64[i] = sblock->w64[i];
  }
}

ATTR_TARGET_S128
void mzd_copy_s128_128(mzd_local_t* dst, mzd_local_t const* src) {
  BLOCK(dst, 0)->w128[0] = CONST_BLOCK(src, 0)->w128[0];
}

ATTR_TARGET_S128
void mzd_copy_s128_256(mzd_local_t* dst, mzd_local_t const* src) {
  for (unsigned int i = 0; i < 2; ++i) {
    dst->w128[i] = src->w128[i];
  }
}


/* implementation of mzd_xor and variants */

ATTR_TARGET_S128
void mzd_xor_s128_128(mzd_local_t* res, mzd_local_t const* first, mzd_local_t const* second) {
  block_t* rblock       = BLOCK(res, 0);
  const block_t* fblock = CONST_BLOCK(first, 0);
  const block_t* sblock = CONST_BLOCK(second, 0);

  rblock->w128[0] = mm128_xor(fblock->w128[0], sblock->w128[0]);
}

ATTR_TARGET_S128
static void mzd_xor_s128_blocks(block_t* rblock, const block_t* fblock, const block_t* sblock,
                                unsigned int count) {
  for (; count; --count, ++rblock, ++fblock, ++sblock) {
    rblock->w128[0] = mm128_xor(fblock->w128[0], sblock->w128[0]);
    rblock->w128[1] = mm128_xor(fblock->w128[1], sblock->w128[1]);
  }
}

ATTR_TARGET_S128
void mzd_xor_s128_256(mzd_local_t* res, mzd_local_t const* first, mzd_local_t const* second) {
  mzd_xor_s128_blocks(BLOCK(res, 0), CONST_BLOCK(first, 0), CONST_BLOCK(second, 0), 1);
}





static void mzd_xor_uint64_block(block_t* rblock, const block_t* fblock, const block_t* sblock,
                                 const unsigned int len) {
  for (unsigned int i = 0; i < len; ++i) {
    rblock->w64[i] = fblock->w64[i] ^ sblock->w64[i];
  }
}


void mzd_xor_uint64_128(mzd_local_t* res, mzd_local_t const* first, mzd_local_t const* second) {
  mzd_xor_uint64_block(BLOCK(res, 0), CONST_BLOCK(first, 0), CONST_BLOCK(second, 0), 2);
}

void mzd_xor_uint64_192(mzd_local_t* res, mzd_local_t const* first, mzd_local_t const* second) {
  mzd_xor_uint64_block(BLOCK(res, 0), CONST_BLOCK(first, 0), CONST_BLOCK(second, 0), 3);
}

void mzd_xor_uint64_256(mzd_local_t* res, mzd_local_t const* first, mzd_local_t const* second) {
  mzd_xor_uint64_block(BLOCK(res, 0), CONST_BLOCK(first, 0), CONST_BLOCK(second, 0), 4);
}




/* implementation of mzd_and_* and variants */

ATTR_TARGET_S128
void mzd_and_s128_128(mzd_local_t* res, mzd_local_t const* first, mzd_local_t const* second) {
  block_t* rblock       = BLOCK(res, 0);
  const block_t* fblock = CONST_BLOCK(first, 0);
  const block_t* sblock = CONST_BLOCK(second, 0);

  rblock->w128[0] = mm128_and(fblock->w128[0], sblock->w128[0]);
}

ATTR_TARGET_S128
static inline void mzd_and_s128_blocks(block_t* rblock, const block_t* fblock,
                                       const block_t* sblock, unsigned int count) {
  for (; count; --count, ++rblock, ++fblock, ++sblock) {
    rblock->w128[0] = mm128_and(fblock->w128[0], sblock->w128[0]);
    rblock->w128[1] = mm128_and(fblock->w128[1], sblock->w128[1]);
  }
}

ATTR_TARGET_S128
void mzd_and_s128_256(mzd_local_t* res, mzd_local_t const* first, mzd_local_t const* second) {
  mzd_and_s128_blocks(BLOCK(res, 0), CONST_BLOCK(first, 0), CONST_BLOCK(second, 0), 1);
}


static inline void mzd_and_uint64_block(block_t* rblock, const block_t* fblock,
                                        const block_t* sblock, const unsigned int len) {
  for (unsigned int i = 0; i < len; ++i) {
    rblock->w64[i] = fblock->w64[i] & sblock->w64[i];
  }
}

void mzd_and_uint64_128(mzd_local_t* res, mzd_local_t const* first, mzd_local_t const* second) {
  mzd_and_uint64_block(BLOCK(res, 0), CONST_BLOCK(first, 0), CONST_BLOCK(second, 0), 2);
}

void mzd_and_uint64_192(mzd_local_t* res, mzd_local_t const* first, mzd_local_t const* second) {
  mzd_and_uint64_block(BLOCK(res, 0), CONST_BLOCK(first, 0), CONST_BLOCK(second, 0), 3);
}

void mzd_and_uint64_256(mzd_local_t* res, mzd_local_t const* first, mzd_local_t const* second) {
  mzd_and_uint64_block(BLOCK(res, 0), CONST_BLOCK(first, 0), CONST_BLOCK(second, 0), 4);
}

/* shifts and rotations */

void mzd_shift_left_uint64_128(mzd_local_t* res, const mzd_local_t* val, unsigned int count) {
  const unsigned int right_count = 8 * sizeof(word) - count;
  const block_t* block           = CONST_BLOCK(val, 0);
  block_t* rblock                = BLOCK(res, 0);

  rblock->w64[1] = (block->w64[1] << count) | (block->w64[0] >> right_count);
  rblock->w64[0] = block->w64[0] << count;
}

void mzd_shift_right_uint64_128(mzd_local_t* res, const mzd_local_t* val, unsigned int count) {
  const unsigned int left_count = 8 * sizeof(word) - count;
  const block_t* block          = CONST_BLOCK(val, 0);
  block_t* rblock               = BLOCK(res, 0);

  rblock->w64[0] = (block->w64[0] >> count) | (block->w64[1] << left_count);
  rblock->w64[1] = block->w64[1] >> count;
}

void mzd_shift_left_uint64_192(mzd_local_t* res, const mzd_local_t* val, unsigned int count) {
  const unsigned int right_count = 8 * sizeof(word) - count;
  const block_t* block           = CONST_BLOCK(val, 0);
  block_t* rblock                = BLOCK(res, 0);

  rblock->w64[2] = (block->w64[2] << count) | (block->w64[1] >> right_count);
  rblock->w64[1] = (block->w64[1] << count) | (block->w64[0] >> right_count);
  rblock->w64[0] = block->w64[0] << count;
}

void mzd_shift_right_uint64_192(mzd_local_t* res, const mzd_local_t* val, unsigned int count) {
  const unsigned int left_count = 8 * sizeof(word) - count;
  const block_t* block          = CONST_BLOCK(val, 0);
  block_t* rblock               = BLOCK(res, 0);

  rblock->w64[0] = (block->w64[0] >> count) | (block->w64[1] << left_count);
  rblock->w64[1] = (block->w64[1] >> count) | (block->w64[2] << left_count);
  rblock->w64[2] = block->w64[2] >> count;
}

void mzd_shift_left_uint64_256(mzd_local_t* res, const mzd_local_t* val, unsigned int count) {
  const unsigned int right_count = 8 * sizeof(word) - count;
  const block_t* block           = CONST_BLOCK(val, 0);
  block_t* rblock                = BLOCK(res, 0);

  rblock->w64[3] = (block->w64[3] << count) | (block->w64[2] >> right_count);
  rblock->w64[2] = (block->w64[2] << count) | (block->w64[1] >> right_count);
  rblock->w64[1] = (block->w64[1] << count) | (block->w64[0] >> right_count);
  rblock->w64[0] = block->w64[0] << count;
}

void mzd_shift_right_uint64_256(mzd_local_t* res, const mzd_local_t* val, unsigned int count) {
  const unsigned int left_count = 8 * sizeof(word) - count;
  const block_t* block          = CONST_BLOCK(val, 0);
  block_t* rblock               = BLOCK(res, 0);

  rblock->w64[0] = (block->w64[0] >> count) | (block->w64[1] << left_count);
  rblock->w64[1] = (block->w64[1] >> count) | (block->w64[2] << left_count);
  rblock->w64[2] = (block->w64[2] >> count) | (block->w64[3] << left_count);
  rblock->w64[3] = block->w64[3] >> count;
}

#if defined(PICNIC_STATIC)
void mzd_rotate_left_uint64_128(mzd_local_t* res, const mzd_local_t* val, unsigned int count) {
  const unsigned int right_count = 8 * sizeof(word) - count;
  const block_t* block           = CONST_BLOCK(val, 0);
  block_t* rblock                = BLOCK(res, 0);

  const word tmp = block->w64[1] >> right_count;
  rblock->w64[1] = (block->w64[1] << count) | (block->w64[0] >> right_count);
  rblock->w64[0] = (block->w64[0] << count) | tmp;
}

void mzd_rotate_right_uint64_128(mzd_local_t* res, const mzd_local_t* val, unsigned int count) {
  const unsigned int left_count = 8 * sizeof(word) - count;
  const block_t* block          = CONST_BLOCK(val, 0);
  block_t* rblock               = BLOCK(res, 0);

  const word tmp = block->w64[0] << left_count;
  rblock->w64[0] = (block->w64[0] >> count) | (block->w64[1] << left_count);
  rblock->w64[1] = (block->w64[1] >> count) | tmp;
}

void mzd_rotate_left_uint64_192(mzd_local_t* res, const mzd_local_t* val, unsigned int count) {
  const unsigned int right_count = 8 * sizeof(word) - count;
  const block_t* block           = CONST_BLOCK(val, 0);
  block_t* rblock                = BLOCK(res, 0);

  const word tmp = block->w64[2] >> right_count;
  rblock->w64[2] = (block->w64[2] << count) | (block->w64[1] >> right_count);
  rblock->w64[1] = (block->w64[1] << count) | (block->w64[0] >> right_count);
  rblock->w64[0] = (block->w64[0] << count) | tmp;
}

void mzd_rotate_right_uint64_192(mzd_local_t* res, const mzd_local_t* val, unsigned int count) {
  const unsigned int left_count = 8 * sizeof(word) - count;
  const block_t* block          = CONST_BLOCK(val, 0);
  block_t* rblock               = BLOCK(res, 0);

  const word tmp = block->w64[0] << left_count;
  rblock->w64[0] = (block->w64[0] >> count) | (block->w64[1] << left_count);
  rblock->w64[1] = (block->w64[1] >> count) | (block->w64[2] << left_count);
  rblock->w64[2] = (block->w64[2] >> count) | tmp;
}

void mzd_rotate_left_uint64_256(mzd_local_t* res, const mzd_local_t* val, unsigned int count) {
  const unsigned int right_count = 8 * sizeof(word) - count;
  const block_t* block           = CONST_BLOCK(val, 0);
  block_t* rblock                = BLOCK(res, 0);

  const word tmp = block->w64[3] >> right_count;
  rblock->w64[3] = (block->w64[3] << count) | (block->w64[2] >> right_count);
  rblock->w64[2] = (block->w64[2] << count) | (block->w64[1] >> right_count);
  rblock->w64[1] = (block->w64[1] << count) | (block->w64[0] >> right_count);
  rblock->w64[0] = (block->w64[0] << count) | tmp;
}

void mzd_rotate_right_uint64_256(mzd_local_t* res, const mzd_local_t* val, unsigned int count) {
  const unsigned int left_count = 8 * sizeof(word) - count;
  const block_t* block          = CONST_BLOCK(val, 0);
  block_t* rblock               = BLOCK(res, 0);

  const word tmp = block->w64[0] << left_count;
  rblock->w64[0] = (block->w64[0] >> count) | (block->w64[1] << left_count);
  rblock->w64[1] = (block->w64[1] >> count) | (block->w64[2] << left_count);
  rblock->w64[2] = (block->w64[2] >> count) | (block->w64[3] << left_count);
  rblock->w64[3] = (block->w64[3] >> count) | tmp;
}
#endif




ATTR_TARGET_S128 ATTR_ARTIFICIAL ATTR_CONST static inline word128
mm128_compute_mask(const word idx, const size_t bit) {
  return mm128_broadcast_u64(-((idx >> bit) & 1));
}

ATTR_TARGET_S128
void mzd_mul_v_s128_128(mzd_local_t* c, mzd_local_t const* v, mzd_local_t const* A) {
  block_t* cblock       = BLOCK(c, 0);
  const word* vptr      = CONST_BLOCK(v, 0)->w64;
  const block_t* Ablock = CONST_BLOCK(A, 0);

  word128 cval[4] ATTR_ALIGNED(alignof(word128)) = {mm128_zero, mm128_zero, mm128_zero, mm128_zero};
  for (unsigned int w = 2; w; --w, ++vptr) {
    word idx = *vptr;
    for (unsigned int i = sizeof(word) * 8; i; i -= 4, idx >>= 4, Ablock += 2) {
      cval[0] = mm128_xor_mask(cval[0], Ablock[0].w128[0], mm128_compute_mask(idx, 0));
      cval[1] = mm128_xor_mask(cval[1], Ablock[0].w128[1], mm128_compute_mask(idx, 1));
      cval[0] = mm128_xor_mask(cval[0], Ablock[1].w128[0], mm128_compute_mask(idx, 2));
      cval[1] = mm128_xor_mask(cval[1], Ablock[1].w128[1], mm128_compute_mask(idx, 3));
    }
  }
  cblock->w128[0] = mm128_xor(cval[0], cval[1]);
}

ATTR_TARGET_S128
void mzd_addmul_v_s128_128(mzd_local_t* c, mzd_local_t const* v, mzd_local_t const* A) {
  block_t* cblock       = BLOCK(c, 0);
  const word* vptr      = CONST_BLOCK(v, 0)->w64;
  const block_t* Ablock = CONST_BLOCK(A, 0);

  word128 cval[4] ATTR_ALIGNED(alignof(word128)) = {cblock->w128[0], mm128_zero, mm128_zero,
                                                    mm128_zero};
  for (unsigned int w = 2; w; --w, ++vptr) {
    word idx = *vptr;
    for (unsigned int i = sizeof(word) * 8; i; i -= 4, idx >>= 4, Ablock += 2) {
      cval[0] = mm128_xor_mask(cval[0], Ablock[0].w128[0], mm128_compute_mask(idx, 0));
      cval[1] = mm128_xor_mask(cval[1], Ablock[0].w128[1], mm128_compute_mask(idx, 1));
      cval[0] = mm128_xor_mask(cval[0], Ablock[1].w128[0], mm128_compute_mask(idx, 2));
      cval[1] = mm128_xor_mask(cval[1], Ablock[1].w128[1], mm128_compute_mask(idx, 3));
    }
  }
  cblock->w128[0] = mm128_xor(cval[0], cval[1]);
}

ATTR_TARGET_S128
void mzd_mul_v_s128_129(mzd_local_t* c, mzd_local_t const* v, mzd_local_t const* A) {
  block_t* cblock       = BLOCK(c, 0);
  const word* vptr      = CONST_BLOCK(v, 0)->w64;
  const block_t* Ablock = CONST_BLOCK(A, 0);

  word128 cval[4] ATTR_ALIGNED(alignof(word128)) = {mm128_zero, mm128_zero, mm128_zero, mm128_zero};
  {
    Ablock += 63;
    word idx = (*vptr) >> 63;
    mm128_xor_mask_region(&cval[0], Ablock[0].w128, mm128_compute_mask(idx, 0), 2);
    vptr++;
    Ablock++;
  }
  for (unsigned int w = 2; w; --w, ++vptr) {
    word idx = *vptr;
    for (unsigned int i = sizeof(word) * 8; i; i -= 2, idx >>= 2, Ablock += 2) {
      mm128_xor_mask_region(&cval[0], Ablock[0].w128, mm128_compute_mask(idx, 0), 2);
      mm128_xor_mask_region(&cval[2], Ablock[1].w128, mm128_compute_mask(idx, 1), 2);
    }
  }
  cblock->w128[0] = mm128_xor(cval[0], cval[2]);
  cblock->w128[1] = mm128_xor(cval[1], cval[3]);
}

ATTR_TARGET_S128
void mzd_addmul_v_s128_129(mzd_local_t* c, mzd_local_t const* v, mzd_local_t const* A) {
  block_t* cblock       = BLOCK(c, 0);
  const word* vptr      = CONST_BLOCK(v, 0)->w64;
  const block_t* Ablock = CONST_BLOCK(A, 0);

  word128 cval[4] ATTR_ALIGNED(alignof(word128)) = {cblock->w128[0], cblock->w128[1], mm128_zero,
                                                    mm128_zero};
  {
    Ablock += 63;
    word idx = (*vptr) >> 63;
    mm128_xor_mask_region(&cval[0], Ablock[0].w128, mm128_compute_mask(idx, 0), 2);
    vptr++;
    Ablock++;
  }
  for (unsigned int w = 2; w; --w, ++vptr) {
    word idx = *vptr;
    for (unsigned int i = sizeof(word) * 8; i; i -= 2, idx >>= 2, Ablock += 2) {
      mm128_xor_mask_region(&cval[0], Ablock[0].w128, mm128_compute_mask(idx, 0), 2);
      mm128_xor_mask_region(&cval[2], Ablock[1].w128, mm128_compute_mask(idx, 1), 2);
    }
  }
  cblock->w128[0] = mm128_xor(cval[0], cval[2]);
  cblock->w128[1] = mm128_xor(cval[1], cval[3]);
}

ATTR_TARGET_S128
void mzd_mul_v_s128_192(mzd_local_t* c, mzd_local_t const* v, mzd_local_t const* A) {
  block_t* cblock       = BLOCK(c, 0);
  const word* vptr      = CONST_BLOCK(v, 0)->w64;
  const block_t* Ablock = CONST_BLOCK(A, 0);

  word128 cval[4] ATTR_ALIGNED(alignof(word128)) = {mm128_zero, mm128_zero, mm128_zero, mm128_zero};
  for (unsigned int w = 3; w; --w, ++vptr) {
    word idx = *vptr;
    for (unsigned int i = sizeof(word) * 8; i; i -= 2, idx >>= 2, Ablock += 2) {
      mm128_xor_mask_region(&cval[0], Ablock[0].w128, mm128_compute_mask(idx, 0), 2);
      mm128_xor_mask_region(&cval[2], Ablock[1].w128, mm128_compute_mask(idx, 1), 2);
    }
  }
  cblock->w128[0] = mm128_xor(cval[0], cval[2]);
  cblock->w128[1] = mm128_xor(cval[1], cval[3]);
}

ATTR_TARGET_S128
void mzd_addmul_v_s128_192(mzd_local_t* c, mzd_local_t const* v, mzd_local_t const* A) {
  block_t* cblock       = BLOCK(c, 0);
  const word* vptr      = CONST_BLOCK(v, 0)->w64;
  const block_t* Ablock = CONST_BLOCK(A, 0);

  word128 cval[4] ATTR_ALIGNED(alignof(word128)) = {cblock->w128[0], cblock->w128[1], mm128_zero,
                                                    mm128_zero};
  for (unsigned int w = 3; w; --w, ++vptr) {
    word idx = *vptr;
    for (unsigned int i = sizeof(word) * 8; i; i -= 2, idx >>= 2, Ablock += 2) {
      mm128_xor_mask_region(&cval[0], Ablock[0].w128, mm128_compute_mask(idx, 0), 2);
      mm128_xor_mask_region(&cval[2], Ablock[1].w128, mm128_compute_mask(idx, 1), 2);
    }
  }
  cblock->w128[0] = mm128_xor(cval[0], cval[2]);
  cblock->w128[1] = mm128_xor(cval[1], cval[3]);
}

ATTR_TARGET_S128
void mzd_mul_v_s128_256(mzd_local_t* c, mzd_local_t const* v, mzd_local_t const* A) {
  block_t* cblock       = BLOCK(c, 0);
  const word* vptr      = CONST_BLOCK(v, 0)->w64;
  const block_t* Ablock = CONST_BLOCK(A, 0);

  word128 cval[4] ATTR_ALIGNED(alignof(word128)) = {mm128_zero, mm128_zero, mm128_zero, mm128_zero};
  for (unsigned int w = 4; w; --w, ++vptr) {
    word idx = *vptr;
    for (unsigned int i = sizeof(word) * 8; i; i -= 2, idx >>= 2, Ablock += 2) {
      mm128_xor_mask_region(&cval[0], Ablock[0].w128, mm128_compute_mask(idx, 0), 2);
      mm128_xor_mask_region(&cval[2], Ablock[1].w128, mm128_compute_mask(idx, 1), 2);
    }
  }
  cblock->w128[0] = mm128_xor(cval[0], cval[2]);
  cblock->w128[1] = mm128_xor(cval[1], cval[3]);
}

ATTR_TARGET_S128
void mzd_addmul_v_s128_256(mzd_local_t* c, mzd_local_t const* v, mzd_local_t const* A) {
  block_t* cblock       = BLOCK(c, 0);
  const word* vptr      = CONST_BLOCK(v, 0)->w64;
  const block_t* Ablock = CONST_BLOCK(A, 0);

  word128 cval[4] ATTR_ALIGNED(alignof(word128)) = {cblock->w128[0], cblock->w128[1], mm128_zero,
                                                    mm128_zero};
  for (unsigned int w = 4; w; --w, ++vptr) {
    word idx = *vptr;
    for (unsigned int i = sizeof(word) * 8; i; i -= 2, idx >>= 2, Ablock += 2) {
      mm128_xor_mask_region(&cval[0], Ablock[0].w128, mm128_compute_mask(idx, 0), 2);
      mm128_xor_mask_region(&cval[2], Ablock[1].w128, mm128_compute_mask(idx, 1), 2);
    }
  }
  cblock->w128[0] = mm128_xor(cval[0], cval[2]);
  cblock->w128[1] = mm128_xor(cval[1], cval[3]);
}





static void clear_uint64_block(block_t* block, const unsigned int idx) {
  for (unsigned int i = 0; i < idx; ++i) {
    block->w64[i] = 0;
  }
}


static void mzd_xor_mask_uint64_block(block_t* rblock, const block_t* fblock, const word mask,
                                      const unsigned int idx) {
  for (unsigned int i = 0; i < idx; ++i) {
    rblock->w64[i] ^= fblock->w64[i] & mask;
  }
}

void mzd_addmul_v_uint64_128(mzd_local_t* c, mzd_local_t const* v, mzd_local_t const* A) {
  block_t* cblock       = BLOCK(c, 0);
  const word* vptr      = CONST_BLOCK(v, 0)->w64;
  const block_t* Ablock = CONST_BLOCK(A, 0);

  for (unsigned int w = 2; w; --w, ++vptr) {
    word idx = *vptr;
    for (unsigned int i = sizeof(word) * 8; i; i -= 2, idx >>= 2, Ablock += 1) {
      const uint64_t mask1 = -(idx & 1);
      const uint64_t mask2 = -((idx >> 1) & 1);
      cblock->w64[0] ^= (Ablock->w64[0] & mask1) ^ (Ablock->w64[2] & mask2);
      cblock->w64[1] ^= (Ablock->w64[1] & mask1) ^ (Ablock->w64[3] & mask2);
    }
  }
}

void mzd_mul_v_uint64_128(mzd_local_t* c, mzd_local_t const* v, mzd_local_t const* A) {
  clear_uint64_block(BLOCK(c, 0), 2);
  mzd_addmul_v_uint64_128(c, v, A);
}

void mzd_addmul_v_uint64_129(mzd_local_t* c, mzd_local_t const* v, mzd_local_t const* A) {
  block_t* cblock       = BLOCK(c, 0);
  const word* vptr      = CONST_BLOCK(v, 0)->w64;
  const block_t* Ablock = CONST_BLOCK(A, 0);

  Ablock += 63;
  {
    word idx            = (*vptr) >> 63;
    const uint64_t mask = -(idx & 1);
    mzd_xor_mask_uint64_block(cblock, Ablock, mask, 3);
    Ablock++;
    vptr++;
  }

  for (unsigned int w = 2; w; --w, ++vptr) {
    word idx = *vptr;
    for (unsigned int i = sizeof(word) * 8; i; --i, idx >>= 1, ++Ablock) {
      const uint64_t mask = -(idx & 1);
      mzd_xor_mask_uint64_block(cblock, Ablock, mask, 3);
    }
  }
}

void mzd_mul_v_uint64_129(mzd_local_t* c, mzd_local_t const* v, mzd_local_t const* A) {
  clear_uint64_block(BLOCK(c, 0), 3);
  mzd_addmul_v_uint64_129(c, v, A);
}

void mzd_addmul_v_uint64_192(mzd_local_t* c, mzd_local_t const* v, mzd_local_t const* A) {
  block_t* cblock       = BLOCK(c, 0);
  const word* vptr      = CONST_BLOCK(v, 0)->w64;
  const block_t* Ablock = CONST_BLOCK(A, 0);

  for (unsigned int w = 3; w; --w, ++vptr) {
    word idx = *vptr;
    for (unsigned int i = sizeof(word) * 8; i; --i, idx >>= 1, ++Ablock) {
      const uint64_t mask = -(idx & 1);
      mzd_xor_mask_uint64_block(cblock, Ablock, mask, 3);
    }
  }
}

void mzd_mul_v_uint64_192(mzd_local_t* c, mzd_local_t const* v, mzd_local_t const* A) {
  clear_uint64_block(BLOCK(c, 0), 3);
  mzd_addmul_v_uint64_192(c, v, A);
}

void mzd_addmul_v_uint64_256(mzd_local_t* c, mzd_local_t const* v, mzd_local_t const* A) {
  block_t* cblock       = BLOCK(c, 0);
  const word* vptr      = CONST_BLOCK(v, 0)->w64;
  const block_t* Ablock = CONST_BLOCK(A, 0);

  for (unsigned int w = 4; w; --w, ++vptr) {
    word idx = *vptr;

    for (unsigned int i = sizeof(word) * 8; i; --i, idx >>= 1, ++Ablock) {
      const uint64_t mask = -(idx & 1);
      mzd_xor_mask_uint64_block(cblock, Ablock, mask, 4);
    }
  }
}

void mzd_mul_v_uint64_256(mzd_local_t* c, mzd_local_t const* v, mzd_local_t const* A) {
  clear_uint64_block(BLOCK(c, 0), 4);
  mzd_addmul_v_uint64_256(c, v, A);
}








// no SIMD







