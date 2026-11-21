// ClangTop++ v0.3.0 - modular C++17 TUI system & kernel monitor
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include "collectors/kernel_catalog.hpp"
#include "config.hpp"
#include "core/platform_collector.hpp"
#include "core/unit_formatter.hpp"
#include "ui/canvas.hpp"
#include "ui/terminal.hpp"
#include "ui/theme.hpp"
#include "ui/views_kernel_lab.hpp"
#include "ui/views_resources.hpp"
#include "ui/views_system.hpp"
#include "ui/widgets.hpp"

namespace {

using namespace ct;

struct Options {
  bool version = false;
  bool help = false;
  bool render_once = false;
  bool render_all = false;
  int frames = 0;
  int tab = 0;
  int width = 200;
  int height = 50;
  int refresh_ms = -1;
};

void usage() {
  std::printf(
      "ClangTop++ v0.3.0 - high-density C++17 TUI system & kernel monitor\n\n"
      "Usage: ClangTop++ [options]\n"
      "  --version            print version and exit\n"
      "  --help               this text\n"
      "  --render-once[=WxH]  headless: render one full frame to stdout and exit\n"
      "  --render-all[=WxH]   headless: render all 8 pages (separated by markers)\n"
      "  --frames N           headless: collect N samples then render once\n"
      "  --tab N              start on page N (0..7)\n"
      "  --refresh MS         refresh rate override (250..2000)\n\n"
      "Keys: 1-8 pages | u uptime | b units | t theme | s sort | / filter/sysctl |\n"
      "      space pause | , settings | ? help | q quit | Kernel Lab: v r m c\n");
}

bool parse_size(const char* s, int& w, int& h) {
  int a = 0, b = 0;
  if (!s) return false;
  if (std::sscanf(s, "%dx%d", &a, &b) == 2 && a >= 40 && b >= 12) {
    w = a;
    h = b;
    return true;
  }
  return false;
}

Options parse_args(int argc, char** argv) {
  Options o;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--version" || a == "-v") o.version = true;
    else if (a == "--help" || a == "-h") o.help = true;
    else if (a.rfind("--render-once", 0) == 0) {
      o.render_once = true;
      auto eq = a.find('=');
      if (eq != std::string::npos) parse_size(a.c_str() + eq + 1, o.width, o.height);
    } else if (a.rfind("--render-all", 0) == 0) {
      o.render_all = true;
      o.render_once = true;
      auto eq = a.find('=');
      if (eq != std::string::npos) parse_size(a.c_str() + eq + 1, o.width, o.height);
    } else if (a == "--headless") {
      o.render_once = true;
    } else if (a == "--frames" && i + 1 < argc) {
      o.frames = std::atoi(argv[++i]);
    } else if (a == "--tab" && i + 1 < argc) {
      o.tab = std::atoi(argv[++i]);
    } else if (a == "--refresh" && i + 1 < argc) {
      o.refresh_ms = std::atoi(argv[++i]);
    } else if (a.rfind("--", 0) == 0) {
      std::fprintf(stderr, "unknown option: %s\n", a.c_str());
      o.help = true;
    }
  }
  if (o.tab < 0) o.tab = 0;
  if (o.tab > 7) o.tab = 7;
  return o;
}

void cycle_unit(Settings& c) {
  int u = (static_cast<int>(c.unit) + 1) % 5;
  c.unit = static_cast<UnitMode>(u);
}

void cycle_uptime(Settings& c) {
  int u = (static_cast<int>(c.uptime) + 1) % 3;
  c.uptime = static_cast<UptimeFmt>(u);
}

void cycle_theme(Settings& c) {
  int t = (static_cast<int>(c.theme) + 1) % 4;
  c.theme = static_cast<ThemeId>(t);
}

void cycle_refresh(Settings& c, int dir) {
  static const int rates[] = {250, 500, 750, 1000, 1500, 2000};
  int idx = 3;
  int best = 1 << 30;
  for (int i = 0; i < 6; ++i) {
    int d = std::abs(rates[i] - c.refresh_ms);
    if (d < best) {
      best = d;
      idx = i;
    }
  }
  idx += dir;
  if (idx < 0) idx = 0;
  if (idx > 5) idx = 5;
  c.refresh_ms = rates[idx];
}

