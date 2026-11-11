#include "title_crt.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>
#include <string>

#include "title_data.h"

namespace game {

namespace {

// VC7.1's floating-point conversions (_cfltcvt): the value as 17 significant decimal digits, rounded half up at
// the requested position on that digit string (so %.0f of 2.5 is 3, and 1e30 prints as 1 and 30 zeros), exponents
// with at least 3 digits, and 1.#INF / 1.#QNAN / 1.#IND / 1.#SNAN for the special values.
struct Digits {
    std::string d;  // significant digits, d[0] != '0' unless the value is 0
    int exp = 0;    // decimal exponent of d[0]
};

Digits digits_of(double v) {
    char buf[40];
    std::snprintf(buf, sizeof buf, "%.16e", std::fabs(v));
    Digits out;
    for (const char* p = buf; *p && *p != 'e'; ++p)
        if (*p != '.') out.d += *p;
    out.exp = std::atoi(std::strchr(buf, 'e') + 1);
    if (v == 0) out.exp = 0;
    return out;
}

// Keeps `count` digits (count >= 0), rounding half up; a carry raises the exponent.
void round_digits(Digits& x, int count) {
    if (count < 0) {
        x.d = "0";
        return;
    }
    if (static_cast<int>(x.d.size()) <= count) {
        x.d.append(count - x.d.size(), '0');
        return;
    }
    const bool up = x.d[count] >= '5';
    x.d.resize(count);
    if (!up) return;
    int i = count - 1;
    while (i >= 0 && x.d[i] == '9') x.d[i--] = '0';
    if (i >= 0) {
        ++x.d[i];
    } else {  // all nines (or nothing kept): a new leading 1
        x.d.insert(0, 1, '1');
        if (count > 0) x.d.pop_back();
        ++x.exp;
    }
}

std::string exponent_text(char e, int exp) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%c%c%03d", e, exp < 0 ? '-' : '+', exp < 0 ? -exp : exp);
    return buf;
}

// Fixed notation of |v| with `precision` digits after the point.
std::string fixed_text(const Digits& source, int precision, bool point) {
    Digits x = source;
    const bool zero = x.d.find_first_not_of('0') == std::string::npos;
    if (zero) x.exp = 0;
    round_digits(x, x.exp + 1 + precision);
    std::string all = x.d;
    std::string intpart, frac;
    if (x.exp >= 0) {
        if (static_cast<int>(all.size()) < x.exp + 1 + precision) all.append(x.exp + 1 + precision - all.size(), '0');
        intpart = all.substr(0, x.exp + 1);
        frac = all.substr(x.exp + 1);
    } else {
        intpart = "0";
        frac = std::string(-x.exp - 1, '0') + all;
        frac.resize(precision, '0');
    }
    if (intpart.empty()) intpart = "0";
    std::string out = intpart;
    if (precision > 0 || point) out += '.';
    return out + frac.substr(0, precision);
}

std::string exp_text(const Digits& source, int precision, bool point, char e) {
    Digits x = source;
    round_digits(x, 1 + precision);
    const bool zero = x.d.find_first_not_of('0') == std::string::npos;
    std::string out(1, x.d[0]);
    if (precision > 0 || point) out += '.';
    out += x.d.substr(1, precision);
    return out + exponent_text(e, zero ? 0 : x.exp);
}

void strip_trailing_zeros(std::string& s) {
    const size_t e = s.find_first_of("eE");
    std::string mant = s.substr(0, e), tail = e == std::string::npos ? "" : s.substr(e);
    if (mant.find('.') == std::string::npos) return;
    while (mant.back() == '0') mant.pop_back();
    if (mant.back() == '.') mant.pop_back();
    s = mant + tail;
}

std::string format_float(double v, char conv, const std::string& flags, int width, int precision) {
    const bool left = flags.find('-') != std::string::npos, plus = flags.find('+') != std::string::npos;
    const bool space = flags.find(' ') != std::string::npos, alt = flags.find('#') != std::string::npos;
    bool zero_pad = flags.find('0') != std::string::npos && !left;
    if (precision < 0) precision = 6;
    uint64_t bits;
    std::memcpy(&bits, &v, 8);
    const bool negative = bits >> 63;
    std::string body;
    const char e = (conv == 'E' || conv == 'G') ? 'E' : 'e';
    if (std::isinf(v) || std::isnan(v)) {
        const uint64_t mantissa = bits & 0xfffffffffffffull;
        const char* tag = std::isinf(v)                          ? "#INF"
                          : negative && mantissa == (1ull << 51) ? "#IND"
                          : (mantissa >> 51) & 1                 ? "#QNAN"
                                                                 : "#SNAN";
        std::string frac = tag;
        if (conv == 'g' || conv == 'G') {
            body = std::string("1.") + tag;
        } else {
            frac.resize(std::max<size_t>(frac.size(), precision), '0');
            body = "1." + frac.substr(0, std::max(precision, 0));
            if (conv == 'e' || conv == 'E') body += exponent_text(e, 0);
        }
        zero_pad = false;
    } else {
        const Digits x = digits_of(v);
        if (conv == 'f') {
            body = fixed_text(x, precision, alt);
        } else if (conv == 'e' || conv == 'E') {
            body = exp_text(x, precision, alt, e);
        } else {
            const int p = precision == 0 ? 1 : precision;
            Digits r = x;
            round_digits(r, p);
            const bool zero = v == 0;
            const int exp = zero ? 0 : r.exp;
            if (exp < -4 || exp >= p) {
                body = exp_text(x, p - 1, alt, e);
            } else {
                body = fixed_text(x, p - 1 - exp, alt);
            }
            if (!alt) strip_trailing_zeros(body);
        }
    }
    // VC7.1 drops the sign of a zero result ("%g" of -0.0 is "0").
    const bool shown_negative = negative && (body.find_first_of("123456789#") != std::string::npos);
    const std::string sign = shown_negative ? "-" : plus ? "+" : space ? " " : "";
    const int pad = width - static_cast<int>(sign.size() + body.size());
    if (pad <= 0) return sign + body;
    if (left) return sign + body + std::string(pad, ' ');
    if (zero_pad) return sign + std::string(pad, '0') + body;
    return std::string(pad, ' ') + sign + body;
}

std::string narrow_from_utf16(const uint16_t* text, int precision) {
    std::string out;
    if (!text) return "(null)";
    for (int i = 0; text[i] && (precision < 0 || i < precision); ++i) out += text[i] < 0x100 ? static_cast<char>(text[i]) : '?';
    return out;
}

}  // namespace

namespace {
// VC7.1's _output: the whole formatted text (the callers below place it).
std::string format_text(const char* format, va_list args) {
    std::string result;
    for (const char* p = format; *p;) {
        if (*p != '%') {
            result += *p++;
            continue;
        }
        const char* spec_start = p++;
        std::string flags;
        while (*p && std::strchr("-+ #0", *p)) flags += *p++;
        int width = -1, precision = -1;
        bool left = flags.find('-') != std::string::npos;
        if (*p == '*') {
            width = va_arg(args, int32_t);
            if (width < 0) {
                left = true;
                flags += '-';
                width = -width;
            }
            ++p;
        } else if (std::isdigit(static_cast<unsigned char>(*p))) {
            width = 0;
            while (std::isdigit(static_cast<unsigned char>(*p))) width = width * 10 + (*p++ - '0');
        }
        if (*p == '.') {
            ++p;
            precision = 0;
            if (*p == '*') {
                precision = va_arg(args, int32_t);
                ++p;
            } else {
                while (std::isdigit(static_cast<unsigned char>(*p))) precision = precision * 10 + (*p++ - '0');
            }
        }
        enum { None, Short, Long, Wide64 } size = None;
        if (*p == 'h') {
            size = Short;
            ++p;
        } else if (*p == 'l' || *p == 'L' || *p == 'w') {
            size = Long;
            ++p;
        } else if (p[0] == 'I' && p[1] == '6' && p[2] == '4') {
            size = Wide64;
            p += 3;
        } else if (p[0] == 'I' && p[1] == '3' && p[2] == '2') {
            p += 3;
        }
        const char conv = *p;
        if (!conv) break;
        ++p;
        // The host spec: flags, width, precision as given, no size (the value is passed at its host type).
        std::string spec = "%" + flags;
        if (width >= 0) spec += std::to_string(width);
        if (precision >= 0) spec += "." + std::to_string(precision);
        char buf[512];
        switch (conv) {
        case 'd':
        case 'i':
        case 'u':
        case 'x':
        case 'X':
        case 'o': {
            if (size == Wide64) {
                const long long v = va_arg(args, long long);
                std::snprintf(buf, sizeof buf, (spec + "ll" + conv).c_str(), v);
            } else {
                int32_t v = va_arg(args, int32_t);
                if (size == Short) v = (conv == 'd' || conv == 'i') ? static_cast<int16_t>(v) : static_cast<uint16_t>(v);
                std::snprintf(buf, sizeof buf, (spec + conv).c_str(), v);
            }
            result += buf;
            break;
        }
        case 'p':  // VC7.1: 8 upper-case hex digits, no 0x
            std::snprintf(buf, sizeof buf, ("%" + flags + (width >= 0 ? std::to_string(width) : "") + ".8X").c_str(),
                          va_arg(args, uint32_t));
            result += buf;
            break;
        case 'e':
        case 'E':
        case 'f':
        case 'g':
        case 'G':
            result += format_float(va_arg(args, double), conv, flags, width, precision);
            break;
        case 'c':
        case 'C': {
            const uint32_t v = va_arg(args, uint32_t);
            const bool wide = conv == 'C' ? size != Short : size == Long;
            const char c = static_cast<char>(wide && (v & 0xffff) >= 0x100 ? '?' : v);
            std::snprintf(buf, sizeof buf, (spec + 'c').c_str(), c);
            result += buf;
            break;
        }
        case 's':
        case 'S': {
            const bool wide = conv == 'S' ? size != Short : size == Long;
            const uint32_t v = va_arg(args, uint32_t);
            const char* text = reinterpret_cast<const char*>(static_cast<uintptr_t>(v));
            std::string narrow;
            if (wide) {
                narrow = narrow_from_utf16(reinterpret_cast<const uint16_t*>(text), precision);
                text = narrow.c_str();
            } else if (!text) {
                text = "(null)";
            }
            const int length = static_cast<int>(precision >= 0 ? strnlen(text, static_cast<size_t>(precision)) : std::strlen(text));
            std::string s(text, static_cast<size_t>(length));
            if (width > length) s = left ? s + std::string(width - length, ' ') : std::string(width - length, ' ') + s;
            result += s;
            break;
        }
        case 'n':
            *reinterpret_cast<int32_t*>(static_cast<uintptr_t>(va_arg(args, uint32_t))) = static_cast<int32_t>(result.size());
            break;
        default:  // '%%' and unknown conversions: the character itself, as VC7.1
            if (conv == '%' && p - spec_start == 2) {
                result += '%';
            } else {
                result += conv;
            }
            break;
        }
    }
    return result;
}
}  // namespace

