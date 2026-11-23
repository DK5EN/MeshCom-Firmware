// Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/*
 * MESHCOM-PATCH: vendored copy of ESP-IDF v4.4.7 components/lwip/apps/dhcpserver/dhcpserver.c
 * (arduino-esp32 2.0.17 / framework package 3.20017.*), upstream license above unchanged.
 * It replaces the dhcpserver.c.obj member of the prebuilt liblwip.a: this object defines all 11
 * symbols that member exports, so the linker never pulls the archive member. Every change is marked
 * "MESHCOM-PATCH DS-0x"; see README-dhcpserver.md for the patch list and the re-sync procedure.
 * Only for the Arduino ESP32 (IDF 4.x) builds; safeboot (IDF 5), native and nRF52 do not compile it.
 */
/* The Arduino builder does not define ESP_IDF_VERSION_MAJOR on the command line: without this include both guards
 * below were false and the file compiled to an empty object, so the stock liblwip member was linked (advisor
 * 2026-11-23). Same pattern as mdns.c. */
#if defined(ESP_PLATFORM) && !defined(NATIVE_BUILD)
#include "esp_idf_version.h"
#endif
#if (defined(ESP32) && !defined(NATIVE_BUILD) && defined(ESP_IDF_VERSION_MAJOR) && ESP_IDF_VERSION_MAJOR == 4) || \
    defined(MESHCOM_DHCPS_HOST_TEST)

#include <stdint.h>
#include <stdbool.h>

/* MESHCOM-PATCH DS-02 (IDF #2, GHSA-g764-gwc3-75m5): bounds-checked DHCP option walker.
 * Pure function (no lwIP types) so a host test can include this file with
 * -DMESHCOM_DHCPS_HOST_TEST and exercise it alone.
 *   opts  : first option byte, avail : number of option bytes that are really in the packet
 *   pos   : offset of the option to read
 * Returns 1 and fills code/vlen/next when a complete option (code, length byte, vlen value bytes)
 * lies entirely inside [0, avail); pad (0) and end (255) are single bytes with vlen 0 (RFC 2132).
 * Returns 0 when pos is at/after the end or the option is truncated: the caller must stop. The
 * value of an option starts at opts + pos + 2. */
static int dhcps_opt_step(const uint8_t *opts, uint16_t avail, uint16_t pos,
                          uint8_t *code, uint8_t *vlen, uint16_t *next)
{
    if (pos >= avail) {
        return 0;
    }
    uint8_t c = opts[pos];
    if (c == 0 || c == 255) {
        *code = c;
        *vlen = 0;
        *next = (uint16_t)(pos + 1);
        return 1;
    }
    if ((uint32_t)pos + 2u > avail) {
        return 0;                       /* length byte missing */
    }
    uint8_t l = opts[pos + 1];
    if ((uint32_t)pos + 2u + l > avail) {
        return 0;                       /* value runs past the packet */
    }
    *code = c;
    *vlen = l;
    *next = (uint16_t)(pos + 2u + l);
    return 1;
}

#endif /* ESP32 IDF4 || host test */

#if defined(ESP32) && !defined(NATIVE_BUILD) && defined(ESP_IDF_VERSION_MAJOR) && ESP_IDF_VERSION_MAJOR == 4

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <strings.h>
#include <assert.h>
#include "lwip/inet.h"
#include "lwip/err.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"
#include "lwip/mem.h"
#include "lwip/ip_addr.h"
#include "esp_netif.h"

#include "dhcpserver/dhcpserver.h"
#include "dhcpserver/dhcpserver_options.h"

#if ESP_DHCP

#define BOOTP_BROADCAST 0x8000

#define DHCP_REQUEST        1
#define DHCP_REPLY          2
#define DHCP_HTYPE_ETHERNET 1
#define DHCP_HLEN_ETHERNET  6
#define DHCP_MSG_LEN      236

#define DHCPS_SERVER_PORT  67
#define DHCPS_CLIENT_PORT  68

#define DHCPDISCOVER  1
#define DHCPOFFER     2
#define DHCPREQUEST   3
#define DHCPDECLINE   4
#define DHCPACK       5
#define DHCPNAK       6
#define DHCPRELEASE   7

#define DHCP_OPTION_SUBNET_MASK   1
#define DHCP_OPTION_ROUTER        3
#define DHCP_OPTION_DNS_SERVER    6
#define DHCP_OPTION_REQ_IPADDR   50
#define DHCP_OPTION_LEASE_TIME   51
#define DHCP_OPTION_MSG_TYPE     53
#define DHCP_OPTION_SERVER_ID    54
#define DHCP_OPTION_INTERFACE_MTU 26
#define DHCP_OPTION_PERFORM_ROUTER_DISCOVERY 31
#define DHCP_OPTION_BROADCAST_ADDRESS 28
#define DHCP_OPTION_REQ_LIST     55
#define DHCP_OPTION_END         255

//#define USE_CLASS_B_NET 1
#define DHCPS_DEBUG          0
#define DHCPS_LOG printf


#define IS_INVALID_SUBNET_MASK(x)  (((x-1) | x) != 0xFFFFFFFF)
/* Notes:
*  CIDR eliminates the traditional Class A, Class B and Class C addresses.
 */
#define IP_CLASS_HOST_NUM(mask)            (0xffffffff & ~mask)

#define DHCP_CHECK_SUBNET_MASK_IP(mask)                                                                   \
    do {                                                                                                  \
        if (IS_INVALID_SUBNET_MASK(mask)) {                                                               \
            DHCPS_LOG("dhcps: Illegal subnet mask.\n");                                                   \
            return ERR_ARG;                                                                               \
        }                                                                                                 \
    } while (0)

#define DHCP_CHECK_IP_MATCH_SUBNET_MASK(mask, ip)                           \
    u32_t start_ip = 0;                                                     \
    u32_t end_ip = 0;                                                       \
    do {                                                                    \
        start_ip = ip & mask;                                               \
        end_ip = start_ip | ~mask;                                          \
        if (ip == end_ip || ip == start_ip)                                 \
        {                                                                   \
            DHCPS_LOG("dhcps: ip address and subnet mask do not match.\n"); \
            return ERR_ARG;                                                 \
        }                                                                   \
    } while (0)

#define MAX_STATION_NUM CONFIG_LWIP_DHCPS_MAX_STATION_NUM

