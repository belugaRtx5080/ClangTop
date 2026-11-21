// ClangTop++ - CPU collector: /proc/stat, cpufreq, hwmon/thermal, per-process stats.
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <dirent.h>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <unistd.h>

#include "../core/platform_collector.hpp"

namespace ct {
namespace {

// ---------------------------------------------------------------- generic helpers
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

std::string read_all(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return {};
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

std::string read_first_line(const std::string& path) {
  std::ifstream f(path);
  std::string line;
  if (!std::getline(f, line)) return {};
  return trim(line);
}

uint64_t read_u64_file(const std::string& path) {
  std::ifstream f(path);
  unsigned long long v = 0;
  if (!(f >> v)) return 0;
  return static_cast<uint64_t>(v);
}

std::vector<std::string> list_dir(const std::string& root, const std::string& prefix) {
  std::vector<std::string> out;
  DIR* d = opendir(root.c_str());
  if (!d) return out;
  while (struct dirent* e = readdir(d)) {
    std::string n = e->d_name;
    if (n == "." || n == "..") continue;
    if (!prefix.empty() && n.rfind(prefix, 0) != 0) continue;
    out.push_back(n);
  }
  closedir(d);
  std::sort(out.begin(), out.end());
  return out;
}

int to_int(const std::string& s, int fallback = 0) {
  char* end = nullptr;
  long v = std::strtol(s.c_str(), &end, 10);
  if (end == s.c_str()) return fallback;
  return static_cast<int>(v);
}

// ---------------------------------------------------------------- /proc/stat
typedef std::array<uint64_t, 8> Ticks;

struct Pct {
  double app = 0, kern = 0, io = 0, irq = 0, idle = 0, util = 0;
};

bool parse_ticks(std::istringstream& is, Ticks& t) {
  bool any = false;
  for (std::size_t i = 0; i < t.size(); ++i) {
    std::string tok;
    if (!(is >> tok)) {
      t[i] = 0;
      continue;
    }
    uint64_t v = 0;
    if (to_u64(tok, v)) {
      t[i] = v;
      any = true;
    } else {
      t[i] = 0;
    }
  }
  return any;
}

bool ticks_zero(const Ticks& t) {
  for (uint64_t v : t)
    if (v != 0) return false;
  return true;
}

double diff(uint64_t cur, uint64_t prev) {
  return cur >= prev ? static_cast<double>(cur - prev) : 0.0;
}

Pct percentages(const Ticks& prev, const Ticks& cur) {
  double app = diff(cur[0], prev[0]) + diff(cur[1], prev[1]);
  double kern = diff(cur[2], prev[2]);
  double idle = diff(cur[3], prev[3]);
  double io = diff(cur[4], prev[4]);
  double irq = diff(cur[5], prev[5]) + diff(cur[6], prev[6]);
  double steal = diff(cur[7], prev[7]);
  double sum = app + kern + io + irq + idle + steal;
  Pct p;
  if (sum <= 0) return p;
  p.app = 100.0 * app / sum;
  p.kern = 100.0 * kern / sum;
  p.io = 100.0 * io / sum;
  p.irq = 100.0 * irq / sum;
  p.idle = 100.0 * (idle + steal) / sum;
  p.util = 100.0 * (app + kern + io + irq) / sum;
  return p;
}

// ---------------------------------------------------------------- temperatures
struct TempScan {
  std::map<int, double> per_core;
  double package = 0;
  bool has_package = false;
};

void note_package(TempScan& ts, double c) {
  if (c > 0 && (!ts.has_package || c > ts.package)) {
    ts.package = c;
    ts.has_package = true;
  }
}

void scan_cpu_temps(TempScan& ts) {
  for (const std::string& hw : list_dir("/sys/class/hwmon", "hwmon")) {
    std::string base = "/sys/class/hwmon/" + hw;
    std::string chip = read_first_line(base + "/name");
    bool cpu_chip = (chip == "coretemp" || chip == "k10temp" || chip == "zenpower" ||
                     chip == "cpu_thermal" || chip == "soc_thermal");
    for (int n = 1; n <= 64; ++n) {
      std::string num = std::to_string(n);
      std::string in = base + "/temp" + num + "_input";
      uint64_t milli = read_u64_file(in);
      if (milli == 0) continue;
      double c = static_cast<double>(milli) / 1000.0;
      if (c <= 0 || c > 200) continue;
      std::string label = read_first_line(base + "/temp" + num + "_label");
      bool used = false;
      if (label.rfind("Core ", 0) == 0) {
        int id = to_int(label.substr(5), -1);
        if (id >= 0) {
          ts.per_core[id] = c;
          used = true;
        }
      }
      if (!used && (label.find("Package") != std::string::npos || label == "Tctl" ||
                    label == "Tdie" || label.find("Physical") != std::string::npos)) {
        note_package(ts, c);
        used = true;
      }
      if (!used && cpu_chip) note_package(ts, c);
    }
  }
  if (!ts.has_package && ts.per_core.empty()) {
    for (const std::string& zone : list_dir("/sys/class/thermal", "thermal_zone")) {
      std::string base = "/sys/class/thermal/" + zone;
      std::string type = read_first_line(base + "/type");
      if (type.find("x86_pkg") == std::string::npos && type.find("cpu") == std::string::npos &&
          type.find("soc") == std::string::npos && type.find("pkg") == std::string::npos &&
          type.find("acpitz") == std::string::npos)
        continue;
      double c = static_cast<double>(read_u64_file(base + "/temp")) / 1000.0;
      if (c > 0 && c <= 200) note_package(ts, c);
    }
  }
}
// ---------------------------------------------------------------- per-process stat
struct RawProc {
  int pid = 0;
  std::string comm;
  char state = 'R';
  int threads = 1;
  int processor = -1;
  uint64_t utime = 0, stime = 0, starttime = 0, vsize = 0, rss_pages = 0;
};

bool parse_proc_stat(const std::string& content, RawProc& r) {
  std::size_t l = content.find('(');
  std::size_t rp = content.rfind(')');
  if (l == std::string::npos || rp == std::string::npos || rp <= l) return false;
  r.comm = content.substr(l + 1, rp - l - 1);
  std::istringstream is(content.substr(rp + 1));
  std::vector<std::string> tok;
  std::string t;
  while (is >> t) tok.push_back(t);
  if (tok.size() < 22) return false;
  r.state = tok[0].empty() ? 'R' : tok[0][0];
  uint64_t v = 0;
  if (to_u64(tok[11], v)) r.utime = v;
  if (to_u64(tok[12], v)) r.stime = v;
  r.threads = to_int(tok[17], 1);
  if (to_u64(tok[19], v)) r.starttime = v;
  if (to_u64(tok[20], v)) r.vsize = v;
  if (to_u64(tok[21], v)) r.rss_pages = v;
  if (tok.size() > 36) r.processor = to_int(tok[36], -1);
  return true;
}

struct TickPrev {
  uint64_t ut = 0, st = 0, start = 0;
};

std::unordered_map<int, TickPrev>& proc_prev_ticks() {
  static std::unordered_map<int, TickPrev> m;
  return m;
}

double read_uptime_s() {
  std::ifstream f("/proc/uptime");
  double up = 0;
  if (f >> up && up > 0) return up;
  std::ifstream st("/proc/stat");
  std::string line;
  uint64_t btime = 0;
  while (std::getline(st, line)) {
    if (line.rfind("btime ", 0) != 0) continue;
    std::istringstream is(line.substr(6));
    is >> btime;
    break;
  }
  if (btime > 0) {
    std::time_t now = std::time(nullptr);
    if (static_cast<uint64_t>(now) > btime) return static_cast<double>(now - btime);
  }
  return 0;
}

std::unordered_map<int, double> cpuinfo_mhz() {
  std::unordered_map<int, double> out;
  std::ifstream f("/proc/cpuinfo");
  std::string line;
  int cur = -1;
  while (std::getline(f, line)) {
    std::size_t c = line.find(':');
    if (c == std::string::npos) continue;
    std::string key = trim(line.substr(0, c));
    std::string val = trim(line.substr(c + 1));
    if (key == "processor") {
      cur = to_int(val, -1);
    } else if (key == "cpu MHz" && cur >= 0) {
      char* end = nullptr;
      double m = std::strtod(val.c_str(), &end);
      if (end != val.c_str()) out[cur] = m;
    }
  }
  return out;
}

}  // namespace

void CpuCollector::collect(Snapshot& s, double dt) {
  if (dt <= 0.0001) dt = 0.001;
  const long hz = sysconf(_SC_CLK_TCK);
  const double clk = hz > 0 ? static_cast<double>(hz) : 100.0;
  const long ps = sysconf(_SC_PAGESIZE);
  const uint64_t page = ps > 0 ? static_cast<uint64_t>(ps) : 4096ULL;
  const double uptime_s = read_uptime_s();

  std::array<uint64_t, 8> agg{};
  bool have_agg = false;
  std::vector<std::pair<int, Ticks>> cpus;
  {
    std::ifstream f("/proc/stat");
    std::string line;
    while (std::getline(f, line)) {
      if (line.rfind("cpu", 0) != 0) continue;
      std::istringstream is(line);
      std::string tag;
      is >> tag;
      Ticks t{};
      if (!parse_ticks(is, t)) continue;
      if (tag == "cpu") {
        agg = t;
        have_agg = true;
      } else if (tag.size() > 3 && std::isdigit(static_cast<unsigned char>(tag[3]))) {
        cpus.push_back(std::make_pair(to_int(tag.substr(3)), t));
      }
    }
  }
  std::sort(cpus.begin(), cpus.end(),
            [](const std::pair<int, Ticks>& a, const std::pair<int, Ticks>& b) { return a.first < b.first; });

  int ncpu = static_cast<int>(cpus.size());
  if (ncpu <= 0) {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    ncpu = n > 0 ? static_cast<int>(n) : 1;
  }
  s.ncpu = ncpu;

  const bool prev_ok = have_;
  const std::size_t old_size = prev_.size();
  std::size_t need = 1;
  for (const auto& c : cpus) need = std::max(need, static_cast<std::size_t>(c.first) + std::size_t(2));
  if (prev_.size() < need) prev_.resize(need);

  s.cpu = CpuSample{};
  if (prev_ok && have_agg && old_size > 0 && !ticks_zero(prev_[0])) {
    Pct p = percentages(prev_[0], agg);
    s.cpu.app = p.app;
    s.cpu.kern = p.kern;
    s.cpu.io = p.io;
    s.cpu.irq = p.irq;
    s.cpu.idle = p.idle;
    s.cpu.total = p.app + p.kern + p.io + p.irq;
  }
  prev_[0] = agg;

  TempScan temps;
  scan_cpu_temps(temps);
  std::unordered_map<int, double> cpuinfo = cpuinfo_mhz();

  s.cores.clear();
  s.cores.reserve(cpus.size());
  for (const auto& c : cpus) {
    const int id = c.first;
    const std::size_t idx = static_cast<std::size_t>(id) + 1;
    CoreInfo ci;
    ci.id = id;
    if (prev_ok && idx < old_size && !ticks_zero(prev_[idx])) {
      Pct p = percentages(prev_[idx], c.second);
      ci.app = p.app;
      ci.kern = p.kern;
      ci.io = p.io;
      ci.irq = p.irq;
      ci.idle = p.idle;
      ci.util = p.util;
    }
    prev_[idx] = c.second;

    std::string base = "/sys/devices/system/cpu/cpu" + std::to_string(id);
    uint64_t pkg = read_u64_file(base + "/topology/physical_package_id");
    ci.cluster = static_cast<int>(pkg);
    uint64_t khz = read_u64_file(base + "/cpufreq/scaling_cur_freq");
    if (khz > 0) {
      ci.mhz = static_cast<double>(khz) / 1000.0;
    } else {
      auto it = cpuinfo.find(id);
      if (it != cpuinfo.end()) ci.mhz = it->second;
    }
    ci.max_mhz = static_cast<double>(read_u64_file(base + "/cpufreq/cpuinfo_max_freq")) / 1000.0;
    ci.min_mhz = static_cast<double>(read_u64_file(base + "/cpufreq/cpuinfo_min_freq")) / 1000.0;
    ci.governor = read_first_line(base + "/cpufreq/scaling_governor");
    auto tc = temps.per_core.find(id);
    if (tc != temps.per_core.end()) {
      ci.temp_c = tc->second;
    } else if (temps.has_package) {
      ci.temp_c = temps.package;
    }
    s.cores.push_back(std::move(ci));
  }
  have_ = true;

  s.procs.clear();
  auto& pticks = proc_prev_ticks();
  const double cap = 100.0 * static_cast<double>(s.ncpu > 0 ? s.ncpu : 1);
  int scanned = 0;
  DIR* d = opendir("/proc");
  if (d) {
    while (struct dirent* e = readdir(d)) {
      if (scanned >= 400) break;
      const char* nm = e->d_name;
      if (!all_digits(nm)) continue;
      ++scanned;
      const int pid = to_int(nm);
      const std::string base = std::string("/proc/") + nm;
      RawProc rp;
      rp.pid = pid;
      std::string stat = read_all(base + "/stat");
      if (stat.empty() || !parse_proc_stat(stat, rp)) continue;

      if (rp.rss_pages == 0) {
        std::ifstream sm(base + "/statm");
        uint64_t size = 0, resident = 0;
        if (sm >> size >> resident) rp.rss_pages = resident;
      }
      if (rp.comm.empty()) {
        std::string cmd = read_all(base + "/cmdline");
        if (!cmd.empty()) {
          std::size_t z = cmd.find('\0');
          std::string arg = cmd.substr(0, z);
          std::size_t slash = arg.rfind('/');
          rp.comm = (slash == std::string::npos) ? arg : arg.substr(slash + 1);
        }
        if (rp.comm.empty()) rp.comm = "?";
      }

      double cpu = 0;
      auto it = pticks.find(pid);
      if (prev_ok && it != pticks.end() && it->second.start == rp.starttime) {
        double du = diff(rp.utime, it->second.ut);
        double ds = diff(rp.stime, it->second.st);
        cpu = 100.0 * (du + ds) / clk / dt;
        if (cpu < 0) cpu = 0;
        if (cpu > cap) cpu = cap;
      }
      pticks[pid] = TickPrev{rp.utime, rp.stime, rp.starttime};

      ProcInfo p;
      p.pid = pid;
      p.name = rp.comm;
      p.state = rp.state;
      p.threads = rp.threads > 0 ? rp.threads : 1;
      p.core = rp.processor;
      p.cpu_pct = cpu;
      p.rss = rp.rss_pages * page;
      p.vsz = rp.vsize;
      double started = static_cast<double>(rp.starttime) / clk;
      p.age_s = uptime_s > started ? uptime_s - started : 0;

      ProcTrack& t = track_[pid];
      t.samples += 1.0;
      t.avg += (cpu - t.avg) / t.samples;
      if (cpu > t.peak) t.peak = cpu;
      t.spark.push_back(cpu);
      if (t.spark.size() > 40) t.spark.erase(t.spark.begin(), t.spark.end() - 40);
      p.cpu_avg = t.avg;
      p.cpu_peak = t.peak;
      p.spark = t.spark;

      s.procs.push_back(std::move(p));
    }
    closedir(d);
  }

  if (pticks.size() > 20000) pticks.clear();
  if (track_.size() > 20000) track_.clear();

  std::sort(s.procs.begin(), s.procs.end(),
            [](const ProcInfo& a, const ProcInfo& b) { return a.cpu_pct > b.cpu_pct; });
  if (s.procs.size() > 160) s.procs.resize(160);
}

}  // namespace ct
