#include "nal_entropy.h"

namespace game {

namespace {

// The decoders' state: the stream as a bit position (LSB first in little-endian words, read
// a word at a time from the aligned word holding the position), the output and what is left
// to decode.
class Decoder {
public:
    Decoder(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count)
        : reader_(reader),
          pos_(static_cast<uint64_t>(reader->ptr) * 8 + reader->bit),  // the byte address * 8 overflows 32 bits
          dst_(reinterpret_cast<uint8_t*>(dst)),
          stride_(stride),
          count_(count) {}

    bool more() const { return count_ != 0; }

    // The next n bits (n <= 32), not consumed.
    uint32_t peek(uint32_t n) const {
        const uint32_t* word = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>((pos_ >> 5) * 4));
        const uint32_t shift = static_cast<uint32_t>(pos_ & 31);
        uint64_t bits = word[0];
        if (shift + n > 32) bits |= static_cast<uint64_t>(word[1]) << 32;
        bits >>= shift;
        return n == 32 ? static_cast<uint32_t>(bits) : static_cast<uint32_t>(bits) & ((1u << n) - 1);
    }

    // One value with a code of `length` bits.
    void value(uint32_t length, int32_t v) {
        pos_ += length;
        put(static_cast<uint32_t>(v));
        --count_;
    }

    // A run of `run` zeros with a code of `length` bits; what does not fit is left over.
    void zeros(uint32_t length, uint32_t run) {
        pos_ += length;
        const uint32_t n = count_ < run ? count_ : run;
        for (uint32_t i = 0; i < n; ++i) put(0);
        if (count_ < run) {
            left_ = run - count_;
            count_ = 0;
        } else {
            count_ -= run;
        }
    }

    // Stores the position in the reader; returns the zeros left over.
    uint32_t finish() {
        reader_->ptr = static_cast<uint32_t>(pos_ >> 3);
        reader_->bit = static_cast<uint8_t>(pos_ & 7);
        return left_;
    }

private:
    void put(uint32_t v) {
        *reinterpret_cast<uint32_t*>(dst_) = v;
        dst_ += stride_;
    }

    EntropyReader* reader_;
    uint64_t pos_;
    uint8_t* dst_;
    int32_t stride_;
    uint32_t count_;
    uint32_t left_ = 0;
};

// A sign-folded field of `bits` bits: values with the top bit set stand for themselves, the
// others for value - (2^bits - 1) (with 2 bits: 0, 1, 2, 3 -> -3, -2, +2, +3).
int32_t fold(uint32_t value, uint32_t bits) {
    const int32_t v = static_cast<int32_t>(value);
    return (value & (1u << (bits - 1))) ? v : v - static_cast<int32_t>((1u << bits) - 1);
}

// A fixed-width code: 0 is a run of zeros, the other codes value - 2^(bits-1).
uint32_t fixed_width(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count, uint32_t bits, uint32_t run) {
    Decoder d(reader, dst, stride, count);
    while (d.more()) {
        const uint32_t c = d.peek(bits);
        if (!c) {
            d.zeros(bits, run);
        } else {
            d.value(bits, static_cast<int32_t>(c) - static_cast<int32_t>(1u << (bits - 1)));
        }
    }
    return d.finish();
}

}  // namespace

uint32_t XB_CDECL NAL_EntropyMode00(EntropyReader*, uint32_t* dst, int32_t stride, uint32_t count) {
    for (; count; --count) {
        *dst = 0;
        dst = reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(dst) + stride);
    }
    return 0;
}

uint32_t XB_CDECL NAL_EntropyMode01(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count) {
    Decoder d(reader, dst, stride, count);
    while (d.more()) {
        const uint32_t c = d.peek(2);
        if (!(c & 1)) {
            d.value(1, 0);
        } else {
            d.value(2, static_cast<int32_t>(c) - 2);  // 01 -> -1, 11 -> +1
        }
    }
    return d.finish();
}