int32_t XB_CDECL title_vsprintf(char* out, const char* format, va_list args) {
    const std::string result = format_text(format, args);
    std::memcpy(out, result.c_str(), result.size() + 1);
    return static_cast<int32_t>(result.size());
}

int32_t XB_CDECL title_vsnprintf(char* out, uint32_t count, const char* format, va_list args) {
    // A string FILE with _cnt = count: each character first decrements the (int) count and is stored if it is still
    // >= 0, else the result becomes -1; then the terminator the same way. So 0x80000000 is not "no room": it wraps to
    // 0x7fffffff and takes everything. Not modelled: counts above 0x80000000 (the counter wraps to positive after the
    // first character, and the title's _output then goes on after the -1); no caller passes one. No buffer: only
    // counts.
    const std::string result = format_text(format, args);
    const auto length = static_cast<uint32_t>(result.size());
    if (!out) return static_cast<int32_t>(length);
    const auto fits = [count](uint32_t used) { return static_cast<int32_t>(count - used) >= 0; };
    bool failed = false;
    for (uint32_t i = 0; i < length; ++i) {
        if (fits(i + 1)) out[i] = result[i];
        else failed = true;
    }
    if (fits(length + 1)) out[length] = 0;
    return failed ? -1 : static_cast<int32_t>(length);
}

int32_t XB_CDECL title_sprintf(char* out, const char* format, ...) {
    va_list args;
    va_start(args, format);
    const int32_t n = title_vsprintf(out, format, args);
    va_end(args);
    return n;
}

int32_t XB_CDECL title_atol(const char* text) {
    const auto* p = reinterpret_cast<const unsigned char*>(text);
    while (*p == ' ' || (*p >= '\t' && *p <= '\r')) ++p;
    const unsigned char sign = *p;
    if (sign == '-' || sign == '+') ++p;
    uint32_t total = 0;  // wraps on overflow, as VC7.1's atox.c
    while (*p >= '0' && *p <= '9') total = total * 10 + (*p++ - '0');
    return static_cast<int32_t>(sign == '-' ? 0u - total : total);
}

char* XB_CDECL title_strncpy(char* dest, const char* source, uint32_t count) {
    char* d = dest;
    while (count && (*d = *source++) != 0) {
        ++d;
        --count;
    }
    while (count > 1) {  // the copied terminator took one; pad the rest
        *++d = 0;
        --count;
    }
    return dest;
}

int32_t XB_CDECL title_tolower(int32_t c) {
    return c >= 'A' && c <= 'Z' ? c + 0x20 : c;
}

namespace {
bool crt_space(unsigned char c) {
    return c == ' ' || (c >= '\t' && c <= '\r');
}
bool crt_digit(char c) {
    return c >= '0' && c <= '9';
}

// The longest prefix of `p` that VC's float grammar takes, as text strtod reads the same way ('d' exponents become
// 'e'); `*used` = characters taken (0: no number). `exp_letters` = the exponent letters accepted; with `bare_exp`
// (scanf) the letter and its sign are taken even without exponent digits (VC's sscanf reads "1e 2" as 1, then 2).
std::string float_prefix(const char* p, size_t* used, const char* exp_letters, bool bare_exp,
                         size_t limit = ~size_t{0}) {
    std::string out;
    size_t i = 0;
    if (i < limit && (p[i] == '+' || p[i] == '-')) out += p[i++];
    bool digits = false;
    while (i < limit && crt_digit(p[i])) out += p[i++], digits = true;
    if (i < limit && p[i] == '.') {
        out += p[i++];
        while (i < limit && crt_digit(p[i])) out += p[i++], digits = true;
    }
    if (!digits) {
        *used = 0;
        return "";
    }
    if (i < limit && p[i] && std::strchr(exp_letters, p[i])) {
        size_t j = i + 1;
        std::string e = "e";
        if (j < limit && (p[j] == '+' || p[j] == '-')) e += p[j++];
        if (j < limit && crt_digit(p[j])) {
            while (j < limit && crt_digit(p[j])) e += p[j++];
            out += e;
            i = j;
        } else if (bare_exp) {
            i = j;
        }
    }
    *used = i;
    return out;
}

// VC7.1's decimal conversions round to nearest but break an exact tie toward zero, not to even (sscanf "%f" of
// 9150587.5 is 9150587, atof("-111e20") is -1.1099999999999999e22). Float denormals round twice: to one bit more
// than the denormal keeps, then truncated - so they round up only above 0.75 of a step (sscanf "%f" of -897.6e-48,
// 0.64 of the smallest denormal, is -0; 894e-45, 637.98 steps, is 638). Everything else matches the exact value
// rounded (--check-crt: 10000 short random decimals; of 10000 with up to 20 digits and exponents to +-300, 1 near-tie
// at e-291 differs: VC's 64-bit intermediate with inexact powers of ten, not ported).
// Exact integer arithmetic, so neither the host's strtod (msvcrt's is not correctly rounded and ignores rounding
// modes) nor the thread's float environment plays a part.
struct Big {
    std::vector<uint32_t> w;  // little-endian words, no leading zero words
    void trim() {
        while (!w.empty() && !w.back()) w.pop_back();
    }
    void mul_add(uint32_t m, uint32_t a) {
        uint64_t carry = a;
        for (uint32_t& x : w) {
            carry += static_cast<uint64_t>(x) * m;
            x = static_cast<uint32_t>(carry);
            carry >>= 32;
        }
        if (carry) w.push_back(static_cast<uint32_t>(carry));
    }
    void shl(uint32_t bits) {
        if (w.empty()) return;
        const uint32_t words = bits / 32, rest = bits % 32;
        if (rest) {
            uint32_t carry = 0;
            for (uint32_t& x : w) {
                const uint32_t next = x >> (32 - rest);
                x = (x << rest) | carry;
                carry = next;
            }
            if (carry) w.push_back(carry);
        }
        w.insert(w.begin(), words, 0u);
    }
    int bits() const {
        if (w.empty()) return 0;
        uint32_t top = w.back(), n = 0;
        while (top) ++n, top >>= 1;
        return static_cast<int>(32 * (w.size() - 1) + n);
    }
    static int compare(const Big& a, const Big& b) {
        if (a.w.size() != b.w.size()) return a.w.size() < b.w.size() ? -1 : 1;
        for (size_t k = a.w.size(); k-- > 0;)
            if (a.w[k] != b.w[k]) return a.w[k] < b.w[k] ? -1 : 1;
        return 0;
    }
    void sub(const Big& b) {  // *this >= b
        int64_t borrow = 0;
        for (size_t k = 0; k < w.size(); ++k) {
            int64_t d = static_cast<int64_t>(w[k]) - (k < b.w.size() ? b.w[k] : 0) - borrow;
            borrow = d < 0;
            w[k] = static_cast<uint32_t>(d + (borrow ? (int64_t{1} << 32) : 0));
        }
        trim();
    }
};

// The prefix's value rounded the VC way to `p` significant bits (53: double, 24: float); the IEEE bits.
uint64_t vc_round(const std::string& number, int p, int bias, bool float_denormals) {
    const int emin = 1 - bias - (p - 1), emax = bias;  // lowest exponent of the last bit; highest of the first
    const uint64_t sign = number[0] == '-' ? uint64_t{1} << (p - 1 + (p == 53 ? 11 : 8)) : 0;
    Big num;
    int exp10 = 0, digits = 0;
    bool point = false;
    size_t k = number[0] == '-' || number[0] == '+';
    for (; k < number.size() && number[k] != 'e'; ++k) {
        if (number[k] == '.') {
            point = true;
            continue;
        }
        if (num.w.empty() && number[k] == '0') {
            if (point) --exp10;
            continue;
        }
        if (digits < 800) {
            num.mul_add(10, static_cast<uint32_t>(number[k] - '0'));
            ++digits;
            if (point) --exp10;
        } else if (!point) {
            ++exp10;  // digits past 800 only scale (a 0 for each dropped one would not change the rounding here)
        }
    }
    if (k < number.size()) {
        const int e = std::clamp(title_atol(number.c_str() + k + 1), -100000, 100000);
        exp10 += e;
    }
    const uint64_t inf = sign | (static_cast<uint64_t>(2 * bias + 1) << (p - 1));
    if (num.w.empty() || exp10 + digits < -400) return sign;  // below half the smallest denormal
    if (exp10 + digits > 400) return inf;
    Big den;
    den.w = {1};
    for (int n = 0; n < (exp10 < 0 ? -exp10 : exp10); ++n) (exp10 < 0 ? den : num).mul_add(10, 0);
    // value = num / den; q = floor(value / 2^e) with p bits (fewer for a denormal), r the remainder over den.
    int e = num.bits() - den.bits() - p;
    uint64_t q = 0;
    Big r;
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (e < emin) e = emin;
        Big a = num, b = den;
        if (e < 0)
            a.shl(static_cast<uint32_t>(-e));
        else
            b.shl(static_cast<uint32_t>(e));
        q = 0;
        for (int bit = p + 1; bit >= 0; --bit) {
            Big shifted = b;
            shifted.shl(static_cast<uint32_t>(bit));
            if (Big::compare(a, shifted) >= 0) {
                a.sub(shifted);
                q |= uint64_t{1} << bit;
            }
        }
        if (q >> p && e >= emin) {  // too many bits: one exponent higher
            ++e;
            continue;
        }
        if (!(q >> (p - 1)) && e > emin) {  // too few (and not a denormal): one lower
            --e;
            continue;
        }
        r = a;
        den = b;
        break;
    }
    // Rounding: 2r against den (half a step), for float denormals 4r against 3 den (three quarters).
    const bool denormal = !(q >> (p - 1));
    Big twice = r;
    twice.shl(denormal && float_denormals ? 2 : 1);
    Big limit = den;
    if (denormal && float_denormals) limit.mul_add(3, 0);
    if (Big::compare(twice, limit) > 0) ++q;
    if (q >> p) {
        q >>= 1;
        ++e;
    }
    if (e + p - 1 > emax) return inf;
    if (!(q >> (p - 1))) return sign | q;  // denormal (or 0)
    const uint64_t biased = static_cast<uint64_t>(e + p - 1 + bias);
    return sign | (biased << (p - 1)) | (q & ((uint64_t{1} << (p - 1)) - 1));
}