#define DHCPS_STATE_OFFER 1
#define DHCPS_STATE_DECLINE 2
#define DHCPS_STATE_ACK 3
#define DHCPS_STATE_NAK 4
#define DHCPS_STATE_IDLE 5
#define DHCPS_STATE_RELEASE 6

typedef struct _list_node {
	void *pnode;
	struct _list_node *pnext;
} list_node;

/* MESHCOM-PATCH DS-03 (lwIP #5): a lease that was only offered (DISCOVER) or created for an unknown
 * MAC is "unconfirmed" and lives DHCPS_OFFER_HOLD_SECS only; it becomes a full lease when the client
 * is ACKed (REQUEST). lease_timer counts DHCPS_COARSE_TIMER_SECS ticks. */
#define DHCPS_OFFER_HOLD_SECS   30
#define DHCPS_OFFER_HOLD_TICKS  (DHCPS_OFFER_HOLD_SECS / DHCPS_COARSE_TIMER_SECS)
/* MESHCOM-PATCH DS-03: station limit enforced when a new MAC arrives (was only checked by the
 * coarse timer afterwards, and never above the DHCPS_MAX_LEASE address range). */
#define DHCPS_STATION_LIMIT     ((MAX_STATION_NUM) < DHCPS_MAX_LEASE ? (MAX_STATION_NUM) : DHCPS_MAX_LEASE)
/* MESHCOM-PATCH DS-04: largest DHCP message accepted (Ethernet MTU); larger datagrams are dropped. */
#define DHCPS_RX_MAX_LEN        1500

static void kill_oldest_dhcps_pool(void);

////////////////////////////////////////////////////////////////////////////////////

static const u32_t magic_cookie  = 0x63538263;

static struct netif   *dhcps_netif = NULL;
static ip4_addr_t  broadcast_dhcps;
static ip4_addr_t server_address;
static ip4_addr_t dns_server = {0};
static ip4_addr_t client_address;        //added
static ip4_addr_t client_address_plus;
static ip4_addr_t s_dhcps_mask = {
#ifdef USE_CLASS_B_NET
        .addr = PP_HTONL(LWIP_MAKEU32(255, 240, 0, 0))
#else
        .addr = PP_HTONL(LWIP_MAKEU32(255, 255, 255, 0))
#endif
    };

static list_node *plist = NULL;
static bool renew = false;

static dhcps_lease_t dhcps_poll;
static dhcps_time_t dhcps_lease_time = DHCPS_LEASE_TIME_DEF;  //minute
static dhcps_offer_t dhcps_offer = 0xFF;
static dhcps_offer_t dhcps_dns = 0x00;
static dhcps_cb_t dhcps_cb;

/******************************************************************************
 * FunctionName : dhcps_option_info
 * Description  : get the DHCP message option info
 * Parameters   : op_id -- DHCP message option id
 *                opt_len -- DHCP message option length
 * Returns      : DHCP message option addr
*******************************************************************************/
void *dhcps_option_info(u8_t op_id, u32_t opt_len)
{
    void *option_arg = NULL;

    switch (op_id) {
        case IP_ADDRESS_LEASE_TIME:
            if (opt_len == sizeof(dhcps_time_t)) {
                option_arg = &dhcps_lease_time;
            }

            break;

        case REQUESTED_IP_ADDRESS:
            if (opt_len == sizeof(dhcps_lease_t)) {
                option_arg = &dhcps_poll;
            }

            break;

        case ROUTER_SOLICITATION_ADDRESS:
            if (opt_len == sizeof(dhcps_offer_t)) {
                option_arg = &dhcps_offer;
            }

            break;

        case DOMAIN_NAME_SERVER:
            if (opt_len == sizeof(dhcps_offer_t)) {
                option_arg = &dhcps_dns;
            }

            break;
        case SUBNET_MASK:
            if (opt_len == sizeof(s_dhcps_mask)) {
                option_arg = &s_dhcps_mask;
            }

            break;
        default:
            break;
    }

    return option_arg;
}

/******************************************************************************
 * FunctionName : dhcps_set_option_info
 * Description  : set the DHCP message option info
 * Parameters   : op_id -- DHCP message option id
 *                opt_info -- DHCP message option info
 *                opt_len -- DHCP message option length
 * Returns      : none
*******************************************************************************/
void dhcps_set_option_info(u8_t op_id, void *opt_info, u32_t opt_len)
{
    if (opt_info == NULL) {
        return;
    }
    switch (op_id) {
        case IP_ADDRESS_LEASE_TIME:
            if (opt_len == sizeof(dhcps_time_t)) {
                dhcps_lease_time = *(dhcps_time_t *)opt_info;
            }

            break;

        case REQUESTED_IP_ADDRESS:
            if (opt_len == sizeof(dhcps_lease_t)) {
                dhcps_poll = *(dhcps_lease_t *)opt_info;
            }

            break;

        case ROUTER_SOLICITATION_ADDRESS:
            if (opt_len == sizeof(dhcps_offer_t)) {
                dhcps_offer = *(dhcps_offer_t *)opt_info;
            }

            break;

        case DOMAIN_NAME_SERVER:
            if (opt_len == sizeof(dhcps_offer_t)) {
                dhcps_dns = *(dhcps_offer_t *)opt_info;
            }
            break;

        case SUBNET_MASK:
            if (opt_len == sizeof(s_dhcps_mask)) {
                s_dhcps_mask = *(ip4_addr_t *)opt_info;
            }


        default:
            break;
    }
    return;
}

/******************************************************************************
 * FunctionName : node_insert_to_list
 * Description  : insert the node to the list
 * Parameters   : phead -- the head node of the list
 *                pinsert -- the insert node of the list
 * Returns      : none
*******************************************************************************/
static void node_insert_to_list(list_node **phead, list_node *pinsert)
{
    list_node *plist = NULL;
    struct dhcps_pool *pdhcps_pool = NULL;
    struct dhcps_pool *pdhcps_node = NULL;

    if (*phead == NULL) {
        *phead = pinsert;
    } else {
        plist = *phead;
        pdhcps_node = pinsert->pnode;
        pdhcps_pool = plist->pnode;

        if (pdhcps_node->ip.addr < pdhcps_pool->ip.addr) {
            pinsert->pnext = plist;
            *phead = pinsert;
        } else {
            while (plist->pnext != NULL) {
                pdhcps_pool = plist->pnext->pnode;

                if (pdhcps_node->ip.addr < pdhcps_pool->ip.addr) {
                    pinsert->pnext = plist->pnext;
                    plist->pnext = pinsert;
                    break;
                }

                plist = plist->pnext;
            }

            if (plist->pnext == NULL) {
                plist->pnext = pinsert;
            }
        }
    }

//	pinsert->pnext = NULL;
}

