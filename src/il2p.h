/*
 * il2p.h
 *
 * IP Node Project
 *
 * Based on the Dire Wolf program
 * Copyright (C) 2011-2021 John Langner
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

#include "fec.h"
#include "audio.h"
#include "ax25_pad.h"

/*
 * The Sync Word is 3-Bytes or 24-bits...
 */
#define IL2P_SYNC_WORD_SIZE 3

/*
 * ...and contains the following value:
 */
#define IL2P_SYNC_WORD 0xF15E48

#define IL2P_HEADER_SIZE 13
#define IL2P_HEADER_PARITY 2

#define IL2P_MAX_PAYLOAD_SIZE 1023
#define IL2P_MAX_PAYLOAD_BLOCKS 5
#define IL2P_MAX_PARITY_SYMBOLS 16
#define IL2P_MAX_ENCODED_PAYLOAD_SIZE (IL2P_MAX_PAYLOAD_SIZE + IL2P_MAX_PAYLOAD_BLOCKS * IL2P_MAX_PARITY_SYMBOLS)

#define IL2P_MAX_PACKET_SIZE (IL2P_SYNC_WORD_SIZE + IL2P_HEADER_SIZE + IL2P_HEADER_PARITY + IL2P_MAX_ENCODED_PAYLOAD_SIZE)

    enum il2p_s
    {
        IL2P_SEARCHING = 0,
        IL2P_HEADER,
        IL2P_PAYLOAD,
        IL2P_DECODE
    };

    struct il2p_context_s
    {
        enum il2p_s state;
        unsigned int acc;
        int bc;
        int hc;
        int eplen;
        int pc;
        unsigned char shdr[IL2P_HEADER_SIZE + IL2P_HEADER_PARITY];
        unsigned char uhdr[IL2P_HEADER_SIZE];
        unsigned char spayload[IL2P_MAX_ENCODED_PAYLOAD_SIZE];
    };

    typedef struct
    {
        int payload_byte_count;
        int payload_block_count;
        int small_block_size;
        int large_block_size;
        int large_block_count;
        int small_block_count;
        int parity_symbols_per_block;
    } il2p_payload_properties_t;

    void il2p_init(void);
    struct rs *il2p_find_rs(int nparity);
    void il2p_encode_rs(unsigned char *tx_data, int data_size, int num_parity, unsigned char *parity_out);
    int il2p_decode_rs(unsigned char *rec_block, int data_size, int num_parity, unsigned char *out);
    void il2p_rec_bit(int dbit);
    int il2p_send_frame(packet_t pp);
    void il2p_send_idle(int nbits);
    int il2p_encode_frame(packet_t pp, unsigned char *iout);
    packet_t il2p_decode_frame(unsigned char *irec);
    packet_t il2p_decode_header_payload(unsigned char *uhdr, unsigned char *epayload, int *symbols_corrected);
    int il2p_type_1_header(packet_t pp, unsigned char *hdr);
    packet_t il2p_decode_header_type_1(unsigned char *hdr, int num_sym_changed);
    int il2p_clarify_header(unsigned char *irec, unsigned char *uhdr);
    void il2p_scramble_block(unsigned char *in, unsigned char *out, int len);
    void il2p_descramble_block(unsigned char *in, unsigned char *out, int len);
    int il2p_payload_compute(il2p_payload_properties_t *p, int payload_size);
    int il2p_encode_payload(unsigned char *payload, int payload_size, unsigned char *enc);
    int il2p_decode_payload(unsigned char *received, int payload_size, unsigned char *payload_out, int *symbols_corrected);
    int il2p_get_header_attributes(unsigned char *hdr);

#ifdef __cplusplus
}
#endif
