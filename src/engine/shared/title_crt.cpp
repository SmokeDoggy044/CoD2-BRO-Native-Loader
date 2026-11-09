#include "title_crt.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
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

int32_t title_vsprintf(char* out, const char* format, va_list args) {
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
    std::memcpy(out, result.c_str(), result.size() + 1);
    return static_cast<int32_t>(result.size());
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
    return failures;
}

}  // namespace game
