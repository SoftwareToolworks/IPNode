/*
 * tx.h
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

#include <complex.h>

#include "audio.h"

    void tx_init(struct audio_s *);
    void tx_frame_bits(unsigned char *tx_bits, int);

#ifdef __cplusplus
}
#endif
