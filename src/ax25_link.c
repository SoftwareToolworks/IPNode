/*
 * ax25_link.c
 *
 * IP Node Project
 *
 * Based on the Dire Wolf program
 * Copyright (C) 2011-2021 John Langner
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define _DEFAULT_SOURCE

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>
#include <time.h>
#include <bsd/bsd.h>

#include "ipnode.h"
#include "ax25_pad.h"
#include "ax25_link.h"
#include "dlq.h"
#include "tq.h"
#include "ptt.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

/*
 * AX.25 data link state machine.
 */

enum dlsm_state_e
{
    state_0_disconnected = 0,
    state_1_awaiting_connection = 1,
    state_2_awaiting_release = 2,
    state_3_connected = 3,
    state_4_timer_recovery = 4,
};

typedef struct ax25_dlsm_s
{
    int magic1;

#define MAGIC1 0x11592201

    struct ax25_dlsm_s *next; // Next in linked list.

    int stream_id;
    int client;
    char addrs[AX25_ADDRS][AX25_MAX_ADDR_LEN];

#define OWNCALL AX25_SOURCE
#define PEERCALL AX25_DESTINATION

    double start_time;
    enum dlsm_state_e state; // Current state..
    int n1_paclen;
    int n2_retry;
    int k_maxframe;
    int rc;
    int vs;
    int va;
    int vr;
    int layer_3_initiated;
    int peer_receiver_busy;
    int reject_exception;
    int own_receiver_busy;
    int acknowledge_pending;
    float srt;
    float t1v;

#define INIT_T1V_SRT                 \
    S->t1v = g_misc_config_p->frack; \
    S->srt = S->t1v / 2.0;

    int radio_channel_busy;
    double t1_exp;
    double t1_paused_at;
    float t1_remaining_when_last_stopped;
    int t1_had_expired;
    double t3_exp;

#define T3_DEFAULT 300.0

    int count_recv_frame_type[frame_not_AX25 + 1];
    int peak_rc_value;
    cdata_t *i_frame_queue;
    cdata_t *txdata_by_ns[128];
    int magic3;

#define MAGIC3 0x03331301

    cdata_t *rxdata_by_ns[128];
    int magic2;

#define MAGIC2 0x02221201

    cdata_t *ra_buff;
    int ra_following;
} ax25_dlsm_t;

static ax25_dlsm_t *list_head = NULL;

typedef struct reg_callsign_s
{
    char callsign[AX25_MAX_ADDR_LEN];
    int client;
    struct reg_callsign_s *next;
    int magic;
} reg_callsign_t;

static reg_callsign_t *reg_callsign_list = NULL;
static int dcd_status;
static int ptt_status;

#define SET_VS(n)    \
    {                \
        S->vs = (n); \
    }

#define SET_VA(n)                             \
    {                                         \
        S->va = (n);                          \
        int x = AX25MODULO(n - 1);            \
        while (S->txdata_by_ns[x] != NULL)    \
        {                                     \
            cdata_delete(S->txdata_by_ns[x]); \
            S->txdata_by_ns[x] = NULL;        \
            x = AX25MODULO(x - 1);            \
        }                                     \
    }

#define SET_VR(n)    \
    {                \
        S->vr = (n); \
    }

#define AX25MODULO(n) ((n)&7)

#define WITHIN_WINDOW_SIZE(x) (x->vs != AX25MODULO(x->va + x->k_maxframe))

#define START_T1 start_t1(S)
#define IS_T1_RUNNING is_t1_running(S)
#define STOP_T1 stop_t1(S)
#define PAUSE_T1 pause_t1(S)
#define RESUME_T1 resume_t1(S)

#define START_T3 start_t3(S)
#define STOP_T3 stop_t3(S)

static void dl_data_indication(ax25_dlsm_t *, int, char *, int);
static void i_frame(ax25_dlsm_t *S, cmdres_t, int, int, int, int, char *, int);
static void i_frame_continued(ax25_dlsm_t *, int, int, int, char *, int);
static int is_ns_in_window(ax25_dlsm_t *, int);
static void send_srej_frames(ax25_dlsm_t *, int *, int, int);
static int resend_for_srej(ax25_dlsm_t *, int, unsigned char *, int);
static void rr_rnr_frame(ax25_dlsm_t *, int, cmdres_t, int, int);
static void rej_frame(ax25_dlsm_t *, cmdres_t, int, int);
static void srej_frame(ax25_dlsm_t *, cmdres_t, int, int, unsigned char *, int);
static void sabm_frame(ax25_dlsm_t *, int);
static void disc_frame(ax25_dlsm_t *, int);
static void dm_frame(ax25_dlsm_t *, int);
static void ua_frame(ax25_dlsm_t *, int);
static void frmr_frame(ax25_dlsm_t *);
static void ui_frame(ax25_dlsm_t *, cmdres_t, int);
static void t1_expiry(ax25_dlsm_t *);
static void t3_expiry(ax25_dlsm_t *);
static void nr_error_recovery(ax25_dlsm_t *);
static void clear_exception_conditions(ax25_dlsm_t *);
static void transmit_enquiry(ax25_dlsm_t *);
static void select_t1_value(ax25_dlsm_t *);
static void establish_data_link(ax25_dlsm_t *);
static void set_version_2_0(ax25_dlsm_t *);
static int is_good_nr(ax25_dlsm_t *, int);
static void i_frame_pop_off_queue(ax25_dlsm_t *);
static void discard_i_queue(ax25_dlsm_t *);
static void invoke_retransmission(ax25_dlsm_t *, int);
static void check_i_frame_ackd(ax25_dlsm_t *, int);
static void check_need_for_response(ax25_dlsm_t *, ax25_frame_type_t, cmdres_t, int);
static void enquiry_response(ax25_dlsm_t *, ax25_frame_type_t, int);
static void enter_new_state(ax25_dlsm_t *, enum dlsm_state_e);

// Use macros above rather than calling these directly.

static void start_t1(ax25_dlsm_t *);
static void stop_t1(ax25_dlsm_t *);
static int is_t1_running(ax25_dlsm_t *);
static void pause_t1(ax25_dlsm_t *);
static void resume_t1(ax25_dlsm_t *);
static void start_t3(ax25_dlsm_t *);
static void stop_t3(ax25_dlsm_t *);

static struct misc_config_s *g_misc_config_p;

double dtime_now()
{
    struct timespec ts;

    clock_gettime(CLOCK_REALTIME, &ts);

    return ((double)(ts.tv_sec) + (double)(ts.tv_nsec) * 0.000000001);
}

void ax25_link_init(struct misc_config_s *pconfig)
{
    g_misc_config_p = pconfig;
}

static void i_frame_pop_off_queue(ax25_dlsm_t *S)
{
    if (S->i_frame_queue == NULL)
    {
        return;
    }

    switch (S->state)
    {
    case state_1_awaiting_connection:
        if (S->layer_3_initiated)
        {
            cdata_t *txdata;

            txdata = S->i_frame_queue; // Remove from head of list.
            S->i_frame_queue = txdata->next;
            cdata_delete(txdata);
        }
        break;

    case state_3_connected:
    case state_4_timer_recovery:
        while ((!S->peer_receiver_busy) && S->i_frame_queue != NULL && WITHIN_WINDOW_SIZE(S))
        {
            cdata_t *txdata = S->i_frame_queue; // Remove from head of list.
            S->i_frame_queue = txdata->next;
            txdata->next = NULL;

            cmdres_t cr = cr_cmd;
            int ns = S->vs;
            int nr = S->vr;
            int p = 0;

            packet_t pp = ax25_i_frame(S->addrs, cr, nr, ns, p, txdata->pid, (unsigned char *)(txdata->data), txdata->len);

            lm_data_request(TQ_PRIO_1_LO, pp);

            // Stash in sent array in case it gets lost and needs to be sent again.

            if (S->txdata_by_ns[ns] != NULL)
            {
                cdata_delete(S->txdata_by_ns[ns]);
            }

            S->txdata_by_ns[ns] = txdata;

            SET_VS(AX25MODULO(S->vs + 1)); // increment sequence of last sent.

            S->acknowledge_pending = 0;

            STOP_T3;
            START_T1;
        }
        break;

    case state_0_disconnected:
    case state_2_awaiting_release:
        break;
    }
}

