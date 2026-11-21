// ClangTop++ - kernel lab: discovery, live inspector, async benchmarks, snapshots/diff
#pragma once
#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ct {

struct InstalledKernel {
  std::string version;
  std::string flavor;
  std::string image_path;
  std::string modules_path;
  uint64_t image_size = 0;
  int module_count = -1;  // -1 if unknown
  bool active = false;
};

struct SysctlParam {
  std::string key;    // dot form, e.g. kernel.pid_max
  std::string value;
};

struct MitigationEntry {
  std::string name;
  std::string status;
};

struct KernelInspectorInfo {
  std::string active_kernel;  // uname -r
  std::string uname_a;
  std::string cmdline;
  std::string config_source;
  std::string config_excerpt;
  std::string preemption_model;
  std::vector<MitigationEntry> mitigations;
  std::vector<SysctlParam> sysctl_params;
};

struct BenchmarkResult {
  bool valid = false;
  bool running = false;
  double elapsed_s = 0.0;
  double ipc_gbps = 0.0;       // Pipe/IPC throughput
  double ctx_switch_ns = 0.0;  // context switch latency
  double syscall_mops = 0.0;   // M calls/s
  double vfs_gbps = 0.0;       // page cache VFS throughput
  std::string error;
  std::string timestamp;
};

struct KernelSnapshot {
  std::string name;
  std::string saved_at;
  std::string cmdline;
  std::string preemption_model;
  std::map<std::string, std::string> mitigations;
  std::map<std::string, std::string> sysctl;
  BenchmarkResult bench;
};

struct DiffMetric {
  std::string name;
  double a = 0.0;
  double b = 0.0;
  double delta_pct = 0.0;  // (b-a)/a*100 when a!=0, else 0
  bool has_a = false;
  bool has_b = false;
  bool lower_is_better = false;
};

struct KernelDiff {
  bool ok = false;
  std::string error;
  std::string a_name;
  std::string b_name;
  std::vector<DiffMetric> metrics;
  std::vector<std::string> notes;
};

class KernelLab {
 public:
  KernelLab();
  ~KernelLab();

  KernelLab(const KernelLab&) = delete;
  KernelLab& operator=(const KernelLab&) = delete;

  void refresh_installed();
  void refresh_inspector(bool force_sysctl = false);

  void start_benchmark();
  void stop_benchmark();

  std::vector<InstalledKernel> installed_kernels() const;
  KernelInspectorInfo inspector() const;
  BenchmarkResult benchmark() const;

  std::string active_kernel() const;

  std::string snapshot_dir() const;
  std::vector<std::string> list_snapshots() const;
  bool save_snapshot(std::string* err = nullptr);
  bool load_snapshot(const std::string& name, KernelSnapshot& out, std::string* err = nullptr) const;
  KernelDiff diff_snapshots(const std::string& a_name, const std::string& b_name) const;

  static std::string sanitize_name(const std::string& in);

 private:
  void run_benchmark();
  static std::string now_string();
  static std::string read_text_file(const std::string& path, std::size_t max_bytes = 256 * 1024);
  static bool write_text_file(const std::string& path, const std::string& data);
  static std::vector<SysctlParam> collect_sysctl();
  static std::string detect_preemption(const std::string& cmdline, const std::string& config_path);
  static std::vector<MitigationEntry> collect_mitigations();
  static std::string config_fallback_path(const std::string& uname_r);
  static KernelSnapshot make_snapshot_locked(const KernelInspectorInfo& inspector, const BenchmarkResult& bench);
  static std::string to_json(const KernelSnapshot& s);
  static bool from_json(const std::string& text, KernelSnapshot& out, std::string* err);

  mutable std::mutex mu_;
  std::vector<InstalledKernel> installed_;
  KernelInspectorInfo inspector_;
  BenchmarkResult bench_;
  std::thread bench_thread_;
  std::atomic<bool> stop_{false};
  bool sysctl_loaded_ = false;
};

}  // namespace ct