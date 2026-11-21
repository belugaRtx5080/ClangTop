// ClangTop++ - unit rounding / decimal precision formatter
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

#include "../config.hpp"

namespace ct {

class UnitFormatter {
 public:
  UnitFormatter() = default;
  explicit UnitFormatter(const Settings& s) : unit_(s.unit), prec_(s.decimals), uptime_(s.uptime) {}

  void configure(const Settings& s) { unit_ = s.unit; prec_ = s.decimals; uptime_ = s.uptime; }

  int precision() const { return prec_; }
  UnitMode unit() const { return unit_; }

  static std::string fixed(double v, int digits) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", digits, std::isfinite(v) ? v : 0.0);
    return buf;
  }

  std::string num(double v) const { return fixed(v, prec_); }

  std::string num(double v, int extra) const {
    int d = prec_ + extra;
    if (d < 0) d = 0;
    if (d > 4) d = 4;
    return fixed(v, d);
  }

  // Structured byte scaling. `override_unit` allows callers to force e.g. GB.
  std::string bytes(double value, UnitMode override_unit = UnitMode::Auto) const {
    if (!std::isfinite(value)) value = 0;
    bool neg = value < 0;
    double v = std::fabs(value);
    UnitMode u = override_unit == UnitMode::Auto ? unit_ : override_unit;
    const char* suffix = "B";
    int digits = prec_;
    switch (u) {
      case UnitMode::Bytes:
        suffix = "B";
        digits = 0;
        break;
      case UnitMode::KB: v /= 1024.0; suffix = "KB"; break;
      case UnitMode::MB: v /= 1024.0 * 1024.0; suffix = "MB"; break;
      case UnitMode::GB: v /= 1024.0 * 1024.0 * 1024.0; suffix = "GB"; break;
      case UnitMode::Auto: {
        static const char* sfx[] = {"B", "KB", "MB", "GB", "TB"};
        int i = 0;
        while (v >= 1024.0 && i < 4) { v /= 1024.0; ++i; }
        suffix = sfx[i];
        digits = (i == 0) ? 0 : prec_;
        break;
      }
    }
    std::string out;
    if (neg) out += "-";
    out += fixed(v, digits);
    out += " ";
    out += suffix;
    return out;
  }

  // Compact byte string (for chart axis labels): 1.2G / 340M / 12K / 512B
  std::string bytes_short(double value) const {
    double v = std::fabs(value);
    const char* sign = value < 0 ? "-" : "";
    char buf[64];
    if (v >= 1024.0 * 1024.0 * 1024.0)
      std::snprintf(buf, sizeof(buf), "%s%.*fG", sign, prec_, v / (1024.0 * 1024.0 * 1024.0));
    else if (v >= 1024.0 * 1024.0)
      std::snprintf(buf, sizeof(buf), "%s%.*fM", sign, prec_, v / (1024.0 * 1024.0));
    else if (v >= 1024.0)
      std::snprintf(buf, sizeof(buf), "%s%.*fK", sign, prec_, v / 1024.0);
    else
      std::snprintf(buf, sizeof(buf), "%s%.0fB", sign, v);
    return buf;
  }

  std::string rate(double bps) const { return bytes(bps) + "/s"; }
  std::string rate_short(double bps) const { return bytes_short(bps) + "/s"; }

  std::string pct(double v) const { return fixed(v, prec_) + "%"; }

  std::string watts(double w) const { return fixed(w, prec_ + 1) + " W"; }
  std::string volts(double v) const { return fixed(v, prec_ + 1) + " V"; }
  std::string amps(double a) const { return fixed(a, prec_ + 1) + " A"; }
  std::string watt_hours(double wh) const { return fixed(wh, prec_) + " Wh"; }
  std::string temp(double c) const { return fixed(c, prec_) + "\xC2\xB0" "C"; }
  std::string mhz(double m) const { return fixed(m / 1000.0, prec_) + " GHz"; }

  // DD:HH:MM:SS
  static std::string dur_clock(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0) seconds = 0;
    uint64_t s = static_cast<uint64_t>(seconds);
    uint64_t d = s / 86400;
    uint64_t h = (s % 86400) / 3600;
    uint64_t m = (s % 3600) / 60;
    uint64_t sec = s % 60;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%02llu:%02llu:%02llu:%02llu",
                  static_cast<unsigned long long>(d), static_cast<unsigned long long>(h),
                  static_cast<unsigned long long>(m), static_cast<unsigned long long>(sec));
    return buf;
  }

  // 3d 04h 12m
  static std::string dur_compact(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0) seconds = 0;
    uint64_t s = static_cast<uint64_t>(seconds);
    uint64_t d = s / 86400;
    uint64_t h = (s % 86400) / 3600;
    uint64_t m = (s % 3600) / 60;
    char buf[64];
    if (d > 0) std::snprintf(buf, sizeof(buf), "%llud %02lluh %02llum",
                             (unsigned long long)d, (unsigned long long)h, (unsigned long long)m);
    else if (h > 0) std::snprintf(buf, sizeof(buf), "%lluh %02llum",
                                  (unsigned long long)h, (unsigned long long)m);
    else std::snprintf(buf, sizeof(buf), "%llum %02llus", (unsigned long long)m,
                       (unsigned long long)(s % 60));
    return buf;
  }

  std::string duration(double seconds) const {
    return uptime_ == UptimeFmt::Compact ? dur_compact(seconds) : dur_clock(seconds);
  }

  // "2 days, 3 hours, 4 minutes" - comparable to `uptime -p`.
  static std::string uptime_pretty(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0) seconds = 0;
    uint64_t s = static_cast<uint64_t>(seconds);
    uint64_t d = s / 86400, h = (s % 86400) / 3600, m = (s % 3600) / 60;
    std::string out = "up ";
    bool any = false;
    auto add = [&](uint64_t n, const char* one, const char* many) {
      if (!n) return;
      if (any) out += ", ";
      out += std::to_string(n) + " " + (n == 1 ? one : many);
      any = true;
    };
    add(d, "day", "days");
    add(h, "hour", "hours");
    add(m, "minute", "minutes");
    if (!any) out += "0 minutes";
    return out;
  }

  std::string uptime(double seconds) const {
    switch (uptime_) {
      case UptimeFmt::Long: return dur_clock(seconds);
      case UptimeFmt::Compact: return dur_compact(seconds);
      case UptimeFmt::Clock: return uptime_pretty(seconds);
    }
    return dur_clock(seconds);
  }

 private:
  UnitMode unit_ = UnitMode::Auto;
  int prec_ = 1;
  UptimeFmt uptime_ = UptimeFmt::Long;
};

}  // namespace ct
