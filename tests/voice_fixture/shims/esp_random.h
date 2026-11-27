#pragma once
#include <cstddef>
#include <cstdint>
void esp_fill_random(void* destination, size_t size);
inline uint32_t esp_random() { return 0; }