void change_setting(AppState& st, int dir, PlatformCollector& pc) {
  switch (st.settings_row) {
    case 0: {
      int u = (static_cast<int>(st.cfg.unit) + (dir > 0 ? 1 : 4)) % 5;
      st.cfg.unit = static_cast<UnitMode>(u);
      break;
    }
    case 1: {
      int p = st.cfg.decimals + (dir > 0 ? 1 : -1);
      if (p < 0) p = 2;
      if (p > 2) p = 0;
      st.cfg.decimals = p;
      break;
    }
    case 2: {
      int u = (static_cast<int>(st.cfg.uptime) + (dir > 0 ? 1 : 2)) % 3;
      st.cfg.uptime = static_cast<UptimeFmt>(u);
      break;
    }
    case 3:
      cycle_refresh(st.cfg, dir > 0 ? 1 : -1);
      break;
    case 4: {
      int t = (static_cast<int>(st.cfg.theme) + (dir > 0 ? 1 : 3)) % 4;
      st.cfg.theme = static_cast<ThemeId>(t);
      break;
    }
    case 5:
      st.editing_scan = true;
      st.note("edit scan path, Enter to apply", st.now);
      break;
    default: break;
  }
  st.cfg.clamp();
  pc.set_scan_path(st.cfg.scan_path);
}

void render_page(Canvas& cv, int page, const Snapshot& s, AppState& st) {
  const Theme& th = theme_by_id(st.cfg.theme);
  UnitFormatter fmt(st.cfg);
  cv.clear(th.fg, th.bg);
  draw_header(cv, s, st, th, fmt);
  // clamp layout to term_cols-1 to avoid right-edge wrap/clipping
  Rect area{0, 2, std::max(0, cv.width() - 1), cv.height() - 3};
  if (area.h > 0) {
    switch (page) {
      case 0: view_overview(cv, area, s, st, th, fmt); break;
      case 1: view_power(cv, area, s, st, th, fmt); break;
      case 2: view_kernel(cv, area, s, st, th, fmt); break;
      case 3: view_cpu(cv, area, s, st, th, fmt); break;
      case 4: view_memory(cv, area, s, st, th, fmt); break;
      case 5: view_netio(cv, area, s, st, th, fmt); break;
      case 6: view_fscode(cv, area, s, st, th, fmt); break;
      case 7: view_kernel_lab(cv, area, s, st, th, fmt); break;
      default: break;
    }
  }
  draw_footer(cv, st, th);
  if (st.show_help) draw_help(cv, th, cv.width(), cv.height());
  if (st.show_settings) draw_settings(cv, st, th, fmt, cv.width(), cv.height());
  if (st.show_filter) draw_filter(cv, st, th, cv.width(), cv.height());
  if (st.kernel_sysctl_filter_mode) {
    Rect in;
    overlay_frame(cv, th, cv.width(), cv.height(), "Filter sysctl (Kernel Lab)", in);
    cv.text(in.x, in.y, "/ " + st.kernel_sysctl_filter + "_", th.fg, th.bg);
    cv.text(in.x, in.y + 2, "Enter apply   Esc clear/cancel   Backspace delete", th.muted, th.bg);
    cv.text(in.x, in.y + 3, "Active sysctl filter: " + (st.kernel_sysctl_filter.empty() ? std::string("<none>") : st.kernel_sysctl_filter), th.accent, th.bg);
  }
}

