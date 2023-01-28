/*
 * fec_encode.c
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

#include "fec.h"

void encode_rs_char(struct rs *rs, unsigned char *data, unsigned char *bb)
{

    memset(bb, 0, NROOTS * sizeof(unsigned char)); // clear out the FEC data area

    for (int i = 0; i < NN - NROOTS; i++)
    {
        unsigned char feedback = INDEX_OF[data[i] ^ bb[0]];

        if (feedback != A0)
        { /* feedback term is non-zero */
            for (int j = 1; j < NROOTS; j++)
                bb[j] ^= ALPHA_TO[MODNN(feedback + GENPOLY[NROOTS - j])];
        }

        /* Shift */
        memmove(&bb[0], &bb[1], sizeof(unsigned char) * (NROOTS - 1));

        if (feedback != A0)
            bb[NROOTS - 1] = ALPHA_TO[MODNN(feedback + GENPOLY[0])];
        else
            bb[NROOTS - 1] = 0;
    }
}
