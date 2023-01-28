/*
 * rx.h
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

    void rx_init(struct audio_s *pa);
    float rx_dc_average(void);
    void rx_process(void);

#ifdef __cplusplus
}
#endif
