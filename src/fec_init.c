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

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "ipnode.h"
#include "fec.h"

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

    rs->alpha_to = (unsigned char *)calloc((rs->nn + 1), sizeof(unsigned char));

    if (rs->alpha_to == NULL)
    {
        free(rs);
        return NULL;
    }

    rs->index_of = (unsigned char *)calloc((rs->nn + 1), sizeof(unsigned char));

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
    rs->genpoly = (unsigned char *)calloc((nroots + 1), sizeof(unsigned char));

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