static void discard_i_queue(ax25_dlsm_t *S)
{
    while (S->i_frame_queue != NULL)
    {
        cdata_t *t = S->i_frame_queue;

        S->i_frame_queue = S->i_frame_queue->next;

        cdata_delete(t);
    }
}

static int next_stream_id = 0;

static ax25_dlsm_t *get_link_handle(char addrs[AX25_ADDRS][AX25_MAX_ADDR_LEN], int client, int create)
{
    ax25_dlsm_t *p;

    // Look for existing.

    if (client == -1) // from the radio.
    {
        // address order is reversed for compare.
        for (p = list_head; p != NULL; p = p->next)
        {
            if (strcmp(addrs[AX25_DESTINATION], p->addrs[OWNCALL]) == 0 &&
                strcmp(addrs[AX25_SOURCE], p->addrs[PEERCALL]) == 0)
            {

                return (p);
            }
        }
    }
    else // from client app
    {
        for (p = list_head; p != NULL; p = p->next)
        {
            if (p->client == client &&
                strcmp(addrs[AX25_SOURCE], p->addrs[OWNCALL]) == 0 &&
                strcmp(addrs[AX25_DESTINATION], p->addrs[PEERCALL]) == 0)
            {

                return (p);
            }
        }
    }

    // Could not find existing.  Should we create a new one?

    if (!create)
    {
        return (NULL);
    }

    // If it came from the radio, search for destination our registered callsign list.

    int incoming_for_client = -1; // which client app registered the callsign?

    if (client == -1) // from the radio.
    {
        reg_callsign_t *r, *found;

        found = NULL;
        for (r = reg_callsign_list; r != NULL && found == NULL; r = r->next)
        {

            if (strcmp(addrs[AX25_DESTINATION], r->callsign) == 0)
            {
                found = r;
                incoming_for_client = r->client;
            }
        }

        if (found == NULL)
        {
            return (NULL);
        }
    }

    // Create new data link state machine.

    p = calloc(1, sizeof(ax25_dlsm_t));

    if (p == NULL)
    {
        fprintf(stderr, "FATAL ERROR: Out of memory.\n");
        exit(EXIT_FAILURE);
    }

    p->magic1 = MAGIC1;
    p->start_time = dtime_now();
    p->stream_id = next_stream_id++;

    // If it came in over the radio, we need to swap source/destination

    if (incoming_for_client >= 0)
    {
        strlcpy(p->addrs[AX25_SOURCE], addrs[AX25_DESTINATION], sizeof(p->addrs[AX25_SOURCE]));
        strlcpy(p->addrs[AX25_DESTINATION], addrs[AX25_SOURCE], sizeof(p->addrs[AX25_DESTINATION]));

        p->client = incoming_for_client;
    }
    else
    {
        memcpy(p->addrs, addrs, sizeof(p->addrs));
        p->client = client;
    }

    p->state = state_0_disconnected;
    p->t1_remaining_when_last_stopped = -999; // Invalid, don't use.

    p->magic2 = MAGIC2;
    p->magic3 = MAGIC3;

    // No need for critical region because this should all be in one thread.
    p->next = list_head;
    list_head = p;

    return p;
}

static void dl_data_indication(ax25_dlsm_t *S, int pid, char *data, int len)
{
    if (S->ra_buff == NULL)
    {
        // Ready state.
        if (pid != AX25_PID_SEGMENTATION_FRAGMENT)
        {
            return;
        }
        else if (data[0] & 0x80)
        {
            // Ready state, First segment.
            S->ra_following = data[0] & 0x7f;
            int total = (S->ra_following + 1) * (len - 1) - 1; // len should be other side's N1
            S->ra_buff = cdata_new(data[1], NULL, total);
            S->ra_buff->size = total;  // max that we are expecting.
            S->ra_buff->len = len - 2; // how much accumulated so far.
            memcpy(S->ra_buff->data, data + 2, len - 2);
        }
        else
        {
            fprintf(stderr, "Stream %d: AX.25 Reassembler Protocol Error Z: Not first segment in ready state.\n", S->stream_id);
        }
    }
    else
    {
        // Reassembling data state
        if (pid != AX25_PID_SEGMENTATION_FRAGMENT)
        {
            fprintf(stderr, "Stream %d: AX.25 Reassembler Protocol Error Z: Not segment in reassembling state.\n", S->stream_id);
            cdata_delete(S->ra_buff);
            S->ra_buff = NULL;
            return;
        }
        else if (data[0] & 0x80)
        {
            fprintf(stderr, "Stream %d: AX.25 Reassembler Protocol Error Z: First segment in reassembling state.\n", S->stream_id);
            cdata_delete(S->ra_buff);
            S->ra_buff = NULL;
            return;
        }
        else if ((data[0] & 0x7f) != S->ra_following - 1)
        {
            fprintf(stderr, "Stream %d: AX.25 Reassembler Protocol Error Z: Segments out of sequence.\n", S->stream_id);
            cdata_delete(S->ra_buff);
            S->ra_buff = NULL;
            return;
        }
        else
        {
            // Reassembling data state, Not first segment.
            S->ra_following = data[0] & 0x7f;
            if (S->ra_buff->len + len - 1 <= S->ra_buff->size)
            {
                memcpy(S->ra_buff->data + S->ra_buff->len, data + 1, len - 1);
                S->ra_buff->len += len - 1;
            }
            else
            {
                fprintf(stderr, "Stream %d: AX.25 Reassembler Protocol Error Z: Segments exceed buffer space.\n", S->stream_id);
                cdata_delete(S->ra_buff);
                S->ra_buff = NULL;
                return;
            }

            if (S->ra_following == 0)
            {
                // Last one.
                cdata_delete(S->ra_buff);
                S->ra_buff = NULL;
            }
        }
    }
}

void lm_channel_busy(dlq_item_t *E)
{
    switch (E->activity)
    {
    case OCTYPE_DCD:
        dcd_status = E->status;
        break;

    case OCTYPE_PTT:
        ptt_status = E->status;
        break;

    default:
        break;
    }

    int busy = dcd_status | ptt_status;

    /*
     * We know if the given radio channel is busy or not.
     * This must be applied to all data link state machines associated with that radio channel.
     */

    ax25_dlsm_t *S;

    for (S = list_head; S != NULL; S = S->next)
    {
        if (busy && !S->radio_channel_busy)
        {
            S->radio_channel_busy = 1;
            PAUSE_T1;
        }
        else if (!busy && S->radio_channel_busy)
        {
            S->radio_channel_busy = 0;
            RESUME_T1;
        }
    }
}

