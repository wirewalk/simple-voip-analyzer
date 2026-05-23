#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>

#include "voip.h"

static volatile int g_running = 1;

static voip_stream_table_t g_streams;

static void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
}

static void on_packet(const uint8_t *raw, size_t len,
                      const struct timespec *ts, void *user_data) {
    (void)ts;
    (void)user_data;

    voip_parsed_packet_t pkt;
    if (voip_parse_rtp(raw, len, &pkt) != 0)
        return;

    voip_stream_t *stream = voip_stream_find_or_create(&g_streams, pkt.rtp.ssrc);
    if (!stream)
        return;

    voip_stream_update(stream, &pkt);
}

int main(int argc, char *argv[]) {
    const char *iface = NULL;

    if (argc > 1) {
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            fprintf(stderr, "Usage: %s [interface]\n", argv[0]);
            fprintf(stderr, "  interface - сетевой интерфейс (по умолчанию - любой)\n");
            return 0;
        }
        iface = argv[1];
    }

    voip_stream_table_init(&g_streams);
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    printf("simple-voip-analyzer: слушаем RTP на %s\n",
           iface ? iface : "всех интерфейсах");
    printf("Ctrl+C для остановки\n\n");

    if (voip_capture_start(iface, on_packet, NULL) != 0) {
        fprintf(stderr, "Ошибка: не удалось запустить захват\n");
        return 1;
    }

    while (g_running) {
        struct timespec ts = {0, 500000000};
        nanosleep(&ts, NULL);
        voip_capture_dispatch();
        voip_stream_table_print(&g_streams);
    }

    voip_capture_stop();
    printf("\n=== Итоговая статистика ===\n");
    voip_stream_table_print(&g_streams);
    voip_stream_table_free(&g_streams);

    return 0;
}