uint32_t XB_CDECL NAL_EntropyMode02(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count) {
    Decoder d(reader, dst, stride, count);
    while (d.more()) {
        const uint32_t c = d.peek(4);
        if (!(c & 1)) {
            d.zeros(1, 7);
        } else if (!(c & 2)) {
            d.zeros(2, 2);
        } else if (!(c & 4)) {
            d.value(3, 0);
        } else {
            d.value(4, static_cast<int32_t>(c >> 2) - 2);  // -1, +1
        }
    }
    return d.finish();
}

uint32_t XB_CDECL NAL_EntropyMode03(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count) {
    Decoder d(reader, dst, stride, count);
    while (d.more()) {
        const uint32_t c = d.peek(3);
        if (!(c & 1)) {
            d.zeros(1, 3);
        } else if (!(c & 2)) {
            d.value(2, 0);
        } else {
            d.value(3, static_cast<int32_t>(c >> 1) - 2);  // -1, +1
        }
    }
    return d.finish();
}

uint32_t XB_CDECL NAL_EntropyMode04(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count) {
    Decoder d(reader, dst, stride, count);
    while (d.more()) {
        const uint32_t c = d.peek(4);
        if (!(c & 1)) {
            d.zeros(1, 4);
        } else if (!(c & 2)) {
            d.zeros(2, 2);
        } else if (!(c & 4)) {
            d.value(3, 0);
        } else {
            d.value(4, static_cast<int32_t>(c >> 2) - 2);  // -1, +1
        }
    }
    return d.finish();
}

uint32_t XB_CDECL NAL_EntropyMode05(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count) {
    Decoder d(reader, dst, stride, count);
    while (d.more()) {
        const uint32_t c = d.peek(2);
        if (!c) {
            d.zeros(2, 6);
        } else {
            d.value(2, static_cast<int32_t>(c) - 2);  // -1, 0, +1
        }
    }
    return d.finish();
}

uint32_t XB_CDECL NAL_EntropyMode06(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count) {
    Decoder d(reader, dst, stride, count);
    while (d.more()) {
        const uint32_t c = d.peek(3);
        if (!(c & 1)) {
            d.value(1, 0);
        } else {
            const int32_t t = static_cast<int32_t>(c >> 1);
            d.value(3, (t >> 1) + t - 2);  // -2, -1, +1, +2
        }
    }
    return d.finish();
}

uint32_t XB_CDECL NAL_EntropyMode07(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count) {
    Decoder d(reader, dst, stride, count);
    while (d.more()) {
        const uint32_t c = d.peek(3);
        if (c & 3) {
            d.value(2, static_cast<int32_t>(c & 3) - 2);  // -1, 0, +1
        } else {
            d.value(3, static_cast<int32_t>(c) - 2);  // -2, +2
        }
    }
    return d.finish();
}

uint32_t XB_CDECL NAL_EntropyMode08(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count) {
    Decoder d(reader, dst, stride, count);
    while (d.more()) {
        const uint32_t c = d.peek(5);
        if (!(c & 1)) {
            d.zeros(1, 4);
        } else if (!(c & 2)) {
            d.zeros(2, 2);
        } else if (!(c & 4)) {
            d.value(3, 0);
        } else {
            const int32_t t = static_cast<int32_t>(c >> 3);
            d.value(5, (t >> 1) + t - 2);  // -2, -1, +1, +2
        }
    }
    return d.finish();
}

uint32_t XB_CDECL NAL_EntropyMode09(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count) {
    Decoder d(reader, dst, stride, count);
    while (d.more()) {
        const uint32_t c = d.peek(4);
        if (c & 3) {
            d.value(2, static_cast<int32_t>(c & 3) - 2);  // -1, 0, +1
        } else {
            d.value(4, fold(c >> 2, 2));  // -3, -2, +2, +3
        }
    }
    return d.finish();
}

uint32_t XB_CDECL NAL_EntropyMode0A(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count) {
    Decoder d(reader, dst, stride, count);
    while (d.more()) {
        const uint32_t c = d.peek(3);
        if (!c) {
            d.zeros(3, 3);
        } else {
            d.value(3, static_cast<int32_t>(c) - 4);  // -3..+3
        }
    }
    return d.finish();
}

