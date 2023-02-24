/*
 * ax25_pad.c
 *
 * IP Node Project
 *
 * Based on the Dire Wolf program
 * Copyright (C) 2011-2021 John Langner
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <bsd/bsd.h>

#include "ipnode.h"
#include "ax25_pad.h"
#include "crc16.h"

#define CLEAR_LAST_ADDR_FLAG this_p->frame_data[2 * 7 - 1] &= ~SSID_LAST_MASK
#define SET_LAST_ADDR_FLAG this_p->frame_data[2 * 7 - 1] |= SSID_LAST_MASK

static int set_addrs(packet_t, char[][AX25_MAX_ADDR_LEN], cmdres_t);

static volatile int new_count = 0;
static volatile int delete_count = 0;
static volatile int last_seq_num = 0;

packet_t ax25_new()
{
    last_seq_num++;
    new_count++;

    if (new_count > delete_count + 256)
    {
        fprintf(stderr, "Error: Memory leak new=%d, delete=%d\n", new_count, delete_count);
    }

    struct packet_s *this_p = (struct packet_s *)calloc(1, sizeof(struct packet_s));

    if (this_p == NULL)
    {
        fprintf(stderr, "ERROR - can't allocate memory in ax25_new.\n");
    }

    this_p->seq = last_seq_num;

    return this_p;
}

void ax25_delete(packet_t this_p)
{
    if (this_p == NULL)
    {
        fprintf(stderr, "ERROR - NULL pointer passed to ax25_delete.\n");
        return;
    }

    delete_count++;

    free(this_p);
}

packet_t ax25_from_frame(unsigned char *fbuf, int flen)
{
    if (flen < AX25_MIN_PACKET_LEN || flen > AX25_MAX_PACKET_LEN)
    {
        fprintf(stderr, "Frame length %d not in allowable range of %d to %d.\n", flen, AX25_MIN_PACKET_LEN, AX25_MAX_PACKET_LEN);
        return NULL;
    }

    packet_t this_p = ax25_new();

    /* Copy the whole thing intact. */

    memcpy(this_p->frame_data, fbuf, flen);
    this_p->frame_data[flen] = 0;
    this_p->frame_len = flen;

    return this_p;
}

static const char *position_name[1 + AX25_ADDRS] = {"Destination", "Source"};

int ax25_parse_addr(int position, char *in_addr, char *out_addr, int *out_ssid)
{
    char sstr[8];

    *out_addr = '\0';
    *out_ssid = 0;

    if (strlen(in_addr) == 0)
    {
        fprintf(stderr, "%sAddress \"%s\" is empty.\n", position_name[position], in_addr);
        return 0;
    }

    char *p = in_addr;
    int i = 0;

    for (p = in_addr; *p != '\0' && *p != '-' && *p != '*'; p++)
    {
        if (i >= 6)
        {
            fprintf(stderr, "%sAddress is too long. \"%s\" has more than 6 characters.\n", position_name[position], in_addr);
            return 0;
        }

        if (!isalnum(*p))
        {
            fprintf(stderr, "%sAddress, \"%s\" contains character other than letter or digit in character position %d.\n", position_name[position], in_addr, (int)(long)(p - in_addr) + 1);
            return 0;
        }

        out_addr[i++] = *p;
        out_addr[i] = '\0';

        if (islower(*p))
        {
            fprintf(stderr, "%sAddress has lower case letters. \"%s\" must be all upper case.\n", position_name[position], in_addr);
            return 0;
        }
    }

    int j = 0;
    sstr[j] = '\0';

    if (*p == '-')
    {
        for (p++; isalnum(*p); p++)
        {
            if (j >= 2)
            {
                fprintf(stderr, "%sSSID is too long. SSID part of \"%s\" has more than 2 characters.\n", position_name[position], in_addr);
                return 0;
            }

            sstr[j++] = *p;
            sstr[j] = '\0';

            if (!isdigit(*p))
            {
                fprintf(stderr, "%sSSID must be digits. \"%s\" has letters in SSID.\n", position_name[position], in_addr);
                return 0;
            }
        }

        int k = atoi(sstr);

        if (k < 0 || k > 15)
        {
            fprintf(stderr, "%sSSID out of range. SSID of \"%s\" not in range of 0 to 15.\n", position_name[position], in_addr);
            return 0;
        }

        *out_ssid = k;
    }

    if (*p != '\0')
    {
        fprintf(stderr, "Invalid character \"%c\" found in %saddress \"%s\".\n", *p, position_name[position], in_addr);
        return 0;
    }

    return 1;
}

