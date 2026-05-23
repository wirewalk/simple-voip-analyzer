#include "voip.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <time.h>

static uint32_t stream_hash(uint32_t ssrc) {
    uint32_t h = ssrc;
    h = ((h >> 16) ^ h) * 0x45d9f3b;
    h = ((h >> 16) ^ h) * 0x45d9f3b;
    h = (h >> 16) ^ h;
    return h % VOIP_STREAM_TABLE_SIZE;
}

void voip_stream_table_init(voip_stream_table_t *table) {
    memset(table, 0, sizeof(*table));
}

voip_stream_t *voip_stream_find_or_create(voip_stream_table_t *table,
                                           uint32_t ssrc) {
    uint32_t idx = stream_hash(ssrc);
    voip_stream_t *s = table->buckets[idx];
    while (s) {
        if (s->ssrc == ssrc)
            return s;
        s = s->next;
    }

    voip_stream_t *ns = calloc(1, sizeof(voip_stream_t));
    if (!ns)
        return NULL;

    ns->ssrc = ssrc;
    ns->expected_seq = UINT16_MAX;
    ns->stats.mos = 4.5;
    clock_gettime(CLOCK_MONOTONIC, &ns->stats.first_seen);

    ns->next = table->buckets[idx];
    table->buckets[idx] = ns;
    table->count++;
    return ns;
}

static void seq_bitmap_set(voip_stream_t *s, uint16_t seq) {
    uint32_t bit = seq & 0x7F;
    s->seq_bitmap[bit / 32] |= (1U << (bit % 32));
}

static int seq_bitmap_test(const voip_stream_t *s, uint16_t seq) {
    uint32_t bit = seq & 0x7F;
    return (s->seq_bitmap[bit / 32] >> (bit % 32)) & 1;
}

static double ts_to_ms(const struct timespec *ts) {
    return ts->tv_sec * 1000.0 + ts->tv_nsec / 1e6;
}

void voip_stream_update(voip_stream_t *stream, const voip_parsed_packet_t *pkt) {
    voip_stream_stats_t *st = &stream->stats;
    uint16_t seq = pkt->rtp.seq_num;

    if (st->received == 0) {
        stream->expected_seq = seq + 1;
        stream->last_rtp_ts  = pkt->rtp.timestamp;
        stream->last_arrival = pkt->arrival_time;
        st->min_payload = (uint32_t)pkt->payload_len;
        st->max_payload = (uint32_t)pkt->payload_len;
        st->avg_payload = (uint32_t)pkt->payload_len;
        st->received = 1;
        seq_bitmap_set(stream, seq);
        return;
    }

    if (seq_bitmap_test(stream, seq)) {
        st->duplicates++;
        return;
    }

    int16_t diff = (int16_t)(seq - stream->expected_seq);
    if (diff == 0) {
        stream->expected_seq = seq + 1;
    } else if (diff > 0) {
        st->lost += diff;
        stream->expected_seq = seq + 1;
    } else {
        st->reordered++;
        st->lost = (st->lost > 0) ? st->lost - 1 : 0;
    }

    seq_bitmap_set(stream, seq);

    double d_arrival = ts_to_ms(&pkt->arrival_time) - ts_to_ms(&stream->last_arrival);
    double d_rtp = 0;
    uint32_t clock_rate = 8000;
    if (pkt->rtp.payload_type == 102 || pkt->rtp.payload_type == 111)
        clock_rate = 48000;
    int32_t ts_diff = (int32_t)(pkt->rtp.timestamp - stream->last_rtp_ts);
    d_rtp = (ts_diff / (double)clock_rate) * 1000.0;

    double d = fabs(d_arrival - d_rtp);
    stream->jitter += (d - stream->jitter) / 16.0;
    st->jitter_ms = stream->jitter;

    stream->last_rtp_ts  = pkt->rtp.timestamp;
    stream->last_arrival = pkt->arrival_time;

    st->received++;
    uint32_t plen = (uint32_t)pkt->payload_len;
    if (plen < st->min_payload) st->min_payload = plen;
    if (plen > st->max_payload) st->max_payload = plen;
    st->avg_payload = st->avg_payload + (plen - st->avg_payload) / st->received;

    uint32_t total = st->received + st->lost;
    st->loss_rate = total > 0 ? (st->lost * 100.0 / total) : 0.0;

    double loss_penalty = 0.9 * st->loss_rate;
    double jitter_penalty = 0.05 * (st->jitter_ms / 10.0);
    st->mos = 4.5 - loss_penalty - jitter_penalty;
    if (st->mos < 1.0) st->mos = 1.0;

    st->last_seen = pkt->arrival_time;
}

const char *voip_codec_name(uint8_t pt) {
    switch (pt) {
        case 0:  return "PCMU (G.711u)";
        case 8:  return "PCMA (G.711a)";
        case 9:  return "G.722";
        case 18: return "G.729";
        case 96: return "Opus (dyn)";
        case 97: return "iLBC (dyn)";
        case 102: return "Opus";
        case 111: return "Opus/SILK";
        default: return "unknown";
    }
}

void voip_format_duration(const struct timespec *start, const struct timespec *end,
                          char *buf, size_t bufsize) {
    double sec = (end->tv_sec - start->tv_sec) +
                 (end->tv_nsec - start->tv_nsec) / 1e9;
    int h = (int)(sec / 3600);
    int m = (int)((sec - h * 3600) / 60);
    int s = (int)(sec - h * 3600 - m * 60);
    snprintf(buf, bufsize, "%02d:%02d:%02d", h, m, s);
}

void voip_stream_table_print(const voip_stream_table_t *table) {
    printf("\033[H\033[2J");
    printf("=== RTP Streams (%d active) ===\n\n", table->count);
    printf("%-12s %-18s %-6s %-10s %5s %5s %6s %6s %4s %s\n",
           "SSRC", "Codec", "Pkts", "Lost", "Loss%", "Jitter",
           "MOS", "Reord", "Dup", "Duration");
    printf("%-12s %-18s %-6s %-10s %5s %5s %6s %6s %4s %s\n",
           "----", "-----", "----", "----", "-----", "------",
           "---", "-----", "---", "--------");

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    for (int i = 0; i < VOIP_STREAM_TABLE_SIZE; i++) {
        voip_stream_t *s = table->buckets[i];
        while (s) {
            char dur[16];
            voip_format_duration(&s->stats.first_seen, &now, dur, sizeof(dur));
            printf("%08x     %-18s %-6u %-10u %5.1f %5.1f %6.2f %6u %4u %s\n",
                   s->ssrc,
                   voip_codec_name(s->stats.received > 0 ? 0 : 0),
                   s->stats.received,
                   s->stats.lost,
                   s->stats.loss_rate,
                   s->stats.jitter_ms,
                   s->stats.mos,
                   s->stats.reordered,
                   s->stats.duplicates,
                   dur);
            s = s->next;
        }
    }
}

void voip_stream_table_free(voip_stream_table_t *table) {
    for (int i = 0; i < VOIP_STREAM_TABLE_SIZE; i++) {
        voip_stream_t *s = table->buckets[i];
        while (s) {
            voip_stream_t *next = s->next;
            free(s);
            s = next;
        }
        table->buckets[i] = NULL;
    }
    table->count = 0;
}
