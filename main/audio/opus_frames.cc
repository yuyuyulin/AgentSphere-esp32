#include "audio/opus_frames.h"

#include "esp_opus_dec.h"
#include "esp_opus_enc.h"

namespace agentsphere {

bool OpusFrames::OpenEncoder() {
    Close();
    esp_opus_enc_config_t cfg = ESP_OPUS_ENC_CONFIG_DEFAULT();
    cfg.sample_rate = 16000;
    cfg.channel = 1;
    cfg.bitrate = 24000;
    cfg.frame_duration = ESP_OPUS_ENC_FRAME_DURATION_60_MS;
    cfg.complexity = 3;
    return esp_opus_enc_open(&cfg, sizeof(cfg), &encoder_) == ESP_AUDIO_ERR_OK;
}

bool OpusFrames::OpenDecoder() {
    Close();
    esp_opus_dec_cfg_t cfg = ESP_OPUS_DEC_CONFIG_DEFAULT();
    cfg.sample_rate = 16000;
    cfg.channel = 1;
    cfg.frame_duration = ESP_OPUS_DEC_FRAME_DURATION_60_MS;
    return esp_opus_dec_open(&cfg, sizeof(cfg), &decoder_) == ESP_AUDIO_ERR_OK;
}

void OpusFrames::Close() {
    if (encoder_) esp_opus_enc_close(encoder_);
    if (decoder_) esp_opus_dec_close(decoder_);
    encoder_ = nullptr;
    decoder_ = nullptr;
}

unsigned OpusFrames::Encode() {
    esp_audio_enc_in_frame_t in{};
    in.buffer = reinterpret_cast<uint8_t*>(pcm);
    in.len = sizeof(pcm);
    esp_audio_enc_out_frame_t out{};
    out.buffer = packet;
    out.len = sizeof(packet);
    if (esp_opus_enc_process(encoder_, &in, &out) != ESP_AUDIO_ERR_OK) return 0;
    return out.encoded_bytes <= sizeof(packet) ? out.encoded_bytes : 0;
}

bool OpusFrames::Decode(uint8_t* data, unsigned len) {
    esp_audio_dec_in_raw_t in{};
    in.buffer = data;
    in.len = len;
    esp_audio_dec_out_frame_t out{};
    out.buffer = reinterpret_cast<uint8_t*>(pcm);
    out.len = sizeof(pcm);
    esp_audio_dec_info_t info{};
    return esp_opus_dec_decode(decoder_, &in, &out, &info) == ESP_AUDIO_ERR_OK &&
           in.consumed == len && out.decoded_size == sizeof(pcm);
}

}  // namespace agentsphere
