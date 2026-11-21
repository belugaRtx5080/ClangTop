// ClangTop++ - smoke test (no external test framework)
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

#include "collectors/kernel_catalog.hpp"
#include "collectors/kernel_lab.hpp"
#include "config.hpp"
#include "core/platform_collector.hpp"
#include "core/ring_buffer.hpp"
#include "core/unit_formatter.hpp"
#include "ui/canvas.hpp"
#include "ui/theme.hpp"
#include "ui/views_kernel_lab.hpp"
#include "ui/views_resources.hpp"
#include "ui/views_system.hpp"
#include "ui/widgets.hpp"

namespace {

int g_fail = 0;
int g_pass = 0;

void check(bool ok, const std::string& what) {
  if (ok) {
    ++g_pass;
    std::printf("  ok   %s\n", what.c_str());
  } else {
    ++g_fail;
    std::printf("  FAIL %s\n", what.c_str());
  }
}

void test_ring_buffer() {
  std::printf("[ring_buffer]\n");
  ct::RingBuffer<8> rb;
  for (int i = 0; i < 10; ++i) rb.push(i);
  check(rb.size() == 8, "ring buffer keeps capacity");
  check(rb.at(0) == 2.0 && rb.at(7) == 9.0, "ring buffer ordering after wrap");
  check(std::fabs(rb.max() - 9.0) < 1e-9, "ring buffer max");
  check(std::fabs(rb.min() - 2.0) < 1e-9, "ring buffer min");
  check(std::fabs(rb.avg() - 5.5) < 1e-9, "ring buffer avg");
  auto tail = rb.tail(3);
  check(tail.size() == 3 && tail[2] == 9.0, "ring buffer tail");
  rb.clear();
  check(rb.empty(), "ring buffer clear");
}

void test_formatter() {
  std::printf("[unit_formatter]\n");
  ct::Settings s;
  s.decimals = 1;
  ct::UnitFormatter f(s);
  check(f.bytes(1536) == "1.5 KB", "auto bytes 1536 -> 1.5 KB (" + f.bytes(1536) + ")");
  check(f.bytes(1024.0 * 1024.0 * 3.0) == "3.0 MB", "auto bytes 3MiB -> 3.0 MB (" + f.bytes(3 * 1024 * 1024) + ")");
  s.unit = ct::UnitMode::MB;
  s.decimals = 0;
  f.configure(s);
  check(f.bytes(5.0 * 1024 * 1024) == "5 MB", "forced MB + 0 decimals (" + f.bytes(5.0 * 1024 * 1024) + ")");
  s.unit = ct::UnitMode::Bytes;
  f.configure(s);
  check(f.bytes(999) == "999 B", "forced bytes (" + f.bytes(999) + ")");
  check(ct::UnitFormatter::dur_clock(93784) == "01:02:03:04", "DD:HH:MM:SS uptime");
  check(ct::UnitFormatter::dur_compact(93784) == "1d 02h 03m", "compact uptime (" + ct::UnitFormatter::dur_compact(93784) + ")");
  check(ct::UnitFormatter::uptime_pretty(93784) == "up 1 day, 2 hours, 3 minutes",
        "uptime -p style (" + ct::UnitFormatter::uptime_pretty(93784) + ")");
  s.decimals = 2;
  f.configure(s);
  check(f.pct(42.123) == "42.12%", "percent precision (" + f.pct(42.123) + ")");
  check(f.watts(12.345) == "12.345 W", "watts precision (" + f.watts(12.345) + ")");
}

void test_canvas() {
  std::printf("[canvas]\n");
  ct::Theme th = ct::theme_by_id(ct::ThemeId::Gruvbox);
  ct::Canvas cv(80, 20);
  cv.clear(th.fg, th.bg);
  cv.box(ct::Rect{0, 0, 80, 10}, "Test", "stat 1", th.border, th.bg, th.accent, th.dim);
  std::vector<std::vector<double>> series;
  series.push_back({1, 5, 2, 8, 3, 9, 4, 7});
  series.push_back({2, 3, 4, 5, 6, 5, 4, 3});
  cv.braille_chart(ct::Rect{2, 2, 76, 6}, series, {th.accent, th.blue}, 0, 10, false);
  cv.stacked_area(ct::Rect{2, 11, 76, 5},
                  {{4, 4, 5, 6}, {2, 3, 2, 1}, {1, 1, 2, 2}},
                  {th.magenta, th.cyan, th.yellow}, 0, 12);
  cv.sparkline(2, 18, 40, {1, 2, 3, 4, 5, 4, 3, 2}, 0, 5, th.green);
  std::string out = cv.render();
  check(!out.empty(), "canvas renders output");
  check(out.find("\xE2\x94\x8C") != std::string::npos, "box drawing ┌ present");
  check(out.find("\xE2\xA0") != std::string::npos, "braille glyphs present");
  check(out.find("\xE2\x96") != std::string::npos, "block sparkline glyphs present");
  check(out.find("Test") != std::string::npos, "panel title rendered");
  check(out.find("\x1b[38;2;") != std::string::npos, "truecolor SGR emitted");
  ct::Canvas tiny(1, 1);
  tiny.clear(th.fg, th.bg);
  tiny.box(ct::Rect{0, 0, 1, 1}, "x", "y", th.border, th.bg, th.fg, th.fg);
  tiny.braille_chart(ct::Rect{0, 0, 1, 1}, series, {th.fg}, 0, 1, true);
  check(!tiny.render().empty(), "canvas clamps tiny rects");
}

void test_themes() {
  std::printf("[theme]\n");
  check(ct::theme_table().size() == 4, "four themes registered");
  check(ct::theme_by_id(ct::ThemeId::Gruvbox).name == "Gruvbox Dark", "gruvbox default");
  check(ct::theme_by_id(ct::ThemeId::Catppuccin).name == "Catppuccin Mocha", "catppuccin");
  check(ct::theme_by_id(ct::ThemeId::Nord).name == "Nord", "nord");
  check(ct::theme_by_id(ct::ThemeId::TokyoNight).name == "Tokyo Night", "tokyo night");
  check(ct::theme_by_id(ct::ThemeId::Nord).series.size() >= 4, "theme series palette");
}

void test_collector(ct::Snapshot& snap) {
  std::printf("[collectors]\n");
  ct::PlatformCollector pc;
  pc.set_scan_path(".");
  snap = pc.collect();
  std::this_thread::sleep_for(std::chrono::milliseconds(120));
  snap = pc.collect();
  std::this_thread::sleep_for(std::chrono::milliseconds(120));
  snap = pc.collect();
  check(snap.sys.uname_sys.size() >= 3, "uname sysname: " + snap.sys.uname_sys);
  check(snap.sys.uname_release.size() > 0, "uname release: " + snap.sys.uname_release);
  check(snap.sys.uptime > 0, "uptime sampled");
  check(!snap.sys.uptime_human.empty(), "uptime human: " + snap.sys.uptime_human);
  check(snap.ncpu >= 1, "ncpu >= 1 (" + std::to_string(snap.ncpu) + ")");
  check(static_cast<int>(snap.cores.size()) == snap.ncpu, "per-core samples match ncpu");
  check(snap.cpu.app >= 0 && snap.cpu.app <= 100.0, "cpu app in range");
  check(snap.mem.total > 0, "meminfo total > 0 (" + std::to_string(snap.mem.total) + ")");
  check(snap.mem.avail > 0 && snap.mem.avail <= snap.mem.total, "meminfo available");
  check(!snap.procs.empty(), "process list populated (" + std::to_string(snap.procs.size()) + ")");
  bool sane_proc = false;
  for (const auto& p : snap.procs) {
    if (p.pid > 0 && !p.name.empty()) sane_proc = true;
  }
  check(sane_proc, "process entries have pid+name");
  check(snap.irq.names.size() == snap.irq.rates.size(), "irq names/rates parallel");
  check(!snap.fs.empty(), "mounted filesystems discovered (" + std::to_string(snap.fs.size()) + ")");
  bool sane_fs = !snap.fs.empty() && snap.fs[0].total > 0;
  check(sane_fs, "first mount has size");
  check(!snap.code.path.empty(), "code scanner path: " + snap.code.path);
}

void test_views(const ct::Snapshot& snap) {
  std::printf("[views]\n");
  ct::AppState st;
  for (int i = 0; i < 120; ++i) {
    ct::Snapshot c = snap;
    c.cpu.app = 20 + 10 * std::sin(i * 0.2);
    c.cpu.kern = 8 + 4 * std::cos(i * 0.13);
    c.mem.avail = snap.mem.avail;
    st.on_sample(c);
  }
  const ct::Theme& th = ct::theme_by_id(ct::ThemeId::Gruvbox);
  ct::UnitFormatter fmt(st.cfg);
  int sizes[2][2] = {{200, 50}, {100, 24}};
  for (auto& dim : sizes) {
    ct::Canvas cv(dim[0], dim[1]);
    ct::Rect area{0, 2, dim[0], dim[1] - 3};
    std::size_t total = 0;
    auto render_view = [&](auto fn) {
      cv.clear(th.fg, th.bg);
      fn();
      return cv.render().size();
    };
    total += render_view([&]() { ct::view_overview(cv, area, snap, st, th, fmt); });
    total += render_view([&]() { ct::view_power(cv, area, snap, st, th, fmt); });
    total += render_view([&]() { ct::view_kernel(cv, area, snap, st, th, fmt); });
    total += render_view([&]() { ct::view_cpu(cv, area, snap, st, th, fmt); });
    total += render_view([&]() { ct::view_memory(cv, area, snap, st, th, fmt); });
    total += render_view([&]() { ct::view_netio(cv, area, snap, st, th, fmt); });
    total += render_view([&]() { ct::view_fscode(cv, area, snap, st, th, fmt); });
    total += render_view([&]() { ct::view_kernel_lab(cv, area, snap, st, th, fmt); });
    check(total > 8000, "all 8 views render at " + std::to_string(dim[0]) + "x" + std::to_string(dim[1]) +
                             " (" + std::to_string(total) + " bytes)");
  }
}

void test_scanner() {
  std::printf("[fs_code_scanner]\n");
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::path dir = fs::temp_directory_path(ec) / ("clangtop_scan_" + std::to_string(::getpid()));
  fs::create_directories(dir, ec);
  {
    std::ofstream a(dir / "alpha.cpp");
    a << "// comment\n#include <cstdio>\n\nint main() {\n  return 0;\n}\n";
    std::ofstream b(dir / "beta.py");
    b << "# comment\nimport os\n\nprint('hi')\n";
  }
  ct::FsCodeScanner scanner;
  scanner.start(dir.string());
  ct::CodeScan scan;
  for (int i = 0; i < 100; ++i) {
    scan = scanner.snapshot();
    if (scan.done) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  scanner.stop();
  check(scan.done, "async scan completed");
  check(scan.files >= 2, "scan counted files (" + std::to_string(scan.files) + ")");
  check(scan.total_lines >= 8, "scan counted lines (" + std::to_string(scan.total_lines) + ")");
  bool cpp = false, py = false;
  for (const auto& l : scan.langs) {
    if (l.lang.find("C++") != std::string::npos || l.lang.find("C/") != std::string::npos) cpp = true;
    if (l.lang.find("Python") != std::string::npos) py = true;
  }
  check(cpp && py, "language detection includes C/C++ and Python");
  fs::remove_all(dir, ec);
}

void test_kernel_catalog_lab() {
  std::printf("[kernel_catalog_lab]\n");
  const auto& cat = ct::kernel_catalog();
  check(!cat.empty(), "kernel catalog is non-empty (" + std::to_string(cat.size()) + " entries)");
  bool has_mainline = false, has_lts = false, has_zen = false, has_cachyos = false, has_rt = false;
  bool has_freebsd = false, has_openbsd = false, has_xnu = false, has_zircon = false, has_sel4 = false, has_redox = false;
  for (const auto& e : cat) {
    std::string n = e.name;
    if (n.find("Mainline") != std::string::npos || n.find("linux") != std::string::npos) has_mainline = true;
    if (n.find("LTS") != std::string::npos) has_lts = true;
    if (n.find("Zen") != std::string::npos || n.find("Liquorix") != std::string::npos) has_zen = true;
    if (n.find("CachyOS") != std::string::npos || n.find("BORE") != std::string::npos) has_cachyos = true;
    if (n.find("Realtime") != std::string::npos || n.find("PREEMPT_RT") != std::string::npos) has_rt = true;
    if (n.find("FreeBSD") != std::string::npos) has_freebsd = true;
    if (n.find("OpenBSD") != std::string::npos) has_openbsd = true;
    if (n.find("XNU") != std::string::npos || n.find("Darwin") != std::string::npos) has_xnu = true;
    if (n.find("Zircon") != std::string::npos) has_zircon = true;
    if (n.find("seL4") != std::string::npos) has_sel4 = true;
    if (n.find("Redox") != std::string::npos) has_redox = true;
  }
  check(has_mainline && has_lts && has_zen && has_cachyos && has_rt, "catalog contains key Linux variants");
  check(has_freebsd && has_openbsd && has_xnu && has_zircon && has_sel4 && has_redox, "catalog contains key non-Linux kernels");

  ct::KernelLab lab;
  lab.refresh_installed();
  lab.refresh_inspector(true);
  auto installed = lab.installed_kernels();
  auto inspector = lab.inspector();
  auto bench = lab.benchmark();
  check(true, "kernel lab refresh/inspect did not crash");
  check(!inspector.active_kernel.empty() || !inspector.uname_a.empty(), "inspector reports active kernel/uname");
  check(!installed.empty() || !inspector.active_kernel.empty(), "installed kernels scanned or active kernel known");
  check(bench.running == false, "benchmark not running initially");
  auto snaps = lab.list_snapshots();
  check(true, "kernel lab snapshot listing works (" + std::to_string(snaps.size()) + " snapshots)");
}

}  // namespace

int main() {
  std::printf("ClangTop++ v0.3.0 smoke test\n");
  test_ring_buffer();
  test_formatter();
  test_canvas();
  test_themes();
  ct::Snapshot snap;
  test_collector(snap);
  test_views(snap);
  test_scanner();
  test_kernel_catalog_lab();
  std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}
