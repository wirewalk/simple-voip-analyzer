#include "voip.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;

#define ASSERT(cond, msg) do { \
    tests_run++; \
    if (cond) { tests_passed++; } \
    else { fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, msg); } \
} while(0)

/* Минимальный RTP-пакет: V=2, PT=0(PCMU), seq=42, ts=160, ssrc=0xAABBCCDD */
static void test_parse_minimal_rtp(void) {
    uint8_t raw[] = {
        0x80,                   /* V=2, P=0, X=0, CC=0 */
        0x00,                   /* M=0, PT=0 (PCMU) */
        0x00, 0x2A,             /* seq = 42 */
        0x00, 0x00, 0x00, 0xA0, /* timestamp = 160 */
        0xAA, 0xBB, 0xCC, 0xDD, /* ssrc */
        0x01, 0x02, 0x03, 0x04, /* payload */
        0x05, 0x06, 0x07, 0x08,
    };

    voip_parsed_packet_t pkt;
    int rc = voip_parse_rtp(raw, sizeof(raw), &pkt);
    ASSERT(rc == 0, "parse minimal RTP should succeed");
    ASSERT(pkt.rtp.version == 2, "version should be 2");
    ASSERT(pkt.rtp.payload_type == 0, "payload type should be 0 (PCMU)");
    ASSERT(pkt.rtp.seq_num == 42, "seq should be 42");
    ASSERT(pkt.rtp.timestamp == 160, "timestamp should be 160");
    ASSERT(pkt.rtp.ssrc == 0xAABBCCDD, "ssrc should match");
    ASSERT(pkt.payload_len == 8, "payload len should be 8");
    ASSERT(pkt.payload[0] == 0x01, "first payload byte");
}

static void test_parse_with_csrc(void) {
    uint8_t raw[] = {
        0x82,                   /* V=2, CC=2 */
        0x08,                   /* M=0, PT=8 (PCMA) */
        0x00, 0x01,             /* seq = 1 */
        0x00, 0x00, 0x00, 0x00, /* timestamp = 0 */
        0x11, 0x22, 0x33, 0x44, /* ssrc */
        0x01, 0x01, 0x01, 0x01, /* CSRC 1 */
        0x02, 0x02, 0x02, 0x02, /* CSRC 2 */
        0xAA, 0xBB,             /* payload */
    };

    voip_parsed_packet_t pkt;
    int rc = voip_parse_rtp(raw, sizeof(raw), &pkt);
    ASSERT(rc == 0, "parse RTP with CSRC should succeed");
    ASSERT(pkt.rtp.csrc_count == 2, "csrc_count should be 2");
    ASSERT(pkt.rtp.payload_type == 8, "payload type should be 8 (PCMA)");
    ASSERT(pkt.payload_len == 2, "payload len should be 2");
    ASSERT(pkt.payload[0] == 0xAA, "first payload byte");
}

static void test_parse_with_extension(void) {
    uint8_t raw[] = {
        0x90,                   /* V=2, X=1 */
        0x60,                   /* M=0, PT=96 */
        0x00, 0x0A,             /* seq = 10 */
        0x00, 0x00, 0x10, 0x00, /* timestamp */
        0xDE, 0xAD, 0xBE, 0xEF, /* ssrc */
        0xAB, 0xCD, 0x00, 0x01, /* ext header: profile=0xABCD, length=1 (4 bytes) */
        0x11, 0x22, 0x33, 0x44, /* ext data */
        0xCC,                   /* payload */
    };

    voip_parsed_packet_t pkt;
    int rc = voip_parse_rtp(raw, sizeof(raw), &pkt);
    ASSERT(rc == 0, "parse RTP with extension should succeed");
    ASSERT(pkt.rtp.extension == 1, "extension flag should be 1");
    ASSERT(pkt.rtp.payload_type == 96, "payload type should be 96");
    ASSERT(pkt.payload_len == 1, "payload len should be 1");
    ASSERT(pkt.payload[0] == 0xCC, "payload byte");
}

static void test_parse_padding(void) {
    uint8_t raw[] = {
        0xA0,                   /* V=2, P=1 */
        0x00,                   /* PT=0 */
        0x00, 0x01,             /* seq = 1 */
        0x00, 0x00, 0x00, 0x00, /* timestamp */
        0x11, 0x22, 0x33, 0x44, /* ssrc */
        0xAA, 0xBB, 0xCC,       /* payload (3 байта) */
        0xDD, 0x00, 0x03,       /* padding: 3 байта (включая последний) */
    };

    voip_parsed_packet_t pkt;
    int rc = voip_parse_rtp(raw, sizeof(raw), &pkt);
    ASSERT(rc == 0, "parse padded RTP should succeed");
    ASSERT(pkt.rtp.padding == 1, "padding flag should be 1");
    ASSERT(pkt.payload_len == 3, "payload should exclude padding (3)");
    ASSERT(pkt.payload[0] == 0xAA, "first payload byte");
}

