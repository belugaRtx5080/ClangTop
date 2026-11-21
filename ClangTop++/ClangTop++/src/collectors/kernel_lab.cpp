// ClangTop++ - kernel lab implementation
#include "kernel_lab.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace ct {
namespace {

std::string trim_copy(std::string s) {
  auto not_space = [](unsigned char c) { return !std::isspace(c); };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
  s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
  return s;
}

std::string lower_copy(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

bool file_exists_regular(const std::string& p) {
  std::error_code ec;
  return fs::exists(p, ec) && fs::is_regular_file(p, ec);
}

bool dir_exists(const std::string& p) {
  std::error_code ec;
  return fs::exists(p, ec) && fs::is_directory(p, ec);
}

std::string uname_r_str() {
  struct utsname u {};
  if (::uname(&u) == 0) return std::string(u.release);
  return {};
}

std::string uname_a_str() {
  struct utsname u {};
  if (::uname(&u) == 0) {
    return std::string(u.sysname) + " " + u.nodename + " " + u.release + " " + u.version + " " + u.machine;
  }
  return {};
}

std::string detect_flavor(const std::string& ver, const std::string& path) {
  std::string s = lower_copy(ver + " " + path);
  struct Tag { const char* key; const char* label; };
  static const Tag tags[] = {
      {"cachyos", "cachyos-bore"}, {"bore", "bore"}, {"liquorix", "liquorix"}, {"zen", "zen"},
      {"xanmod", "xanmod"}, {"hardened", "hardened"}, {"realtime", "rt"}, {"-rt", "rt"},
      {"lts", "lts"}, {"clear", "clear"}, {"gki", "gki"}, {"android", "android"},
      {"generic", "generic"}, {"server", "server"}, {"cloud", "cloud"},
  };
  for (const auto& t : tags) {
    if (s.find(t.key) != std::string::npos) return t.label;
  }
  return "stock";
}

int count_modules(const std::string& modules_dir) {
  if (!dir_exists(modules_dir)) return -1;
  int count = 0;
  std::error_code ec;
  for (fs::recursive_directory_iterator it(modules_dir, fs::directory_options::skip_permission_denied, ec), end;
       !ec && it != end; it.increment(ec)) {
    if (ec) break;
    if (!it->is_regular_file(ec) || ec) continue;
    auto ext = it->path().extension().string();
    auto name = it->path().filename().string();
    if (ext == ".ko" || ext == ".xz" || ext == ".gz" || ext == ".zst" || name.find(".ko") != std::string::npos) ++count;
  }
  return count;
}

uint64_t file_size_u64(const std::string& path) {
  std::error_code ec;
  auto sz = fs::file_size(path, ec);
  if (ec) return 0;
  return static_cast<uint64_t>(sz);
}

std::string json_escape(const std::string& in) {
  std::string out;
  out.reserve(in.size() + 8);
  for (unsigned char c : in) {
    switch (c) {
      case '\\': out += "\\\\"; break;
      case '"': out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out += static_cast<char>(c);
        }
    }
  }
  return out;
}

bool extract_json_string(const std::string& s, const std::string& key, std::string& out) {
  std::string pat = "\"" + key + "\"";
  auto pos = s.find(pat);
  if (pos == std::string::npos) return false;
  pos = s.find(':', pos + pat.size());
  if (pos == std::string::npos) return false;
  pos = s.find('"', pos + 1);
  if (pos == std::string::npos) return false;
  std::string val;
  for (std::size_t i = pos + 1; i < s.size(); ++i) {
    char c = s[i];
    if (c == '\\' && i + 1 < s.size()) {
      char n = s[++i];
      switch (n) {
        case '"': val += '"'; break;
        case '\\': val += '\\'; break;
        case 'n': val += '\n'; break;
        case 'r': val += '\r'; break;
        case 't': val += '\t'; break;
        default: val += n; break;
      }
    } else if (c == '"') {
      out = val;
      return true;
    } else {
      val += c;
    }
  }
  return false;
}

bool extract_json_double(const std::string& s, const std::string& key, double& out) {
  std::string pat = "\"" + key + "\"";
  auto pos = s.find(pat);
  if (pos == std::string::npos) return false;
  pos = s.find(':', pos + pat.size());
  if (pos == std::string::npos) return false;
  ++pos;
  while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos]))) ++pos;
  const char* start = s.c_str() + pos;
  char* endp = nullptr;
  errno = 0;
  double v = std::strtod(start, &endp);
  if (endp == start) return false;
  out = v;
  return true;
}

