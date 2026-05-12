#pragma once

#include <cstdint>

constexpr inline size_t max_periods_per_packet {10};

struct __attribute__((packed)) BikePacket {
    uint64_t seq_num;
    uint64_t periods_buf_us[max_periods_per_packet];
    uint8_t periods_buf_len;
};

static_assert(sizeof(BikePacket) <= 250);
