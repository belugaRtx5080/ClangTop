// ClangTop++ - mounted filesystems + asynchronous source tree scanner.
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <sys/statvfs.h>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "../core/platform_collector.hpp"

namespace ct {
namespace {

std::string trim(const std::string& in) {
  std::size_t a = 0, b = in.size();
  while (a < b && std::isspace(static_cast<unsigned char>(in[a]))) ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(in[b - 1]))) --b;
  return in.substr(a, b - a);
}

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

bool starts_with(const std::string& s, const char* p) {
  return s.rfind(p, 0) == 0;
}

enum class Cmt { None, C, Hash, Py, Lua, Haskell, Html, Php, Css };

struct LangDef {
  const char* name;
  Cmt style;
};

const LangDef* lang_for_ext(const std::string& ext) {
  static const LangDef cpp{"C/C++", Cmt::C};
  static const LangDef py{"Python", Cmt::Py};
  static const LangDef rust{"Rust", Cmt::C};
  static const LangDef go{"Go", Cmt::C};
  static const LangDef js{"JavaScript", Cmt::C};
  static const LangDef ts{"TypeScript", Cmt::C};
  static const LangDef java{"Java", Cmt::C};
  static const LangDef shell{"Shell", Cmt::Hash};
  static const LangDef lua{"Lua", Cmt::Lua};
  static const LangDef ruby{"Ruby", Cmt::Hash};
  static const LangDef cs{"C#", Cmt::C};
  static const LangDef php{"PHP", Cmt::Php};
  static const LangDef swift{"Swift", Cmt::C};
  static const LangDef kotlin{"Kotlin", Cmt::C};
  static const LangDef zig{"Zig", Cmt::C};
  static const LangDef haskell{"Haskell", Cmt::Haskell};
  static const LangDef md{"Markdown", Cmt::None};
  static const LangDef json{"JSON", Cmt::None};
  static const LangDef yaml{"YAML/TOML", Cmt::Hash};
  static const LangDef html{"HTML/CSS", Cmt::Html};
  static const LangDef css{"HTML/CSS", Cmt::Css};
  static const LangDef elixir{"Elixir", Cmt::Hash};
  static const LangDef dart{"Dart", Cmt::C};
  static const LangDef scala{"Scala", Cmt::C};

  if (ext == "c" || ext == "h" || ext == "cc" || ext == "cpp" || ext == "cxx" || ext == "hpp" ||
      ext == "hh" || ext == "hxx" || ext == "inl" || ext == "ipp" || ext == "tpp" || ext == "m" ||
      ext == "mm")
    return &cpp;
  if (ext == "py" || ext == "pyw") return &py;
  if (ext == "rs") return &rust;
  if (ext == "go") return &go;
  if (ext == "js" || ext == "mjs" || ext == "cjs" || ext == "jsx") return &js;
  if (ext == "ts" || ext == "tsx") return &ts;
  if (ext == "java") return &java;
  if (ext == "sh" || ext == "bash" || ext == "zsh" || ext == "fish" || ext == "ksh") return &shell;
  if (ext == "lua") return &lua;
  if (ext == "rb") return &ruby;
  if (ext == "cs") return &cs;
  if (ext == "php") return &php;
  if (ext == "swift") return &swift;
  if (ext == "kt" || ext == "kts") return &kotlin;
  if (ext == "zig") return &zig;
  if (ext == "hs") return &haskell;
  if (ext == "md" || ext == "markdown") return &md;
  if (ext == "json") return &json;
  if (ext == "yaml" || ext == "yml" || ext == "toml") return &yaml;
  if (ext == "html" || ext == "htm") return &html;
  if (ext == "css" || ext == "scss" || ext == "sass" || ext == "less") return &css;
  if (ext == "ex" || ext == "exs") return &elixir;
  if (ext == "dart") return &dart;
  if (ext == "scala" || ext == "sc") return &scala;
  return nullptr;
}

bool skip_dir_name(const std::string& n) {
  static const std::unordered_set<std::string> names = {
      ".git", ".hg", ".svn", "build", "node_modules", "target", "dist", ".cache", ".venv",
      "venv", "__pycache__", ".npm"};
  if (names.count(n)) return true;
  return starts_with(n, "cmake-build-");
}

