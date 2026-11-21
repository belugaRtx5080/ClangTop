// ClangTop++ - shared metric data model
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace ct {

// ---------------------------------------------------------------- CPU
struct CpuSample {
  double app = 0;   // user + nice
  double kern = 0;  // system
  double io = 0;    // iowait
  double irq = 0;   // irq + softirq
  double idle = 0;  // idle + steal
  double total = 0; // app+kern+io+irq (busy)
};

struct CoreInfo {
  int id = 0;
  int cluster = 0;
  double app = 0, kern = 0, io = 0, irq = 0, idle = 0, util = 0;
  double mhz = 0, max_mhz = 0, min_mhz = 0;
  std::string governor;
  double temp_c = 0;
};

struct ProcInfo {
  int pid = 0;
  std::string name;
  char state = 'R';
  int threads = 1;
  int core = -1;         // last CPU (processor)
  double cpu_pct = 0;    // instantaneous, % of one core (can exceed 100 for multi-threaded)
  double cpu_avg = 0;    // tracked since first sighting
  double cpu_peak = 0;
  double mem_pct = 0;
  uint64_t rss = 0, vsz = 0;
  double rd_bps = 0, wr_bps = 0;
  uint64_t rd_total = 0, wr_total = 0;
  double age_s = 0;      // exact per-process uptime
  std::vector<double> spark;  // short CPU history
};

// ---------------------------------------------------------------- Memory
struct MemInfo {
  uint64_t total = 0, avail = 0, used = 0, free = 0;
  uint64_t cached = 0, buffers = 0, dirty = 0, shmem = 0, anon = 0;
  uint64_t swap_total = 0, swap_used = 0;
  uint64_t slab = 0, pagetables = 0;
  double pressure = 0;  // PSI memory some avg10
};

// ---------------------------------------------------------------- Power
struct PowerSensor {
  std::string name;
  double watts = 0, volts = 0, amps = 0;
  bool battery = false;
};

struct PowerInfo {
  bool present = false;
  std::string model = "-", status = "-", technology = "-";
  double volts = 0, amps = 0, watts = 0;
  double wh_now = 0, wh_full = 0, wh_design = 0;
  double capacity = 0, health = 0;
  int cycles = 0;
  double temp_c = 0;
  double total_watts = 0;
  std::vector<PowerSensor> sensors;
  struct TempSensor { std::string name; double c = 0; };
  std::vector<TempSensor> temps;
};

// ---------------------------------------------------------------- Network
struct NetDevInfo {
  std::string name;
  uint64_t rx = 0, tx = 0, rx_pkt = 0, tx_pkt = 0;
  uint64_t rx_err = 0, tx_err = 0, rx_drop = 0, tx_drop = 0;
  double rx_bps = 0, tx_bps = 0;
};

struct NetInfo {
  std::vector<NetDevInfo> devs;
  double rx_bps = 0, tx_bps = 0;
  uint64_t rx_total = 0, tx_total = 0;
  uint64_t rx_pkt = 0, tx_pkt = 0, rx_err = 0, tx_err = 0;
  uint64_t session_rx = 0, session_tx = 0;
  int tcp_socks = 0, udp_socks = 0, tcp_estab = 0, tcp_timewait = 0;
};

// ---------------------------------------------------------------- Disk
struct DiskDevInfo {
  std::string name;
  double r_bps = 0, w_bps = 0;
  uint64_t r_total = 0, w_total = 0;
  double iops = 0, lat_ms = 0;
};

struct DiskInfo {
  std::vector<DiskDevInfo> devs;
  double r_bps = 0, w_bps = 0, iops = 0, lat_ms = 0, util = 0;
  uint64_t r_total = 0, w_total = 0;
};

// ---------------------------------------------------------------- IRQ
struct IrqInfo {
  std::vector<std::string> names;
  std::vector<double> rates;                    // per IRQ, per second
  std::vector<std::vector<double>> core;        // per IRQ -> per-core rate
  std::vector<double> core_rate;                // per-core total rate
  double total_rate = 0;
};

// ---------------------------------------------------------------- Kernel
struct KernelInfo {
  double ctx_rate = 0;      // context switches / s
  double forks_rate = 0;
  double pgfault_rate = 0;  // page faults / s (minor+major)
  double pgmajfault_rate = 0;
  uint64_t ctx_total = 0;
  uint64_t pgfault_total = 0;
  uint64_t pgmajfault_total = 0;
  double procs_running = 0, procs_blocked = 0;
  double psi_cpu = 0, psi_io = 0, psi_mem = 0;
  uint64_t slab = 0, pagetables = 0, kernel_stack = 0;
  std::vector<std::pair<std::string, uint64_t>> slab_top;
  int threads = 0;
  double load1 = 0, load5 = 0, load15 = 0;
  std::string sched_policy = "default";
  std::vector<std::string> sched_lines;
};

// ---------------------------------------------------------------- FS / Code
struct FsInfo {
  std::string source, mount, fstype;
  uint64_t total = 0, used = 0, avail = 0;
  double pct = 0;
};

struct LangStat {
  std::string lang;
  uint64_t files = 0, lines = 0, code = 0, comments = 0, blanks = 0, bytes = 0;
};

struct CodeScan {
  bool running = false;
  bool done = false;
  std::string path;
  uint64_t files = 0, total_lines = 0, total_code = 0, total_bytes = 0;
  std::vector<LangStat> langs;   // sorted by code desc
  double elapsed = 0;
  double progress = 0;
  std::string current;
};

// ---------------------------------------------------------------- System
struct SystemInfo {
  std::string uname_sys = "Linux", uname_release, uname_version, hostname, arch;
  double uptime = 0;
  std::string uptime_human;
};

struct Snapshot {
  double ts = 0;
  SystemInfo sys;
  CpuSample cpu;
  std::vector<CoreInfo> cores;
  int ncpu = 0;
  MemInfo mem;
  PowerInfo power;
  NetInfo net;
  DiskInfo disk;
  IrqInfo irq;
  KernelInfo kern;
  std::vector<ProcInfo> procs;
  std::vector<FsInfo> fs;
  CodeScan code;
};

}  // namespace ct
