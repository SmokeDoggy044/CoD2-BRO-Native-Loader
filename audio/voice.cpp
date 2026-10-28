#include "voice.h"

#include "test_mode.h"

#include <algorithm>
#include <cstring>

namespace audio {

void Voice::set_format(const Format& format) {
    Guard lock(mutex_, TestLock::Voice);
    format_ = format;
    block_.assign(format.block_align, 0);
    block_fill_ = 0;
    decoded_.assign(size_t{format.frames_per_block()} * format.channels, 0);
    decoded_block_ = UINT32_MAX;
    decoded_next_ = decoded_count_ = 0;
}

Format Voice::format() const {
    Guard lock(mutex_, TestLock::Voice);
    return format_;
}

// --- Buffers --------------------------------------------------------------------------

void Voice::set_buffer_data(const uint8_t* data, uint32_t size) {
    Guard lock(mutex_, TestLock::Voice);
    data_ = data;
    data_size_ = size;
    play_start_ = play_length_ = loop_start_ = loop_length_ = 0;
    cursor_ = 0;
}

void Voice::set_play_region(uint32_t start, uint32_t length) {
    Guard lock(mutex_, TestLock::Voice);
    play_start_ = start;
    play_length_ = length;
}

void Voice::set_loop_region(uint32_t start, uint32_t length) {
    Guard lock(mutex_, TestLock::Voice);
    loop_start_ = start;
    loop_length_ = length;
}

void Voice::set_position(uint32_t offset) {
    Guard lock(mutex_, TestLock::Voice);
    cursor_ = std::min(format_.bytes_to_frames(offset), format_.bytes_to_frames(region_bytes()));
}

uint32_t Voice::position() const {
    Guard lock(mutex_, TestLock::Voice);
    return format_.frames_to_bytes(cursor_);
}

void Voice::play(bool looping, bool from_start) {
    Guard lock(mutex_, TestLock::Voice);
    if (from_start) cursor_ = 0;
    looping_ = looping;
    playing_ = true;
    paused_ = false;
}

void Voice::stop() {
    if (kind_ == Kind::Stream) {
        flush();
        return;
    }
    Guard lock(mutex_, TestLock::Voice);
    playing_ = false;
    paused_ = false;
}

uint32_t Voice::region_bytes() const {
    const uint32_t end = play_length_ ? std::min(play_start_ + play_length_, data_size_) : data_size_;
    return end > play_start_ ? end - play_start_ : 0;
}

// --- Streams --------------------------------------------------------------------------

bool Voice::submit(const Packet& packet, size_t max_packets) {
    Guard lock(mutex_, TestLock::Voice);
    if (packets_.size() >= max_packets) return false;
    if (packet.status) *packet.status = kPacketPending;
    if (packet.completed) *packet.completed = 0;
    packets_.push_back(packet);
    playing_ = true;
    ending_ = false;
    starved_ = false;
    return true;
}

size_t Voice::queued_packets() const {
    Guard lock(mutex_, TestLock::Voice);
    return packets_.size();
}

void Voice::discontinuity() {
    Guard lock(mutex_, TestLock::Voice);
    if (playing_) ending_ = true;
}

void Voice::flush() {
    Guard lock(mutex_, TestLock::Voice);
    for (Packet& packet : packets_) {
        const uint32_t played = &packet == &packets_.front() ? packet_offset_ : 0;
        finish_packet(packet, kPacketFlushed, played);
    }
    packets_.clear();
    packet_offset_ = 0;
    block_fill_ = 0;
    decoded_next_ = decoded_count_ = 0;
    playing_ = paused_ = ending_ = starved_ = false;
}

void Voice::finish_packet(Packet& packet, uint32_t status, uint32_t played) {
    if (packet.completed) *packet.completed = played;
    if (packet.status) *packet.status = status;
    if (on_packet_done) on_packet_done(packet, status);
}

// --- Both -----------------------------------------------------------------------------

void Voice::set_paused(bool paused) {
    Guard lock(mutex_, TestLock::Voice);
    paused_ = paused;
}

bool Voice::paused() const {
    Guard lock(mutex_, TestLock::Voice);
    return paused_;
}

bool Voice::playing() const {
    Guard lock(mutex_, TestLock::Voice);
    return playing_;
}

bool Voice::looping() const {
    Guard lock(mutex_, TestLock::Voice);
    return looping_;
}

bool Voice::starved() const {
    Guard lock(mutex_, TestLock::Voice);
    return starved_;
}

// --- Rendering ------------------------------------------------------------------------

size_t Voice::render(int16_t* out, size_t frames) {
    Guard lock(mutex_, TestLock::Voice);
    if (!playing_ || format_.block_align == 0) return 0;
    if (paused_) {
        std::memset(out, 0, frames * format_.channels * sizeof(int16_t));
        return frames;
    }
    return kind_ == Kind::Buffer ? render_buffer(out, frames) : render_stream(out, frames);
}

void Voice::copy_region_frames(uint32_t first, uint32_t count, int16_t* out) {
    const uint32_t channels = format_.channels;
    const uint32_t per_block = format_.frames_per_block();
    while (count > 0) {
        const uint32_t block = first / per_block;
        const uint32_t within = first % per_block;
        const uint32_t take = std::min(count, per_block - within);
        const uint64_t offset = uint64_t{play_start_} + uint64_t{block} * format_.block_align;
        if (!data_ || offset + format_.block_align > data_size_) {
            std::memset(out, 0, size_t{take} * channels * sizeof(int16_t));
        } else if (per_block == 1) {
            decode_block(format_, data_ + offset, out);
        } else {
            if (decoded_block_ != block) {
                decode_block(format_, data_ + offset, decoded_.data());
                decoded_block_ = block;
            }
            std::memcpy(out, decoded_.data() + size_t{within} * channels, size_t{take} * channels * sizeof(int16_t));
        }
        out += size_t{take} * channels;
        first += take;
        count -= take;
    }
}

size_t Voice::render_buffer(int16_t* out, size_t frames) {
    // The title may rewrite the data between calls (e.g. a ring buffer it streams
    // into), so decoded blocks are only reused within one call.
    decoded_block_ = UINT32_MAX;
    const uint32_t region = format_.bytes_to_frames(region_bytes());
    const uint32_t loop_start = std::min(format_.bytes_to_frames(loop_start_), region);
    const uint32_t loop_end =
        loop_length_ ? std::min(format_.bytes_to_frames(loop_start_ + loop_length_), region) : region;
    size_t produced = 0;
    while (produced < frames) {
        const uint32_t end = looping_ ? loop_end : region;
        if (cursor_ >= end) {
            if (looping_ && loop_end > loop_start) {
                cursor_ = loop_start;
                continue;
            }
            playing_ = false;
            cursor_ = 0;
            break;
        }
        const auto take = static_cast<uint32_t>(std::min<size_t>(frames - produced, end - cursor_));
        copy_region_frames(cursor_, take, out + produced * format_.channels);
        cursor_ += take;
        produced += take;
    }
    return produced;
}

size_t Voice::render_stream(int16_t* out, size_t frames) {
    const uint32_t channels = format_.channels;
    size_t produced = 0;
    while (produced < frames) {
        if (decoded_next_ < decoded_count_) {
            const auto take = static_cast<uint32_t>(std::min<size_t>(frames - produced, decoded_count_ - decoded_next_));
            std::memcpy(out + produced * channels, decoded_.data() + size_t{decoded_next_} * channels,
                        size_t{take} * channels * sizeof(int16_t));
            decoded_next_ += take;
            produced += take;
            continue;
        }
        // Assemble the next block from the packets; it may span several.
        while (block_fill_ < format_.block_align && !packets_.empty()) {
            Packet& packet = packets_.front();
            const uint32_t take = std::min(format_.block_align - block_fill_, packet.size - packet_offset_);
            if (take) std::memcpy(block_.data() + block_fill_, packet.data + packet_offset_, take);
            block_fill_ += take;
            packet_offset_ += take;
            if (packet_offset_ >= packet.size) {
                finish_packet(packet, kPacketSuccess, packet.size);
                packets_.pop_front();
                packet_offset_ = 0;
            }
        }
        if (block_fill_ == format_.block_align) {
            decode_block(format_, block_.data(), decoded_.data());
            decoded_next_ = 0;
            decoded_count_ = format_.frames_per_block();
            block_fill_ = 0;
            starved_ = false;
            continue;
        }
        if (ending_) {  // all data played
            block_fill_ = 0;
            playing_ = ending_ = false;
            break;
        }
        // Out of data: play silence until more arrives.
        std::memset(out + produced * channels, 0, (frames - produced) * channels * sizeof(int16_t));
        produced = frames;
        starved_ = true;
    }
    return produced;
}

}  // namespace audio