void lm_seize_confirm(dlq_item_t *E)
{
    ax25_dlsm_t *S;

    for (S = list_head; S != NULL; S = S->next)
    {
        switch (S->state)
        {
        case state_0_disconnected:
        case state_1_awaiting_connection:
        case state_2_awaiting_release:
            break;

        case state_3_connected:
        case state_4_timer_recovery:

            i_frame_pop_off_queue(S);

            // Need an RR if we didn't have I frame send the necessary ack.

            if (S->acknowledge_pending)
            {
                S->acknowledge_pending = 0;
                enquiry_response(S, frame_not_AX25, 0);
            }

            break;
        }
    }
}

/*
 * Called from rx upon DLQ_REC_FRAME
 */
void lm_data_indication(dlq_item_t *E)
{
    ax25_frame_type_t ftype;
    cmdres_t cr;
    int pf;
    int nr;
    int ns;
    ax25_dlsm_t *S;
    int client_not_applicable = -1;
    int n;

    if (E->pp == NULL)
    {
        fprintf(stderr, "Internal Error, packet pointer is null\n");
        return;
    }

    // Copy addresses from frame into event structure.

    for (n = 0; n < 2; n++)
    {
        ax25_get_addr_with_ssid(E->pp, n, E->addrs[n]);
    }

    ftype = ax25_frame_type(E->pp, &cr, &pf, &nr, &ns);

    S = get_link_handle(E->addrs, client_not_applicable,
                        (ftype == frame_type_U_SABM) | (ftype == frame_type_U_SABME));

    if (S == NULL)
    {
        return;
    }

    /*
     * Now we need to use ax25_frame_type again because the previous results, for nr and ns, might be wrong.
     */

    ftype = ax25_frame_type(E->pp, &cr, &pf, &nr, &ns);

    // Gather statistics useful for testing.

    if (ftype <= frame_not_AX25)
    {
        S->count_recv_frame_type[ftype]++;
    }

    switch (ftype)
    {
    case frame_type_I:
        if (cr != cr_cmd)
        {
            fprintf(stderr, "Stream %d: AX.25 Protocol Error S: must be COMMAND.\n", S->stream_id);
        }
        break;

    case frame_type_S_RR:
    case frame_type_S_RNR:
    case frame_type_S_REJ:
        if (cr != cr_cmd && cr != cr_res)
        {
            fprintf(stderr, "Stream %d: AX.25 Protocol Error: must be COMMAND or RESPONSE.\n", S->stream_id);
        }
        break;

    case frame_type_U_SABM:
    case frame_type_U_DISC:
        if (cr != cr_cmd)
        {
            fprintf(stderr, "Stream %d: AX.25 Protocol Error: must be COMMAND.\n", S->stream_id);
        }
        break;

    case frame_type_S_SREJ:
    case frame_type_U_DM:
    case frame_type_U_UA:
    case frame_type_U_FRMR:
        if (cr != cr_res)
        {
            fprintf(stderr, "Stream %d: AX.25 Protocol Error: must be RESPONSE.\n", S->stream_id);
        }
        break;

    case frame_type_U_UI:
    case frame_type_U:
    case frame_not_AX25:
    case frame_type_U_XID:
    case frame_type_U_TEST:
    case frame_type_U_SABME:
        // not expected.
        break;
    }

    switch (ftype)
    {

    case frame_type_I: // Information
    {
        int pid;
        unsigned char *info_ptr;
        int info_len;

        pid = ax25_get_pid(E->pp);
        info_len = ax25_get_info(E->pp, &info_ptr);

        i_frame(S, cr, pf, nr, ns, pid, (char *)info_ptr, info_len);
    }
    break;

    case frame_type_S_RR: // Receive Ready - System Ready To Receive
        rr_rnr_frame(S, 1, cr, pf, nr);
        break;

    case frame_type_S_RNR: // Receive Not Ready - TNC Buffer Full
        rr_rnr_frame(S, 0, cr, pf, nr);
        break;

    case frame_type_S_REJ: // Reject Frame - Out of Sequence or Duplicate
        rej_frame(S, cr, pf, nr);
        break;

    case frame_type_S_SREJ: // Selective Reject - Ask for selective frame(s) repeat
    {
        unsigned char *info_ptr;
        int info_len;

        info_len = ax25_get_info(E->pp, &info_ptr);
        srej_frame(S, cr, pf, nr, info_ptr, info_len);
    }
    break;

    case frame_type_U_SABM: // Set Async Balanced Mode
        sabm_frame(S, pf);
        break;

    case frame_type_U_DISC: // Disconnect
        disc_frame(S, pf);
        break;

    case frame_type_U_DM: // Disconnect Mode
        dm_frame(S, pf);
        break;

    case frame_type_U_UA: // Unnumbered Acknowledge
        ua_frame(S, pf);
        break;

    case frame_type_U_FRMR: // Frame Reject
        frmr_frame(S);
        break;

    case frame_type_U_UI: // Unnumbered Information
        ui_frame(S, cr, pf);
        break;

    case frame_type_U:   // other Unnumbered, not used by AX.25.
    case frame_not_AX25: // Could not get control byte from frame.
    case frame_type_U_SABME:
    case frame_type_U_XID:
    case frame_type_U_TEST:
        break;
    }

    if (S->i_frame_queue != NULL &&
        (S->state == state_3_connected || S->state == state_4_timer_recovery) &&
        (!S->peer_receiver_busy) &&
        WITHIN_WINDOW_SIZE(S))
    {

        // S->acknowledge_pending = 1;
        lm_seize_request();
    }
}

static void i_frame(ax25_dlsm_t *S, cmdres_t cr, int p, int nr, int ns, int pid, char *info_ptr, int info_len)
{
    switch (S->state)
    {
    case state_0_disconnected:

        // Logic from flow chart for "all other commands."

        if (cr == cr_cmd)
        {
            cmdres_t r = cr_res; // DM response with F taken from P.
            int f = p;
            int nopid = 0; // PID applies only for I and UI frames.

            packet_t pp = ax25_u_frame(S->addrs, r, frame_type_U_DM, f, nopid, NULL, 0);
            lm_data_request(TQ_PRIO_1_LO, pp);
        }
        break;

    case state_1_awaiting_connection:
        // Ignore it.  Keep same state.
        break;

    case state_2_awaiting_release:

        // Logic from flow chart for "I, RR, RNR, REJ, SREJ commands."

        if (cr == cr_cmd && p == 1)
        {
            cmdres_t r = cr_res; // DM response with F = 1.
            int f = 1;
            int nopid = 0; // PID applies only for I and UI frames.

            packet_t pp = ax25_u_frame(S->addrs, r, frame_type_U_DM, f, nopid, NULL, 0);
            lm_data_request(TQ_PRIO_1_LO, pp);
        }
        break;

    case state_3_connected:
    case state_4_timer_recovery:

        if (info_len >= 0 && info_len <= AX25_MAX_INFO_LEN)
        {

            if (is_good_nr(S, nr))
            {
                check_i_frame_ackd(S, nr);

                if (S->state == state_4_timer_recovery && S->va == S->vs)
                {
                    STOP_T1;
                    select_t1_value(S);
                    START_T3;
                    S->rc = 0;
                    enter_new_state(S, state_3_connected);
                }

                if (S->own_receiver_busy)
                {
                    if (p == 1)
                    {
                        cmdres_t cr = cr_res;
                        int f = 1;
                        int nr = S->vr;
                        packet_t pp;

                        pp = ax25_s_frame(S->addrs, cr, frame_type_S_RNR, nr, f, NULL, 0);

                        lm_data_request(TQ_PRIO_1_LO, pp);

                        S->acknowledge_pending = 0;
                    }
                }
                else
                {
                    i_frame_continued(S, p, ns, pid, info_ptr, info_len);
                }
            }
            else
            {
                nr_error_recovery(S);
                enter_new_state(S, state_1_awaiting_connection);
            }
        }
        else
        {
            fprintf(stderr, "Stream %d: AX.25 Protocol Error O: Information part length, %d, not in range of 0 thru %d.\n", S->stream_id, info_len, AX25_MAX_INFO_LEN);

            establish_data_link(S);
            S->layer_3_initiated = 0;
            enter_new_state(S, state_1_awaiting_connection);
        }
        break;
    }
}