/******************************************************************************
 * FunctionName : node_delete_from_list
 * Description  : remove the node from list
 * Parameters   : phead -- the head node of the list
 *                pdelete -- the remove node of the list
 * Returns      : none
*******************************************************************************/
void node_remove_from_list(list_node **phead, list_node *pdelete)
{
    list_node *plist = NULL;

    plist = *phead;

    if (plist == NULL) {
        *phead = NULL;
    } else {
        if (plist == pdelete) {
            // Note: Ignoring the "use after free" warnings, as it could only happen
            // if the linked list contains loops
            *phead = plist->pnext; // NOLINT(clang-analyzer-unix.Malloc)
            pdelete->pnext = NULL;
        } else {
            while (plist != NULL) {
                if (plist->pnext == pdelete) { // NOLINT(clang-analyzer-unix.Malloc)
                    plist->pnext = pdelete->pnext;
                    pdelete->pnext = NULL;
                }

                plist = plist->pnext;
            }
        }
    }
}

/******************************************************************************
 * FunctionName : add_msg_type
 * Description  : add TYPE option of DHCP message
 * Parameters   : optptr -- the addr of DHCP message option
 * Returns      : the addr of DHCP message option
*******************************************************************************/
static u8_t *add_msg_type(u8_t *optptr, u8_t type)
{
    *optptr++ = DHCP_OPTION_MSG_TYPE;
    *optptr++ = 1;
    *optptr++ = type;
    return optptr;
}

/******************************************************************************
 * FunctionName : add_offer_options
 * Description  : add OFFER option of DHCP message
 * Parameters   : optptr -- the addr of DHCP message option
 * Returns      : the addr of DHCP message option
*******************************************************************************/
static u8_t *add_offer_options(u8_t *optptr)
{
    ip4_addr_t ipadd;

    ipadd.addr = *((u32_t *) &server_address);

    *optptr++ = DHCP_OPTION_SUBNET_MASK;
    *optptr++ = 4;
    *optptr++ = ip4_addr1(&s_dhcps_mask);
    *optptr++ = ip4_addr2(&s_dhcps_mask);
    *optptr++ = ip4_addr3(&s_dhcps_mask);
    *optptr++ = ip4_addr4(&s_dhcps_mask);

    *optptr++ = DHCP_OPTION_LEASE_TIME;
    *optptr++ = 4;
    *optptr++ = ((dhcps_lease_time * DHCPS_LEASE_UNIT) >> 24) & 0xFF;
    *optptr++ = ((dhcps_lease_time * DHCPS_LEASE_UNIT) >> 16) & 0xFF;
    *optptr++ = ((dhcps_lease_time * DHCPS_LEASE_UNIT) >> 8) & 0xFF;
    *optptr++ = ((dhcps_lease_time * DHCPS_LEASE_UNIT) >> 0) & 0xFF;

    *optptr++ = DHCP_OPTION_SERVER_ID;
    *optptr++ = 4;
    *optptr++ = ip4_addr1(&ipadd);
    *optptr++ = ip4_addr2(&ipadd);
    *optptr++ = ip4_addr3(&ipadd);
    *optptr++ = ip4_addr4(&ipadd);

    if (dhcps_router_enabled(dhcps_offer)) {
        esp_netif_ip_info_t if_ip;
        memset(&if_ip , 0x00, sizeof(esp_netif_ip_info_t));
        esp_netif_get_ip_info(dhcps_netif->state, &if_ip);

        ip4_addr_t* gw_ip = (ip4_addr_t*)&if_ip.gw;

        if (!ip4_addr_isany_val(*gw_ip)) {
            *optptr++ = DHCP_OPTION_ROUTER;
            *optptr++ = 4;
            *optptr++ = ip4_addr1(gw_ip);
            *optptr++ = ip4_addr2(gw_ip);
            *optptr++ = ip4_addr3(gw_ip);
            *optptr++ = ip4_addr4(gw_ip);
        }
    }

    *optptr++ = DHCP_OPTION_DNS_SERVER;
    *optptr++ = 4;
    if (dhcps_dns_enabled(dhcps_dns)) {
        *optptr++ = ip4_addr1(&dns_server);
        *optptr++ = ip4_addr2(&dns_server);
        *optptr++ = ip4_addr3(&dns_server);
        *optptr++ = ip4_addr4(&dns_server);
    }else {
        *optptr++ = ip4_addr1(&ipadd);
        *optptr++ = ip4_addr2(&ipadd);
        *optptr++ = ip4_addr3(&ipadd);
        *optptr++ = ip4_addr4(&ipadd);
    }

    ip4_addr_t broadcast_addr = { .addr = (ipadd.addr & s_dhcps_mask.addr) | ~s_dhcps_mask.addr };
    *optptr++ = DHCP_OPTION_BROADCAST_ADDRESS;
    *optptr++ = 4;
    *optptr++ = ip4_addr1(&broadcast_addr);
    *optptr++ = ip4_addr2(&broadcast_addr);
    *optptr++ = ip4_addr3(&broadcast_addr);
    *optptr++ = ip4_addr4(&broadcast_addr);

    *optptr++ = DHCP_OPTION_INTERFACE_MTU;
    *optptr++ = 2;
    *optptr++ = 0x05;
    *optptr++ = 0xdc;

    *optptr++ = DHCP_OPTION_PERFORM_ROUTER_DISCOVERY;
    *optptr++ = 1;
    *optptr++ = 0x00;

    *optptr++ = 43;
    *optptr++ = 6;

    *optptr++ = 0x01;
    *optptr++ = 4;
    *optptr++ = 0x00;
    *optptr++ = 0x00;
    *optptr++ = 0x00;
    *optptr++ = 0x02;

    return optptr;
}

/******************************************************************************
 * FunctionName : add_end
 * Description  : add end option of DHCP message
 * Parameters   : optptr -- the addr of DHCP message option
 * Returns      : the addr of DHCP message option
*******************************************************************************/
static u8_t *add_end(u8_t *optptr)
{
    *optptr++ = DHCP_OPTION_END;
    return optptr;
}

