#pragma once

// One Xbox voice: a DirectSound buffer or stream, as the APU plays it. Pure playback
// logic - which samples come next - independent of the output library:
//
// - Buffers play straight from title memory (SetBufferData does not copy): a play
//   region of the data, optionally looping over a loop region inside it. The title
//   may rewrite that memory at any time, so samples are read when they are played.
// - Streams play title-owned packets in order. When a packet has been consumed its
//   status word becomes XMP_STATUS_SUCCESS (it read E_PENDING until then) and its
//   completed-size word is set; titles poll these. Without packets a stream plays
//   silence (starved) until a Discontinuity ends it.
//
// All methods are thread-safe; render() runs on the mixer thread.

#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <vector>

#include "format.h"

namespace audio {

constexpr uint32_t kPacketSuccess = 0x00000000;  // XMP_STATUS_SUCCESS
constexpr uint32_t kPacketPending = 0x8000000A;  // XMP_STATUS_PENDING (E_PENDING)
constexpr uint32_t kPacketFlushed = 0x80004004;  // XMP_STATUS_FLUSHED (E_ABORT)

struct Packet {
    const uint8_t* data = nullptr;
    uint32_t size = 0;
    uint32_t* status = nullptr;     // optional
    uint32_t* completed = nullptr;  // optional: bytes played
    uint32_t completion = 0;        // event handle or callback context (owner's business)
};

class Voice {
public:
    enum class Kind { Buffer, Stream };

    explicit Voice(Kind kind) : kind_(kind) {}
    Voice(const Voice&) = delete;
    Voice& operator=(const Voice&) = delete;

    Kind kind() const { return kind_; }
    void set_format(const Format& format);
    Format format() const;

    // --- Buffers (positions and regions in bytes) ---
    void set_buffer_data(const uint8_t* data, uint32_t size);
    void set_play_region(uint32_t start, uint32_t length);  // length 0: to the end of the data
    void set_loop_region(uint32_t start, uint32_t length);  // relative to the play region; 0: to its end
    void set_position(uint32_t offset);                     // relative to the play region
    uint32_t position() const;
    void play(bool looping, bool from_start);
    void stop();  // buffers: stop; streams: same as flush()

    // --- Streams ---
    // Queues a packet (its status becomes pending). False if `max_packets` are queued.
    bool submit(const Packet& packet, size_t max_packets);
    size_t queued_packets() const;
    void discontinuity();  // the stream ends after the queued packets
    void flush();          // drops queued packets (status flushed) and stops

    // --- Both ---
    void set_paused(bool paused);
    bool paused() const;
    bool playing() const;  // started and not finished (paused voices still count)
    bool looping() const;
    bool starved() const;

    // Mixer side: writes up to `frames` interleaved 16-bit frames. Returns how many;
    // fewer than asked means the voice ended (it is no longer playing).
    size_t render(int16_t* out, size_t frames);

    // The comparison harness's fork (xbox_dsound.h, hold_for_fork): the voice locked, so that
    // the copy sees it between changes.
    void hold() const { mutex_.lock(); }
    void release() const { mutex_.unlock(); }

    // Called (from render, with the voice locked - keep it short) when a packet
    // completes or is flushed, after its status words are written.
    std::function<void(const Packet& packet, uint32_t status)> on_packet_done;

private:
    void finish_packet(Packet& packet, uint32_t status, uint32_t played);
    size_t render_buffer(int16_t* out, size_t frames);
    size_t render_stream(int16_t* out, size_t frames);
    // Copies frames [first, first + count) of the play region.
    void copy_region_frames(uint32_t first, uint32_t count, int16_t* out);
    uint32_t region_bytes() const;

    const Kind kind_;
    mutable std::mutex mutex_;
    Format format_;
    bool playing_ = false;
    bool paused_ = false;
    bool looping_ = false;

    // Buffer.
    const uint8_t* data_ = nullptr;
    uint32_t data_size_ = 0;
    uint32_t play_start_ = 0, play_length_ = 0;
    uint32_t loop_start_ = 0, loop_length_ = 0;
    uint32_t cursor_ = 0;  // frames into the play region

    // Stream.
    std::deque<Packet> packets_;
    uint32_t packet_offset_ = 0;  // bytes of the front packet consumed
    bool ending_ = false;
    bool starved_ = false;

    // Decoding: one block, and how much of it has been used.
    std::vector<uint8_t> block_;  // stream: block being assembled from packets
    uint32_t block_fill_ = 0;
    std::vector<int16_t> decoded_;
    uint32_t decoded_block_ = UINT32_MAX;  // buffer: which block decoded_ holds
    uint32_t decoded_next_ = 0, decoded_count_ = 0;  // stream: frames left in decoded_
};

}  // namespace audio