void ax25_get_addr_with_ssid(packet_t this_p, int n, char *station)
{
    char sstr[8];

    n &= 0x01;

    for (int i = 0; i < 6; i++)
    {
        station[i] = (this_p->frame_data[n * 7 + i] >> 1) & 0x7f;
    }

    station[6] = '\0';

    for (int i = 5; i >= 0; i--)
    {
        if (station[i] == '\0')
        {
            fprintf(stderr, "Station address \"%s\" contains nul character.  AX.25 protocol requires trailing ASCII spaces when less than 6 characters.\n", station);
        }
        else if (station[i] == ' ')
            station[i] = '\0';
        else
            break;
    }

    int ssid = ax25_get_ssid(this_p, n);

    if (ssid != 0)
    {
        snprintf(sstr, sizeof(sstr), "-%d", ssid);
        strlcat(station, sstr, 10);
    }
}

void ax25_get_addr_no_ssid(packet_t this_p, int n, char *station)
{
    for (int i = 0; i < 6; i++)
    {
        station[i] = (this_p->frame_data[n * 7 + i] >> 1) & 0x7f;
    }

    station[6] = '\0';

    for (int i = 5; i >= 0; i--)
    {
        if (station[i] == ' ')
            station[i] = '\0';
        else
            break;
    }
}

int ax25_get_ssid(packet_t this_p, int n)
{
    return ((this_p->frame_data[n * 7 + 6] & SSID_SSID_MASK) >> SSID_SSID_SHIFT);
}

int ax25_get_control_offset()
{
    return 14;
}

int ax25_get_pid_offset()
{
    return (ax25_get_control_offset() + 1);
}

int ax25_get_num_pid(packet_t this_p)
{
    int c = this_p->frame_data[ax25_get_control_offset()];

    if ((c & 0x01) == 0 || c == 0x03 || c == 0x13)
    {
        int pid = this_p->frame_data[ax25_get_pid_offset(this_p)];

        if (pid == AX25_PID_ESCAPE_CHARACTER)
        {
            return 2; /* pid 1111 1111 means another follows. */
        }

        return 1;
    }

    return 0;
}

int ax25_get_info_offset(packet_t this_p)
{
    return ax25_get_control_offset() + 1 + ax25_get_num_pid(this_p);
}

int ax25_get_num_info(packet_t this_p)
{
    int len = this_p->frame_len - 14 - 1 - ax25_get_num_pid(this_p);

    return (len < 0) ? 0 : len;
}

int ax25_get_info(packet_t this_p, unsigned char **paddr)
{
    unsigned char *info_ptr = this_p->frame_data + ax25_get_info_offset(this_p);
    int info_len = ax25_get_num_info(this_p);

    info_ptr[info_len] = '\0';

    *paddr = info_ptr;

    return info_len;
}

void ax25_set_info(packet_t this_p, unsigned char *new_info_ptr, int new_info_len)
{
    unsigned char *old_info_ptr;

    int old_info_len = ax25_get_info(this_p, &old_info_ptr);

    this_p->frame_len -= old_info_len;

    if (new_info_len < 0)
        new_info_len = 0;

    if (new_info_len > AX25_MAX_INFO_LEN)
        new_info_len = AX25_MAX_INFO_LEN;

    memcpy(old_info_ptr, new_info_ptr, new_info_len);

    this_p->frame_len += new_info_len;
}

