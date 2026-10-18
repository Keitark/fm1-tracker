/* SPDX-License-Identifier: Apache-2.0 */
#ifndef FM1_SHA256_H
#define FM1_SHA256_H
#include <stddef.h>
#include <stdint.h>
typedef struct {
    uint32_t state[8];
    uint64_t bytes;
    uint8_t block[64];
    unsigned used;
} fm1_sha256;
void fm1_sha256_init(fm1_sha256 *);
void fm1_sha256_update(fm1_sha256 *,const void *,size_t);
void fm1_sha256_final(fm1_sha256 *,uint8_t [32]);
#endif
