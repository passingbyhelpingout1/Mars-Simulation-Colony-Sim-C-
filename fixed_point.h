// fixed_point.h
#pragma once
#include <cstdint>
#include <cmath>

struct FP {
  static constexpr int64_t SCALE = 1000;        // 3 decimal places
  int64_t raw = 0;

  static FP fromDouble(double x) { return FP{ (int64_t)llround(x * SCALE) }; }
  static FP fromInt(int64_t x)   { return FP{ x * SCALE }; }
  double toDouble() const        { return (double)raw / SCALE; }

  // basic ops (all exact on the scaled integers)
  FP& operator+=(FP o) { raw += o.raw; return *this; }
  FP& operator-=(FP o) { raw -= o.raw; return *this; }
  friend FP operator+(FP a, FP b){ return FP{a.raw + b.raw}; }
  friend FP operator-(FP a, FP b){ return FP{a.raw - b.raw}; }

  // multiply/divide by scalar (use with hours, counts, etc.)
  friend FP operator*(FP a, int64_t k){ return FP{ a.raw * k }; }
  friend FP operator/(FP a, int64_t k){ return FP{ a.raw / k }; }

  // rare: fixed*fixed -> fixed (watch for overflow if values can be huge)
  static FP mul(FP a, FP b){ return FP{ (a.raw * b.raw) / SCALE }; }
};
