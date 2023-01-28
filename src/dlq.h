/*
 * dlq.h
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

#include "ax25_pad.h"
#include "audio.h"

#define TXDATA_MAGIC 0x09110911

    typedef struct cdata_s
    {
        struct cdata_s *next;
        int magic;
        int pid;
        int size;
        int len;
        char data[];
    } cdata_t;

    /* Types of things that can be in queue. */

    typedef enum dlq_type_e
    {
        DLQ_REC_FRAME,
        DLQ_CHANNEL_BUSY,
        DLQ_SEIZE_CONFIRM
    } dlq_type_t;

    typedef struct dlq_item_s
    {
        struct dlq_item_s *nextp;
        cdata_t *txdata;
        packet_t pp;
        dlq_type_t type;
        int client;
        int activity;
        int status;
        char addrs[AX25_ADDRS][AX25_MAX_ADDR_LEN];
    } dlq_item_t;

    void dlq_init(void);
    void dlq_rec_frame(packet_t pp);
    void dlq_channel_busy(int activity, int status);
    void dlq_seize_confirm();
    int dlq_wait_while_empty(double timeout_val);
    struct dlq_item_s *dlq_remove(void);
    void dlq_delete(struct dlq_item_s *pitem);
    cdata_t *cdata_new(int pid, char *data, int len);
    void cdata_delete(cdata_t *txdata);

#ifdef __cplusplus
}
#endif
