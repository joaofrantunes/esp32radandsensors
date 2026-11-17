#pragma once
#include <stdint.h>
#include <math.h>
#include <stdio.h>

// One bucket per actual acquisition interval. No interpolation of pulses.
namespace GeigerStats {
constexpr uint32_t INTERVAL_MS = 15000UL;
constexpr unsigned CAPACITY = 240;
struct Window {
  uint64_t counts;
  uint64_t elapsed;
  bool ready;
  double cpm() const { return elapsed ? 60000.0 * counts / elapsed : 0.0; }
};
struct Reading {
  uint32_t sampledAt, count, elapsed;
  uint64_t total;
  Window minute, fiveMinutes, hour;
};
class Accumulator {
  struct Bucket { uint32_t count, elapsed; } buckets[CAPACITY] = {};
  unsigned next = 0, size = 0;
  uint64_t total = 0;
  Window window(uint32_t target) const {
    Window w = {};
    for (unsigned i = 0; i < size; ++i) {
      const Bucket &b = buckets[(next + CAPACITY - 1 - i) % CAPACITY];
      w.counts += b.count;
      w.elapsed += b.elapsed;
      if (w.elapsed >= target) {
        // An unusually long bucket cannot describe the requested window.
        w.ready = w.elapsed <= uint64_t(target) + INTERVAL_MS;
        break;
      }
    }
    return w;
  }
public:
  Reading record(uint32_t now, uint32_t count, uint32_t elapsed) {
    total += count;
    buckets[next] = {count, elapsed};
    next = (next + 1) % CAPACITY;
    if (size < CAPACITY) ++size;
    return {now, count, elapsed, total, window(60000), window(300000), window(3600000)};
  }
};

inline void numberOrNull(char *out, size_t len, bool valid, double value, int precision = 3) {
  if (valid && isfinite(value)) snprintf(out, len, "%.*f", precision, value);
  else snprintf(out, len, "null");
}
// JSON is one coherent snapshot: raw pulses, exposure, and derived rates.
inline bool encode(const Reading &r, char *out, size_t len, double doseRatio = 0.0) {
  if (!r.elapsed) return false;
  char minute[32], five[32], hour[32], sigma[32], doseRate[40], doseTotal[40], ratio[32];
  numberOrNull(minute, sizeof minute, r.minute.ready, r.minute.cpm());
  numberOrNull(five, sizeof five, r.fiveMinutes.ready, r.fiveMinutes.cpm());
  numberOrNull(hour, sizeof hour, r.hour.ready, r.hour.cpm() * 60.0);
  // sqrt(N) is an approximate 1-sigma count uncertainty, not a dose error.
  // Suppress it for small samples, including zero (does not mean no uncertainty).
  numberOrNull(sigma, sizeof sigma, r.minute.ready && r.minute.counts >= 20,
               r.minute.elapsed ? 60000.0 * sqrt(double(r.minute.counts)) / r.minute.elapsed : 0.0);
  const double estimate = r.minute.ready ? r.minute.cpm() : 60000.0 * r.count / r.elapsed;
  numberOrNull(doseRate, sizeof doseRate, doseRatio > 0 && r.minute.ready, r.minute.cpm() * doseRatio, 6);
  numberOrNull(doseTotal, sizeof doseTotal, doseRatio > 0, r.total * doseRatio / 60.0, 6);
  numberOrNull(ratio, sizeof ratio, doseRatio > 0, doseRatio, 9);
  const int n = snprintf(out, len,
    "{\"count\":%lu,\"elapsed_ms\":%lu,\"total\":%llu,\"cpm\":%.3f,"
    "\"cpm60\":%s,\"cpm300\":%s,\"cph_est\":%.3f,\"estimate_ms\":%llu,"
    "\"cph_rolling\":%s,\"window60_ms\":%llu,\"window300_ms\":%llu,\"window3600_ms\":%llu,\"sigma_cpm\":%s,"
    "\"dose_rate_est\":%s,\"dose_total_est\":%s,\"dose_ratio\":%s}",
    static_cast<unsigned long>(r.count), static_cast<unsigned long>(r.elapsed),
    static_cast<unsigned long long>(r.total), 60000.0 * r.count / r.elapsed,
    minute, five, estimate * 60.0,
    static_cast<unsigned long long>(r.minute.ready ? r.minute.elapsed : r.elapsed), hour,
    static_cast<unsigned long long>(r.minute.ready ? r.minute.elapsed : 0),
    static_cast<unsigned long long>(r.fiveMinutes.ready ? r.fiveMinutes.elapsed : 0),
    static_cast<unsigned long long>(r.hour.ready ? r.hour.elapsed : 0), sigma, doseRate, doseTotal, ratio);
  return n > 0 && static_cast<size_t>(n) < len;
}
}