void ax25_set_nextp(packet_t this_p, packet_t next_p)
{
    this_p->nextp = next_p;
}

packet_t ax25_get_nextp(packet_t this_p)
{
    return this_p->nextp;
}

int ax25_pack(packet_t this_p, unsigned char result[AX25_MAX_PACKET_LEN])
{

    memcpy(result, this_p->frame_data, this_p->frame_len);

    return this_p->frame_len;
}

ax25_frame_type_t ax25_frame_type(packet_t this_p, cmdres_t *cr, int *pf, int *nr, int *ns)
{
    *cr = cr_11;
    *pf = -1;
    *nr = -1;
    *ns = -1;

    int c = ax25_get_control(this_p);

    if (c < 0)
    {
        return frame_not_AX25;
    }

    int dst_c = this_p->frame_data[AX25_DESTINATION];
    int src_c = this_p->frame_data[AX25_SOURCE];

    if (dst_c)
    {
        if (src_c)
        {
            *cr = cr_11;
        }
        else
        {
            *cr = cr_cmd;
        }
    }
    else
    {
        if (src_c)
        {
            *cr = cr_res;
        }
        else
        {
            *cr = cr_00;
        }
    }

    if ((c & 1) == 0)
    {
        *ns = (c >> 1) & 7;
        *pf = (c >> 4) & 1;
        *nr = (c >> 5) & 7;

        return frame_type_I;
    }
    else if ((c & 2) == 0)
    {
        *pf = (c >> 4) & 1;
        *nr = (c >> 5) & 7;

        switch ((c >> 2) & 3)
        {
        case 0:
            return (frame_type_S_RR);
            break;
        case 1:
            return (frame_type_S_RNR);
            break;
        case 2:
            return (frame_type_S_REJ);
            break;
        case 3:
            return (frame_type_S_SREJ);
            break;
        }
    }
    else
    {
        *pf = (c >> 4) & 1;

        switch (c & 0xef)
        {
        case 0x2f:
            return (frame_type_U_SABM);
        case 0x43:
            return (frame_type_U_DISC);
        case 0x0f:
            return (frame_type_U_DM);
        case 0x63:
            return (frame_type_U_UA);
        case 0x87:
            return (frame_type_U_FRMR);
        case 0x03:
            return (frame_type_U_UI);
        default:
            return (frame_type_U);
        }
    }

    return frame_not_AX25;
}

int ax25_is_null_frame(packet_t this_p)
{
    return this_p->frame_len == 0;
}

int ax25_get_control(packet_t this_p)
{
    if (this_p->frame_len == 0)
        return -1;

    return this_p->frame_data[ax25_get_control_offset()];
}

int ax25_get_pid(packet_t this_p)
{
    if (this_p->frame_len == 0)
        return -1;

    return this_p->frame_data[ax25_get_pid_offset(this_p)];
}

int ax25_get_frame_len(packet_t this_p)
{
    return this_p->frame_len;
}

unsigned char *ax25_get_frame_data_ptr(packet_t this_p)
{
    return this_p->frame_data;
}

