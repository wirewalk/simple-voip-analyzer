#include "voip.h"
#include <stdio.h>

int voip_capture_start(const char *iface, voip_packet_cb cb, void *user_data) {
    (void)iface; (void)cb; (void)user_data;
    fprintf(stderr, "Захват отключён (собрано без libpcap)\n");
    return -1;
}

void voip_capture_stop(void) {}

int voip_capture_dispatch(void) { return 0; }
