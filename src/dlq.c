/*
 * dlq.c
 *
 * IP Node Project
 *
 * Based on the Dire Wolf program
 * Copyright (C) 2011-2021 John Langner
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "ipnode.h"
#include "ax25_pad.h"
#include "audio.h"
#include "dlq.h"

static struct dlq_item_s *queue_head = NULL;

static pthread_mutex_t dlq_mutex;
static pthread_cond_t wake_up_cond;
static pthread_mutex_t wake_up_mutex;

static volatile int recv_thread_is_waiting = 0;
static volatile int s_new_count = 0;
static volatile int s_delete_count = 0;
static volatile int s_cdata_new_count = 0;
static volatile int s_cdata_delete_count = 0;

void dlq_init()
{
    queue_head = NULL;

    int err = pthread_mutex_init(&wake_up_mutex, NULL);

    err = pthread_mutex_init(&dlq_mutex, NULL);

    if (err != 0)
    {
        fprintf(stderr, "dlq_init: pthread_mutex_init err=%d", err);
        exit(1);
    }

    err = pthread_cond_init(&wake_up_cond, NULL);

    if (err != 0)
    {
        fprintf(stderr, "dlq_init: pthread_cond_init err=%d", err);
        exit(1);
    }

    recv_thread_is_waiting = 0;
}

static void append_to_queue(struct dlq_item_s *pnew)
{
    int queue_length = 0;

    pnew->nextp = NULL;

    int err = pthread_mutex_lock(&dlq_mutex);

    if (err != 0)
    {
        fprintf(stderr, "dlq append_to_queue: pthread_mutex_lock err=%d", err);
        exit(1);
    }

    if (queue_head == NULL)
    {
        queue_head = pnew;
        queue_length = 1;
    }
    else
    {
        queue_length = 2; /* head + new one */
        struct dlq_item_s *plast = queue_head;

        while (plast->nextp != NULL)
        {
            plast = plast->nextp;
            queue_length++;
        }

        plast->nextp = pnew;
    }

    err = pthread_mutex_unlock(&dlq_mutex);

    if (err != 0)
    {
        fprintf(stderr, "dlq append_to_queue: pthread_mutex_unlock err=%d", err);
        exit(1);
    }

    if (queue_length > 10)
    {
        fprintf(stderr, "Received frame queue is out of control. Length=%d.\n", queue_length);
    }

    if (recv_thread_is_waiting)
    {

        err = pthread_mutex_lock(&wake_up_mutex);

        if (err != 0)
        {
            fprintf(stderr, "dlq append_to_queue: pthread_mutex_lock wu err=%d", err);
            exit(1);
        }

        err = pthread_cond_signal(&wake_up_cond);

        if (err != 0)
        {
            fprintf(stderr, "dlq append_to_queue: pthread_cond_signal err=%d", err);
            exit(1);
        }

        err = pthread_mutex_unlock(&wake_up_mutex);

        if (err != 0)
        {
            fprintf(stderr, "dlq append_to_queue: pthread_mutex_unlock wu err=%d", err);
            exit(1);
        }
    }
}

/*
 * Called from il2p_rec upon IL2P_DECODE
 */
void dlq_rec_frame(packet_t pp)
{
    struct dlq_item_s *pnew = (struct dlq_item_s *)calloc(1, sizeof(struct dlq_item_s));

    s_new_count++;

    // TODO where does 50 come from?

    if (s_new_count > s_delete_count + 50)
    {
        fprintf(stderr, "INTERNAL ERROR:  DLQ memory leak, new=%d, delete=%d\n", s_new_count, s_delete_count);
    }

    pnew->nextp = NULL;
    pnew->type = DLQ_REC_FRAME;
    pnew->pp = pp;

    append_to_queue(pnew);
}

/*
 * Called from ptt
 */
void dlq_channel_busy(int activity, int status)
{
    if (activity == OCTYPE_PTT || activity == OCTYPE_DCD)
    {
        struct dlq_item_s *pnew = (struct dlq_item_s *)calloc(1, sizeof(struct dlq_item_s));
        s_new_count++;

        pnew->type = DLQ_CHANNEL_BUSY;
        pnew->activity = activity;
        pnew->status = status;

        append_to_queue(pnew);
    }
}

/*
 * Called from tx
 */
void dlq_seize_confirm()
{
    struct dlq_item_s *pnew = (struct dlq_item_s *)calloc(1, sizeof(struct dlq_item_s));
    s_new_count++;

    pnew->type = DLQ_SEIZE_CONFIRM;

    append_to_queue(pnew);
}

int dlq_wait_while_empty(double timeout)
{
    int timed_out_result = 0;

    if (queue_head == NULL)
    {
        int err = pthread_mutex_lock(&wake_up_mutex);

        if (err != 0)
        {
            fprintf(stderr, "dlq_wait_while_empty: pthread_mutex_lock wu err=%d", err);
            exit(1);
        }

        recv_thread_is_waiting = 1;

        if (timeout != 0.0)
        {
            struct timespec abstime;

            abstime.tv_sec = (time_t)(long)timeout;
            abstime.tv_nsec = (long)((timeout - (long)abstime.tv_sec) * 1000000000.0);

            err = pthread_cond_timedwait(&wake_up_cond, &wake_up_mutex, &abstime);

            if (err == ETIMEDOUT)
            {
                timed_out_result = 1;
            }
        }
        else
        {
            err = pthread_cond_wait(&wake_up_cond, &wake_up_mutex);
        }

        recv_thread_is_waiting = 0;

        err = pthread_mutex_unlock(&wake_up_mutex);

        if (err != 0)
        {
            fprintf(stderr, "dlq_wait_while_empty: pthread_mutex_unlock wu err=%d", err);
            exit(1);
        }
    }

    return timed_out_result;
}

struct dlq_item_s *dlq_remove()
{
    struct dlq_item_s *result = NULL;

    int err = pthread_mutex_lock(&dlq_mutex);

    if (err != 0)
    {
        fprintf(stderr, "dlq_remove: pthread_mutex_lock err=%d", err);
        exit(1);
    }

    if (queue_head != NULL)
    {
        result = queue_head;
        queue_head = queue_head->nextp;
    }

    err = pthread_mutex_unlock(&dlq_mutex);

    if (err != 0)
    {
        fprintf(stderr, "dlq_remove: pthread_mutex_unlock err=%d", err);
        exit(1);
    }

    return result;
}

void dlq_delete(struct dlq_item_s *pitem)
{
    if (pitem == NULL)
    {
        return;
    }

    s_delete_count++;

    if (pitem->pp != NULL)
    {
        ax25_delete(pitem->pp);
        pitem->pp = NULL;
    }

    if (pitem->txdata != NULL)
    {
        cdata_delete(pitem->txdata);
        pitem->txdata = NULL;
    }

    free(pitem);
}

cdata_t *cdata_new(int pid, char *data, int len)
{
    int size = (len + 127) & ~0x7f;

    cdata_t *cdata = calloc(1, sizeof(cdata_t) + size);

    cdata->magic = TXDATA_MAGIC;
    cdata->next = NULL;
    cdata->pid = pid;
    cdata->size = size;
    cdata->len = len;

    s_cdata_new_count++;

    if (data != NULL)
    {
        memcpy(cdata->data, data, len);
    }

    return cdata;
}

void cdata_delete(cdata_t *cdata)
{
    if (cdata == NULL)
    {
        return;
    }

    if (cdata->magic != TXDATA_MAGIC)
    {
        fprintf(stderr, "WARNING: cdata block corrupt\n");
        return;
    }

    s_cdata_delete_count++;

    free(cdata);
}