void count_source(std::ifstream& f, Cmt style, uint64_t& lines, uint64_t& code, uint64_t& comments,
                  uint64_t& blanks) {
  const char* line1 = nullptr;
  const char* line2 = nullptr;
  std::string bstart, bend;
  bool triple = false;
  switch (style) {
    case Cmt::C: line1 = "//"; bstart = "/*"; bend = "*/"; break;
    case Cmt::Hash: line1 = "#"; break;
    case Cmt::Py: line1 = "#"; triple = true; break;
    case Cmt::Lua: line1 = "--"; bstart = "--[["; bend = "]]"; break;
    case Cmt::Haskell: line1 = "--"; bstart = "{-"; bend = "-}"; break;
    case Cmt::Html: bstart = "<!--"; bend = "-->"; break;
    case Cmt::Php: line1 = "//"; line2 = "#"; bstart = "/*"; bend = "*/"; break;
    case Cmt::Css: bstart = "/*"; bend = "*/"; break;
    case Cmt::None: break;
  }

  bool in_block = false;
  std::string line;
  while (std::getline(f, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    ++lines;
    std::string t = trim(line);
    if (t.empty()) {
      ++blanks;
      continue;
    }
    if (in_block) {
      ++comments;
      if (t.find(bend) != std::string::npos) in_block = false;
      continue;
    }
    if (!bstart.empty() && starts_with(t, bstart.c_str())) {
      ++comments;
      if (t.find(bend, bstart.size()) == std::string::npos) in_block = true;
      continue;
    }
    if ((line1 && starts_with(t, line1)) || (line2 && starts_with(t, line2))) {
      ++comments;
      continue;
    }
    if (triple && (starts_with(t, "\"\"\"") || starts_with(t, "'''"))) {
      ++comments;
      std::string fence = t.substr(0, 3);
      if (t.find(fence, 3) == std::string::npos) {
        in_block = true;
        bend = fence;
      }
      continue;
    }
    ++code;
  }
}

std::string unescape_mount(const std::string& in) {
  std::string out;
  for (std::size_t i = 0; i < in.size(); ++i) {
    if (in[i] == '\\' && i + 3 < in.size() && in[i + 1] >= '0' && in[i + 1] <= '7' &&
        in[i + 2] >= '0' && in[i + 2] <= '7' && in[i + 3] >= '0' && in[i + 3] <= '7') {
      int v = (in[i + 1] - '0') * 64 + (in[i + 2] - '0') * 8 + (in[i + 3] - '0');
      out += static_cast<char>(v);
      i += 3;
    } else {
      out += in[i];
    }
  }
  return out;
}

bool pseudo_fs(const std::string& t) {
  static const std::unordered_set<std::string> names = {
      "proc", "sysfs", "devtmpfs", "devpts", "securityfs", "debugfs", "tracefs", "pstore", "bpf",
      "autofs", "mqueue", "hugetlbfs", "fusectl", "configfs", "nfsd", "binfmt_misc", "efivarfs",
      "rpc_pipefs", "squashfs", "cgroup", "cgroup2"};
  if (names.count(t)) return true;
  if (starts_with(t, "cgroup")) return true;
  if (starts_with(t, "snap")) return true;
  return false;
}

}  // namespace
struct FsCodeScanner::Impl {
  mutable std::mutex m;
  CodeScan state;
  std::thread worker;
  std::atomic<bool> stop_flag{false};

  void run(const std::string& root);
};

void FsCodeScanner::Impl::run(const std::string& root) {
  namespace fs = std::filesystem;
  const auto t0 = std::chrono::steady_clock::now();
  std::unordered_map<std::string, LangStat> agg;
  uint64_t files = 0, bytes = 0, total_lines = 0, total_code = 0;

  auto publish = [&](const std::string& current, bool finished) {
    double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::lock_guard<std::mutex> lk(m);
    state.files = files;
    state.total_bytes = bytes;
    state.total_lines = total_lines;
    state.total_code = total_code;
    state.elapsed = elapsed;
    state.current = finished ? std::string() : current;
    if (finished) {
      state.progress = 1.0;
      state.running = false;
      state.done = true;
    } else {
      double p = static_cast<double>(files) / 10000.0;
      if (p > 0.95) p = 0.95;
      state.progress = p;
    }
    state.langs.clear();
    state.langs.reserve(agg.size());
    for (const auto& kv : agg) state.langs.push_back(kv.second);
    std::sort(state.langs.begin(), state.langs.end(),
              [](const LangStat& a, const LangStat& b) { return a.code > b.code; });
  };

  std::error_code ec;
  try {
    fs::recursive_directory_iterator it(fs::path(root), fs::directory_options::skip_permission_denied, ec);
    fs::recursive_directory_iterator end;
    while (it != end) {
      if (stop_flag.load()) break;
      if (ec) {
        ec.clear();
        it.increment(ec);
        continue;
      }
      const fs::directory_entry& e = *it;
      std::error_code fec;
      const bool is_dir = e.is_directory(fec);
      fec.clear();
      const bool is_file = e.is_regular_file(fec);
      const std::string p = e.path().string();

      if (is_dir) {
        std::string name = e.path().filename().string();
        if (skip_dir_name(name) || p == "/proc" || p == "/sys" || p == "/dev")
          it.disable_recursion_pending();
      } else if (is_file) {
        std::error_code sec;
        std::uintmax_t sz = e.file_size(sec);
        if (sec) sz = 0;
        ++files;
        bytes += static_cast<uint64_t>(sz);

        std::string ext;
        std::size_t dot = p.rfind('.');
        std::size_t slash = p.find_last_of('/');
        if (dot != std::string::npos && (slash == std::string::npos || dot > slash + 1))
          ext = lower(p.substr(dot + 1));
        const LangDef* def = ext.empty() ? nullptr : lang_for_ext(ext);

        if (def && sz <= 2ULL * 1024ULL * 1024ULL) {
          std::ifstream f(p, std::ios::binary);
          if (f) {
            uint64_t lines = 0, code = 0, comments = 0, blanks = 0;
            count_source(f, def->style, lines, code, comments, blanks);
            LangStat& ls = agg[def->name];
            ls.lang = def->name;
            ls.files += 1;
            ls.lines += lines;
            ls.code += code;
            ls.comments += comments;
            ls.blanks += blanks;
            ls.bytes += static_cast<uint64_t>(sz);
            total_lines += lines;
            total_code += code;
          }
        }
        publish(p, false);
      }
      it.increment(ec);
    }
  } catch (...) {
    // Never let filesystem errors escape the worker.
  }
  publish(std::string(), true);
}

