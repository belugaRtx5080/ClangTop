// ClangTop++ - truecolor cell canvas + Unicode Braille/Block chart engine
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "theme.hpp"

namespace ct {

struct Rect {
  int x = 0, y = 0, w = 0, h = 0;
  int right() const { return x + w; }
  int bottom() const { return y + h; }
  bool empty() const { return w <= 0 || h <= 0; }
  Rect inset(int p) const {
    return Rect{x + p, y + p, std::max(0, w - 2 * p), std::max(0, h - 2 * p)};
  }
  bool contains(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

struct Cell {
  uint32_t ch = ' ';
  RGB fg{235, 219, 178};  // Gruvbox fg  #ebdbb2
  RGB bg{40, 40, 40};     // Gruvbox bg  #282828 (never pure black)
};

inline void utf8_append(std::string& out, uint32_t cp) {
  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

inline uint32_t utf8_next(const std::string& s, std::size_t& i) {
  if (i >= s.size()) return 0;
  unsigned char c = static_cast<unsigned char>(s[i]);
  if (c < 0x80) {
    ++i;
    return c;
  }
  int extra = 0;
  uint32_t cp = 0;
  if ((c & 0xE0) == 0xC0) {
    extra = 1;
    cp = c & 0x1F;
  } else if ((c & 0xF0) == 0xE0) {
    extra = 2;
    cp = c & 0x0F;
  } else if ((c & 0xF8) == 0xF0) {
    extra = 3;
    cp = c & 0x07;
  } else {
    ++i;
    return '?';
  }
  if (i + static_cast<std::size_t>(extra) >= s.size()) {
    i = s.size();
    return '?';
  }
  for (int k = 1; k <= extra; ++k) {
    unsigned char cc = static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)]);
    if ((cc & 0xC0) != 0x80) {
      ++i;
      return '?';
    }
    cp = (cp << 6) | (cc & 0x3F);
  }
  i += static_cast<std::size_t>(extra) + 1;
  return cp;
}

// Count printable codepoints (wide chars are rare here; CJK not used in chrome).
inline int text_width(const std::string& s) {
  std::size_t i = 0;
  int n = 0;
  while (i < s.size()) {
    uint32_t cp = utf8_next(s, i);
    if (cp == 0) break;
    if (cp >= 0x1100 && (cp <= 0x115F || (cp >= 0x2E80 && cp <= 0xA4CF) ||
                         (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0xF900 && cp <= 0xFAFF) ||
                         (cp >= 0xFE30 && cp <= 0xFE6F) || (cp >= 0xFF00 && cp <= 0xFF60) ||
                         (cp >= 0xFFE0 && cp <= 0xFFE6)))
      n += 2;
    else
      n += 1;
  }
  return n;
}

class Canvas {
 public:
  Canvas(int w, int h) : w_(std::max(0, w)), h_(std::max(0, h)), cells_(static_cast<std::size_t>(w_) * static_cast<std::size_t>(h_)) {}

  int width() const { return w_; }
  int height() const { return h_; }
  Rect bounds() const { return Rect{0, 0, w_, h_}; }

  void resize(int w, int h) {
    w_ = std::max(0, w);
    h_ = std::max(0, h);
    cells_.assign(static_cast<std::size_t>(w_) * static_cast<std::size_t>(h_), Cell{});
  }

  void clear(RGB fg, RGB bg) {
    for (auto& c : cells_) {
      c.ch = ' ';
      c.fg = fg;
      c.bg = bg;
    }
  }

  void set(int x, int y, uint32_t ch, RGB fg, RGB bg) {
    if (x < 0 || y < 0 || x >= w_ || y >= h_) return;
    Cell& c = cells_[static_cast<std::size_t>(y) * static_cast<std::size_t>(w_) + static_cast<std::size_t>(x)];
    c.ch = ch;
    c.fg = fg;
    c.bg = bg;
  }

