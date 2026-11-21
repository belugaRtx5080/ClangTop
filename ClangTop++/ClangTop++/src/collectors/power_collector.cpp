// ClangTop++ - power collector: batteries, RAPL, hwmon, GPU, thermal zones.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#if defined(__FreeBSD__)
#include <sys/sysctl.h>
#include <sys/types.h>
#endif

#include "../core/platform_collector.hpp"

namespace ct {
#if defined(__linux__)
namespace {

std::string trim(const std::string& in) {
  std::size_t a = 0, b = in.size();
  while (a < b && std::isspace(static_cast<unsigned char>(in[a]))) ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(in[b - 1]))) --b;
  return in.substr(a, b - a);
}

std::string read_str(const std::string& path) {
  std::ifstream f(path);
  std::string line;
  if (!std::getline(f, line)) return {};
  return trim(line);
}

long long read_i64(const std::string& path) {
  std::ifstream f(path);
  long long v = 0;
  if (!(f >> v)) return 0;
  return v;
}

uint64_t read_u64(const std::string& path) {
  std::ifstream f(path);
  unsigned long long v = 0;
  if (!(f >> v)) return 0;
  return static_cast<uint64_t>(v);
}

bool exists_file(const std::string& path) {
  std::ifstream f(path);
  return static_cast<bool>(f);
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

std::string canonical(const std::string& path) {
  char buf[4096];
  if (::realpath(path.c_str(), buf)) return std::string(buf);
  return path;
}

std::string gpu_label(const std::string& chip) {
  std::string low;
  for (char c : chip) low += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (low.find("gpu") != std::string::npos || low.find("nvidia") != std::string::npos ||
      low.find("amdgpu") != std::string::npos || low.find("radeon") != std::string::npos ||
      low.find("nouveau") != std::string::npos || low.find("intel") != std::string::npos)
    return chip.empty() ? "GPU" : "GPU " + chip;
  return chip;
}

bool battery_chip(const std::string& chip) {
  std::string low;
  for (char c : chip) low += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return low.find("batt") != std::string::npos || low.find("acpi") != std::string::npos;
}

}  // namespace
#endif  // __linux__
void PowerCollector::collect(Snapshot& s, double dt) {
  s.power = PowerInfo{};
  if (dt <= 0.0001) dt = 0.001;
#if defined(__linux__)
  PowerInfo& p = s.power;
  double battery_discharge = 0;
  bool first_batt = true;
  double sum_wh_now = 0, sum_wh_full = 0, sum_wh_design = 0, sum_watts = 0, sum_amps = 0;
  double first_volts = 0, first_temp = 0, first_capacity = 0, first_health = 0;
  int max_cycles = 0;
  bool any_discharging = false, any_charging = false;
  std::string first_status, first_model = "-", first_tech = "-";

  for (const std::string& name : list_dir("/sys/class/power_supply", "BAT")) {
    const std::string base = "/sys/class/power_supply/" + name;
    std::string status = read_str(base + "/status");
    std::string model = read_str(base + "/model_name");
    if (model.empty()) model = name;
    std::string tech = read_str(base + "/technology");
    if (tech.empty()) tech = "-";

    double volts = static_cast<double>(read_i64(base + "/voltage_now")) / 1e6;
    double amps = std::fabs(static_cast<double>(read_i64(base + "/current_now"))) / 1e6;
    double pnow = std::fabs(static_cast<double>(read_i64(base + "/power_now"))) / 1e6;
    double watts = pnow > 0 ? pnow : volts * amps;

    double wh_now = static_cast<double>(read_i64(base + "/energy_now")) / 1e6;
    double wh_full = static_cast<double>(read_i64(base + "/energy_full")) / 1e6;
    double wh_design = static_cast<double>(read_i64(base + "/energy_full_design")) / 1e6;
    if (wh_now <= 0) {
      double ch = static_cast<double>(read_i64(base + "/charge_now")) / 1e6;
      wh_now = ch * volts;
    }
    if (wh_full <= 0) {
      double ch = static_cast<double>(read_i64(base + "/charge_full")) / 1e6;
      wh_full = ch * volts;
    }
    if (wh_design <= 0) {
      double ch = static_cast<double>(read_i64(base + "/charge_full_design")) / 1e6;
      wh_design = ch * volts;
    }
    int cap_file = static_cast<int>(read_i64(base + "/capacity"));
    double capacity = cap_file > 0 ? cap_file : (wh_full > 0 ? 100.0 * wh_now / wh_full : 0);
    double health = wh_design > 0 ? 100.0 * wh_full / wh_design : 0;
    int cycles = static_cast<int>(read_i64(base + "/cycle_count"));
    double temp = static_cast<double>(read_i64(base + "/temp")) / 10.0;
    if (temp < 0) temp = 0;

    p.present = true;
    if (first_batt) {
      first_model = model;
      first_tech = tech;
      first_status = status;
      first_volts = volts;
      first_temp = temp;
      first_capacity = capacity;
      first_health = health;
      first_batt = false;
    }
    sum_wh_now += wh_now;
    sum_wh_full += wh_full;
    sum_wh_design += wh_design;
    sum_watts += watts;
    sum_amps += amps;
    max_cycles = std::max(max_cycles, cycles);
    p.volts = volts > 0 ? volts : p.volts;
    if (status == "Discharging") {
      any_discharging = true;
      battery_discharge += watts;
    } else if (status == "Charging") {
      any_charging = true;
    }

    PowerSensor ps;
    ps.name = model;
    ps.watts = watts;
    ps.volts = volts;
    ps.amps = amps;
    ps.battery = true;
    p.sensors.push_back(std::move(ps));

    if (temp > 0 && p.temps.size() < 10) p.temps.push_back(PowerInfo::TempSensor{name, temp});
  }
  if (p.present) {
    p.model = first_model;
    p.technology = first_tech;
    p.status = any_discharging ? "Discharging" : (any_charging ? "Charging" : first_status);
    if (p.volts <= 0) p.volts = first_volts;
    p.amps = sum_amps;
    p.watts = sum_watts;
    p.wh_now = sum_wh_now;
    p.wh_full = sum_wh_full;
    p.wh_design = sum_wh_design;
    p.capacity = p.wh_full > 0 ? 100.0 * p.wh_now / p.wh_full : first_capacity;
    if (p.capacity > 100) p.capacity = 100;
    p.health = p.wh_design > 0 ? 100.0 * p.wh_full / p.wh_design : first_health;
    if (p.health > 100) p.health = 100;
    p.cycles = max_cycles;
    p.temp_c = first_temp;
  }

  // ------------------------------------------------------------ RAPL
  double sensor_sum = 0;
  for (const std::string& zone : list_dir("/sys/class/powercap", "intel-rapl:")) {
    bool package = true;
    for (char c : zone.substr(11))
      if (!std::isdigit(static_cast<unsigned char>(c))) package = false;
    if (!package) continue;
    const std::string base = "/sys/class/powercap/" + zone;
    if (!exists_file(base + "/energy_uj")) continue;
    uint64_t cur = read_u64(base + "/energy_uj");
    double watts = 0;
    auto it = energy_prev_.find(base);
    if (it != energy_prev_.end()) {
      uint64_t prev = it->second;
      uint64_t maxr = read_u64(base + "/max_energy_range_uj");
      uint64_t delta = 0;
      if (cur >= prev) {
        delta = cur - prev;
      } else if (maxr > prev) {
        delta = (maxr - prev) + cur;
      }
      if (maxr > 0 && delta > maxr) delta = 0;
      watts = static_cast<double>(delta) / 1e6 / dt;
    }
    energy_prev_[base] = cur;
    PowerSensor ps;
    ps.name = "RAPL";
    ps.watts = watts;
    sensor_sum += watts;
    p.sensors.push_back(std::move(ps));
  }

  // ------------------------------------------------------------ GPU hwmon
  std::unordered_set<std::string> gpu_files;
  for (const std::string& card : list_dir("/sys/class/drm", "card")) {
    if (card.size() <= 4 || !std::isdigit(static_cast<unsigned char>(card[4]))) continue;
    const std::string root = "/sys/class/drm/" + card + "/device/hwmon";
    for (const std::string& hw : list_dir(root, "hwmon")) {
      const std::string base = root + "/" + hw;
      const std::string pf = base + "/power1_input";
      if (!exists_file(pf)) continue;
      double watts = static_cast<double>(read_u64(pf)) / 1e6;
      if (watts <= 0) continue;
      std::string chip = read_str(base + "/name");
      PowerSensor ps;
      ps.name = gpu_label(chip);
      ps.watts = watts;
      sensor_sum += watts;
      p.sensors.push_back(std::move(ps));
      gpu_files.insert(canonical(pf));
    }
  }

  // ------------------------------------------------------------ hwmon power
  for (const std::string& hw : list_dir("/sys/class/hwmon", "hwmon")) {
    const std::string base = "/sys/class/hwmon/" + hw;
    const std::string chip = read_str(base + "/name");
    for (int n = 1; n <= 16; ++n) {
      const std::string pf = base + "/power" + std::to_string(n) + "_input";
      if (!exists_file(pf)) continue;
      if (gpu_files.count(canonical(pf))) continue;
      double watts = static_cast<double>(read_u64(pf)) / 1e6;
      if (watts <= 0) continue;
      PowerSensor ps;
      ps.name = gpu_label(chip);
      ps.watts = watts;
      ps.battery = battery_chip(chip);
      if (!ps.battery) sensor_sum += watts;
      p.sensors.push_back(std::move(ps));
    }
  }

  // ------------------------------------------------------------ temperature sensors
  std::unordered_set<std::string> seen_temps;
  std::unordered_set<long long> seen_values;
  auto add_temp = [&](const std::string& name, double c, bool allow_equal_value) {
    if (c <= 0 || c > 150 || p.temps.size() >= 10) return;
    long long key = static_cast<long long>(std::llround(c * 10.0));
    if (!allow_equal_value && seen_values.count(key)) return;
    if (!seen_temps.insert(name + "|" + std::to_string(key)).second) return;
    seen_values.insert(key);
    p.temps.push_back(PowerInfo::TempSensor{name, c});
  };
  for (const std::string& hw : list_dir("/sys/class/hwmon", "hwmon")) {
    const std::string base = "/sys/class/hwmon/" + hw;
    const std::string chip = read_str(base + "/name");
    for (int n = 1; n <= 64; ++n) {
      const std::string num = std::to_string(n);
      double c = static_cast<double>(read_u64(base + "/temp" + num + "_input")) / 1000.0;
      if (c <= 0) continue;
      std::string label = read_str(base + "/temp" + num + "_label");
      std::string name = chip.empty() ? ("temp" + num) : chip;
      if (!label.empty()) name += " " + label;
      add_temp(name, c, true);
    }
  }
  for (const std::string& zone : list_dir("/sys/class/thermal", "thermal_zone")) {
    const std::string base = "/sys/class/thermal/" + zone;
    double c = static_cast<double>(read_u64(base + "/temp")) / 1000.0;
    if (c <= 0) continue;
    std::string type = read_str(base + "/type");
    add_temp(type.empty() ? zone : type, c, false);
  }

  p.total_watts = sensor_sum + (any_discharging ? battery_discharge : 0.0);
  if (p.total_watts <= 0 && p.volts > 0) p.total_watts = p.volts * p.amps;
  have_ = true;
  prev_t_ = prev_t_ + dt;

#elif defined(__FreeBSD__)
  int state = -1;
  std::size_t len = sizeof(state);
  if (sysctlbyname("hw.acpi.battery.state", &state, &len, nullptr, 0) == 0) {
    s.power.present = true;
    if (state & 1) s.power.status = "Discharging";
    else if (state & 2) s.power.status = "Charging";
    else s.power.status = "Full";
  }
  int life = -1;
  len = sizeof(life);
  if (sysctlbyname("hw.acpi.battery.life", &life, &len, nullptr, 0) == 0 && life >= 0) {
    s.power.present = true;
    s.power.capacity = life;
  }
  int mins = -1;
  len = sizeof(mins);
  if (sysctlbyname("hw.acpi.battery.time", &mins, &len, nullptr, 0) == 0) {
    (void)mins;
  }
  (void)dt;
#else
  (void)s;
  (void)dt;
#endif
}

}  // namespace ct
