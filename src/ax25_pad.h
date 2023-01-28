/*
 * ax25_pad.h
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

#define AX25_DESTINATION 0
#define AX25_SOURCE 1
#define AX25_ADDRS 2

#define AX25_MAX_ADDR_LEN 12
#define AX25_MIN_INFO_LEN 0
#define AX25_MAX_INFO_LEN 2048

#define AX25_MIN_PACKET_LEN (2 * 2 + 1)
#define AX25_MAX_PACKET_LEN (AX25_ADDRS * 2 + 2 + 3 + AX25_MAX_INFO_LEN)

#define AX25_UI_FRAME 3
#define AX25_PID_NO_LAYER_3 0xf0
#define AX25_PID_SEGMENTATION_FRAGMENT 0x08
#define AX25_PID_ESCAPE_CHARACTER 0xff

#define SSID_RR_MASK 0x60
#define SSID_RR_SHIFT 5

#define SSID_SSID_MASK 0x1e
#define SSID_SSID_SHIFT 1

#define SSID_LAST_MASK 0x01

    typedef struct packet_s
    {
        struct packet_s *nextp;
        int seq;
        int frame_len;
        int modulo;
        double release_time;
        unsigned char frame_data[AX25_MAX_PACKET_LEN + 1];
    } *packet_t;

    typedef enum cmdres_e
    {
        cr_res = 0,
        cr_cmd = 1,
        cr_00 = 2,
        cr_11 = 3
    } cmdres_t;

    typedef enum ax25_frame_type_e
    {
        frame_type_I = 0,
        frame_type_S_RR,
        frame_type_S_RNR,
        frame_type_S_REJ,
        frame_type_S_SREJ,
        frame_type_U_SABME,
        frame_type_U_SABM,
        frame_type_U_DISC,
        frame_type_U_DM,
        frame_type_U_UA,
        frame_type_U_FRMR,
        frame_type_U_UI,
        frame_type_U_XID,
        frame_type_U_TEST,
        frame_type_U,
        frame_not_AX25
    } ax25_frame_type_t;

    packet_t ax25_new(void);
    packet_t ax25_from_frame(unsigned char *data, int len);
    void ax25_delete(packet_t pp);
    int ax25_parse_addr(int position, char *in_addr, char *out_addr, int *out_ssid);
    void ax25_get_addr_with_ssid(packet_t pp, int n, char *station);
    void ax25_get_addr_no_ssid(packet_t pp, int n, char *station);
    int ax25_get_ssid(packet_t pp, int n);
    int ax25_get_info(packet_t pp, unsigned char **paddr);
    void ax25_set_info(packet_t pp, unsigned char *info_ptr, int info_len);
    void ax25_set_nextp(packet_t this_p, packet_t next_p);
    packet_t ax25_get_nextp(packet_t this_p);
    int ax25_pack(packet_t pp, unsigned char result[AX25_MAX_PACKET_LEN]);
    ax25_frame_type_t ax25_frame_type(packet_t this_p, cmdres_t *cr, int *pf, int *nr, int *ns);
    int ax25_is_null_frame(packet_t this_p);
    int ax25_get_control(packet_t this_p);
    int ax25_get_control_offset(void);
    int ax25_get_pid(packet_t this_p);
    int ax25_get_frame_len(packet_t this_p);
    unsigned char *ax25_get_frame_data_ptr(packet_t this_p);

    packet_t ax25_u_frame(char addrs[][AX25_MAX_ADDR_LEN], cmdres_t cr, ax25_frame_type_t ftype, int pf, int pid, unsigned char *pinfo, int info_len);
    packet_t ax25_s_frame(char addrs[][AX25_MAX_ADDR_LEN], cmdres_t cr, ax25_frame_type_t ftype, int nr, int pf, unsigned char *pinfo, int info_len);
    packet_t ax25_i_frame(char addrs[][AX25_MAX_ADDR_LEN], cmdres_t cr, int nr, int ns, int pf, int pid, unsigned char *pinfo, int info_len);

#ifdef __cplusplus
}
#endif