  void glyph(int x, int y, uint32_t ch, RGB fg) {
    if (x < 0 || y < 0 || x >= w_ || y >= h_) return;
    Cell& c = cells_[static_cast<std::size_t>(y) * static_cast<std::size_t>(w_) + static_cast<std::size_t>(x)];
    c.ch = ch;
    c.fg = fg;
  }

  void text(int x, int y, const std::string& s, RGB fg, RGB bg) {
    std::size_t i = 0;
    int cx = x;
    while (i < s.size()) {
      uint32_t cp = utf8_next(s, i);
      if (cp == 0) break;
      if (cx >= w_) break;
      if (cx >= 0 && y >= 0 && y < h_) set(cx, y, cp, fg, bg);
      ++cx;
    }
  }

  void text(int x, int y, const std::string& s, RGB fg) {
    std::size_t i = 0;
    int cx = x;
    while (i < s.size()) {
      uint32_t cp = utf8_next(s, i);
      if (cp == 0) break;
      if (cx >= w_) break;
      if (cx >= 0 && y >= 0 && y < h_) glyph(cx, y, cp, fg);
      ++cx;
    }
  }

  void text_clipped(int x, int y, int maxw, const std::string& s, RGB fg, RGB bg) {
    if (maxw <= 0) return;
    std::string cut;
    std::size_t i = 0;
    int n = 0;
    while (i < s.size() && n < maxw) {
      std::size_t before = i;
      uint32_t cp = utf8_next(s, i);
      if (cp == 0) break;
      if (i < s.size() && n == maxw - 1) {
        utf8_append(cut, 0x2026);
        ++n;
        break;
      }
      utf8_append(cut, cp);
      ++n;
      if (i == before) break;
    }
    text(x, y, cut, fg, bg);
  }

  void fill(const Rect& r, uint32_t ch, RGB fg, RGB bg) {
    for (int y = r.y; y < r.y + r.h; ++y)
      for (int x = r.x; x < r.x + r.w; ++x) set(x, y, ch, fg, bg);
  }

  void hline(int x, int y, int w, uint32_t ch, RGB fg, RGB bg) {
    for (int i = 0; i < w; ++i) set(x + i, y, ch, fg, bg);
  }
  void vline(int x, int y, int h, uint32_t ch, RGB fg, RGB bg) {
    for (int i = 0; i < h; ++i) set(x, y + i, ch, fg, bg);
  }

  // ┌─ Title ───────── [right] ─┐
  void box(const Rect& r, const std::string& title, const std::string& right, RGB fg, RGB bg,
           RGB title_fg, RGB right_fg) {
    if (r.w < 2 || r.h < 2) return;
    int x0 = r.x, y0 = r.y, x1 = r.x + r.w - 1, y1 = r.y + r.h - 1;
    set(x0, y0, 0x250C, fg, bg);       // ┌
    set(x1, y0, 0x2510, fg, bg);       // ┐
    set(x0, y1, 0x2514, fg, bg);       // └
    set(x1, y1, 0x2518, fg, bg);       // ┘
    for (int x = x0 + 1; x < x1; ++x) {
      set(x, y0, 0x2500, fg, bg);      // ─
      set(x, y1, 0x2500, fg, bg);
    }
    for (int y = y0 + 1; y < y1; ++y) {
      set(x0, y, 0x2502, fg, bg);      // │
      set(x1, y, 0x2502, fg, bg);
    }
    if (!title.empty() && r.w > 6) {
      std::string t = " " + title + " ";
      text_clipped(x0 + 2, y0, r.w - 4, t, title_fg, bg);
    }
    if (!right.empty() && r.w > 10) {
      std::string rs = " " + right + " ";
      int rw = text_width(rs);
      int rx = x1 - 1 - rw;
      if (rx > x0 + 2 + text_width(title) + 1) text(rx, y0, rs, right_fg);
    }
    // rounded inner corners are intentionally crisp square
  }

  void box(const Rect& r, RGB fg, RGB bg) { box(r, "", "", fg, bg, fg, fg); }

