/*
 * fec_init.c
 *
 * IP Node Project
 *
 * Based on the Dire Wolf program
 * Copyright (C) 2011-2021 John Langner
 * Copyright (C) 2002 Phil Karn
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>   // uint64_t
#include <inttypes.h> // PRIx64

#include "ipnode.h"
#include "fec.h"

static struct
{
    int symsize;   // Symbol size, bits (1-8).  Always 8 for this application.
    int genpoly;   // Field generator polynomial coefficients.
    int fcs;       // First root of RS code generator polynomial, index form.
    int prim;      // Primitive element to generate polynomial roots.
    int nroots;    // RS code generator polynomial degree (number of roots).
                   // Same as number of check bytes added.
    struct rs *rs; // Pointer to RS codec control block.  Filled in at init time.
} Tab[3] = {
    {8, 0x11d, 1, 1, 16, NULL}, // RS(255,239)
    {8, 0x11d, 1, 1, 32, NULL}, // RS(255,223)
    {8, 0x11d, 1, 1, 64, NULL}, // RS(255,191)
};

struct correlation_tag_s
{
    uint64_t value;    // 64 bit value, send LSB first.
    int n_block_radio; // Size of transmitted block, all in bytes.
    int k_data_radio;  // Size of transmitted data part.
    int n_block_rs;    // Size of RS algorithm block.
    int k_data_rs;     // Size of RS algorithm data part.
    int itab;          // Index into Tab array.
} tags[] = {
    /* Tag_00 */ {0x566ED2717946107ELL, 0, 0, 0, 0, -1}, // Reserved

    /* Tag_01 */ {0xB74DB7DF8A532F3ELL, 255, 239, 255, 239, 0}, //  RS(255, 239) 16-byte check value, 239 information bytes
    /* Tag_02 */ {0x26FF60A600CC8FDELL, 144, 128, 255, 239, 0}, //  RS(144,128) - shortened RS(255, 239), 128 info bytes
    /* Tag_03 */ {0xC7DC0508F3D9B09ELL, 80, 64, 255, 239, 0},   //  RS(80,64) - shortened RS(255, 239), 64 info bytes
    /* Tag_04 */ {0x8F056EB4369660EELL, 48, 32, 255, 239, 0},   //  RS(48,32) - shortened RS(255, 239), 32 info bytes

    /* Tag_05 */ {0x6E260B1AC5835FAELL, 255, 223, 255, 223, 1}, //  RS(255, 223) 32-byte check value, 223 information bytes
    /* Tag_06 */ {0xFF94DC634F1CFF4ELL, 160, 128, 255, 223, 1}, //  RS(160,128) - shortened RS(255, 223), 128 info bytes
    /* Tag_07 */ {0x1EB7B9CDBC09C00ELL, 96, 64, 255, 223, 1},   //  RS(96,64) - shortened RS(255, 223), 64 info bytes
    /* Tag_08 */ {0xDBF869BD2DBB1776LL, 64, 32, 255, 223, 1},   //  RS(64,32) - shortened RS(255, 223), 32 info bytes

    /* Tag_09 */ {0x3ADB0C13DEAE2836LL, 255, 191, 255, 191, 2}, //  RS(255, 191) 64-byte check value, 191 information bytes
    /* Tag_0A */ {0xAB69DB6A543188D6LL, 192, 128, 255, 191, 2}, //  RS(192, 128) - shortened RS(255, 191), 128 info bytes
    /* Tag_0B */ {0x4A4ABEC4A724B796LL, 128, 64, 255, 191, 2},  //  RS(128, 64) - shortened RS(255, 191), 64 info bytes

    /* Tag_0C */ {0x0293D578626B67E6LL, 0, 0, 0, 0, -1}, // Undefined
    /* Tag_0D */ {0xE3B0B0D6917E58A6LL, 0, 0, 0, 0, -1}, // Undefined
    /* Tag_0E */ {0x720267AF1BE1F846LL, 0, 0, 0, 0, -1}, // Undefined
    /* Tag_0F */ {0x93210201E8F4C706LL, 0, 0, 0, 0, -1}, // Undefined
    /* Tag_40 */ {0x41C246CB5DE62A7ELL, 0, 0, 0, 0, -1}  // Reserved
};

#define CLOSE_ENOUGH 8