FsCodeScanner::FsCodeScanner() : impl_(new Impl()) {}
FsCodeScanner::~FsCodeScanner() { stop(); }

bool FsCodeScanner::running() const {
  if (!impl_) return false;
  std::lock_guard<std::mutex> lk(impl_->m);
  return impl_->state.running;
}

CodeScan FsCodeScanner::snapshot() const {
  if (!impl_) return CodeScan{};
  std::lock_guard<std::mutex> lk(impl_->m);
  return impl_->state;
}

void FsCodeScanner::stop() {
  if (!impl_) return;
  impl_->stop_flag.store(true);
  if (impl_->worker.joinable()) impl_->worker.join();
  std::lock_guard<std::mutex> lk(impl_->m);
  impl_->state.running = false;
}

void FsCodeScanner::start(const std::string& path) {
  if (!impl_) return;
  stop();
  impl_->stop_flag.store(false);
  std::string root = path;
  if (root.empty()) {
    const char* home = std::getenv("HOME");
    root = (home && *home) ? std::string(home) : std::string(".");
  }
  {
    std::lock_guard<std::mutex> lk(impl_->m);
    impl_->state = CodeScan{};
    impl_->state.path = root;
    impl_->state.running = true;
    impl_->state.done = false;
    impl_->state.progress = 0.0;
  }
  impl_->worker = std::thread([this, root]() {
    if (impl_) impl_->run(root);
  });
}

void FsCodeScanner::collect_mounts(std::vector<FsInfo>& out) {
  out.clear();
  std::ifstream f("/proc/mounts");
  if (!f) return;
  std::unordered_set<std::string> seen;
  struct Cand {
    FsInfo fi;
  };
  std::vector<Cand> cands;
  std::string line;
  while (std::getline(f, line)) {
    std::istringstream is(line);
    std::string src, mnt, type, opts;
    if (!(is >> src >> mnt >> type >> opts)) continue;
    mnt = unescape_mount(mnt);
    if (pseudo_fs(type)) continue;
    if (seen.count(src)) continue;
    struct statvfs vfs;
    if (statvfs(mnt.c_str(), &vfs) != 0) continue;
    uint64_t total = static_cast<uint64_t>(vfs.f_blocks) * vfs.f_frsize;
    if (total == 0) continue;
    uint64_t avail = static_cast<uint64_t>(vfs.f_bavail) * vfs.f_frsize;
    uint64_t freeb = static_cast<uint64_t>(vfs.f_bfree) * vfs.f_frsize;
    uint64_t used = total > freeb ? total - freeb : 0;
    FsInfo fi;
    fi.source = src;
    fi.mount = mnt;
    fi.fstype = type;
    fi.total = total;
    fi.used = used;
    fi.avail = avail;
    fi.pct = total ? (100.0 * static_cast<double>(used) / static_cast<double>(total)) : 0.0;
    seen.insert(src);
    cands.push_back({fi});
  }
  std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.fi.total > b.fi.total; });
  if (cands.size() > 14) cands.resize(14);
  for (const auto& c : cands) out.push_back(c.fi);
}

}  // namespace ct