packet_t ax25_u_frame(char addrs[][AX25_MAX_ADDR_LEN], cmdres_t cr, ax25_frame_type_t ftype, int pf, int pid, unsigned char *pinfo, int info_len)
{
    int ctrl = 0;
    unsigned int t = 999; // 1 = must be cmd, 0 = must be response, 2 = can be either.
    int i = 0;            // Is Info part allowed?

    packet_t this_p = ax25_new();

    if (this_p == NULL)
        return NULL;

    this_p->modulo = 0;

    if (!set_addrs(this_p, addrs, cr))
    {
        fprintf(stderr, "Internal error in %s: Could not set addresses for U frame.\n", __func__);
        ax25_delete(this_p);
        return NULL;
    }

    switch (ftype)
    {
    case frame_type_U_SABM:
        ctrl = 0x2f;
        t = 1;
        break;
    case frame_type_U_DISC:
        ctrl = 0x43;
        t = 1;
        break;
    case frame_type_U_DM:
        ctrl = 0x0f;
        t = 0;
        break;
    case frame_type_U_UA:
        ctrl = 0x63;
        t = 0;
        break;
    case frame_type_U_FRMR:
        ctrl = 0x87;
        t = 0;
        i = 1;
        break;
    case frame_type_U_UI:
        ctrl = 0x03;
        t = 2;
        i = 1;
        break;

    default:
        fprintf(stderr, "Internal error in %s: Invalid ftype %d for U frame.\n", __func__, ftype);
        ax25_delete(this_p);
        return NULL;
        break;
    }

    if (pf)
        ctrl |= 0x10;

    if (t != 2)
    {
        if (cr != t)
        {
            fprintf(stderr, "Internal error in %s: U frame, cr is %d but must be %d. ftype=%d\n", __func__, cr, t, ftype);
        }
    }

    unsigned char *p = this_p->frame_data + this_p->frame_len;
    *p++ = ctrl;

    this_p->frame_len++;

    if (ftype == frame_type_U_UI)
    {
        if (pid < 0 || pid == 0 || pid == 0xff)
        {
            fprintf(stderr, "Internal error in %s: U frame, Invalid pid value 0x%02x.\n", __func__, pid);
            pid = AX25_PID_NO_LAYER_3;
        }

        *p++ = pid;
        this_p->frame_len++;
    }

    if (i)
    {
        if (pinfo != NULL && info_len > 0)
        {
            if (info_len > AX25_MAX_INFO_LEN)
            {
                fprintf(stderr, "Internal error in %s: U frame, Invalid information field length %d.\n", __func__, info_len);
                info_len = AX25_MAX_INFO_LEN;
            }

            memcpy(p, pinfo, info_len);
            p += info_len;
            this_p->frame_len += info_len;
        }
    }
    else
    {
        if (pinfo != NULL && info_len > 0)
        {
            fprintf(stderr, "Internal error in %s: Info part not allowed for U frame type.\n", __func__);
        }
    }

    *p = '\0';

    return this_p;
}

packet_t ax25_s_frame(char addrs[][AX25_MAX_ADDR_LEN], cmdres_t cr, ax25_frame_type_t ftype, int nr, int pf, unsigned char *pinfo, int info_len)
{
    packet_t this_p;
    unsigned char *p;
    int ctrl = 0;

    this_p = ax25_new();

    if (this_p == NULL)
        return (NULL);

    if (!set_addrs(this_p, addrs, cr))
    {
        fprintf(stderr, "Internal error in %s: Could not set addresses for S frame.\n", __func__);
        ax25_delete(this_p);
        return (NULL);
    }

    if (nr < 0 || nr >= 8)
    {
        fprintf(stderr, "Internal error in %s: Invalid N(R) %d for S frame.\n", __func__, nr);
        nr &= 7;
    }

    // Erratum: The AX.25 spec is not clear about whether SREJ should be command, response, or both.
    // The underlying X.25 spec clearly says it is response only.  Let's go with that.

    if (ftype == frame_type_S_SREJ && cr != cr_res)
    {
        fprintf(stderr, "Internal error in %s: SREJ must be response.\n", __func__);
    }

    switch (ftype)
    {
    case frame_type_S_RR:
        ctrl = 0x01;
        break;
    case frame_type_S_RNR:
        ctrl = 0x05;
        break;
    case frame_type_S_REJ:
        ctrl = 0x09;
        break;
    case frame_type_S_SREJ:
        ctrl = 0x0d;
        break;
    default:
        fprintf(stderr, "Internal error in %s: Invalid ftype %d for S frame.\n", __func__, ftype);
        ax25_delete(this_p);
        return (NULL);
        break;
    }

    p = this_p->frame_data + this_p->frame_len;

    if (pf)
        ctrl |= 0x10;

    ctrl |= nr << 5;
    *p++ = ctrl;
    this_p->frame_len++;

    if (ftype == frame_type_S_SREJ)
    {
        if (pinfo != NULL && info_len > 0)
        {
            if (info_len > AX25_MAX_INFO_LEN)
            {
                fprintf(stderr, "Internal error in %s: SREJ frame, Invalid information field length %d.\n", __func__, info_len);
                info_len = AX25_MAX_INFO_LEN;
            }

            memcpy(p, pinfo, info_len);
            p += info_len;
            this_p->frame_len += info_len;
        }
    }
    else
    {
        if (pinfo != NULL || info_len != 0)
        {
            fprintf(stderr, "Internal error in %s: Info part not allowed for RR, RNR, REJ frame.\n", __func__);
        }
    }

    *p = '\0';

    return this_p;
}

