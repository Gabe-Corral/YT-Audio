#include "audio.hpp"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/samplefmt.h>
}

namespace audio {

std::string ffmpeg_error_string(int errnum) {
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(errnum, buffer, sizeof(buffer));
    return std::string(buffer);
}

[[noreturn]] void throw_error(const std::string& message) {
    throw std::runtime_error(message);
}

SdlGuard::SdlGuard() {
    if (SDL_Init(SDL_INIT_AUDIO) != 0) {
        throw_error(std::string("SDL_Init failed: ") + SDL_GetError());
    }
}

SdlGuard::~SdlGuard() {
    SDL_Quit();
}

AudioDevice::~AudioDevice() {
    close();
}

void AudioDevice::open(int freq, Uint8 channels, Uint16 samples) {
    close();

    SDL_AudioSpec desired{};
    desired.freq = freq;
    desired.format = AUDIO_F32SYS;
    desired.channels = channels;
    desired.samples = samples;
    desired.callback = nullptr;

    device_id_ = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained_, 0);
    if (device_id_ == 0) {
        throw_error(std::string("SDL_OpenAudioDevice failed: ") + SDL_GetError());
    }
}

void AudioDevice::close() {
    if (device_id_ != 0) {
        SDL_CloseAudioDevice(device_id_);
        device_id_ = 0;
        obtained_ = {};
    }
}

void AudioDevice::start() const {
    if (device_id_ == 0) {
        throw_error("Audio device is not open.");
    }
    SDL_PauseAudioDevice(device_id_, 0);
}

void AudioDevice::pause() const {
    if (device_id_ == 0) {
        throw_error("Audio device is not open.");
    }
    SDL_PauseAudioDevice(device_id_, 1);
}

void AudioDevice::clear() const {
    if (device_id_ != 0) {
        SDL_ClearQueuedAudio(device_id_);
    }
}

void AudioDevice::queue(const std::uint8_t* data, std::uint32_t size) const {
    if (device_id_ == 0) {
        throw_error("Audio device is not open.");
    }

    if (SDL_QueueAudio(device_id_, data, size) != 0) {
        throw_error(std::string("SDL_QueueAudio failed: ") + SDL_GetError());
    }
}

std::uint32_t AudioDevice::queued_size() const {
    if (device_id_ == 0) {
        return 0;
    }
    return SDL_GetQueuedAudioSize(device_id_);
}

SDL_AudioDeviceID AudioDevice::id() const {
    return device_id_;
}

const SDL_AudioSpec& AudioDevice::spec() const {
    return obtained_;
}

AudioStreamPlayer::AudioStreamPlayer()
    : sdl_guard_(std::make_unique<SdlGuard>()) {
    avformat_network_init();
    audio_device_.open();
}

AudioStreamPlayer::~AudioStreamPlayer() {
    cleanup();
    avformat_network_deinit();
}

void AudioStreamPlayer::play(const std::string& url) {
    cleanup();
    open_stream(url);
    setup_decoder();
    setup_resampler();

    packet_ = av_packet_alloc();
    frame_ = av_frame_alloc();

    if (packet_ == nullptr || frame_ == nullptr) {
        throw_error("Failed to allocate packet/frame.");
    }

    std::cout << "Streaming from: " << url << '\n';
    std::cout << "Press Ctrl+C to stop.\n";

    bool audio_started = false;

    while (true) {
        int ret = av_read_frame(format_ctx_, packet_);

        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                break;
            }

            std::cerr << "av_read_frame warning: "
                      << ffmpeg_error_string(ret) << '\n';
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        if (packet_->stream_index != audio_stream_index_) {
            av_packet_unref(packet_);
            continue;
        }

        ret = avcodec_send_packet(codec_ctx_, packet_);
        av_packet_unref(packet_);

        if (ret < 0) {
            std::cerr << "avcodec_send_packet warning: "
                      << ffmpeg_error_string(ret) << '\n';
            continue;
        }

        while (true) {
            ret = avcodec_receive_frame(codec_ctx_, frame_);

            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            }

            if (ret < 0) {
                std::cerr << "avcodec_receive_frame warning: "
                          << ffmpeg_error_string(ret) << '\n';
                break;
            }

            queue_converted_frame();

            if (!audio_started && audio_device_.queued_size() > 0) {
                audio_device_.start();
                audio_started = true;
            }
        }
    }

    flush_decoder();
    wait_for_playback_finish();
}

void AudioStreamPlayer::stop() {
    audio_device_.clear();
    audio_device_.pause();
    cleanup();
}

void AudioStreamPlayer::open_stream(const std::string& url) {
    AVDictionary* options = nullptr;
    av_dict_set(&options, "reconnect", "1", 0);
    av_dict_set(&options, "reconnect_streamed", "1", 0);
    av_dict_set(&options, "reconnect_delay_max", "5", 0);

    const int ret = avformat_open_input(&format_ctx_, url.c_str(), nullptr, &options);
    av_dict_free(&options);

    if (ret < 0) {
        throw_error("avformat_open_input failed: " + ffmpeg_error_string(ret));
    }

    const int stream_info_ret = avformat_find_stream_info(format_ctx_, nullptr);
    if (stream_info_ret < 0) {
        throw_error("avformat_find_stream_info failed: " +
                    ffmpeg_error_string(stream_info_ret));
    }

    audio_stream_index_ =
        av_find_best_stream(format_ctx_, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);

    if (audio_stream_index_ < 0) {
        throw_error("No audio stream found: " +
                    ffmpeg_error_string(audio_stream_index_));
    }
}

