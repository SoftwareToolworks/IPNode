/*
 * tq.h
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

#define TQ_PRIO_0_HI 0
#define TQ_PRIO_1_LO 1
#define TQ_NUM_PRIO 2

#define il2p_mutex_lock(x)                                                                                    \
  {                                                                                                           \
    int err = pthread_mutex_lock(x);                                                                          \
    if (err != 0)                                                                                             \
    {                                                                                                         \
      fprintf(stderr, "Fatal: INTERNAL ERROR %s %d pthread_mutex_lock returned %d", __FILE__, __LINE__, err); \
      exit(1);                                                                                                \
    }                                                                                                         \
  }

#define il2p_mutex_try_lock(x)                                                                                   \
  ({                                                                                                             \
    int err = pthread_mutex_trylock(x);                                                                          \
    if (err != 0 && err != EBUSY)                                                                                \
    {                                                                                                            \
      fprintf(stderr, "Fatal: INTERNAL ERROR %s %d pthread_mutex_trylock returned %d", __FILE__, __LINE__, err); \
      exit(1);                                                                                                   \
    };                                                                                                           \
    !err;                                                                                                        \
  })

#define il2p_mutex_unlock(x)                                                                                    \
  {                                                                                                             \
    int err = pthread_mutex_unlock(x);                                                                          \
    if (err != 0)                                                                                               \
    {                                                                                                           \
      fprintf(stderr, "Fatal: INTERNAL ERROR %s %d pthread_mutex_unlock returned %d", __FILE__, __LINE__, err); \
      exit(1);                                                                                                  \
    }                                                                                                           \
  }

  void tq_init(struct audio_s *);
  void tq_append(int, packet_t);
  void lm_data_request(int, packet_t);
  void lm_seize_request(void);
  void tq_wait_while_empty(void);
  packet_t tq_remove(int);
  packet_t tq_peek(int);
  int tq_count(int, char *, char *, int);

#ifdef __cplusplus
}
#endif
