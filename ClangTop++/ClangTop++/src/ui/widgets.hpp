// ClangTop++ - shared TUI widgets, chrome, overlays and view state
#pragma once
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../collectors/kernel_catalog.hpp"
#include "../collectors/kernel_lab.hpp"
#include "../config.hpp"
#include "../core/metrics_types.hpp"
#include "../core/ring_buffer.hpp"
#include "../core/unit_formatter.hpp"
#include "canvas.hpp"
#include "theme.hpp"

namespace ct {

inline const char* const kTabs[8] = {"Overview", "Power", "Kernel/IRQ", "CPU", "Memory", "Net/IO", "FS/Code", "Kernel Lab"};

enum SortMode : int { kSortCpu = 0, kSortMem, kSortPid, kSortName, kSortDisk, kSortAge, kSortCount };
inline const char* sort_name(int m) {
  switch (m) {
    case kSortCpu: return "CPU%";
    case kSortMem: return "MEM";
    case kSortPid: return "PID";
    case kSortName: return "NAME";
    case kSortDisk: return "DISK";
    case kSortAge: return "AGE";
    default: return "CPU%";
  }
}

// ---------------------------------------------------------------- state
struct AppState {
  Settings cfg;
  int page = 0;
  bool paused = false;
  bool show_help = false;
  bool show_settings = false;
  bool show_filter = false;
  int settings_row = 0;
  bool editing_scan = false;
  int sort_mode = kSortCpu;
  std::string filter;
  std::string toast;
  double toast_until = 0;
  double now = 0;

  // Kernel Lab (tab 8)
  std::shared_ptr<KernelLab> kernel_lab = std::make_shared<KernelLab>();
  bool kernel_left_catalog = false;  // false: installed kernels, true: global catalog
  int kernel_installed_sel = 0;
  int kernel_catalog_sel = 0;
  int kernel_sysctl_sel = 0;
  int kernel_sysctl_scroll = 0;
  bool kernel_sysctl_filter_mode = false;
  std::string kernel_sysctl_filter;
  std::vector<std::string> kernel_snapshot_names;
  int kernel_snapshot_sel = 0;
  int kernel_diff_stage = 0;  // 0 none, 1 pick A, 2 pick B
  std::string kernel_diff_a;
  KernelDiff kernel_diff_result;
  bool kernel_diff_has = false;
  double kernel_last_refresh = 0.0;

  RingBuffer<600> h_cpu_app, h_cpu_kern, h_cpu_io, h_cpu_irq;
  std::vector<RingBuffer<600>> h_cores;
  std::vector<RingBuffer<600>> h_core_mhz;
  RingBuffer<600> h_mem_app, h_mem_kern, h_mem_cache, h_mem_swap, h_mem_free;
  RingBuffer<600> h_pwr_total, h_batt_v, h_batt_wh;
  RingBuffer<600> h_pwr_rapl, h_pwr_gpu, h_pwr_batt;
  RingBuffer<600> h_net_rx, h_net_tx, h_net_rx_total, h_net_tx_total;
  RingBuffer<600> h_disk_r, h_disk_w;
  RingBuffer<600> h_irq, h_ctx, h_psi_cpu, h_psi_io, h_psi_mem;
  RingBuffer<600> h_slab, h_pt, h_load;

  std::size_t window_samples() const {
    int n = 120000 / std::max(250, cfg.refresh_ms);
    if (n < 8) n = 8;
    if (n > 600) n = 600;
    return static_cast<std::size_t>(n);
  }

  void note(const std::string& msg, double ts) {
    toast = msg;
    toast_until = ts + 2.5;
  }

