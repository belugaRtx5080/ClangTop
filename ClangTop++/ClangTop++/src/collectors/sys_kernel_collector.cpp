// ClangTop++ - interrupts, scheduler, PSI, slab, kernel threads and PlatformCollector.
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <sys/utsname.h>
#include <unistd.h>

#if defined(__FreeBSD__)
#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/time.h>
#endif

#if defined(__FreeBSD__)
#include <sys/sysctl.h>
#include <sys/time.h>
#endif

#include "../core/platform_collector.hpp"
#include "../core/unit_formatter.hpp"

namespace ct {
namespace {

std::string trim(const std::string& in) {
  std::size_t a = 0, b = in.size();
  while (a < b && std::isspace(static_cast<unsigned char>(in[a]))) ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(in[b - 1]))) --b;
  return in.substr(a, b - a);
}

bool to_u64(const std::string& s, uint64_t& out) {
  if (s.empty()) return false;
  char* end = nullptr;
  unsigned long long v = std::strtoull(s.c_str(), &end, 10);
  if (end == s.c_str()) return false;
  out = static_cast<uint64_t>(v);
  return true;
}

bool all_digits(const char* s) {
  if (!s || !*s) return false;
  for (const char* p = s; *p; ++p)
    if (!std::isdigit(static_cast<unsigned char>(*p))) return false;
  return true;
}

double psi_some_avg10(const std::string& path) {
  std::ifstream f(path);
  std::string line;
  while (std::getline(f, line)) {
    if (line.rfind("some", 0) != 0) continue;
    std::size_t p = line.find("avg10=");
    if (p == std::string::npos) return 0;
    char* end = nullptr;
    double v = std::strtod(line.c_str() + p + 6, &end);
    if (end == line.c_str() + p + 6) return 0;
    return v;
  }
  return 0;
}

std::unordered_map<std::string, uint64_t> read_meminfo() {
  std::unordered_map<std::string, uint64_t> out;
  std::ifstream f("/proc/meminfo");
  std::string line;
  while (std::getline(f, line)) {
    std::size_t c = line.find(':');
    if (c == std::string::npos) continue;
    std::string key = trim(line.substr(0, c));
    std::istringstream is(line.substr(c + 1));
    unsigned long long v = 0;
    std::string unit;
    if (!(is >> v)) continue;
    is >> unit;
    if (unit == "kB") v *= 1024ULL;
    out[key] = static_cast<uint64_t>(v);
  }
  return out;
}

struct IrqLine {
  std::string name;
  std::vector<uint64_t> counts;
};

std::vector<IrqLine> read_interrupts() {
  std::vector<IrqLine> out;
  std::ifstream f("/proc/interrupts");
  std::string line;
  std::unordered_set<std::string> used;
  while (std::getline(f, line)) {
    if (out.size() >= 512) break;
    std::size_t colon = line.find(':');
    if (colon == std::string::npos) continue;
    const std::string head = trim(line.substr(0, colon));
    const std::string rest = line.substr(colon + 1);
    std::vector<uint64_t> counts;
    std::size_t pos = 0;
    std::size_t desc_start = std::string::npos;
    while (pos < rest.size()) {
      while (pos < rest.size() && std::isspace(static_cast<unsigned char>(rest[pos]))) ++pos;
      if (pos >= rest.size()) break;
      std::size_t start = pos;
      while (pos < rest.size() && !std::isspace(static_cast<unsigned char>(rest[pos]))) ++pos;
      std::string tok = rest.substr(start, pos - start);
      uint64_t v = 0;
      if (to_u64(tok, v)) {
        counts.push_back(v);
      } else {
        desc_start = start;
        break;
      }
    }
    std::string name = desc_start == std::string::npos ? head : trim(rest.substr(desc_start));
    if (name.empty()) name = head.empty() ? "irq" : head;
    if (name.size() > 48) name.resize(48);
    if (!used.insert(name).second) name = head + " " + name;
    IrqLine il;
    il.name = name;
    il.counts = std::move(counts);
    out.push_back(std::move(il));
  }
  return out;
}