  // ------------------------------------------------------------ charts
  // Multi-series Braille line (optionally filled area) chart.
  void braille_chart(const Rect& r, const std::vector<std::vector<double>>& series,
                     const std::vector<RGB>& colors, double lo, double hi, bool fill) {
    if (r.w <= 0 || r.h <= 0 || series.empty()) return;
    if (!(hi > lo)) hi = lo + 1.0;
    const int pw = r.w * 2;
    const int ph = r.h * 4;
    static const uint8_t bits[2][4] = {{0x01, 0x02, 0x04, 0x40}, {0x08, 0x10, 0x20, 0x80}};
    std::vector<uint8_t> dots(static_cast<std::size_t>(pw) * static_cast<std::size_t>(ph), 0);

    auto plot = [&](int px, int py) {
      if (px < 0 || py < 0 || px >= pw || py >= ph) return;
      dots[static_cast<std::size_t>(py) * static_cast<std::size_t>(pw) + static_cast<std::size_t>(px)] |=
          bits[px % 2][py % 4];
    };

    auto sample_at = [](const std::vector<double>& s, double pos) {
      std::size_t n = s.size();
      if (n == 0) return 0.0;
      if (n == 1) return s[0];
      if (pos <= 0) return s[0];
      if (pos >= static_cast<double>(n - 1)) return s[n - 1];
      std::size_t i0 = static_cast<std::size_t>(pos);
      double f = pos - static_cast<double>(i0);
      if (i0 + 1 >= n) return s[n - 1];
      return s[i0] * (1.0 - f) + s[i0 + 1] * f;
    };

    for (std::size_t si = 0; si < series.size(); ++si) {
      const auto& s = series[si];
      if (s.empty()) continue;
      std::fill(dots.begin(), dots.end(), 0);
      for (int px = 0; px < pw; ++px) {
        double pos = pw > 1 ? static_cast<double>(px) * (static_cast<double>(s.size()) - 1.0) / (pw - 1) : 0.0;
        double v = sample_at(s, pos);
        double norm = (v - lo) / (hi - lo);
        if (norm < 0) norm = 0;
        if (norm > 1) norm = 1;
        int py = static_cast<int>(std::lround((1.0 - norm) * (ph - 1)));
        if (fill) {
          for (int y = py; y < ph; ++y) plot(px, y);
        } else {
          plot(px, py);
          if (px + 1 < pw) {
            double pos2 = static_cast<double>(px + 1) * (static_cast<double>(s.size()) - 1.0) / (pw - 1);
            double v2 = sample_at(s, pos2);
            double n2 = (v2 - lo) / (hi - lo);
            if (n2 < 0) n2 = 0;
            if (n2 > 1) n2 = 1;
            int py2 = static_cast<int>(std::lround((1.0 - n2) * (ph - 1)));
            int a = std::min(py, py2), b = std::max(py, py2);
            for (int y = a; y <= b; ++y) plot(px, y);
          }
        }
      }
      RGB col = si < colors.size() ? colors[si] : RGB{200, 200, 200};
      for (int cy = 0; cy < r.h; ++cy) {
        for (int cx = 0; cx < r.w; ++cx) {
          uint8_t mask = 0;
          for (int sy = 0; sy < 4; ++sy)
            for (int sx = 0; sx < 2; ++sx) {
              if (dots[static_cast<std::size_t>(cy * 4 + sy) * static_cast<std::size_t>(pw) +
                       static_cast<std::size_t>(cx * 2 + sx)])
                mask |= static_cast<uint8_t>(bits[sx][sy]);
            }
          if (mask) glyph(r.x + cx, r.y + cy, 0x2800u + mask, col);
        }
      }
    }
  }

