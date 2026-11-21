// ClangTop++ - raw POSIX terminal handling (termios + TIOCGWINSZ)
#pragma once
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <string>

namespace ct {

class Terminal {
 public:
  enum Key {
    kNone = 0,
    kEsc = 27,
    kEnter = 13,
    kTab = 9,
    kBackspace = 127,
    kUp = 1001,
    kDown = 1002,
    kLeft = 1003,
    kRight = 1004,
    kHome = 1005,
    kEnd = 1006,
    kPgUp = 1007,
    kPgDn = 1008,
    kDel = 1009,
    kResize = 1010,
  };

  Terminal() = default;
  ~Terminal() { leave(); }
  Terminal(const Terminal&) = delete;
  Terminal& operator=(const Terminal&) = delete;

  bool is_tty() const { return ::isatty(STDIN_FILENO) && ::isatty(STDOUT_FILENO); }

  static volatile sig_atomic_t& resize_flag() {
    static volatile sig_atomic_t flag = 0;
    return flag;
  }

  bool enter() {
    if (!is_tty()) return false;
    if (::tcgetattr(STDIN_FILENO, &orig_) != 0) return false;
    have_orig_ = true;
    termios raw = orig_;
    raw.c_iflag &= ~static_cast<tcflag_t>(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    raw.c_oflag &= ~static_cast<tcflag_t>(OPOST);
    raw.c_lflag &= ~static_cast<tcflag_t>(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    raw.c_cflag &= ~static_cast<tcflag_t>(CSIZE | PARENB);
    raw.c_cflag |= CS8;
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (::tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) return false;
    raw_ = true;
    write("\x1b[?1049h\x1b[?25l\x1b[?7l\x1b[2J");
    ::signal(SIGWINCH, [](int) { Terminal::resize_flag() = 1; });
    struct sigaction sa {};
    sa.sa_handler = [](int) {};
    ::sigaction(SIGPIPE, &sa, nullptr);
    return true;
  }

  void leave() {
    if (raw_) {
      write("\x1b[0m\x1b[?7h\x1b[?25h\x1b[?1049l");
      flush();
      if (have_orig_) ::tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_);
      raw_ = false;
    }
  }

  void size(int& w, int& h) const {
    winsize ws{};
    if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
      w = ws.ws_col;
      h = ws.ws_row;
    } else {
      w = 100;
      h = 32;
    }
  }

  void write(const std::string& s) const {
    const char* p = s.data();
    std::size_t n = s.size();
    while (n > 0) {
      ssize_t k = ::write(STDOUT_FILENO, p, n);
      if (k < 0) {
        if (errno == EINTR) continue;
        return;
      }
      p += k;
      n -= static_cast<std::size_t>(k);
    }
  }
  void flush() const {}

  // Non-blocking key read with timeout; returns 0 when nothing is available.
  int read_key(int timeout_ms) {
    if (pending_ != 0) {
      int k = pending_;
      pending_ = 0;
      return k;
    }
    pollfd pfd{STDIN_FILENO, POLLIN, 0};
    int rc = ::poll(&pfd, 1, timeout_ms);
    if (rc <= 0) return 0;
    unsigned char c = 0;
    if (::read(STDIN_FILENO, &c, 1) != 1) return 0;
    if (c == 27) return parse_escape();
    return static_cast<int>(c);
  }

 private:
  int get_byte(int timeout_ms) {
    pollfd pfd{STDIN_FILENO, POLLIN, 0};
    if (::poll(&pfd, 1, timeout_ms) <= 0) return -1;
    unsigned char c = 0;
    if (::read(STDIN_FILENO, &c, 1) != 1) return -1;
    return static_cast<int>(c);
  }

  int parse_escape() {
    int b = get_byte(30);
    if (b < 0) return kEsc;
    if (b == '[' || b == 'O') {
      int c = get_byte(30);
      if (c < 0) return kEsc;
      if (c >= '1' && c <= '9') {
        int d = get_byte(30);
        while (d >= '0' && d <= '9') d = get_byte(30);
        if (d == '~') {
          switch (c) {
            case '1': return kHome;
            case '2': return kDel;  // insert-ish
            case '3': return kDel;
            case '4': return kEnd;
            case '5': return kPgUp;
            case '6': return kPgDn;
            case '7': return kHome;
            case '8': return kEnd;
            default: return kEsc;
          }
        }
        return kEsc;
      }
      switch (c) {
        case 'A': return kUp;
        case 'B': return kDown;
        case 'C': return kRight;
        case 'D': return kLeft;
        case 'H': return kHome;
        case 'F': return kEnd;
        default: return kEsc;
      }
    }
    pending_ = b;
    return kEsc;
  }

  bool raw_ = false;
  bool have_orig_ = false;
  int pending_ = 0;
  termios orig_{};
};

}  // namespace ct
