// ClangTop++ - fixed capacity ring buffer for chart series
#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace ct {

template <std::size_t N>
class RingBuffer {
 public:
  static constexpr std::size_t capacity() { return N; }

  void push(double v) {
    if (!std::isfinite(v)) v = 0.0;
    data_[head_] = v;
    head_ = (head_ + 1) % N;
    if (count_ < N) ++count_;
  }

  std::size_t size() const { return count_; }
  bool empty() const { return count_ == 0; }

  // 0 = oldest, size()-1 = newest
  double at(std::size_t i) const {
    if (i >= count_) return 0.0;
    std::size_t start = (head_ + N - count_) % N;
    return data_[(start + i) % N];
  }
  double operator[](std::size_t i) const { return at(i); }

  double latest() const { return count_ ? at(count_ - 1) : 0.0; }

  double max() const {
    if (!count_) return 0.0;
    double m = at(0);
    for (std::size_t i = 1; i < count_; ++i) m = std::fmax(m, at(i));
    return m;
  }
  double min() const {
    if (!count_) return 0.0;
    double m = at(0);
    for (std::size_t i = 1; i < count_; ++i) m = std::fmin(m, at(i));
    return m;
  }
  double avg() const {
    if (!count_) return 0.0;
    double s = 0;
    for (std::size_t i = 0; i < count_; ++i) s += at(i);
    return s / static_cast<double>(count_);
  }

  // Copy the most recent `n` values into a flat vector (oldest -> newest).
  std::vector<double> tail(std::size_t n) const {
    if (n > count_) n = count_;
    std::vector<double> out;
    out.reserve(n);
    for (std::size_t i = count_ - n; i < count_; ++i) out.push_back(at(i));
    return out;
  }

  std::vector<double> all() const { return tail(count_); }
  void clear() { head_ = 0; count_ = 0; }

 private:
  std::array<double, N> data_{};
  std::size_t head_ = 0;
  std::size_t count_ = 0;
};

// A series with an auto-expanding ceiling, used by charts that need "nice" max.
class AutoScale {
 public:
  void observe(double v) {
    if (v > peak_) peak_ = v;
    else peak_ *= 0.9995;  // slow decay so labels shrink again
    if (peak_ < 1e-9) peak_ = 1e-9;
  }
  double peak() const { return peak_; }
  void reset() { peak_ = 0; }

 private:
  double peak_ = 0;
};

}  // namespace ct