uint32_t XB_CDECL NAL_EntropyMode0B(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count) {
    Decoder d(reader, dst, stride, count);
    while (d.more()) {
        const uint32_t c = d.peek(5);
        if (c & 3) {
            d.value(2, static_cast<int32_t>(c & 3) - 2);  // -1, 0, +1
        } else {
            const int32_t m = static_cast<int32_t>(c >> 2);
            d.value(5, (m & 4) ? m - 2 : m - 5);  // -5..-2, +2..+5
        }
    }
    return d.finish();
}

uint32_t XB_CDECL NAL_EntropyMode0C(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count) {
    Decoder d(reader, dst, stride, count);
    while (d.more()) {
        const uint32_t c = d.peek(4);
        if (!c) {
            d.zeros(4, 4);
        } else {
            d.value(4, static_cast<int32_t>(c) - 8);  // -7..+7
        }
    }
    return d.finish();
}

uint32_t XB_CDECL NAL_EntropyMode0D(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count) {
    Decoder d(reader, dst, stride, count);
    while (d.more()) {
        const uint32_t c = d.peek(5);
        const int32_t low = static_cast<int32_t>(c & 7);
        if (low >= 3) {
            d.value(3, low - 5);  // -2..+2
        } else if (low == 2) {
            d.value(4, (c & 8) ? 3 : -3);
        } else if (low == 1) {
            d.value(5, -4 - static_cast<int32_t>(c >> 3));  // -4..-7
        } else {
            d.value(5, static_cast<int32_t>(c >> 3) + 4);  // +4..+7
        }
    }
    return d.finish();
}

uint32_t XB_CDECL NAL_EntropyMode0E(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count) {
    Decoder d(reader, dst, stride, count);
    while (d.more()) {
        const uint32_t c = d.peek(6);
        if (c & 3) {
            d.value(2, static_cast<int32_t>(c & 3) - 2);  // -1, 0, +1
        } else if (!(c & 4)) {
            d.value(5, fold((c >> 3) & 3, 2));  // -3, -2, +2, +3
        } else {
            d.value(6, fold(c >> 3, 3));  // -7..-4, +4..+7
        }
    }
    return d.finish();
}

uint32_t XB_CDECL NAL_EntropyMode0F(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count) {
    Decoder d(reader, dst, stride, count);
    while (d.more()) {
        const uint32_t c = d.peek(5);
        if (!(c & 1)) {
            if (!(c & 0xf)) {
                d.zeros(4, 4);
            } else {
                d.value(4, static_cast<int32_t>((c & 0xf) >> 1) - 4);  // -3..+3
            }
        } else {
            d.value(5, fold(c >> 2, 3) << ((c >> 1) & 1));  // +-4..7, or twice that
        }
    }
    return d.finish();
}

uint32_t XB_CDECL NAL_EntropyMode10(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count) {
    Decoder d(reader, dst, stride, count);
    while (d.more()) {
        const uint32_t c = d.peek(7);
        if (c & 3) {
            d.value(2, static_cast<int32_t>(c & 3) - 2);  // -1, 0, +1
        } else if (!(c & 4)) {
            d.value(5, fold((c >> 3) & 3, 2));  // -3, -2, +2, +3
        } else {
            d.value(7, fold(c >> 4, 3) << ((c >> 3) & 1));  // +-4..7, or twice that
        }
    }
    return d.finish();
}

uint32_t XB_CDECL NAL_EntropyMode11(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count) {
    Decoder d(reader, dst, stride, count);
    while (d.more()) {
        const uint32_t c = d.peek(7);
        if (!(c & 1)) {
            if (!(c & 2)) {
                d.zeros(2, 8);
            } else {
                d.value(2, 0);
            }
        } else if (!(c & 2)) {
            d.value(3, static_cast<int32_t>((c >> 1) & 2) - 1);  // -1, +1
        } else if (!(c & 4)) {
            d.value(5, fold((c >> 3) & 3, 2));  // -3, -2, +2, +3
        } else {
            d.value(7, fold(c >> 4, 3) << ((c >> 3) & 1));  // +-4..7, or twice that
        }
    }
    return d.finish();
}