static void i_frame_continued(ax25_dlsm_t *S, int p, int ns, int pid, char *info_ptr, int info_len)
{
    if (ns == S->vr)
    {
        SET_VR(AX25MODULO(S->vr + 1));
        S->reject_exception = 0;

        dl_data_indication(S, pid, info_ptr, info_len);

        if (S->rxdata_by_ns[ns] != NULL)
        {
            cdata_delete(S->rxdata_by_ns[ns]);
            S->rxdata_by_ns[ns] = NULL;
        }

        while (S->rxdata_by_ns[S->vr] != NULL)
        {

            dl_data_indication(S, S->rxdata_by_ns[S->vr]->pid, S->rxdata_by_ns[S->vr]->data, S->rxdata_by_ns[S->vr]->len);

            // Don't keep around anymore after sending it to client app.

            cdata_delete(S->rxdata_by_ns[S->vr]);
            S->rxdata_by_ns[S->vr] = NULL;

            SET_VR(AX25MODULO(S->vr + 1));
        }

        if (p)
        {
            int f = 1;
            int nr = S->vr;       // Next expected sequence number.
            cmdres_t cr = cr_res; // response with F set to 1.
            packet_t pp;

            pp = ax25_s_frame(S->addrs, cr, frame_type_S_RR, nr, f, NULL, 0);
            lm_data_request(TQ_PRIO_1_LO, pp);
            S->acknowledge_pending = 0;
        }
        else if (!S->acknowledge_pending)
        {

            S->acknowledge_pending = 1;

            lm_seize_request();
        }
    }
    else if (S->reject_exception)
    {
        if (p)
        {
            int f = 1;
            int nr = S->vr;       // Next expected sequence number.
            cmdres_t cr = cr_res; // response with F set to 1.
            packet_t pp;

            pp = ax25_s_frame(S->addrs, cr, frame_type_S_RR, nr, f, NULL, 0);
            lm_data_request(TQ_PRIO_1_LO, pp);
            S->acknowledge_pending = 0;
        }
    }
    else
    {
        fprintf(stderr, "INTERNAL ERROR: Should not be sending SREJ in basic (modulo 8) mode.\n");

        if (is_ns_in_window(S, ns))
        {
            if (S->rxdata_by_ns[ns] != NULL)
            {
                cdata_delete(S->rxdata_by_ns[ns]);
                S->rxdata_by_ns[ns] = NULL;
            }

            S->rxdata_by_ns[ns] = cdata_new(pid, info_ptr, info_len);

            if (p == 1)
            {
                int f = 1;
                enquiry_response(S, frame_type_I, f);
            }
            else if (S->own_receiver_busy)
            {
                cmdres_t cr = cr_res; // send RNR response
                int f = 0;            // we know p=0 here.
                int nr = S->vr;
                packet_t pp;

                pp = ax25_s_frame(S->addrs, cr, frame_type_S_RNR, nr, f, NULL, 0);
                lm_data_request(TQ_PRIO_1_LO, pp);
            }
            else if (S->rxdata_by_ns[AX25MODULO(ns - 1)] == NULL)
            {
                int ask_for_resend[8];
                int ask_resend_count = 0;
                int allow_f1 = 1; // F=1 from X.25 2.4.6.4 b) 3)

                // send only for this gap, not cumulative from V(R).

                int last = AX25MODULO(ns - 1);
                int first = last;

                while (first != S->vr && S->rxdata_by_ns[AX25MODULO(first - 1)] == NULL)
                {
                    first = AX25MODULO(first - 1);
                }

                int x = first;

                do
                {
                    ask_for_resend[ask_resend_count++] = AX25MODULO(x);
                    x = AX25MODULO(x + 1);
                } while (x != AX25MODULO(last + 1));

                send_srej_frames(S, ask_for_resend, ask_resend_count, allow_f1);
            }
        }
        else
        {
            if (p == 1)
            {
                int f = 1;
                enquiry_response(S, frame_type_I, f);
            }
        }
    }
}

static int is_ns_in_window(ax25_dlsm_t *S, int ns)
{
    /* Shift all values relative to V(R) before comparing so we won't have wrap around. */

#define adjust_by_vr(x) (AX25MODULO((x)-S->vr))

    int adjusted_vr = adjust_by_vr(S->vr); // A clever compiler would know it is zero.
    int adjusted_ns = adjust_by_vr(ns);
    int adjusted_vrpk = adjust_by_vr(S->vr + 63); // 63 generous

    int result = (adjusted_vr < adjusted_ns) && (adjusted_ns < adjusted_vrpk);

    return result;
}

static void send_srej_frames(ax25_dlsm_t *S, int *resend, int count, int allow_f1)
{
    if (count <= 0)
    {
        fprintf(stderr, "Fatal: INTERNAL ERROR, count=%d\n", count);
        return;
    }

    // Something is wrong!  We ask for more than the window size.

    if (count > S->k_maxframe)
    {

        fprintf(stderr, "INTERNAL ERROR - Extreme number of SREJ\n");
        fprintf(stderr, "state=%d, count=%d, k=%d, V(R)=%d\n", S->state, count, S->k_maxframe, S->vr);
        fprintf(stderr, "resend[]=");

        for (int i = 0; i < count; i++)
        {
            fprintf(stderr, " %d", resend[i]);
        }

        fprintf(stderr, "\nrxdata_by_ns[]=");

        for (int i = 0; i < 128; i++)
        {
            if (S->rxdata_by_ns[i] != NULL)
            {
                fprintf(stderr, " %d", i);
            }
        }

        fprintf(stderr, "\n");
    }

    for (int i = 0; i < count; i++)
    {
        int nr = resend[i];
        int f = allow_f1 && (nr == S->vr);  // Set if we are ack-ing one before.

        if (f)
        {
            S->acknowledge_pending = 0;
        }

        if (nr < 0 || nr >= 8)
        {
            fprintf(stderr, "INTERNAL ERROR, nr=%d\n", nr);
            nr = AX25MODULO(nr);
        }

        packet_t pp = ax25_s_frame(S->addrs, cr_res, frame_type_S_SREJ, nr, f, NULL, 0);// SREJ is always response. (p.s. cr_res is an enum)
        
        lm_data_request(TQ_PRIO_1_LO, pp);
    }
}

