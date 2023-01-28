/*
 * il2p_scramble.c
 *
 * IP Node Project
 *
 * Based on the Dire Wolf program
 * Copyright (C) 2011-2021 John Langner
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "ipnode.h"
#include "il2p.h"

// Scramble bits for il2p transmit.

// Note that there is a delay of 5 until the first bit comes out.
// So we need to need to ignore the first 5 out and stick in
// an extra 5 filler bits to flush at the end.

#define INIT_TX_LSFR 0x00f

static inline int scramble_bit(int in, int *state)
{
    int out = ((*state >> 4) ^ *state) & 1;
    *state = ((((in ^ *state) & 1) << 9) | (*state ^ ((*state & 1) << 4))) >> 1;

    return out;
}

// Undo data scrambling for il2p receive.

#define INIT_RX_LSFR 0x1f0

static inline int descramble_bit(int in, int *state)
{
    int out = (in ^ *state) & 1;
    *state = ((*state >> 1) | ((in & 1) << 8)) ^ ((in & 1) << 3);

    return out;
}

/*--------------------------------------------------------------------------------
 *
 * Function:	il2p_scramble_block
 *
 * Purpose:	Scramble a block before adding RS parity.
 *
 * Inputs:	in		Array of bytes.
 *		len		Number of bytes both in and out.
 *
 * Outputs:	out		Array of bytes.
 *
 *--------------------------------------------------------------------------------*/

void il2p_scramble_block(unsigned char *in, unsigned char *out, int len)
{
    int tx_lfsr_state = INIT_TX_LSFR;

    memset(out, 0, len);

    int skipping = 1; // Discard the first 5 out.
    int ob = 0;       // Index to output byte.
    int om = 0x80;    // Output bit mask;

    for (int ib = 0; ib < len; ib++)
    {
        for (int im = 0x80; im != 0; im >>= 1)
        {
            int s = scramble_bit((in[ib] & im) != 0, &tx_lfsr_state);

            if (ib == 0 && im == 0x04)
                skipping = 0;

            if (!skipping)
            {
                if (s)
                {
                    out[ob] |= om;
                }

                om >>= 1;

                if (om == 0)
                {
                    om = 0x80;
                    ob++;
                }
            }
        }
    }

    int x = tx_lfsr_state;

    for (int n = 0; n < 5; n++)
    {
        int s = scramble_bit(0, &x);

        if (s)
        {
            out[ob] |= om;
        }

        om >>= 1;

        if (om == 0)
        {
            om = 0x80;
            ob++;
        }
    }
}

void il2p_descramble_block(unsigned char *in, unsigned char *out, int len)
{
    int rx_lfsr_state = INIT_RX_LSFR;

    memset(out, 0, len);

    for (int b = 0; b < len; b++)
    {
        for (int m = 0x80; m != 0; m >>= 1)
        {
            int d = descramble_bit((in[b] & m) != 0, &rx_lfsr_state);

            if (d)
            {
                out[b] |= m;
            }
        }
    }
}