int count_kernel_threads() {
  int n = 0;
  DIR* d = opendir("/proc");
  if (!d) return 0;
  while (struct dirent* e = readdir(d)) {
    if (!all_digits(e->d_name)) continue;
    std::ifstream f(std::string("/proc/") + e->d_name + "/stat");
    std::string line;
    if (!std::getline(f, line)) continue;
    std::size_t rp = line.rfind(')');
    if (rp == std::string::npos) continue;
    std::istringstream is(line.substr(rp + 1));
    std::vector<std::string> tok;
    std::string t;
    while (is >> t) tok.push_back(t);
    if (tok.size() < 7) continue;
    uint64_t flags = 0;
    if (!to_u64(tok[6], flags)) continue;
    if (flags & 0x00200000ULL) ++n;
  }
  closedir(d);
  return n;
}

}  // namespace
void SysKernelCollector::collect(Snapshot& s, double dt) {
  if (dt <= 0.0001) dt = 0.001;
  KernelInfo& k = s.kern;
  k = KernelInfo{};

  uint64_t ctxt_stat = 0, forks = 0;
  double running = 0, blocked = 0;
  bool have_ctxt_stat = false, have_forks = false;
  {
    std::ifstream f("/proc/stat");
    std::string line;
    while (std::getline(f, line)) {
      std::istringstream is(line);
      std::string key;
      is >> key;
      if (key == "ctxt") {
        uint64_t v = 0;
        if (is >> v) {
          ctxt_stat = v;
          have_ctxt_stat = true;
        }
      } else if (key == "processes") {
        uint64_t v = 0;
        if (is >> v) {
          forks = v;
          have_forks = true;
        }
      } else if (key == "procs_running") {
        is >> running;
      } else if (key == "procs_blocked") {
        is >> blocked;
      }
    }
  }
  k.procs_running = running;
  k.procs_blocked = blocked;

  uint64_t ctxt = ctxt_stat;
  bool have_ctxt = have_ctxt_stat;
  {
    std::ifstream f("/proc/schedstat");
    std::string line;
    if (std::getline(f, line)) {
      std::istringstream is(line);
      std::string a, b;
      is >> a >> b;
      if (a == "ctxt") {
        uint64_t v = 0;
        if (to_u64(b, v)) {
          ctxt = v;
          have_ctxt = true;
        }
      } else if (a != "version" && !b.empty()) {
        uint64_t v = 0;
        if (to_u64(b, v) && v > 1000) {
          ctxt = v;
          have_ctxt = true;
        }
      }
    }
  }

  if (have_ && have_ctxt && ctxt >= prev_ctx_)
    k.ctx_rate = static_cast<double>(ctxt - prev_ctx_) / dt;
  if (have_ && have_forks && forks >= prev_forks_)
    k.forks_rate = static_cast<double>(forks - prev_forks_) / dt;
  if (have_ctxt) k.ctx_total = ctxt;
  prev_ctx_ = ctxt;
  prev_forks_ = forks;

  // ------------------------------------------------------------ page faults
  uint64_t pgfault = 0, pgmaj = 0;
  {
    std::ifstream f("/proc/vmstat");
    std::string key;
    uint64_t val = 0;
    while (f >> key >> val) {
      if (key == "pgfault") pgfault = val;
      else if (key == "pgmajfault") pgmaj = val;
    }
  }
  k.pgfault_total = pgfault;
  k.pgmajfault_total = pgmaj;
  if (have_ && pgfault >= prev_pgfault_)
    k.pgfault_rate = static_cast<double>(pgfault - prev_pgfault_) / dt;
  if (have_ && pgmaj >= prev_pgmajfault_)
    k.pgmajfault_rate = static_cast<double>(pgmaj - prev_pgmajfault_) / dt;
  prev_pgfault_ = pgfault;
  prev_pgmajfault_ = pgmaj;

  // ------------------------------------------------------------ scheduler policy
  k.sched_policy = "default";
  {
    std::ifstream f("/sys/kernel/sched_ext/state");
    std::string st;
    if (std::getline(f, st)) {
      st = trim(st);
      if (!st.empty() && st != "disabled") k.sched_policy = "sched_ext(" + st + ")";
    }
  }
  if (k.sched_policy == "default") {
    // best-effort: modern kernels often use EEVDF, older CFS; label generically
    k.sched_policy = "EEVDF/CFS";
  }

  // ------------------------------------------------------------ interrupts
  std::vector<IrqLine> lines = read_interrupts();
  int ncores = s.ncpu > 0 ? s.ncpu : 0;
  for (const IrqLine& il : lines) ncores = std::max(ncores, static_cast<int>(il.counts.size()));
  if (ncores <= 0) ncores = 1;

  std::unordered_map<std::string, std::vector<uint64_t>> prev_by_name;
  for (std::size_t i = 0; i < prev_irq_names_.size() && i < prev_irq_counts_.size(); ++i)
    prev_by_name[prev_irq_names_[i]] = prev_irq_counts_[i];

  IrqInfo irq;
  irq.core_rate.assign(static_cast<std::size_t>(ncores), 0.0);
  std::vector<uint64_t> core_totals(static_cast<std::size_t>(ncores), 0);
  for (const IrqLine& il : lines) {
    std::vector<double> row(static_cast<std::size_t>(ncores), 0.0);
    double line_rate = 0;
    auto pit = prev_by_name.find(il.name);
    for (std::size_t c = 0; c < il.counts.size() && c < static_cast<std::size_t>(ncores); ++c) {
      uint64_t prev = il.counts[c];
      if (pit != prev_by_name.end() && c < pit->second.size()) prev = pit->second[c];
      uint64_t delta = il.counts[c] >= prev ? il.counts[c] - prev : 0;
      double rate = static_cast<double>(delta) / dt;
      row[c] = rate;
      line_rate += rate;
      irq.core_rate[c] += rate;
      core_totals[c] += il.counts[c];
    }
    irq.names.push_back(il.name);
    irq.rates.push_back(line_rate);
    irq.core.push_back(std::move(row));
    irq.total_rate += line_rate;
  }
  prev_irq_names_.clear();
  prev_irq_counts_.clear();
  prev_irq_names_.reserve(lines.size());
  prev_irq_counts_.reserve(lines.size());
  for (const IrqLine& il : lines) {
    prev_irq_names_.push_back(il.name);
    prev_irq_counts_.push_back(il.counts);
  }
  prev_irq_core_ = core_totals;
  s.irq = std::move(irq);

  // ------------------------------------------------------------ PSI
  k.psi_cpu = psi_some_avg10("/proc/pressure/cpu");
  k.psi_io = psi_some_avg10("/proc/pressure/io");
  k.psi_mem = psi_some_avg10("/proc/pressure/memory");

  // ------------------------------------------------------------ slab
  std::unordered_map<std::string, uint64_t> mi = read_meminfo();
  auto mv = [&](const char* key) -> uint64_t {
    auto it = mi.find(key);
    return it == mi.end() ? 0 : it->second;
  };
  k.slab = mv("Slab");
  k.kernel_stack = mv("KernelStack");
  k.pagetables = mv("PageTables");
  {
    std::vector<std::pair<std::string, uint64_t>> top;
    std::ifstream f("/proc/slabinfo");
    std::string line;
    while (std::getline(f, line)) {
      if (line.empty() || line[0] == '#') continue;
      std::istringstream is(line);
      std::string name;
      unsigned long long active = 0, total = 0, objsize = 0;
      if (!(is >> name >> active >> total >> objsize)) continue;
      top.emplace_back(name, active * objsize);
    }
    std::sort(top.begin(), top.end(),
              [](const std::pair<std::string, uint64_t>& a, const std::pair<std::string, uint64_t>& b) {
                return a.second > b.second;
              });
    if (top.size() > 10) top.resize(10);
    k.slab_top = std::move(top);
  }

  // ------------------------------------------------------------ scheduler summary
  k.threads = count_kernel_threads();
  {
    std::ifstream f("/proc/loadavg");
    f >> k.load1 >> k.load5 >> k.load15;
  }
  k.sched_lines.push_back("schedstat ctxt: " + std::to_string(k.ctx_total));
  k.sched_lines.push_back("runnable " + std::to_string(static_cast<long long>(k.procs_running)));
  k.sched_lines.push_back("blocked " + std::to_string(static_cast<long long>(k.procs_blocked)));
  k.sched_lines.push_back("forks: " + std::to_string(forks));
  k.sched_lines.push_back("kernel threads: " + std::to_string(k.threads));
  have_ = true;
}