  void on_sample(const Snapshot& s) {
    now = s.ts;
    h_cpu_app.push(s.cpu.app);
    h_cpu_kern.push(s.cpu.kern);
    h_cpu_io.push(s.cpu.io);
    h_cpu_irq.push(s.cpu.irq);
    if (h_cores.size() < s.cores.size()) h_cores.resize(s.cores.size());
    if (h_core_mhz.size() < s.cores.size()) h_core_mhz.resize(s.cores.size());
    for (std::size_t i = 0; i < s.cores.size(); ++i) {
      h_cores[i].push(s.cores[i].util);
      h_core_mhz[i].push(s.cores[i].mhz);
    }
    h_mem_app.push(static_cast<double>(s.mem.total > s.mem.free + s.mem.cached + s.mem.buffers
                                           ? s.mem.total - s.mem.free - s.mem.cached - s.mem.buffers
                                           : 0));
    h_mem_kern.push(static_cast<double>(s.mem.slab + s.mem.pagetables));
    h_mem_cache.push(static_cast<double>(s.mem.cached + s.mem.buffers));
    h_mem_swap.push(static_cast<double>(s.mem.swap_used));
    h_mem_free.push(static_cast<double>(s.mem.free));
    h_pwr_total.push(s.power.total_watts);
    h_batt_v.push(s.power.volts);
    h_batt_wh.push(s.power.wh_now);
    for (const auto& sensor : s.power.sensors) {
      if (sensor.name == "RAPL") h_pwr_rapl.push(sensor.watts);
      else if (sensor.name.find("GPU") != std::string::npos) h_pwr_gpu.push(sensor.watts);
      else if (sensor.battery) h_pwr_batt.push(sensor.watts);
    }
    h_net_rx.push(s.net.rx_bps);
    h_net_tx.push(s.net.tx_bps);
    h_net_rx_total.push(static_cast<double>(s.net.session_rx));
    h_net_tx_total.push(static_cast<double>(s.net.session_tx));
    h_disk_r.push(s.disk.r_bps);
    h_disk_w.push(s.disk.w_bps);
    h_irq.push(s.irq.total_rate);
    h_ctx.push(s.kern.ctx_rate);
    h_psi_cpu.push(s.kern.psi_cpu);
    h_psi_io.push(s.kern.psi_io);
    h_psi_mem.push(s.kern.psi_mem);
    h_slab.push(static_cast<double>(s.mem.slab));
    h_pt.push(static_cast<double>(s.mem.pagetables));
    h_load.push(s.kern.load1);
  }
};

// ---------------------------------------------------------------- helpers
inline std::string pad_right(const std::string& s, int w) {
  std::string out = s;
  if (w <= 0) return out;
  int len = text_width(out);
  if (len > w) {
    // trim by codepoint
    std::string cut;
    std::size_t i = 0;
    int n = 0;
    while (i < out.size() && n < w) {
      std::size_t before = i;
      uint32_t cp = utf8_next(out, i);
      if (i >= out.size() && n == w - 1) {
        utf8_append(cut, 0x2026);
        break;
      }
      if (cp == 0) break;
      utf8_append(cut, cp);
      ++n;
      if (i == before) break;
    }
    return cut;
  }
  out.append(static_cast<std::size_t>(w - len), ' ');
  return out;
}

inline std::string pad_left(const std::string& s, int w) {
  int len = text_width(s);
  if (len >= w) return pad_right(s, w);
  std::string out(static_cast<std::size_t>(w - len), ' ');
  out += s;
  return out;
}

inline std::string lower_copy(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

inline std::vector<const ProcInfo*> select_procs(const Snapshot& s, const AppState& st, std::size_t limit) {
  std::vector<const ProcInfo*> out;
  std::string f = lower_copy(st.filter);
  for (const auto& p : s.procs) {
    if (!f.empty() && lower_copy(p.name).find(f) == std::string::npos) continue;
    out.push_back(&p);
  }
  auto cmp = [&](const ProcInfo* a, const ProcInfo* b) {
    switch (st.sort_mode) {
      case kSortMem: return a->rss > b->rss;
      case kSortPid: return a->pid < b->pid;
      case kSortName: return a->name < b->name;
      case kSortDisk: return (a->rd_bps + a->wr_bps) > (b->rd_bps + b->wr_bps);
      case kSortAge: return a->age_s > b->age_s;
      default: return a->cpu_pct > b->cpu_pct;
    }
  };
  std::sort(out.begin(), out.end(), cmp);
  if (out.size() > limit) out.resize(limit);
  return out;
}

// ---------------------------------------------------------------- chrome
inline void panel(Canvas& cv, const Rect& r, const std::string& title, const std::string& right,
                  const Theme& th, const std::function<void(Canvas&, Rect)>& body) {
  if (r.w < 4 || r.h < 3) return;
  cv.fill(r, ' ', th.fg, th.panel);
  cv.box(r, title, right, th.border, th.panel, th.accent, th.dim);
  Rect inner = r.inset(1);
  body(cv, inner);
}

inline void badge(Canvas& cv, int x, int y, const std::string& text, RGB fg, RGB bg) {
  cv.text(x, y, " " + text + " ", fg, bg);
}

inline void kv(Canvas& cv, int x, int y, int label_w, const std::string& k, const std::string& v, const Theme& th,
               RGB vfg) {
  cv.text(x, y, pad_right(k, label_w), th.dim, th.panel);
  cv.text(x + label_w, y, v, vfg, th.panel);
}

inline void draw_header(Canvas& cv, const Snapshot& s, const AppState& st, const Theme& th,
                        const UnitFormatter& fmt) {
  int fw = cv.width();
  int w = std::max(1, fw - 1);  // clamp to term_cols-1 to avoid right-edge wrap/clipping
  cv.fill(Rect{0, 0, fw, 2}, ' ', th.fg, th.bg);

  // ---- row 0: brand + tabs + rate/window
  int x = 1;
  cv.text(x, 0, kAppName, th.accent, th.bg);
  x += text_width(kAppName) + 1;
  std::string ver = std::string("v") + kVersion;
  cv.text(x, 0, ver, th.dim, th.bg);
  x += text_width(ver) + 1;

  for (int i = 0; i < 8; ++i) {
    std::string label = std::to_string(i + 1) + " " + kTabs[i];
    if (w < 170) {
      static const char* const kShort[8] = {"Ovw", "Pwr", "Krn", "CPU", "Mem", "Net", "FS", "KLab"};
      label = std::to_string(i + 1) + " " + kShort[i];
    }
    bool active = (i == st.page);
    std::string t = " " + label + " ";
    if (x + static_cast<int>(text_width(t)) + 2 >= w - 24) break;
    if (active) {
      badge(cv, x, 0, label, th.bg, th.accent);
      x += static_cast<int>(text_width(t)) + 1;
    } else {
      cv.text(x, 0, t, th.dim, th.bg);
      x += static_cast<int>(text_width(t)) + 1;
    }
  }

  char rate[32];
  std::snprintf(rate, sizeof(rate), "@%gs", st.cfg.refresh_ms / 1000.0);
  std::string right = std::string(rate) + "  " + "2m" + "  " + theme_name(st.cfg.theme);
  if (st.paused) right = "PAUSED  " + right;
  int rx = w - 1 - text_width(right);
  if (rx > x + 2) cv.text(rx, 0, right, st.paused ? th.yellow : th.muted, th.bg);

  // ---- row 1: uname / uptime / battery / power / load
  int y = 1;
  int cx = 1;
  auto cell = [&](const std::string& label, const std::string& value, RGB vfg) {
    std::string lab = label + " ";
    cv.text(cx, y, lab, th.muted, th.bg);
    cx += static_cast<int>(text_width(lab));
    cv.text(cx, y, value, vfg, th.bg);
    cx += static_cast<int>(text_width(value));
    cv.text(cx, y, "  \xE2\x94\x82 ", th.border, th.bg);
    cx += 4;
  };

  std::string kernel = s.sys.uname_sys + " " + s.sys.uname_release;
  if (cx < w - 30) cell("kernel", kernel, th.fg);
  std::string upt = fmt.uptime(s.sys.uptime);
  if (st.cfg.uptime != UptimeFmt::Clock) upt += "  (" + UnitFormatter::uptime_pretty(s.sys.uptime) + ")";
  if (cx < w - 24) cell("up", upt, th.green);
  if (s.power.present) {
    std::string bat = fmt.fixed(s.power.capacity, 0) + "% " + fmt.volts(s.power.volts) + " " +
                      fmt.watt_hours(s.power.wh_now);
    if (cx < w - 18) cell("bat", bat, s.power.capacity < 20 ? th.red : th.green);
  } else {
    if (cx < w - 16) cell("pwr", fmt.watts(s.power.total_watts), th.yellow);
  }
  if (cx < w - 14) cell("total", fmt.watts(s.power.total_watts), th.orange);
  if (cx < w - 12)
    cell("load", fmt.fixed(s.kern.load1, 2) + " " + fmt.fixed(s.kern.load5, 2) + " " + fmt.fixed(s.kern.load15, 2), th.blue);
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%d procs  %d thr", static_cast<int>(s.procs.size()), s.kern.threads);
  int tw = text_width(buf);
  int rxp = w - 1 - tw;
  if (rxp > cx + 1 && rxp + tw <= w) cv.text(rxp, y, buf, th.muted, th.bg);
}

inline void draw_footer(Canvas& cv, const AppState& st, const Theme& th) {
  int fw = cv.width();
  int w = std::max(1, fw - 1);
  int y = cv.height() - 1;
  cv.fill(Rect{0, y, fw, 1}, ' ', th.dim, th.bg);
  std::string keys = (st.page == 7)
      ? "1-8 pages | v catalog | r bench | m save | c diff | / sysctl | Up/Dn browse | , settings | ? help | q"
      : "1-8 pages | u uptime | b units | t theme | s sort | / filter | space pause | , settings | ? help | q";
  if (w < 150) keys = st.page == 7 ? "1-8 v r m c / UpDn , ? q" : "1-8 u b t s / space , ? q";
  if (text_width(keys) > w - 2) keys = st.page == 7 ? "1-8 v r m c / ? q" : "1-8 u b t s / ? q";
  cv.text_clipped(1, y, std::max(0, w - 2), keys, th.muted, th.bg);
  std::string page = "[" + std::to_string(st.page + 1) + "/8 " + kTabs[st.page] + "]";
  int px = w - 1 - text_width(page);
  if (px > text_width(keys) + 3) cv.text(px, y, page, th.accent, th.bg);
  if (!st.toast.empty() && st.now < st.toast_until) {
    int tw = text_width(st.toast);
    int tx = std::max(1, (w - tw) / 2);
    if (tx > std::min(text_width(keys) + 2, w / 3)) badge(cv, tx, y, st.toast, th.bg, th.blue);
  }
}

// ---------------------------------------------------------------- table
struct Col {
  std::string head;
  int w = 8;
  bool right = false;
};

inline void draw_table(Canvas& cv, const Rect& r, const std::vector<Col>& cols,
                       const std::vector<std::vector<std::string>>& rows, const Theme& th,
                       const std::vector<RGB>& row_colors = {}) {
  if (r.w <= 0 || r.h <= 0) return;
  int y = r.y;
  int x = r.x;
  for (const auto& c : cols) {
    cv.text(x, y, c.right ? pad_left(c.head, c.w) : pad_right(c.head, c.w), th.muted, th.panel);
    x += c.w + 1;
  }
  cv.hline(r.x, y, r.w, 0x2500, th.border, th.panel);
  y += 1;
  for (std::size_t i = 0; i < rows.size() && y < r.y + r.h; ++i, ++y) {
    x = r.x;
    RGB fg = (i < row_colors.size()) ? row_colors[i] : th.fg;
    for (std::size_t c = 0; c < cols.size() && c < rows[i].size(); ++c) {
      cv.text_clipped(x, y, cols[c].w, cols[c].right ? pad_left(rows[i][c], cols[c].w) : rows[i][c], fg, th.panel);
      x += cols[c].w + 1;
    }
  }
}

// ---------------------------------------------------------------- overlays
inline void overlay_frame(Canvas& cv, const Theme& th, int w, int h, const std::string& title, Rect& out) {
  int cw = std::min(w - 4, std::max(46, w * 3 / 5));
  int ch = std::min(h - 4, std::max(12, h * 3 / 5));
  Rect r{(w - cw) / 2, (h - ch) / 2, cw, ch};
  cv.fill(r, ' ', th.fg, th.bg);
  cv.box(r, title, "", th.accent, th.bg, th.accent, th.muted);
  out = r.inset(2);
}

inline void draw_help(Canvas& cv, const Theme& th, int w, int h) {
  Rect in;
  overlay_frame(cv, th, w, h, "Help / Keybinds", in);
  struct Row { const char* k; const char* d; };
  static const Row rows[] = {
      {"1..8", "switch page (Overview..Kernel Lab)"},
      {"Tab / Left / Right", "cycle pages"},
      {"u", "cycle uptime format (DD:HH:MM:SS / compact / uptime -p)"},
      {"b", "cycle global unit rounding (Auto/B/KB/MB/GB)"},
      {"t", "cycle theme (Gruvbox/Catppuccin/Nord/Tokyo Night)"},
      {"s", "cycle process sort (CPU/MEM/PID/NAME/DISK/AGE)"},
      {"/", "filter processes (tabs 1-7) / filter sysctl (Kernel Lab)"},
      {"space", "pause / resume sampling"},
      {",", "settings: units, decimals, uptime, refresh, theme, scan path"},
      {"+ / -", "refresh rate 250ms .. 2s"},
      {"r", "rescan code (tabs 1-7) / run kernel benchmark (Kernel Lab)"},
      {"v", "Kernel Lab: toggle installed kernels / kernel catalog"},
      {"m", "Kernel Lab: save active kernel snapshot"},
      {"c", "Kernel Lab: pick/compare saved snapshot diff (Up/Down, Enter)"},
      {"Up/Down", "Kernel Lab browse / diff selection (when no overlay)"},
      {"?", "toggle this help"},
      {"q / Esc", "quit / close overlay"},
  };
  int y = in.y;
  for (const auto& row : rows) {
    if (y >= in.y + in.h) break;
    cv.text(in.x, y, pad_right(row.k, 22), th.accent, th.bg);
    cv.text(in.x + 22, y, row.d, th.fg, th.bg);
    ++y;
  }
  cv.text(in.x, in.y + in.h - 1, "Press any key to close", th.muted, th.bg);
}

inline const char* const kSettingsLabels[6] = {"Unit Rounding", "Decimal Precision", "Uptime Format",
                                               "Refresh Rate", "Theme", "Code Scan Path"};

inline void draw_settings(Canvas& cv, const AppState& st, const Theme& th, const UnitFormatter& fmt, int w, int h) {
  Rect in;
  overlay_frame(cv, th, w, h, "Settings", in);
  std::vector<std::string> values;
  values.push_back(unit_mode_name(st.cfg.unit));
  values.push_back(std::to_string(st.cfg.decimals));
  values.push_back(uptime_fmt_name(st.cfg.uptime));
  values.push_back(std::to_string(st.cfg.refresh_ms) + " ms");
  values.push_back(theme_name(st.cfg.theme));
  values.push_back(st.cfg.scan_path.empty() ? std::string("$HOME") : st.cfg.scan_path);

  int y = in.y;
  for (int i = 0; i < 6; ++i) {
    bool sel = (i == st.settings_row);
    std::string line = " " + pad_right(kSettingsLabels[i], 20) + "  " + values[static_cast<std::size_t>(i)];
    if (sel) {
      cv.fill(Rect{in.x, y, in.w, 1}, ' ', th.fg, th.bg);
      cv.text(in.x, y, ">", th.accent, th.bg);
      cv.text(in.x + 1, y, line, th.fg, th.blue);
    } else {
      cv.text(in.x + 1, y, line, th.dim, th.bg);
    }
    ++y;
  }
  y += 1;
  cv.text(in.x + 1, y, "Enter edit/cycle   <> change   Esc close", th.muted, th.bg);
  y += 1;
  cv.text(in.x + 1, y, "Global unit rounding + decimals apply to all charts/tables.", th.muted, th.bg);
  y += 1;
  if (!st.cfg.scan_path.empty())
    cv.text_clipped(in.x + 1, y, in.w - 2, "scan: " + st.cfg.scan_path, th.green, th.bg);
  else
    cv.text(in.x + 1, y, "scan: $HOME (async std::thread)", th.green, th.bg);
  y += 1;
  cv.text(in.x + 1, y, "Live preview: " + fmt.rate(1234.5678) + "  " + fmt.pct(42.123) + "  " +
                            fmt.watts(12.345) + "  " + fmt.uptime(93784), th.accent, th.bg);
}

inline void draw_filter(Canvas& cv, const AppState& st, const Theme& th, int w, int h) {
  Rect in;
  overlay_frame(cv, th, w, h, "Filter processes", in);
  cv.text(in.x, in.y, "/ " + st.filter + "_", th.fg, th.bg);
  cv.text(in.x, in.y + 2, "Enter apply   Esc clear/cancel   Backspace delete", th.muted, th.bg);
  cv.text(in.x, in.y + 3, "Active filter: " + (st.filter.empty() ? std::string("<none>") : st.filter), th.accent, th.bg);
}

// ---------------------------------------------------------------- chart labels
inline void chart_labels(Canvas& cv, const Rect& r, const std::string& vmax, const std::string& vmin,
                         const std::string& avg, const Theme& th) {
  if (r.h < 1) return;
  cv.text_clipped(r.x, r.y, r.w, vmax, th.muted, th.panel);
  if (r.h > 1) cv.text_clipped(r.x, r.y + r.h - 1, r.w, vmin, th.muted, th.panel);
  if (!avg.empty()) {
    int aw = text_width(avg);
    if (aw + 2 < r.w) cv.text(r.x + r.w - aw, r.y, avg, th.dim, th.panel);
  }
}

}  // namespace ct