bool extract_json_bool(const std::string& s, const std::string& key, bool& out) {
  std::string pat = "\"" + key + "\"";
  auto pos = s.find(pat);
  if (pos == std::string::npos) return false;
  pos = s.find(':', pos + pat.size());
  if (pos == std::string::npos) return false;
  auto t = s.find("true", pos);
  auto f = s.find("false", pos);
  if (t != std::string::npos && (f == std::string::npos || t < f)) { out = true; return true; }
  if (f != std::string::npos) { out = false; return true; }
  return false;
}

std::string extract_json_object(const std::string& s, const std::string& key) {
  std::string pat = "\"" + key + "\"";
  auto pos = s.find(pat);
  if (pos == std::string::npos) return {};
  pos = s.find('{', pos + pat.size());
  if (pos == std::string::npos) return {};
  int depth = 0;
  bool in_str = false;
  for (std::size_t i = pos; i < s.size(); ++i) {
    char c = s[i];
    if (in_str) {
      if (c == '\\') { ++i; continue; }
      if (c == '"') in_str = false;
      continue;
    }
    if (c == '"') { in_str = true; continue; }
    if (c == '{') ++depth;
    else if (c == '}') {
      --depth;
      if (depth == 0) return s.substr(pos, i - pos + 1);
    }
  }
  return {};
}

void parse_json_map(const std::string& obj, std::map<std::string, std::string>& out) {
  std::size_t i = 0;
  while (i < obj.size()) {
    auto q1 = obj.find('"', i);
    if (q1 == std::string::npos) break;
    auto q2 = obj.find('"', q1 + 1);
    if (q2 == std::string::npos) break;
    std::string k = obj.substr(q1 + 1, q2 - q1 - 1);
    auto colon = obj.find(':', q2 + 1);
    if (colon == std::string::npos) break;
    auto q3 = obj.find('"', colon + 1);
    if (q3 == std::string::npos) break;
    std::string v;
    std::size_t j = q3 + 1;
    for (; j < obj.size(); ++j) {
      char c = obj[j];
      if (c == '\\' && j + 1 < obj.size()) {
        char n = obj[++j];
        if (n == 'n') v += '\n';
        else if (n == 'r') v += '\r';
        else if (n == 't') v += '\t';
        else v += n;
      } else if (c == '"') {
        break;
      } else {
        v += c;
      }
    }
    out[k] = v;
    i = j + 1;
  }
}

}  // namespace

KernelLab::KernelLab() {
  bench_.valid = false;
  bench_.running = false;
  refresh_installed();
  refresh_inspector(true);
}

KernelLab::~KernelLab() {
  stop_benchmark();
}

std::string KernelLab::now_string() {
  std::time_t t = std::time(nullptr);
  std::tm tm {};
#if defined(_WIN32)
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  char buf[64];
  std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
  return std::string(buf);
}

std::string KernelLab::sanitize_name(const std::string& in) {
  std::string out;
  out.reserve(in.size());
  for (unsigned char c : in) {
    if (std::isalnum(c) || c == '.' || c == '_' || c == '-') out += static_cast<char>(c);
    else out += '_';
  }
  if (out.empty()) out = "kernel";
  return out;
}

std::string KernelLab::read_text_file(const std::string& path, std::size_t max_bytes) {
  std::ifstream f(path, std::ios::in | std::ios::binary);
  if (!f) return {};
  std::string data;
  data.resize(max_bytes);
  f.read(&data[0], static_cast<std::streamsize>(max_bytes));
  data.resize(static_cast<std::size_t>(f.gcount()));
  // gzip magic -> not plain text config
  if (data.size() >= 2 && static_cast<unsigned char>(data[0]) == 0x1f && static_cast<unsigned char>(data[1]) == 0x8b) {
    return {};
  }
  return data;
}