static void rr_rnr_frame(ax25_dlsm_t *S, int ready, cmdres_t cr, int pf, int nr)
{
    switch (S->state)
    {

    case state_0_disconnected:

        if (cr == cr_cmd)
        {
            cmdres_t r = cr_res; // DM response with F taken from P.
            int f = pf;
            int nopid = 0; // PID only for I and UI frames.
            packet_t pp = ax25_u_frame(S->addrs, r, frame_type_U_DM, f, nopid, NULL, 0);
            lm_data_request(TQ_PRIO_1_LO, pp);
        }
        break;

    case state_1_awaiting_connection:

        // do nothing.
        break;

    case state_2_awaiting_release:

        // Logic from flow chart for "I, RR, RNR, REJ, SREJ commands."

        if (cr == cr_cmd && pf == 1)
        {
            cmdres_t r = cr_res; // DM response with F = 1.
            int f = 1;
            int nopid = 0; // PID applies only for I and UI frames.

            packet_t pp = ax25_u_frame(S->addrs, r, frame_type_U_DM, f, nopid, NULL, 0);
            lm_data_request(TQ_PRIO_1_LO, pp);
        }

        break;

    case state_3_connected:

        S->peer_receiver_busy = !ready;

        if (cr == cr_cmd && pf)
        {
            check_need_for_response(S, ready ? frame_type_S_RR : frame_type_S_RNR, cr, pf);
        }

        if (is_good_nr(S, nr))
        {
            check_i_frame_ackd(S, nr);
        }
        else
        {
            nr_error_recovery(S);
            enter_new_state(S, state_1_awaiting_connection);
        }

        break;

    case state_4_timer_recovery:

        S->peer_receiver_busy = !ready;

        if (cr == cr_res && pf == 1)
        {

            // RR/RNR Response with F==1.

            STOP_T1;
            select_t1_value(S);

            if (is_good_nr(S, nr))
            {

                SET_VA(nr);
                if (S->vs == S->va)
                {
                    START_T3;
                    S->rc = 0;
                    enter_new_state(S, state_3_connected);
                }
                else
                {
                    invoke_retransmission(S, nr);
                    STOP_T3;
                    START_T1;
                    S->acknowledge_pending = 0;
                }
            }
            else
            {
                nr_error_recovery(S);
                enter_new_state(S, state_1_awaiting_connection);
            }
        }
        else
        {
            if (cr == cr_cmd && pf == 1)
            {
                int f = 1;
                enquiry_response(S, ready ? frame_type_S_RR : frame_type_S_RNR, f);
            }

            if (is_good_nr(S, nr))
            {

                SET_VA(nr);

                if (cr == cr_res && pf == 0)
                {

                    if (S->vs == S->va)
                    { // all caught up with ack from other guy.
                        STOP_T1;
                        select_t1_value(S);
                        START_T3;
                        S->rc = 0;
                        enter_new_state(S, state_3_connected);
                    }
                }
            }
            else
            {
                nr_error_recovery(S);
                enter_new_state(S, state_1_awaiting_connection);
            }
        }
        break;
    }
}

static void rej_frame(ax25_dlsm_t *S, cmdres_t cr, int pf, int nr)
{

    switch (S->state)
    {

    case state_0_disconnected:

        // states 0 and 2 are very similar with one tiny little difference.

        if (cr == cr_cmd)
        {
            cmdres_t r = cr_res; // DM response with F taken from P.
            int f = pf;
            int nopid = 0; // PID is only for I and UI.

            packet_t pp = ax25_u_frame(S->addrs, r, frame_type_U_DM, f, nopid, NULL, 0);
            lm_data_request(TQ_PRIO_1_LO, pp);
        }
        break;

    case state_1_awaiting_connection:
        // Do nothing.
        break;

    case state_2_awaiting_release:

        if (cr == cr_cmd && pf == 1)
        {
            cmdres_t r = cr_res; // DM response with F = 1.
            int f = 1;
            int nopid = 0;

            packet_t pp = ax25_u_frame(S->addrs, r, frame_type_U_DM, f, nopid, NULL, 0);
            lm_data_request(TQ_PRIO_1_LO, pp);
        }
        break;

    case state_3_connected:

        S->peer_receiver_busy = 0;

        check_need_for_response(S, frame_type_S_REJ, cr, pf);

        if (is_good_nr(S, nr))
        {
            SET_VA(nr);
            STOP_T1;
            STOP_T3;
            select_t1_value(S);

            invoke_retransmission(S, nr);

            // T3 is already stopped.
            START_T1;
            S->acknowledge_pending = 0;
        }
        else
        {
            nr_error_recovery(S);
            enter_new_state(S, state_1_awaiting_connection);
        }
        break;

    case state_4_timer_recovery:

        S->peer_receiver_busy = 0;

        if (cr == cr_res && pf == 1)
        {

            STOP_T1;
            select_t1_value(S);

            if (is_good_nr(S, nr))
            {

                SET_VA(nr);
                if (S->vs == S->va)
                {
                    START_T3;
                    S->rc = 0;
                    enter_new_state(S, state_3_connected);
                }
                else
                {
                    invoke_retransmission(S, nr);
                    STOP_T3;
                    START_T1;
                    S->acknowledge_pending = 0;
                }
            }
            else
            {
                nr_error_recovery(S);
                enter_new_state(S, state_1_awaiting_connection);
            }
        }
        else
        {
            if (cr == cr_cmd && pf == 1)
            {
                int f = 1;
                enquiry_response(S, frame_type_S_REJ, f);
            }

            if (is_good_nr(S, nr))
            {

                SET_VA(nr);

                if (S->vs != S->va)
                {
                    invoke_retransmission(S, nr);
                    STOP_T3;
                    START_T1;
                    S->acknowledge_pending = 0;
                }
            }
            else
            {
                nr_error_recovery(S);
                enter_new_state(S, state_1_awaiting_connection);
            }
        }
        break;
    }
}

static void srej_frame(ax25_dlsm_t *S, cmdres_t cr, int f, int nr, unsigned char *info, int info_len)
{

    switch (S->state)
    {

    case state_0_disconnected:
    case state_1_awaiting_connection:
    case state_2_awaiting_release:
        break;

    case state_3_connected:

        S->peer_receiver_busy = 0;

        if (is_good_nr(S, nr))
        {

            if (f)
            {
                SET_VA(nr);
            }
            STOP_T1;
            START_T3;
            select_t1_value(S);

            int num_resent = resend_for_srej(S, nr, info, info_len);

            if (num_resent)
            {
                STOP_T3;
                START_T1;
                S->acknowledge_pending = 0;
            }
            // keep same state.
        }
        else
        {
            nr_error_recovery(S);
            enter_new_state(S, state_1_awaiting_connection);
        }
        break;

    case state_4_timer_recovery:

        S->peer_receiver_busy = 0;
        STOP_T1;
        select_t1_value(S);

        if (is_good_nr(S, nr))
        {

            if (f)
            {
                SET_VA(nr);
            }

            if (S->vs == S->va)
            { // ACKs all caught up.  Back to state 3.
                START_T3;
                S->rc = 0; // My enhancement.  See Erratum note in select_t1_value.
                enter_new_state(S, state_3_connected);
            }
            else
            {
                int num_resent = resend_for_srej(S, nr, info, info_len);

                if (num_resent)
                {
                    STOP_T3;
                    START_T1;
                    S->acknowledge_pending = 0;
                }
            }
        }
        else
        {
            nr_error_recovery(S);
            enter_new_state(S, state_1_awaiting_connection);
        }
        break;
    }
}