/******************************************************************************
 * FunctionName : create_msg
 * Description  : create response message
 * Parameters   : m -- DHCP message info
 * Returns      : none
*******************************************************************************/
static void create_msg(struct dhcps_msg *m)
{
    ip4_addr_t client;


    client.addr = *((uint32_t *) &client_address);

    m->op = DHCP_REPLY;

    m->htype = DHCP_HTYPE_ETHERNET;

    m->hlen = 6;

    m->hops = 0;
//        os_memcpy((char *) xid, (char *) m->xid, sizeof(m->xid));
    m->secs = 0;
    m->flags = htons(BOOTP_BROADCAST);

    memcpy((char *) m->yiaddr, (char *) &client.addr, sizeof(m->yiaddr));

    memset((char *) m->ciaddr, 0, sizeof(m->ciaddr));

    memset((char *) m->siaddr, 0, sizeof(m->siaddr));

    memset((char *) m->giaddr, 0, sizeof(m->giaddr));

    memset((char *) m->sname, 0, sizeof(m->sname));

    memset((char *) m->file, 0, sizeof(m->file));

    memset((char *) m->options, 0, sizeof(m->options));

    u32_t magic_cookie_temp = magic_cookie;

    memcpy((char *) m->options, &magic_cookie_temp, sizeof(magic_cookie_temp));
}

struct pbuf * dhcps_pbuf_alloc(u16_t len)
{
    u16_t mlen = sizeof(struct dhcps_msg);

    if (len > mlen) {
#if DHCPS_DEBUG
        DHCPS_LOG("dhcps: len=%d mlen=%d", len, mlen);
#endif
        mlen = len;
    }

    return pbuf_alloc(PBUF_TRANSPORT, mlen, PBUF_RAM);
}

/******************************************************************************
 * FunctionName : send_offer
 * Description  : DHCP message OFFER Response
 * Parameters   : m -- DHCP message info
 * Returns      : none
*******************************************************************************/
static void send_offer(struct dhcps_msg *m, u16_t len)
{
    u8_t *end;
    struct pbuf *p, *q;
    u8_t *data;
    u16_t cnt = 0;
    u16_t i;
#if DHCPS_DEBUG
    err_t SendOffer_err_t;
#endif
    create_msg(m);

    end = add_msg_type(&m->options[4], DHCPOFFER);
    end = add_offer_options(end);
    end = add_end(end);

    p = dhcps_pbuf_alloc(len);
#if DHCPS_DEBUG
    DHCPS_LOG("udhcp: send_offer>>p->ref = %d\n", p->ref);
#endif

    if (p != NULL) {

#if DHCPS_DEBUG
        DHCPS_LOG("dhcps: send_offer>>pbuf_alloc succeed\n");
        DHCPS_LOG("dhcps: send_offer>>p->tot_len = %d\n", p->tot_len);
        DHCPS_LOG("dhcps: send_offer>>p->len = %d\n", p->len);
#endif
        q = p;

        while (q != NULL) {
            data = (u8_t *)q->payload;

            for (i = 0; i < q->len; i++) {
                data[i] = ((u8_t *) m)[cnt++];
#if DHCPS_DEBUG
                DHCPS_LOG("%02x ", data[i]);

                if ((i + 1) % 16 == 0) {
                    DHCPS_LOG("\n");
                }

#endif
            }

            q = q->next;
        }
    } else {

#if DHCPS_DEBUG
        DHCPS_LOG("dhcps: send_offer>>pbuf_alloc failed\n");
#endif
        return;
    }

    ip_addr_t ip_temp = IPADDR4_INIT(0x0);
    ip4_addr_set(ip_2_ip4(&ip_temp), &broadcast_dhcps);
    struct udp_pcb *pcb_dhcps = dhcps_netif->dhcps_pcb;
#if DHCPS_DEBUG
    SendOffer_err_t = udp_sendto(pcb_dhcps, p, &ip_temp, DHCPS_CLIENT_PORT);
    DHCPS_LOG("dhcps: send_offer>>udp_sendto result %x\n", SendOffer_err_t);
#else
    udp_sendto(pcb_dhcps, p, &ip_temp, DHCPS_CLIENT_PORT);
#endif

    if (p->ref != 0) {
#if DHCPS_DEBUG
        DHCPS_LOG("udhcp: send_offer>>free pbuf\n");
#endif
        pbuf_free(p);
    }
}

/******************************************************************************
 * FunctionName : send_nak
 * Description  : DHCP message NACK Response
 * Parameters   : m -- DHCP message info
 * Returns      : none
*******************************************************************************/
static void send_nak(struct dhcps_msg *m, u16_t len)
{
    u8_t *end;
    struct pbuf *p, *q;
    u8_t *data;
    u16_t cnt = 0;
    u16_t i;
#if DHCPS_DEBUG
    err_t SendNak_err_t;
#endif
    create_msg(m);

    end = add_msg_type(&m->options[4], DHCPNAK);
    end = add_end(end);

    p = dhcps_pbuf_alloc(len);
#if DHCPS_DEBUG
    DHCPS_LOG("udhcp: send_nak>>p->ref = %d\n", p->ref);
#endif

    if (p != NULL) {

#if DHCPS_DEBUG
        DHCPS_LOG("dhcps: send_nak>>pbuf_alloc succeed\n");
        DHCPS_LOG("dhcps: send_nak>>p->tot_len = %d\n", p->tot_len);
        DHCPS_LOG("dhcps: send_nak>>p->len = %d\n", p->len);
#endif
        q = p;

        while (q != NULL) {
            data = (u8_t *)q->payload;

            for (i = 0; i < q->len; i++) {
                data[i] = ((u8_t *) m)[cnt++];
#if DHCPS_DEBUG
                DHCPS_LOG("%02x ", data[i]);

                if ((i + 1) % 16 == 0) {
                    DHCPS_LOG("\n");
                }

#endif
            }

            q = q->next;
        }
    } else {

#if DHCPS_DEBUG
        DHCPS_LOG("dhcps: send_nak>>pbuf_alloc failed\n");
#endif
        return;
    }

    ip_addr_t ip_temp = IPADDR4_INIT(0x0);
    ip4_addr_set(ip_2_ip4(&ip_temp), &broadcast_dhcps);
    struct udp_pcb *pcb_dhcps = dhcps_netif->dhcps_pcb;
#if DHCPS_DEBUG
    SendNak_err_t = udp_sendto(pcb_dhcps, p, &ip_temp, DHCPS_CLIENT_PORT);
    DHCPS_LOG("dhcps: send_nak>>udp_sendto result %x\n", SendNak_err_t);
#else
    udp_sendto(pcb_dhcps, p, &ip_temp, DHCPS_CLIENT_PORT);
#endif

    if (p->ref != 0) {
#if DHCPS_DEBUG
        DHCPS_LOG("udhcp: send_nak>>free pbuf\n");
#endif
        pbuf_free(p);
    }
}

