/*
 * ax25_link.h
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
#include "dlq.h"
#include "config.h"

#define AX25_N1_PACLEN_MIN 1                 // Max bytes in Information part of frame.
#define AX25_N1_PACLEN_DEFAULT 256           // some v2.0 implementations have 128
#define AX25_N1_PACLEN_MAX AX25_MAX_INFO_LEN // from ax25_pad.h

#define AX25_N2_RETRY_MIN 1 // Number of times to retry before giving up.
#define AX25_N2_RETRY_DEFAULT 10
#define AX25_N2_RETRY_MAX 15

#define AX25_T1V_FRACK_MIN 1     // Number of seconds to wait before retrying.
#define AX25_T1V_FRACK_DEFAULT 3
#define AX25_T1V_FRACK_MAX 15

#define AX25_K_MAXFRAME_MIN 1 // Window size - number of I frames to send before waiting for ack.
#define AX25_K_MAXFRAME_DEFAULT 4
#define AX25_K_MAXFRAME_MAX 7

    double dtime_now(void);
    double ax25_link_get_next_timer_expiry(void);
    void ax25_link_init(struct misc_config_s *);
    void lm_data_indication(dlq_item_t *);
    void lm_seize_confirm(dlq_item_t *);
    void lm_channel_busy(dlq_item_t *);
    void dl_timer_expiry(void);

#ifdef __cplusplus
}
#endif
