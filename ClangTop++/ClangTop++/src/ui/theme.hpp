// ClangTop++ - themes (truecolor palettes)
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "../config.hpp"

namespace ct {

struct RGB {
  uint8_t r = 255, g = 255, b = 255;
  bool operator==(const RGB& o) const { return r == o.r && g == o.g && b == o.b; }
  bool operator!=(const RGB& o) const { return !(*this == o); }
};

inline RGB rgb(uint8_t r, uint8_t g, uint8_t b) { return RGB{r, g, b}; }
inline RGB mix(const RGB& a, const RGB& b, double t) {
  if (t < 0) t = 0;
  if (t > 1) t = 1;
  return RGB{static_cast<uint8_t>(a.r + (b.r - a.r) * t),
             static_cast<uint8_t>(a.g + (b.g - a.g) * t),
             static_cast<uint8_t>(a.b + (b.b - a.b) * t)};
}

struct Theme {
  std::string name;
  RGB bg;        // window background
  RGB panel;     // panel fill
  RGB fg;        // primary text
  RGB dim;       // secondary text
  RGB muted;     // chart grid / labels
  RGB border;    // box borders
  RGB accent;    // headers / highlights
  RGB green;
  RGB yellow;
  RGB red;
  RGB blue;
  RGB magenta;
  RGB cyan;
  RGB orange;
  RGB purple;
  std::vector<RGB> series;
};

inline const std::vector<Theme>& theme_table() {
  static const std::vector<Theme> table = {
      // ---------------------------------------------------------- Gruvbox Dark
      Theme{"Gruvbox Dark",
            rgb(40, 40, 40),      // bg         #282828
            rgb(50, 48, 47),      // panel      #32302f
            rgb(235, 219, 178),   // fg         #ebdbb2
            rgb(168, 153, 132),   // dim        #a89984
            rgb(146, 131, 116),   // muted      #928374
            rgb(80, 73, 69),      // border     #504945
            rgb(250, 189, 47),    // accent     #fabd2f
            rgb(184, 187, 38),    // green      #b8bb26
            rgb(250, 189, 47),    // yellow     #fabd2f
            rgb(251, 73, 52),     // red        #fb4934
            rgb(131, 165, 152),   // blue       #83a598 (muted aqua)
            rgb(211, 134, 155),   // magenta    #d3869b (muted purple)
            rgb(131, 165, 152),   // cyan       #83a598 (muted aqua)
            rgb(254, 128, 25),    // orange     #fe8019
            rgb(211, 134, 155),   // purple     #d3869b
            {rgb(184, 187, 38), rgb(250, 189, 47), rgb(254, 128, 25),
             rgb(131, 165, 152), rgb(251, 73, 52), rgb(211, 134, 155)}},
      // ---------------------------------------------------- Catppuccin Mocha
      Theme{"Catppuccin Mocha",
            rgb(30, 30, 46),
            rgb(24, 24, 37),
            rgb(205, 214, 244),
            rgb(166, 173, 200),
            rgb(108, 112, 134),
            rgb(69, 71, 90),
            rgb(137, 180, 250),
            rgb(166, 227, 161),
            rgb(249, 226, 175),
            rgb(243, 139, 168),
            rgb(137, 180, 250),
            rgb(203, 166, 247),
            rgb(148, 226, 213),
            rgb(250, 179, 135),
            rgb(245, 194, 231),
            {rgb(137, 180, 250), rgb(250, 179, 135), rgb(203, 166, 247),
             rgb(166, 227, 161), rgb(249, 226, 175), rgb(243, 139, 168)}},
      // ------------------------------------------------------------------ Nord
      Theme{"Nord",
            rgb(46, 52, 64),
            rgb(39, 44, 54),
            rgb(236, 239, 244),
            rgb(216, 222, 233),
            rgb(129, 161, 193),
            rgb(76, 86, 106),
            rgb(136, 192, 208),
            rgb(163, 190, 140),
            rgb(235, 203, 139),
            rgb(191, 97, 106),
            rgb(129, 161, 193),
            rgb(180, 142, 173),
            rgb(143, 188, 187),
            rgb(208, 135, 112),
            rgb(180, 142, 173),
            {rgb(136, 192, 208), rgb(208, 135, 112), rgb(180, 142, 173),
             rgb(163, 190, 140), rgb(235, 203, 139), rgb(191, 97, 106)}},
      // ------------------------------------------------------------ Tokyo Night
      Theme{"Tokyo Night",
            rgb(26, 27, 38),
            rgb(22, 22, 30),
            rgb(192, 202, 245),
            rgb(169, 177, 214),
            rgb(86, 95, 137),
            rgb(59, 66, 97),
            rgb(122, 162, 247),
            rgb(158, 206, 106),
            rgb(224, 175, 104),
            rgb(247, 118, 142),
            rgb(122, 162, 247),
            rgb(187, 154, 247),
            rgb(125, 207, 255),
            rgb(255, 158, 100),
            rgb(187, 154, 247),
            {rgb(125, 207, 255), rgb(255, 158, 100), rgb(187, 154, 247),
             rgb(158, 206, 106), rgb(224, 175, 104), rgb(247, 118, 142)}},
  };
  return table;
}

inline const Theme& theme_by_id(ThemeId id) {
  int i = static_cast<int>(id);
  if (i < 0 || i >= static_cast<int>(theme_table().size())) i = 0;
  return theme_table()[static_cast<std::size_t>(i)];
}

// Heat ramp: green -> yellow -> orange -> red, used by per-core heatmaps.
inline RGB heat_color(const Theme& t, double frac) {
  if (frac <= 0) return mix(t.panel, t.green, 0.25);
  if (frac >= 1.0) return t.red;
  if (frac < 0.5) return mix(t.green, t.yellow, frac / 0.5);
  if (frac < 0.8) return mix(t.yellow, t.orange, (frac - 0.5) / 0.3);
  return mix(t.orange, t.red, (frac - 0.8) / 0.2);
}

}  // namespace ct
