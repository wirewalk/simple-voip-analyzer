#include "voip.h"

#ifdef HAVE_PCAP

#include <pcap.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <time.h>

extern volatile int g_running;

static pcap_t          *g_pcap = NULL;
static voip_packet_cb   g_cb   = NULL;
static void            *g_cb_data = NULL;

#define MAX_SNIFF_SNAPLEN 65535

static void pcap_handler_wrap(unsigned char *user,
                              const struct pcap_pkthdr *hdr,
                              const unsigned char *bytes) {
    (void)user;
    if (!g_cb) return;

    struct timespec ts;
    ts.tv_sec  = hdr->ts.tv_sec;
    ts.tv_nsec = hdr->ts.tv_usec * 1000;

    int link_type = pcap_datalink(g_pcap);
    int offset = -1;

    switch (link_type) {
        case DLT_EN10MB: offset = 14; break;
        case DLT_LINUX_SLL: offset = 16; break;
        case DLT_RAW: offset = 0; break;
        case DLT_NULL: offset = 4; break;
        default: return;
    }

    if ((int)hdr->caplen < offset)
        return;

    const uint8_t *ip_pkt = bytes + offset;
    uint8_t ihl;

    if ((int)hdr->caplen > offset + 1) {
        ihl = (ip_pkt[0] & 0x0F) * 4;
    } else {
        return;
    }

    if ((int)hdr->caplen < offset + ihl + 8)
        return;

    uint8_t proto = ip_pkt[9];
    if (proto != 17)
        return;

    const uint8_t *udp = ip_pkt + ihl;
    uint16_t udp_len = (uint16_t)((udp[4] << 8) | udp[5]);
    if (udp_len < 8)
        return;

    const uint8_t *payload = udp + 8;
    size_t payload_len = udp_len - 8;
    if (payload_len < 12)
        return;

    if ((payload[0] & 0xC0) != 0x80)
        return;

    g_cb(payload, payload_len, &ts, g_cb_data);
}

int voip_capture_start(const char *iface, voip_packet_cb cb, void *user_data) {
    char errbuf[PCAP_ERRBUF_SIZE] = {0};

    g_cb = cb;
    g_cb_data = user_data;

    if (iface) {
        g_pcap = pcap_open_live(iface, MAX_SNIFF_SNAPLEN, 1, 1000, errbuf);
    } else {
        pcap_if_t *devs = NULL;
        if (pcap_findalldevs(&devs, errbuf) == -1 || !devs) {
            fprintf(stderr, "Ошибка поиска интерфейсов: %s\n", errbuf);
            return -1;
        }
        g_pcap = pcap_open_live(devs[0].name, MAX_SNIFF_SNAPLEN, 1, 1000, errbuf);
        pcap_freealldevs(devs);
    }

    if (!g_pcap) {
        fprintf(stderr, "Ошибка pcap_open_live: %s\n", errbuf);
        return -1;
    }

    struct bpf_program fp;
    if (pcap_compile(g_pcap, &fp, "udp", 1, PCAP_NETMASK_UNKNOWN) == -1) {
        fprintf(stderr, "Ошибка pcap_compile: %s\n", pcap_geterr(g_pcap));
        pcap_close(g_pcap);
        g_pcap = NULL;
        return -1;
    }

    if (pcap_setfilter(g_pcap, &fp) == -1) {
        fprintf(stderr, "Ошибка pcap_setfilter: %s\n", pcap_geterr(g_pcap));
        pcap_freecode(&fp);
        pcap_close(g_pcap);
        g_pcap = NULL;
        return -1;
    }
    pcap_freecode(&fp);

    if (pcap_setnonblock(g_pcap, 1, errbuf) == -1) {
        fprintf(stderr, "Ошибка pcap_setnonblock: %s\n", errbuf);
        pcap_close(g_pcap);
        g_pcap = NULL;
        return -1;
    }

    return 0;
}

void voip_capture_stop(void) {
    if (g_pcap) {
        pcap_breakloop(g_pcap);
        pcap_close(g_pcap);
        g_pcap = NULL;
    }
}

int voip_capture_dispatch(void) {
    if (!g_pcap) return -1;
    return pcap_dispatch(g_pcap, 64, pcap_handler_wrap, NULL);
}

#else

int voip_capture_start(const char *iface, voip_packet_cb cb, void *user_data) {
    (void)iface; (void)cb; (void)user_data;
    return -1;
}

void voip_capture_stop(void) {}

int voip_capture_dispatch(void) { return 0; }

#endif