// ---------------------------------------------------------------- PlatformCollector
double system_uptime() {
  {
    std::ifstream f("/proc/uptime");
    double up = 0;
    if (f >> up && up >= 0) return up;
  }
#if defined(CLOCK_BOOTTIME)
  struct timespec ts;
  if (clock_gettime(CLOCK_BOOTTIME, &ts) == 0)
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) / 1e9;
#endif
#if defined(__FreeBSD__)
  struct timeval tv;
  std::size_t len = sizeof(tv);
  int mib[2] = {CTL_KERN, KERN_BOOTTIME};
  if (sysctl(mib, 2, &tv, &len, nullptr, 0) == 0) {
    std::time_t now = std::time(nullptr);
    if (static_cast<long long>(now) >= static_cast<long long>(tv.tv_sec))
      return static_cast<double>(now - tv.tv_sec);
  }
#endif
  return 0;
}

struct PlatformCollector::Impl {
  CpuCollector cpu;
  PowerCollector power;
  MemIoNetCollector memio;
  SysKernelCollector sysk;
  FsCodeScanner scanner;
  std::chrono::steady_clock::time_point start;
  std::chrono::steady_clock::time_point prev;
  bool have_prev = false;
  std::string scan_path;

  Impl();
};

PlatformCollector::Impl::Impl()
    : start(std::chrono::steady_clock::now()), prev(start) {
  const char* home = std::getenv("HOME");
  scan_path = (home && *home) ? std::string(home) : std::string(".");
  scanner.start(scan_path);
}