static int resend_for_srej(ax25_dlsm_t *S, int nr, unsigned char *info, int info_len)
{
    cmdres_t cr = cr_cmd;
    int i_frame_nr = S->vr;
    int i_frame_ns = nr;
    int p = 0;
    int num_resent = 0;

    cdata_t *txdata = S->txdata_by_ns[i_frame_ns];

    if (txdata != NULL)
    {
        packet_t pp = ax25_i_frame(S->addrs, cr, i_frame_nr, i_frame_ns, p, txdata->pid, (unsigned char *)(txdata->data), txdata->len);

        lm_data_request(TQ_PRIO_1_LO, pp);
        num_resent++;
    }
    else
    {
        fprintf(stderr, "Stream %d: INTERNAL ERROR for SREJ.  I frame for N(S)=%d is not available.\n", S->stream_id, i_frame_ns);
    }

    // Multi-SREJ if there is an information part.

    for (int j = 0; j < info_len; j++)
    {
        i_frame_ns = (info[j] >> 5) & 0x07; // no provision for span.

        txdata = S->txdata_by_ns[i_frame_ns];

        if (txdata != NULL)
        {
            packet_t pp = ax25_i_frame(S->addrs, cr, i_frame_nr, i_frame_ns, p, txdata->pid, (unsigned char *)(txdata->data), txdata->len);
            lm_data_request(TQ_PRIO_1_LO, pp);
            num_resent++;
        }
        else
        {
            fprintf(stderr, "Stream %d: INTERNAL ERROR for Multi-SREJ.  I frame for N(S)=%d is not available.\n", S->stream_id, i_frame_ns);
        }
    }

    return num_resent;
}

static void sabm_frame(ax25_dlsm_t *S, int p)
{
    packet_t pp;
    cmdres_t res;
    int f;
    int nopid;

    switch (S->state)
    {

    case state_0_disconnected:

        set_version_2_0(S);

        res = cr_res;
        f = p;
        nopid = 0; // PID is only for I and UI.

        pp = ax25_u_frame(S->addrs, res, frame_type_U_UA, f, nopid, NULL, 0);
        lm_data_request(TQ_PRIO_1_LO, pp);

        clear_exception_conditions(S);

        SET_VS(0);
        SET_VA(0);
        SET_VR(0);

        fprintf(stderr, "Stream %d: Connected to %s (v2.0)\n", S->stream_id, S->addrs[PEERCALL]);

        INIT_T1V_SRT;
        START_T3;
        S->rc = 0; // My enhancement.  See Erratum note in select_t1_value.
        enter_new_state(S, state_3_connected);
        break;

    case state_1_awaiting_connection:

        // Don't combine with state 5.  They are slightly different.

        res = cr_res;
        f = p;
        nopid = 0;

        pp = ax25_u_frame(S->addrs, res, frame_type_U_UA, f, nopid, NULL, 0);
        lm_data_request(TQ_PRIO_1_LO, pp); // stay in state 1.
        break;

    case state_2_awaiting_release:
        res = cr_res;
        f = p;
        nopid = 0;

        pp = ax25_u_frame(S->addrs, res, frame_type_U_DM, f, nopid, NULL, 0);
        lm_data_request(TQ_PRIO_0_HI, pp); // expedited stay in state 2.
        break;

    case state_3_connected:
    case state_4_timer_recovery:
        res = cr_res;
        f = p;
        nopid = 0;

        pp = ax25_u_frame(S->addrs, res, frame_type_U_UA, f, nopid, NULL, 0);
        lm_data_request(TQ_PRIO_1_LO, pp);

        if (S->state == state_4_timer_recovery)
        {
            set_version_2_0(S);
        }

        clear_exception_conditions(S);

        if (S->vs != S->va)
        {
            discard_i_queue(S);
        }
        STOP_T1;
        START_T3;
        SET_VS(0);
        SET_VA(0);
        SET_VR(0);
        S->rc = 0;
        enter_new_state(S, state_3_connected);
        break;
    }
}

static void disc_frame(ax25_dlsm_t *S, int p)
{

    switch (S->state)
    {

    case state_0_disconnected:
    case state_1_awaiting_connection:
    {
        cmdres_t res = cr_res;
        int f = p;
        int nopid = 0;

        packet_t pp = ax25_u_frame(S->addrs, res, frame_type_U_DM, f, nopid, NULL, 0);
        lm_data_request(TQ_PRIO_1_LO, pp);
    }
    // keep current state, 0, 1, or 5.
    break;

    case state_2_awaiting_release:

    {
        cmdres_t res = cr_res;
        int f = p;
        int nopid = 0;

        packet_t pp = ax25_u_frame(S->addrs, res, frame_type_U_UA, f, nopid, NULL, 0);
        lm_data_request(TQ_PRIO_0_HI, pp); // expedited
    }
    // keep current state, 2.
    break;

    case state_3_connected:
    case state_4_timer_recovery:

    {
        discard_i_queue(S);

        cmdres_t res = cr_res;
        int f = p;
        int nopid = 0;

        packet_t pp = ax25_u_frame(S->addrs, res, frame_type_U_UA, f, nopid, NULL, 0);
        lm_data_request(TQ_PRIO_1_LO, pp);

        fprintf(stderr, "Stream %d: Disconnected from %s.\n", S->stream_id, S->addrs[PEERCALL]);

        STOP_T1;
        STOP_T3;
        enter_new_state(S, state_0_disconnected);
    }
    break;
    }
}

static void dm_frame(ax25_dlsm_t *S, int f)
{
    switch (S->state)
    {

    case state_0_disconnected:
        // Do nothing.
        break;

    case state_1_awaiting_connection:

        if (f == 1)
        {
            discard_i_queue(S);
            fprintf(stderr, "Stream %d: Disconnected from %s.\n", S->stream_id, S->addrs[PEERCALL]);
            STOP_T1;
            enter_new_state(S, state_0_disconnected);
        }
        break;

    case state_2_awaiting_release:

        if (f == 1)
        {
            fprintf(stderr, "Stream %d: Disconnected from %s.\n", S->stream_id, S->addrs[PEERCALL]);
            STOP_T1;
            enter_new_state(S, state_0_disconnected);
        }
        break;

    case state_3_connected:
    case state_4_timer_recovery:
        fprintf(stderr, "Stream %d: Disconnected from %s.\n", S->stream_id, S->addrs[PEERCALL]);
        discard_i_queue(S);
        STOP_T1;
        STOP_T3;
        enter_new_state(S, state_0_disconnected);
        break;
    }
}

static void ua_frame(ax25_dlsm_t *S, int f)
{
    switch (S->state)
    {

    case state_0_disconnected:
        break;

    case state_1_awaiting_connection:

        if (f == 1)
        {
            if (S->layer_3_initiated)
            {
                fprintf(stderr, "Stream %d: Connected to %s\n", S->stream_id, S->addrs[PEERCALL]);
            }
            else if (S->vs != S->va)
            {
                INIT_T1V_SRT;
                START_T3;

                fprintf(stderr, "Stream %d: Connected to %s\n", S->stream_id, S->addrs[PEERCALL]);
            }

            STOP_T1;
            // My version.
            START_T3;

            SET_VS(0);
            SET_VA(0);
            SET_VR(0);
            select_t1_value(S);
            S->rc = 0;
            enter_new_state(S, state_3_connected);
        }
        break;

    case state_2_awaiting_release:
        if (f == 1)
        {
            fprintf(stderr, "Stream %d: Disconnected from %s.\n", S->stream_id, S->addrs[PEERCALL]);
            STOP_T1;
            enter_new_state(S, state_0_disconnected);
        }
        else
        {
            // stay in same state.
        }
        break;

    case state_3_connected:
    case state_4_timer_recovery:

        establish_data_link(S);
        S->layer_3_initiated = 0;
        enter_new_state(S, state_1_awaiting_connection);
        break;
    }
}

static void frmr_frame(ax25_dlsm_t *S)
{
    switch (S->state)
    {
    case state_0_disconnected:
    case state_1_awaiting_connection:
    case state_2_awaiting_release:
        break;

    case state_3_connected:
    case state_4_timer_recovery:

        set_version_2_0(S);
        establish_data_link(S);
        S->layer_3_initiated = 0;
        enter_new_state(S, state_1_awaiting_connection);
        break;
    }
}

