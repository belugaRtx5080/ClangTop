// ClangTop++ - built-in catalog of Linux and non-Linux kernels
#pragma once
#include <string>
#include <vector>

namespace ct {

struct KernelCatalogEntry {
  std::string name;
  std::string family;            // Linux / BSD / Hybrid / Microkernel
  std::string architecture_type;
  std::string scheduler;
  std::string default_hz;
  std::string preemption;
  std::string language;
  std::vector<std::string> pros;
  std::vector<std::string> cons;
  std::vector<std::string> best_for;
  std::vector<std::string> key_sysctl_hints;
};

inline const std::vector<KernelCatalogEntry>& kernel_catalog() {
  static const std::vector<KernelCatalogEntry> kCatalog = {
      // ----------------------------- Linux variants
      {"Mainline (linux)", "Linux", "Monolithic (modular)", "EEVDF (CFS successor)", "1000 Hz (typical, config-dependent)",
       "PREEMPT_DYNAMIC / voluntary / preemptible (config)", "C, ASM, Rust (emerging)",
       {"Latest hardware enablement and drivers", "Newest kernel features", "Fast upstream bug/security fixes"},
       {"Short support window", "Potential regressions between releases"},
       {"Developers", "New hardware", "Bleeding-edge desktops"},
       {"kernel.sched_autogroup_enabled", "vm.swappiness", "kernel.pid_max"}},
      {"LTS (linux-lts 6.12/6.6)", "Linux", "Monolithic (modular)", "EEVDF/CFS", "250-1000 Hz",
       "PREEMPT_DYNAMIC / voluntary", "C, ASM",
       {"Long-term maintenance and backports", "High stability for production", "Predictable behavior"},
       {"Older features/drivers than mainline", "Some performance patches arrive later"},
       {"Servers", "Production workstations", "Stability-focused systems"},
       {"vm.dirty_ratio", "vm.dirty_background_ratio", "kernel.panic"}},
      {"Zen / Liquorix (linux-zen)", "Linux", "Monolithic (modular)", "EEVDF with desktop/latency tuning", "1000 Hz",
       "Preemptible / low-latency oriented", "C, ASM",
       {"Improved desktop responsiveness", "Good interactive/multimedia feel", "Popular gaming/desktop choice"},
       {"Not focused on strict real-time determinism", "May trade some throughput for latency"},
       {"Desktop", "Gaming", "Multimedia production"},
       {"kernel.sched_latency_ns", "vm.vfs_cache_pressure", "kernel.split_lock_mitigate"}},
      {"CachyOS BORE (linux-cachyos)", "Linux", "Monolithic (modular)", "BORE (Burst-Oriented Response Enhancer)", "1000 Hz",
       "Preemptible desktop-focused", "C, ASM",
       {"Strong interactivity under load", "Gaming/desktop optimizations", "BORE scheduler behavior"},
       {"Out-of-tree patchset differences", "Less conservative than LTS"},
       {"Gaming rigs", "Interactive desktops", "Latency-sensitive desktop workloads"},
       {"kernel.sched_bore", "kernel.sched_autogroup_enabled", "vm.compaction_proactiveness"}},
      {"Hardened (linux-hardened)", "Linux", "Monolithic (modular)", "EEVDF/CFS", "300-1000 Hz",
       "Preemptible/Voluntary (security-focused config)", "C, ASM",
       {"Extra hardening patches and defaults", "Reduced attack surface options", "Security-conscious configuration"},
       {"Possible performance overhead", "Some software may need compatibility tuning"},
       {"Security-focused desktops/servers", "Threat-exposed environments"},
       {"kernel.kptr_restrict", "kernel.yama.ptrace_scope", "kernel.unprivileged_userns_clone"}},
      {"Realtime (linux-rt / PREEMPT_RT)", "Linux", "Monolithic (modular)", "RT-aware scheduling (SCHED_FIFO/RR/DEADLINE)", "1000 Hz",
       "PREEMPT_RT (fully preemptible kernel)", "C, ASM",
       {"Low and bounded latency", "Deterministic response for RT tasks", "Mature real-time ecosystem"},
       {"Throughput trade-offs vs throughput-optimized kernels", "Requires careful system tuning"},
       {"Audio production", "Industrial control", "Robotics/low-latency systems"},
       {"kernel.sched_rt_runtime_us", "kernel.timer_migration", "vm.stat_interval"}},
      {"XanMod (linux-xanmod)", "Linux", "Monolithic (modular)", "EEVDF/CFS with performance patches", "500-1000 Hz",
       "Preemptible", "C, ASM",
       {"Performance-oriented patch set", "Desktop/gaming responsiveness tuning", "Multiple build variants"},
       {"Out-of-tree maintenance model", "Behavior differs from stock kernels"},
       {"Gaming", "Desktop performance", "Enthusiast systems"},
       {"net.core.default_qdisc", "kernel.sched_autogroup_enabled", "vm.swappiness"}},
      {"Clear Linux (linux-clear)", "Linux", "Monolithic (modular)", "EEVDF/CFS (performance-tuned)", "250-1000 Hz",
       "Preemptible/Voluntary", "C, ASM",
       {"Aggressive performance tuning", "Optimized for Intel/Clear Linux stack", "Fast boot and throughput focus"},
       {"Best results in its native ecosystem", "Less tailored for generic distros"},
       {"Intel platforms", "Performance benchmarking", "Clear Linux environments"},
       {"vm.dirty_writeback_centisecs", "net.ipv4.tcp_congestion_control", "kernel.nmi_watchdog"}},
      {"Android GKI", "Linux", "Monolithic (modular)", "EEVDF/CFS (mobile/vendor tuned)", "250-300 Hz typical",
       "Preemptible/Voluntary (device-dependent)", "C, ASM",
       {"Generic Kernel Image ABI for Android", "Improved update path for devices", "LTS-based common baseline"},
       {"Mobile-focused constraints", "Not designed for generic desktop/server use"},
       {"Android devices", "OEM/vendor kernels", "Mobile hardware platforms"},
       {"kernel.sched_util_clamp_max", "vm.extra_free_kbytes", "kernel.printk"}},
      // ----------------------------- Non-Linux kernels
      {"FreeBSD", "BSD", "Monolithic (modular)", "ULE scheduler", "1000 Hz (typical)", "Kernel preemption supported", "C",
       {"Excellent networking stack", "ZFS and jails integration", "Capsicum capability security"},
       {"Smaller desktop driver ecosystem than Linux", "Different tooling and userland conventions"},
       {"Servers", "NAS/storage", "Networking appliances"},
       {"kern.hz", "kern.sched.preempt_thresh", "vfs.zfs.arc_max"}},
      {"OpenBSD", "BSD", "Monolithic", "Traditional BSD scheduler", "100 Hz (typical)", "Limited/cooperative in parts of kernel", "C",
       {"Security-first design and code auditing", "pledge(2)/unveil(2) mitigations", "Simplicity and correctness focus"},
       {"Performance/hardware support can lag", "Smaller package and ecosystem size"},
       {"Firewalls", "Security gateways", "High-assurance network services"},
       {"kern.securelevel", "net.inet.ip.forwarding", "kern.maxproc"}},
      {"NetBSD", "BSD", "Monolithic (modular)", "BSD scheduler", "100 Hz (typical)", "Port-dependent preemption behavior", "C",
       {"Ultra-portable across architectures", "Clean design and documentation", "pkgsrc support"},
       {"Niche desktop usage", "May trail in features/performance on common platforms"},
       {"Portability projects", "Embedded/retro systems", "Cross-platform research"},
       {"kern.maxvnodes", "vm.anonmax", "hw.physmem64"}},
      {"DragonFly BSD", "BSD", "Hybrid/Monolithic design", "LWKT (Lightweight Kernel Threads) / token-based", "1000 Hz (typical)",
       "Preemptive with LWKT messaging model", "C",
       {"HAMMER2 filesystem", "Innovative SMP kernel design", "Message-passing oriented internals"},
       {"Smaller community and hardware support", "Limited application ecosystem"},
       {"Storage experimentation", "SMP kernel research", "BSD enthusiasts"},
       {"kern.hz", "vfs.hammer2.limit_dirty_inodes", "lwkt.token_spin"}},
      {"XNU/Darwin", "Hybrid", "Hybrid (Mach + BSD)", "Mach scheduler integrated with BSD components", "1000 Hz (typical)",
       "Preemptive", "C, C++",
       {"Tight Apple hardware/software integration", "Mature desktop/mobile platform stack", "Strong QoS and power management"},
       {"Closed-source on Apple platforms", "Not intended for generic hardware"},
       {"macOS/iOS devices", "Apple platform development"},
       {"kern.sched", "kern.clockrate", "vm.swapusage"}},
      {"Zircon", "Microkernel", "Microkernel", "Fair/priority-based scheduler", "Configurable/tickless elements",
       "Preemptive", "C++",
       {"Capability-based security model", "Userspace drivers/services", "Modern Fuchsia foundation"},
       {"Young ecosystem", "Different APIs/runtime from Unix-like systems"},
       {"Fuchsia OS", "Capability-oriented systems", "Embedded/product research"},
       {"kernel.scheduler", "kernel.debug", "vm.page-scanner"}},
      {"Redox OS", "Microkernel", "Microkernel", "Priority-based scheduler", "Configurable",
       "Preemptive", "Rust, C/ASM (small parts)",
       {"Memory-safety focused implementation", "Microkernel/userspace driver model", "Modern OS research direction"},
       {"Early-stage maturity", "Limited hardware and software support"},
       {"OS research", "Rust systems experimentation", "Safety-focused prototypes"},
       {"scheme.*", "kernel.*", "drivers.*"}},
      {"seL4", "Microkernel", "Microkernel", "Fixed-priority preemptive scheduler", "Configurable/tickless",
       "Preemptive", "C (formally verified), ASM",
       {"Formally verified kernel properties", "Strong isolation and capability model", "High-assurance suitability"},
       {"Specialized development workflow", "Minimal built-in services/ecosystem"},
       {"Safety-critical systems", "Security-critical embedded", "Verified systems research"},
       {"sel4.ticks", "sel4.domain", "sel4.capabilities"}},
      {"Haiku", "Hybrid", "Hybrid (BeOS-inspired)", "Priority-based preemptive scheduler", "1000 Hz (typical)",
       "Preemptive", "C++",
       {"Responsive desktop experience", "Cohesive BeOS-style design", "Lightweight and focused system"},
       {"Smaller driver/application ecosystem", "Limited mainstream adoption"},
       {"Enthusiast desktops", "BeOS-inspired development"},
       {"kern.scheduler", "vm.*", "dev.*"}},
      {"Windows NT", "Hybrid", "Hybrid", "Multilevel feedback queue (NT scheduler)", "Variable timer tick / high-resolution timers",
       "Preemptive", "C, C++",
       {"Broad hardware and software ecosystem", "Strong desktop/enterprise/gaming support", "Rich kernel subsystems"},
       {"Closed-source kernel", "Different administration/tooling model than Unix-like OSes"},
       {"General-purpose desktops", "Enterprise workstations/servers", "Commercial applications"},
       {"PriorityControl\\Win32PrioritySeparation", "powercfg settings", "bcdedit options"}},
  };
  return kCatalog;
}

}  // namespace ct