// small ramp so a fresh headless frame is not a single point
void seed_synthetic(AppState& st, const Snapshot& s) {
  for (int i = 0; i < 60; ++i) {
    Snapshot copy = s;
    double k = static_cast<double>(i) / 59.0;
    copy.cpu.app = std::min(100.0, s.cpu.app * (0.6 + 0.4 * k) + 4.0 * std::sin(k * 9.0));
    copy.cpu.kern = std::min(100.0, s.cpu.kern * (0.7 + 0.3 * k) + 2.0 * std::cos(k * 7.0));
    copy.cpu.io = std::max(0.0, s.cpu.io * (0.5 + 0.5 * k));
    copy.cpu.irq = std::max(0.0, s.cpu.irq * (0.6 + 0.4 * k));
    copy.net.rx_bps = s.net.rx_bps * (0.4 + 0.9 * k);
    copy.net.tx_bps = s.net.tx_bps * (0.6 + 0.6 * (1.0 - k));
    copy.disk.r_bps = s.disk.r_bps * (0.5 + 0.7 * k);
    copy.disk.w_bps = s.disk.w_bps * (0.7 + 0.5 * (1.0 - k));
    copy.power.total_watts = s.power.total_watts * (0.9 + 0.15 * k);
    copy.kern.ctx_rate = s.kern.ctx_rate * (0.7 + 0.5 * k);
    copy.irq.total_rate = s.irq.total_rate * (0.6 + 0.6 * k);
    copy.kern.psi_cpu = s.kern.psi_cpu * (0.5 + 0.6 * k);
    copy.kern.psi_io = s.kern.psi_io * (0.4 + 0.7 * (1.0 - k));
    copy.kern.psi_mem = s.kern.psi_mem * (0.5 + 0.5 * k);
    copy.mem.avail = s.mem.avail;
    st.on_sample(copy);
  }
}

void refresh_kernel_lab_if_needed(AppState& st, bool force = false) {
  if (!st.kernel_lab) return;
  if (force || st.now - st.kernel_last_refresh >= 2.0) {
    st.kernel_lab->refresh_installed();
    // sysctl/inspector refresh is relatively light after first load; keep responsive
    st.kernel_lab->refresh_inspector(false);
    st.kernel_snapshot_names = st.kernel_lab->list_snapshots();
    st.kernel_last_refresh = st.now;
  }
}

int run_headless(const Options& o) {
  PlatformCollector pc;
  Settings cfg;
  if (o.refresh_ms > 0) cfg.refresh_ms = o.refresh_ms;
  cfg.clamp();
  AppState st;
  st.cfg = cfg;
  pc.set_scan_path(st.cfg.scan_path);
  if (st.kernel_lab) {
    st.kernel_lab->refresh_installed();
    st.kernel_lab->refresh_inspector(true);
    st.kernel_snapshot_names = st.kernel_lab->list_snapshots();
  }

  Snapshot snap;
  int frames = o.frames > 0 ? o.frames : 1;
  for (int i = 0; i < frames; ++i) {
    snap = pc.collect();
    if (i + 1 < frames) {
      struct timespec ts {0, 60L * 1000L * 1000L};
      nanosleep(&ts, nullptr);
    }
  }
  if (frames == 1) seed_synthetic(st, snap);
  else st.on_sample(snap);

  Canvas cv(o.width, o.height);
  if (o.render_all) {
    for (int p = 0; p < 8; ++p) {
      st.page = p;
      render_page(cv, p, snap, st);
      std::printf("=== PAGE %d: %s (%dx%d) ===\n", p + 1, kTabs[p], cv.width(), cv.height());
      std::string frame = cv.render();
      std::fwrite(frame.data(), 1, frame.size(), stdout);
      std::printf("\n");
    }
    std::fflush(stdout);
    return 0;
  }
  st.page = o.tab;
  render_page(cv, o.tab, snap, st);
  std::string frame = cv.render();
  std::fwrite(frame.data(), 1, frame.size(), stdout);
  std::fputc('\n', stdout);
  std::fflush(stdout);
  return 0;
}

