/*
 * il2p_send.c
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
#include "modulate.h"
#include "il2p.h"
#include "audio.h"

static int number_of_bits_sent;

/*
 * BPSK 128 symbol Pseudo Noise (PN) sequence
 */
#if 0
unsigned char pn_preamble[] = {
    0, 1, 1, 0, 0, 1, 1, 1,
    0, 1, 0, 0, 1, 1, 0, 0,
    1, 1, 0, 1, 0, 0, 1, 0,
    1, 0, 1, 0, 1, 0, 1, 1,
    1, 0, 1, 1, 1, 1, 0, 0,
    1, 0, 0, 1, 1, 0, 1, 0,
    1, 1, 0, 1, 0, 0, 1, 0,
    0, 0, 0, 1, 1, 0, 1, 0,
    1, 1, 1, 0, 0, 1, 1, 0,
    1, 1, 0, 0, 1, 1, 0, 1,
    1, 0, 1, 1, 0, 0, 0, 1,
    0, 1, 0, 1, 0, 0, 0, 1,
    0, 0, 1, 0, 1, 1, 0, 0,
    0, 0, 0, 1, 1, 1, 0, 1,
    1, 0, 1, 1, 0, 0, 1, 1,
    0, 1, 0, 1, 0, 0, 0, 1
};

/*
 * Send BPSK PN to modulator
 */
static void il2p_send_pn_preamble() {
    for (int i = 0; i < sizeof(pn_preamble); i++) {
        if (pn_preamble[i] == 0) {
            put_bit(0);
            put_bit(0);
        } else {
            put_bit(1);
            put_bit(1);
        }

        number_of_bits_sent += 2;
    }
}
#endif

/*
 * Send data to modulator
 */
static void il2p_send_data(unsigned char *b, int count)
{
    for (int j = 0; j < count; j++)
    {
        unsigned char x = b[j];

        for (int k = 0; k < 8; k++)
        {
            put_bit((x & 0x80) != 0);
            x <<= 1;
        }

        number_of_bits_sent += 8;
    }
}

int il2p_send_frame(packet_t pp)
{
    unsigned char encoded[IL2P_MAX_PACKET_SIZE];

    encoded[0] = (IL2P_SYNC_WORD >> 16) & 0xff;
    encoded[1] = (IL2P_SYNC_WORD >> 8) & 0xff;
    encoded[2] = (IL2P_SYNC_WORD)&0xff;

    int elen = il2p_encode_frame(pp, encoded + IL2P_SYNC_WORD_SIZE);

    if (elen == -1)
    {
        fprintf(stderr, "Fatal: IL2P: Unable to encode frame into IL2P\n");
        return -1;
    }

    elen += IL2P_SYNC_WORD_SIZE;

    number_of_bits_sent = 0; // incremented in send_bit

    // Send bits to modulator.

    // il2p_send_pn_preamble(); // experimental
    il2p_send_data(encoded, elen);

    return number_of_bits_sent;
}

/*
 * Send txdelay and txtail to modulator
 */
void il2p_send_idle(int nbits)
{
    for (int i = 0; i < nbits; i++)
    {
        put_bit(0);
    }

    number_of_bits_sent += nbits;
}