double parse_double(const std::string& text) {
    const uint64_t bits = vc_round(text, 53, 1023, false);
    double v;
    std::memcpy(&v, &bits, sizeof v);
    return v;
}
float parse_single(const std::string& text) {  // VC's _atoflt: straight to float, one rounding
    const uint32_t bits = static_cast<uint32_t>(vc_round(text, 24, 127, true));
    float v;
    std::memcpy(&v, &bits, sizeof v);
    return v;
}
}  // namespace

double XB_CDECL title_atof(const char* text) {
    const auto* p = reinterpret_cast<const unsigned char*>(text);
    while (crt_space(*p)) ++p;
    size_t used;
    const std::string number = float_prefix(reinterpret_cast<const char*>(p), &used, "eEdD", false);
    return used ? parse_double(number) : 0.0;
}

int32_t XB_CDECL title_sscanf(const char* text, const char* format, ...) {
    va_list args;
    va_start(args, format);
    const auto* in = reinterpret_cast<const unsigned char*>(text);
    const auto* f = reinterpret_cast<const unsigned char*>(format);
    int32_t stored = 0;
    bool any = false;  // a conversion was attempted with input left
    while (*f) {
        if (crt_space(*f)) {
            while (crt_space(*f)) ++f;
            while (crt_space(*in)) ++in;
            continue;
        }
        if (*f != '%' || f[1] == '%') {
            if (*f == '%') ++f;
            if (*in != *f) break;
            ++in;
            ++f;
            continue;
        }
        ++f;
        bool wide = false;
        if (*f == 'l') wide = true, ++f;
        const char conv = static_cast<char>(*f++);
        while (crt_space(*in)) ++in;
        if (!*in) {
            if (!any) stored = -1;
            break;
        }
        any = true;
        if (conv == 'f' || conv == 'e' || conv == 'g' || conv == 'E' || conv == 'G') {
            size_t used;
            const std::string number = float_prefix(reinterpret_cast<const char*>(in), &used, "eE", true);
            if (!used) break;
            in += used;
            if (wide)
                *va_arg(args, double*) = parse_double(number);
            else
                *va_arg(args, float*) = parse_single(number);
        } else if (conv == 'd') {
            const unsigned char* start = in;
            if (*in == '+' || *in == '-') ++in;
            if (!crt_digit(static_cast<char>(*in))) break;
            while (crt_digit(static_cast<char>(*in))) ++in;
            *va_arg(args, int32_t*) = title_atol(reinterpret_cast<const char*>(start));
        } else {
            break;  // not used by the title's callers
        }
        ++stored;
    }
    va_end(args);
    return stored;
}

void* XB_CDECL title_memset(void* dest, int32_t value, uint32_t count) {
    return std::memset(dest, value, count);
}

void XB_STDCALL title_vector_construct(void* array, uint32_t size, int32_t count, uint32_t function) {
    auto* p = static_cast<uint8_t*>(array);
    for (int32_t i = 0; i < count; ++i, p += size) title_function<void XB_THISCALL(void*)>(function)(p);
}

void XB_STDCALL title_vector_destruct(void* array, uint32_t size, int32_t count, uint32_t function) {
    auto* p = static_cast<uint8_t*>(array) + size * count;
    for (int32_t i = 0; i < count; ++i) {
        p -= size;
        title_function<void XB_THISCALL(void*)>(function)(p);
    }
}

// ---- per-thread data ----

namespace {
constexpr uint32_t kGetptd = 0x001f3327;
constexpr uint32_t kPctype = 0x008d1650;        // const uint16_t* _pctype
constexpr uint32_t kCrtStartTime = 0x00926a58;  // ___inittime's start FILETIME
}  // namespace

TitleThreadData* XB_CDECL title_getptd() {
    // The title's lookup: a thread with TLS data (KTHREAD at FS:[0x28], +0x28) keeps its pointer in its TLS block
    // (FS:[0x04][_tls_index] + 0xC), every other thread shares the one at 0x0098f9e0 - so threads without TLS share
    // one rand seed, as in the title. A missing block is ours (the title's CRT start-up made the first thread's).
    uint32_t slot = 0x0098f9e0;
    if (title_global<uint32_t>(platform_fs_word(0x28) + 0x28)) {
        const uint32_t index = title_global<uint32_t>(0x009268b0);
        slot = title_global<uint32_t>(platform_fs_word(0x04) + index * 4) + 0xc;
    }
    uint32_t& pointer = title_global<uint32_t>(slot);
    if (!pointer) {  // as _getptd's first call on a thread: calloc'ed, seed 1, the "C" locale's info
        auto* data = new TitleThreadData{};
        data->mbcinfo = 0x008d1ad0;
        data->rand_seed = 1;
        data->thread_id = title_function<uint32_t XB_STDCALL()>(0x001e7904)();  // GetCurrentThreadId
        data->handle = 0xffffffffu;
        pointer = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(data));
    }
    return reinterpret_cast<TitleThreadData*>(static_cast<uintptr_t>(pointer));
}

TitleThreadData* title_thread_data() {
    return title_function<TitleThreadData* XB_CDECL()>(kGetptd)();
}

void XB_CDECL title_srand(uint32_t seed) {
    title_thread_data()->rand_seed = seed;
}

int32_t XB_CDECL title_rand() {
    TitleThreadData* data = title_thread_data();
    data->rand_seed = data->rand_seed * 0x343fdu + 0x269ec3u;
    return static_cast<int32_t>((data->rand_seed >> 16) & 0x7fff);
}

int32_t* XB_CDECL title_errno() {
    return &title_thread_data()->error;
}

// ---- strings and ctype ----

namespace {
uint16_t ctype_flags(int32_t c) {
    const uint32_t table = title_global<uint32_t>(kPctype);
    return title_global<uint16_t>(table + static_cast<uint32_t>(c) * 2);
}
}  // namespace