bool KernelLab::write_text_file(const std::string& path, const std::string& data) {
  std::ofstream f(path, std::ios::out | std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f.write(data.data(), static_cast<std::streamsize>(data.size()));
  return static_cast<bool>(f);
}

std::string KernelLab::active_kernel() const {
  std::lock_guard<std::mutex> lk(mu_);
  if (!inspector_.active_kernel.empty()) return inspector_.active_kernel;
  return uname_r_str();
}

void KernelLab::refresh_installed() {
  const std::string active = uname_r_str();
  std::map<std::string, InstalledKernel> by_ver;

  if (dir_exists("/boot")) {
    std::error_code ec;
    for (const auto& e : fs::directory_iterator("/boot", fs::directory_options::skip_permission_denied, ec)) {
      if (ec) break;
      std::error_code ec2;
      if (!e.is_regular_file(ec2) || ec2) continue;
      const std::string name = e.path().filename().string();
      if (name.rfind("vmlinuz-", 0) == 0) {
        std::string ver = name.substr(std::string("vmlinuz-").size());
        if (ver.empty()) continue;
        auto& k = by_ver[ver];
        k.version = ver;
        k.image_path = e.path().string();
        k.image_size = file_size_u64(k.image_path);
      } else if (name.rfind("vmlinux-", 0) == 0) {
        std::string ver = name.substr(std::string("vmlinux-").size());
        auto& k = by_ver[ver];
        if (k.version.empty()) k.version = ver;
        if (k.image_path.empty()) { k.image_path = e.path().string(); k.image_size = file_size_u64(k.image_path); }
      } else if (name.rfind("kernel", 0) == 0) {
        std::string ver = active.empty() ? name : active;
        auto& k = by_ver[ver];
        if (k.version.empty()) k.version = ver;
        if (k.image_path.empty()) { k.image_path = e.path().string(); k.image_size = file_size_u64(k.image_path); }
      }
    }
  }

  if (dir_exists("/lib/modules")) {
    std::error_code ec;
    for (const auto& e : fs::directory_iterator("/lib/modules", fs::directory_options::skip_permission_denied, ec)) {
      if (ec) break;
      std::error_code ec2;
      if (!e.is_directory(ec2) || ec2) continue;
      std::string ver = e.path().filename().string();
      auto& k = by_ver[ver];
      if (k.version.empty()) k.version = ver;
      k.modules_path = e.path().string();
      k.module_count = count_modules(k.modules_path);
      if (k.flavor.empty()) k.flavor = detect_flavor(ver, k.modules_path);
    }
  }

  // BSD-ish locations
  if (dir_exists("/boot/kernel")) {
    std::string ver = active.empty() ? std::string("bsd-kernel") : active;
    auto& k = by_ver[ver];
    if (k.version.empty()) k.version = ver;
    if (k.image_path.empty() && file_exists_regular("/boot/kernel/kernel")) {
      k.image_path = "/boot/kernel/kernel";
      k.image_size = file_size_u64(k.image_path);
    }
    if (k.modules_path.empty()) k.modules_path = "/boot/kernel";
  }

  std::vector<InstalledKernel> list;
  for (auto& kv : by_ver) {
    auto& k = kv.second;
    if (k.flavor.empty()) k.flavor = detect_flavor(k.version, k.image_path);
    k.active = (!active.empty() && k.version == active);
    if (k.module_count < 0 && !k.modules_path.empty()) k.module_count = count_modules(k.modules_path);
    list.push_back(k);
  }
  std::sort(list.begin(), list.end(), [&](const InstalledKernel& a, const InstalledKernel& b) {
    if (a.active != b.active) return a.active > b.active;
    return a.version > b.version;
  });
  if (list.empty() && !active.empty()) {
    InstalledKernel k;
    k.version = active;
    k.flavor = detect_flavor(active, "");
    k.active = true;
    list.push_back(k);
  }

  std::lock_guard<std::mutex> lk(mu_);
  installed_ = std::move(list);
  inspector_.active_kernel = active;
}

std::vector<MitigationEntry> KernelLab::collect_mitigations() {
  std::vector<MitigationEntry> out;
  const std::string base = "/sys/devices/system/cpu/vulnerabilities";
  if (!dir_exists(base)) return out;
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(base, fs::directory_options::skip_permission_denied, ec)) {
    if (ec) break;
    std::error_code ec2;
    if (!e.is_regular_file(ec2) || ec2) continue;
    MitigationEntry m;
    m.name = e.path().filename().string();
    std::ifstream f(e.path());
    if (f) {
      std::string line;
      std::getline(f, line);
      m.status = trim_copy(line);
    }
    if (m.status.empty()) m.status = "(unreadable)";
    out.push_back(std::move(m));
  }
  std::sort(out.begin(), out.end(), [](const MitigationEntry& a, const MitigationEntry& b) { return a.name < b.name; });
  return out;
}