/******************************************************************************
 * FunctionName : send_ack
 * Description  : DHCP message ACK Response
 * Parameters   : m -- DHCP message info
 * Returns      : none
*******************************************************************************/
static void send_ack(struct dhcps_msg *m, u16_t len)
{
    u8_t *end;
    struct pbuf *p, *q;
    u8_t *data;
    u16_t cnt = 0;
    u16_t i;
    err_t SendAck_err_t;
    create_msg(m);

    end = add_msg_type(&m->options[4], DHCPACK);
    end = add_offer_options(end);
    end = add_end(end);

    p = dhcps_pbuf_alloc(len);
#if DHCPS_DEBUG
    DHCPS_LOG("udhcp: send_ack>>p->ref = %d\n", p->ref);
#endif

    if (p != NULL) {

#if DHCPS_DEBUG
        DHCPS_LOG("dhcps: send_ack>>pbuf_alloc succeed\n");
        DHCPS_LOG("dhcps: send_ack>>p->tot_len = %d\n", p->tot_len);
        DHCPS_LOG("dhcps: send_ack>>p->len = %d\n", p->len);
#endif
        q = p;

        while (q != NULL) {
            data = (u8_t *)q->payload;

            for (i = 0; i < q->len; i++) {
                data[i] = ((u8_t *) m)[cnt++];
#if DHCPS_DEBUG
                DHCPS_LOG("%02x ", data[i]);

                if ((i + 1) % 16 == 0) {
                    DHCPS_LOG("\n");
                }

#endif
            }

            q = q->next;
        }
    } else {

#if DHCPS_DEBUG
        DHCPS_LOG("dhcps: send_ack>>pbuf_alloc failed\n");
#endif
        return;
    }

    ip_addr_t ip_temp = IPADDR4_INIT(0x0);
    ip4_addr_set(ip_2_ip4(&ip_temp), &broadcast_dhcps);
    struct udp_pcb *pcb_dhcps = dhcps_netif->dhcps_pcb;
    SendAck_err_t = udp_sendto(pcb_dhcps, p, &ip_temp, DHCPS_CLIENT_PORT);
#if DHCPS_DEBUG
    DHCPS_LOG("dhcps: send_ack>>udp_sendto result %x\n", SendAck_err_t);
#endif

    if (SendAck_err_t == ERR_OK) {
        dhcps_cb(m->yiaddr);
    }

    if (p->ref != 0) {
#if DHCPS_DEBUG
        DHCPS_LOG("udhcp: send_ack>>free pbuf\n");
#endif
        pbuf_free(p);
    }
}

/******************************************************************************
 * FunctionName : parse_options
 * Description  : parse DHCP message options
 * Parameters   : optptr -- DHCP message option info
 *                len -- DHCP message option length
 * Returns      : none
*******************************************************************************/
static u8_t parse_options(u8_t *optptr, s16_t len)
{
    ip4_addr_t client;
    struct dhcps_state s;

    client.addr = *((uint32_t *) &client_address);

    u16_t type = 0;
    /* MESHCOM-PATCH DS-02: never trust the option length bytes; every option is checked to lie
     * inside the received packet (dhcps_opt_step) before its value is read. */
    u16_t avail = (len > 0) ? (u16_t)len : 0;
    u16_t pos = 0;
    u16_t next = 0;
    u8_t code = 0;
    u8_t vlen = 0;

    s.state = DHCPS_STATE_IDLE;

    while (dhcps_opt_step(optptr, avail, pos, &code, &vlen, &next)) {
        const u8_t *val = optptr + pos + 2;

        if (code == DHCP_OPTION_END) {
            break;
        }

        switch (code) {

            case DHCP_OPTION_MSG_TYPE:	//53
                if (vlen >= 1) {
                    type = val[0];
                }
                break;

            case DHCP_OPTION_REQ_IPADDR://50
                if (vlen >= 4) {
                    if (memcmp((char *) &client.addr, (const char *) val, 4) == 0) {
                        s.state = DHCPS_STATE_ACK;
                    } else {
                        s.state = DHCPS_STATE_NAK;
                    }
                }

                break;

            default:
                break;
        }

        pos = next;
    }

    switch (type) {

        case DHCPDISCOVER://1
            s.state = DHCPS_STATE_OFFER;
#if DHCPS_DEBUG
            DHCPS_LOG("dhcps: DHCPD_STATE_OFFER\n");
#endif
            break;

        case DHCPREQUEST://3
            if (!(s.state == DHCPS_STATE_ACK || s.state == DHCPS_STATE_NAK)) {
                if (renew == true) {
                    s.state = DHCPS_STATE_ACK;
                } else {
                    s.state = DHCPS_STATE_NAK;
                }

#if DHCPS_DEBUG
                DHCPS_LOG("dhcps: DHCPD_STATE_NAK\n");
#endif
            }

            break;

        case DHCPDECLINE://4
            s.state = DHCPS_STATE_IDLE;
#if DHCPS_DEBUG
            DHCPS_LOG("dhcps: DHCPD_STATE_IDLE\n");
#endif
            break;

        case DHCPRELEASE://7
            s.state = DHCPS_STATE_RELEASE;
#if DHCPS_DEBUG
            DHCPS_LOG("dhcps: DHCPD_STATE_IDLE\n");
#endif
            break;
    }

#if DHCPS_DEBUG
    DHCPS_LOG("dhcps: return s.state = %d\n", s.state);
#endif
    return s.state;
}

