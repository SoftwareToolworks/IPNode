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
#include <complex.h>
#include <stdbool.h>
#include <string.h>

#include "ipnode.h"
#include "tx.h"
#include "il2p.h"
#include "audio.h"
#include "rrc_fir.h"
#include "constellation.h"

/*
 * Transmit bits are stored in tx_bits array
 */
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

    unsigned char tx_bits[elen * 8];

    for (int i = 0; i < (elen * 8); i++)
    {
        tx_bits[i] = 0U;
    }

    int number_of_bits = 0;

    /*
     * Send byte to modulator MSB first
     */
    for (int j = 0; j < elen; j++)
    {
        unsigned char x = encoded[j];

        for (int k = 0; k < 8; k++)
        {
            tx_bits[(j * 8) + k] = (x & 0x80) != 0;
            x <<= 1;
        }

        number_of_bits += 8;
    }

    tx_frame_bits(Mode_QPSK, tx_bits, number_of_bits);

    return number_of_bits;
}

/*
 * Send txdelay and txtail flag bits to modulator
 */
void il2p_send_idle(int num_flags)
{
    int number_of_bits = 0;

    unsigned char tx_bits[num_flags * 8];  // one flag is 8-Bits

    /*
     * Send byte to modulator MSB first
     */
    for (int i = 0; i < num_flags; i++)
    {
        unsigned char x = FLAG;

        for (int j = 0; j < 8; j++)
        {
            tx_bits[(i * 8) + j] = (x & 0x80) != 0;
            x <<= 1;
        }

        number_of_bits += 8;
    }

    tx_frame_bits(Mode_BPSK, tx_bits, number_of_bits);
}

