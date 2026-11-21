// ClangTop++ - system views: Overview, Power, Kernel & IRQ
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
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
inline RGB vs_sc(const Theme& th, std::size_t i) {
  if (th.series.empty()) return th.accent;
  return th.series[i % th.series.size()];
}

inline Rect vs_cell(const Rect& a, int cols, int rows, int idx) {
  if (cols <= 0 || rows <= 0 || a.w <= 0 || a.h <= 0) return Rect{};
  const int c = idx % cols, r = idx / cols;
  const int x0 = a.x + a.w * c / cols;
  const int x1 = a.x + a.w * (c + 1) / cols;
  const int y0 = a.y + a.h * r / rows;
  const int y1 = a.y + a.h * (r + 1) / rows;
  return Rect{x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
}

inline double vs_peak(const std::vector<std::vector<double>>& ss) {
  double m = 0.0;
  for (const auto& v : ss)
    for (double x : v) m = std::max(m, x);
  return m;
}

inline double vs_avg(const std::vector<double>& v) {
  if (v.empty()) return 0.0;
  double sum = 0.0;
  for (double x : v) sum += x;
  return sum / static_cast<double>(v.size());
}

inline std::string vs_pct0(double v) { return UnitFormatter::fixed(v, 0) + "%"; }

// Colored block + label legend, wraps onto r.h rows.
inline void vs_legend(Canvas& cv, const Rect& r,
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

// "16 █ 42%" core cell, clipped to w columns.
inline void vs_core_cell(Canvas& cv, int x, int y, int w, double u, const std::string& label,
                         const Theme& th) {
  if (w < 2) return;
  const RGB col = heat_color(th, u);
  const int lab = std::min<int>(3, w - 1);
  cv.text_clipped(x, y, lab, label, th.dim, th.panel);
  int px = x + lab;
  int rest = w - lab;
  if (rest <= 0) return;
  cv.glyph(px, y, 0x2588, col);
  ++px;
  --rest;
  if (rest > 0) cv.text_clipped(px, y, rest, vs_pct0(u * 100.0), col, th.panel);
}

inline std::vector<const ProcInfo*> vs_top_cpu(const Snapshot& s, std::size_t n) {
  std::vector<const ProcInfo*> out;
  out.reserve(s.procs.size());
  for (const auto& p : s.procs) out.push_back(&p);
  std::sort(out.begin(), out.end(),
            [](const ProcInfo* a, const ProcInfo* b) { return a->cpu_pct > b->cpu_pct; });
  if (out.size() > n) out.resize(n);
  return out;
}

// Kernel thread / daemon heuristic: real kernel threads or core daemons, low thread count.
inline bool vs_kernel_task(const ProcInfo& p) {
  if (p.threads > 2) return false;
  if (!p.name.empty() && p.name[0] == 'k') return true;
  static const char* const exact[] = {
      "init",   "systemd",  "systemd-journal", "systemd-udevd", "journald", "dbus-daemon",
      "cron",   "crond",    "rsyslogd",        "auditd",        "sshd",     "irqbalance",
      "thermald", "haveged", "polkitd",        "agetty",        "login",    "kthreadd",
      "idle"};
  static const char* const prefix[] = {"migration", "rcu_", "watchdog", "ksoftirqd", "scsi_",
                                       "jbd2",      "kworker", "kcompactd"};
  for (const char* k : exact)
    if (p.name == k) return true;
  for (const char* k : prefix) {
    const std::string key(k);
    if (p.name.compare(0, key.size(), key) == 0) return true;
  }
  return false;
}

}  // namespace detail
// page 1 - six panel grid (Overview)
inline void view_overview(Canvas& cv, const Rect& area, const Snapshot& s, const AppState& st,
                          const Theme& th, const UnitFormatter& fmt) {
  using namespace detail;
  if (area.w < 24 || area.h < 6) return;
  const std::size_t win = st.window_samples();
  const int cols = area.w >= 76 ? 2 : 1;
  const int rows = area.h >= 17 ? 3 : (area.h >= 10 ? 2 : 1);

  for (int idx = 0; idx < 6; ++idx) {
    const Rect r = vs_cell(area, cols, rows, idx);
    if (r.w < 8 || r.h < 3) continue;

    if (idx == 0) {
      auto app = st.h_cpu_app.tail(win);
      auto kern = st.h_cpu_kern.tail(win);
      panel(cv, r, "CPU", "busy " + fmt.pct(s.cpu.total), th, [&](Canvas& c, Rect in) {
        if (in.h < 1) return;
        const bool leg = in.h >= 3;
        Rect chart{in.x, in.y, in.w, leg ? in.h - 1 : in.h};
        c.braille_chart(chart, {app, kern}, {vs_sc(th, 0), vs_sc(th, 1)}, 0.0, 100.0, false);
        chart_labels(c, chart, "100%", "0%", "avg " + fmt.pct(vs_avg(app)), th);
        if (leg)
          vs_legend(c, Rect{in.x, in.y + in.h - 1, in.w, 1},
                    {{"App " + fmt.pct(app.empty() ? 0.0 : app.back()), vs_sc(th, 0)},
                     {"Kern " + fmt.pct(kern.empty() ? 0.0 : kern.back()), vs_sc(th, 1)},
                     {"IO " + fmt.pct(s.cpu.io), th.yellow},
                     {"IRQ " + fmt.pct(s.cpu.irq), th.magenta}},
                    th);
      });
    } else if (idx == 1) {
      const int n = static_cast<int>(s.cores.size());
      double avg = 0.0;
      for (const auto& cc : s.cores) avg += cc.util;
      if (n) avg /= static_cast<double>(n);
      panel(cv, r, "CORES", std::to_string(n ? n : s.ncpu) + " cores " + fmt.pct(avg), th,
            [&](Canvas& c, Rect in) {
              if (n == 0) {
                c.text_clipped(in.x, in.y, in.w, "no per-core data", th.dim, th.panel);
                return;
              }
              const int per_row = std::max(1, in.w / 8);
              int chunk = 1, cells = n;
              int gcols = std::min(cells, per_row);
              int grows = std::max(1, (cells + gcols - 1) / gcols);
              while (grows > in.h && chunk < n) {
                chunk *= 2;
                cells = (n + chunk - 1) / chunk;
                gcols = std::min(cells, per_row);
                grows = std::max(1, (cells + gcols - 1) / gcols);
              }
              if (gcols < 1) gcols = 1;
              int cw = std::max(2, std::min(8, in.w / gcols));
              for (int ci = 0; ci < cells; ++ci) {
                const int gx = ci % gcols, gy = ci / gcols;
                const int x = in.x + gx * cw, y = in.y + gy;
                if (x >= in.x + in.w || y >= in.y + in.h) break;
                const int base = ci * chunk;
                const int end = std::min(n, base + chunk);
                double u = 0.0;
                for (int k = base; k < end; ++k) u += s.cores[static_cast<std::size_t>(k)].util;
                u /= static_cast<double>(std::max(1, end - base));
                const std::string label =
                    chunk == 1 ? std::to_string(s.cores[static_cast<std::size_t>(base)].id) : "*";
                vs_core_cell(c, x, y, std::min(cw, in.x + in.w - x), std::clamp(u / 100.0, 0.0, 1.0),
                             label, th);
              }
            });
    } else if (idx == 2) {
      auto ma = st.h_mem_app.tail(win);
      auto mk = st.h_mem_kern.tail(win);
      auto mc = st.h_mem_cache.tail(win);
      auto mf = st.h_mem_free.tail(win);
      std::vector<std::vector<double>> ser{ma, mk, mc, mf};
      const double total = static_cast<double>(std::max<uint64_t>(s.mem.total, 1));
      const double hi = std::max(total, vs_peak(ser));
      panel(cv, r, "MEMORY", "used " + fmt.bytes(static_cast<double>(s.mem.used)), th,
            [&](Canvas& c, Rect in) {
              if (in.h < 1) return;
              const bool leg = in.h >= 3;
              Rect chart{in.x, in.y, in.w, leg ? in.h - 1 : in.h};
              c.stacked_area(chart, ser,
                             {vs_sc(th, 0), vs_sc(th, 1), vs_sc(th, 2), vs_sc(th, 3)}, 0.0, hi);
              chart_labels(c, chart, fmt.bytes_short(hi), "0",
                           "cache " + fmt.bytes_short(mc.empty() ? 0.0 : mc.back()), th);
              if (leg)
                vs_legend(c, Rect{in.x, in.y + in.h - 1, in.w, 1},
                          {{"App " + fmt.bytes_short(ma.empty() ? 0.0 : ma.back()), vs_sc(th, 0)},
                           {"Kern " + fmt.bytes_short(mk.empty() ? 0.0 : mk.back()), vs_sc(th, 1)},
                           {"Cache " + fmt.bytes_short(mc.empty() ? 0.0 : mc.back()), vs_sc(th, 2)},
                           {"Free " + fmt.bytes_short(mf.empty() ? 0.0 : mf.back()), vs_sc(th, 3)}},
                          th);
            });
    } else if (idx == 3) {
      auto pw = st.h_pwr_total.tail(win);
      auto bw = st.h_batt_wh.tail(win);
      const double peak = std::max(vs_peak({pw}), 1.0);
      const double hi = peak * 1.15;
      const std::string right =
          s.power.present ? fmt.pct(s.power.capacity) + " " + fmt.volts(s.power.volts)
                          : "AC " + fmt.watts(s.power.total_watts);
      panel(cv, r, "POWER", right, th, [&](Canvas& c, Rect in) {
        if (in.h < 1) return;
        const bool leg = in.h >= 3;
        Rect chart{in.x, in.y, in.w, leg ? in.h - 1 : in.h};
        c.braille_chart(chart, {pw}, {th.orange}, 0.0, hi, true);
        if (s.power.present && !bw.empty()) {
          const double bmax = vs_peak({bw});
          if (bmax > 0) c.braille_chart(chart, {bw}, {th.blue}, 0.0, bmax * 1.1, false);
        }
        chart_labels(c, chart, fmt.watts(peak), "0 W", "avg " + fmt.watts(vs_avg(pw)), th);
        if (!leg) return;
        std::vector<std::pair<std::string, RGB>> items{
            {"W " + fmt.watts(pw.empty() ? 0.0 : pw.back()), th.orange}};
        if (s.power.present)
          items.push_back({"Batt " + fmt.watt_hours(bw.empty() ? s.power.wh_now : bw.back()), th.blue});
        const std::string bt = s.power.present ? ("BAT " + vs_pct0(s.power.capacity)) : "AC";
        const int btw = text_width(bt) + 2;
        vs_legend(c, Rect{in.x, in.y + in.h - 1, std::max(0, in.w - btw - 1), 1}, items, th);
        if (in.w > btw + 8) {
          RGB bg = th.green;
          if (s.power.present && s.power.capacity < 20) bg = th.red;
          else if (s.power.present && s.power.capacity < 40) bg = th.yellow;
          badge(c, in.x + in.w - btw, in.y + in.h - 1, bt, th.bg, bg);
        }
      });
    } else if (idx == 4) {
      auto rx = st.h_net_rx.tail(win);
      auto tx = st.h_net_tx.tail(win);
      const double peak = vs_peak({rx, tx});
      const double hi = std::max(1024.0, peak * 1.15);
      panel(cv, r, "NET RX/TX", "R " + fmt.rate_short(rx.empty() ? 0.0 : rx.back()) + " T " +
                                     fmt.rate_short(tx.empty() ? 0.0 : tx.back()),
            th, [&](Canvas& c, Rect in) {
              if (in.h < 1) return;
              const int legrows = in.h >= 6 ? 2 : (in.h >= 3 ? 1 : 0);
              Rect chart{in.x, in.y, in.w, in.h - legrows};
              c.braille_chart(chart, {rx, tx}, {th.cyan, th.magenta}, 0.0, hi, false);
              chart_labels(c, chart, fmt.rate_short(hi), "0", "avg " + fmt.rate_short(vs_avg(rx)), th);
              if (legrows > 0)
                vs_legend(c, Rect{in.x, in.y + in.h - legrows, in.w, legrows},
                          {{"RX " + fmt.rate_short(rx.empty() ? 0.0 : rx.back()), th.cyan},
                           {"TX " + fmt.rate_short(tx.empty() ? 0.0 : tx.back()), th.magenta},
                           {"sRX " + fmt.bytes(static_cast<double>(s.net.session_rx)), th.blue},
                           {"sTX " + fmt.bytes(static_cast<double>(s.net.session_tx)), th.orange}},
                          th);
            });
    } else {
      auto dr = st.h_disk_r.tail(win);
      auto dw = st.h_disk_w.tail(win);
      const double peak = vs_peak({dr, dw});
      const double hi = std::max(128.0 * 1024.0, peak * 1.15);
      panel(cv, r, "DISK + TOP CPU",
            "R " + fmt.rate_short(dr.empty() ? 0.0 : dr.back()) + " W " +
                fmt.rate_short(dw.empty() ? 0.0 : dw.back()),
            th, [&](Canvas& c, Rect in) {
              int prows = std::min<int>(4, std::max(0, in.h - 3));
              int ch = in.h - prows;
              if (ch < 2) {
                ch = std::min(in.h, 3);
                prows = std::max(0, in.h - ch);
              }
              Rect chart{in.x, in.y, in.w, ch};
              c.braille_chart(chart, {dr, dw}, {vs_sc(th, 0), vs_sc(th, 1)}, 0.0, hi, false);
              chart_labels(c, chart, fmt.rate_short(hi), "0",
                           "avg " + fmt.rate_short(vs_avg(dr)), th);
              if (prows <= 0) return;
              auto top = vs_top_cpu(s, static_cast<std::size_t>(prows));
              double smax = 0.0;
              for (const ProcInfo* p : top)
                for (double v : p->spark) smax = std::max(smax, v);
              if (smax <= 0) smax = 1.0;
              int y = in.y + chart.h;
              const int namew = std::min(14, std::max(6, in.w / 3));
              for (const ProcInfo* p : top) {
                if (y >= in.y + in.h) break;
                c.text_clipped(in.x, y, namew, p->name, th.fg, th.panel);
                const int cx = in.x + namew + 1;
                c.text_clipped(cx, y, 6, pad_left(fmt.pct(p->cpu_pct), 6), th.accent, th.panel);
                const int sx = cx + 7;
                const int sw = in.x + in.w - sx;
                if (sw > 1)
                  c.sparkline(sx, y, sw, p->spark, 0.0, smax, heat_color(th, p->cpu_pct / 100.0));
                ++y;
              }
            });
    }
  }
}
// page 2 - battery, wattage breakdown, sensors
inline void view_power(Canvas& cv, const Rect& area, const Snapshot& s, const AppState& st,
                       const Theme& th, const UnitFormatter& fmt) {
  using namespace detail;
  if (area.w < 24 || area.h < 8) return;
  const std::size_t win = st.window_samples();
  Rect bat, watt, brk, tmp;
  if (area.w >= 76) {
    int topH = std::max(8, area.h * 55 / 100);
    topH = std::min(topH, area.h - 4);
    const Rect r1{area.x, area.y, area.w, topH};
    const Rect r2{area.x, area.y + topH, area.w, area.h - topH};
    bat = vs_cell(r1, 2, 1, 0);
    watt = vs_cell(r1, 2, 1, 1);
    brk = vs_cell(r2, 2, 1, 0);
    tmp = vs_cell(r2, 2, 1, 1);
  } else {
    const int hrow = std::max(4, area.h / 4);
    int used = 0;
    auto slice = [&](int want) {
      Rect r{area.x, area.y + used, area.w, 0};
      r.h = std::max(0, std::min(want, area.h - used));
      used += r.h;
      return r;
    };
    bat = slice(hrow);
    watt = slice(hrow);
    brk = slice(hrow);
    tmp = slice(area.h - used);
  }

  panel(cv, bat, "BATTERY", s.power.present ? fmt.pct(s.power.capacity) + " cap" : "AC", th,
        [&](Canvas& c, Rect in) {
          int y = in.y;
          if (!s.power.present) {
            c.text_clipped(in.x, y, in.w, "AC / no battery", th.yellow, th.panel);
            ++y;
            if (y < in.y + in.h) {
              c.text_clipped(in.x, y, in.w, "system draw " + fmt.watts(s.power.total_watts), th.fg,
                             th.panel);
              ++y;
            }
            if (y < in.y + in.h)
              c.text_clipped(in.x, y, in.w,
                             std::to_string(s.power.sensors.size()) + " power sensors tracked", th.dim,
                             th.panel);
            return;
          }
          const std::string health = "HEALTH " + fmt.pct(s.power.health);
          const int hw = text_width(health) + 2;
          if (y < in.y + in.h) {
            c.text_clipped(in.x, y, std::max(0, in.w - hw - 1), "model " + s.power.model, th.fg, th.panel);
            if (in.w > hw + 6) {
              const RGB hc = s.power.health < 60 ? th.red : (s.power.health < 80 ? th.yellow : th.green);
              badge(c, in.x + in.w - hw, y, health, th.bg, hc);
            }
            ++y;
          }
          if (y < in.y + in.h) {
            c.text_clipped(in.x, y, in.w, s.power.status + " | " + s.power.technology, th.dim, th.panel);
            ++y;
          }
          const int lw = std::min(8, std::max(5, in.w / 6));
          const int colw = std::max(0, in.w / 2);
          auto pair2 = [&](const char* k1, const std::string& v1, const char* k2,
                           const std::string& v2) {
            if (y >= in.y + in.h) return;
            c.text_clipped(in.x, y, lw, k1, th.dim, th.panel);
            c.text_clipped(in.x + lw, y, std::max(0, colw - lw), v1, th.fg, th.panel);
            if (!v2.empty() && colw < in.w) {
              c.text_clipped(in.x + colw, y, lw, k2, th.dim, th.panel);
              c.text_clipped(in.x + colw + lw, y, std::max(0, in.w - colw - lw), v2, th.fg, th.panel);
            }
            ++y;
          };
          pair2("volts", fmt.volts(s.power.volts), "amps", fmt.amps(s.power.amps));
          pair2("watts", fmt.watts(s.power.watts), "temp", fmt.temp(s.power.temp_c));
          pair2("now", fmt.watt_hours(s.power.wh_now), "full", fmt.watt_hours(s.power.wh_full));
          pair2("design", fmt.watt_hours(s.power.wh_design), "cycles",
                UnitFormatter::fixed(s.power.cycles, 0));
          auto gauge = [&](const char* k, double frac, RGB col) {
            if (y >= in.y + in.h) return;
            c.text_clipped(in.x, y, 5, k, th.dim, th.panel);
            if (in.w > 6) c.hbar(in.x + 5, y, in.w - 5, frac, col, th.border, th.panel);
            ++y;
          };
          gauge("cap", std::clamp(s.power.capacity / 100.0, 0.0, 1.0),
                heat_color(th, static_cast<double>(s.power.capacity) / 100.0));
          gauge("hp", std::clamp(s.power.health / 100.0, 0.0, 1.0),
                s.power.health < 60 ? th.red : th.green);
        });

  {
    auto rapl = st.h_pwr_rapl.tail(win);
    auto gpu = st.h_pwr_gpu.tail(win);
    auto batt = st.h_pwr_batt.tail(win);
    auto tot = st.h_pwr_total.tail(win);
    std::vector<std::vector<double>> ser{rapl, gpu, batt, tot};
    const double peak = vs_peak(ser);
    const double hi = std::max(1.0, peak * 1.15);
    panel(cv, watt, "WATTAGE", fmt.watts(s.power.total_watts), th, [&](Canvas& c, Rect in) {
      if (in.h < 1) return;
      int legrows = in.h >= 8 ? 2 : (in.h >= 4 ? 1 : 0);
      Rect chart{in.x, in.y, in.w, in.h - legrows};
      c.braille_chart(chart, ser, {vs_sc(th, 0), vs_sc(th, 1), vs_sc(th, 2), th.orange}, 0.0, hi,
                      false);
      chart_labels(c, chart, fmt.watts(peak), "0 W", "avg " + fmt.watts(vs_avg(tot)), th);
      if (legrows > 0)
        vs_legend(c, Rect{in.x, in.y + in.h - legrows, in.w, legrows},
                  {{"RAPL " + fmt.watts(rapl.empty() ? 0.0 : rapl.back()), vs_sc(th, 0)},
                   {"GPU " + fmt.watts(gpu.empty() ? 0.0 : gpu.back()), vs_sc(th, 1)},
                   {"Batt " + fmt.watts(batt.empty() ? 0.0 : batt.back()), vs_sc(th, 2)},
                   {"Total " + fmt.watts(tot.empty() ? 0.0 : tot.back()), th.orange}},
                  th);
    });
  }

  panel(cv, brk, "POWER BREAKDOWN", std::string("sensors ") + std::to_string(s.power.sensors.size()),
        th, [&](Canvas& c, Rect in) {
          if (s.power.sensors.empty()) {
            c.text_clipped(in.x, in.y, in.w, "no power sensors reported", th.dim, th.panel);
            return;
          }
          double sum = 0.0;
          for (const auto& ps : s.power.sensors) sum += std::max(0.0, ps.watts);
          const double total = std::max({sum, s.power.total_watts, 0.001});
          int y = in.y;
          int i = 0;
          for (const auto& ps : s.power.sensors) {
            if (y >= in.y + in.h) break;
            const int namew = std::min(14, std::max(6, in.w / 3));
            const int vw = std::min(10, std::max(6, in.w / 5));
            c.text_clipped(in.x, y, namew, ps.name, th.fg, th.panel);
            const int bx = in.x + namew + 1;
            const int bw = in.w - namew - vw - 2;
            if (bw > 1) {
              const RGB col = ps.battery ? th.green : vs_sc(th, static_cast<std::size_t>(i));
              c.hbar(bx, y, bw, ps.watts / total, col, th.border, th.panel);
            }
            const std::string v = fmt.watts(ps.watts);
            c.text_clipped(in.x + in.w - vw, y, vw, pad_left(v, vw),
                           ps.battery ? th.green : th.fg, th.panel);
            ++y;
            ++i;
          }
        });

  panel(cv, tmp, "THERMALS",
        s.power.temps.empty() ? "none" : (std::to_string(s.power.temps.size()) + " sensors"), th,
        [&](Canvas& c, Rect in) {
          if (s.power.temps.empty()) {
            c.text_clipped(in.x, in.y, in.w, "no thermal sensors", th.dim, th.panel);
            if (in.h > 1 && s.power.present)
              c.text_clipped(in.x, in.y + 1, in.w, "battery " + fmt.temp(s.power.temp_c), th.fg,
                             th.panel);
            return;
          }
          int y = in.y;
          for (const auto& ts : s.power.temps) {
            if (y >= in.y + in.h) break;
            const int namew = std::min(16, std::max(6, in.w / 3));
            const int vw = std::min(9, std::max(6, in.w / 5));
            const double frac = std::clamp(ts.c / 100.0, 0.0, 1.0);
            const RGB col = heat_color(th, frac);
            c.text_clipped(in.x, y, namew, ts.name, th.fg, th.panel);
            const int bx = in.x + namew + 1;
            const int bw = in.w - namew - vw - 2;
            if (bw > 1) c.hbar(bx, y, bw, frac, col, th.border, th.panel);
            const std::string v = fmt.temp(ts.c);
            c.text_clipped(in.x + in.w - vw, y, vw, pad_left(v, vw), col, th.panel);
            ++y;
          }
        });
}
// page 3 - interrupts, kernel threads, scheduler, PSI, slab
inline void view_kernel(Canvas& cv, const Rect& area, const Snapshot& s, const AppState& st,
                        const Theme& th, const UnitFormatter& fmt) {
  using namespace detail;
  if (area.w < 24 || area.h < 6) return;
  const std::size_t win = st.window_samples();
  const int cols = area.w >= 76 ? 2 : 1;
  const int rows = area.h >= 17 ? 3 : (area.h >= 10 ? 2 : 1);

  for (int idx = 0; idx < 6; ++idx) {
    const Rect r = vs_cell(area, cols, rows, idx);
    if (r.w < 8 || r.h < 3) continue;

    if (idx == 0) {
      auto irq = st.h_irq.tail(win);
      const double peak = std::max(vs_peak({irq}), 1.0);
      const double hi = peak * 1.15;
      panel(cv, r, "IRQ/S", fmt.num(s.irq.total_rate, 0) + "/s", th, [&](Canvas& c, Rect in) {
        if (in.h < 1) return;
        const bool strip = !s.irq.core_rate.empty() && in.h >= 4;
        Rect chart{in.x, in.y, in.w, strip ? in.h - 1 : in.h};
        c.braille_chart(chart, {irq}, {th.magenta}, 0.0, hi, true);
        chart_labels(c, chart, fmt.num(peak, 0) + "/s", "0", "avg " + fmt.num(vs_avg(irq), 0), th);
        if (!strip) return;
        const int n = static_cast<int>(s.irq.core_rate.size());
        double mx = 0.0;
        for (double v : s.irq.core_rate) mx = std::max(mx, v);
        const int cellw = std::max(1, in.w / std::max(1, n));
        const int y = in.y + in.h - 1;
        for (int i = 0; i < n; ++i) {
          const int x = in.x + i * cellw;
          if (x >= in.x + in.w) break;
          const double frac = mx > 0 ? s.irq.core_rate[static_cast<std::size_t>(i)] / mx : 0.0;
          c.heat(x, y, std::clamp(frac, 0.0, 1.0), th);
        }
      });
    } else if (idx == 1) {
      panel(cv, r, "TOP IRQ", std::to_string(s.irq.names.size()) + " lines", th,
            [&](Canvas& c, Rect in) {
              const int n = static_cast<int>(std::min(s.irq.names.size(), s.irq.rates.size()));
              if (n == 0) {
                c.text_clipped(in.x, in.y, in.w, "no interrupt data", th.dim, th.panel);
                return;
              }
              std::vector<int> order(static_cast<std::size_t>(n));
              for (int i = 0; i < n; ++i) order[static_cast<std::size_t>(i)] = i;
              std::sort(order.begin(), order.end(), [&](int a, int b) {
                return s.irq.rates[static_cast<std::size_t>(a)] >
                       s.irq.rates[static_cast<std::size_t>(b)];
              });
              const double mx = std::max(1.0, s.irq.rates[static_cast<std::size_t>(order[0])]);
              const int namew = std::min(22, std::max(8, in.w / 2));
              const int vw = std::min(9, std::max(6, in.w / 5));
              int y = in.y;
              for (int k = 0; k < n && y < in.y + in.h && k < 10; ++k, ++y) {
                const int i = order[static_cast<std::size_t>(k)];
                const std::string& nm = s.irq.names[static_cast<std::size_t>(i)];
                const double rate = s.irq.rates[static_cast<std::size_t>(i)];
                c.text_clipped(in.x, y, namew, nm, th.fg, th.panel);
                const int bx = in.x + namew + 1;
                const int bw = in.w - namew - vw - 2;
                if (bw > 1)
                  c.hbar(bx, y, bw, rate / mx, vs_sc(th, static_cast<std::size_t>(k)),
                         th.border, th.panel);
                const std::string v = fmt.num(rate, 0) + "/s";
                c.text_clipped(in.x + in.w - vw, y, vw, pad_left(v, vw), th.accent, th.panel);
              }
            });
    } else if (idx == 2) {
      panel(cv, r, "KERNEL TASKS", std::to_string(s.procs.size()) + " procs", th,
            [&](Canvas& c, Rect in) {
              if (in.h < 2) return;
              std::vector<const ProcInfo*> kt;
              for (const auto& p : s.procs)
                if (vs_kernel_task(p)) kt.push_back(&p);
              std::sort(kt.begin(), kt.end(), [](const ProcInfo* a, const ProcInfo* b) {
                return a->cpu_pct > b->cpu_pct;
              });
              if (kt.empty()) {
                c.text_clipped(in.x, in.y, in.w, "no kernel tasks in snapshot", th.dim, th.panel);
                return;
              }
              int namew = std::max(6, in.w - 32);
              std::vector<Col> cols;
              cols.push_back(Col{"PID", 6, false});
              cols.push_back(Col{"NAME", namew, false});
              cols.push_back(Col{"AGE DD:HH:MM:SS", 12, true});
              cols.push_back(Col{"CPU%", 6, true});
              cols.push_back(Col{"THR", 4, true});
              while (!cols.empty()) {
                int total = 0;
                for (const auto& col : cols) total += col.w + 1;
                if (total <= in.w) break;
                cols.pop_back();
              }
              if (cols.size() < 2) return;
              std::vector<std::vector<std::string>> rws;
              const std::size_t cap = in.h >= 2 ? static_cast<std::size_t>(in.h - 2) : 0;
              for (std::size_t i = 0; i < kt.size() && i < cap; ++i) {
                const ProcInfo* p = kt[i];
                rws.push_back({std::to_string(p->pid), p->name,
                               UnitFormatter::dur_clock(p->age_s), fmt.pct(p->cpu_pct),
                               std::to_string(p->threads)});
              }
              draw_table(c, in, cols, rws, th);
            });
    } else if (idx == 3) {
      auto ctx = st.h_ctx.tail(win);
      panel(cv, r, "SCHEDULER", "load " + fmt.fixed(s.kern.load1, 2), th, [&](Canvas& c, Rect in) {
        int y = in.y;
        const int colw = in.w >= 40 ? in.w / 2 : in.w;
        const int lw = 8;
        auto line2 = [&](const char* k1, const std::string& v1, const char* k2,
                         const std::string& v2) {
          if (y >= in.y + in.h) return;
          c.text_clipped(in.x, y, lw, k1, th.dim, th.panel);
          c.text_clipped(in.x + lw, y, std::max(0, colw - lw), v1, th.fg, th.panel);
          if (colw < in.w && !v2.empty()) {
            c.text_clipped(in.x + colw, y, lw, k2, th.dim, th.panel);
            c.text_clipped(in.x + colw + lw, y, std::max(0, in.w - colw - lw), v2, th.blue, th.panel);
          }
          ++y;
        };
        line2("ctx/s", fmt.num(s.kern.ctx_rate, 0), "ctx tot",
              fmt.num(static_cast<double>(s.kern.ctx_total), 0));
        line2("forks/s", fmt.num(s.kern.forks_rate, 0), "threads", std::to_string(s.kern.threads));
        line2("running", fmt.num(s.kern.procs_running, 0), "blocked",
              fmt.num(s.kern.procs_blocked, 0));
        line2("load5", fmt.fixed(s.kern.load5, 2), "load15", fmt.fixed(s.kern.load15, 2));
        if (y < in.y + in.h) {
          const double mx = std::max(vs_peak({ctx}), 1.0);
          c.sparkline(in.x, y, in.w, ctx, 0.0, mx, th.accent);
          ++y;
        }
        for (const auto& ln : s.kern.sched_lines) {
          if (y >= in.y + in.h) break;
          c.text_clipped(in.x, y, in.w, ln, th.dim, th.panel);
          ++y;
        }
      });
    } else if (idx == 4) {
      panel(cv, r, "PRESSURE (PSI)", "avg10", th, [&](Canvas& c, Rect in) {
        struct PRow {
          const char* n;
          double v;
          RGB col;
        };
        const PRow prs[3] = {{"CPU", s.kern.psi_cpu, th.blue},
                             {"IO", s.kern.psi_io, th.orange},
                             {"MEM", s.kern.psi_mem, th.magenta}};
        int y = in.y;
        double worst = 0.0;
        for (const auto& pr : prs) {
          if (y >= in.y + in.h) break;
          const int labw = 4;
          const int vw = std::min(9, std::max(6, in.w / 5));
          const double frac = std::clamp(pr.v / 100.0, 0.0, 1.0);
          worst = std::max(worst, pr.v);
          c.text_clipped(in.x, y, labw, pr.n, th.fg, th.panel);
          const int bx = in.x + labw;
          const int bw = in.w - labw - vw - 1;
          if (bw > 1) c.hbar(bx, y, bw, frac, heat_color(th, frac), th.border, th.panel);
          const std::string v = fmt.pct(pr.v);
          c.text_clipped(in.x + in.w - vw, y, vw, pad_left(v, vw), pr.col, th.panel);
          ++y;
        }
        if (y < in.y + in.h) {
          const char* tag = worst >= 50 ? "CRITICAL" : (worst >= 20 ? "HIGH" : (worst >= 5 ? "ELEVATED" : "OK"));
          const RGB col = worst >= 50 ? th.red : (worst >= 20 ? th.orange : (worst >= 5 ? th.yellow : th.green));
          badge(c, in.x, y, tag, th.bg, col);
        }
      });
    } else {
      auto slab = st.h_slab.tail(win);
      auto pt = st.h_pt.tail(win);
      const double peak = vs_peak({slab, pt});
      const double hi = std::max(128.0 * 1024.0, peak * 1.15);
      panel(cv, r, "SLAB / PAGETABLES", fmt.bytes(static_cast<double>(s.mem.slab)), th,
            [&](Canvas& c, Rect in) {
              int lists = static_cast<int>(std::min<std::size_t>(s.kern.slab_top.size(), 4));
              int ch = in.h - lists;
              if (ch < 2) {
                ch = std::min(in.h, 2);
                lists = std::max(0, in.h - ch);
              }
              Rect chart{in.x, in.y, in.w, ch};
              c.braille_chart(chart, {slab, pt}, {vs_sc(th, 4), vs_sc(th, 5)}, 0.0, hi, false);
              chart_labels(c, chart, fmt.bytes_short(hi), "0",
                           "avg " + fmt.bytes_short(vs_avg(slab)), th);
              int y = in.y + chart.h;
              for (int i = 0; i < lists && y < in.y + in.h; ++i, ++y) {
                const auto& it = s.kern.slab_top[static_cast<std::size_t>(i)];
                const int vw = std::min(11, std::max(7, in.w / 4));
                c.text_clipped(in.x, y, std::max(0, in.w - vw - 1), it.first, th.fg, th.panel);
                const std::string v = fmt.bytes(static_cast<double>(it.second));
                c.text_clipped(in.x + in.w - vw, y, vw, pad_left(v, vw), th.dim, th.panel);
              }
            });
    }
  }
}

}  // namespace ct
