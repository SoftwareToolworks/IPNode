/*
 * il2p_payload.c
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

int il2p_payload_compute(il2p_payload_properties_t *p, int payload_size)
{
    memset(p, 0, sizeof(il2p_payload_properties_t));

    if (payload_size < 0 || payload_size > IL2P_MAX_PAYLOAD_SIZE)
    {
        return -1;
    }
    else if (payload_size == 0)
    {
        return 0;
    }

    p->payload_byte_count = payload_size;
    p->payload_block_count = (p->payload_byte_count + 238) / 239;
    p->small_block_size = p->payload_byte_count / p->payload_block_count;
    p->large_block_size = p->small_block_size + 1;
    p->large_block_count = p->payload_byte_count - (p->payload_block_count * p->small_block_size);
    p->small_block_count = p->payload_block_count - p->large_block_count;
    p->parity_symbols_per_block = 16;

    // Return the total size for the encoded format.

    return (p->small_block_count * (p->small_block_size + p->parity_symbols_per_block) +
            p->large_block_count * (p->large_block_size + p->parity_symbols_per_block));
}

int il2p_encode_payload(unsigned char *payload, int payload_size, unsigned char *enc)
{
    if (payload_size > IL2P_MAX_PAYLOAD_SIZE)
    {
        return -1;
    }
    else if (payload_size == 0)
    {
        return 0;
    }

    // Determine number of blocks and sizes.

    il2p_payload_properties_t ipp;

    int e = il2p_payload_compute(&ipp, payload_size);

    if (e <= 0)
    {
        return e;
    }

    unsigned char *pin = payload;
    unsigned char *pout = enc;

    int encoded_length = 0;
    
    unsigned char scram[256];
    unsigned char parity[IL2P_MAX_PARITY_SYMBOLS];

    // First the large blocks.

    for (int b = 0; b < ipp.large_block_count; b++)
    {
        il2p_scramble_block(pin, scram, ipp.large_block_size);
        memcpy(pout, scram, ipp.large_block_size);

        pin += ipp.large_block_size;
        pout += ipp.large_block_size;

        encoded_length += ipp.large_block_size;
        il2p_encode_rs(scram, ipp.large_block_size, ipp.parity_symbols_per_block, parity);

        memcpy(pout, parity, ipp.parity_symbols_per_block);

        pout += ipp.parity_symbols_per_block;
        encoded_length += ipp.parity_symbols_per_block;
    }

    // Then the small blocks.

    for (int b = 0; b < ipp.small_block_count; b++)
    {
        il2p_scramble_block(pin, scram, ipp.small_block_size);
        memcpy(pout, scram, ipp.small_block_size);

        pin += ipp.small_block_size;
        pout += ipp.small_block_size;

        encoded_length += ipp.small_block_size;
        il2p_encode_rs(scram, ipp.small_block_size, ipp.parity_symbols_per_block, parity);

        memcpy(pout, parity, ipp.parity_symbols_per_block);

        pout += ipp.parity_symbols_per_block;
        encoded_length += ipp.parity_symbols_per_block;
    }

    return encoded_length;
}

int il2p_decode_payload(unsigned char *received, int payload_size, unsigned char *payload_out, int *symbols_corrected)
{
    // Determine number of blocks and sizes.

    il2p_payload_properties_t ipp;

    int e = il2p_payload_compute(&ipp, payload_size);

    if (e <= 0)
    {
        return e;
    }

    unsigned char *pin = received;
    unsigned char *pout = payload_out;

    int decoded_length = 0;
    int failed = 0;

    unsigned char corrected_block[255];

    // First the large blocks.

    for (int b = 0; b < ipp.large_block_count; b++)
    {
        memset(corrected_block, 0, 255);

        int e = il2p_decode_rs(pin, ipp.large_block_size, ipp.parity_symbols_per_block, corrected_block);

        if (e < 0)
            failed = 1;

        *symbols_corrected += e;

        il2p_descramble_block(corrected_block, pout, ipp.large_block_size);

        pin += ipp.large_block_size + ipp.parity_symbols_per_block;
        pout += ipp.large_block_size;

        decoded_length += ipp.large_block_size;
    }

    // Then the small blocks.

    for (int b = 0; b < ipp.small_block_count; b++)
    {
        memset(corrected_block, 0, 255);

        int e = il2p_decode_rs(pin, ipp.small_block_size, ipp.parity_symbols_per_block, corrected_block);

        if (e < 0)
            failed = 1;

        *symbols_corrected += e;

        il2p_descramble_block(corrected_block, pout, ipp.small_block_size);

        pin += ipp.small_block_size + ipp.parity_symbols_per_block;
        pout += ipp.small_block_size;
    
        decoded_length += ipp.small_block_size;
    }

    if (failed)
    {
        return -2;
    }

    if (decoded_length != payload_size)
    {
        printf("IL2P Internal error: decoded_length = %d, payload_size = %d\n", decoded_length, payload_size);
        return -3;
    }

    return decoded_length;
}
