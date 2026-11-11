#pragma once

// NT/FILETIME time: 100 ns units since 1601-01-01 (a Monday), proleptic Gregorian calendar (H. Hinnant's
// days-from-civil algorithms, shifted to the 1601 epoch). Header-only: the kernel's RtlTime* exports and our own
// XAPI time functions share it.

#include <chrono>
#include <cstdint>

namespace platform {

struct TimeFields {  // TIME_FIELDS
    int16_t year, month, day, hour, minute, second, millisecond, weekday;
};
static_assert(sizeof(TimeFields) == 16);

namespace civil_detail {

constexpr int64_t kTicksPerMillisecond = 10'000;
constexpr int64_t kTicksPerDay = 864'000'000'000;
constexpr int64_t kDaysFrom1601To1970 = 134'774;

// Days since 1970-01-01.
inline int64_t days_from_civil(int64_t y, int64_t m, int64_t d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

inline void civil_from_days(int64_t z, int64_t& y, int64_t& m, int64_t& d) {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const int64_t doe = z - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp + (mp < 10 ? 3 : -9);
    y = yoe + era * 400 + (m <= 2);
}

}  // namespace civil_detail

inline TimeFields time_to_fields(int64_t time) {
    using namespace civil_detail;
    const int64_t days = time / kTicksPerDay;
    int64_t rest = time % kTicksPerDay;
    int64_t y, m, d;
    civil_from_days(days - kDaysFrom1601To1970, y, m, d);
    TimeFields fields;
    fields.year = static_cast<int16_t>(y);
    fields.month = static_cast<int16_t>(m);
    fields.day = static_cast<int16_t>(d);
    fields.weekday = static_cast<int16_t>((days + 1) % 7);  // 1601-01-01 was a Monday
    fields.millisecond = static_cast<int16_t>(rest / kTicksPerMillisecond % 1000);
    rest /= kTicksPerMillisecond * 1000;
    fields.second = static_cast<int16_t>(rest % 60);
    fields.minute = static_cast<int16_t>(rest / 60 % 60);
    fields.hour = static_cast<int16_t>(rest / 3600);
    return fields;
}

// False (and *time untouched) for fields out of range; the weekday is ignored.
inline bool fields_to_time(const TimeFields& f, int64_t* time) {
    using namespace civil_detail;
    if (f.year < 1601 || f.month < 1 || f.month > 12 || f.day < 1 || f.hour < 0 || f.hour > 23 || f.minute < 0 ||
        f.minute > 59 || f.second < 0 || f.second > 59 || f.millisecond < 0 || f.millisecond > 999)
        return false;
    const int64_t days = days_from_civil(f.year, f.month, f.day) + kDaysFrom1601To1970;
    int64_t y, m, d;  // reject days past the end of the month (Feb 30 -> Mar 2)
    civil_from_days(days - kDaysFrom1601To1970, y, m, d);
    if (m != f.month) return false;
    *time = days * kTicksPerDay +
            ((f.hour * 3600 + f.minute * 60 + f.second) * 1000LL + f.millisecond) * kTicksPerMillisecond;
    return true;
}

// The current UTC time in those units.
inline int64_t system_time_now() {
    const auto since_1970 =
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch());
    return since_1970.count() / 100 + civil_detail::kDaysFrom1601To1970 * civil_detail::kTicksPerDay;
}

}  // namespace platform
