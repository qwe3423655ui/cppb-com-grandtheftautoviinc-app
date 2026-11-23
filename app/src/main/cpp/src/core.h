// NEON CITY - core math / utility types (no external dependencies)
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

constexpr float PI = 3.14159265358979f;

struct V2 { float x = 0, z = 0; };           // ground-plane vector (x east, z south)
inline V2 operator+(V2 a, V2 b) { return {a.x + b.x, a.z + b.z}; }
inline V2 operator-(V2 a, V2 b) { return {a.x - b.x, a.z - b.z}; }
inline V2 operator*(V2 a, float s) { return {a.x * s, a.z * s}; }
inline float dot(V2 a, V2 b) { return a.x * b.x + a.z * b.z; }
inline float len(V2 a) { return std::sqrt(dot(a, a)); }
inline V2 norm(V2 a) { float l = len(a); return l > 1e-6f ? a * (1.f / l) : V2{0, 1}; }

struct V3 { float x = 0, y = 0, z = 0; };
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float len(V3 a) { return std::sqrt(dot(a, a)); }
inline V3 norm(V3 a) { float l = len(a); return l > 1e-8f ? a * (1.f / l) : V3{0, 1, 0}; }
inline V3 v3(V2 a, float y = 0) { return {a.x, y, a.z}; }

struct Col { float r = 0, g = 0, b = 0; };   // linear-ish colour (gamma 2.0 convention)
inline Col operator+(Col a, Col b) { return {a.r + b.r, a.g + b.g, a.b + b.b}; }
inline Col operator-(Col a, Col b) { return {a.r - b.r, a.g - b.g, a.b - b.b}; }
inline Col operator*(Col a, float s) { return {a.r * s, a.g * s, a.b * s}; }
inline Col operator*(Col a, Col b) { return {a.r * b.r, a.g * b.g, a.b * b.b}; }
inline Col rgb(int r, int g, int b) { float x = r / 255.f, y = g / 255.f, z = b / 255.f; return {x * x, y * y, z * z}; }
inline Col mix(Col a, Col b, float t) { return a + (b - a) * t; }

inline float sat(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float smooth(float a, float b, float x) { float t = sat((x - a) / (b - a)); return t * t * (3 - 2 * t); }
inline float wrapPi(float a) { a = std::fmod(a + PI, 2 * PI); if (a < 0) a += 2 * PI; return a - PI; }
inline uint32_t hash32(uint32_t x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }

struct Rng {
  uint32_t s = 2463534242u;
  uint32_t u() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
  float f() { return (u() >> 8) * (1.f / 16777216.f); }
  float r(float a, float b) { return a + (b - a) * f(); }
  int i(int n) { return int(u() % uint32_t(n)); }
};