std::string KernelLab::config_fallback_path(const std::string& uname_r) {
  std::vector<std::string> candidates;
  if (!uname_r.empty()) {
    candidates.push_back("/boot/config-" + uname_r);
    candidates.push_back("/usr/lib/modules/" + uname_r + "/build/.config");
    candidates.push_back("/lib/modules/" + uname_r + "/build/.config");
  }
  for (const auto& p : candidates) {
    if (file_exists_regular(p)) return p;
  }
  // try /proc/config.gz only if plain text (unlikely) - read_text_file rejects gzip
  if (file_exists_regular("/proc/config.gz")) {
    auto txt = read_text_file("/proc/config.gz", 4096);
    if (!txt.empty()) return "/proc/config.gz";
  }
  if (dir_exists("/boot")) {
    std::error_code ec;
    for (const auto& e : fs::directory_iterator("/boot", fs::directory_options::skip_permission_denied, ec)) {
      if (ec) break;
      auto name = e.path().filename().string();
      if (name.rfind("config-", 0) == 0 && !uname_r.empty() && name.find(uname_r) != std::string::npos) {
        return e.path().string();
      }
    }
  }
  return {};
}

std::string KernelLab::detect_preemption(const std::string& cmdline, const std::string& config_path) {
  std::string cl = lower_copy(cmdline);
  if (cl.find("preempt=full") != std::string::npos) return "PREEMPT/Full (cmdline)";
  if (cl.find("preempt=rt") != std::string::npos || cl.find("preempt_rt") != std::string::npos) return "PREEMPT_RT (cmdline)";
  if (cl.find("preempt=voluntary") != std::string::npos) return "Voluntary (cmdline)";
  if (cl.find("preempt=none") != std::string::npos) return "None/Server (cmdline)";
  if (file_exists_regular("/sys/kernel/realtime")) {
    auto v = trim_copy(read_text_file("/sys/kernel/realtime", 64));
    if (v == "1") return "PREEMPT_RT";
  }
  if (!config_path.empty() && file_exists_regular(config_path)) {
    std::ifstream f(config_path);
    std::string line;
    bool preempt_rt = false, preempt = false, voluntary = false, none = false, dynamic = false, lazy = false;
    int lines = 0;
    while (std::getline(f, line) && lines++ < 50000) {
      if (line == "CONFIG_PREEMPT_RT=y") preempt_rt = true;
      else if (line == "CONFIG_PREEMPT=y") preempt = true;
      else if (line == "CONFIG_PREEMPT_VOLUNTARY=y") voluntary = true;
      else if (line == "CONFIG_PREEMPT_NONE=y") none = true;
      else if (line == "CONFIG_PREEMPT_DYNAMIC=y") dynamic = true;
      else if (line == "CONFIG_PREEMPT_LAZY=y") lazy = true;
    }
    if (preempt_rt) return "PREEMPT_RT (config)";
    if (dynamic && lazy) return "PREEMPT_DYNAMIC/lazy (config)";
    if (dynamic) return "PREEMPT_DYNAMIC (config)";
    if (preempt) return "Preemptible (config)";
    if (voluntary) return "Voluntary (config)";
    if (none) return "Non-preemptive (config)";
  }
  return "Unknown";
}

std::vector<SysctlParam> KernelLab::collect_sysctl() {
  std::vector<SysctlParam> out;
  const std::string root = "/proc/sys";
  if (!dir_exists(root)) return out;
  std::error_code ec;
  fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
  for (; !ec && it != end; it.increment(ec)) {
    if (ec) break;
    std::error_code ec2;
    if (!it->is_regular_file(ec2) || ec2) continue;
    std::string p = it->path().string();
    if (p.size() <= root.size() + 1) continue;
    std::string rel = p.substr(root.size() + 1);
    for (char& c : rel) if (c == '/') c = '.';
    SysctlParam sp;
    sp.key = rel;
    {
      std::ifstream f(p);
      if (f) {
        std::string line;
        std::getline(f, line);
        if (line.size() > 180) line.resize(180);
        sp.value = trim_copy(line);
      } else {
        sp.value = "(n/a)";
      }
    }
    out.push_back(std::move(sp));
    if (out.size() >= 5000) break;
  }
  std::sort(out.begin(), out.end(), [](const SysctlParam& a, const SysctlParam& b) { return a.key < b.key; });
  return out;
}