static void ui_frame(ax25_dlsm_t *S, cmdres_t cr, int pf)
{
    if (cr == cr_cmd && pf == 1)
    {

        switch (S->state)
        {

        case state_0_disconnected:
        case state_1_awaiting_connection:
        case state_2_awaiting_release:
        {
            cmdres_t r = cr_res; // DM response with F taken from P.
            int nopid = 0;       // PID applies only for I and UI frames.

            packet_t pp = ax25_u_frame(S->addrs, r, frame_type_U_DM, pf, nopid, NULL, 0);
            lm_data_request(TQ_PRIO_1_LO, pp);
        }
        break;

        case state_3_connected:
        case state_4_timer_recovery:

            enquiry_response(S, frame_type_U_UI, pf);
            break;
        }
    }
}

void dl_timer_expiry()
{
    ax25_dlsm_t *p;
    double now = dtime_now();

    for (p = list_head; p != NULL; p = p->next)
    {
        if (p->t1_exp != 0 && p->t1_paused_at == 0 && p->t1_exp <= now)
        {
            p->t1_exp = 0;
            p->t1_paused_at = 0;
            p->t1_had_expired = 1;
            t1_expiry(p);
        }
    }

    for (p = list_head; p != NULL; p = p->next)
    {
        if (p->t3_exp != 0 && p->t3_exp <= now)
        {
            p->t3_exp = 0;
            t3_expiry(p);
        }
    }
}

static void t1_expiry(ax25_dlsm_t *S)
{

    switch (S->state)
    {

    case state_0_disconnected:

        // Ignore it.
        break;

    case state_1_awaiting_connection:

        if (S->rc == S->n2_retry)
        {
            discard_i_queue(S);

            fprintf(stderr, "Failed to connect to %s after %d tries.\n", S->addrs[PEERCALL], S->n2_retry);
            enter_new_state(S, state_0_disconnected);
        }
        else
        {
            cmdres_t cmd = cr_cmd;
            int p = 1;
            int nopid = 0;

            packet_t pp;

            S->rc = (S->rc + 1);

            if (S->rc > S->peak_rc_value)
                S->peak_rc_value = S->rc; // Keep statistics.

            pp = ax25_u_frame(S->addrs, cmd, frame_type_U_SABM, p, nopid, NULL, 0);
            lm_data_request(TQ_PRIO_1_LO, pp);
            select_t1_value(S);
            START_T1;
            // Keep same state.
        }
        break;

    case state_2_awaiting_release:

        if (S->rc == S->n2_retry)
        {
            fprintf(stderr, "Stream %d: Disconnected from %s.\n", S->stream_id, S->addrs[PEERCALL]);
            enter_new_state(S, state_0_disconnected);
        }
        else
        {
            cmdres_t cmd = cr_cmd;
            int p = 1;
            int nopid = 0;

            packet_t pp;

            S->rc = (S->rc + 1);

            if (S->rc > S->peak_rc_value)
                S->peak_rc_value = S->rc;

            pp = ax25_u_frame(S->addrs, cmd, frame_type_U_DISC, p, nopid, NULL, 0);
            lm_data_request(TQ_PRIO_1_LO, pp);
            select_t1_value(S);
            START_T1;
            // stay in same state
        }
        break;

    case state_3_connected:

        S->rc = 1;
        transmit_enquiry(S);
        enter_new_state(S, state_4_timer_recovery);
        break;

    case state_4_timer_recovery:

        if (S->rc == S->n2_retry)
        {
            fprintf(stderr, "Stream %d: Disconnected from %s due to timeouts.\n", S->stream_id, S->addrs[PEERCALL]);

            discard_i_queue(S);

            cmdres_t cr = cr_res; // DM can only be response.
            int f = 0;            // Erratum: Assuming F=0 because it is not response to P=1
            int nopid = 0;

            packet_t pp = ax25_u_frame(S->addrs, cr, frame_type_U_DM, f, nopid, NULL, 0);
            lm_data_request(TQ_PRIO_1_LO, pp);

            enter_new_state(S, state_0_disconnected);
        }
        else
        {
            S->rc = (S->rc + 1);

            if (S->rc > S->peak_rc_value)
                S->peak_rc_value = S->rc; // gather statistics.

            transmit_enquiry(S);
            // Keep same state.
        }
        break;
    }
}

static void t3_expiry(ax25_dlsm_t *S)
{
    switch (S->state)
    {

    case state_0_disconnected:
    case state_1_awaiting_connection:
    case state_2_awaiting_release:
    case state_4_timer_recovery:

        break;

    case state_3_connected:
        S->rc = 1;
        transmit_enquiry(S);
        enter_new_state(S, state_4_timer_recovery);
        break;
    }
}

static void nr_error_recovery(ax25_dlsm_t *S)
{
    establish_data_link(S);
    S->layer_3_initiated = 0;
}

static void establish_data_link(ax25_dlsm_t *S)
{
    cmdres_t cmd = cr_cmd;
    int p = 1;
    packet_t pp;
    int nopid = 0;

    clear_exception_conditions(S);

    S->rc = 1;
    pp = ax25_u_frame(S->addrs, cmd, frame_type_U_SABM, p, nopid, NULL, 0);
    lm_data_request(TQ_PRIO_1_LO, pp);
    STOP_T3;
    START_T1;
}

static void clear_exception_conditions(ax25_dlsm_t *S)
{
    S->peer_receiver_busy = 0;
    S->reject_exception = 0;
    S->own_receiver_busy = 0;
    S->acknowledge_pending = 0;

    for (int n = 0; n < 128; n++)
    {
        if (S->rxdata_by_ns[n] != NULL)
        {
            cdata_delete(S->rxdata_by_ns[n]);
            S->rxdata_by_ns[n] = NULL;
        }
    }
}

static void transmit_enquiry(ax25_dlsm_t *S)
{
    int p = 1;
    int nr = S->vr;
    cmdres_t cmd = cr_cmd;

    packet_t pp = ax25_s_frame(S->addrs, cmd, S->own_receiver_busy ? frame_type_S_RNR : frame_type_S_RR, nr, p, NULL, 0);

    lm_data_request(TQ_PRIO_1_LO, pp);

    S->acknowledge_pending = 0;
    START_T1;
}

static void enquiry_response(ax25_dlsm_t *S, ax25_frame_type_t frame_type, int f)
{
    cmdres_t cr = cr_res; // Response, not command as seen in flow chart.
    int nr = S->vr;
    packet_t pp;

    if (f == 1 && (frame_type == frame_type_S_RR || frame_type == frame_type_S_RNR || frame_type == frame_type_I))
    {

        if (S->own_receiver_busy)
        {

            // I'm busy.

            pp = ax25_s_frame(S->addrs, cr, frame_type_S_RNR, nr, f, NULL, 0);
            lm_data_request(TQ_PRIO_1_LO, pp);

            S->acknowledge_pending = 0; // because we sent N(R) from V(R).
        }
        else
        {
            pp = ax25_s_frame(S->addrs, cr, frame_type_S_RR, nr, f, NULL, 0);
            lm_data_request(TQ_PRIO_1_LO, pp);

            S->acknowledge_pending = 0;
        }
    }
    else
    {

        // For cases other than (RR, RNR, I) command, P=1.

        pp = ax25_s_frame(S->addrs, cr, S->own_receiver_busy ? frame_type_S_RNR : frame_type_S_RR, nr, f, NULL, 0);
        lm_data_request(TQ_PRIO_1_LO, pp);

        S->acknowledge_pending = 0;
    }
}