static void test_parse_too_short(void) {
    uint8_t short_pkt[] = { 0x80, 0x00, 0x00, 0x01 };
    voip_parsed_packet_t pkt;
    int rc = voip_parse_rtp(short_pkt, sizeof(short_pkt), &pkt);
    ASSERT(rc == -1, "short packet should fail");
}

static void test_parse_bad_version(void) {
    uint8_t raw[12] = {0};
    raw[0] = 0x40; /* version = 1 */
    voip_parsed_packet_t pkt;
    int rc = voip_parse_rtp(raw, sizeof(raw), &pkt);
    ASSERT(rc == -1, "wrong RTP version should fail");
}

static void test_parse_null(void) {
    voip_parsed_packet_t pkt;
    ASSERT(voip_parse_rtp(NULL, 100, &pkt) == -1, "NULL data should fail");
    ASSERT(voip_parse_rtp((uint8_t*)"x", 100, NULL) == -1, "NULL out should fail");
}

static void test_stream_table_create_find(void) {
    voip_stream_table_t table;
    voip_stream_table_init(&table);
    ASSERT(table.count == 0, "table should start empty");

    voip_stream_t *s1 = voip_stream_find_or_create(&table, 0x11111111);
    ASSERT(s1 != NULL, "create stream 1");
    ASSERT(s1->ssrc == 0x11111111, "ssrc should match");
    ASSERT(table.count == 1, "count should be 1");

    voip_stream_t *s2 = voip_stream_find_or_create(&table, 0x22222222);
    ASSERT(s2 != NULL, "create stream 2");
    ASSERT(table.count == 2, "count should be 2");

    voip_stream_t *found = voip_stream_find_or_create(&table, 0x11111111);
    ASSERT(found == s1, "find existing should return same ptr");
    ASSERT(table.count == 2, "count should still be 2");

    voip_stream_table_free(&table);
}

static void test_stream_update_basic(void) {
    voip_stream_table_t table;
    voip_stream_table_init(&table);

    voip_stream_t *s = voip_stream_find_or_create(&table, 0xAABBCCDD);
    ASSERT(s != NULL, "create stream");

    uint8_t rtp1[] = {
        0x80, 0x00, 0x00, 0x64, 0x00, 0x00, 0x00, 0x00,
        0xAA, 0xBB, 0xCC, 0xDD,
        0x01, 0x02, 0x03, 0x04,
    };
    uint8_t rtp2[] = {
        0x80, 0x00, 0x00, 0x65, 0x00, 0x00, 0x00, 0xA0,
        0xAA, 0xBB, 0xCC, 0xDD,
        0x01, 0x02, 0x03, 0x04,
    };

    voip_parsed_packet_t pkt;
    voip_parse_rtp(rtp1, sizeof(rtp1), &pkt);
    voip_stream_update(s, &pkt);
    ASSERT(s->stats.received == 1, "first packet: received=1");
    ASSERT(s->stats.lost == 0, "first packet: lost=0");

    voip_parse_rtp(rtp2, sizeof(rtp2), &pkt);
    voip_stream_update(s, &pkt);
    ASSERT(s->stats.received == 2, "second packet: received=2");
    ASSERT(s->stats.lost == 0, "second packet: lost=0 (seq+1)");

    voip_stream_table_free(&table);
}

static void test_stream_detect_loss(void) {
    voip_stream_table_t table;
    voip_stream_table_init(&table);
    voip_stream_t *s = voip_stream_find_or_create(&table, 0x1);

    uint8_t rtp1[] = {
        0x80, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x01,
        0xFF,
    };
    uint8_t rtp2[] = {
        0x80, 0x00, 0x00, 0x0E, 0x00, 0x00, 0x00, 0xA0,
        0x00, 0x00, 0x00, 0x01,
        0xFF,
    };

    voip_parsed_packet_t pkt;
    voip_parse_rtp(rtp1, sizeof(rtp1), &pkt);
    voip_stream_update(s, &pkt);
    voip_parse_rtp(rtp2, sizeof(rtp2), &pkt);
    voip_stream_update(s, &pkt);

    ASSERT(s->stats.received == 2, "received 2 packets");
    ASSERT(s->stats.lost == 3, "lost 3 (seq 10->14, gap=3)");
    ASSERT(s->stats.loss_rate > 0, "loss rate > 0");

    voip_stream_table_free(&table);
}

