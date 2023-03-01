/*
 * il2p_rec.c
 *
 * IP Node Project
 *
 * Based on the Dire Wolf program
 * Copyright (C) 2011-2021 John Langner
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ipnode.h"
#include "il2p.h"
#include "demod.h"
#include "dlq.h"

static struct il2p_context_s il2p_context;

/*
 * Called from demod
 */
void il2p_rec_bit(int dbit)
{
    struct il2p_context_s *F = &il2p_context;
    packet_t pp;
    int corrected; // not used

    memset(F, 0, sizeof(struct il2p_context_s));

    // Accumulate most recent 24 bits received.  Most recent is LSB.

    F->acc = ((F->acc << 1) | (dbit & 1)) & 0x00ffffff;

    // State machine to look for sync word then gather appropriate number of header and payload bytes.

    switch (F->state)
    {
    case IL2P_SEARCHING: // Searching for the sync word.

        if (__builtin_popcount(F->acc ^ IL2P_SYNC_WORD) <= 1) // allow single bit mismatch
        {
            F->state = IL2P_HEADER;
            F->bc = 0;
            F->hc = 0;
        }
        break;

    case IL2P_HEADER: // Gathering the header.

        F->bc++;

        if (F->bc == 8) // full byte has been collected.
        {
            F->bc = 0;

            F->shdr[F->hc++] = F->acc & 0xff;

            if (F->hc == IL2P_HEADER_SIZE + IL2P_HEADER_PARITY) // Have all of header
            {

                // Fix any errors and descramble.
                int corrected = il2p_clarify_header(F->shdr, F->uhdr);

                if (corrected >= 0) // Good header.
                {
                    // How much payload is expected?
                    il2p_payload_properties_t plprop;

                    int len = il2p_get_header_attributes(F->uhdr);

                    F->eplen = il2p_payload_compute(&plprop, len);

                    if (F->eplen >= 1) // Need to gather payload.
                    {
                        F->pc = 0;
                        F->state = IL2P_PAYLOAD;
                    }
                    else if (F->eplen == 0) // No payload.
                    {
                        F->pc = 0;
                        F->state = IL2P_DECODE;
                    }
                    else // Error.
                    {
                        F->state = IL2P_SEARCHING;
                    }
                }
                else // corrected == -1
                {
                    F->state = IL2P_SEARCHING; // Header failed FEC check.
                }
            }
        }
        break;

    case IL2P_PAYLOAD: // Gathering the payload, if any.

        F->bc++;

        if (F->bc == 8) // full byte has been collected.
        {
            F->bc = 0;

            F->spayload[F->pc++] = F->acc & 0xff;

            if (F->pc == F->eplen)
            {
                F->state = IL2P_DECODE;
            }
        }
        break;

    case IL2P_DECODE:

        pp = il2p_decode_header_payload(F->uhdr, F->spayload, &corrected);

        if (pp != NULL)
        {
            dlq_rec_frame(pp);
        }

        F->state = IL2P_SEARCHING;
        break;
    }
}