uint32_t XB_CDECL NAL_EntropyMode12(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count) {
    Decoder d(reader, dst, stride, count);
    while (d.more()) {
        const uint32_t c = d.peek(5);
        if (!c) {
            d.zeros(5, 5);
        } else if (!(c & 3)) {
            d.value(5, static_cast<int32_t>(c >> 2) - 4);  // -3..+3
        } else {
            d.value(5, fold(c >> 2, 3) << ((c & 3) - 1));  // +-4..7, times 1, 2 or 4
        }
    }
    return d.finish();
}

uint32_t XB_CDECL NAL_EntropyMode13(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count) {
    Decoder d(reader, dst, stride, count);
    while (d.more()) {
        const uint32_t c = d.peek(6);
        if (!(c & 1)) {
            if (!(c & 0xf)) {
                d.zeros(4, 4);
            } else {
                d.value(4, static_cast<int32_t>((c & 0xf) >> 1) - 4);  // -3..+3
            }
        } else if (!(c & 2)) {
            d.value(5, fold((c >> 2) & 7, 3));  // -7..-4, +4..+7
        } else {
            d.value(6, fold(c >> 3, 3) << (((c >> 2) & 1) + 1));  // +-4..7 times 2 or 4
        }
    }
    return d.finish();
}

uint32_t XB_CDECL NAL_EntropyMode14(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count) {
    Decoder d(reader, dst, stride, count);
    while (d.more()) {
        const uint32_t c = d.peek(7);
        const int32_t low = static_cast<int32_t>(c & 7);
        if (low >= 3) {
            d.value(3, low - 5);  // -2..+2
        } else if (low == 0) {
            d.value(4, (c & 8) ? 3 : -3);
        } else if (low == 1) {
            d.value(6, fold((c >> 3) & 7, 3));  // -7..-4, +4..+7
        } else {
            d.value(7, fold(c >> 4, 3) << (((c >> 3) & 1) + 1));  // +-4..7 times 2 or 4
        }
    }
    return d.finish();
}

uint32_t XB_CDECL NAL_EntropyMode15(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count) {
    Decoder d(reader, dst, stride, count);
    while (d.more()) {
        const uint32_t c = d.peek(7);
        if (!(c & 1)) {
            if (!(c & 2)) {
                d.zeros(2, 8);
            } else {
                d.value(2, 0);
            }
        } else if (!(c & 2)) {
            d.value(3, static_cast<int32_t>((c >> 1) & 2) - 1);  // -1, +1
        } else if (!(c & 0xc)) {
            d.value(6, fold((c >> 4) & 3, 2));  // -3, -2, +2, +3
        } else {
            d.value(7, fold(c >> 4, 3) << (((c >> 2) & 3) - 1));  // +-4..7 times 1, 2 or 4
        }
    }
    return d.finish();
}

uint32_t XB_CDECL NAL_EntropyMode16(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count) {
    Decoder d(reader, dst, stride, count);
    while (d.more()) {
        const uint32_t c = d.peek(6);
        if (!(c & 1)) {
            if (!(c & 0xf)) {
                d.zeros(4, 4);
            } else {
                d.value(4, static_cast<int32_t>((c & 0xf) >> 1) - 4);  // -3..+3
            }
        } else {
            d.value(6, fold(c >> 3, 3) << ((c >> 1) & 3));  // +-4..7 times 1, 2, 4 or 8
        }
    }
    return d.finish();
}

uint32_t XB_CDECL NAL_EntropyMode17(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count) {
    Decoder d(reader, dst, stride, count);
    while (d.more()) {
        const uint32_t c = d.peek(6);
        if (!(c & 1)) {
            if (!(c & 0x1f)) {
                d.zeros(5, 5);
            } else {
                d.value(5, static_cast<int32_t>((c & 0x1f) >> 1) - 8);  // -7..+7
            }
        } else if (!(c & 2)) {
            d.value(5, fold((c >> 2) & 7, 3) << 1);  // +-8..14
        } else {
            d.value(6, fold(c >> 3, 3) << (((c >> 2) & 1) + 2));  // +-4..7 times 4 or 8
        }
    }
    return d.finish();
}