  // Stacked Braille area chart, first series at the bottom.
  void stacked_area(const Rect& r, const std::vector<std::vector<double>>& series,
                    const std::vector<RGB>& colors, double lo, double hi) {
    if (r.w <= 0 || r.h <= 0 || series.empty()) return;
    if (!(hi > lo)) hi = lo + 1.0;
    const int pw = r.w * 2;
    const int ph = r.h * 4;
    static const uint8_t bits[2][4] = {{0x01, 0x02, 0x04, 0x40}, {0x08, 0x10, 0x20, 0x80}};

    std::vector<std::vector<uint8_t>> layer(series.size(),
                                            std::vector<uint8_t>(static_cast<std::size_t>(pw) * static_cast<std::size_t>(ph), 0));
    auto sample_at = [](const std::vector<double>& s, double pos) {
      std::size_t n = s.size();
      if (n == 0) return 0.0;
      if (n == 1) return s[0];
      if (pos <= 0) return s[0];
      if (pos >= static_cast<double>(n - 1)) return s[n - 1];
      std::size_t i0 = static_cast<std::size_t>(pos);
      double f = pos - static_cast<double>(i0);
      if (i0 + 1 >= n) return s[n - 1];
      return s[i0] * (1.0 - f) + s[i0 + 1] * f;
    };
    auto val_to_py = [&](double v) {
      double norm = (v - lo) / (hi - lo);
      if (norm < 0) norm = 0;
      if (norm > 1) norm = 1;
      return static_cast<int>(std::lround((1.0 - norm) * (ph - 1)));
    };
    std::vector<double> cum(series.size(), 0.0);
    for (int px = 0; px < pw; ++px) {
      double pos = pw > 1 ? static_cast<double>(px) * (static_cast<double>(series[0].size()) - 1.0) / (pw - 1) : 0.0;
      double running = 0;
      for (std::size_t k = 0; k < series.size(); ++k) {
        double v = sample_at(series[k], pos);
        if (v < 0) v = 0;
        double prev = running;
        running += v;
        int top = val_to_py(running);
        int bot = val_to_py(prev);
        if (bot <= top) bot = top + 1;
        for (int y = top; y <= bot && y < ph; ++y) {
          if (y < 0) continue;
          layer[k][static_cast<std::size_t>(y) * static_cast<std::size_t>(pw) + static_cast<std::size_t>(px)] |=
              bits[px % 2][y % 4];
        }
      }
    }
    for (std::size_t k = 0; k < series.size(); ++k) {
      RGB col = k < colors.size() ? colors[k] : RGB{200, 200, 200};
      for (int cy = 0; cy < r.h; ++cy) {
        for (int cx = 0; cx < r.w; ++cx) {
          uint8_t mask = 0;
          for (int sy = 0; sy < 4; ++sy)
            for (int sx = 0; sx < 2; ++sx)
              if (layer[k][static_cast<std::size_t>(cy * 4 + sy) * static_cast<std::size_t>(pw) +
                            static_cast<std::size_t>(cx * 2 + sx)])
                mask |= static_cast<uint8_t>(bits[sx][sy]);
          if (mask) glyph(r.x + cx, r.y + cy, 0x2800u + mask, col);
        }
      }
    }
  }

  void braille_heat(const Rect& r, const std::vector<double>& vals, int cols, const Theme& t) {
    if (r.w <= 0 || r.h <= 0 || cols <= 0 || vals.empty()) return;
    static const uint32_t dot[4] = {0x2801, 0x2803, 0x2807, 0x2847};  // ⠁ ⠃ ⠇ ⡇
    for (int cy = 0; cy < r.h; ++cy)
      for (int cx = 0; cx < r.w; ++cx) {
        int idx = cy * r.w + cx;
        if (idx >= static_cast<int>(vals.size())) return;
        double v = vals[static_cast<std::size_t>(idx)];
        if (v < 0) v = 0;
        if (v > 1) v = 1;
        uint32_t ch = dot[std::min(3, static_cast<int>(v * 4.0))];
        glyph(r.x + cx, r.y + cy, ch, heat_color(t, v));
      }
    (void)cols;
  }