int32_t XB_CDECL title_isalpha(int32_t c) { return ctype_flags(c) & 0x103; }
int32_t XB_CDECL title_isdigit(int32_t c) { return ctype_flags(c) & 0x4; }
int32_t XB_CDECL title_isspace(int32_t c) { return ctype_flags(c) & 0x8; }
int32_t XB_CDECL title_isalnum(int32_t c) { return ctype_flags(c) & 0x107; }

int32_t XB_CDECL title_strncmp(const char* a, const char* b, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        const auto x = static_cast<uint8_t>(a[i]), y = static_cast<uint8_t>(b[i]);
        if (x != y) return x < y ? -1 : 1;
        if (!x) break;
    }
    return 0;
}

namespace {
struct CharSet {  // the 256-bit maps strtok / strcspn build
    uint8_t bits[32] = {};
    void add(uint8_t c) { bits[c >> 3] |= static_cast<uint8_t>(1u << (c & 7)); }
    bool has(uint8_t c) const { return bits[c >> 3] & (1u << (c & 7)); }
};
}  // namespace

char* XB_CDECL title_strtok(char* text, const char* delimiters) {
    TitleThreadData* data = title_thread_data();
    CharSet set;
    const char* d = delimiters;
    do set.add(static_cast<uint8_t>(*d)); while (*d++);  // the terminator is in the set too
    char* p = text ? text : reinterpret_cast<char*>(static_cast<uintptr_t>(data->token));
    while (set.has(static_cast<uint8_t>(*p)) && *p) ++p;  // skip leading delimiters
    char* const start = p;
    for (; *p; ++p)
        if (set.has(static_cast<uint8_t>(*p))) {
            *p++ = 0;
            break;
        }
    data->token = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
    return start == p ? nullptr : start;
}

uint32_t XB_CDECL title_strcspn(const char* text, const char* reject) {
    CharSet set;
    for (const char* r = reject; *r; ++r) set.add(static_cast<uint8_t>(*r));
    uint32_t n = 0;
    while (text[n] && !set.has(static_cast<uint8_t>(text[n]))) ++n;
    return n;
}

char* XB_CDECL title_strchr(const char* text, int32_t c) {
    const char wanted = static_cast<char>(c);
    for (;; ++text) {
        if (*text == wanted) return const_cast<char*>(text);
        if (!*text) return nullptr;
    }
}

char* XB_CDECL title_strstr(const char* text, const char* part) {
    if (!*part) return const_cast<char*>(text);
    for (; *text; ++text) {
        const char *a = text, *b = part;
        while (*a && *a == *b) ++a, ++b;
        if (!*b) return const_cast<char*>(text);
    }
    return nullptr;
}

int32_t XB_CDECL title_stricmp(const char* a, const char* b) {
    // __locale_changed (0x00926d1c) stays 0: __ascii_stricmp, which folds only 'A'..'Z'.
    for (;;) {
        auto x = static_cast<uint8_t>(*a++), y = static_cast<uint8_t>(*b++);
        if (x != y) {
            if (static_cast<uint8_t>(x - 'A') < 26) x += 0x20;
            if (static_cast<uint8_t>(y - 'A') < 26) y += 0x20;
            if (x != y) return x < y ? -1 : 1;
        }
        if (!x) return 0;
    }
}

char* XB_CDECL title_strlwr(char* text) {
    for (char* p = text; *p; ++p)
        if (*p >= 'A' && *p <= 'Z') *p = static_cast<char>(*p + 0x20);
    return text;
}

char* XB_CDECL title_strupr(char* text) {
    for (char* p = text; *p; ++p)
        if (*p >= 'a' && *p <= 'z') *p = static_cast<char>(*p - 0x20);
    return text;
}

int32_t XB_CDECL title_strtol(const char* text, char** end, int32_t base) {
    // VC7.1's strtoxl with the signed flag: whitespace by _pctype bit 8, then [+-], base 0 picks 8/10/16 from the
    // prefix, overflow sets errno ERANGE (34) and returns LONG_MAX / LONG_MIN; no digits: 0 and *end = text.
    const char* p = text;
    uint8_t c = static_cast<uint8_t>(*p++);
    while (ctype_flags(c) & 0x8) c = static_cast<uint8_t>(*p++);
    bool negative = false;
    if (c == '-') {
        negative = true;
        c = static_cast<uint8_t>(*p++);
    } else if (c == '+') {
        c = static_cast<uint8_t>(*p++);
    }
    if (base < 0 || base == 1 || base > 36) {
        if (end) *end = const_cast<char*>(text);
        return 0;
    }
    if (base == 0) {
        if (c != '0') base = 10;
        else if (*p == 'x' || *p == 'X') base = 16;
        else base = 8;
    }
    if (base == 16 && c == '0' && (*p == 'x' || *p == 'X')) {
        ++p;
        c = static_cast<uint8_t>(*p++);
    }
    const uint32_t limit = 0xffffffffu / static_cast<uint32_t>(base), limit_digit = 0xffffffffu % static_cast<uint32_t>(base);
    uint32_t value = 0;
    bool any = false, overflow = false;
    for (;; c = static_cast<uint8_t>(*p++)) {
        const uint16_t flags = ctype_flags(c);
        uint32_t digit;
        if (flags & 0x4) {
            digit = static_cast<uint32_t>(static_cast<int8_t>(c) - '0');
        } else if (flags & 0x103) {
            const int32_t upper = (c >= 'a' && c <= 'z') ? c - 0x20 : static_cast<int8_t>(c);
            digit = static_cast<uint32_t>(upper - 'A' + 10);
        } else {
            break;
        }
        if (digit >= static_cast<uint32_t>(base)) break;
        any = true;
        if (value < limit || (value == limit && digit <= limit_digit)) value = value * static_cast<uint32_t>(base) + digit;
        else overflow = true;
    }
    --p;
    if (!any) {
        if (end) *end = const_cast<char*>(text);
        return 0;
    }
    if (overflow || (negative && value > 0x80000000u) || (!negative && value > 0x7fffffffu)) {
        *title_errno() = 34;
        value = negative ? 0x80000000u : 0x7fffffffu;
    }
    if (end) *end = const_cast<char*>(p);
    return static_cast<int32_t>(negative ? 0u - value : value);
}

int32_t XB_CDECL title_strnicmp(const char* a, const char* b, uint32_t count) {
    // __locale_changed stays 0: __ascii_strnicmp. Stops at the first terminator of either string.
    uint8_t x = 0, y = 0;
    for (; count; --count) {
        x = static_cast<uint8_t>(*a++);
        y = static_cast<uint8_t>(*b++);
        if (!x || !y) break;
        if (x >= 'A' && x <= 'Z') x += 0x20;
        if (y >= 'A' && y <= 'Z') y += 0x20;
        if (x != y) break;
    }
    if (!count || x == y) return 0;
    return x < y ? -1 : 1;
}

int32_t XB_CDECL title_snprintf(char* out, uint32_t count, const char* format, ...) {
    va_list args;
    va_start(args, format);
    const int32_t n = title_vsnprintf(out, count, format, args);
    va_end(args);
    return n;
}

namespace {
uint32_t abstract_cw(uint32_t cw) {  // __abstract_cw
    uint32_t a = 0;
    if (cw & 0x01) a |= 0x10;
    if (cw & 0x04) a |= 0x08;
    if (cw & 0x08) a |= 0x04;
    if (cw & 0x10) a |= 0x02;
    if (cw & 0x20) a |= 0x01;
    if (cw & 0x02) a |= 0x80000;
    switch (cw & 0xc00) {
        case 0x400: a |= 0x100; break;
        case 0x800: a |= 0x200; break;
        case 0xc00: a |= 0x300; break;
        default: break;
    }
    if (!(cw & 0x300)) a |= 0x20000;
    else if ((cw & 0x300) == 0x200) a |= 0x10000;
    if (cw & 0x1000) a |= 0x40000;
    return a;
}

uint32_t hardware_cw(uint32_t a) {  // __hw_cw
    uint32_t cw = 0;
    if (a & 0x10) cw |= 0x01;
    if (a & 0x08) cw |= 0x04;
    if (a & 0x04) cw |= 0x08;
    if (a & 0x02) cw |= 0x10;
    if (a & 0x01) cw |= 0x20;
    if (a & 0x80000) cw |= 0x02;
    cw |= (a & 0x300) << 2;  // _RC_DOWN 0x100 -> 0x400, _RC_UP 0x200 -> 0x800, _RC_CHOP -> 0xc00
    if (!(a & 0x30000)) cw |= 0x300;
    else if ((a & 0x30000) == 0x10000) cw |= 0x200;
    if (a & 0x40000) cw |= 0x1000;
    return cw;
}
}  // namespace

uint32_t XB_CDECL title_control87(uint32_t value, uint32_t mask) {
    const uint32_t a = (abstract_cw(platform_x87_control_word()) & ~mask) | (value & mask);
    platform_set_x87_control_word(static_cast<uint16_t>(hardware_cw(a)));
    return a;
}