uint32_t XB_CDECL NAL_EntropyMode18(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count) {
    Decoder d(reader, dst, stride, count);
    while (d.more()) {
        const uint32_t c = d.peek(6);
        if (!(c & 1)) {
            if (!(c & 0x1f)) {
                d.zeros(5, 5);
            } else {
                d.value(5, static_cast<int32_t>((c & 0x1f) >> 1) - 8);  // -7..+7
            }
        } else {
            d.value(6, fold(c >> 3, 3) << (((c >> 1) & 3) + 1));  // +-4..7 times 2..16
        }
    }
    return d.finish();
}

uint32_t XB_CDECL NAL_EntropyMode19(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count) {
    Decoder d(reader, dst, stride, count);
    while (d.more()) {
        const uint32_t c = d.peek(6);
        const uint32_t low = c & 7;
        if (low < 2) {
            const int32_t v = static_cast<int32_t>((c & 0x1f) >> 3);
            if (!(c & 0x1f)) {
                d.zeros(5, 5);
            } else if (c & 1) {
                d.value(5, v);  // 0..+3
            } else {
                d.value(5, -v);  // -1..-3
            }
        } else {
            d.value(6, fold(c >> 3, 3) << (low - 2));  // +-4..7 times 1..32
        }
    }
    return d.finish();
}

uint32_t XB_CDECL NAL_EntropyMode1A(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count) {
    Decoder d(reader, dst, stride, count);
    while (d.more()) {
        const uint32_t c = d.peek(7);
        if (!(c & 1)) {
            if (!(c & 0x1f)) {
                d.zeros(5, 5);
            } else {
                d.value(5, static_cast<int32_t>((c & 0x1f) >> 1) - 8);  // -7..+7
            }
        } else {
            d.value(7, fold(c >> 4, 3) << (((c >> 1) & 7) + 1));  // +-4..7 times 2..256
        }
    }
    return d.finish();
}

uint32_t XB_CDECL NAL_EntropyMode1B(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count) {
    Decoder d(reader, dst, stride, count);
    while (d.more()) {
        const uint32_t c = d.peek(9);
        const uint32_t low = c & 7;
        if (low < 2) {
            const int32_t v = static_cast<int32_t>((c & 0x1f) >> 3);
            if (!(c & 0x1f)) {
                d.zeros(5, 5);
            } else if (c & 1) {
                d.value(5, v);  // 0..+3
            } else {
                d.value(5, -v);  // -1..-3
            }
        } else if (low != 7) {
            d.value(6, fold((c >> 3) & 7, 3) << (low - 2));  // +-4..7 times 1..16
        } else {
            d.value(9, fold(c >> 6, 3) << (((c >> 3) & 7) + 5));  // +-4..7 times 32..4096
        }
    }
    return d.finish();
}

uint32_t XB_CDECL NAL_EntropyMode1C(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count) {
    Decoder d(reader, dst, stride, count);
    while (d.more()) {
        const uint32_t c = d.peek(10);
        const uint32_t low = c & 7;
        if (low < 2) {
            const int32_t v = static_cast<int32_t>((c & 0x1f) >> 3);
            if (!(c & 0x1f)) {
                d.zeros(5, 5);
            } else if (c & 1) {
                d.value(5, v);  // 0..+3
            } else {
                d.value(5, -v);  // -1..-3
            }
        } else if (low != 7) {
            d.value(6, fold((c >> 3) & 7, 3) << (low - 2));  // +-4..7 times 1..16
        } else {
            d.value(10, fold(c >> 7, 3) << (((c >> 3) & 0xf) + 5));  // +-4..7 times 2^5..2^20
        }
    }
    return d.finish();
}