static void invoke_retransmission(ax25_dlsm_t *S, int nr_input)
{
    int local_vs;
    int sent_count = 0;

    if (S->txdata_by_ns[nr_input] == NULL)
    {
        fprintf(stderr, "Internal Error, Can't resend starting with N(S) = %d.  It is not available\n", nr_input);
        return;
    }

    local_vs = nr_input;

    do
    {

        if (S->txdata_by_ns[local_vs] != NULL)
        {

            cmdres_t cr = cr_cmd;
            int ns = local_vs;
            int nr = S->vr;
            int p = 0;

            packet_t pp = ax25_i_frame(S->addrs, cr, nr, ns, p,
                                       S->txdata_by_ns[ns]->pid, (unsigned char *)(S->txdata_by_ns[ns]->data), S->txdata_by_ns[ns]->len);

            lm_data_request(TQ_PRIO_1_LO, pp);
            // Keep it around in case we need to send again.

            sent_count++;
        }
        else
        {
            fprintf(stderr, "Internal Error, state=%d, need to retransmit N(S) = %d for REJ but it is not available\n", S->state, local_vs);
        }
        local_vs = AX25MODULO(local_vs + 1);

    } while (local_vs != S->vs);

    if (sent_count == 0)
    {
        fprintf(stderr, "Internal Error, Nothing to retransmit. N(R)=%d\n", nr_input);
    }
}

static void check_i_frame_ackd(ax25_dlsm_t *S, int nr)
{
    if (S->peer_receiver_busy)
    {
        SET_VA(nr);
        START_T3;

        if (!IS_T1_RUNNING)
        {
            START_T1;
        }
    }
    else if (nr == S->vs)
    {
        SET_VA(nr);
        STOP_T1;
        START_T3;
        select_t1_value(S);
    }
    else if (nr != S->va)
    {
        SET_VA(nr);
        START_T1;
    }
}

static void check_need_for_response(ax25_dlsm_t *S, ax25_frame_type_t frame_type, cmdres_t cr, int pf)
{
    if (cr == cr_cmd && pf == 1)
    {
        int f = 1;
        enquiry_response(S, frame_type, f);
    }
}

static void select_t1_value(ax25_dlsm_t *S)
{
    float old_srt = S->srt;

    if (S->rc == 0)
    {

        if (S->t1_remaining_when_last_stopped >= 0)
        { // Negative means invalid, don't use it.
            S->srt = 7. / 8. * S->srt + 1. / 8. * (S->t1v - S->t1_remaining_when_last_stopped);
        }

        if (S->srt < 1)
        {
            S->srt = 1;
        }

        S->t1v = S->srt * 2;
    }
    else
    {

        if (S->t1_had_expired)
        {
            S->t1v = S->rc * 0.25 + S->srt * 2;
        }
    }

    if (S->t1v < 0.99 || S->t1v > 30)
    {
        fprintf(stderr, "INTERNAL ERROR?  Stream %d: select_t1_value, rc = %d, t1 remaining = %.3f, old srt = %.3f, new srt = %.3f, Extreme new t1v = %.3f\n",
                S->stream_id, S->rc, S->t1_remaining_when_last_stopped, old_srt, S->srt, S->t1v);
    }
}

static void set_version_2_0(ax25_dlsm_t *S)
{
    S->n1_paclen = g_misc_config_p->paclen;
    S->k_maxframe = g_misc_config_p->maxframe;
    S->n2_retry = g_misc_config_p->retry;
}

static int is_good_nr(ax25_dlsm_t *S, int nr)
{
    int adjusted_va, adjusted_nr, adjusted_vs;
    int result;

    /* Adjust all values relative to V(a) before comparing so we won't have wrap around. */

#define adjust_by_va(x) (AX25MODULO((x)-S->va))

    adjusted_va = adjust_by_va(S->va); // A clever compiler would know it is zero.
    adjusted_nr = adjust_by_va(nr);
    adjusted_vs = adjust_by_va(S->vs);

    result = adjusted_va <= adjusted_nr && adjusted_nr <= adjusted_vs;

    return result;
}

static void enter_new_state(ax25_dlsm_t *S, enum dlsm_state_e new_state)
{
    if ((new_state == state_3_connected || new_state == state_4_timer_recovery) &&
        S->state != state_3_connected && S->state != state_4_timer_recovery)
    {

        ptt_set(OCTYPE_CON, 1); // Turn on connected indicator if configured.
    }
    else if ((new_state != state_3_connected && new_state != state_4_timer_recovery) &&
             (S->state == state_3_connected || S->state == state_4_timer_recovery))
    {

        ptt_set(OCTYPE_CON, 0);
    }

    S->state = new_state;
}

static void start_t1(ax25_dlsm_t *S)
{
    double now = dtime_now();

    S->t1_exp = now + S->t1v;

    if (S->radio_channel_busy)
    {
        S->t1_paused_at = now;
    }
    else
    {
        S->t1_paused_at = 0;
    }

    S->t1_had_expired = 0;
}

static void stop_t1(ax25_dlsm_t *S)
{
    double now = dtime_now();

    RESUME_T1; // adjust expire time if paused.

    if (S->t1_exp == 0.0)
    {
        // Was already stopped.
    }
    else
    {
        S->t1_remaining_when_last_stopped = S->t1_exp - now;

        if (S->t1_remaining_when_last_stopped < 0)
            S->t1_remaining_when_last_stopped = 0;
    }

    // Normally this would be at the top but we don't know time remaining at that point.

    S->t1_exp = 0.0;       // now stopped.
    S->t1_had_expired = 0; // remember that it did not expire.
}

static int is_t1_running(ax25_dlsm_t *S)
{
    int result = S->t1_exp != 0.0;

    return result;
}

static void pause_t1(ax25_dlsm_t *S)
{
    if (S->t1_paused_at == 0.0)
    {
        double now = dtime_now();

        S->t1_paused_at = now;
    }
}

static void resume_t1(ax25_dlsm_t *S)
{
    if (S->t1_exp == 0.0)
    {
        // Stopped so there is nothing to do.
    }
    else if (S->t1_paused_at == 0.0)
    {
        // Running but not paused.
    }
    else
    {
        double now = dtime_now();
        double paused_for_sec = now - S->t1_paused_at;

        S->t1_exp += paused_for_sec;
        S->t1_paused_at = 0.0;
    }
}

static void start_t3(ax25_dlsm_t *S)
{
    double now = dtime_now();

    S->t3_exp = now + T3_DEFAULT;
}

static void stop_t3(ax25_dlsm_t *S)
{
    S->t3_exp = 0.0;
}

double ax25_link_get_next_timer_expiry()
{
    double tnext = 0.0;
    ax25_dlsm_t *p;

    for (p = list_head; p != NULL; p = p->next)
    {

        // Consider if running and not paused.

        if (p->t1_exp != 0.0 && p->t1_paused_at == 0.0)
        {
            if (tnext == 0.0)
            {
                tnext = p->t1_exp;
            }
            else if (p->t1_exp < tnext)
            {
                tnext = p->t1_exp;
            }
        }

        if (p->t3_exp != 0.0)
        {
            if (tnext == 0.0)
            {
                tnext = p->t3_exp;
            }
            else if (p->t3_exp < tnext)
            {
                tnext = p->t3_exp;
            }
        }
    }

    return tnext;
}