int fec_tag_find_match(uint64_t t)
{
    for (int c = CTAG_MIN; c <= CTAG_MAX; c++)
    {
        if (__builtin_popcountll(t ^ tags[c].value) <= CLOSE_ENOUGH)
        {
            return c;
        }
    }
    return -1;
}

// Get properties of specified CTAG number.

struct rs *fec_get_rs(int ctag_num)
{
    return Tab[tags[ctag_num].itab].rs;
}

uint64_t fec_get_ctag_value(int ctag_num)
{
    return tags[ctag_num].value;
}

int fec_get_k_data_radio(int ctag_num)
{
    return tags[ctag_num].k_data_radio;
}

int fec_get_k_data_rs(int ctag_num)
{
    return tags[ctag_num].k_data_rs;
}

int fec_get_nroots(int ctag_num)
{
    return Tab[tags[ctag_num].itab].nroots;
}

struct rs *init_rs_char(unsigned int symsize, unsigned int gfpoly, unsigned fcr, unsigned prim, unsigned int nroots)
{
    if (symsize > (8 * sizeof(unsigned char)))
        return NULL; /* Need version with ints rather than chars */

    if (fcr >= (1 << symsize))
        return NULL;

    if (prim == 0 || prim >= (1 << symsize))
        return NULL;

    if (nroots >= (1 << symsize))
        return NULL; /* Can't have more roots than symbol values! */

    struct rs *rs = (struct rs *)calloc(1, sizeof(struct rs));
    rs->mm = symsize;
    rs->nn = (1 << symsize) - 1;
    rs->alpha_to = (unsigned char *)malloc(sizeof(unsigned char) * (rs->nn + 1));

    if (rs->alpha_to == NULL)
    {
        free(rs);
        return NULL;
    }

    rs->index_of = (unsigned char *)malloc(sizeof(unsigned char) * (rs->nn + 1));

    if (rs->index_of == NULL)
    {
        free(rs->alpha_to);
        free(rs);
        return NULL;
    }

    /* Generate Galois field lookup tables */
    rs->index_of[0] = A0; /* log(zero) = -inf */
    rs->alpha_to[A0] = 0; /* alpha**-inf = 0 */

    int sr = 1;

    for (int i = 0; i < rs->nn; i++)
    {
        rs->index_of[sr] = i;
        rs->alpha_to[i] = sr;
        sr <<= 1;

        if (sr & (1 << symsize))
            sr ^= gfpoly;

        sr &= rs->nn;
    }

    if (sr != 1)
    {
        /* field generator polynomial is not primitive! */
        free(rs->alpha_to);
        free(rs->index_of);
        free(rs);
        return NULL;
    }

    /* Form RS code generator polynomial from its roots */
    rs->genpoly = (unsigned char *)malloc(sizeof(unsigned char) * (nroots + 1));

    if (rs->genpoly == NULL)
    {
        free(rs->alpha_to);
        free(rs->index_of);
        free(rs);
        return NULL;
    }

    rs->fcr = fcr;
    rs->prim = prim;
    rs->nroots = nroots;

    /* Find prim-th root of 1, used in decoding */

    int iprim;

    for (iprim = 1; (iprim % prim) != 0; iprim += rs->nn)
        ;

    rs->iprim = iprim / prim;

    rs->genpoly[0] = 1;

    for (int i = 0, root = fcr * prim; i < nroots; i++, root += prim)
    {
        rs->genpoly[i + 1] = 1;

        /* Multiply rs->genpoly[] by  @**(root + x) */
        for (int j = i; j > 0; j--)
        {
            if (rs->genpoly[j] != 0)
                rs->genpoly[j] = rs->genpoly[j - 1] ^ rs->alpha_to[modnn(rs, rs->index_of[rs->genpoly[j]] + root)];
            else
                rs->genpoly[j] = rs->genpoly[j - 1];
        }

        /* rs->genpoly[0] can never be zero */
        rs->genpoly[0] = rs->alpha_to[modnn(rs, rs->index_of[rs->genpoly[0]] + root)];
    }

    /* convert rs->genpoly[] to index form for quicker encoding */

    for (int i = 0; i <= nroots; i++)
    {
        rs->genpoly[i] = rs->index_of[rs->genpoly[i]];
    }

    return rs;
}
