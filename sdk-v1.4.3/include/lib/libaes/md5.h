/* md5.h
 *
 * Copyright (C) 2006-2015 wolfSSL Inc.
 *
 * This file is part of wolfSSL. (formerly known as CyaSSL)
 *
 * wolfSSL is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * wolfSSL is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */

#ifndef WOLF_CRYPT_MD5_H
#define WOLF_CRYPT_MD5_H

// #include <wolfssl/wolfcrypt/types.h>

#ifndef NO_MD5

#ifdef HAVE_FIPS
    #define wc_InitMd5   InitMd5
    #define wc_Md5Update Md5Update
    #define wc_Md5Final  Md5Final
    #define wc_Md5Hash   Md5Hash
#endif

#ifdef __cplusplus
    extern "C" {
#endif

/* in bytes */
enum {
#ifdef STM32F2_HASH
    MD5_REG_SIZE    =  4,      /* STM32 register size, bytes */
#endif
    MD5             =  0,      /* hash type unique */
    MD5_BLOCK_SIZE  = 64,
    MD5_DIGEST_SIZE = 16,
    MD5_PAD_SIZE    = 56
};

#if defined(WOLFSSL_PIC32MZ_HASH)
#include "port/pic32/pic32mz-crypt.h"
#endif

#ifndef WOLFSSL_TI_HASH

/* MD5 digest */
typedef struct Md5 {
    uint32_t  buffLen;   /* in bytes          */
    uint32_t  loLen;     /* length in bytes   */
    uint32_t  hiLen;     /* length in bytes   */
    uint32_t  buffer[MD5_BLOCK_SIZE  / sizeof(uint32_t)];
    #if !defined(WOLFSSL_PIC32MZ_HASH)
    uint32_t  digest[MD5_DIGEST_SIZE / sizeof(uint32_t)];
    #else
    uint32_t  digest[PIC32_HASH_SIZE / sizeof(uint32_t)];
    pic32mz_desc desc ; /* Crypt Engine descripter */
    #endif
} Md5;

#else /* WOLFSSL_TI_HASH */
    #include "wolfssl/wolfcrypt/port/ti/ti-hash.h"
#endif

void wc_InitMd5(Md5*);
void wc_Md5Update(Md5*, const uint8_t*, uint32_t);
void wc_Md5Final(Md5*, uint8_t*);
int  wc_Md5Hash(const uint8_t*, uint32_t, uint8_t*);

#ifdef __cplusplus
    } /* extern "C" */
#endif

#endif /* NO_MD5 */
#endif /* WOLF_CRYPT_MD5_H */