  // Block sparkline, values mapped [lo,hi] onto ▁▂▃▄▅▆▇█
  void sparkline(int x, int y, int w, const std::vector<double>& vals, double lo, double hi, RGB fg) {
    if (w <= 0) return;
    static const uint32_t blocks[8] = {0x2581, 0x2582, 0x2583, 0x2584,
                                       0x2585, 0x2586, 0x2587, 0x2588};
    if (vals.empty()) return;
    int n = static_cast<int>(vals.size());
    for (int i = 0; i < w; ++i) {
      double v;
      if (n == 1) v = vals[0];
      else {
        double pos = static_cast<double>(i) * (n - 1) / std::max(1, w - 1);
        int i0 = static_cast<int>(pos);
        double f = pos - i0;
        int i1 = std::min(n - 1, i0 + 1);
        v = vals[static_cast<std::size_t>(i0)] * (1 - f) + vals[static_cast<std::size_t>(i1)] * f;
      }
      double norm = (v - lo) / (hi > lo ? (hi - lo) : 1.0);
      if (norm < 0) norm = 0;
      if (norm > 0.999) norm = 0.999;
      int idx = static_cast<int>(norm * 8.0);
      glyph(x + i, y, blocks[std::max(0, std::min(7, idx))], fg);
    }
  }

  void hbar(int x, int y, int w, double frac, RGB fg, RGB track, RGB bg) {
    if (w <= 0) return;
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    int fillw = static_cast<int>(std::lround(frac * w));
    for (int i = 0; i < w; ++i) set(x + i, y, i < fillw ? 0x2588 : 0x2591, i < fillw ? fg : track, bg);
  }

  // Bottom-anchored block bar with 1/8-cell precision.
  void vbar(int x, int y_top, int h, double frac, RGB fg, RGB bg, bool track) {
    if (h <= 0) return;
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    static const uint32_t partial[9] = {0x20, 0x2581, 0x2582, 0x2583, 0x2584,
                                        0x2585, 0x2586, 0x2587, 0x2588};
    int eighths = static_cast<int>(std::lround(frac * h * 8.0));
    for (int row = 0; row < h; ++row) {
      int from_bottom = h - 1 - row;
      int e = eighths - from_bottom * 8;
      int y = y_top + row;
      if (e >= 8) set(x, y, 0x2588, fg, bg);
      else if (e > 0) set(x, y, partial[std::min(8, e)], fg, bg);
      else if (track) set(x, y, 0x2591, RGB{90, 90, 90}, bg);
    }
  }

  void heat(int x, int y, double frac, const Theme& t) {
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    glyph(x, y, 0x2588, heat_color(t, frac));
  }

  std::string render() const {
    std::string out;
    out.reserve(static_cast<std::size_t>(w_) * static_cast<std::size_t>(h_) * 8);
    RGB cur_fg{1, 2, 3}, cur_bg{4, 5, 6};
    bool first = true;
    for (int y = 0; y < h_; ++y) {
      for (int x = 0; x < w_; ++x) {
        const Cell& c = cells_[static_cast<std::size_t>(y) * static_cast<std::size_t>(w_) + static_cast<std::size_t>(x)];
        if (first || c.fg != cur_fg || c.bg != cur_bg) {
          char buf[48];
          std::snprintf(buf, sizeof(buf), "\x1b[38;2;%u;%u;%um\x1b[48;2;%u;%u;%um",
                        static_cast<unsigned>(c.fg.r), static_cast<unsigned>(c.fg.g),
                        static_cast<unsigned>(c.fg.b), static_cast<unsigned>(c.bg.r),
                        static_cast<unsigned>(c.bg.g), static_cast<unsigned>(c.bg.b));
          out += buf;
          cur_fg = c.fg;
          cur_bg = c.bg;
          first = false;
        }
        utf8_append(out, c.ch);
      }
      out += "\x1b[0m";
      if (y + 1 < h_) out += "\r\n";
      cur_fg = RGB{1, 2, 3};
      cur_bg = RGB{4, 5, 6};
      first = true;
    }
    return out;
  }

  RGB fg_at(int x, int y) const {
    if (x < 0 || y < 0 || x >= w_ || y >= h_) return RGB{};
    return cells_[static_cast<std::size_t>(y) * static_cast<std::size_t>(w_) + static_cast<std::size_t>(x)].fg;
  }

 private:
  int w_ = 0;
  int h_ = 0;
  std::vector<Cell> cells_;
};

}  // namespace ct
