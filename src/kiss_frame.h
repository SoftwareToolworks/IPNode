/*
 * kiss_frame.h
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

#include "audio.h"

#define KISS_CMD_DATA_FRAME 0

#define FEND 0xC0
#define FESC 0xDB
#define TFEND 0xDC
#define TFESC 0xDD

    enum kiss_state_e
    {
        KS_SEARCHING,
        KS_COLLECTING
    };

#define MAX_KISS_LEN 2048

    typedef struct kiss_frame_s
    {
        enum kiss_state_e state;
        int kiss_len;
        unsigned char kiss_msg[MAX_KISS_LEN];
    } kiss_frame_t;

    void kiss_frame_init(struct audio_s *);
    int kiss_encapsulate(unsigned char *, int, unsigned char *);
    void kiss_rec_byte(kiss_frame_t *, unsigned char, int);

#ifdef __cplusplus
}
#endif