void KernelLab::refresh_inspector(bool force_sysctl) {
  const std::string ar = uname_r_str();
  KernelInspectorInfo info;
  info.active_kernel = ar;
  info.uname_a = uname_a_str();
  info.cmdline = trim_copy(read_text_file("/proc/cmdline", 64 * 1024));
  info.mitigations = collect_mitigations();
  const std::string cfg = config_fallback_path(ar);
  info.config_source = cfg.empty() ? std::string("(not found; /proc/config.gz may be absent/compressed)") : cfg;
  if (!cfg.empty()) {
    std::ifstream f(cfg);
    std::string line;
    int n = 0;
    std::string excerpt;
    while (std::getline(f, line) && n++ < 5000) {
      if (line.find("CONFIG_PREEMPT") != std::string::npos || line.find("CONFIG_HZ") != std::string::npos ||
          line.find("CONFIG_SCHED") != std::string::npos || line.find("CONFIG_NO_HZ") != std::string::npos) {
        if (!excerpt.empty()) excerpt += " | ";
        excerpt += trim_copy(line);
        if (excerpt.size() > 260) { excerpt.resize(260); excerpt += "..."; break; }
      }
    }
    info.config_excerpt = excerpt;
  }
  info.preemption_model = detect_preemption(info.cmdline, cfg);

  bool need_sysctl = force_sysctl;
  {
    std::lock_guard<std::mutex> lk(mu_);
    if (!sysctl_loaded_ || inspector_.sysctl_params.empty()) need_sysctl = true;
  }
  std::vector<SysctlParam> sysctl;
  if (need_sysctl) sysctl = collect_sysctl();

  std::lock_guard<std::mutex> lk(mu_);
  inspector_.active_kernel = info.active_kernel;
  inspector_.uname_a = info.uname_a;
  inspector_.cmdline = info.cmdline;
  inspector_.config_source = info.config_source;
  inspector_.config_excerpt = info.config_excerpt;
  inspector_.preemption_model = info.preemption_model;
  inspector_.mitigations = std::move(info.mitigations);
  if (need_sysctl) {
    inspector_.sysctl_params = std::move(sysctl);
    sysctl_loaded_ = true;
  }
}

void KernelLab::start_benchmark() {
  std::thread old;
  {
    std::lock_guard<std::mutex> lk(mu_);
    if (bench_.running) return;
    if (bench_thread_.joinable()) old = std::move(bench_thread_);
    stop_.store(false);
    bench_.running = true;
    bench_.valid = false;
    bench_.error.clear();
    bench_.elapsed_s = 0.0;
    bench_.ipc_gbps = 0.0;
    bench_.ctx_switch_ns = 0.0;
    bench_.syscall_mops = 0.0;
    bench_.vfs_gbps = 0.0;
    bench_.timestamp = now_string();
    bench_thread_ = std::thread([this]() { run_benchmark(); });
  }
  if (old.joinable()) old.join();
}

void KernelLab::stop_benchmark() {
  stop_.store(true);
  std::thread t;
  {
    std::lock_guard<std::mutex> lk(mu_);
    if (bench_thread_.joinable()) t = std::move(bench_thread_);
  }
  if (t.joinable()) t.join();
  std::lock_guard<std::mutex> lk(mu_);
  bench_.running = false;
}

std::vector<InstalledKernel> KernelLab::installed_kernels() const {
  std::lock_guard<std::mutex> lk(mu_);
  return installed_;
}

KernelInspectorInfo KernelLab::inspector() const {
  std::lock_guard<std::mutex> lk(mu_);
  return inspector_;
}

BenchmarkResult KernelLab::benchmark() const {
  std::lock_guard<std::mutex> lk(mu_);
  return bench_;
}