uint32_t XB_CDECL NAL_EntropyMode1D(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count) {
    Decoder d(reader, dst, stride, count);
    while (d.more()) {
        const uint32_t c = d.peek(8);
        const uint32_t low = c & 0x1f;
        if (low == 2) {
            d.value(5, 0);
        } else if (low < 2) {
            const int32_t v = static_cast<int32_t>((c >> 5) & 3);
            d.value(7, (c & 1) ? v + 1 : -1 - v);  // +1..+4, -1..-4
        } else {
            d.value(8, fold(c >> 5, 3) << (low - 3));  // +-4..7 times 2^0..2^28
        }
    }
    return d.finish();
}

uint32_t XB_CDECL NAL_EntropyMode2F(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count) {
    return fixed_width(reader, dst, stride, count, 5, 5);  // -15..+15
}

uint32_t XB_CDECL NAL_EntropyMode30(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count) {
    Decoder d(reader, dst, stride, count);
    while (d.more()) {
        const uint32_t c = d.peek(7);
        if (!(c & 1)) {
            d.zeros(1, 4);
        } else if (!(c & 2)) {
            d.value(2, 0);
        } else {
            const int32_t t = static_cast<int32_t>(c >> 2);
            d.value(7, (t >> 4) + t - 16);  // -16..-1, +1..+16
        }
    }
    return d.finish();
}

uint32_t XB_CDECL NAL_EntropyMode31(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count) {
    Decoder d(reader, dst, stride, count);
    while (d.more()) {
        const uint32_t c = d.peek(7);
        if (!(c & 1)) {
            if (!(c & 2)) {
                d.zeros(2, 8);
            } else {
                d.value(2, 0);
            }
        } else if (!(c & 2)) {
            d.value(3, static_cast<int32_t>((c >> 1) & 2) - 1);  // -1, +1
        } else {
            const int32_t m = static_cast<int32_t>(c >> 2);
            d.value(7, (m & 0x10) ? m - 14 : m - 17);  // -17..-2, +2..+17
        }
    }
    return d.finish();
}

uint32_t XB_CDECL NAL_EntropyMode32(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count) {
    Decoder d(reader, dst, stride, count);
    while (d.more()) {
        const uint32_t c = d.peek(7);
        if (c & 3) {
            d.value(2, static_cast<int32_t>(c & 3) - 2);  // -1, 0, +1
        } else {
            const int32_t m = static_cast<int32_t>(c >> 2);
            d.value(7, (m & 0x10) ? m - 14 : m - 17);  // -17..-2, +2..+17
        }
    }
    return d.finish();
}

uint32_t XB_CDECL NAL_EntropyMode33(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count) {
    return fixed_width(reader, dst, stride, count, 6, 6);  // -31..+31
}

uint32_t XB_CDECL NAL_EntropyMode34(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count) {
    Decoder d(reader, dst, stride, count);
    while (d.more()) {
        const uint32_t c = d.peek(8);
        if (!(c & 1)) {
            if (!(c & 2)) {
                d.zeros(2, 8);
            } else {
                d.value(2, 0);
            }
        } else if (!(c & 2)) {
            d.value(3, static_cast<int32_t>((c >> 1) & 2) - 1);  // -1, +1
        } else {
            const int32_t m = static_cast<int32_t>(c >> 2);
            d.value(8, (m & 0x20) ? m - 30 : m - 33);  // -33..-2, +2..+33
        }
    }
    return d.finish();
}

uint32_t XB_CDECL NAL_EntropyMode35(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count) {
    Decoder d(reader, dst, stride, count);
    while (d.more()) {
        const uint32_t c = d.peek(8);
        const int32_t low = static_cast<int32_t>(c & 7);
        if (low >= 3) {
            d.value(3, low - 5);  // -2..+2
        } else if (low == 2) {
            d.value(4, (c & 8) ? 3 : -3);
        } else if (low == 1) {
            d.value(8, static_cast<int32_t>(c >> 3) + 4);  // +4..+35
        } else {
            d.value(8, -4 - static_cast<int32_t>(c >> 3));  // -4..-35
        }
    }
    return d.finish();
}