static void test_stream_detect_duplicate(void) {
    voip_stream_table_t table;
    voip_stream_table_init(&table);
    voip_stream_t *s = voip_stream_find_or_create(&table, 0x1);

    uint8_t rtp[] = {
        0x80, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x01,
        0xFF,
    };

    voip_parsed_packet_t pkt;
    voip_parse_rtp(rtp, sizeof(rtp), &pkt);
    voip_stream_update(s, &pkt);
    voip_stream_update(s, &pkt);

    ASSERT(s->stats.received == 1, "received 1 (second is duplicate)");
    ASSERT(s->stats.duplicates == 1, "duplicates = 1");

    voip_stream_table_free(&table);
}

static void test_stream_detect_reorder(void) {
    voip_stream_table_t table;
    voip_stream_table_init(&table);
    voip_stream_t *s = voip_stream_find_or_create(&table, 0x1);

    uint8_t rtp_a[] = {
        0x80, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x01, 0xFF,
    };
    uint8_t rtp_b[] = {
        0x80, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0xA0,
        0x00, 0x00, 0x00, 0x01, 0xFF,
    };
    uint8_t rtp_c[] = {
        0x80, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x01, 0x40,
        0x00, 0x00, 0x00, 0x01, 0xFF,
    };

    voip_parsed_packet_t pkt;
    voip_parse_rtp(rtp_a, sizeof(rtp_a), &pkt);
    voip_stream_update(s, &pkt);
    voip_parse_rtp(rtp_b, sizeof(rtp_b), &pkt);
    voip_stream_update(s, &pkt);
    voip_parse_rtp(rtp_c, sizeof(rtp_c), &pkt);
    voip_stream_update(s, &pkt);

    ASSERT(s->stats.received == 3, "received 3");
    ASSERT(s->stats.reordered == 1, "1 reordered (seq 11 arrived after 12)");
    ASSERT(s->stats.lost == 0, "lost = 0 (reorder recovered)");

    voip_stream_table_free(&table);
}

static void test_codec_name(void) {
    ASSERT(strcmp(voip_codec_name(0), "PCMU (G.711u)") == 0, "PT 0 = PCMU");
    ASSERT(strcmp(voip_codec_name(8), "PCMA (G.711a)") == 0, "PT 8 = PCMA");
    ASSERT(strcmp(voip_codec_name(9), "G.722") == 0, "PT 9 = G.722");
    ASSERT(strcmp(voip_codec_name(18), "G.729") == 0, "PT 18 = G.729");
    ASSERT(strcmp(voip_codec_name(96), "Opus (dyn)") == 0, "PT 96 = Opus (dyn)");
    ASSERT(strcmp(voip_codec_name(99), "unknown") == 0, "PT 99 = unknown");
}

static void test_mos_degrades_with_loss(void) {
    voip_stream_table_t table;
    voip_stream_table_init(&table);
    voip_stream_t *s = voip_stream_find_or_create(&table, 0x1);

    voip_parsed_packet_t pkt;
    uint8_t rtp[13];
    memset(rtp, 0, sizeof(rtp));
    rtp[0] = 0x80;
    rtp[8] = 0; rtp[9] = 0; rtp[10] = 0; rtp[11] = 0x01;

    rtp[2] = 0; rtp[3] = 0;
    voip_parse_rtp(rtp, sizeof(rtp), &pkt);
    voip_stream_update(s, &pkt);

    rtp[2] = 0; rtp[3] = 10;
    voip_parse_rtp(rtp, sizeof(rtp), &pkt);
    voip_stream_update(s, &pkt);

    ASSERT(s->stats.mos < 4.5, "MOS should degrade with 90% loss");
    ASSERT(s->stats.mos >= 1.0, "MOS should be >= 1.0");

    voip_stream_table_free(&table);
}

int main(void) {
    test_parse_minimal_rtp();
    test_parse_with_csrc();
    test_parse_with_extension();
    test_parse_padding();
    test_parse_too_short();
    test_parse_bad_version();
    test_parse_null();
    test_stream_table_create_find();
    test_stream_update_basic();
    test_stream_detect_loss();
    test_stream_detect_duplicate();
    test_stream_detect_reorder();
    test_codec_name();
    test_mos_degrades_with_loss();

    printf("\n%d / %d тестов пройдено\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
