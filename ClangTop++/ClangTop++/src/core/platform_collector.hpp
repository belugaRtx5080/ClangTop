// ClangTop++ - platform metric collectors (Linux /proc & /sys, BSD sysctl fallback)
#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "metrics_types.hpp"

namespace ct {

// /proc/interrupts, /proc/schedstat, /proc/pressure/*, /proc/slabinfo, kernel threads
class SysKernelCollector {
 public:
  void collect(Snapshot& s, double dt);

 private:
  bool have_ = false;
  uint64_t prev_ctx_ = 0;
  uint64_t prev_forks_ = 0;
  uint64_t prev_pgfault_ = 0;
  uint64_t prev_pgmajfault_ = 0;
  std::vector<std::string> prev_irq_names_;
  std::vector<std::vector<uint64_t>> prev_irq_counts_;
  std::vector<uint64_t> prev_irq_core_;
};

// /proc/stat, /proc/cpuinfo, cpufreq, thermal, per-process CPU
class CpuCollector {
 public:
  void collect(Snapshot& s, double dt);

 private:
  bool have_ = false;
  std::vector<std::array<uint64_t, 8>> prev_;
  struct ProcTrack {
    double avg = 0;
    double peak = 0;
    double samples = 0;
    std::vector<double> spark;
  };
  std::unordered_map<int, ProcTrack> track_;
};

// batteries, RAPL powercap, hwmon, GPU power, ACPI/BSD sysctl
class PowerCollector {
 public:
  void collect(Snapshot& s, double dt);

 private:
  bool have_ = false;
  std::unordered_map<std::string, uint64_t> energy_prev_;  // micro-joules
  double prev_t_ = 0;
};

// /proc/meminfo, /proc/net/dev, /proc/diskstats, /proc/net/{tcp,udp}, /proc/[pid]/io
class MemIoNetCollector {
 public:
  void collect(Snapshot& s, double dt);

 private:
  bool have_ = false;
  uint64_t prev_rx_ = 0, prev_tx_ = 0;
  std::unordered_map<std::string, std::pair<uint64_t, uint64_t>> net_prev_;
  struct DiskPrev {
    uint64_t rsect = 0, wsect = 0, ios = 0, ms = 0;
  };
  std::unordered_map<std::string, DiskPrev> disk_prev_;
  std::unordered_map<int, std::pair<uint64_t, uint64_t>> proc_io_prev_;
};

// Mounted filesystems + async $HOME source scanner
class FsCodeScanner {
 public:
  FsCodeScanner();
  ~FsCodeScanner();
  FsCodeScanner(const FsCodeScanner&) = delete;
  FsCodeScanner& operator=(const FsCodeScanner&) = delete;

  void start(const std::string& path);
  void stop();
  void collect_mounts(std::vector<FsInfo>& out);
  CodeScan snapshot() const;
  bool running() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Aggregates every collector into one Snapshot.
class PlatformCollector {
 public:
  PlatformCollector();
  ~PlatformCollector();
  PlatformCollector(const PlatformCollector&) = delete;
  PlatformCollector& operator=(const PlatformCollector&) = delete;

  void set_scan_path(const std::string& path);
  void rescan_code();
  Snapshot collect();  // full sample; dt measured internally

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace ct
