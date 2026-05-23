#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <sys/stat.h>

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

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s [-r dir] [interface]\n", prog);
    fprintf(stderr, "  -r dir      записывать payload в каталог (по SSRC)\n");
    fprintf(stderr, "  interface   сетевой интерфейс (по умолчанию - любой)\n");
    fprintf(stderr, "\nДля воспроизведения G.711 (PCMU):\n");
    fprintf(stderr, "  ffplay -f mulaw -ar 8000 -ac 1 <file.raw>\n");
    fprintf(stderr, "Для PCMA:\n");
    fprintf(stderr, "  ffplay -f alaw -ar 8000 -ac 1 <file.raw>\n");
}

int main(int argc, char *argv[]) {
    const char *iface = NULL;
    const char *rec_dir = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-r") == 0 && i + 1 < argc) {
            rec_dir = argv[++i];
        } else {
            iface = argv[i];
        }
    }

    voip_stream_table_init(&g_streams);

    if (rec_dir) {
        mkdir(rec_dir, 0755);
        g_streams.record = 1;
        snprintf(g_streams.record_dir, sizeof(g_streams.record_dir), "%s", rec_dir);
        printf("Запись payload в %s/\n", rec_dir);
    }

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

    if (rec_dir) {
        printf("\nФайлы записи в %s/ (G.711: ffplay -f mulaw -ar 8000 -ac 1 <file>)\n",
               rec_dir);
    }

    voip_stream_table_free(&g_streams);

    return 0;
}