void KernelLab::run_benchmark() {
  using clock = std::chrono::steady_clock;
  auto t0 = clock::now();
  BenchmarkResult r;
  r.running = true;
  r.timestamp = now_string();
  try {
    // 1) IPC pipe throughput
    {
      int fds[2] = {-1, -1};
      if (::pipe(fds) == 0) {
        const std::size_t buf_sz = 1 << 16;
        std::vector<char> wbuf(buf_sz, 'x'), rbuf(buf_sz);
        const auto deadline = clock::now() + std::chrono::milliseconds(450);
        std::atomic<bool> done{false};
        std::atomic<uint64_t> bytes{0};
        std::thread reader([&]() {
          while (!done.load(std::memory_order_relaxed)) {
            ssize_t n = ::read(fds[0], rbuf.data(), rbuf.size());
            if (n > 0) bytes.fetch_add(static_cast<uint64_t>(n), std::memory_order_relaxed);
            else if (n == 0) break;
            else if (errno != EINTR && errno != EAGAIN) break;
          }
          for (;;) {
            ssize_t n = ::read(fds[0], rbuf.data(), rbuf.size());
            if (n > 0) bytes.fetch_add(static_cast<uint64_t>(n), std::memory_order_relaxed);
            else break;
          }
        });
        auto ws = clock::now();
        while (clock::now() < deadline && !stop_.load()) {
          ssize_t n = ::write(fds[1], wbuf.data(), wbuf.size());
          if (n < 0 && errno != EINTR && errno != EAGAIN) break;
        }
        done.store(true, std::memory_order_relaxed);
        ::close(fds[1]);
        reader.join();
        ::close(fds[0]);
        auto we = clock::now();
        double sec = std::chrono::duration<double>(we - ws).count();
        if (sec <= 0) sec = 1e-6;
        r.ipc_gbps = (static_cast<double>(bytes.load()) / sec) / (1024.0 * 1024.0 * 1024.0);
      }
    }

    // 2) Context switch / scheduler latency (pipe ping-pong)
    {
      int p1[2] = {-1, -1}, p2[2] = {-1, -1};
      if (::pipe(p1) == 0 && ::pipe(p2) == 0) {
        const int iters = 15000;
        auto start = clock::now();
        std::thread peer([&]() {
          char c = 0;
          for (int i = 0; i < iters && !stop_.load(); ++i) {
            if (::read(p1[0], &c, 1) <= 0) break;
            if (::write(p2[1], &c, 1) <= 0) break;
          }
        });
        char c = 'x';
        for (int i = 0; i < iters && !stop_.load(); ++i) {
          if (::write(p1[1], &c, 1) <= 0) break;
          if (::read(p2[0], &c, 1) <= 0) break;
        }
        peer.join();
        auto end = clock::now();
        ::close(p1[0]); ::close(p1[1]); ::close(p2[0]); ::close(p2[1]);
        double ns = std::chrono::duration<double, std::nano>(end - start).count();
        double switches = static_cast<double>(iters) * 2.0;
        r.ctx_switch_ns = switches > 0 ? (ns / switches) : 0.0;
      }
    }

    // 3) Syscall overhead (M calls/s)
    {
      const auto slice = std::chrono::milliseconds(450);
      const auto end = clock::now() + slice;
      uint64_t calls = 0;
      volatile pid_t sink = 0;
      struct timespec ts {};
      while (clock::now() < end && !stop_.load()) {
        for (int i = 0; i < 1024; ++i) {
          ::clock_gettime(CLOCK_MONOTONIC, &ts);
          sink = ::getpid();
          ++calls;
        }
      }
      (void)sink; (void)ts;
      double sec = std::chrono::duration<double>(slice).count();
      r.syscall_mops = (static_cast<double>(calls) / sec) / 1e6;
    }

    // 4) Page cache VFS throughput
    {
      std::string path = "/tmp/clangtop_vfs_bench_" + std::to_string(::getpid()) + ".bin";
      const std::size_t total = 32u * 1024u * 1024u;
      const std::size_t chunk = 1u << 20;
      std::vector<char> buf(chunk, 0x3c);
      uint64_t written = 0;
      auto w0 = clock::now();
      int fdw = ::open(path.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0600);
      if (fdw >= 0) {
        while (written < total && !stop_.load()) {
          std::size_t nbytes = std::min(chunk, total - static_cast<std::size_t>(written));
          ssize_t n = ::write(fdw, buf.data(), nbytes);
          if (n > 0) written += static_cast<uint64_t>(n);
          else if (n < 0 && errno == EINTR) continue;
          else break;
        }
        ::close(fdw);
      }
      auto w1 = clock::now();
      uint64_t readb = 0;
      auto r0 = clock::now();
      int fdr = ::open(path.c_str(), O_RDONLY);
      if (fdr >= 0) {
        for (;;) {
          if (stop_.load()) break;
          ssize_t n = ::read(fdr, buf.data(), buf.size());
          if (n > 0) readb += static_cast<uint64_t>(n);
          else if (n == 0) break;
          else if (errno == EINTR) continue;
          else break;
        }
        ::close(fdr);
      }
      auto r1 = clock::now();
      ::unlink(path.c_str());
      double wsec = std::chrono::duration<double>(w1 - w0).count();
      double rsec = std::chrono::duration<double>(r1 - r0).count();
      double wg = wsec > 0 ? (static_cast<double>(written) / wsec) / (1024.0 * 1024.0 * 1024.0) : 0.0;
      double rg = rsec > 0 ? (static_cast<double>(readb) / rsec) / (1024.0 * 1024.0 * 1024.0) : 0.0;
      r.vfs_gbps = std::max(wg, rg);
    }

    r.valid = true;
  } catch (...) {
    r.valid = false;
    r.error = "benchmark exception";
  }
  r.elapsed_s = std::chrono::duration<double>(clock::now() - t0).count();
  r.running = false;
  r.timestamp = now_string();

  std::lock_guard<std::mutex> lk(mu_);
  bench_ = r;
}

std::string KernelLab::snapshot_dir() const {
  const char* home = std::getenv("HOME");
  std::string base = (home && *home) ? std::string(home) : std::string("/tmp");
  return base + "/.config/clangtop/snapshots";
}

