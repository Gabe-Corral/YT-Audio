// audio_stream_player.hpp
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <SDL2/SDL.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
}

namespace audio {

std::string ffmpeg_error_string(int errnum);
[[noreturn]] void throw_error(const std::string& message);

class SdlGuard {
public:
    SdlGuard();
    ~SdlGuard();

    SdlGuard(const SdlGuard&) = delete;
    SdlGuard& operator=(const SdlGuard&) = delete;
};

class AudioDevice {
public:
    AudioDevice() = default;
    ~AudioDevice();

    AudioDevice(const AudioDevice&) = delete;
    AudioDevice& operator=(const AudioDevice&) = delete;

    void open(int freq = 48000, Uint8 channels = 2, Uint16 samples = 4096);
    void close();

    void start() const;
    void pause() const;
    void clear() const;
    void queue(const std::uint8_t* data, std::uint32_t size) const;

    [[nodiscard]] std::uint32_t queued_size() const;
    [[nodiscard]] SDL_AudioDeviceID id() const;
    [[nodiscard]] const SDL_AudioSpec& spec() const;

private:
    SDL_AudioDeviceID device_id_ = 0;
    SDL_AudioSpec obtained_{};
};

class AudioStreamPlayer {
public:
    AudioStreamPlayer();
    ~AudioStreamPlayer();

    AudioStreamPlayer(const AudioStreamPlayer&) = delete;
    AudioStreamPlayer& operator=(const AudioStreamPlayer&) = delete;

    void play(const std::string& url);
    void stop();

private:
    void open_stream(const std::string& url);
    void setup_decoder();
    void setup_resampler();
    void queue_converted_frame();
    void flush_decoder();
    void wait_for_playback_finish() const;
    void cleanup();

private:
    std::unique_ptr<SdlGuard> sdl_guard_;
    AudioDevice audio_device_;

    AVFormatContext* format_ctx_ = nullptr;
    AVCodecContext* codec_ctx_ = nullptr;
    SwrContext* swr_ctx_ = nullptr;
    AVPacket* packet_ = nullptr;
    AVFrame* frame_ = nullptr;

    int audio_stream_index_ = -1;
};

} // namespace audio