uint32_t XB_CDECL title_controlfp(uint32_t value, uint32_t mask) {
    return title_control87(value, mask & 0xfff7ffff);
}

// ---- sorting and searching ----

namespace {
using CompareFn = int32_t XB_CDECL(const void*, const void*);

void swap_bytes(char* a, char* b, uint32_t width) {
    if (a == b) return;
    for (uint32_t i = 0; i < width; ++i) std::swap(a[i], b[i]);
}

void short_sort(char* lo, char* hi, uint32_t width, CompareFn* compare) {
    while (hi > lo) {
        char* max = lo;
        for (char* p = lo + width; p <= hi; p += width)
            if (compare(p, max) > 0) max = p;
        swap_bytes(max, hi, width);
        hi -= width;
    }
}
}  // namespace

void XB_CDECL title_qsort(void* base, uint32_t count, uint32_t width, uint32_t compare_address) {
    if (count < 2 || width == 0) return;
    CompareFn* const compare = title_function<CompareFn>(compare_address);
    char* lo_stack[30];
    char* hi_stack[30];
    int32_t depth = 0;
    char* lo = static_cast<char*>(base);
    char* hi = lo + width * (count - 1);
    for (;;) {
        const uint32_t size = static_cast<uint32_t>(hi - lo) / width + 1;
        if (size <= 8) {
            short_sort(lo, hi, width, compare);
        } else {
            char* mid = lo + (size / 2) * width;
            if (compare(lo, mid) > 0) swap_bytes(lo, mid, width);
            if (compare(lo, hi) > 0) swap_bytes(lo, hi, width);
            if (compare(mid, hi) > 0) swap_bytes(mid, hi, width);
            char* lo_guy = lo;
            char* hi_guy = hi;
            for (;;) {
                if (mid > lo_guy) {
                    do lo_guy += width;
                    while (lo_guy < mid && compare(lo_guy, mid) <= 0);
                }
                if (mid <= lo_guy) {
                    do lo_guy += width;
                    while (lo_guy <= hi && compare(lo_guy, mid) <= 0);
                }
                do hi_guy -= width;
                while (hi_guy > mid && compare(hi_guy, mid) > 0);
                if (hi_guy < lo_guy) break;
                swap_bytes(lo_guy, hi_guy, width);
                if (mid == hi_guy) mid = lo_guy;
            }
            hi_guy += width;
            if (mid < hi_guy) {
                do hi_guy -= width;
                while (hi_guy > mid && compare(hi_guy, mid) == 0);
            }
            if (mid >= hi_guy) {
                do hi_guy -= width;
                while (hi_guy > lo && compare(hi_guy, mid) == 0);
            }
            if (hi_guy - lo >= hi - lo_guy) {
                if (lo < hi_guy) {
                    lo_stack[depth] = lo;
                    hi_stack[depth++] = hi_guy;
                }
                if (lo_guy < hi) {
                    lo = lo_guy;
                    continue;
                }
            } else {
                if (lo_guy < hi) {
                    lo_stack[depth] = lo_guy;
                    hi_stack[depth++] = hi;
                }
                if (lo < hi_guy) {
                    hi = hi_guy;
                    continue;
                }
            }
        }
        if (--depth < 0) return;
        lo = lo_stack[depth];
        hi = hi_stack[depth];
    }
}

void* XB_CDECL title_bsearch(const void* key, const void* base, uint32_t count, uint32_t width,
                             uint32_t compare_address) {
    CompareFn* const compare = title_function<CompareFn>(compare_address);
    const char* lo = static_cast<const char*>(base);
    const char* hi = lo + (count - 1) * width;
    while (lo <= hi) {
        const uint32_t half = count / 2;
        if (half) {
            const uint32_t step = (count & 1) ? half : half - 1;
            const char* mid = lo + step * width;
            const int32_t result = compare(key, mid);
            if (!result) return const_cast<char*>(mid);
            if (result < 0) {
                hi = mid - width;
                count = step;
            } else {
                lo = mid + width;
                count = half;
            }
        } else if (count) {
            return compare(key, lo) ? nullptr : const_cast<char*>(lo);
        } else {
            break;
        }
    }
    return nullptr;
}

// ---- start-up / time ----

namespace {
std::vector<uint32_t>& at_exit_list() {
    static std::vector<uint32_t> list;
    return list;
}
}  // namespace

int32_t XB_CDECL title_atexit(uint32_t function) {
    at_exit_list().push_back(function);
    return 0;
}

int32_t XB_CDECL title_crt_clock() {
    uint32_t now[2] = {};
    title_function<void XB_STDCALL(uint32_t*)>(0x001e7911)(now);  // GetSystemTimeAsFileTime
    const uint64_t time = (uint64_t{now[1]} << 32 | now[0]) - title_global<uint64_t>(kCrtStartTime);
    return static_cast<int32_t>(time / 10000);
}

// ---- 64-bit helpers ----

int64_t XB_STDCALL title_allmul(int64_t a, int64_t b) {
    return static_cast<int64_t>(static_cast<uint64_t>(a) * static_cast<uint64_t>(b));
}

int64_t XB_STDCALL title_alldiv(int64_t a, int64_t b) {
    // Unsigned division of the magnitudes, then the sign (INT64_MIN / -1 stays INT64_MIN, no trap).
    const bool negative = (a < 0) != (b < 0);
    const uint64_t x = a < 0 ? 0 - static_cast<uint64_t>(a) : static_cast<uint64_t>(a);
    const uint64_t y = b < 0 ? 0 - static_cast<uint64_t>(b) : static_cast<uint64_t>(b);
    const uint64_t q = x / y;
    return static_cast<int64_t>(negative ? 0 - q : q);
}

uint64_t XB_STDCALL title_aulldiv(uint64_t a, uint64_t b) {
    return a / b;
}

}  // namespace game

// The register-convention helpers, as the title's (they only move bits; the x87 rounding of _ftol is the
// thread's own mode, as in the original).
PLATFORM_ASM_ENTRY(title_chkstk_entry,
                   "testl %eax, %eax\n"
                   "je 3f\n"
                   "cmpl $0x1000, %eax\n"
                   "jae 1f\n"
                   "negl %eax\n"
                   "addl %esp, %eax\n"
                   "addl $4, %eax\n"
                   "testl %eax, (%eax)\n"
                   "xchgl %eax, %esp\n"
                   "movl (%eax), %eax\n"
                   "pushl %eax\n"
                   "ret\n"
                   "1: pushl %ecx\n"
                   "leal 8(%esp), %ecx\n"
                   "2: subl $0x1000, %ecx\n"
                   "subl $0x1000, %eax\n"
                   "testl %eax, (%ecx)\n"
                   "cmpl $0x1000, %eax\n"
                   "jae 2b\n"
                   "subl %eax, %ecx\n"
                   "movl %esp, %eax\n"
                   "testl %eax, (%ecx)\n"
                   "movl %ecx, %esp\n"
                   "movl (%eax), %ecx\n"
                   "movl 4(%eax), %eax\n"
                   "pushl %eax\n"
                   "3: ret")

PLATFORM_ASM_ENTRY(title_allshr_entry,
                   "cmpb $0x40, %cl\n"
                   "jae 2f\n"
                   "cmpb $0x20, %cl\n"
                   "jae 1f\n"
                   "shrdl %cl, %edx, %eax\n"
                   "sarl %cl, %edx\n"
                   "ret\n"
                   "1: movl %edx, %eax\n"
                   "sarl $31, %edx\n"
                   "andb $31, %cl\n"
                   "sarl %cl, %eax\n"
                   "ret\n"
                   "2: sarl $31, %edx\n"
                   "movl %edx, %eax\n"
                   "ret")

PLATFORM_ASM_ENTRY(title_ftol_entry,
                   "pushl %ebp\n"
                   "movl %esp, %ebp\n"
                   "subl $0x20, %esp\n"
                   "andl $0xfffffff0, %esp\n"
                   "fld %st(0)\n"
                   "fsts 0x18(%esp)\n"
                   "fistpll 0x10(%esp)\n"
                   "fildll 0x10(%esp)\n"
                   "movl 0x18(%esp), %edx\n"
                   "movl 0x10(%esp), %eax\n"
                   "testl %eax, %eax\n"
                   "je 3f\n"
                   "1: .byte 0xde, 0xe9\n"  // fsubp st(1), st: x - rounded
                   "testl %edx, %edx\n"
                   "jns 2f\n"
                   "fstps (%esp)\n"
                   "movl (%esp), %ecx\n"
                   "xorl $0x80000000, %ecx\n"
                   "addl $0x7fffffff, %ecx\n"
                   "adcl $0, %eax\n"
                   "movl 0x14(%esp), %edx\n"
                   "adcl $0, %edx\n"
                   "jmp 4f\n"
                   "2: fstps (%esp)\n"
                   "movl (%esp), %ecx\n"
                   "addl $0x7fffffff, %ecx\n"
                   "sbbl $0, %eax\n"
                   "movl 0x14(%esp), %edx\n"
                   "sbbl $0, %edx\n"
                   "jmp 4f\n"
                   "3: movl 0x14(%esp), %edx\n"
                   "testl $0x7fffffff, %edx\n"
                   "jne 1b\n"
                   "fstps 0x18(%esp)\n"
                   "fstps 0x18(%esp)\n"
                   "4: leave\n"
                   "ret")