packet_t ax25_i_frame(char addrs[][AX25_MAX_ADDR_LEN], cmdres_t cr, int nr, int ns, int pf, int pid, unsigned char *pinfo, int info_len)
{
    packet_t this_p;
    unsigned char *p;
    int ctrl = 0;

    this_p = ax25_new();

    if (this_p == NULL)
        return (NULL);

    if (!set_addrs(this_p, addrs, cr))
    {
        fprintf(stderr, "Internal error in %s: Could not set addresses for I frame.\n", __func__);
        ax25_delete(this_p);
        return (NULL);
    }

    if (nr < 0 || nr >= 8)
    {
        fprintf(stderr, "Internal error in %s: Invalid N(R) %d for I frame.\n", __func__, nr);
        nr &= 7;
    }

    if (ns < 0 || ns >= 8)
    {
        fprintf(stderr, "Internal error in %s: Invalid N(S) %d for I frame.\n", __func__, ns);
        ns &= 7;
    }

    p = this_p->frame_data + this_p->frame_len;

    ctrl = (nr << 5) | (ns << 1);

    if (pf)
        ctrl |= 0x10;

    *p++ = ctrl;
    this_p->frame_len++;

    if (pid < 0 || pid == 0 || pid == 0xff)
    {
        fprintf(stderr, "Warning: Client application provided invalid PID value, %d, for I frame.\n", pid);
        pid = AX25_PID_NO_LAYER_3;
    }

    *p++ = pid;
    this_p->frame_len++;

    if (pinfo != NULL && info_len > 0)
    {
        if (info_len > AX25_MAX_INFO_LEN)
        {
            fprintf(stderr, "Internal error in %s: I frame, Invalid information field length %d.\n", __func__, info_len);
            info_len = AX25_MAX_INFO_LEN;
        }
        memcpy(p, pinfo, info_len);
        p += info_len;
        this_p->frame_len += info_len;
    }

    *p = '\0';

    return (this_p);
}

static int set_addrs(packet_t pp, char addrs[][AX25_MAX_ADDR_LEN], cmdres_t cr)
{
    for (int n = 0; n < 2; n++)
    {
        unsigned char *pa = pp->frame_data + n * 7;
        char oaddr[AX25_MAX_ADDR_LEN];
        int ssid;

        int ok = ax25_parse_addr(n, addrs[n], oaddr, &ssid);

        if (!ok)
            return 0;

        // Fill in address.

        memset(pa, ' ' << 1, 6);

        for (int j = 0; oaddr[j]; j++)
        {
            pa[j] = oaddr[j] << 1;
        }

        pa += 6;

        *pa = 0x60 | ((ssid & 0xf) << 1);

        switch (n)
        {
        case AX25_DESTINATION:
            if (cr == cr_cmd)
                *pa |= 0x80;
            break;
        case AX25_SOURCE:
            if (cr == cr_res)
                *pa |= 0x80; // fall through
        default:
            break;
        }

        if (n == 1)
        {
            *pa |= 1;
        }

        pp->frame_len += 7;
    }

    return 1;
}