/******************************************************************************
 * FunctionName : parse_msg
 * Description  : parse DHCP message from netif
 * Parameters   : m -- DHCP message info
 *                len -- DHCP message length
 * Returns      : DHCP message type
*******************************************************************************/
static s16_t parse_msg(struct dhcps_msg *m, u16_t len)
{
    u32_t lease_timer = (dhcps_lease_time * DHCPS_LEASE_UNIT)/DHCPS_COARSE_TIMER_SECS;

    if (memcmp((char *)m->options, &magic_cookie, sizeof(magic_cookie)) == 0) {
#if DHCPS_DEBUG
        DHCPS_LOG("dhcps: len = %d\n", len);
#endif
        ip4_addr_t addr_tmp;

        struct dhcps_pool *pdhcps_pool = NULL;
        list_node *pnode = NULL;
        list_node *pback_node = NULL;
        ip4_addr_t first_address;
        bool flag = false;

        first_address.addr = dhcps_poll.start_ip.addr;
        client_address.addr = client_address_plus.addr;
        renew = false;

        /* MESHCOM-PATCH DS-03 (lwIP #5): unknown MAC while the table is at the station limit: make
         * room first (oldest unconfirmed lease, else oldest lease) so random client MACs can neither
         * grow the table without bound nor use up the address range. */
        {
            u16_t n_leases = 0;
            bool mac_known = false;
            list_node *pn;

            for (pn = plist; pn != NULL; pn = pn->pnext) {
                struct dhcps_pool *pp = pn->pnode;
                n_leases++;
                if (memcmp(pp->mac, m->chaddr, sizeof(pp->mac)) == 0) {
                    mac_known = true;
                }
            }

            if (!mac_known && n_leases >= DHCPS_STATION_LIMIT) {
                kill_oldest_dhcps_pool();
            }
        }

        if (plist != NULL) {
            for (pback_node = plist; pback_node != NULL; pback_node = pback_node->pnext) {
                pdhcps_pool = pback_node->pnode;

                if (memcmp(pdhcps_pool->mac, m->chaddr, sizeof(pdhcps_pool->mac)) == 0) {
                    if (memcmp(&pdhcps_pool->ip.addr, m->ciaddr, sizeof(pdhcps_pool->ip.addr)) == 0) {
                        renew = true;
                    }

                    client_address.addr = pdhcps_pool->ip.addr;
                    /* MESHCOM-PATCH DS-03: the lease timer is no longer refreshed to the full lease
                     * on any packet of a known MAC; only an ACK extends it (see below). */
                    pnode = pback_node;
                    goto POOL_CHECK;
                } else if (pdhcps_pool->ip.addr == client_address_plus.addr) {
                    addr_tmp.addr = htonl(client_address_plus.addr);
                    addr_tmp.addr++;
                    client_address_plus.addr = htonl(addr_tmp.addr);
                    client_address.addr = client_address_plus.addr;
                }

                if (flag == false) { // search the fisrt unused ip
                    if (first_address.addr < pdhcps_pool->ip.addr) {
                        flag = true;
                    } else {
                        addr_tmp.addr = htonl(first_address.addr);
                        addr_tmp.addr++;
                        first_address.addr = htonl(addr_tmp.addr);
                    }
                }
            }
        } else {
            client_address.addr = dhcps_poll.start_ip.addr;
        }

        if (client_address_plus.addr > dhcps_poll.end_ip.addr) {
            client_address.addr = first_address.addr;
        }

        if (client_address.addr > dhcps_poll.end_ip.addr) {
            client_address_plus.addr = dhcps_poll.start_ip.addr;
            pdhcps_pool = NULL;
            pnode = NULL;
        } else {
            /* MESHCOM-PATCH DS-01 (lwIP #4): mem_malloc results were never checked (memset(NULL)).
             * On failure nothing is leaked, no lease is created and the request is dropped. */
            pdhcps_pool = (struct dhcps_pool *)mem_malloc(sizeof(struct dhcps_pool));
            if (pdhcps_pool == NULL) {
                memset(&client_address, 0x0, sizeof(client_address));
                return 0;
            }
            memset(pdhcps_pool , 0x00 , sizeof(struct dhcps_pool));

            pdhcps_pool->ip.addr = client_address.addr;
            memcpy(pdhcps_pool->mac, m->chaddr, sizeof(pdhcps_pool->mac));
            /* MESHCOM-PATCH DS-03: new entries are unconfirmed until the client is ACKed. */
            pdhcps_pool->lease_timer = DHCPS_OFFER_HOLD_TICKS;
            pnode = (list_node *)mem_malloc(sizeof(list_node));
            if (pnode == NULL) {
                free(pdhcps_pool);
                memset(&client_address, 0x0, sizeof(client_address));
                return 0;
            }
            memset(pnode , 0x00 , sizeof(list_node));

            pnode->pnode = pdhcps_pool;
            pnode->pnext = NULL;
            node_insert_to_list(&plist, pnode);

            if (client_address.addr == dhcps_poll.end_ip.addr) {
                client_address_plus.addr = dhcps_poll.start_ip.addr;
            } else {
                addr_tmp.addr = htonl(client_address.addr);
                addr_tmp.addr++;
                client_address_plus.addr = htonl(addr_tmp.addr);
            }
        }

POOL_CHECK:

        if ((client_address.addr > dhcps_poll.end_ip.addr) || (ip4_addr_isany(&client_address))) {
            if (pnode != NULL) {
                node_remove_from_list(&plist, pnode);
                free(pnode);
                pnode = NULL;
            }

            if (pdhcps_pool != NULL) {
                free(pdhcps_pool);
                pdhcps_pool = NULL;
            }

            return 4;
        }

        s16_t ret = parse_options(&m->options[4], len);

        /* MESHCOM-PATCH DS-03: promote on ACK; refresh an unconfirmed entry on OFFER. Confirmed
         * leases are never shortened. */
        if (pdhcps_pool != NULL) {
            if (ret == DHCPS_STATE_ACK) {
                pdhcps_pool->lease_timer = lease_timer;
            } else if (ret == DHCPS_STATE_OFFER && pdhcps_pool->lease_timer < DHCPS_OFFER_HOLD_TICKS) {
                pdhcps_pool->lease_timer = DHCPS_OFFER_HOLD_TICKS;
            }
        }

        if (ret == DHCPS_STATE_RELEASE || ret == DHCPS_STATE_NAK) {
            if (pnode != NULL) {
                node_remove_from_list(&plist, pnode);
                free(pnode);
                pnode = NULL;
            }

            if (pdhcps_pool != NULL) {
                free(pdhcps_pool);
                pdhcps_pool = NULL;
            }

            memset(&client_address, 0x0, sizeof(client_address));
        }

#if DHCPS_DEBUG
        DHCPS_LOG("dhcps: xid changed\n");
        DHCPS_LOG("dhcps: client_address.addr = %x\n", client_address.addr);
#endif
        return ret;
    }

    return 0;
}

