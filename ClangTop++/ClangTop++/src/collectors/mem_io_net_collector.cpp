// ClangTop++ - memory, network and disk I/O collector.
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <unistd.h>

#include "../core/platform_collector.hpp"

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

double diff_rate(uint64_t cur, uint64_t prev, double dt) {
  if (cur < prev || dt <= 0) return 0.0;
  return static_cast<double>(cur - prev) / dt;
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

uint64_t memval(const std::unordered_map<std::string, uint64_t>& m, const char* key) {
  auto it = m.find(key);
  return it == m.end() ? 0 : it->second;
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

void count_sockets(const char* path, bool tcp, int& n, int& estab, int& tw) {
  std::ifstream f(path);
  std::string line;
  bool first = true;
  while (std::getline(f, line)) {
    if (first) {
      first = false;
      continue;
    }
    std::istringstream is(line);
    std::string sl, local, remote, state;
    if (!(is >> sl >> local >> remote >> state)) continue;
    ++n;
    if (!tcp) continue;
    if (state == "01") ++estab;
    else if (state == "06") ++tw;
  }
}

bool whole_disk(const std::string& name) {
  auto all_alpha = [](const std::string& s, std::size_t from) {
    for (std::size_t i = from; i < s.size(); ++i)
      if (!std::isalpha(static_cast<unsigned char>(s[i]))) return false;
    return s.size() > from;
  };
  auto all_digits_from = [](const std::string& s, std::size_t from) {
    if (s.size() <= from) return false;
    for (std::size_t i = from; i < s.size(); ++i)
      if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
    return true;
  };
  if (name.size() >= 3 && name.compare(0, 2, "sd") == 0 && all_alpha(name, 2)) return true;
  if (name.size() >= 3 && name.compare(0, 2, "vd") == 0 && all_alpha(name, 2)) return true;
  if (name.rfind("nvme", 0) == 0) {
    std::size_t p = 4;
    std::size_t d1 = p;
    while (p < name.size() && std::isdigit(static_cast<unsigned char>(name[p]))) ++p;
    if (p == d1 || p >= name.size() || name[p] != 'n') return false;
    ++p;
    std::size_t d2 = p;
    while (p < name.size() && std::isdigit(static_cast<unsigned char>(name[p]))) ++p;
    return p == name.size() && p > d2;
  }
  if (name.rfind("mmcblk", 0) == 0) return all_digits_from(name, 6);
  return false;
}

}  // namespace
void MemIoNetCollector::collect(Snapshot& s, double dt) {
  if (dt <= 0.0001) dt = 0.001;

  // ------------------------------------------------------------ memory
  std::unordered_map<std::string, uint64_t> mi = read_meminfo();
  MemInfo m;
  m.total = memval(mi, "MemTotal");
  m.avail = memval(mi, "MemAvailable");
  if (m.avail == 0) m.avail = memval(mi, "MemFree");
  m.free = memval(mi, "MemFree");
  m.cached = memval(mi, "Cached") + memval(mi, "SReclaimable");
  m.buffers = memval(mi, "Buffers");
  m.dirty = memval(mi, "Dirty");
  m.shmem = memval(mi, "Shmem");
  m.anon = memval(mi, "AnonPages");
  m.swap_total = memval(mi, "SwapTotal");
  uint64_t swap_free = memval(mi, "SwapFree");
  m.swap_used = m.swap_total > swap_free ? m.swap_total - swap_free : 0;
  m.slab = memval(mi, "Slab");
  m.pagetables = memval(mi, "PageTables");
  m.used = m.total > m.avail ? m.total - m.avail : 0;
  m.pressure = psi_some_avg10("/proc/pressure/memory");
  s.mem = m;

  // ------------------------------------------------------------ network
  NetInfo net;
  std::unordered_map<std::string, std::pair<uint64_t, uint64_t>> next_net;
  {
    std::ifstream f("/proc/net/dev");
    std::string line;
    while (std::getline(f, line)) {
      std::size_t colon = line.find(':');
      if (colon == std::string::npos) continue;
      std::string iface = trim(line.substr(0, colon));
      if (iface.empty() || iface == "lo") continue;
      std::istringstream is(line.substr(colon + 1));
      std::vector<std::string> tok;
      std::string t;
      while (is >> t) tok.push_back(t);
      if (tok.size() < 12) continue;
      uint64_t v[12] = {0};
      for (std::size_t i = 0; i < 12 && i < tok.size(); ++i) to_u64(tok[i], v[i]);

      NetDevInfo d;
      d.name = iface;
      d.rx = v[0];
      d.rx_pkt = v[1];
      d.rx_err = v[2];
      d.rx_drop = v[3];
      d.tx = v[8];
      d.tx_pkt = v[9];
      d.tx_err = v[10];
      d.tx_drop = v[11];
      auto it = net_prev_.find(iface);
      if (have_ && it != net_prev_.end()) {
        d.rx_bps = diff_rate(d.rx, it->second.first, dt);
        d.tx_bps = diff_rate(d.tx, it->second.second, dt);
      }
      next_net[iface] = std::make_pair(d.rx, d.tx);

      net.rx_bps += d.rx_bps;
      net.tx_bps += d.tx_bps;
      net.rx_total += d.rx;
      net.tx_total += d.tx;
      net.rx_pkt += d.rx_pkt;
      net.tx_pkt += d.tx_pkt;
      net.rx_err += d.rx_err;
      net.tx_err += d.tx_err;
      net.devs.push_back(std::move(d));
    }
  }
  net_prev_.swap(next_net);
  if (!have_) {
    prev_rx_ = net.rx_total;
    prev_tx_ = net.tx_total;
  }
  net.session_rx = net.rx_total >= prev_rx_ ? net.rx_total - prev_rx_ : 0;
  net.session_tx = net.tx_total >= prev_tx_ ? net.tx_total - prev_tx_ : 0;
  count_sockets("/proc/net/tcp", true, net.tcp_socks, net.tcp_estab, net.tcp_timewait);
  count_sockets("/proc/net/tcp6", true, net.tcp_socks, net.tcp_estab, net.tcp_timewait);
  count_sockets("/proc/net/udp", false, net.udp_socks, net.tcp_estab, net.tcp_timewait);
  count_sockets("/proc/net/udp6", false, net.udp_socks, net.tcp_estab, net.tcp_timewait);
  s.net = std::move(net);

  // ------------------------------------------------------------ disk
  DiskInfo disk;
  std::unordered_map<std::string, DiskPrev> next_disk;
  double agg_ms = 0, agg_ios = 0, agg_util = 0;
  int dev_count = 0;
  {
    std::ifstream f("/proc/diskstats");
    std::string line;
    while (std::getline(f, line)) {
      std::istringstream is(line);
      std::vector<std::string> tok;
      std::string t;
      while (is >> t) tok.push_back(t);
      if (tok.size() < 14) continue;
      const std::string name = tok[2];
      if (!whole_disk(name)) continue;
      uint64_t rsect = 0, wsect = 0, rd = 0, wr = 0, ms = 0;
      to_u64(tok[5], rsect);
      to_u64(tok[9], wsect);
      to_u64(tok[3], rd);
      to_u64(tok[7], wr);
      to_u64(tok[12], ms);

      DiskDevInfo d;
      d.name = name;
      d.r_total = rsect * 512ULL;
      d.w_total = wsect * 512ULL;
      auto it = disk_prev_.find(name);
      if (have_ && it != disk_prev_.end()) {
        d.r_bps = diff_rate(rsect, it->second.rsect, dt) * 512.0;
        d.w_bps = diff_rate(wsect, it->second.wsect, dt) * 512.0;
        double delta_ios_abs = 0;
        if (rd + wr >= it->second.ios) delta_ios_abs = static_cast<double>((rd + wr) - it->second.ios);
        d.iops = delta_ios_abs / dt;
        double dms = ms >= it->second.ms ? static_cast<double>(ms - it->second.ms) : 0;
        d.lat_ms = delta_ios_abs > 0 ? dms / delta_ios_abs : 0;
        double util = dms / (dt * 1000.0) * 100.0;
        if (util > 100) util = 100;
        if (util < 0) util = 0;
        agg_util += util;
        agg_ms += dms;
        agg_ios += delta_ios_abs;
      }
      next_disk[name] = DiskPrev{rsect, wsect, rd + wr, ms};

      disk.r_bps += d.r_bps;
      disk.w_bps += d.w_bps;
      disk.iops += d.iops;
      disk.r_total += d.r_total;
      disk.w_total += d.w_total;
      ++dev_count;
      disk.devs.push_back(std::move(d));
    }
  }
  disk_prev_.swap(next_disk);
  disk.lat_ms = agg_ios > 0 ? agg_ms / agg_ios : 0;
  disk.util = dev_count > 0 ? agg_util / dev_count : 0;
  s.disk = std::move(disk);

  // ------------------------------------------------------------ per-process I/O
  const uint64_t total_mem = s.mem.total;
  for (ProcInfo& p : s.procs) {
    const std::string base = "/proc/" + std::to_string(p.pid);
    uint64_t rd_bytes = 0, wr_bytes = 0;
    bool got_io = false;
    {
      std::ifstream f(base + "/io");
      std::string line;
      while (std::getline(f, line)) {
        if (line.rfind("read_bytes:", 0) == 0) {
          std::istringstream is(line.substr(11));
          if (is >> rd_bytes) got_io = true;
        } else if (line.rfind("write_bytes:", 0) == 0) {
          std::istringstream is(line.substr(12));
          if (is >> wr_bytes) got_io = true;
        }
      }
    }
    if (got_io) {
      p.rd_total = rd_bytes;
      p.wr_total = wr_bytes;
      auto it = proc_io_prev_.find(p.pid);
      if (have_ && it != proc_io_prev_.end()) {
        p.rd_bps = diff_rate(rd_bytes, it->second.first, dt);
        p.wr_bps = diff_rate(wr_bytes, it->second.second, dt);
      }
      proc_io_prev_[p.pid] = std::make_pair(rd_bytes, wr_bytes);
    }
    if (p.rss == 0) {
      std::ifstream sm(base + "/statm");
      uint64_t size = 0, resident = 0;
      if (sm >> size >> resident) {
        const long ps = sysconf(_SC_PAGESIZE);
        p.rss = resident * static_cast<uint64_t>(ps > 0 ? ps : 4096);
      }
    }
    p.mem_pct = total_mem > 0 ? 100.0 * static_cast<double>(p.rss) / static_cast<double>(total_mem) : 0;
  }
  if (proc_io_prev_.size() > 20000) proc_io_prev_.clear();
  have_ = true;
}

}  // namespace ct
