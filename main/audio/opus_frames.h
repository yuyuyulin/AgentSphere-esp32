#pragma once

#include <cstddef>
#include <cstdint>

namespace agentsphere {

// The worker owns one half-duplex codec at a time. Each packet is one raw
// 60 ms Opus message, with no Ogg or length prefix on the wire.
class OpusFrames {
public:
    static constexpr unsigned kSamples = 960;
    static constexpr unsigned kMaxPacket = 1275;

    bool OpenEncoder();
    bool OpenDecoder();
    void Close();
    unsigned Encode();
    bool Decode(uint8_t* data, unsigned len);

    alignas(16) int16_t pcm[kSamples]{};
    alignas(16) uint8_t packet[kMaxPacket]{};

private:
    void* encoder_ = nullptr;
    void* decoder_ = nullptr;
};

}  // namespace agentsphere
