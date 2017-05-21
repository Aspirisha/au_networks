//
// Created by andy on 5/20/17.
//

#ifndef LAB1_PERSISTENCE_H
#define LAB1_PERSISTENCE_H

void serialize_uint32_t(uint32_t s, std::vector<uint8_t> &data);
void serialize_uint16_t(uint16_t s, std::vector<uint8_t> &data);
void serialize_uint8_t(uint8_t s, std::vector<uint8_t> &data);
void serialize_uint64_t(uint64_t s, std::vector<uint8_t> &data);
void serialize_string_uint16(const std::string &s, std::vector<uint8_t> &data);


template <class T>
uint16_t deserialize_uint16_t(T &iter) {
    uint16_t hi = *iter++;
    uint16_t lo = *iter++;

    return (hi << 8) + lo;
}

template <class T>
uint32_t deserialize_uint32_t(T &iter) {
    uint32_t result = 0;
    for (int i = 24; i >= 0; i -= 8) {
        uint32_t byte = *iter++;
        result |= (byte << i);
    }

    return result;
}
uint8_t deserialize_uint8_t(std::vector<uint8_t>::const_iterator &iter);
uint64_t deserialize_uint64_t(std::vector<uint8_t>::const_iterator &iter);
std::string deserialize_string_uint16(std::vector<uint8_t>::const_iterator &iter);

#endif //LAB1_PERSISTENCE_H
