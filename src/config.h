/*
 * config.h
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

    struct misc_config_s
    {
        int frack;    /* Number of seconds to wait for ack to transmission. */
        int retry;    /* Number of times to retry before giving up. */
        int paclen;   /* Max number of bytes in information part of frame. */
        int maxframe; /* Max frames to send before ACK.  mod 8 "Window" size. */
    };

    void config_init(char *fname, struct audio_s *p_modem, struct misc_config_s *misc_config);

#ifdef __cplusplus
}
#endif