/******************************************************************************
 * FunctionName : handle_dhcp
 * Description  : If an incoming DHCP message is in response to us, then trigger the state machine
 * Parameters   : arg -- arg user supplied argument (udp_pcb.recv_arg)
 * 				  pcb -- the udp_pcb which received data
 * 			      p -- the packet buffer that was received
 * 				  addr -- the remote IP address from which the packet was received
 * 				  port -- the remote port from which the packet was received
 * Returns      : none
*******************************************************************************/
static void handle_dhcp(void *arg,
                        struct udp_pcb *pcb,
                        struct pbuf *p,
                        const ip_addr_t *addr,
                        u16_t port)
{
    struct dhcps_msg *pmsg_dhcps = NULL;
    s16_t tlen, malloc_len;

#if DHCPS_DEBUG
    DHCPS_LOG("dhcps: handle_dhcp-> receive a packet\n");
#endif

    if (p == NULL) {
        return;
    }

    /* MESHCOM-PATCH DS-04: s16_t tlen/malloc_len overflowed for tot_len > 32767 and the copy loops
     * only followed two pbufs. Drop datagrams that cannot be a DHCP message (shorter than the fixed
     * header + cookie, or larger than DHCPS_RX_MAX_LEN) and copy the whole chain with
     * pbuf_copy_partial(). */
    if (p->tot_len < DHCP_MSG_LEN + 4 || p->tot_len > DHCPS_RX_MAX_LEN) {
        pbuf_free(p);
        return;
    }

    malloc_len = sizeof(struct dhcps_msg);
    if (malloc_len < p->tot_len) {
        malloc_len = p->tot_len;
    }

    pmsg_dhcps = (struct dhcps_msg *)mem_malloc(malloc_len);
    if (NULL == pmsg_dhcps) {
        pbuf_free(p);
        return;
    }

    memset(pmsg_dhcps , 0x00 , malloc_len);
    tlen = p->tot_len;
    pbuf_copy_partial(p, pmsg_dhcps, p->tot_len, 0);

    switch (parse_msg(pmsg_dhcps, tlen - 240)) {
        case DHCPS_STATE_OFFER://1
#if DHCPS_DEBUG
            DHCPS_LOG("dhcps: handle_dhcp-> DHCPD_STATE_OFFER\n");
#endif
            send_offer(pmsg_dhcps, malloc_len);
            break;

        case DHCPS_STATE_ACK://3
#if DHCPS_DEBUG
            DHCPS_LOG("dhcps: handle_dhcp-> DHCPD_STATE_ACK\n");
#endif
            send_ack(pmsg_dhcps, malloc_len);
            break;

        case DHCPS_STATE_NAK://4
#if DHCPS_DEBUG
            DHCPS_LOG("dhcps: handle_dhcp-> DHCPD_STATE_NAK\n");
#endif
            send_nak(pmsg_dhcps, malloc_len);
            break;

        default :
            break;
    }

#if DHCPS_DEBUG
    DHCPS_LOG("dhcps: handle_dhcp-> pbuf_free(p)\n");
#endif
    pbuf_free(p);
    free(pmsg_dhcps);
    pmsg_dhcps = NULL;
}

/******************************************************************************
 * FunctionName : dhcps_poll_set
 * Description  : set ip poll from start to end for station
 * Parameters   : ip -- The current ip addr
 * Returns      : none
*******************************************************************************/
static void dhcps_poll_set(u32_t ip)
{
    u32_t server_ip = 0;
    u32_t start_ip = 0;
    u32_t end_ip = 0;
    u32_t range_start_ip = 0;
    u32_t range_end_ip = 0;

    if (dhcps_poll.enable == true) {
        server_ip = htonl(ip);
        start_ip = htonl(dhcps_poll.start_ip.addr);
        end_ip = htonl(dhcps_poll.end_ip.addr);

        /*config ip information can't contain local ip*/
        if ((server_ip >= start_ip) && (server_ip <= end_ip)) {
            dhcps_poll.enable = false;
        } else {
            /*config ip information must be in the same segment as the local ip*/
            if (!ip4_addr_netcmp(&dhcps_poll.start_ip, &server_address, &s_dhcps_mask)
                || !ip4_addr_netcmp(&dhcps_poll.end_ip, &server_address, &s_dhcps_mask)
                || (end_ip - start_ip + 1 > DHCPS_MAX_LEASE)) {
                dhcps_poll.enable = false;
            }
        }
    }

    if (dhcps_poll.enable == false) {
        server_ip = htonl(ip);
        range_start_ip = server_ip & htonl(s_dhcps_mask.addr);
        range_end_ip = range_start_ip | ~htonl(s_dhcps_mask.addr);

        if (server_ip - range_start_ip > range_end_ip - server_ip) {
            range_start_ip = range_start_ip + 1;
            range_end_ip = server_ip - 1;
        } else {
            range_start_ip = server_ip + 1;
            range_end_ip = range_end_ip - 1;
        }
        if (range_end_ip - range_start_ip + 1 > DHCPS_MAX_LEASE) {
            range_end_ip = range_start_ip + DHCPS_MAX_LEASE - 1;
        }
        bzero(&dhcps_poll, sizeof(dhcps_poll));
        dhcps_poll.start_ip.addr = range_start_ip;
        dhcps_poll.end_ip.addr = range_end_ip;
        dhcps_poll.start_ip.addr = htonl(dhcps_poll.start_ip.addr);
        dhcps_poll.end_ip.addr = htonl(dhcps_poll.end_ip.addr);
        dhcps_poll.enable = true;
    }

}


/******************************************************************************
 * FunctionName : dhcps_set_new_lease_cb
 * Description  : set callback for dhcp server when it assign an IP
 *                to the connected dhcp client
 * Parameters   : cb -- callback for dhcp server
 * Returns      : none
*******************************************************************************/
void dhcps_set_new_lease_cb(dhcps_cb_t cb)
{
    dhcps_cb = cb;
}

