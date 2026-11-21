// ClangTop++ - resource views: CPU, Memory, Net & Disk I/O, FS & Code
#pragma once
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../core/metrics_types.hpp"
#include "../core/unit_formatter.hpp"
#include "canvas.hpp"
#include "theme.hpp"
#include "widgets.hpp"

namespace ct {
namespace detail {

// ---------------------------------------------------------------- helpers
inline RGB rd_sc(const Theme& th, std::size_t i) {
  if (th.series.empty()) return th.accent;
  return th.series[i % th.series.size()];
}

inline Rect rd_cell(const Rect& a, int cols, int rows, int idx) {
  if (cols <= 0 || rows <= 0 || a.w <= 0 || a.h <= 0) return Rect{};
  const int c = idx % cols, r = idx / cols;
  const int x0 = a.x + a.w * c / cols;
  const int x1 = a.x + a.w * (c + 1) / cols;
  const int y0 = a.y + a.h * r / rows;
  const int y1 = a.y + a.h * (r + 1) / rows;
  return Rect{x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
}

inline double rd_peak(const std::vector<std::vector<double>>& ss) {
  double m = 0.0;
  for (const auto& v : ss)
    for (double x : v) m = std::max(m, x);
  return m;
}

inline double rd_avg(const std::vector<double>& v) {
  if (v.empty()) return 0.0;
  double sum = 0.0;
  for (double x : v) sum += x;
  return sum / static_cast<double>(v.size());
}

inline int rd_cols_width(const std::vector<Col>& cols) {
  int w = 0;
  for (const auto& c : cols) w += c.w + 1;
  return w;
}

inline void rd_fit_cols(std::vector<Col>& cols, int w) {
  while (!cols.empty() && rd_cols_width(cols) > w) cols.pop_back();
}

// UTF-8 block sparkline string (8 levels), exactly `w` cells wide.
inline std::string rd_spark_str(const std::vector<double>& vals, int w) {
  if (w <= 0) return std::string();
  static const uint32_t blocks[8] = {0x2581, 0x2582, 0x2583, 0x2584,
                                     0x2585, 0x2586, 0x2587, 0x2588};
  std::string out;
  out.reserve(static_cast<std::size_t>(w) * 3);
  if (vals.empty()) {
    out.assign(static_cast<std::size_t>(w), ' ');
    return out;
  }
  double hi = 0.0;
  for (double v : vals)
    if (std::isfinite(v)) hi = std::max(hi, v);
  if (hi <= 0) hi = 1.0;
  const int n = static_cast<int>(vals.size());
  for (int i = 0; i < w; ++i) {
    double v;
    if (n == 1) {
      v = vals[0];
    } else {
      const double pos = static_cast<double>(i) * (n - 1) / std::max(1, w - 1);
      const int i0 = static_cast<int>(pos);
      const int i1 = std::min(n - 1, i0 + 1);
      const double f = pos - i0;
      v = vals[static_cast<std::size_t>(i0)] * (1 - f) + vals[static_cast<std::size_t>(i1)] * f;
    }
    double norm = v / hi;
    if (norm < 0) norm = 0;
    if (norm > 1) norm = 1;
    const int idx = static_cast<int>(norm * 7.999);
    utf8_append(out, blocks[std::max(0, std::min(7, idx))]);
  }
  return out;
}

// UTF-8 usage bar string, exactly `w` cells wide.
inline std::string rd_bar_str(double frac, int w) {
  if (w <= 0) return std::string();
  if (frac < 0) frac = 0;
  if (frac > 1) frac = 1;
  const int filled = static_cast<int>(std::lround(frac * w));
  std::string out;
  out.reserve(static_cast<std::size_t>(w) * 3);
  for (int i = 0; i < w; ++i) utf8_append(out, i < filled ? 0x2588 : 0x2591);
  return out;
}

inline std::vector<const ProcInfo*> rd_top_mem(const Snapshot& s, std::size_t n) {
  std::vector<const ProcInfo*> out;
  out.reserve(s.procs.size());
  for (const auto& p : s.procs) out.push_back(&p);
  std::sort(out.begin(), out.end(),
            [](const ProcInfo* a, const ProcInfo* b) { return a->rss > b->rss; });
  if (out.size() > n) out.resize(n);
  return out;
}

inline std::vector<const ProcInfo*> rd_top_disk(const Snapshot& s, std::size_t n) {
  std::vector<const ProcInfo*> out;
  out.reserve(s.procs.size());
  for (const auto& p : s.procs) out.push_back(&p);
  std::sort(out.begin(), out.end(), [](const ProcInfo* a, const ProcInfo* b) {
    return (a->rd_bps + a->wr_bps) > (b->rd_bps + b->wr_bps);
  });
  if (out.size() > n) out.resize(n);
  return out;
}

inline void rd_legend(Canvas& cv, const Rect& r,
                      const std::vector<std::pair<std::string, RGB>>& items, const Theme& th) {
  if (r.h <= 0 || r.w <= 0) return;
  int x = r.x, y = r.y;
  for (const auto& it : items) {
    const int need = text_width(it.first) + 3;
    if (x + need > r.x + r.w && x > r.x) {
      x = r.x;
      ++y;
    }
    if (y >= r.y + r.h) return;
    const int avail = r.x + r.w - x;
    if (avail < 3) return;
    cv.glyph(x, y, 0x25A0, it.second);
    cv.text_clipped(x + 2, y, avail - 2, it.first, th.dim, th.panel);
    x += need + 1;
  }
}

inline void rd_core_cell(Canvas& cv, int x, int y, int w, double u, const std::string& label,
                         const Theme& th) {  if (w < 2) return;
  const RGB col = heat_color(th, u);
  const int lab = std::min<int>(3, w - 1);
  cv.text_clipped(x, y, lab, label, th.dim, th.panel);
  int px = x + lab;
  int rest = w - lab;
  if (rest <= 0) return;
  cv.glyph(px, y, 0x2588, col);
  ++px;
  --rest;
  if (rest > 0)
    cv.text_clipped(px, y, rest, UnitFormatter::fixed(u * 100.0, 0) + "%", col, th.panel);
}

inline RGB rd_pct_color(const Theme& th, double pct) {
  if (pct > 90) return th.red;
  if (pct > 75) return th.yellow;
  return th.fg;
}

inline RGB rd_load_color(const Theme& th, double pct) {
  double f = pct / 100.0;
  if (f < 0) f = 0;
  if (f > 1) f = 1;
  RGB dark_green = mix(th.bg, th.green, 0.25);
  if (f <= 0.01) return dark_green;
  if (f < 0.35) return mix(dark_green, th.green, f / 0.35);
  if (f < 0.60) return mix(th.green, th.yellow, (f - 0.35) / 0.25);
  if (f < 0.85) return mix(th.yellow, th.orange, (f - 0.60) / 0.25);
  return mix(th.orange, th.red, (f - 0.85) / 0.15);
}

inline double rd_nice_cpu_ceil(double peak_pct) {
  if (peak_pct <= 0.0) return 10.0;
  double v = peak_pct * 1.15;
  if (v <= 10.0) return 10.0;
  if (v <= 20.0) return 20.0;
  if (v <= 40.0) return 40.0;
  if (v <= 50.0) return 50.0;
  if (v <= 60.0) return 60.0;
  if (v <= 80.0) return 80.0;
  return 100.0;
}

inline std::string rd_time_ago(std::size_t samples, int refresh_ms) {
  double secs = static_cast<double>(samples) * static_cast<double>(std::max(1, refresh_ms)) / 1000.0;
  int s = static_cast<int>(secs + 0.5);
  int m = s / 60;
  int sec = s % 60;
  char buf[32];
  std::snprintf(buf, sizeof(buf), "-%dm%02ds", m, sec);
  return std::string(buf);
}

inline std::string rd_clock_hms(double seconds) {
  if (!std::isfinite(seconds) || seconds < 0) seconds = 0;
  long long t = static_cast<long long>(seconds);
  long long h = t / 3600;
  long long m = (t % 3600) / 60;
  long long s = t % 60;
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%02lld:%02lld:%02lld", h, m, s);
  return std::string(buf);
}

inline std::string rd_compact_dur(double seconds) {
  if (!std::isfinite(seconds) || seconds < 0) seconds = 0;
  long long t = static_cast<long long>(seconds);
  long long d = t / 86400;
  long long h = (t % 86400) / 3600;
  long long m = (t % 3600) / 60;
  long long s = t % 60;
  char buf[32];
  if (d > 0) std::snprintf(buf, sizeof(buf), "%lldd%02lldh", d, h);
  else if (h > 0) std::snprintf(buf, sizeof(buf), "%lldh%02lldm", h, m);
  else if (m > 0) std::snprintf(buf, sizeof(buf), "%lldm%02llds", m, s);
  else std::snprintf(buf, sizeof(buf), "%llds", s);
  return std::string(buf);
}

inline std::string rd_proc_group_name(std::string name) {
  // normalize kernel workers and common thread pools
  if (name.rfind("kworker", 0) == 0) return "kworker/*";
  std::size_t slash = name.find('/');
  if (slash != std::string::npos && slash > 0) name = name.substr(0, slash);
  // strip trailing :N or -N thread suffixes
  while (!name.empty()) {
    char c = name.back();
    if (std::isdigit(static_cast<unsigned char>(c)) || c == ':' || c == '-' || c == '_') {
      // only strip numeric suffix patterns conservatively
      if (c == ':' || c == '-') { name.pop_back(); break; }
      // keep base names like python3; strip if preceded by separator+digits
      std::size_t i = name.size();
      while (i > 0 && std::isdigit(static_cast<unsigned char>(name[i - 1]))) --i;
      if (i < name.size() && i > 0 && (name[i - 1] == ':' || name[i - 1] == '-' || name[i - 1] == '/')) {
        name.resize(i - 1);
        continue;
      }
      break;
    }
    break;
  }
  // trailing separators
  while (!name.empty() && (name.back() == '-' || name.back() == ':' || name.back() == '/')) name.pop_back();
  if (name.empty()) name = "?";
  return name;
}

inline std::string rd_cpu_cluster_tag(const std::vector<CoreInfo>& cores, int id) {
  int cluster = -1;
  for (const auto& c : cores) if (c.id == id) { cluster = c.cluster; break; }
  if (cluster < 0) return "C" + std::to_string(id);
  // heuristic: low cluster index P/perf, high E/eff (best-effort)
  int maxc = -1;
  for (const auto& c : cores) if (c.cluster > maxc) maxc = c.cluster;
  if (maxc <= 0) return "C" + std::to_string(id);
  return (cluster == 0 ? "P" : "E") + std::to_string(id);
}

inline void rd_xaxis_labels(Canvas& cv, const Rect& r, int refresh_ms, const Theme& th) {
  if (r.w < 8 || r.h < 1) return;
  std::string left = rd_time_ago(120000 / std::max(250, refresh_ms), refresh_ms);
  if (left.empty()) left = "-1m59s";
  std::string mid = "-59s";
  std::string right = "now";
  cv.text_clipped(r.x, r.y, std::min(8, r.w / 3), left, th.dim, th.panel);
  int mx = r.x + r.w / 2 - 2;
  if (mx > r.x + 7 && mx + 4 < r.x + r.w - 4) cv.text(mx, r.y, mid, th.dim, th.panel);
  int rx = r.x + r.w - 3;
  if (rx > mx + 4) cv.text(rx, r.y, right, th.dim, th.panel);
}

}  // namespace detail
// page 4 - 5-panel CPU dashboard
inline void view_cpu(Canvas& cv, const Rect& area, const Snapshot& s, const AppState& st,
                     const Theme& th, const UnitFormatter& fmt) {
  using namespace detail;
  if (area.w < 40 || area.h < 12) {
    // compact fallback
    if (area.w < 20 || area.h < 4) return;
    panel(cv, area, "CPU", fmt.pct(s.cpu.total), th, [&](Canvas& c, Rect in) {
      auto tot = st.h_cpu_app.tail(st.window_samples());
      if (in.h > 0) c.braille_chart(in, {tot}, {th.green}, 0.0, 100.0, false);
    });
    return;
  }

  const std::size_t win = st.window_samples();
  const int lw = std::max(20, area.w * 68 / 100);
  const int rw = area.w - lw;
  const int h1 = std::max(6, area.h * 30 / 100);
  const int h2 = std::max(6, area.h * 34 / 100);
  const int h3 = area.h - h1 - h2;
  Rect r_tl{area.x, area.y, lw, h1};
  Rect r_tr{area.x + lw, area.y, rw, h1};
  Rect r_ml{area.x, area.y + h1, lw, h2};
  Rect r_mr{area.x + lw, area.y + h1, rw, h2};
  Rect r_bl{area.x, area.y + h1 + h2, lw, h3};
  Rect r_br{area.x + lw, area.y + h1 + h2, rw, h3};

  // histories
  auto h_app = st.h_cpu_app.tail(win);
  auto h_kern = st.h_cpu_kern.tail(win);
  auto h_io = st.h_cpu_io.tail(win);
  auto h_irq = st.h_cpu_irq.tail(win);
  std::vector<double> h_sys(h_app.size(), 0.0);
  std::vector<double> h_tot(h_app.size(), 0.0);
  for (std::size_t i = 0; i < h_app.size(); ++i) {
    double a = i < h_app.size() ? h_app[i] : 0.0;
    double k = i < h_kern.size() ? h_kern[i] : 0.0;
    double io = i < h_io.size() ? h_io[i] : 0.0;
    double ir = i < h_irq.size() ? h_irq[i] : 0.0;
    h_sys[i] = k + io + ir;
    h_tot[i] = a + k + io + ir;
  }
  auto avg_of = [](const std::vector<double>& v) {
    if (v.empty()) return 0.0;
    double s = 0;
    for (double x : v) s += x;
    return s / static_cast<double>(v.size());
  };
  auto peak_of = [](const std::vector<double>& v) {
    double m = 0.0;
    for (double x : v) if (x > m) m = x;
    return m;
  };

  // ------------------------------------------------ Top-left: CPU usage
  {
    double app_now = h_app.empty() ? s.cpu.app : h_app.back();
    double kern_now = h_kern.empty() ? s.cpu.kern : h_kern.back();
    double sys_now = kern_now + (h_io.empty() ? s.cpu.io : h_io.back()) + (h_irq.empty() ? s.cpu.irq : h_irq.back());
    std::string right = "app " + fmt.pct(app_now) + " | kern " + fmt.pct(kern_now) + " | sys " + fmt.pct(sys_now);
    panel(cv, r_tl, "CPU usage", right, th, [&](Canvas& c, Rect in) {
      if (in.h < 3) return;
      int bar_h = in.h >= 5 ? 1 : 0;
      int axis_h = 1;
      int chart_h = in.h - bar_h - axis_h;
      if (chart_h < 2) { chart_h = std::max(1, in.h - 1); bar_h = 0; axis_h = in.h - chart_h; }
      Rect chart{in.x, in.y, in.w, chart_h};
      double peak = peak_of(h_tot);
      if (peak < peak_of(h_app)) peak = peak_of(h_app);
      double ceilv = rd_nice_cpu_ceil(peak);
      c.braille_chart(chart, {h_tot, h_app}, {th.green, mix(th.green, th.fg, 0.2)}, 0.0, ceilv, false);
      chart_labels(c, chart, fmt.pct(ceilv), "0%", "", th);
      // X axis
      if (axis_h > 0) rd_xaxis_labels(c, Rect{in.x, chart.y + chart.h, in.w, 1}, st.cfg.refresh_ms, th);
      // stacked bottom bars: app/kern/io/irq
      if (bar_h > 0) {
        int by = in.y + in.h - 1;
        int w = in.w;
        if (w > 0) {
          double total = app_now + kern_now + (h_io.empty() ? s.cpu.io : h_io.back()) + (h_irq.empty() ? s.cpu.irq : h_irq.back());
          if (total < 0.001) total = 0.001;
          // show as share of ceil (usage intensity) across width
          double scale = total / ceilv;
          if (scale > 1.0) scale = 1.0;
          int span = std::max(1, static_cast<int>(std::lround(scale * w)));
          auto segw = [&](double v) { return std::max(0, static_cast<int>(std::lround((v / total) * span))); };
          int wa = segw(app_now);
          int wk = segw(kern_now);
          int wi = segw(h_io.empty() ? s.cpu.io : h_io.back());
          int wq = std::max(0, span - wa - wk - wi);
          int x = in.x;
          for (int i = 0; i < wa && x < in.x + w; ++i, ++x) c.glyph(x, by, 0x2581, th.green);
          for (int i = 0; i < wk && x < in.x + w; ++i, ++x) c.glyph(x, by, 0x2581, th.yellow);
          for (int i = 0; i < wi && x < in.x + w; ++i, ++x) c.glyph(x, by, 0x2581, th.orange);
          for (int i = 0; i < wq && x < in.x + w; ++i, ++x) c.glyph(x, by, 0x2581, th.red);
          // legend at right of bar if space
          std::string leg = " App  Kern  IO  IRQ";
          int lx = in.x + in.w - text_width(leg);
          if (lx > x + 1) {
            c.glyph(lx, by, 0x2588, th.green); c.text(lx + 1, by, "App", th.dim);
            c.glyph(lx + 5, by, 0x2588, th.yellow); c.text(lx + 6, by, "Kern", th.dim);
            c.glyph(lx + 11, by, 0x2588, th.orange); c.text(lx + 12, by, "IO", th.dim);
            c.glyph(lx + 15, by, 0x2588, th.red); c.text(lx + 16, by, "IRQ", th.dim);
          }
        }
      }
    });
  }

  // ------------------------------------------------ Top-right: Now
  {
    panel(cv, r_tr, "Now", "% of all cores", th, [&](Canvas& c, Rect in) {
      int y = in.y;
      const int ncpu = std::max(1, s.ncpu);
      double app_now = s.cpu.app;
      double kern_now = s.cpu.kern;
      double sys_now = s.cpu.kern + s.cpu.io + s.cpu.irq;
      double total_now = s.cpu.total;
      double peak = peak_of(h_tot);
      double avg = avg_of(h_tot);
      // APP line
      if (y < in.y + in.h) {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "APP %s = %s of one core", fmt.pct(app_now).c_str(), fmt.pct(app_now).c_str());
        c.text_clipped(in.x, y, in.w, buf, th.fg, th.panel);
        ++y;
      }
      if (y < in.y + in.h) {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "  kernel %s | peak %s | avg %s", fmt.pct(kern_now).c_str(), fmt.pct(peak).c_str(), fmt.pct(avg).c_str());
        c.text_clipped(in.x, y, in.w, buf, th.dim, th.panel);
        ++y;
      }
      if (y < in.y + in.h) {
        int busiest = -1; double bval = -1;
        for (const auto& core : s.cores) if (core.util > bval) { bval = core.util; busiest = core.id; }
        char buf[160];
        if (busiest >= 0)
          std::snprintf(buf, sizeof(buf), "SYSTEM %s | busiest cpu%d %s", fmt.pct(sys_now).c_str(), busiest, fmt.pct(bval).c_str());
        else
          std::snprintf(buf, sizeof(buf), "SYSTEM %s", fmt.pct(sys_now).c_str());
        c.text_clipped(in.x, y, in.w, buf, th.fg, th.panel);
        ++y;
      }
      if (y < in.y + in.h) {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "THREADS %d running %d blocked %d", s.kern.threads,
                      static_cast<int>(s.kern.procs_running), static_cast<int>(s.kern.procs_blocked));
        c.text_clipped(in.x, y, in.w, buf, th.fg, th.panel);
        ++y;
      }
      if (y < in.y + in.h) {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "ctx switches/s %s   forks/s %s", fmt.fixed(s.kern.ctx_rate, 0).c_str(), fmt.fixed(s.kern.forks_rate, 1).c_str());
        c.text_clipped(in.x, y, in.w, buf, th.dim, th.panel);
        ++y;
      }
      if (y < in.y + in.h) {
        char buf[160];
        double minor = s.kern.pgfault_rate - s.kern.pgmajfault_rate;
        if (minor < 0) minor = 0;
        std::snprintf(buf, sizeof(buf), "page faults/s %s  minor %s major %s", fmt.fixed(s.kern.pgfault_rate, 0).c_str(), fmt.fixed(minor, 0).c_str(), fmt.fixed(s.kern.pgmajfault_rate, 1).c_str());
        c.text_clipped(in.x, y, in.w, buf, th.dim, th.panel);
        ++y;
      }
      if (y < in.y + in.h) {
        std::string up = rd_clock_hms(s.sys.uptime);
        std::string pol = s.kern.sched_policy.empty() ? "default" : s.kern.sched_policy;
        c.text_clipped(in.x, y, in.w, "uptime " + up + "   " + pol, th.dim, th.panel);
        ++y;
      }
      if (y < in.y + in.h) {
        double tmax = 0, cur_mhz = 0, max_mhz = 0;
        for (const auto& core : s.cores) {
          if (core.temp_c > tmax) tmax = core.temp_c;
          if (core.mhz > cur_mhz) cur_mhz = core.mhz;
          if (core.max_mhz > max_mhz) max_mhz = core.max_mhz;
        }
        const char* tlabel = "THERMAL OK";
        RGB tcol = th.green;
        if (tmax >= 85) { tlabel = "THERMAL HOT"; tcol = th.red; }
        else if (tmax >= 70) { tlabel = "THERMAL WARM"; tcol = th.yellow; }
        badge(c, in.x, y, tlabel, th.bg, tcol);
        char freq[64];
        std::snprintf(freq, sizeof(freq), " %s / %s GHz", fmt.fixed(cur_mhz / 1000.0, 2).c_str(), fmt.fixed((max_mhz > 0 ? max_mhz : cur_mhz) / 1000.0, 2).c_str());
        c.text_clipped(in.x + text_width(tlabel) + 3, y, std::max(0, in.w - text_width(tlabel) - 3), freq, th.dim, th.panel);
      }
      (void)ncpu; (void)total_now;
    });
  }

  // ------------------------------------------------ Middle-left: Per-core load matrix
  {
    std::string right = "last " + rd_time_ago(win, st.cfg.refresh_ms).substr(1) + " | colour = load";
    panel(cv, r_ml, "Per-core load", right, th, [&](Canvas& c, Rect in) {
      if (in.h < 1 || s.cores.empty()) return;
      int readout_w = 12; // " XX% 1.44G"
      int matrix_w = in.w - readout_w;
      if (matrix_w < 8) { matrix_w = in.w; readout_w = 0; }
      int label_w = (s.cores.size() > 9) ? 4 : 3;
      int hist_w = matrix_w - label_w - 1;
      if (hist_w < 4) hist_w = std::max(1, matrix_w - 2);
      int rows = std::min(in.h, static_cast<int>(s.cores.size()));
      // if too many cores, compress by sampling
      for (int row = 0; row < rows; ++row) {
        int ci = row;
        if (static_cast<int>(s.cores.size()) > rows && rows > 0)
          ci = static_cast<int>((static_cast<long long>(row) * s.cores.size()) / rows);
        if (ci >= static_cast<int>(s.cores.size())) ci = static_cast<int>(s.cores.size()) - 1;
        const CoreInfo& core = s.cores[static_cast<std::size_t>(ci)];
        int y = in.y + row;
        std::string lab = "C" + std::to_string(core.id);
        c.text_clipped(in.x, y, label_w, pad_left(lab, label_w), th.dim, th.panel);
        // history blocks
        std::vector<double> hv;
        if (ci < static_cast<int>(st.h_cores.size())) hv = st.h_cores[static_cast<std::size_t>(ci)].tail(win);
        if (hv.empty()) hv.push_back(core.util);
        for (int x = 0; x < hist_w; ++x) {
          double pos = (hist_w > 1) ? (static_cast<double>(x) * (hv.size() - 1) / (hist_w - 1)) : 0.0;
          std::size_t i0 = static_cast<std::size_t>(pos);
          std::size_t i1 = std::min(hv.size() - 1, i0 + 1);
          double f = pos - static_cast<double>(i0);
          double v = hv[i0] * (1.0 - f) + hv[i1] * f;
          c.glyph(in.x + label_w + 1 + x, y, 0x2588, rd_load_color(th, v));
        }
        // live readout
        if (readout_w >= 8) {
          char buf[32];
          std::snprintf(buf, sizeof(buf), "%3.0f%% %4.2fG", core.util, core.mhz / 1000.0);
          c.text_clipped(in.x + matrix_w, y, readout_w, buf, heat_color(th, core.util / 100.0), th.panel);
        }
      }
    });
  }

  // ------------------------------------------------ Middle-right: Cluster frequency
  {
    panel(cv, r_mr, "Cluster frequency GHz", "", th, [&](Canvas& c, Rect in) {
      if (in.h < 3 || st.h_core_mhz.empty()) {
        c.text_clipped(in.x, in.y, in.w, "no frequency history", th.dim, th.panel);
        return;
      }
      int leg_h = std::min(2, std::max(1, in.h - 3));
      Rect chart{in.x, in.y, in.w, in.h - leg_h - 1};
      if (chart.h < 2) chart.h = std::max(1, in.h - 1);
      std::vector<std::vector<double>> series;
      std::vector<RGB> colors;
      double maxghz = 0.0;
      // group by cluster: average mhz per cluster, else per-core if few
      int nclusters = 0;
      for (const auto& core : s.cores) nclusters = std::max(nclusters, core.cluster + 1);
      bool by_cluster = nclusters > 1 && nclusters <= 8;
      if (by_cluster) {
        series.resize(static_cast<std::size_t>(nclusters));
        for (int cl = 0; cl < nclusters; ++cl) {
          // average history across cores in cluster
          std::vector<std::size_t> members;
          for (std::size_t i = 0; i < s.cores.size(); ++i) if (s.cores[i].cluster == cl) members.push_back(i);
          if (members.empty()) continue;
          std::size_t len = st.h_core_mhz[members[0]].tail(win).size();
          series[static_cast<std::size_t>(cl)].assign(len, 0.0);
          for (std::size_t mi = 0; mi < members.size(); ++mi) {
            auto hv = st.h_core_mhz[members[mi]].tail(win);
            for (std::size_t k = 0; k < hv.size() && k < series[static_cast<std::size_t>(cl)].size(); ++k)
              series[static_cast<std::size_t>(cl)][k] += hv[k] / 1000.0;
          }
          for (double& v : series[static_cast<std::size_t>(cl)]) { v /= static_cast<double>(members.size()); if (v > maxghz) maxghz = v; }
          colors.push_back(rd_sc(th, static_cast<std::size_t>(cl)));
        }
      } else {
        std::size_t count = std::min<std::size_t>(s.cores.size(), 8);
        for (std::size_t i = 0; i < count; ++i) {
          auto hv = st.h_core_mhz[i].tail(win);
          std::vector<double> ghz(hv.size(), 0.0);
          for (std::size_t k = 0; k < hv.size(); ++k) { ghz[k] = hv[k] / 1000.0; if (ghz[k] > maxghz) maxghz = ghz[k]; }
          series.push_back(std::move(ghz));
          colors.push_back(rd_sc(th, i));
        }
      }
      if (maxghz < 0.5) maxghz = 0.5;
      c.braille_chart(chart, series, colors, 0.0, maxghz * 1.1, false);
      chart_labels(c, chart, fmt.fixed(maxghz, 1) + " GHz", "0.0", "", th);
      rd_xaxis_labels(c, Rect{in.x, chart.y + chart.h, in.w, 1}, st.cfg.refresh_ms, th);
      // legend
      std::vector<std::pair<std::string, RGB>> items;
      if (by_cluster) {
        for (int cl = 0; cl < nclusters && cl < 6; ++cl) items.push_back({"C" + std::to_string(cl), rd_sc(th, static_cast<std::size_t>(cl))});
      } else {
        for (std::size_t i = 0; i < std::min<std::size_t>(series.size(), 6); ++i) items.push_back({"cpu" + std::to_string(i), rd_sc(th, i)});
      }
      if (in.h - chart.h - 1 > 0) rd_legend(c, Rect{in.x, in.y + in.h - 1, in.w, 1}, items, th);
    });
  }

  // ------------------------------------------------ Bottom-left: Threads / Processes
  {
    panel(cv, r_bl, "Threads / Processes", "by CPU | % of one core", th, [&](Canvas& c, Rect in) {
      if (in.h < 2) return;
      bool show_avg = in.w >= 78;
      bool show_peak = in.w >= 86;
      bool show_core = in.w >= 60;
      bool show_time = in.w >= 70;
      int spark_w = 12;
      int name_w = std::clamp(in.w / 5, 8, 18);
      std::vector<Col> cols;
      cols.push_back(Col{"PID", 6, false});
      cols.push_back(Col{"NAME", name_w, false});
      cols.push_back(Col{"S", 1, false});
      cols.push_back(Col{"CPU%", 6, true});
      if (show_avg) cols.push_back(Col{"AVG", 6, true});
      if (show_peak) cols.push_back(Col{"PEAK", 6, true});
      cols.push_back(Col{"LAST " + rd_time_ago(win, st.cfg.refresh_ms).substr(1), spark_w, false});
      if (show_core) cols.push_back(Col{"CORE", 4, false});
      if (show_time) cols.push_back(Col{"TIME", 7, true});
      rd_fit_cols(cols, in.w);
      // recompute spark width if tight
      int used = 0; for (const auto& col : cols) used += col.w + 1;
      // adjust spark column width
      for (auto& col : cols) if (col.head.rfind("LAST", 0) == 0) { col.w = std::max(6, col.w + (in.w - used)); }

      std::size_t cap = static_cast<std::size_t>(std::max(0, in.h - 2));
      auto procs = select_procs(s, st, cap);
      std::vector<std::vector<std::string>> rows;
      std::vector<RGB> colors;
      for (const ProcInfo* p : procs) {
        std::vector<std::string> row;
        row.push_back(std::to_string(p->pid));
        row.push_back(p->name);
        row.push_back(std::string(1, p->state ? p->state : '?'));
        row.push_back(fmt.pct(p->cpu_pct));
        if (show_avg) row.push_back(fmt.pct(p->cpu_avg));
        if (show_peak) row.push_back(fmt.pct(p->cpu_peak));
        // spark
        int sw = spark_w;
        for (const auto& col : cols) if (col.head.rfind("LAST", 0) == 0) sw = col.w;
        row.push_back(rd_spark_str(p->spark, sw));
        if (show_core) row.push_back(p->core >= 0 ? ("c" + std::to_string(p->core)) : "-");
        if (show_time) row.push_back(rd_compact_dur(p->age_s));
        if (row.size() > cols.size()) row.resize(cols.size());
        rows.push_back(std::move(row));
        colors.push_back(p->cpu_pct > 100 ? th.red : (p->cpu_pct > 50 ? th.yellow : th.fg));
      }
      draw_table(c, in, cols, rows, th, colors);
    });
  }

  // ------------------------------------------------ Bottom-right: Process Groups / Thread Pools
  {
    panel(cv, r_br, "Process Groups / Thread Pools", "", th, [&](Canvas& c, Rect in) {
      if (in.h < 2) return;
      struct Group { std::string name; int n = 0; double now = 0; double avg = 0; };
      std::unordered_map<std::string, Group> groups;
      for (const auto& p : s.procs) {
        std::string g = rd_proc_group_name(p.name);
        auto& it = groups[g];
        it.name = g; it.n += 1; it.now += p.cpu_pct; it.avg += p.cpu_avg;
      }
      std::vector<Group> list;
      list.reserve(groups.size());
      for (auto& kv : groups) list.push_back(std::move(kv.second));
      std::sort(list.begin(), list.end(), [](const Group& a, const Group& b){ return a.now > b.now; });
      int name_w = std::clamp(in.w / 3, 8, 18);
      bool show_avg = in.w >= 34;
      int share_w = 8;
      std::vector<Col> cols;
      cols.push_back(Col{"GROUP", name_w, false});
      cols.push_back(Col{"N", 3, true});
      cols.push_back(Col{"NOW%", 6, true});
      if (show_avg) cols.push_back(Col{"AVG%", 6, true});
      cols.push_back(Col{"SHARE", share_w, false});
      rd_fit_cols(cols, in.w);
      // adjust share width
      int used = 0; for (const auto& col : cols) used += col.w + 1;
      for (auto& col : cols) if (col.head == "SHARE") col.w = std::max(4, col.w + (in.w - used));
      for (const auto& col : cols) if (col.head == "SHARE") share_w = col.w;
      double max_now = 0; for (const auto& g : list) if (g.now > max_now) max_now = g.now;
      if (max_now <= 0) max_now = 1.0;
      std::size_t cap = static_cast<std::size_t>(std::max(0, in.h - 2));
      if (list.size() > cap) list.resize(cap);
      std::vector<std::vector<std::string>> rows;
      for (const auto& g : list) {
        std::vector<std::string> row;
        row.push_back(g.name);
        row.push_back(std::to_string(g.n));
        row.push_back(fmt.pct(g.now));
        if (show_avg) row.push_back(fmt.pct(g.avg));
        row.push_back(rd_bar_str(g.now / max_now, share_w));
        if (row.size() > cols.size()) row.resize(cols.size());
        rows.push_back(std::move(row));
      }
      draw_table(c, in, cols, rows, th);
    });
  }
}
// page 5 - stacked memory area, breakdown, top processes
inline void view_memory(Canvas& cv, const Rect& area, const Snapshot& s, const AppState& st,
                        const Theme& th, const UnitFormatter& fmt) {
  using namespace detail;
  if (area.w < 24 || area.h < 8) return;
  const std::size_t win = st.window_samples();
  int h1 = std::clamp(area.h * 45 / 100, 5, std::max(5, area.h - 3));
  int h2 = area.h >= 16 ? 4 : std::min(3, std::max(0, area.h - h1));
  if (h1 + h2 > area.h) h2 = std::max(0, area.h - h1);
  const Rect rc{area.x, area.y, area.w, h1};
  const Rect rb{area.x, area.y + h1, area.w, h2};
  const Rect rt{area.x, area.y + h1 + h2, area.w, area.h - h1 - h2};

  {
    auto ma = st.h_mem_app.tail(win);
    auto mk = st.h_mem_kern.tail(win);
    auto mc = st.h_mem_cache.tail(win);
    auto ms = st.h_mem_swap.tail(win);
    auto mf = st.h_mem_free.tail(win);
    std::vector<std::vector<double>> ser{ma, mk, mc, ms, mf};
    const double total = static_cast<double>(std::max<uint64_t>(s.mem.total, 1));
    const double hi = std::max(total, rd_peak(ser));
    panel(cv, rc, "MEMORY",
          "total " + fmt.bytes(static_cast<double>(s.mem.total)) + "  avail " +
              fmt.bytes(static_cast<double>(s.mem.avail)),
          th, [&](Canvas& c, Rect in) {
            if (in.h < 1) return;
            const int legrows = in.h >= 6 ? 2 : (in.h >= 3 ? 1 : 0);
            Rect chart{in.x, in.y, in.w, in.h - legrows};
            c.stacked_area(chart, ser,
                           {rd_sc(th, 0), rd_sc(th, 1), rd_sc(th, 2), rd_sc(th, 3), rd_sc(th, 4)},
                           0.0, hi);
            chart_labels(c, chart, fmt.bytes_short(hi), "0",
                         "app " + fmt.bytes_short(ma.empty() ? 0.0 : ma.back()), th);
            if (legrows > 0)
              rd_legend(c, Rect{in.x, in.y + in.h - legrows, in.w, legrows},
                        {{"App " + fmt.bytes_short(ma.empty() ? 0.0 : ma.back()), rd_sc(th, 0)},
                         {"Kern " + fmt.bytes_short(mk.empty() ? 0.0 : mk.back()), rd_sc(th, 1)},
                         {"Cache " + fmt.bytes_short(mc.empty() ? 0.0 : mc.back()), rd_sc(th, 2)},
                         {"Swap " + fmt.bytes_short(ms.empty() ? 0.0 : ms.back()), rd_sc(th, 3)},
                         {"Free " + fmt.bytes_short(mf.empty() ? 0.0 : mf.back()), rd_sc(th, 4)}},
                        th);
          });
  }

  panel(cv, rb, "MEMORY BREAKDOWN", fmt.pct(s.mem.pressure) + " pressure", th,
        [&](Canvas& c, Rect in) {
          if (in.h < 1) return;
          struct CellV {
            const char* label;
            std::string value;
            RGB col;
          };
          const std::vector<CellV> cellsv = {
              {"Total", fmt.bytes(static_cast<double>(s.mem.total)), th.fg},
              {"Avail", fmt.bytes(static_cast<double>(s.mem.avail)), th.green},
              {"App", fmt.bytes(static_cast<double>(s.mem.used)), rd_sc(th, 0)},
              {"Cached", fmt.bytes(static_cast<double>(s.mem.cached + s.mem.buffers)), th.cyan},
              {"Buffers", fmt.bytes(static_cast<double>(s.mem.buffers)), th.dim},
              {"Dirty", fmt.bytes(static_cast<double>(s.mem.dirty)), th.yellow},
              {"Swap", fmt.bytes(static_cast<double>(s.mem.swap_used)) + " / " +
                           fmt.bytes(static_cast<double>(s.mem.swap_total)),
               th.magenta},
              {"Pressure", fmt.pct(s.mem.pressure),
               heat_color(th, std::clamp(s.mem.pressure / 100.0, 0.0, 1.0))}};
          const int cc = in.w >= 64 ? 4 : 2;
          const int crow = (static_cast<int>(cellsv.size()) + cc - 1) / cc;
          const int cw = std::max(4, in.w / cc);
          for (int i = 0; i < crow; ++i) {
            if (i >= in.h) break;
            for (int j = 0; j < cc; ++j) {
              const int k = i * cc + j;
              if (k >= static_cast<int>(cellsv.size())) break;
              const int x = in.x + j * cw;
              const int w = std::min(cw, in.x + in.w - x) - 1;
              if (w < 4 || x >= in.x + in.w) continue;
              const CellV& cell = cellsv[static_cast<std::size_t>(k)];
              const int lw = std::min(8, std::max(3, w / 3));
              c.text_clipped(x, in.y + i, lw, cell.label, th.dim, th.panel);
              c.text_clipped(x + lw + 1, in.y + i, std::max(0, w - lw - 1), cell.value, cell.col,
                             th.panel);
            }
          }
        });

  panel(cv, rt, "TOP MEMORY", std::to_string(s.procs.size()) + " procs", th,
        [&](Canvas& c, Rect in) {
          if (in.h < 2) return;
          int namew = std::clamp(in.w / 4, 8, 20);
          int fixedw = 7 + (namew + 1) + 10 + 6;
          int barw = in.w - fixedw - 1;
          if (barw < 4) {
            barw = 0;
            namew = std::max(6, in.w - 24);
            fixedw = 7 + (namew + 1) + 10 + 6;
          }
          std::vector<Col> cols;
          cols.push_back(Col{"PID", 6, false});
          cols.push_back(Col{"NAME", namew, false});
          cols.push_back(Col{"RSS", 9, true});
          cols.push_back(Col{"MEM%", 5, true});
          if (barw >= 4) cols.push_back(Col{"USAGE", barw, false});
          rd_fit_cols(cols, in.w);
          std::size_t cap = static_cast<std::size_t>(in.h - 2);
          auto procs = rd_top_mem(s, cap);
          const double total = static_cast<double>(std::max<uint64_t>(s.mem.total, 1));
          std::vector<std::vector<std::string>> rows;
          for (const ProcInfo* p : procs) {
            std::vector<std::string> row{std::to_string(p->pid), p->name,
                                         fmt.bytes(static_cast<double>(p->rss)), fmt.pct(p->mem_pct)};
            if (barw >= 4)
              row.push_back(rd_bar_str(static_cast<double>(p->rss) / total, barw));
            if (row.size() > cols.size()) row.resize(cols.size());
            rows.push_back(std::move(row));
          }
          draw_table(c, in, cols, rows, th);
        });
}
// page 6 - RX/TX counters + chart, sockets, disk I/O per process
inline void view_netio(Canvas& cv, const Rect& area, const Snapshot& s, const AppState& st,
                       const Theme& th, const UnitFormatter& fmt) {
  using namespace detail;
  if (area.w < 24 || area.h < 8) return;
  const std::size_t win = st.window_samples();
  int r1 = area.h / 3, r2 = area.h / 3;
  if (area.h < 17) {
    r1 = std::max(4, area.h * 40 / 100);
    r2 = std::max(4, (area.h - r1) / 2);
  }
  const int r3 = area.h - r1 - r2;
  const int c1w = area.w * 58 / 100;
  const int c2w = area.w - c1w;
  const Rect net{area.x, area.y, c1w, r1};
  const Rect tot{area.x + c1w, area.y, c2w, r1};
  const Rect ifc{area.x, area.y + r1, c1w, r2};
  const Rect dsk{area.x + c1w, area.y + r1, c2w, r2};
  const Rect dpr{area.x, area.y + r1 + r2, c1w, r3};
  const Rect dev{area.x + c1w, area.y + r1 + r2, c2w, r3};

  {
    auto rx = st.h_net_rx.tail(win);
    auto tx = st.h_net_tx.tail(win);
    const double peak = rd_peak({rx, tx});
    const double hi = std::max(1024.0, peak * 1.15);
    panel(cv, net, "NETWORK RX/TX",
          "R " + fmt.rate_short(s.net.rx_bps) + "  T " + fmt.rate_short(s.net.tx_bps), th,
          [&](Canvas& c, Rect in) {
            if (in.h < 1) return;
            const int legrows = in.h >= 6 ? 2 : (in.h >= 3 ? 1 : 0);
            Rect chart{in.x, in.y, in.w, in.h - legrows};
            c.braille_chart(chart, {rx, tx}, {th.cyan, th.magenta}, 0.0, hi, false);
            chart_labels(c, chart, fmt.rate_short(hi), "0", "avg " + fmt.rate_short(rd_avg(rx)), th);
            if (legrows > 0)
              rd_legend(c, Rect{in.x, in.y + in.h - legrows, in.w, legrows},
                        {{"RX " + fmt.rate_short(rx.empty() ? 0.0 : rx.back()), th.cyan},
                         {"TX " + fmt.rate_short(tx.empty() ? 0.0 : tx.back()), th.magenta},
                         {"sRX " + fmt.bytes(static_cast<double>(s.net.session_rx)), th.blue},
                         {"sTX " + fmt.bytes(static_cast<double>(s.net.session_tx)), th.orange}},
                        th);
          });
  }

  panel(cv, tot, "TOTALS", "session / boot", th, [&](Canvas& c, Rect in) {
    int y = in.y;
    auto pair_line = [&](const char* k1, const std::string& v1, const char* k2,
                         const std::string& v2, RGB col) {
      if (y >= in.y + in.h) return;
      const int colw = in.w >= 36 ? in.w / 2 : in.w;
      const int lw = std::min(9, std::max(4, colw / 3));
      c.text_clipped(in.x, y, lw, k1, th.dim, th.panel);
      c.text_clipped(in.x + lw, y, std::max(0, colw - lw), v1, col, th.panel);
      if (colw < in.w && !v2.empty()) {
        c.text_clipped(in.x + colw, y, lw, k2, th.dim, th.panel);
        c.text_clipped(in.x + colw + lw, y, std::max(0, in.w - colw - lw), v2, col, th.panel);
      }
      ++y;
    };
    pair_line("sess RX", fmt.bytes(static_cast<double>(s.net.session_rx)), "sess TX",
              fmt.bytes(static_cast<double>(s.net.session_tx)), th.blue);
    pair_line("boot RX", fmt.bytes(static_cast<double>(s.net.rx_total)), "boot TX",
              fmt.bytes(static_cast<double>(s.net.tx_total)), th.cyan);
    pair_line("pkts RX", UnitFormatter::fixed(static_cast<double>(s.net.rx_pkt), 0), "pkts TX",
              UnitFormatter::fixed(static_cast<double>(s.net.tx_pkt), 0), th.fg);
    pair_line("errs RX", UnitFormatter::fixed(static_cast<double>(s.net.rx_err), 0), "errs TX",
              UnitFormatter::fixed(static_cast<double>(s.net.tx_err), 0),
              (s.net.rx_err || s.net.tx_err) ? th.red : th.green);
    pair_line("tcp", UnitFormatter::fixed(s.net.tcp_socks, 0), "udp",
              UnitFormatter::fixed(s.net.udp_socks, 0), th.fg);
    pair_line("estab", UnitFormatter::fixed(s.net.tcp_estab, 0), "tw",
              UnitFormatter::fixed(s.net.tcp_timewait, 0), th.accent);
  });

  panel(cv, ifc, "INTERFACES", std::to_string(s.net.devs.size()) + " devs", th,
        [&](Canvas& c, Rect in) {
          if (in.h < 2) return;
          std::vector<Col> cols;
          cols.push_back(Col{"IFACE", std::min(10, std::max(6, in.w / 4)), false});
          cols.push_back(Col{"RX/s", 9, true});
          cols.push_back(Col{"TX/s", 9, true});
          if (in.w >= 44) cols.push_back(Col{"ERR", 5, true});
          if (in.w >= 56) cols.push_back(Col{"RX TOT", 10, true});
          rd_fit_cols(cols, in.w);
          std::vector<std::vector<std::string>> rows;
          for (const auto& d : s.net.devs) {
            std::vector<std::string> row{d.name, fmt.rate_short(d.rx_bps), fmt.rate_short(d.tx_bps)};
            if (cols.size() >= 4)
              row.push_back(UnitFormatter::fixed(static_cast<double>(d.rx_err + d.tx_err), 0));
            if (cols.size() >= 5) row.push_back(fmt.bytes(static_cast<double>(d.rx)));
            if (row.size() > cols.size()) row.resize(cols.size());
            rows.push_back(std::move(row));
          }
          draw_table(c, in, cols, rows, th);
        });

  {
    auto dr = st.h_disk_r.tail(win);
    auto dw = st.h_disk_w.tail(win);
    const double peak = rd_peak({dr, dw});
    const double hi = std::max(128.0 * 1024.0, peak * 1.15);
    panel(cv, dsk, "DISK I/O",
          "lat " + fmt.fixed(s.disk.lat_ms, 1) + " ms  " + fmt.fixed(s.disk.iops, 0) + " iops", th,
          [&](Canvas& c, Rect in) {
            if (in.h < 1) return;
            const int legrows = in.h >= 6 ? 2 : (in.h >= 3 ? 1 : 0);
            Rect chart{in.x, in.y, in.w, in.h - legrows};
            c.braille_chart(chart, {dr, dw}, {rd_sc(th, 0), rd_sc(th, 1)}, 0.0, hi, false);
            chart_labels(c, chart, fmt.rate_short(hi), "0", "avg " + fmt.rate_short(rd_avg(dr)), th);
            if (legrows > 0)
              rd_legend(c, Rect{in.x, in.y + in.h - legrows, in.w, legrows},
                        {{"R " + fmt.rate_short(s.disk.r_bps), rd_sc(th, 0)},
                         {"W " + fmt.rate_short(s.disk.w_bps), rd_sc(th, 1)},
                         {"util " + fmt.pct(s.disk.util), th.yellow},
                         {"lat " + fmt.fixed(s.disk.lat_ms, 1) + "ms", th.dim}},
                        th);
          });
  }

  panel(cv, dpr, "DISK BY PROCESS", "read+write", th, [&](Canvas& c, Rect in) {
    if (in.h < 2) return;
    std::vector<Col> cols;
    cols.push_back(Col{"PID", 6, false});
    cols.push_back(Col{"NAME", std::clamp(in.w / 5, 8, 18), false});
    cols.push_back(Col{"R/s", 9, true});
    cols.push_back(Col{"W/s", 9, true});
    if (in.w >= 52) cols.push_back(Col{"READ", 10, true});
    if (in.w >= 64) cols.push_back(Col{"WRITE", 10, true});
    rd_fit_cols(cols, in.w);
    std::size_t cap = static_cast<std::size_t>(in.h - 2);
    auto procs = rd_top_disk(s, cap);
    std::vector<std::vector<std::string>> rows;
    for (const ProcInfo* p : procs) {
      std::vector<std::string> row{std::to_string(p->pid), p->name, fmt.rate_short(p->rd_bps),
                                   fmt.rate_short(p->wr_bps)};
      if (cols.size() >= 5) row.push_back(fmt.bytes(static_cast<double>(p->rd_total)));
      if (cols.size() >= 6) row.push_back(fmt.bytes(static_cast<double>(p->wr_total)));
      if (row.size() > cols.size()) row.resize(cols.size());
      rows.push_back(std::move(row));
    }
    draw_table(c, in, cols, rows, th);
  });

  panel(cv, dev, "DEVICES", std::to_string(s.disk.devs.size()) + " disks", th,
        [&](Canvas& c, Rect in) {
          if (in.h < 2) return;
          std::vector<Col> cols;
          cols.push_back(Col{"DEV", std::min(10, std::max(6, in.w / 4)), false});
          cols.push_back(Col{"R/s", 9, true});
          cols.push_back(Col{"W/s", 9, true});
          if (in.w >= 44) cols.push_back(Col{"IOPS", 6, true});
          if (in.w >= 50) cols.push_back(Col{"LAT", 6, true});
          rd_fit_cols(cols, in.w);
          std::vector<std::vector<std::string>> rows;
          for (const auto& d : s.disk.devs) {
            std::vector<std::string> row{d.name, fmt.rate_short(d.r_bps), fmt.rate_short(d.w_bps)};
            if (cols.size() >= 4) row.push_back(fmt.fixed(d.iops, 0));
            if (cols.size() >= 5) row.push_back(fmt.fixed(d.lat_ms, 1));
            if (row.size() > cols.size()) row.resize(cols.size());
            rows.push_back(std::move(row));
          }
          draw_table(c, in, cols, rows, th);
        });
}
// page 7 - mounts df bars + async tokei-style LOC
inline void view_fscode(Canvas& cv, const Rect& area, const Snapshot& s, const AppState& st,
                        const Theme& th, const UnitFormatter& fmt) {
  using namespace detail;
  if (area.w < 24 || area.h < 6) return;
  Rect mounts, scan, langs;
  if (area.w >= 76 && area.h >= 14) {
    int topH = std::max(8, area.h * 52 / 100);
    topH = std::min(topH, std::max(6, area.h - 5));
    const int mw = area.w * 58 / 100;
    mounts = Rect{area.x, area.y, mw, topH};
    scan = Rect{area.x + mw, area.y, area.w - mw, topH};
    langs = Rect{area.x, area.y + topH, area.w, area.h - topH};
  } else {
    int h1 = std::max(5, area.h * 45 / 100);
    int h2 = std::max(4, area.h * 25 / 100);
    if (h1 + h2 > area.h) h2 = std::max(0, area.h - h1);
    mounts = Rect{area.x, area.y, area.w, h1};
    scan = Rect{area.x, area.y + h1, area.w, h2};
    langs = Rect{area.x, area.y + h1 + h2, area.w, area.h - h1 - h2};
  }

  panel(cv, mounts, "MOUNTS", std::to_string(s.fs.size()) + " filesystems", th,
        [&](Canvas& c, Rect in) {
          if (in.h < 1 || in.w < 20) return;
          int mountw = std::clamp(in.w / 3, 8, 26);
          int fsw = 7, usedw = 13, pctw = 5;
          int barw = in.w - mountw - fsw - usedw - pctw - 4;
          if (barw < 3) {
            fsw = 0;
            barw = in.w - mountw - usedw - pctw - 3;
          }
          if (barw < 0) barw = 0;
          int y = in.y;
          if (in.h >= 3) {
            int x = in.x;
            c.text_clipped(x, y, mountw, "MOUNT", th.muted, th.panel);
            x += mountw + 1;
            if (fsw > 0) {
              c.text_clipped(x, y, fsw, "FS", th.muted, th.panel);
              x += fsw + 1;
            }
            if (barw > 0) {
              c.text_clipped(x, y, barw, "USAGE", th.muted, th.panel);
              x += barw + 1;
            }
            c.text_clipped(x, y, usedw, "USED / TOTAL", th.muted, th.panel);
            c.text_clipped(in.x + in.w - pctw, y, pctw, "USE%", th.muted, th.panel);
            ++y;
            c.hline(in.x, y, in.w, 0x2500, th.border, th.panel);
            ++y;
          }
          for (const auto& f : s.fs) {
            if (y >= in.y + in.h) break;
            int x = in.x;
            c.text_clipped(x, y, mountw, f.mount, th.fg, th.panel);
            x += mountw + 1;
            if (fsw > 0) {
              c.text_clipped(x, y, fsw, f.fstype, th.dim, th.panel);
              x += fsw + 1;
            }
            const double frac = std::clamp(f.pct / 100.0, 0.0, 1.0);
            if (barw > 0) {
              const RGB col = frac > 0.9 ? th.red : (frac > 0.75 ? th.yellow : th.green);
              c.hbar(x, y, barw, frac, col, th.border, th.panel);
              x += barw + 1;
            }
            const std::string ut = fmt.bytes(static_cast<double>(f.used)) + "/" +
                                   fmt.bytes(static_cast<double>(f.total));
            c.text_clipped(x, y, usedw, pad_left(ut, usedw), th.fg, th.panel);
            c.text_clipped(in.x + in.w - pctw, y, pctw, pad_left(fmt.fixed(f.pct, 0) + "%", pctw),
                           rd_pct_color(th, f.pct), th.panel);
            ++y;
          }
        });

  panel(cv, scan, "CODE SCAN",
        s.code.running ? "running" : (s.code.done ? "done" : "idle"), th, [&](Canvas& c, Rect in) {
          int y = in.y;
          const char* state = s.code.running ? "SCANNING" : (s.code.done ? "DONE" : "IDLE");
          const RGB sc = s.code.running ? th.yellow : (s.code.done ? th.green : th.dim);
          if (y < in.y + in.h) {
            badge(c, in.x, y, state, th.bg, sc);
            const int usedx = text_width(state) + 3;
            if (in.w > usedx + 4)
              c.text_clipped(in.x + usedx, y, in.w - usedx, s.code.done ? "complete" : "async thread",
                             th.dim, th.panel);
            ++y;
          }
          std::string path = s.code.path.empty() ? (st.cfg.scan_path.empty() ? std::string("$HOME") : st.cfg.scan_path)
                                                 : s.code.path;
          if (y < in.y + in.h) {
            c.text_clipped(in.x, y, in.w, "path " + path, th.fg, th.panel);
            ++y;
          }
          if (y < in.y + in.h) {
            c.text_clipped(in.x, y, in.w,
                           UnitFormatter::fixed(static_cast<double>(s.code.files), 0) + " files   " +
                               UnitFormatter::fixed(static_cast<double>(s.code.total_lines), 0) + " lines",
                           th.fg, th.panel);
            ++y;
          }
          if (y < in.y + in.h) {
            c.text_clipped(in.x, y, in.w,
                           fmt.bytes(static_cast<double>(s.code.total_bytes)) + "   " +
                               fmt.duration(s.code.elapsed),
                           th.fg, th.panel);
            ++y;
          }
          if (y < in.y + in.h) {
            double frac = s.code.progress;
            if (frac > 1.0) frac /= 100.0;
            frac = std::clamp(frac, 0.0, 1.0);
            if (!s.code.running && s.code.done) frac = 1.0;
            const int vw = 7;
            const int bw = in.w - vw - 1;
            if (bw > 2) c.hbar(in.x, y, bw, frac, sc, th.border, th.panel);
            c.text_clipped(in.x + in.w - vw, y, vw, pad_left(fmt.pct(frac * 100.0), vw), sc, th.panel);
            ++y;
          }
          if (s.code.running && y < in.y + in.h) {
            c.text_clipped(in.x, y, in.w, "scanning... " + s.code.current, th.yellow, th.panel);
          } else if (!s.code.running && !s.code.done && y < in.y + in.h) {
            c.text_clipped(in.x, y, in.w, "press r to start async scan", th.dim, th.panel);
          }
        });

  panel(cv, langs, "CODE BY LANGUAGE",
        UnitFormatter::fixed(static_cast<double>(s.code.total_code), 0) + " code lines", th,
        [&](Canvas& c, Rect in) {
          if (in.h < 1 || in.w < 24) return;
          uint64_t total = s.code.total_code;
          if (total == 0)
            for (const auto& l : s.code.langs) total += l.code;
          if (total == 0) total = 1;
          int namew = std::clamp(in.w / 4, 8, 18);
          int filew = 7, codew = 9, pctw = 6;
          int barw = in.w - namew - filew - codew - pctw - 4;
          if (barw < 3) {
            barw = 0;
            namew = std::max(8, in.w - filew - codew - pctw - 3);
          }
          int y = in.y;
          if (in.h >= 3) {
            int x = in.x;
            c.text_clipped(x, y, namew, "LANG", th.muted, th.panel);
            x += namew + 1;
            c.text_clipped(x, y, filew, "FILES", th.muted, th.panel);
            x += filew + 1;
            c.text_clipped(x, y, codew, "CODE", th.muted, th.panel);
            x += codew + 1;
            if (barw > 0) {
              c.text_clipped(x, y, barw, "SHARE", th.muted, th.panel);
              x += barw + 1;
            }
            c.text_clipped(in.x + in.w - pctw, y, pctw, "%", th.muted, th.panel);
            ++y;
            c.hline(in.x, y, in.w, 0x2500, th.border, th.panel);
            ++y;
          }
          int i = 0;
          for (const auto& l : s.code.langs) {
            if (y >= in.y + in.h) break;
            const double frac = static_cast<double>(l.code) / static_cast<double>(total);
            int x = in.x;
            c.text_clipped(x, y, namew, l.lang, th.fg, th.panel);
            x += namew + 1;
            c.text_clipped(x, y, filew, pad_left(UnitFormatter::fixed(static_cast<double>(l.files), 0), filew),
                           th.dim, th.panel);
            x += filew + 1;
            c.text_clipped(x, y, codew, pad_left(UnitFormatter::fixed(static_cast<double>(l.code), 0), codew),
                           th.fg, th.panel);
            x += codew + 1;
            const RGB col = rd_sc(th, static_cast<std::size_t>(i));
            if (barw > 0) {
              c.hbar(x, y, barw, std::clamp(frac, 0.0, 1.0), col, th.border, th.panel);
              x += barw + 1;
            }
            c.text_clipped(in.x + in.w - pctw, y, pctw,
                           pad_left(fmt.fixed(frac * 100.0, 1) + "%", pctw), col, th.panel);
            ++y;
            ++i;
          }
        });
}

}  // namespace ct