void AudioStreamPlayer::setup_decoder() {
    AVStream* audio_stream = format_ctx_->streams[audio_stream_index_];
    const AVCodec* decoder = avcodec_find_decoder(audio_stream->codecpar->codec_id);

    if (decoder == nullptr) {
        throw_error("No suitable decoder found.");
    }

    codec_ctx_ = avcodec_alloc_context3(decoder);
    if (codec_ctx_ == nullptr) {
        throw_error("avcodec_alloc_context3 failed.");
    }

    int ret = avcodec_parameters_to_context(codec_ctx_, audio_stream->codecpar);
    if (ret < 0) {
        throw_error("avcodec_parameters_to_context failed: " +
                    ffmpeg_error_string(ret));
    }

    ret = avcodec_open2(codec_ctx_, decoder, nullptr);
    if (ret < 0) {
        throw_error("avcodec_open2 failed: " + ffmpeg_error_string(ret));
    }
}

void AudioStreamPlayer::setup_resampler() {
    AVChannelLayout out_ch_layout{};
    av_channel_layout_default(&out_ch_layout, audio_device_.spec().channels);

    AVChannelLayout in_ch_layout{};
    int ret = 0;

    if (codec_ctx_->ch_layout.nb_channels > 0) {
        ret = av_channel_layout_copy(&in_ch_layout, &codec_ctx_->ch_layout);
        if (ret < 0) {
            av_channel_layout_uninit(&out_ch_layout);
            throw_error("av_channel_layout_copy failed: " +
                        ffmpeg_error_string(ret));
        }
    } else {
        av_channel_layout_default(&in_ch_layout, 2);
    }

    ret = swr_alloc_set_opts2(
        &swr_ctx_,
        &out_ch_layout,
        AV_SAMPLE_FMT_FLT,
        audio_device_.spec().freq,
        &in_ch_layout,
        codec_ctx_->sample_fmt,
        codec_ctx_->sample_rate,
        0,
        nullptr
    );

    av_channel_layout_uninit(&in_ch_layout);
    av_channel_layout_uninit(&out_ch_layout);

    if (ret < 0 || swr_ctx_ == nullptr) {
        throw_error("swr_alloc_set_opts2 failed: " + ffmpeg_error_string(ret));
    }

    ret = swr_init(swr_ctx_);
    if (ret < 0) {
        throw_error("swr_init failed: " + ffmpeg_error_string(ret));
    }
}

void AudioStreamPlayer::queue_converted_frame() {
    const int max_output_samples = av_rescale_rnd(
        swr_get_delay(swr_ctx_, codec_ctx_->sample_rate) + frame_->nb_samples,
        audio_device_.spec().freq,
        codec_ctx_->sample_rate,
        AV_ROUND_UP
    );

    std::uint8_t* output_data = nullptr;
    int output_line_size = 0;

    int ret = av_samples_alloc(
        &output_data,
        &output_line_size,
        audio_device_.spec().channels,
        max_output_samples,
        AV_SAMPLE_FMT_FLT,
        0
    );

    if (ret < 0) {
        av_frame_unref(frame_);
        throw_error("av_samples_alloc failed: " + ffmpeg_error_string(ret));
    }

    const int converted_samples = swr_convert(
        swr_ctx_,
        &output_data,
        max_output_samples,
        const_cast<const std::uint8_t**>(frame_->extended_data),
        frame_->nb_samples
    );

    if (converted_samples < 0) {
        av_freep(&output_data);
        av_frame_unref(frame_);
        std::cerr << "swr_convert warning: "
                  << ffmpeg_error_string(converted_samples) << '\n';
        return;
    }

    const int output_buffer_size = av_samples_get_buffer_size(
        &output_line_size,
        audio_device_.spec().channels,
        converted_samples,
        AV_SAMPLE_FMT_FLT,
        1
    );

    if (output_buffer_size > 0) {
        while (audio_device_.queued_size() > 1024 * 1024) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }

        audio_device_.queue(
            output_data,
            static_cast<std::uint32_t>(output_buffer_size)
        );
    }

    av_freep(&output_data);
    av_frame_unref(frame_);
}

void AudioStreamPlayer::flush_decoder() {
    if (codec_ctx_ == nullptr || frame_ == nullptr) {
        return;
    }

    avcodec_send_packet(codec_ctx_, nullptr);

    while (true) {
        const int ret = avcodec_receive_frame(codec_ctx_, frame_);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        }

        if (ret < 0) {
            break;
        }

        queue_converted_frame();
    }
}

void AudioStreamPlayer::wait_for_playback_finish() const {
    while (audio_device_.queued_size() > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void AudioStreamPlayer::cleanup() {
    if (frame_ != nullptr) {
        av_frame_free(&frame_);
    }

    if (packet_ != nullptr) {
        av_packet_free(&packet_);
    }

    if (swr_ctx_ != nullptr) {
        swr_free(&swr_ctx_);
    }

    if (codec_ctx_ != nullptr) {
        avcodec_free_context(&codec_ctx_);
    }

    if (format_ctx_ != nullptr) {
        avformat_close_input(&format_ctx_);
    }

    audio_stream_index_ = -1;
}

} // namespace audio