/******************************************************************************
 * FunctionName : dhcps_start
 * Description  : start dhcp server function
 * Parameters   : netif -- The current netif addr
 *              : info  -- The current ip info
 * Returns      : none
*******************************************************************************/
err_t dhcps_start(struct netif *netif, ip4_addr_t ip)
{
    if (netif == NULL) {
        return ERR_ARG;
    }
    dhcps_netif = netif;

    if (dhcps_netif->dhcps_pcb != NULL) {
        udp_remove(dhcps_netif->dhcps_pcb);
    }

    dhcps_netif->dhcps_pcb = udp_new();
    struct udp_pcb *pcb_dhcps = dhcps_netif->dhcps_pcb;

    if (pcb_dhcps == NULL || ip4_addr_isany_val(ip)) {
        DHCPS_LOG("dhcps_start(): could not obtain pcb\n");
        return ERR_ARG;
    }

    dhcps_netif->dhcps_pcb = pcb_dhcps;

    IP4_ADDR(&broadcast_dhcps, 255, 255, 255, 255);

    server_address.addr = ip.addr;
    DHCP_CHECK_SUBNET_MASK_IP(htonl(s_dhcps_mask.addr));
    DHCP_CHECK_IP_MATCH_SUBNET_MASK(htonl(s_dhcps_mask.addr), htonl(ip.addr));
    dhcps_poll_set(server_address.addr);

    client_address_plus.addr = dhcps_poll.start_ip.addr;

    udp_bind(pcb_dhcps, &netif->ip_addr, DHCPS_SERVER_PORT);
    udp_recv(pcb_dhcps, handle_dhcp, NULL);
#if DHCPS_DEBUG
    DHCPS_LOG("dhcps:dhcps_start->udp_recv function Set a receive callback handle_dhcp for UDP_PCB pcb_dhcps\n");
#endif
    return ERR_OK;
}

/******************************************************************************
 * FunctionName : dhcps_stop
 * Description  : stop dhcp server function
 * Parameters   : netif -- The current netif addr
 * Returns      : none
*******************************************************************************/
void dhcps_stop(struct netif *netif)
{
    struct netif *apnetif = netif;

    if (apnetif == NULL) {
        printf("dhcps_stop: apnetif == NULL\n");
        return;
    }

    if (apnetif->dhcps_pcb != NULL) {
        udp_disconnect(apnetif->dhcps_pcb);
        udp_remove(apnetif->dhcps_pcb);
        apnetif->dhcps_pcb = NULL;
    }

    list_node *pnode = NULL;
    list_node *pback_node = NULL;
    pnode = plist;

    while (pnode != NULL) {
        pback_node = pnode;
        pnode = pback_node->pnext;
        node_remove_from_list(&plist, pback_node);
        free(pback_node->pnode);
        pback_node->pnode = NULL;
        free(pback_node);
        pback_node = NULL;
    }
}

/******************************************************************************
 * FunctionName : kill_oldest_dhcps_pool
 * Description  : remove the oldest node from list
 * Parameters   : none
 * Returns      : none
*******************************************************************************/
static void kill_oldest_dhcps_pool(void)
{
    /* MESHCOM-PATCH DS-03: rewritten. The original never considered the list head, asserted on a
     * one-node list and unlinked without fixing the head. Victim: the unconfirmed lease with the
     * smallest remaining time; if there is none, the lease with the smallest remaining time. */
    list_node *p = NULL;
    list_node *victim_unconf = NULL;
    list_node *victim_any = NULL;

    for (p = plist; p != NULL; p = p->pnext) {
        struct dhcps_pool *pp = p->pnode;

        if (victim_any == NULL || pp->lease_timer < ((struct dhcps_pool *)victim_any->pnode)->lease_timer) {
            victim_any = p;
        }

        if (pp->lease_timer <= DHCPS_OFFER_HOLD_TICKS &&
            (victim_unconf == NULL ||
             pp->lease_timer < ((struct dhcps_pool *)victim_unconf->pnode)->lease_timer)) {
            victim_unconf = p;
        }
    }

    list_node *victim = (victim_unconf != NULL) ? victim_unconf : victim_any;

    if (victim == NULL) {
        return;
    }

    node_remove_from_list(&plist, victim);
    free(victim->pnode);
    victim->pnode = NULL;
    free(victim);
}

/******************************************************************************
 * FunctionName : dhcps_coarse_tmr
 * Description  : the lease time count
 * Parameters   : none
 * Returns      : none
*******************************************************************************/
void dhcps_coarse_tmr(void)
{
    u8_t num_dhcps_pool = 0;
    list_node *pback_node = NULL;
    list_node *pnode = NULL;
    struct dhcps_pool *pdhcps_pool = NULL;
    pnode = plist;

    while (pnode != NULL) {
        pdhcps_pool = pnode->pnode;
        pdhcps_pool->lease_timer --;

        if (pdhcps_pool->lease_timer == 0) {
            pback_node = pnode;
            pnode = pback_node->pnext;
            node_remove_from_list(&plist, pback_node);
            free(pback_node->pnode);
            pback_node->pnode = NULL;
            free(pback_node);
            pback_node = NULL;
        } else {
            pnode = pnode ->pnext;
            num_dhcps_pool ++;
        }
    }

    if (num_dhcps_pool > MAX_STATION_NUM) {
        kill_oldest_dhcps_pool();
    }
}

/******************************************************************************
 * FunctionName : dhcp_search_ip_on_mac
 * Description  : Search ip address based on mac address
 * Parameters   : mac -- The MAC addr
 *				  ip  -- The IP info
 * Returns      : true or false
*******************************************************************************/
bool dhcp_search_ip_on_mac(u8_t *mac, ip4_addr_t *ip)
{
    struct dhcps_pool *pdhcps_pool = NULL;
    list_node *pback_node = NULL;
    bool ret = false;

    for (pback_node = plist; pback_node != NULL; pback_node = pback_node->pnext) {
        pdhcps_pool = pback_node->pnode;

        if (memcmp(pdhcps_pool->mac, mac, sizeof(pdhcps_pool->mac)) == 0) {
            memcpy(&ip->addr, &pdhcps_pool->ip.addr, sizeof(pdhcps_pool->ip.addr));
            ret = true;
            break;
        }
    }

    return ret;
}

/******************************************************************************
 * FunctionName : dhcps_dns_setserver
 * Description  : set DNS server address for dhcpserver
 * Parameters   : dnsserver -- The DNS server address
 * Returns      : none
*******************************************************************************/
void
dhcps_dns_setserver(const ip_addr_t *dnsserver)
{
    if (dnsserver != NULL) {
        dns_server = *(ip_2_ip4(dnsserver));
    } else {
        dns_server = *(ip_2_ip4(IP_ADDR_ANY));
    }
}

/******************************************************************************
 * FunctionName : dhcps_dns_getserver
 * Description  : get DNS server address for dhcpserver
 * Parameters   : none
 * Returns      : ip4_addr_t
*******************************************************************************/
ip4_addr_t
dhcps_dns_getserver(void)
{
    return dns_server;
}
#endif // ESP_DHCP

#endif /* ESP32 && IDF 4 */
