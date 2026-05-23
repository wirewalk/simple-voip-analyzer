#include "voip.h"
#include <string.h>

int voip_parse_rtp(const uint8_t *data, size_t len, voip_parsed_packet_t *out) {
    if (!data || !out || len < 12)
        return -1;

    memset(out, 0, sizeof(*out));

    uint8_t byte0 = data[0];
    out->rtp.version = (byte0 >> 6) & 0x03;
    if (out->rtp.version != 2)
        return -1;

    out->rtp.padding    = (byte0 >> 5) & 0x01;
    out->rtp.extension  = (byte0 >> 4) & 0x01;
    out->rtp.csrc_count = byte0 & 0x0F;

    uint8_t byte1 = data[1];
    out->rtp.marker       = (byte1 >> 7) & 0x01;
    out->rtp.payload_type = byte1 & 0x7F;

    out->rtp.seq_num   = (uint16_t)((data[2] << 8) | data[3]);
    out->rtp.timestamp = (uint32_t)((data[4] << 24) | (data[5] << 16) |
                                     (data[6] << 8) | data[7]);
    out->rtp.ssrc      = (uint32_t)((data[8] << 24) | (data[9] << 16) |
                                     (data[10] << 8) | data[11]);

    size_t header_len = 12 + out->rtp.csrc_count * 4;
    if (len < header_len)
        return -1;

    if (out->rtp.extension) {
        if (header_len + 4 > len)
            return -1;
        uint16_t ext_len = (uint16_t)((data[header_len + 2] << 8) |
                                       data[header_len + 3]);
        header_len += 4 + ext_len * 4;
        if (len < header_len)
            return -1;
    }

    out->payload_len = len - header_len;
    if (out->rtp.padding && out->payload_len > 0) {
        uint8_t pad_len = data[len - 1];
        if (pad_len < out->payload_len)
            out->payload_len -= pad_len;
        else
            out->payload_len = 0;
    }

    out->payload = data + header_len;
    clock_gettime(CLOCK_MONOTONIC, &out->arrival_time);
    return 0;
}
