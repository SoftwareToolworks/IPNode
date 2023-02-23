/*
 * kiss_pt.h
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
#include "config.h"
#include "kiss_frame.h"

    void kisspt_init(void);
    void kisspt_send_rec_packet(int, unsigned char *, int);

#ifdef __cplusplus
}
#endif
