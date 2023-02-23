/*
 * fec.h
 *
 * IP Node Project
 *
 * Based on the Dire Wolf program
 * Copyright (C) 2011-2021 John Langner
 * Copyright (C) 2002 Phil Karn
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>

    struct rs
    {
        unsigned int mm;
        unsigned int nn;
        unsigned char *alpha_to;
        unsigned char *index_of;
        unsigned char *genpoly;
        unsigned int nroots;
        unsigned char fcr;
        unsigned char prim;
        unsigned char iprim;
    };

#define NN (rs->nn)
#define A0 (rs->nn)

#define ALPHA_TO (rs->alpha_to)
#define INDEX_OF (rs->index_of)
#define GENPOLY (rs->genpoly)
#define NROOTS (rs->nroots)
#define FCR (rs->fcr)
#define PRIM (rs->prim)
#define IPRIM (rs->iprim)

    static inline int modnn(struct rs *rs, int x)
    {
        while (x >= rs->nn)
        {
            x -= rs->nn;
            x = (x >> rs->mm) + (x & rs->nn);
        }

        return x;
    }
#define MODNN(x) modnn(rs, x)

    void encode_rs_char(struct rs *, unsigned char *, unsigned char *);
    int decode_rs_char(struct rs *, unsigned char *, int *, int);
    struct rs *init_rs_char(unsigned int, unsigned int, unsigned int, unsigned int, unsigned int);

#define FEC_MAX_CHECK 64
#define FEC_BLOCK_SIZE 255

#ifdef __cplusplus
}
#endif
