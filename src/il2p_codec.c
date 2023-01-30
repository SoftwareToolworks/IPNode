/*
 * il2p_coder.c
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

int il2p_encode_frame(packet_t pp, unsigned char *iout)
{
    unsigned char hdr[IL2P_HEADER_SIZE + IL2P_HEADER_PARITY];

    int e = il2p_type_1_header(pp, hdr);

    if (e < 0)
        return -1;

    il2p_scramble_block(hdr, iout, IL2P_HEADER_SIZE);
    il2p_encode_rs(iout, IL2P_HEADER_SIZE, IL2P_HEADER_PARITY, iout + IL2P_HEADER_SIZE);

    int out_len = IL2P_HEADER_SIZE + IL2P_HEADER_PARITY;

    if (e == 0)  // Success. No info part.
    {
        return out_len;
    }

    // Payload is AX.25 info part.
    unsigned char *pinfo;

    int info_len = ax25_get_info(pp, &pinfo);

    int k = il2p_encode_payload(pinfo, info_len, iout + out_len);

    if (k > 0)  // Success. Info part was <= 1023 bytes.
    {
        out_len += k;

        return out_len;
    }

    // Something went wrong with the payload encoding.
    return -1;
}

packet_t il2p_decode_frame(unsigned char *irec)
{
    unsigned char uhdr[IL2P_HEADER_SIZE];
    int e = il2p_clarify_header(irec, uhdr);

    return il2p_decode_header_payload(uhdr, irec + IL2P_HEADER_SIZE + IL2P_HEADER_PARITY, &e);
}

packet_t il2p_decode_header_payload(unsigned char *uhdr, unsigned char *epayload, int *symbols_corrected)
{
    int payload_len = il2p_get_header_attributes(uhdr);

    packet_t pp = il2p_decode_header_type_1(uhdr, *symbols_corrected);

    if (pp == NULL) // Failed for some reason.
    {
        return NULL;
    }

    if (payload_len > 0)
    {
        // This is the AX.25 Information part.

        unsigned char extracted[IL2P_MAX_PAYLOAD_SIZE];
        int e = il2p_decode_payload(epayload, payload_len, extracted, symbols_corrected);

        // It would be possible to have a good header but too many errors in the payload.

        if (e <= 0)
        {
            ax25_delete(pp);
            pp = NULL;
            return pp;
        }

        if (e != payload_len)
        {
            fprintf(stderr, "IL2P Internal Error: %s(): payload_len=%d, e=%d.\n", __func__, payload_len, e);
        }

        ax25_set_info(pp, extracted, payload_len);
    }

    return pp;
}
