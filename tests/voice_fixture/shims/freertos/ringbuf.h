#pragma once
#include <cstdlib>
#include <cstring>
#include <deque>
#include <utility>
#include "freertos/FreeRTOS.h"

using RingbufHandle_t = std::deque<std::pair<void*, size_t>>*;
constexpr int RINGBUF_TYPE_NOSPLIT = 0;
inline RingbufHandle_t xRingbufferCreateWithCaps(size_t, int, unsigned) {
    return new std::deque<std::pair<void*, size_t>>;
}
inline void vRingbufferDeleteWithCaps(RingbufHandle_t buffer) {
    for (const auto& item : *buffer) std::free(item.first);
    delete buffer;
}
inline void* xRingbufferReceive(RingbufHandle_t buffer, size_t* size, TickType_t) {
    if (buffer->empty()) return nullptr;
    auto [item, bytes] = buffer->front();
    buffer->pop_front();
    *size = bytes;
    return item;
}
inline void vRingbufferReturnItem(RingbufHandle_t, void* item) { std::free(item); }
inline int xRingbufferSend(RingbufHandle_t buffer, const void* input, size_t size, TickType_t) {
    auto* copy = std::malloc(size);
    if (!copy) return 0;
    std::memcpy(copy, input, size);
    buffer->emplace_back(copy, size);
    return pdTRUE;
}