std::vector<std::string> KernelLab::list_snapshots() const {
  std::vector<std::string> out;
  std::string dir = snapshot_dir();
  std::error_code ec;
  if (!fs::exists(dir, ec)) return out;
  for (const auto& e : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
    if (ec) break;
    std::error_code ec2;
    if (!e.is_regular_file(ec2) || ec2) continue;
    auto p = e.path();
    if (p.extension() == ".json") out.push_back(p.stem().string());
  }
  std::sort(out.begin(), out.end());
  return out;
}

KernelSnapshot KernelLab::make_snapshot_locked(const KernelInspectorInfo& inspector, const BenchmarkResult& bench) {
  KernelSnapshot s;
  s.name = inspector.active_kernel.empty() ? uname_r_str() : inspector.active_kernel;
  s.saved_at = now_string();
  s.cmdline = inspector.cmdline;
  s.preemption_model = inspector.preemption_model;
  for (const auto& m : inspector.mitigations) s.mitigations[m.name] = m.status;
  // keep a useful subset: include priority keys then cap overall
  static const char* important[] = {
      "kernel.pid_max", "kernel.threads-max", "kernel.sched_autogroup_enabled", "kernel.sched_latency_ns",
      "kernel.sched_min_granularity_ns", "kernel.sched_wakeup_granularity_ns", "vm.swappiness", "vm.dirty_ratio",
      "vm.dirty_background_ratio", "vm.vfs_cache_pressure", "fs.file-max", "net.core.somaxconn",
      "net.core.netdev_max_backlog", "net.ipv4.tcp_congestion_control", "kernel.unprivileged_bpf_disabled",
      "kernel.kptr_restrict", "kernel.yama.ptrace_scope", "kernel.randomize_va_space"};
  for (const char* k : important) {
    for (const auto& kv : inspector.sysctl_params) {
      if (kv.key == k) { s.sysctl[kv.key] = kv.value; break; }
    }
  }
  if (s.sysctl.size() < 200) {
    for (const auto& kv : inspector.sysctl_params) {
      if (s.sysctl.count(kv.key)) continue;
      s.sysctl[kv.key] = kv.value;
      if (s.sysctl.size() >= 200) break;
    }
  }
  s.bench = bench;
  s.bench.running = false;
  return s;
}

std::string KernelLab::to_json(const KernelSnapshot& s) {
  std::ostringstream o;
  o.setf(std::ios::fixed);
  o.precision(6);
  o << "{\n";
  o << "  \"name\": \"" << json_escape(s.name) << "\",\n";
  o << "  \"saved_at\": \"" << json_escape(s.saved_at) << "\",\n";
  o << "  \"cmdline\": \"" << json_escape(s.cmdline) << "\",\n";
  o << "  \"preemption_model\": \"" << json_escape(s.preemption_model) << "\",\n";
  o << "  \"mitigations\": {";
  bool first = true;
  for (const auto& kv : s.mitigations) {
    if (!first) o << ',';
    o << "\n    \"" << json_escape(kv.first) << "\": \"" << json_escape(kv.second) << "\"";
    first = false;
  }
  if (!s.mitigations.empty()) o << "\n  ";
  o << "},\n";
  o << "  \"sysctl\": {";
  first = true;
  for (const auto& kv : s.sysctl) {
    if (!first) o << ',';
    o << "\n    \"" << json_escape(kv.first) << "\": \"" << json_escape(kv.second) << "\"";
    first = false;
  }
  if (!s.sysctl.empty()) o << "\n  ";
  o << "},\n";
  o << "  \"bench\": {\n";
  o << "    \"valid\": " << (s.bench.valid ? "true" : "false") << ",\n";
  o << "    \"elapsed_s\": " << s.bench.elapsed_s << ",\n";
  o << "    \"ipc_gbps\": " << s.bench.ipc_gbps << ",\n";
  o << "    \"ctx_switch_ns\": " << s.bench.ctx_switch_ns << ",\n";
  o << "    \"syscall_mops\": " << s.bench.syscall_mops << ",\n";
  o << "    \"vfs_gbps\": " << s.bench.vfs_gbps << ",\n";
  o << "    \"error\": \"" << json_escape(s.bench.error) << "\",\n";
  o << "    \"timestamp\": \"" << json_escape(s.bench.timestamp) << "\"\n";
  o << "  }\n";
  o << "}\n";
  return o.str();
}

