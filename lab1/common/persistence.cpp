//
// Created by andy on 5/20/17.
//

#include <cstdint>
#include <iterator>
#include <vector>
#include "persistence.h"


void serialize_string_uint16(const std::string &s,
                                      std::vector<uint8_t> &data) {
    serialize_uint16_t(s.size(), data);
    std::copy(s.begin(), s.end(), std::back_inserter(data));
}

void serialize_uint64_t(uint64_t s, std::vector<uint8_t> &data) {
    uint32_t hi = (uint32_t) (s >> 32);
    uint32_t lo = (uint32_t) (s & 0xFFFFFFFF);

    serialize_uint32_t(hi, data);
    serialize_uint32_t(lo, data);
}

void serialize_uint32_t(uint32_t s,
                                 std::vector<uint8_t> &data) {
    for (int i = 24; i >= 0; i -= 8) {
        uint8_t byte = (uint8_t) (s >> i);
        data.push_back(byte);
    }
}

void serialize_uint16_t(uint16_t s,
                                 std::vector<uint8_t> &data) {
    data.push_back(s >> 8);
    data.push_back(s & 0xFF);
}


uint64_t deserialize_uint64_t(
        std::vector<uint8_t>::const_iterator &iter) {
    uint64_t hi = deserialize_uint32_t(iter);
    uint64_t lo = deserialize_uint32_t(iter);

    return (hi << 32) + lo;
}

std::string deserialize_string_uint16(
        std::vector<uint8_t>::const_iterator &iter) {
    uint16_t string_length = deserialize_uint16_t(iter);

    std::string s(iter, std::next(iter, string_length));

    iter = std::next(iter, string_length);
    return s;
}

void serialize_uint8_t(uint8_t s, std::vector<uint8_t> &data) {
    data.push_back(s);
}

uint8_t deserialize_uint8_t(
        std::vector<uint8_t>::const_iterator &iter) {
    return *iter++;
}
