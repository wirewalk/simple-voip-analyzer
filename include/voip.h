#ifndef VOIP_ANALYZER_H
#define VOIP_ANALYZER_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* RTP-заголовок (RFC 3550) */
typedef struct {
    uint8_t  version;       /* Версия: всегда 2 */
    uint8_t  padding;       /* Флаг выравнивания */
    uint8_t  extension;     /* Флаг расширения */
    uint8_t  csrc_count;    /* Количество CSRC-идентификаторов */
    uint8_t  marker;        /* Маркерный бит (конец фрейма) */
    uint8_t  payload_type;  /* Тип кодека (0 = PCMU, 8 = PCMA, 96-127 = dynamic) */
    uint16_t seq_num;       /* Порядковый номер пакета */
    uint32_t timestamp;     /* Временная метка (частота зависит от кодека) */
    uint32_t ssrc;          /* Идентификатор источника */
} voip_rtp_header_t;

/* Разобранный RTP-пакет */
typedef struct {
    voip_rtp_header_t rtp;
    const uint8_t    *payload;
    size_t            payload_len;
    struct timespec   arrival_time;  /* Время прибытия (ядро) */
} voip_parsed_packet_t;

/* Статистика потока */
typedef struct voip_stream_stats {
    uint32_t received;      /* Принято пакетов */
    uint32_t lost;          /* Потеряно (по gap в seq) */
    uint32_t reordered;     /* Пришли не по порядку */
    uint32_t duplicates;    /* Дубликаты (seq уже был) */
    double   loss_rate;     /* % потерь */
    double   jitter_ms;     /* Джиттер, мс */
    double   mos;           /* Оценка MOS (1.0 - 4.5) */
    uint32_t min_payload;   /* Минимальный размер payload */
    uint32_t max_payload;   /* Максимальный размер payload */
    uint32_t avg_payload;   /* Средний размер payload */
    struct timespec first_seen;
    struct timespec last_seen;
} voip_stream_stats_t;

/* Поток RTP (один SSRC) */
typedef struct voip_stream {
    uint32_t                ssrc;
    struct sockaddr_in      src_addr;
    struct sockaddr_in      dst_addr;
    uint16_t                expected_seq;    /* Ожидаемый следующий seq */
    uint32_t                last_rtp_ts;     /* Последний RTP-timestamp */
    struct timespec         last_arrival;    /* Последнее время прибытия */
    double                  jitter;          /* Текущий джиттер (RFC 3550) */
    voip_stream_stats_t     stats;
    uint32_t                seq_bitmap[4];   /* Битовая карта последних 128 seq */
    uint8_t                 last_pt;         /* Последний payload type */
    FILE                   *rec_file;        /* Файл записи payload */
    struct voip_stream     *next;            /* Связный список (хэш-таблица) */
} voip_stream_t;

/* Хэш-таблица потоков */
#define VOIP_STREAM_TABLE_SIZE 1021

typedef struct {
    voip_stream_t *buckets[VOIP_STREAM_TABLE_SIZE];
    int            count;
    int            record;          /* Флаг записи payload */
    char           record_dir[256]; /* Каталог для файлов записи */
} voip_stream_table_t;

/* API захвата */
typedef void (*voip_packet_cb)(const uint8_t *raw, size_t len,
                               const struct timespec *ts, void *user_data);

int  voip_capture_start(const char *iface, voip_packet_cb cb, void *user_data);
void voip_capture_stop(void);
int  voip_capture_dispatch(void);

/* Разбор RTP-пакета */
int voip_parse_rtp(const uint8_t *data, size_t len, voip_parsed_packet_t *out);

/* Таблица потоков */
void voip_stream_table_init(voip_stream_table_t *table);
voip_stream_t *voip_stream_find_or_create(voip_stream_table_t *table,
                                           uint32_t ssrc);
void voip_stream_table_print(const voip_stream_table_t *table);
void voip_stream_table_free(voip_stream_table_t *table);

/* Обновление статистики потока новым пакетом */
void voip_stream_update(voip_stream_t *stream, const voip_parsed_packet_t *pkt);

/* Имя кодека по payload type */
const char *voip_codec_name(uint8_t pt);

/* Форматирование длительности */
void voip_format_duration(const struct timespec *start, const struct timespec *end,
                          char *buf, size_t bufsize);

#endif
