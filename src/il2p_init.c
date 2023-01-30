/*
 * il2p_init.c
 *
 * IP Node Project
 *
 * Based on the Dire Wolf program
 * Copyright (C) 2011-2021 John Langner
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "ipnode.h"
#include "fec.h" // For Reed Solomon stuff.
#include "il2p.h"

#define MAX_NROOTS 16

#define NTAB 5

static struct
{
    int symsize; // Symbol size, bits (1-8).  Always 8 for this application.
    int genpoly; // Field generator polynomial coefficients.
    int fcs;     // First root of RS code generator polynomial, index form, IL2P uses 0.
    int prim;    // Primitive element to generate polynomial roots.
    int nroots;  // RS code generator polynomial degree (number of roots).
    // Same as number of check bytes added.
    struct rs *rs; // Pointer to RS codec control block.  Filled in at init time.
} Tab[NTAB] = {
    {8, 0x11d, 0, 1, 2, NULL},  // 2 parity
    {8, 0x11d, 0, 1, 4, NULL},  // 4 parity
    {8, 0x11d, 0, 1, 6, NULL},  // 6 parity
    {8, 0x11d, 0, 1, 8, NULL},  // 8 parity
    {8, 0x11d, 0, 1, 16, NULL}, // 16 parity
};

void il2p_init()
{
    for (int i = 0; i < NTAB; i++)
    {

        Tab[i].rs = init_rs_char(Tab[i].symsize, Tab[i].genpoly, Tab[i].fcs, Tab[i].prim, Tab[i].nroots);

        if (Tab[i].rs == NULL)
        {
            exit(EXIT_FAILURE);
        }
    }
}

// Find RS codec control block for specified number of parity symbols.

struct rs *il2p_find_rs(int nparity)
{
    for (int n = 0; n < NTAB; n++)
    {
        if (Tab[n].nroots == nparity)
        {
            return Tab[n].rs;
        }
    }

    return Tab[0].rs;
}

void il2p_encode_rs(unsigned char *tx_data, int data_size, int num_parity, unsigned char *parity_out)
{
    unsigned char rs_block[FEC_BLOCK_SIZE];
    memset(rs_block, 0, sizeof(rs_block));
    memcpy(rs_block + sizeof(rs_block) - data_size - num_parity, tx_data, data_size);
    encode_rs_char(il2p_find_rs(num_parity), rs_block, parity_out);
}

int il2p_decode_rs(unsigned char *rec_block, int data_size, int num_parity, unsigned char *out)
{
    //  Use zero padding in front if data size is too small.

    int n = data_size + num_parity; // total size in.

    unsigned char rs_block[FEC_BLOCK_SIZE];

    // We could probably do this more efficiently by skipping the
    // processing of the bytes known to be zero.  Good enough for now.

    memset(rs_block, 0, sizeof(rs_block) - n);
    memcpy(rs_block + sizeof(rs_block) - n, rec_block, n);

    int derrlocs[FEC_MAX_CHECK]; // Half would probably be OK.

    int derrors = decode_rs_char(il2p_find_rs(num_parity), rs_block, derrlocs, 0);
    memcpy(out, rs_block + sizeof(rs_block) - n, data_size);

    // It is possible to have a situation where too many errors are
    // present but the algorithm could get a good code block by "fixing"
    // one of the padding bytes that should be 0.

    for (int i = 0; i < derrors; i++)
    {
        if (derrlocs[i] < sizeof(rs_block) - n)
        {
            derrors = -1;
            break;
        }
    }

    return derrors;
}