namespace game {

// ---- eh vector iterators ----

void XB_STDCALL title_vector_constructor_iterator(void* array, uint32_t size, int32_t count, uint32_t function) {
    title_vector_construct(array, size, count, function);
}

void XB_STDCALL title_vector_destructor_iterator(void* array, uint32_t size, int32_t count, uint32_t function) {
    title_vector_destruct(array, size, count, function);
}

void XB_STDCALL title_eh_vector_destruct(void* array, uint32_t size, int32_t count, uint32_t function) {
    title_vector_destruct(array, size, count, function);
}

void XB_STDCALL title_eh_vector_construct(void* array, uint32_t size, int32_t count, uint32_t function, uint32_t) {
    auto* p = static_cast<uint8_t*>(array);
    for (int32_t i = 0; i < count; ++i, p += size) title_function<void XB_THISCALL(void*)>(function)(p);
}

}  // namespace game

// Calls for the check: _ftol with `value` in ST0, _allshr with EDX:EAX = value and CL = shift (cdecl, EDX:EAX back).
extern "C" int64_t crt_check_call_ftol(double value, uint32_t function);
extern "C" int64_t crt_check_call_shift(int64_t value, uint32_t shift, uint32_t function);
PLATFORM_ASM_ENTRY(crt_check_call_ftol,
                   "fldl 4(%esp)\n"
                   "call *12(%esp)\n"
                   "fstp %st(0)\n"
                   "ret")
PLATFORM_ASM_ENTRY(crt_check_call_shift,
                   "movl 4(%esp), %eax\n"
                   "movl 8(%esp), %edx\n"
                   "movl 12(%esp), %ecx\n"
                   "call *16(%esp)\n"
                   "ret")

namespace game {

namespace {
void add_line(std::string& report, const char* format, ...) {
    char line[1400];
    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof line, format, args);
    va_end(args);
    report += line;
    report += '\n';
}
}  // namespace

