// Fixed-size, lock-free, allocation-free single-producer/single-consumer
// primitives used between the audio thread and one other thread (SPEC §9.4).
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace vdx::util {

// Ring of trivially copyable T. Capacity is a power of two (allocated once).
template <typename T>
class SpscQueue {
 public:
  explicit SpscQueue(size_t capacityPow2) : buf_(capacityPow2), mask_(capacityPow2 - 1) {}

  bool push(const T& v) {
    const size_t w = write_.load(std::memory_order_relaxed);
    const size_t r = read_.load(std::memory_order_acquire);
    if (w - r >= buf_.size()) return false;
    buf_[w & mask_] = v;
    write_.store(w + 1, std::memory_order_release);
    return true;
  }
  bool pop(T& out) {
    const size_t r = read_.load(std::memory_order_relaxed);
    const size_t w = write_.load(std::memory_order_acquire);
    if (r == w) return false;
    out = buf_[r & mask_];
    read_.store(r + 1, std::memory_order_release);
    return true;
  }
  size_t size() const {
    return write_.load(std::memory_order_acquire) - read_.load(std::memory_order_acquire);
  }
  size_t capacity() const { return buf_.size(); }
  void clear() { read_.store(write_.load()); }

 private:
  std::vector<T> buf_;
  size_t mask_;
  alignas(64) std::atomic<size_t> write_{0};
  alignas(64) std::atomic<size_t> read_{0};
};

// SPSC byte stream of length-prefixed messages (transmitted MIDI).
class SpscByteQueue {
 public:
  explicit SpscByteQueue(size_t capacityPow2) : buf_(capacityPow2), mask_(capacityPow2 - 1) {}

  // Writes one message (u16 length + bytes) or nothing if it does not fit.
  bool push(const uint8_t* data, int len) {
    if (len <= 0 || len > 0xFFFF) return false;
    const size_t w = write_.load(std::memory_order_relaxed);
    const size_t r = read_.load(std::memory_order_acquire);
    const size_t need = size_t(len) + 2;
    if (buf_.size() - (w - r) < need) return false;
    put(w, uint8_t(len & 0xFF));
    put(w + 1, uint8_t(len >> 8));
    for (int i = 0; i < len; ++i) put(w + 2 + size_t(i), data[i]);
    write_.store(w + need, std::memory_order_release);
    return true;
  }
  // Copies whole messages (raw bytes, no prefix) into dst; returns bytes copied.
  int pop(uint8_t* dst, int cap) {
    size_t r = read_.load(std::memory_order_relaxed);
    const size_t w = write_.load(std::memory_order_acquire);
    int out = 0;
    while (r != w) {
      const int len = int(get(r)) | (int(get(r + 1)) << 8);
      if (out + len > cap) break;
      for (int i = 0; i < len; ++i) dst[out + i] = get(r + 2 + size_t(i));
      out += len;
      r += size_t(len) + 2;
    }
    read_.store(r, std::memory_order_release);
    return out;
  }
  // Pops one message; returns its length (0 if none, -len if it does not fit).
  int popOne(uint8_t* dst, int cap) {
    size_t r = read_.load(std::memory_order_relaxed);
    const size_t w = write_.load(std::memory_order_acquire);
    if (r == w) return 0;
    const int len = int(get(r)) | (int(get(r + 1)) << 8);
    if (len > cap) return -len;
    for (int i = 0; i < len; ++i) dst[i] = get(r + 2 + size_t(i));
    read_.store(r + size_t(len) + 2, std::memory_order_release);
    return len;
  }

 private:
  void put(size_t i, uint8_t v) { buf_[i & mask_] = v; }
  uint8_t get(size_t i) const { return buf_[i & mask_]; }
  std::vector<uint8_t> buf_;
  size_t mask_;
  alignas(64) std::atomic<size_t> write_{0};
  alignas(64) std::atomic<size_t> read_{0};
};

// Pool of fixed-size payload slots. Any thread may acquire a free slot; the
// thread that consumes the referencing message releases it.
class SlotPool {
 public:
  SlotPool(int count, int slotBytes) : data_(size_t(count) * size_t(slotBytes)), used_(size_t(count)), slotBytes_(slotBytes) {
    for (auto& u : used_) u.store(0, std::memory_order_relaxed);
  }
  // Returns a slot index or -1.
  int acquire() {
    for (size_t i = 0; i < used_.size(); ++i) {
      uint8_t expected = 0;
      if (used_[i].compare_exchange_strong(expected, 1, std::memory_order_acq_rel)) return int(i);
    }
    return -1;
  }
  void release(int slot) {
    if (slot >= 0 && size_t(slot) < used_.size()) used_[size_t(slot)].store(0, std::memory_order_release);
  }
  uint8_t* data(int slot) { return data_.data() + size_t(slot) * size_t(slotBytes_); }
  int slotBytes() const { return slotBytes_; }
  int count() const { return int(used_.size()); }

 private:
  std::vector<uint8_t> data_;
  std::vector<std::atomic<uint8_t>> used_;
  int slotBytes_;
};

// Lock-free triple buffer: one writer publishes whole snapshots, one or more
// readers take the latest (readers serialise among themselves via a flag).
class SnapshotBuffer {
 public:
  explicit SnapshotBuffer(size_t bytes) {
    for (auto& b : bufs_) b.assign(bytes, 0);
    for (auto& l : lens_) l = 0;
  }
  // Writer: copy a snapshot in and publish it.
  void publish(const uint8_t* data, size_t len) {
    const int back = back_;
    std::memcpy(bufs_[size_t(back)].data(), data, len);
    lens_[size_t(back)] = len;
    // swap back <-> middle, mark fresh
    const int prev = middle_.exchange(back | kFresh, std::memory_order_acq_rel);
    back_ = prev & kIndexMask;
  }
  // Reader: copy the latest snapshot out.
  void read(std::vector<uint8_t>& out) {
    while (readerBusy_.exchange(true, std::memory_order_acquire)) {
    }
    int m = middle_.load(std::memory_order_acquire);
    if (m & kFresh) {
      const int prev = middle_.exchange(front_, std::memory_order_acq_rel);
      front_ = prev & kIndexMask;
    }
    out.assign(bufs_[size_t(front_)].begin(), bufs_[size_t(front_)].begin() + long(lens_[size_t(front_)]));
    readerBusy_.store(false, std::memory_order_release);
  }

 private:
  static constexpr int kFresh = 4;
  static constexpr int kIndexMask = 3;
  std::vector<uint8_t> bufs_[3];
  size_t lens_[3];
  int back_ = 0;                  // writer-owned
  int front_ = 1;                 // reader-owned
  std::atomic<int> middle_{2};    // shared
  std::atomic<bool> readerBusy_{false};
};

}  // namespace vdx::util