uint32_t XB_CDECL NAL_EntropyMode36(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count) {
    return fixed_width(reader, dst, stride, count, 7, 7);  // -63..+63
}

uint32_t XB_CDECL NAL_EntropyMode37(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count) {
    return fixed_width(reader, dst, stride, count, 8, 8);  // -127..+127
}

uint32_t XB_CDECL NAL_EntropyMode38(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count) {
    return fixed_width(reader, dst, stride, count, 9, 8);  // -255..+255; the zero run is 8, not 9
}

uint32_t XB_CDECL NAL_EntropyMode39(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count) {
    return fixed_width(reader, dst, stride, count, 10, 8);
}

uint32_t XB_CDECL NAL_EntropyMode3A(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count) {
    return fixed_width(reader, dst, stride, count, 11, 8);
}

uint32_t XB_CDECL NAL_EntropyMode3B(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count) {
    return fixed_width(reader, dst, stride, count, 16, 8);
}

uint32_t XB_CDECL NAL_EntropyMode3C(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count) {
    return fixed_width(reader, dst, stride, count, 24, 8);
}

// Raw 32-bit words, 0x80000000 standing for a run of 8 zeros.
uint32_t XB_CDECL NAL_EntropyMode3D(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count) {
    Decoder d(reader, dst, stride, count);
    while (d.more()) {
        const uint32_t c = d.peek(32);
        if (c == 0x80000000u) {
            d.zeros(32, 8);
        } else {
            d.value(32, static_cast<int32_t>(c));
        }
    }
    return d.finish();
}

uint32_t XB_CDECL NAL_EntropyMode1E(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count) {
    return NAL_EntropyMode00(reader, dst, stride, count);
}

const EntropyModeDecoder kEntropyModeDecoders[64] = {
    // 0x00-0x1f
    NAL_EntropyMode00, NAL_EntropyMode01, NAL_EntropyMode02, NAL_EntropyMode03, NAL_EntropyMode04, NAL_EntropyMode05,
    NAL_EntropyMode06, NAL_EntropyMode07, NAL_EntropyMode08, NAL_EntropyMode09, NAL_EntropyMode0A, NAL_EntropyMode0B,
    NAL_EntropyMode0C, NAL_EntropyMode0D, NAL_EntropyMode0E, NAL_EntropyMode0F, NAL_EntropyMode10, NAL_EntropyMode11,
    NAL_EntropyMode12, NAL_EntropyMode13, NAL_EntropyMode14, NAL_EntropyMode15, NAL_EntropyMode16, NAL_EntropyMode17,
    NAL_EntropyMode18, NAL_EntropyMode19, NAL_EntropyMode1A, NAL_EntropyMode1B, NAL_EntropyMode1C, NAL_EntropyMode1D,
    NAL_EntropyMode1E, NAL_EntropyMode1E,
    // 0x20-0x3f (negative scales): 0x20-0x2e share the first half's decoders
    NAL_EntropyMode00, NAL_EntropyMode01, NAL_EntropyMode02, NAL_EntropyMode03, NAL_EntropyMode04, NAL_EntropyMode05,
    NAL_EntropyMode06, NAL_EntropyMode07, NAL_EntropyMode08, NAL_EntropyMode09, NAL_EntropyMode0A, NAL_EntropyMode0B,
    NAL_EntropyMode0C, NAL_EntropyMode0D, NAL_EntropyMode0E, NAL_EntropyMode2F, NAL_EntropyMode30, NAL_EntropyMode31,
    NAL_EntropyMode32, NAL_EntropyMode33, NAL_EntropyMode34, NAL_EntropyMode35, NAL_EntropyMode36, NAL_EntropyMode37,
    NAL_EntropyMode38, NAL_EntropyMode39, NAL_EntropyMode3A, NAL_EntropyMode3B, NAL_EntropyMode3C, NAL_EntropyMode3D,
    NAL_EntropyMode1E, NAL_EntropyMode1E,
};

}  // namespace game