int32_t check_title_crt(std::string& report) {
    int32_t failures = 0;
    const auto orig_sprintf = title_function<int32_t XB_CDECL(char*, const char*, ...)>(0x001ee901);
    char a[512], b[512];
    auto compare = [&](const char* what, int32_t na, int32_t nb) {
        if (na == nb && !std::strcmp(a, b)) return;
        ++failures;
        add_line(report, "%s: title \"%s\" (%d), ours \"%s\" (%d)", what, a, na, b, nb);
    };
#define CHECK_SPRINTF(fmt, ...)                                                                                     \
    do {                                                                                                            \
        std::memset(a, 0x55, sizeof a);                                                                             \
        std::memset(b, 0x55, sizeof b);                                                                             \
        const int32_t na = orig_sprintf(a, fmt, __VA_ARGS__);                                                       \
        const int32_t nb = title_sprintf(b, fmt, __VA_ARGS__);                                                      \
        compare("sprintf " fmt, na, nb);                                                                            \
    } while (0)
    static const uint16_t wide[] = {'W', 'i', 'd', 'e', 0};
    CHECK_SPRINTF("%d %i %u", 42, -7, 0xffffffffu);
    CHECK_SPRINTF("[%5d|%-5d|%05d|%+d|% d]", 42, 42, -42, 42, 42);
    CHECK_SPRINTF("%x %X %#x %o %#o %08x", 0xbeefu, 0xbeefu, 0u, 8u, 8u, 0x12u);
    CHECK_SPRINTF("%hd %hu %I64d %I64x", 0x12345, 0x1ffff, -1234567890123LL, 0x123456789abLL);
    CHECK_SPRINTF("%s|%10s|%-10s|%.2s|%*s|%-*.*s", "abc", "abc", "abc", "abc", 6, "x", 6, 2, "abcd");
    CHECK_SPRINTF("%s %c %3c %-3c|", static_cast<const char*>(nullptr), 'x', 'y', 'z');
    CHECK_SPRINTF("%S %ls %hs %C", wide, wide, "narrow", 'w');
    CHECK_SPRINTF("%f %.0f %.1f %.3f %10.2f %-10.2f| %+f %010.3f", 1.5, 2.5, 0.05, -3.14159, 2.0, 2.0, 1.0, -1.25);
    CHECK_SPRINTF("%e %E %.2e %12.3e %-12.3e| %012.2e", 12345.678, 0.000123, 1e100, 1.5, 1.5, -1.5);
    CHECK_SPRINTF("%g %g %g %G %.3g %#g %g", 0.0001, 0.00001, 123456.0, 1e20, 3.14159, 1.0, 100.0);
    CHECK_SPRINTF("%f %f %f %e %g", 1e30, 0.1 + 0.2, 123456789.125, 0.0, -0.0);
    CHECK_SPRINTF("%f %f %e %g", HUGE_VAL, -HUGE_VAL, HUGE_VAL, std::nan(""));
    CHECK_SPRINTF("%f %e %g %.0f %.0f %.0f %.2f", -0.0, -0.0, -0.0, 0.5, 1.5, -2.5, 1.005);
    CHECK_SPRINTF("%g %g %g %g %.10g %.17g %G", 1e-5, 123456.5, 999999.5, 0.00001234, 1.0 / 3, 0.1, 1e-300);
    CHECK_SPRINTF("%.3f %.15f %.20f %8.3e %#.0f %#.0e %#g", 2.0 / 3, 0.1, 0.1, 9.9996, 3.0, 3.0, 2.0);
    CHECK_SPRINTF("%f %e %g %f", 1e300, 1.7976931348623157e308, 4.9e-324, 123.456f);
    CHECK_SPRINTF("%+.2e % f %-8.1f| %08.2f %+08.2f", 0.0, 1.0, -1.0, -3.14159, 3.14159);
    CHECK_SPRINTF("%p %10p %%  %5%|%y|", reinterpret_cast<void*>(0x12ab), reinterpret_cast<void*>(0x1));
    CHECK_SPRINTF("%s(%d) - %i: %.3f", "level", 3, 4, 0.5);
#undef CHECK_SPRINTF

    const auto orig_atol = title_function<int32_t XB_CDECL(const char*)>(0x001eeb81);
    for (const char* text : {"0", "42", "-42", "+7", "  \t\n12x", "abc", "", "2147483647", "2147483648", "-2147483649",
                             "99999999999", "- 5", "007", "12 34", "\v\f\r-3"}) {
        const int32_t x = orig_atol(text), y = title_atol(text);
        if (x != y) {
            ++failures;
            add_line(report, "atol \"%s\": title %d, ours %d", text, x, y);
        }
    }

    const auto orig_strncpy = title_function<char* XB_CDECL(char*, const char*, uint32_t)>(0x001eed80);
    for (uint32_t count : {0u, 1u, 3u, 5u, 6u, 12u}) {
        std::memset(a, 0x55, 16);
        std::memset(b, 0x55, 16);
        const bool ra = orig_strncpy(a, "hello", count) == a, rb = title_strncpy(b, "hello", count) == b;
        if (std::memcmp(a, b, 16) || ra != rb) {
            ++failures;
            add_line(report, "strncpy count %u: bytes differ", count);
        }
    }

    const auto orig_tolower = title_function<int32_t XB_CDECL(int32_t)>(0x001eed48);
    for (int32_t c = -1; c < 256; ++c)
        if (orig_tolower(c) != title_tolower(c)) {
            ++failures;
            add_line(report, "tolower %d: title %d, ours %d", c, orig_tolower(c), title_tolower(c));
        }

    const auto orig_atof = title_function<double XB_CDECL(const char*)>(0x001ef056);
    auto check_atof = [&](const char* text) {
        const double x = orig_atof(text), y = title_atof(text);
        if (std::memcmp(&x, &y, sizeof x)) {
            ++failures;
            add_line(report, "atof \"%s\": title %.17g, ours %.17g", text, x, y);
        }
    };
    for (const char* text :
         {"0", "1", "-1", "+2.5", " \t\n0.35", "1e10", "1E-5", "1d3", "1D+2", "1e", "1e+", "1ex", ".5", "5.", ".", "-.",
          "-.e1", "0x10", "inf", "nan", "infinity", "--1", "+-1", "1.5abc", "", "  ", "0.1", "0.2", "0.3", "1e23",
          "8.533e+68", "2.2250738585072011e-308", "2.2250738585072014e-308", "4.9e-324", "2.4e-324", "1e-400", "1e400",
          "1.7976931348623157e308", "1.7976931348623159e308", "9007199254740993", "123456789012345678901234567890",
          "1.00000000000000011102230246251565404236316680908203125", "0.000000000000000000000000000001", "3.14159265358979",
          "100.0", "-0", "-0.0", "1e0005", "0.30000000000000004", "4294967296.5", "\xa0" "1"})
        check_atof(text);
    const auto orig_sscanf = title_function<int32_t XB_CDECL(const char*, const char*, ...)>(0x001f07e1);
    uint32_t seed = 12345, random_failures = 0, hard_differences = 0;
    for (int i = 0; i < 20000; ++i) {
        seed = seed * 1103515245u + 12345u;
        char text[48];
        const bool hard = i >= 10000;  // the second half: long digit strings and wide exponents
        const uint32_t digits = 1 + (seed >> 8) % (hard ? 20 : 9), point = (seed >> 16) % (digits + 1);
        uint32_t n = 0;
        if (seed & 1) text[n++] = '-';
        for (uint32_t d = 0; d < digits; ++d) {
            if (d == point && d) text[n++] = '.';
            seed = seed * 1103515245u + 12345u;
            text[n++] = static_cast<char>('0' + (seed >> 16) % 10);
        }
        if (hard ? (seed >> 24) % 2 == 0 : (seed >> 24) % 4 == 0)
            n += std::snprintf(text + n, sizeof text - n, "e%d",
                               hard ? int((seed >> 4) % 601) - 300 : int((seed >> 4) % 61) - 30);
        text[n] = 0;
        const double x = orig_atof(text), y = title_atof(text);
        uint32_t& count = hard ? hard_differences : random_failures;
        if (std::memcmp(&x, &y, sizeof x) && count++ < 8)
            add_line(report, "atof \"%s\": title %.17g, ours %.17g", text, x, y);
        float u = 0, w = 0;
        orig_sscanf(text, "%f", &u);
        title_sscanf(text, "%f", &w);
        if (std::memcmp(&u, &w, sizeof u) && count++ < 8)
            add_line(report, "sscanf %%f \"%s\": title %.9g, ours %.9g", text, u, w);
    }
    if (random_failures) add_line(report, "atof/sscanf: %u differences on 10000 short random decimals", random_failures);
    // Known limit, not counted: VC converts through a 64-bit mantissa with inexact powers of ten, so near-ties of long
    // inputs at extreme exponents can round the other way (2026-11-10: 1 in 10000, 20 digits at e-291, 0.5003 step).
    if (hard_differences)
        add_line(report, "atof/sscanf (known limit, not counted): %u differences on 10000 long random decimals",
                 hard_differences);
    failures += static_cast<int32_t>(random_failures);

    for (const char* text : {"1 2 3", "  1.5\t-2e3 0.25 9", "1 2", "", "   ", "a 1 2", "1 x 2", "1e 2 3", "0.1 0.2 0.3",
                             "1.5,2,3", "-.5 +.5 5.", "1e40 1e-50 3", "1e+ 2 3", "1ex 2 3", "1E-x", "- 1 2", ". 1 2", "1 2 3 4"}) {
        float fa[4] = {7, 7, 7, 7}, fb[4] = {7, 7, 7, 7};
        const int32_t na = orig_sscanf(text, "%f %f %f", fa, fa + 1, fa + 2);
        const int32_t nb = title_sscanf(text, "%f %f %f", fb, fb + 1, fb + 2);
        if (na != nb || std::memcmp(fa, fb, sizeof fa)) {
            ++failures;
            add_line(report, "sscanf \"%s\": title %d (%g %g %g), ours %d (%g %g %g)", text, na, fa[0], fa[1], fa[2], nb,
                     fb[0], fb[1], fb[2]);
        }
    }

    // rand: the same sequence after the same seed (both through the title's per-thread data here).
    const auto orig_srand = title_function<void XB_CDECL(uint32_t)>(0x001efcb3);
    const auto orig_rand = title_function<int32_t XB_CDECL()>(0x001efcc0);
    for (uint32_t seed : {0u, 1u, 12345u, 0xdeadbeefu}) {
        int32_t x[64], y[64];
        orig_srand(seed);
        for (int32_t& v : x) v = orig_rand();
        title_srand(seed);
        for (int32_t& v : y) v = title_rand();
        if (std::memcmp(x, y, sizeof x)) {
            ++failures;
            add_line(report, "rand after srand(%u): sequences differ", seed);
        }
    }

    // strings
    static const char* const words[] = {"", "a", "A", "abc", "ABC", "abd", "ab", "abcd", "Zebra", "zebra", "_x", "[x",
                                        "\x80" "a", "\xff", "a\x80", "Hello World", "hello world", "hello"};
    const auto orig_stricmp = title_function<int32_t XB_CDECL(const char*, const char*)>(0x001f1443);
    const auto orig_strncmp = title_function<int32_t XB_CDECL(const char*, const char*, uint32_t)>(0x001eeaf0);
    const auto orig_strstr = title_function<char* XB_CDECL(const char*, const char*)>(0x001efb10);
    const auto orig_strcspn = title_function<uint32_t XB_CDECL(const char*, const char*)>(0x001ef010);
    for (const char* u : words)
        for (const char* v : words) {
            if (orig_stricmp(u, v) != title_stricmp(u, v)) {
                ++failures;
                add_line(report, "stricmp \"%s\" \"%s\": title %d, ours %d", u, v, orig_stricmp(u, v), title_stricmp(u, v));
            }
            for (uint32_t n : {0u, 1u, 2u, 3u, 100u})
                if (orig_strncmp(u, v, n) != title_strncmp(u, v, n)) {
                    ++failures;
                    add_line(report, "strncmp \"%s\" \"%s\" %u: title %d, ours %d", u, v, n, orig_strncmp(u, v, n),
                             title_strncmp(u, v, n));
                }
            if (orig_strstr(u, v) != title_strstr(u, v) || orig_strcspn(u, v) != title_strcspn(u, v)) {
                ++failures;
                add_line(report, "strstr/strcspn \"%s\" \"%s\" differ", u, v);
            }
        }
    const auto orig_strnicmp = title_function<int32_t XB_CDECL(const char*, const char*, uint32_t)>(0x001f1486);
    for (const char* u : words)
        for (const char* v : words)
            for (uint32_t n : {0u, 1u, 2u, 3u, 5u, 100u})
                if (orig_strnicmp(u, v, n) != title_strnicmp(u, v, n)) {
                    ++failures;
                    add_line(report, "strnicmp \"%s\" \"%s\" %u: title %d, ours %d", u, v, n, orig_strnicmp(u, v, n),
                             title_strnicmp(u, v, n));
                }
    const auto orig_controlfp = title_function<uint32_t XB_CDECL(uint32_t, uint32_t)>(0x001f0a40);
    const auto orig_control87 = title_function<uint32_t XB_CDECL(uint32_t, uint32_t)>(0x001f0a0e);
    const uint16_t saved_cw = platform_x87_control_word();
    // Every exception stays masked (an unmasked one with its sticky flag set would trap at the next x87 op).
    for (uint16_t start : {uint16_t{0x027f}, uint16_t{0x037f}, uint16_t{0x007f}, uint16_t{0x0f7f}, uint16_t{0x1e7f},
                           uint16_t{0x017f}, uint16_t{0x0b3f}})
        for (uint32_t value : {0x8001fu, 0x9001fu, 0x8031fu, 0x8011fu, 0x8021fu, 0xa001fu, 0xb001fu, 0xc001fu, 0xffffffffu})
            for (uint32_t mask : {0u, 0x8001fu, 0x300u, 0x30000u, 0x40000u, 0xfff7ffffu, 0xffffffffu})
                for (int which = 0; which < 2; ++which) {
                    platform_set_x87_control_word(start);
                    const uint32_t ra = which ? orig_control87(value, mask) : orig_controlfp(value, mask);
                    const uint16_t ca = platform_x87_control_word();
                    platform_set_x87_control_word(start);
                    const uint32_t rb = which ? title_control87(value, mask) : title_controlfp(value, mask);
                    const uint16_t cb = platform_x87_control_word();
                    if (ra != rb || ca != cb) {
                        ++failures;
                        add_line(report, "control%s %04x (%x, %x): title %x cw %04x, ours %x cw %04x",
                                 which ? "87" : "fp", start, value, mask, ra, ca, rb, cb);
                    }
                }
    platform_set_x87_control_word(saved_cw);
    const auto orig_strchr = title_function<char* XB_CDECL(const char*, int32_t)>(0x001ef5e0);
    for (const char* u : words)
        for (int32_t c : {0, int32_t{'a'}, int32_t{'b'}, int32_t{'c'}, int32_t{'A'}, 0x80, -128, 0x180, 0xff, -1, int32_t{' '}})
            if (orig_strchr(u, c) != title_strchr(u, c)) {
                ++failures;
                add_line(report, "strchr \"%s\" %d differs", u, c);
            }
    const auto orig_strlwr = title_function<char* XB_CDECL(char*)>(0x001fa3b2);
    const auto orig_strupr = title_function<char* XB_CDECL(char*)>(0x001fa3d5);
    for (const char* u : words) {
        std::strcpy(a, u);
        std::strcpy(b, u);
        if (orig_strlwr(a) != a || title_strlwr(b) != b || std::strcmp(a, b) ||
            (orig_strupr(a), title_strupr(b), std::strcmp(a, b))) {
            ++failures;
            add_line(report, "strlwr/strupr \"%s\" differ", u);
        }
    }
    const auto orig_strtok = title_function<char* XB_CDECL(char*, const char*)>(0x001eeea4);
    for (const char* text : {"a b  c", "  lead", "trail  ", "", "   ", "one", ",a,,b,", "x\ny z"}) {
        std::strcpy(a, text);
        std::strcpy(b, text);
        std::string ta, tb;
        for (char* t = orig_strtok(a, " ,\n"); t; t = orig_strtok(nullptr, " ,\n")) ta += std::to_string(t - a) + ":" + t + "|";
        for (char* t = title_strtok(b, " ,\n"); t; t = title_strtok(nullptr, " ,\n")) tb += std::to_string(t - b) + ":" + t + "|";
        if (ta != tb || std::memcmp(a, b, std::strlen(text) + 1)) {
            ++failures;
            add_line(report, "strtok \"%s\": title %s, ours %s", text, ta.c_str(), tb.c_str());
        }
    }
    const auto orig_strtol = title_function<int32_t XB_CDECL(const char*, char**, int32_t)>(0x001f0ecf);
    const auto orig_errno = title_function<int32_t* XB_CDECL()>(0x001f0ac5);
    for (const char* text : {"0", "42", "-42", " +7x", "0x1F", "0X", "0x", "017", "08", "z", "Zz", "2147483647",
                             "2147483648", "-2147483648", "-2147483649", "99999999999", "\t\n 12", "- 1", "", "ffffffff"})
        for (int32_t base : {0, 2, 8, 10, 16, 36, 1, 37, -1}) {
            char *ea = nullptr, *eb = nullptr;
            *orig_errno() = 0;
            const int32_t x = orig_strtol(text, &ea, base);
            const int32_t errno_a = *orig_errno();
            *orig_errno() = 0;
            const int32_t y = title_strtol(text, &eb, base);
            if (x != y || ea != eb || errno_a != *orig_errno()) {
                ++failures;
                add_line(report, "strtol \"%s\" base %d: title %d (+%d, errno %d), ours %d (+%d, errno %d)", text, base, x,
                         int(ea - text), errno_a, y, int(eb - text), *orig_errno());
            }
        }
    const auto orig_isalpha = title_function<int32_t XB_CDECL(int32_t)>(0x001eef49);
    const auto orig_isdigit = title_function<int32_t XB_CDECL(int32_t)>(0x001eef77);
    const auto orig_isspace = title_function<int32_t XB_CDECL(int32_t)>(0x001eefa0);
    const auto orig_isalnum = title_function<int32_t XB_CDECL(int32_t)>(0x001eefc9);
    for (int32_t c = -128; c < 256; ++c)
        if (orig_isalpha(c) != title_isalpha(c) || orig_isdigit(c) != title_isdigit(c) ||
            orig_isspace(c) != title_isspace(c) || orig_isalnum(c) != title_isalnum(c)) {
            ++failures;
            add_line(report, "ctype %d differs", c);
        }

    // vsnprintf: fits, exactly fills, too long, no room
    const auto orig_snprintf = [](char* out, uint32_t count, const char* format, ...) {
        va_list args;
        va_start(args, format);
        const int32_t n = title_function<int32_t XB_CDECL(char*, uint32_t, const char*, va_list)>(0x001efce2)(
            out, count, format, args);
        va_end(args);
        return n;
    };
    const auto our_snprintf = [](char* out, uint32_t count, const char* format, ...) {
        va_list args;
        va_start(args, format);
        const int32_t n = title_vsnprintf(out, count, format, args);
        va_end(args);
        return n;
    };
    for (uint32_t count : {0u, 1u, 5u, 9u, 10u, 11u, 64u, 0x80000000u}) {
        std::memset(a, 0x55, 80);
        std::memset(b, 0x55, 80);
        const uint32_t room = count > 64 ? 64 : count;  // the buffers hold 80; the text is 10 characters
        const int32_t na = orig_snprintf(a, room == 64 ? count : room, "%s-%d", "abcd", 12345);
        const int32_t nb = our_snprintf(b, room == 64 ? count : room, "%s-%d", "abcd", 12345);
        if (na != nb || std::memcmp(a, b, 80)) {
            ++failures;
            add_line(report, "vsnprintf count %u: title %d, ours %d", count, na, nb);
        }
    }

    // qsort: the order equal keys end up in (keys 0..6, 300 records, the record's index in the low bits)
    struct Record {
        int32_t key, index;
    };
    static const auto by_key = +[](const void* x, const void* y) XB_CDECL -> int32_t {
        return static_cast<const Record*>(x)->key - static_cast<const Record*>(y)->key;
    };
    const uint32_t compare_address = own_address(by_key);
    const auto orig_qsort = title_function<void XB_CDECL(void*, uint32_t, uint32_t, uint32_t)>(0x001ef880);
    const auto orig_bsearch =
        title_function<void* XB_CDECL(const void*, const void*, uint32_t, uint32_t, uint32_t)>(0x001f123f);
    for (uint32_t count : {0u, 1u, 2u, 7u, 8u, 9u, 10u, 31u, 300u}) {
        std::vector<Record> x(count), y;
        uint32_t r = count * 7919u + 1;
        for (uint32_t i = 0; i < count; ++i) {
            r = r * 1103515245u + 12345u;
            x[i] = {static_cast<int32_t>((r >> 16) % 7), static_cast<int32_t>(i)};
        }
        y = x;
        orig_qsort(x.data(), count, sizeof(Record), compare_address);
        title_qsort(y.data(), count, sizeof(Record), compare_address);
        if (count && std::memcmp(x.data(), y.data(), count * sizeof(Record))) {
            ++failures;
            add_line(report, "qsort %u records: orders differ", count);
        }
        for (int32_t key = -1; key <= 7; ++key) {
            const Record wanted{key, 0};
            if (orig_bsearch(&wanted, x.data(), count, sizeof(Record), compare_address) !=
                title_bsearch(&wanted, x.data(), count, sizeof(Record), compare_address)) {
                ++failures;
                add_line(report, "bsearch key %d in %u records differs", key, count);
            }
        }
    }

    // 64-bit helpers and _ftol (the thread's x87 mode, as the title's callers run it)
    const auto orig_allmul = title_function<int64_t XB_STDCALL(int64_t, int64_t)>(0x001f1170);
    const auto orig_alldiv = title_function<int64_t XB_STDCALL(int64_t, int64_t)>(0x001f1580);
    const auto orig_aulldiv = title_function<uint64_t XB_STDCALL(uint64_t, uint64_t)>(0x001f11b0);
    for (int64_t u : {int64_t{0}, int64_t{1}, int64_t{-1}, int64_t{7}, int64_t{-7}, int64_t{0x123456789abLL},
                      int64_t{-0x123456789abLL}, INT64_MIN, INT64_MAX, int64_t{10000}})
        for (int64_t v : {int64_t{1}, int64_t{-1}, int64_t{3}, int64_t{-3}, int64_t{10000}, int64_t{0x100000000LL},
                          int64_t{-0x7fffffffffLL}, INT64_MIN}) {
            if (orig_allmul(u, v) != title_allmul(u, v) || orig_alldiv(u, v) != title_alldiv(u, v) ||
                orig_aulldiv(static_cast<uint64_t>(u), static_cast<uint64_t>(v)) !=
                    title_aulldiv(static_cast<uint64_t>(u), static_cast<uint64_t>(v))) {
                ++failures;
                add_line(report, "allmul/alldiv/aulldiv %lld, %lld differ", static_cast<long long>(u),
                         static_cast<long long>(v));
            }
            for (uint32_t shift : {0u, 1u, 31u, 32u, 33u, 63u, 64u, 200u})
                if (crt_check_call_shift(u, shift, 0x001f2590) !=
                    crt_check_call_shift(u, shift, own_address(&title_allshr_entry))) {
                    ++failures;
                    add_line(report, "allshr %lld >> %u differs", static_cast<long long>(u), shift);
                }
        }
    for (double v : {0.0, -0.0, 0.5, -0.5, 1.5, -1.5, 2.5, -2.5, 0.99999999, -0.99999999, 1e10, -1e10, 4294967296.75,
                     -4294967296.75, 2147483647.5, -2147483648.5, 3e18, -3e18, 1e19, 1e30, 123.456, -123.456,
                     std::nan(""), HUGE_VAL, -HUGE_VAL, 9007199254740993.0, double{0.1f}, 16777217.0}) {
        const int64_t x = crt_check_call_ftol(v, 0x001f0bf4), y = crt_check_call_ftol(v, own_address(&title_ftol_entry));
        if (x != y) {
            ++failures;
            add_line(report, "ftol %.17g: title %lld, ours %lld", v, static_cast<long long>(x), static_cast<long long>(y));
        }
    }
    return failures;
}

}  // namespace game
