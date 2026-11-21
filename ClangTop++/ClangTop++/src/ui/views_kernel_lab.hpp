// ClangTop++ - Kernel Lab view (tab 8): installed/catalog, benchmark, inspector, snapshot diff
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "../collectors/kernel_catalog.hpp"
#include "../collectors/kernel_lab.hpp"
#include "../core/metrics_types.hpp"
#include "../core/unit_formatter.hpp"
#include "canvas.hpp"
#include "theme.hpp"
#include "widgets.hpp"

namespace ct {

inline void view_kernel_lab(Canvas& cv, const Rect& area, const Snapshot& s, AppState& st, const Theme& th,
                            const UnitFormatter& fmt) {
  (void)s;
  if (!st.kernel_lab) return;
  if (area.w < 20 || area.h < 8) return;

  // refresh snapshot name cache lazily for UI
  st.kernel_snapshot_names = st.kernel_lab->list_snapshots();
  if (st.kernel_snapshot_sel < 0) st.kernel_snapshot_sel = 0;
  if (!st.kernel_snapshot_names.empty() &&
      st.kernel_snapshot_sel >= static_cast<int>(st.kernel_snapshot_names.size()))
    st.kernel_snapshot_sel = static_cast<int>(st.kernel_snapshot_names.size()) - 1;

  const int top_h = std::max(6, area.h * 45 / 100);
  const int bot_h = std::max(4, area.h - top_h);
  const int left_w = std::max(20, area.w * 52 / 100);
  const int right_w = std::max(10, area.w - left_w);
  Rect tl{area.x, area.y, left_w, top_h};
  Rect tr{area.x + left_w, area.y, right_w, top_h};
  Rect bl{area.x, area.y + top_h, left_w, bot_h};
  Rect br{area.x + left_w, area.y + top_h, right_w, bot_h};

  auto installed = st.kernel_lab->installed_kernels();
  auto inspector = st.kernel_lab->inspector();
  auto bench = st.kernel_lab->benchmark();
  const auto& catalog = kernel_catalog();

  auto clamp_sel = [](int& sel, int n) {
    if (n <= 0) { sel = 0; return; }
    if (sel < 0) sel = 0;
    if (sel >= n) sel = n - 1;
  };

  // ---------------- Top-left: installed kernels or global catalog
  {
    std::string title = st.kernel_left_catalog ? "All Kernels Encyclopedia (Linux + BSD/Microkernels)"
                                               : "Installed Kernels";
    std::string right = st.kernel_left_catalog ? "[v] installed | Up/Down browse" : "[v] catalog | active detected";
    panel(cv, tl, title, right, th, [&](Canvas& c, Rect in) {
      if (in.h <= 0 || in.w <= 0) return;
      int y = in.y;
      if (!st.kernel_left_catalog) {
        clamp_sel(st.kernel_installed_sel, static_cast<int>(installed.size()));
        int start = 0;
        if (st.kernel_installed_sel >= in.h) start = st.kernel_installed_sel - in.h + 1;
        for (int i = start; i < static_cast<int>(installed.size()) && y < in.y + in.h; ++i) {
          const auto& k = installed[static_cast<std::size_t>(i)];
          bool sel = (i == st.kernel_installed_sel);
          if (sel) c.fill(Rect{in.x, y, in.w, 1}, ' ', th.fg, th.blue);
          std::string badge = k.active ? " [ACTIVE]" : "";
          std::string mods = (k.module_count >= 0) ? (fmt.num(static_cast<double>(k.module_count), 0) + " mods") : "mods ?";
          std::string size = k.image_size ? fmt.bytes_short(static_cast<double>(k.image_size)) : "size ?";
          std::string line = " " + k.version + "  [" + k.flavor + "]  " + size + "  " + mods + badge;
          RGB fg = sel ? th.fg : (k.active ? th.green : th.fg);
          c.text_clipped(in.x, y, in.w, line, fg, sel ? th.blue : th.panel);
          ++y;
        }
        if (installed.empty()) c.text(in.x, y, "No kernels found in /boot or /lib/modules", th.yellow, th.panel);
      } else {
        clamp_sel(st.kernel_catalog_sel, static_cast<int>(catalog.size()));
        int start = 0;
        if (st.kernel_catalog_sel >= in.h) start = st.kernel_catalog_sel - in.h + 1;
        std::string active = inspector.active_kernel;
        for (int i = start; i < static_cast<int>(catalog.size()) && y < in.y + in.h; ++i) {
          const auto& e = catalog[static_cast<std::size_t>(i)];
          bool sel = (i == st.kernel_catalog_sel);
          bool act = (!active.empty() && (e.name.find(active) != std::string::npos));
          if (sel) c.fill(Rect{in.x, y, in.w, 1}, ' ', th.fg, th.blue);
          std::string mark = act ? " [ACTIVE]" : "";
          std::string line = " " + e.name + "  {" + e.family + "}  sched: " + e.scheduler + mark;
          RGB fg = sel ? th.fg : (act ? th.green : th.fg);
          c.text_clipped(in.x, y, in.w, line, fg, sel ? th.blue : th.panel);
          ++y;
        }
      }
    });
  }

  // ---------------- Top-right: benchmark
  {
    std::string right = bench.running ? "RUNNING..." : "[r] run benchmark";
    panel(cv, tr, "Kernel Throughput & Latency Benchmark", right, th, [&](Canvas& c, Rect in) {
      if (in.h <= 0 || in.w <= 0) return;
      int y = in.y;
      c.text_clipped(in.x, y++, in.w, "Active kernel: " + inspector.active_kernel, th.accent, th.panel);
      if (bench.running) {
        c.text(in.x, y++, "Running async benchmark (~2s): IPC, CtxSwitch, Syscall, VFS...", th.yellow, th.panel);
        c.text(in.x, y++, "UI remains responsive.", th.dim, th.panel);
        return;
      }
      if (!bench.valid) {
        c.text(in.x, y++, "Press 'r' on Kernel Lab tab to run live benchmark.", th.dim, th.panel);
        c.text(in.x, y++, "Measures: IPC pipe GB/s, context switch ns, syscall M/s, VFS cache GB/s", th.muted, th.panel);
        if (!bench.error.empty()) c.text_clipped(in.x, y++, in.w, "Error: " + bench.error, th.red, th.panel);
        return;
      }

      // comparison reference: selected snapshot if available
      KernelSnapshot ref;
      bool has_ref = false;
      if (!st.kernel_snapshot_names.empty()) {
        int rsel = std::clamp(st.kernel_snapshot_sel, 0, static_cast<int>(st.kernel_snapshot_names.size()) - 1);
        std::string err;
        has_ref = st.kernel_lab->load_snapshot(st.kernel_snapshot_names[static_cast<std::size_t>(rsel)], ref, &err) && ref.bench.valid;
      }

      struct M { std::string label; double val; double refv; bool lower_better; int prec; };
      std::vector<M> ms = {
          {"IPC GB/s", bench.ipc_gbps, has_ref ? ref.bench.ipc_gbps : 0.0, false, 2},
          {"Ctx Switch ns", bench.ctx_switch_ns, has_ref ? ref.bench.ctx_switch_ns : 0.0, true, 0},
          {"Syscalls M/s", bench.syscall_mops, has_ref ? ref.bench.syscall_mops : 0.0, false, 2},
          {"VFS Cache GB/s", bench.vfs_gbps, has_ref ? ref.bench.vfs_gbps : 0.0, false, 2},
      };
      for (const auto& m : ms) {
        if (y >= in.y + in.h) break;
        c.text_clipped(in.x, y, 16, m.label, th.dim, th.panel);
        std::string val = fmt.fixed(m.val, m.prec);
        c.text_clipped(in.x + 16, y, 12, val, th.fg, th.panel);
        int bx = in.x + 29;
        int bw = in.w - 30;
        if (bw > 12) {
          double maxv = std::max(m.val, m.refv);
          if (maxv <= 1e-9) maxv = 1.0;
          int wv = std::clamp(static_cast<int>(std::round((m.val / maxv) * (bw - 10))), 0, bw - 10);
          for (int i = 0; i < wv && bx + i < in.x + in.w - 9; ++i) c.glyph(bx + i, y, 0x2588, th.green);
          if (has_ref) {
            int wr = std::clamp(static_cast<int>(std::round((m.refv / maxv) * (bw - 10))), 0, bw - 10);
            int rx = bx + wr;
            if (rx >= in.x && rx < in.x + in.w - 9) c.glyph(rx, y, 0x2502, th.yellow);
            double delta = 0.0;
            if (std::fabs(m.refv) > 1e-12) delta = ((m.val - m.refv) / m.refv) * 100.0;
            char db[16]; std::snprintf(db, sizeof(db), "%+.1f%%", delta);
            bool good = m.lower_better ? (delta < 0) : (delta > 0);
            RGB dfg = std::fabs(delta) < 0.05 ? th.dim : (good ? th.green : th.red);
            c.text_clipped(in.x + in.w - 8, y, 8, pad_left(db, 8), dfg, th.panel);
          }
        }
        ++y;
      }
      if (y < in.y + in.h) {
        if (has_ref) c.text_clipped(in.x, y++, in.w, "vs snapshot: " + ref.name + " (| marker)", th.dim, th.panel);
        else c.text_clipped(in.x, y++, in.w, "Save snapshot with 'm' to compare (yellow marker)", th.dim, th.panel);
      }
      if (y < in.y + in.h) c.text_clipped(in.x, y++, in.w, "Last run: " + bench.timestamp + "  (" + fmt.fixed(bench.elapsed_s, 2) + "s)", th.muted, th.panel);
    });
  }

  // ---------------- Bottom-left: specs/sysctl/mitigations or catalog details
  {
    panel(cv, bl, "Kernel Viewer: Specs, Sysctl & Mitigations",
          st.kernel_left_catalog ? "catalog details" : ("/ filter: " + (st.kernel_sysctl_filter.empty() ? std::string("<none>") : st.kernel_sysctl_filter)),
          th, [&](Canvas& c, Rect in) {
      if (in.h <= 0 || in.w <= 0) return;
      int y = in.y;
      if (st.kernel_left_catalog) {
        if (catalog.empty()) { c.text(in.x, y, "No catalog entries", th.yellow, th.panel); return; }
        int idx = std::clamp(st.kernel_catalog_sel, 0, static_cast<int>(catalog.size()) - 1);
        const auto& e = catalog[static_cast<std::size_t>(idx)];
        auto row = [&](const std::string& k, const std::string& v, RGB col = RGB{0,0,0}) {
          if (y >= in.y + in.h) return;
          RGB fg = (col.r==0 && col.g==0 && col.b==0) ? th.fg : col;
          c.text_clipped(in.x, y, 18, k, th.dim, th.panel);
          c.text_clipped(in.x + 18, y, std::max(0, in.w - 18), v, fg, th.panel);
          ++y;
        };
        row("Name", e.name, th.accent);
        row("Family", e.family);
        row("Arch", e.architecture_type);
        row("Scheduler", e.scheduler);
        row("Default HZ", e.default_hz);
        row("Preemption", e.preemption);
        row("Language", e.language);
        auto block = [&](const char* label, const std::vector<std::string>& items, RGB col) {
          if (y >= in.y + in.h) return;
          c.text(in.x, y++, label, col, th.panel);
          for (const auto& it : items) {
            if (y >= in.y + in.h) break;
            c.text_clipped(in.x + 2, y++, std::max(0, in.w - 2), "- " + it, th.fg, th.panel);
          }
        };
        block("Pros:", e.pros, th.green);
        block("Cons:", e.cons, th.yellow);
        block("Best for:", e.best_for, th.blue);
        block("Sysctl hints:", e.key_sysctl_hints, th.magenta);
        if (y < in.y + in.h) {
          c.text_clipped(in.x, y++, in.w,
            "Active kernel: " + (inspector.active_kernel.empty() ? std::string("?") : inspector.active_kernel) +
            " | preempt: " + inspector.preemption_model, th.dim, th.panel);
        }
        return;
      }

      // Live inspector
      auto kv = [&](const std::string& k, const std::string& v) {
        if (y >= in.y + in.h) return;
        c.text_clipped(in.x, y, 16, k, th.dim, th.panel);
        c.text_clipped(in.x + 16, y, std::max(0, in.w - 16), v.empty() ? "(n/a)" : v, th.fg, th.panel);
        ++y;
      };
      kv("Active", inspector.active_kernel);
      kv("Preemption", inspector.preemption_model);
      kv("Config", inspector.config_source);
      if (!inspector.config_excerpt.empty()) kv("Config keys", inspector.config_excerpt);
      kv("Cmdline", inspector.cmdline);

      if (y < in.y + in.h) c.text(in.x, y++, "Mitigations (/sys/devices/system/cpu/vulnerabilities):", th.yellow, th.panel);
      int mc = 0;
      for (const auto& m : inspector.mitigations) {
        if (y >= in.y + in.h || mc >= 6) break;
        c.text_clipped(in.x, y++, in.w, "  " + m.name + ": " + m.status, th.fg, th.panel);
        ++mc;
      }
      if (inspector.mitigations.empty() && y < in.y + in.h) c.text(in.x, y++, "  (not available)", th.dim, th.panel);

      if (y < in.y + in.h) c.text_clipped(in.x, y++, in.w, "Sysctl (filtered):", th.cyan, th.panel);
      std::string f = lower_copy(st.kernel_sysctl_filter);
      std::vector<const SysctlParam*> filtered;
      filtered.reserve(inspector.sysctl_params.size());
      for (const auto& p : inspector.sysctl_params) {
        if (f.empty() || lower_copy(p.key).find(f) != std::string::npos || lower_copy(p.value).find(f) != std::string::npos)
          filtered.push_back(&p);
      }
      int visible = std::max(0, in.y + in.h - y);
      if (st.kernel_sysctl_scroll < 0) st.kernel_sysctl_scroll = 0;
      int max_scroll = std::max(0, static_cast<int>(filtered.size()) - visible);
      if (st.kernel_sysctl_scroll > max_scroll) st.kernel_sysctl_scroll = max_scroll;
      for (int i = st.kernel_sysctl_scroll; i < static_cast<int>(filtered.size()) && y < in.y + in.h; ++i) {
        const auto* p = filtered[static_cast<std::size_t>(i)];
        c.text_clipped(in.x, y++, in.w, "  " + p->key + " = " + p->value, th.fg, th.panel);
      }
      if (filtered.empty() && y < in.y + in.h) c.text(in.x, y, "  (no matches)", th.dim, th.panel);
    });
  }

  // ---------------- Bottom-right: snapshot diff comparator
  {
    panel(cv, br, "Kernel Snapshot Diff Comparator", "[m] save  [c] diff  Up/Down select", th, [&](Canvas& c, Rect in) {
      if (in.h <= 0 || in.w <= 0) return;
      int y = in.y;
      if (st.kernel_diff_stage == 1) c.text(in.x, y++, "Select snapshot A (Enter/c), Esc cancel", th.yellow, th.panel);
      else if (st.kernel_diff_stage == 2) c.text(in.x, y++, "Select snapshot B (Enter/c), A=" + st.kernel_diff_a, th.yellow, th.panel);
      else c.text(in.x, y++, "Snapshots in ~/.config/clangtop/snapshots:", th.dim, th.panel);

      if (st.kernel_snapshot_names.empty()) {
        if (y < in.y + in.h) c.text(in.x, y++, "No snapshots. Press 'm' to save active kernel snapshot.", th.muted, th.panel);
      } else {
        int list_rows = std::min(static_cast<int>(st.kernel_snapshot_names.size()), std::max(2, in.h / 3));
        int start = 0;
        if (st.kernel_snapshot_sel >= list_rows) start = st.kernel_snapshot_sel - list_rows + 1;
        for (int i = start; i < static_cast<int>(st.kernel_snapshot_names.size()) && y < in.y + in.h - 5; ++i) {
          bool sel = (i == st.kernel_snapshot_sel);
          if (sel) c.fill(Rect{in.x, y, in.w, 1}, ' ', th.fg, th.blue);
          std::string name = st.kernel_snapshot_names[static_cast<std::size_t>(i)];
          c.text_clipped(in.x, y, in.w, (sel ? "> " : "  ") + name, sel ? th.fg : th.dim, sel ? th.blue : th.panel);
          ++y;
        }
      }

      if (y < in.y + in.h) c.hline(in.x, y++, in.w, 0x2500, th.border, th.panel);
      if (st.kernel_diff_has && st.kernel_diff_result.ok) {
        c.text_clipped(in.x, y++, in.w, "A: " + st.kernel_diff_result.a_name + "  vs  B: " + st.kernel_diff_result.b_name, th.accent, th.panel);
        for (const auto& m : st.kernel_diff_result.metrics) {
          if (y >= in.y + in.h) break;
          char buf[200];
          std::snprintf(buf, sizeof(buf), "%-14s A=%8.2f B=%8.2f  %+7.2f%%", m.name.c_str(), m.a, m.b, m.delta_pct);
          bool improved = m.lower_is_better ? (m.delta_pct < 0) : (m.delta_pct > 0);
          RGB col = std::fabs(m.delta_pct) < 0.05 ? th.dim : (improved ? th.green : th.red);
          c.text_clipped(in.x, y++, in.w, buf, col, th.panel);
        }
        for (const auto& n : st.kernel_diff_result.notes) {
          if (y >= in.y + in.h) break;
          c.text_clipped(in.x, y++, in.w, n, th.dim, th.panel);
        }
      } else if (st.kernel_diff_has && !st.kernel_diff_result.ok) {
        c.text_clipped(in.x, y++, in.w, "Diff error: " + st.kernel_diff_result.error, th.red, th.panel);
      } else if (y < in.y + in.h) {
        c.text_clipped(in.x, y++, in.w, "Press 'c' to compare any two saved kernel snapshots.", th.dim, th.panel);
      }
    });
  }
}

}  // namespace ct
