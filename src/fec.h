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

#define MM (rs->mm)
#define NN (rs->nn)

#define ALPHA_TO (rs->alpha_to)
#define INDEX_OF (rs->index_of)
#define GENPOLY (rs->genpoly)
#define NROOTS (rs->nroots)
#define FCR (rs->fcr)
#define PRIM (rs->prim)
#define IPRIM (rs->iprim)
#define A0 (NN)

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

    void encode_rs_char(struct rs *rs, unsigned char *data, unsigned char *bb);
    int decode_rs_char(struct rs *rs, unsigned char *data, int *eras_pos, int no_eras);

    struct rs *init_rs_char(unsigned int symsize, unsigned int gfpoly, unsigned int fcr, unsigned int prim, unsigned int nroots);

    void fec_rec_bit(int dbit);

    struct rs *fec_get_rs(int ctag_num);
    uint64_t fec_get_ctag_value(int ctag_num);
    int fec_get_k_data_radio(int ctag_num);
    int fec_get_k_data_rs(int ctag_num);
    int fec_get_nroots(int ctag_num);
    int fec_tag_find_match(uint64_t t);

#define CTAG_MIN 0x01
#define CTAG_MAX 0x0B

#define FEC_MAX_DATA 239
#define FEC_MAX_CHECK 64
#define FEC_BLOCK_SIZE 255

#ifdef __cplusplus
}
#endif