PlatformCollector::PlatformCollector() : impl_(new Impl()) {}
PlatformCollector::~PlatformCollector() = default;

void PlatformCollector::set_scan_path(const std::string& path) {
  if (!impl_) return;
  if (path == impl_->scan_path) return;
  impl_->scan_path = path;
  impl_->scanner.start(path);
}

void PlatformCollector::rescan_code() {
  if (!impl_) return;
  impl_->scanner.start(impl_->scan_path);
}

Snapshot PlatformCollector::collect() {
  Snapshot s;
  if (!impl_) return s;
  const auto now = std::chrono::steady_clock::now();
  double dt = impl_->have_prev
                  ? std::chrono::duration<double>(now - impl_->prev).count()
                  : 0.001;
  if (dt < 0.001) dt = 0.001;
  if (dt > 60.0) dt = 60.0;
  impl_->prev = now;
  impl_->have_prev = true;

  struct utsname u;
  if (uname(&u) == 0) {
    s.sys.uname_sys = u.sysname;
    s.sys.uname_release = u.release;
    s.sys.uname_version = u.version;
    s.sys.hostname = u.nodename;
    s.sys.arch = u.machine;
  }
  s.sys.uptime = system_uptime();
  s.sys.uptime_human = UnitFormatter::uptime_pretty(s.sys.uptime);

  impl_->cpu.collect(s, dt);
  impl_->memio.collect(s, dt);
  impl_->power.collect(s, dt);
  impl_->sysk.collect(s, dt);
  impl_->scanner.collect_mounts(s.fs);
  s.code = impl_->scanner.snapshot();
  s.ts = std::chrono::duration<double>(now.time_since_epoch()).count();
  if (s.ncpu <= 0) {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    s.ncpu = n > 0 ? static_cast<int>(n) : 1;
  }
  return s;
}

}  // namespace ct
