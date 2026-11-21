// ClangTop++ v0.3.0 - global settings
#pragma once
#include <cstdint>
#include <string>

namespace ct {

inline constexpr const char* kAppName = "ClangTop++";
inline constexpr const char* kVersion = "0.3.0";

// Unit rounding applied to every chart / table value.
enum class UnitMode : int { Auto = 0, Bytes = 1, KB = 2, MB = 3, GB = 4 };
enum class UptimeFmt : int { Long = 0, Compact = 1, Clock = 2 };
enum class ThemeId : int { Gruvbox = 0, Catppuccin = 1, Nord = 2, TokyoNight = 3 };

inline const char* unit_mode_name(UnitMode m) {
  switch (m) {
    case UnitMode::Auto: return "Auto";
    case UnitMode::Bytes: return "Bytes";
    case UnitMode::KB: return "KB";
    case UnitMode::MB: return "MB";
    case UnitMode::GB: return "GB";
  }
  return "Auto";
}
inline const char* uptime_fmt_name(UptimeFmt m) {
  switch (m) {
    case UptimeFmt::Long: return "DD:HH:MM:SS";
    case UptimeFmt::Compact: return "1d 2h 3m";
    case UptimeFmt::Clock: return "Clock";
  }
  return "DD:HH:MM:SS";
}
inline const char* theme_name(ThemeId t) {
  switch (t) {
    case ThemeId::Gruvbox: return "Gruvbox Dark";
    case ThemeId::Catppuccin: return "Catppuccin Mocha";
    case ThemeId::Nord: return "Nord";
    case ThemeId::TokyoNight: return "Tokyo Night";
  }
  return "Gruvbox Dark";
}

struct Settings {
  UnitMode unit = UnitMode::Auto;
  int decimals = 1;              // 0..2
  UptimeFmt uptime = UptimeFmt::Long;
  int refresh_ms = 1000;         // 250..2000
  ThemeId theme = ThemeId::Gruvbox;
  std::string scan_path;         // empty => $HOME

  void clamp() {
    if (decimals < 0) decimals = 0;
    if (decimals > 2) decimals = 2;
    if (refresh_ms < 250) refresh_ms = 250;
    if (refresh_ms > 2000) refresh_ms = 2000;
    int t = static_cast<int>(theme);
    if (t < 0 || t > 3) theme = ThemeId::Gruvbox;
    int u = static_cast<int>(unit);
    if (u < 0 || u > 4) unit = UnitMode::Auto;
    int f = static_cast<int>(uptime);
    if (f < 0 || f > 2) uptime = UptimeFmt::Long;
  }
};

}  // namespace ct
