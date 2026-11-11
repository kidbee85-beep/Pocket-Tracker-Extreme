#pragma once

#include "vendor/dr_mp3/dr_mp3.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace pt::platform::source_live {
// The network worker is the only producer. The source worker is the only
// consumer. No buffer growth or audio-thread work is needed for a live stream.
template<size_t Capacity> class PcmRing {
    static_assert(Capacity > 4096 && Capacity < (uint32_t(1) << 30) && (Capacity&(Capacity-1))==0);
    std::array<float,Capacity * 2> data_{};
    std::atomic<uint32_t> read_{0},write_{0};
public:
    static constexpr size_t memory_bytes = sizeof(float) * Capacity * 2;
    size_t push(const float* data,size_t frames,int channels) {
        const auto w=write_.load(std::memory_order_relaxed),r=read_.load(std::memory_order_acquire);
        const auto n=std::min<size_t>(frames,Capacity-uint32_t(w-r));
        for(size_t i=0;i<n;++i)for(int c=0;c<2;++c)data_[((w+uint32_t(i))%Capacity)*2+c]=data[i*channels+std::min(c,channels-1)];
        write_.store(w+uint32_t(n),std::memory_order_release);return n;
    }
    size_t pop(float* data,size_t frames) {
        const auto r=read_.load(std::memory_order_relaxed),w=write_.load(std::memory_order_acquire);
        const auto n=std::min<size_t>(frames,uint32_t(w-r));
        for(size_t i=0;i<n;++i)for(int c=0;c<2;++c)data[i*2+c]=data_[((r+uint32_t(i))%Capacity)*2+c];
        read_.store(r+uint32_t(n),std::memory_order_release);return n;
    }
    size_t available()const{return uint32_t(write_.load(std::memory_order_acquire)-read_.load(std::memory_order_acquire));}
};

// Low-level dr_mp3 is deliberately used: the file-oriented pull decoder treats
// a short read as EOF. A bounded push buffer survives arbitrary network chunks.
class Mp3Decoder {
    drmp3dec decoder_{};
    std::array<unsigned char,16384> bytes_{};
    // audio-decoders.cpp supplies dr_mp3's default signed16 low-level ABI.
    // Its public read_pcm_frames_f32 converts the same values to float.
    std::array<drmp3_int16,1152*2> decoded_{};
    std::array<float,1152*2> pcm_{};
    size_t used_=0,skip_=0;
    bool header_=false;
    int rate_=0,channels_=0;
    uint64_t input_=0,decodedFrames_=0;
    template<class Sink> void process(Sink& sink,bool final) {
        while(used_>=(final?4u:8192u)) {
            drmp3dec_frame_info info{};
            const int n=drmp3dec_decode_frame(&decoder_,bytes_.data(),int(used_),decoded_.data(),&info);
            if(info.frame_bytes<=0)break;
            if(size_t(info.frame_bytes)>used_)throw std::runtime_error("Invalid live MP3 frame length");
            if(n>0) {
                if(n>1152||info.channels<1||info.channels>2||info.sample_rate<8000||info.sample_rate>48000||info.layer!=3)
                    throw std::runtime_error("Live source must be mono/stereo MPEG Layer III MP3");
                if(rate_&&(rate_!=info.sample_rate||channels_!=info.channels))throw std::runtime_error("Live station changed its audio format; reconnect it");
                rate_=info.sample_rate;channels_=info.channels;decodedFrames_+=uint64_t(n);
                for(int i=0;i<n*channels_;++i)pcm_[i]=decoded_[i]/32768.f;
                sink(pcm_.data(),size_t(n),rate_,channels_);
            }
            used_-=size_t(info.frame_bytes);std::memmove(bytes_.data(),bytes_.data()+info.frame_bytes,used_);
        }
    }
public:
    static constexpr size_t compressed_capacity=16384;
    Mp3Decoder(){drmp3dec_init(&decoder_);}
    template<class Sink> void feed(const unsigned char* data,size_t n,Sink&& sink) {
        input_+=n;
        while(n) {
            if(skip_){const auto part=std::min(n,skip_);data+=part;n-=part;skip_-=part;continue;}
            const auto count=std::min(n,bytes_.size()-used_);std::memcpy(bytes_.data()+used_,data,count);used_+=count;data+=count;n-=count;
            if(!header_&&used_>=10){
                header_=true;
                if(!std::memcmp(bytes_.data(),"ID3",3)){
                    for(int i=6;i<10;++i)if(bytes_[i]&128)throw std::runtime_error("Invalid live MP3 metadata");
                    const size_t tag=(size_t(bytes_[6])<<21)|(size_t(bytes_[7])<<14)|(size_t(bytes_[8])<<7)|bytes_[9];
                    if(tag>4*1024*1024)throw std::runtime_error("Live MP3 metadata exceeds4MiB");
                    const size_t total=10+tag+((bytes_[5]&16)?10:0),remove=std::min(total,used_);
                    skip_=total-remove;used_-=remove;std::memmove(bytes_.data(),bytes_.data()+remove,used_);
                }
            }
            process(sink,false);
            if(used_==bytes_.size())throw std::runtime_error("Live station did not supply decodable MP3 frames");
            if(input_>256*1024&&!decodedFrames_)throw std::runtime_error("Live station is not a supported MP3 stream");
        }
    }
    template<class Sink> void finish(Sink&& sink){process(sink,true);}
    int rate()const{return rate_;}
    uint64_t frames()const{return decodedFrames_;}
    size_t buffered_bytes()const{return used_;}
};
} // namespace pt::platform::source_live
