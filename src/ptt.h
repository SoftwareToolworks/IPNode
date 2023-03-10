/*
 * ptt.h
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

    void ptt_init(struct audio_s *);
    void ptt_set(int, int);
    void ptt_term(void);
    int get_input(int);

#ifdef __cplusplus
}
#endif