int run_interactive(const Options& o) {
  PlatformCollector pc;
  AppState st;
  if (o.refresh_ms > 0) st.cfg.refresh_ms = o.refresh_ms;
  st.cfg.clamp();
  st.page = o.tab;
  pc.set_scan_path(st.cfg.scan_path);
  if (st.kernel_lab) {
    st.kernel_lab->refresh_installed();
    st.kernel_lab->refresh_inspector(true);
    st.kernel_snapshot_names = st.kernel_lab->list_snapshots();
  }

  Terminal term;
  if (!term.enter()) {
    std::fprintf(stderr, "ClangTop++: stdout/stdin is not a terminal (try --render-once)\n");
    return 1;
  }

  Snapshot snap = pc.collect();
  seed_synthetic(st, snap);

  auto last_sample = std::chrono::steady_clock::now();
  int w = 0, h = 0;
  bool running = true;
  bool need_redraw = true;

  while (running) {
    if (Terminal::resize_flag()) {
      Terminal::resize_flag() = 0;
      need_redraw = true;
      term.write("\x1b[2J");
    }
    term.size(w, h);
    if (w < 40 || h < 12) {
      need_redraw = true;
    }

    auto now = std::chrono::steady_clock::now();
    double since = std::chrono::duration<double>(now - last_sample).count();
    if (!st.paused && since * 1000.0 >= st.cfg.refresh_ms) {
      snap = pc.collect();
      st.on_sample(snap);
      last_sample = now;
      if (st.page == 7) refresh_kernel_lab_if_needed(st);
      need_redraw = true;
    }

    if (need_redraw) {
      Canvas cv(w, h);
      if (w < 60 || h < 16) {
        const Theme& th = theme_by_id(st.cfg.theme);
        cv.clear(th.fg, th.bg);
        std::string msg = "terminal too small (" + std::to_string(w) + "x" + std::to_string(h) + ")";
        cv.text(std::max(0, (w - static_cast<int>(msg.size())) / 2), h / 2, msg, th.yellow, th.bg);
      } else {
        render_page(cv, st.page, snap, st);
      }
      std::string frame = "\x1b[H" + cv.render();
      term.write(frame);
      need_redraw = false;
    }

    int timeout = st.paused ? 120 : std::min(250, st.cfg.refresh_ms);
    int key = term.read_key(timeout);
    if (key == 0) continue;

    if (st.editing_scan) {
      if (key == Terminal::kEnter) {
        st.editing_scan = false;
        pc.set_scan_path(st.cfg.scan_path);
        pc.rescan_code();
        st.note("scan path applied: " + (st.cfg.scan_path.empty() ? std::string("$HOME") : st.cfg.scan_path), st.now);
      } else if (key == Terminal::kEsc) {
        st.editing_scan = false;
      } else if (key == Terminal::kBackspace) {
        if (!st.cfg.scan_path.empty()) st.cfg.scan_path.pop_back();
      } else if (key >= 32 && key < 127 && st.cfg.scan_path.size() < 200) {
        st.cfg.scan_path.push_back(static_cast<char>(key));
      }
      need_redraw = true;
      continue;
    }

    if (st.show_settings) {
      if (key == Terminal::kUp) {
        st.settings_row = (st.settings_row + 5) % 6;
        need_redraw = true;
      } else if (key == Terminal::kDown) {
        st.settings_row = (st.settings_row + 1) % 6;
        need_redraw = true;
      } else if (key == Terminal::kLeft) {
        change_setting(st, -1, pc);
        need_redraw = true;
      } else if (key == Terminal::kRight || key == Terminal::kEnter) {
        change_setting(st, 1, pc);
        need_redraw = true;
      } else if (key == ',' || key == Terminal::kEsc || key == 'q') {
        st.show_settings = false;
        st.editing_scan = false;
        need_redraw = true;
      } else if (key == '?') {
        st.show_help = true;
      }
      continue;
    }

    if (st.show_help) {
      st.show_help = false;
      need_redraw = true;
      if (key == 'q') running = false;
      continue;
    }

    if (st.show_filter) {
      if (key == Terminal::kEnter) {
        st.show_filter = false;
        st.note(st.filter.empty() ? "filter cleared" : "filter: " + st.filter, st.now);
      } else if (key == Terminal::kEsc) {
        st.show_filter = false;
        st.filter.clear();
        st.note("filter cleared", st.now);
      } else if (key == Terminal::kBackspace) {
        if (!st.filter.empty()) st.filter.pop_back();
      } else if (key >= 32 && key < 127) {
        st.filter.push_back(static_cast<char>(key));
      }
      need_redraw = true;
      continue;
    }

    if (st.kernel_sysctl_filter_mode) {
      if (key == Terminal::kEnter) {
        st.kernel_sysctl_filter_mode = false;
        st.kernel_sysctl_scroll = 0;
        st.note(st.kernel_sysctl_filter.empty() ? "sysctl filter cleared" : "sysctl filter: " + st.kernel_sysctl_filter, st.now);
      } else if (key == Terminal::kEsc) {
        st.kernel_sysctl_filter_mode = false;
        st.kernel_sysctl_filter.clear();
        st.kernel_sysctl_scroll = 0;
        st.note("sysctl filter cleared", st.now);
      } else if (key == Terminal::kBackspace) {
        if (!st.kernel_sysctl_filter.empty()) st.kernel_sysctl_filter.pop_back();
      } else if (key >= 32 && key < 127) {
        st.kernel_sysctl_filter.push_back(static_cast<char>(key));
      }
      need_redraw = true;
      continue;
    }

    // Kernel Lab diff pick mode uses Up/Down/Enter/Esc/c
    if (st.page == 7 && st.kernel_diff_stage != 0) {
      if (key == Terminal::kUp) {
        if (!st.kernel_snapshot_names.empty()) st.kernel_snapshot_sel = std::max(0, st.kernel_snapshot_sel - 1);
        need_redraw = true; continue;
      } else if (key == Terminal::kDown) {
        if (!st.kernel_snapshot_names.empty()) st.kernel_snapshot_sel = std::min(static_cast<int>(st.kernel_snapshot_names.size()) - 1, st.kernel_snapshot_sel + 1);
        need_redraw = true; continue;
      } else if (key == Terminal::kEnter || key == 'c') {
        if (!st.kernel_snapshot_names.empty()) {
          int idx = std::clamp(st.kernel_snapshot_sel, 0, static_cast<int>(st.kernel_snapshot_names.size()) - 1);
          std::string sel = st.kernel_snapshot_names[static_cast<std::size_t>(idx)];
          if (st.kernel_diff_stage == 1) {
            st.kernel_diff_a = sel;
            st.kernel_diff_stage = 2;
            st.note("diff A='" + sel + "' - select B (Enter/c)", st.now);
          } else if (st.kernel_diff_stage == 2) {
            st.kernel_diff_result = st.kernel_lab->diff_snapshots(st.kernel_diff_a, sel);
            st.kernel_diff_has = true;
            st.kernel_diff_stage = 0;
            st.note("diff computed: " + st.kernel_diff_a + " vs " + sel, st.now);
          }
        } else {
          st.kernel_diff_stage = 0;
          st.note("no snapshots available", st.now);
        }
        need_redraw = true; continue;
      } else if (key == Terminal::kEsc) {
        st.kernel_diff_stage = 0;
        st.note("diff selection cancelled", st.now);
        need_redraw = true; continue;
      }
      // fallthrough to allow q/? if desired? but easier to consume
    }

    switch (key) {
      case 'q':
        running = false;
        break;
      case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8':
        st.page = key - '1';
        if (st.page == 7) refresh_kernel_lab_if_needed(st, true);
        need_redraw = true;
        break;
      case Terminal::kTab:
      case Terminal::kRight:
        st.page = (st.page + 1) % 8;
        if (st.page == 7) refresh_kernel_lab_if_needed(st, true);
        need_redraw = true;
        break;
      case Terminal::kLeft:
        st.page = (st.page + 7) % 8;
        if (st.page == 7) refresh_kernel_lab_if_needed(st, true);
        need_redraw = true;
        break;
      case Terminal::kUp:
        if (st.page == 7) {
          if (st.kernel_left_catalog) {
            st.kernel_catalog_sel = std::max(0, st.kernel_catalog_sel - 1);
          } else {
            st.kernel_installed_sel = std::max(0, st.kernel_installed_sel - 1);
          }
          need_redraw = true;
        }
        break;
      case Terminal::kDown:
        if (st.page == 7) {
          if (st.kernel_left_catalog) {
            int n = static_cast<int>(kernel_catalog().size());
            if (n > 0) st.kernel_catalog_sel = std::min(n - 1, st.kernel_catalog_sel + 1);
          } else {
            auto inst = st.kernel_lab->installed_kernels();
            int n = static_cast<int>(inst.size());
            if (n > 0) st.kernel_installed_sel = std::min(n - 1, st.kernel_installed_sel + 1);
          }
          need_redraw = true;
        }
        break;
      case 'u':
        cycle_uptime(st.cfg);
        st.note(std::string("uptime: ") + uptime_fmt_name(st.cfg.uptime), st.now);
        need_redraw = true;
        break;
      case 'b':
        cycle_unit(st.cfg);
        st.note(std::string("units: ") + unit_mode_name(st.cfg.unit), st.now);
        need_redraw = true;
        break;
      case 't':
        cycle_theme(st.cfg);
        st.note(std::string("theme: ") + theme_name(st.cfg.theme), st.now);
        need_redraw = true;
        break;
      case 's':
        st.sort_mode = (st.sort_mode + 1) % kSortCount;
        st.note(std::string("sort: ") + sort_name(st.sort_mode), st.now);
        need_redraw = true;
        break;
      case '/':
        if (st.page == 7) {
          st.kernel_sysctl_filter_mode = true;
          st.kernel_sysctl_filter.clear();
          st.show_filter = false;
        } else {
          st.show_filter = true;
          st.filter.clear();
          st.kernel_sysctl_filter_mode = false;
        }
        need_redraw = true;
        break;
      case ' ':
        st.paused = !st.paused;
        st.note(st.paused ? "paused" : "resumed", st.now);
        need_redraw = true;
        break;
      case ',':
        st.show_settings = true;
        st.settings_row = 0;
        st.editing_scan = false;
        need_redraw = true;
        break;
      case '?':
        st.show_help = true;
        need_redraw = true;
        break;
      case 'v':
        if (st.page == 7) {
          st.kernel_left_catalog = !st.kernel_left_catalog;
          st.note(st.kernel_left_catalog ? "Kernel Lab: catalog" : "Kernel Lab: installed kernels", st.now);
          need_redraw = true;
        }
        break;
      case 'm':
        if (st.page == 7) {
          std::string err;
          bool ok = st.kernel_lab->save_snapshot(&err);
          st.kernel_snapshot_names = st.kernel_lab->list_snapshots();
          st.note(ok ? "snapshot saved: " + st.kernel_lab->active_kernel() : ("snapshot save failed: " + err), st.now);
          need_redraw = true;
        }
        break;
      case 'c':
        if (st.page == 7) {
          st.kernel_snapshot_names = st.kernel_lab->list_snapshots();
          if (st.kernel_snapshot_names.empty()) {
            st.note("no saved snapshots to diff (press 'm' to save)", st.now);
          } else if (st.kernel_diff_stage == 0) {
            st.kernel_diff_stage = 1;
            st.kernel_snapshot_sel = 0;
            st.note("diff: select snapshot A (Up/Down, Enter/c, Esc cancel)", st.now);
          }
          need_redraw = true;
        }
        break;
      case 'r':
        if (st.page == 7) {
          st.kernel_lab->refresh_installed();
          st.kernel_lab->refresh_inspector(false);
          st.kernel_lab->start_benchmark();
          st.note("kernel benchmark started (~2s)", st.now);
        } else {
          pc.rescan_code();
          st.note("rescanning code...", st.now);
        }
        need_redraw = true;
        break;
      case '+': case '=':
        cycle_refresh(st.cfg, 1);
        st.note("refresh: " + std::to_string(st.cfg.refresh_ms) + " ms", st.now);
        need_redraw = true;
        break;
      case '-': case '_':
        cycle_refresh(st.cfg, -1);
        st.note("refresh: " + std::to_string(st.cfg.refresh_ms) + " ms", st.now);
        need_redraw = true;
        break;
      case Terminal::kEsc:
        if (st.kernel_diff_stage != 0) {
          st.kernel_diff_stage = 0;
          st.note("diff selection cancelled", st.now);
          need_redraw = true;
        } else {
          running = false;
        }
        break;
      default:
        break;
    }
  }

  term.leave();
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  Options o = parse_args(argc, argv);
  if (o.version) {
    std::printf("ClangTop++ v0.3.0\n");
    return 0;
  }
  if (o.help) {
    usage();
    return 0;
  }
  if (o.render_once) return run_headless(o);
  return run_interactive(o);
}