bool KernelLab::from_json(const std::string& text, KernelSnapshot& out, std::string* err) {
  KernelSnapshot s;
  extract_json_string(text, "name", s.name);
  extract_json_string(text, "saved_at", s.saved_at);
  extract_json_string(text, "cmdline", s.cmdline);
  extract_json_string(text, "preemption_model", s.preemption_model);
  parse_json_map(extract_json_object(text, "mitigations"), s.mitigations);
  parse_json_map(extract_json_object(text, "sysctl"), s.sysctl);
  std::string bench_obj = extract_json_object(text, "bench");
  if (!bench_obj.empty()) {
    extract_json_bool(bench_obj, "valid", s.bench.valid);
    extract_json_double(bench_obj, "elapsed_s", s.bench.elapsed_s);
    extract_json_double(bench_obj, "ipc_gbps", s.bench.ipc_gbps);
    extract_json_double(bench_obj, "ctx_switch_ns", s.bench.ctx_switch_ns);
    extract_json_double(bench_obj, "syscall_mops", s.bench.syscall_mops);
    extract_json_double(bench_obj, "vfs_gbps", s.bench.vfs_gbps);
    extract_json_string(bench_obj, "error", s.bench.error);
    extract_json_string(bench_obj, "timestamp", s.bench.timestamp);
  }
  if (s.name.empty()) {
    if (err) *err = "snapshot missing name";
    return false;
  }
  out = std::move(s);
  return true;
}

bool KernelLab::save_snapshot(std::string* err) {
  KernelSnapshot snap;
  std::string dir = snapshot_dir();
  {
    std::lock_guard<std::mutex> lk(mu_);
    snap = make_snapshot_locked(inspector_, bench_);
  }
  if (snap.name.empty()) snap.name = "unknown-kernel";
  std::error_code ec;
  fs::create_directories(dir, ec);
  if (ec) {
    if (err) *err = "cannot create snapshot dir: " + ec.message();
    return false;
  }
  std::string path = dir + "/" + sanitize_name(snap.name) + ".json";
  if (!write_text_file(path, to_json(snap))) {
    if (err) *err = "failed to write snapshot: " + path;
    return false;
  }
  return true;
}

bool KernelLab::load_snapshot(const std::string& name, KernelSnapshot& out, std::string* err) const {
  std::string dir = snapshot_dir();
  std::string path = dir + "/" + sanitize_name(name) + ".json";
  std::ifstream f(path, std::ios::in | std::ios::binary);
  if (!f) {
    path = dir + "/" + name + ".json";
    f.open(path, std::ios::in | std::ios::binary);
  }
  if (!f) {
    if (err) *err = "snapshot not found: " + name;
    return false;
  }
  std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  if (!from_json(text, out, err)) return false;
  out.name = name; // keep requested/listed name stable for UI
  return true;
}

KernelDiff KernelLab::diff_snapshots(const std::string& a_name, const std::string& b_name) const {
  KernelDiff d;
  d.a_name = a_name;
  d.b_name = b_name;
  KernelSnapshot a, b;
  std::string ea, eb;
  if (!load_snapshot(a_name, a, &ea)) { d.error = ea; return d; }
  if (!load_snapshot(b_name, b, &eb)) { d.error = eb; return d; }
  d.ok = true;

  auto add = [&](const std::string& name, double va, double vb, bool lower_better) {
    DiffMetric m;
    m.name = name;
    m.a = va;
    m.b = vb;
    m.has_a = a.bench.valid;
    m.has_b = b.bench.valid;
    m.lower_is_better = lower_better;
    if (std::fabs(va) > 1e-12) m.delta_pct = ((vb - va) / va) * 100.0;
    else if (std::fabs(vb) > 1e-12) m.delta_pct = 100.0;
    else m.delta_pct = 0.0;
    d.metrics.push_back(m);
  };

  add("IPC GB/s", a.bench.ipc_gbps, b.bench.ipc_gbps, false);
  add("Ctx switch ns", a.bench.ctx_switch_ns, b.bench.ctx_switch_ns, true);
  add("Syscalls M/s", a.bench.syscall_mops, b.bench.syscall_mops, false);
  add("VFS GB/s", a.bench.vfs_gbps, b.bench.vfs_gbps, false);

  d.notes.push_back("A: " + a.name + "  preempt=" + a.preemption_model);
  d.notes.push_back("B: " + b.name + "  preempt=" + b.preemption_model);
  if (!a.cmdline.empty() || !b.cmdline.empty()) {
    d.notes.push_back(a.cmdline == b.cmdline ? "cmdline: identical" : "cmdline: differs");
  }
  return d;
}

}  // namespace ct
