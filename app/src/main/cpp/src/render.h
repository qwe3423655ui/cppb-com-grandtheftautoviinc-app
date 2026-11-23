// NEON CITY - tiny multithreaded software 3D renderer (pure C++, no GPU / no libs)
#pragma once
#include "core.h"

enum Mat : uint8_t { M_FLAT, M_EMIT, M_ROADX, M_ROADZ, M_ISECT, M_PAVE, M_GRASS, M_FACADE };
enum DecalKind : uint8_t { D_SHADOW_BLOB, D_SHADOW_HARD, D_GLOW, D_RING, D_BEAM, D_FLAT };

struct Sky {
  Col zenith, horizon, sunCol, ambSky, ambGround, lightCol, cloud;
  V3 lightDir{0, 1, 0}, sunDir{0, 1, 0};
  float night = 0, sunVis = 1, time = 0, shadowK = 0.5f;
};

struct Camera {
  V3 pos, fwd{0, 0, 1}, right{-1, 0, 0}, up{0, 1, 0};
  float fovy = 1.05f;
};
Camera makeCamera(V3 pos, float yaw, float pitch, float fovy);   // forward = (sin y cos p, sin p, cos y cos p)

struct STri {
  float x[3], y[3];
  float x0, y0, iz0, uz0, vz0, dizx, dizy, duzx, duzy, dvzx, dvzy;
  float cr, cg, cb, lr, lg, lb;
  uint32_t seed;
  uint8_t mat;
  float ymin, ymax;
};
struct SSpr { float x, y, r, iz, cr, cg, cb, a; bool add; };
struct CV { float x, y, z, u, v; };

class Renderer {
 public:
  int W = 0, H = 0, threads = 1;
  std::vector<uint32_t> px;                 // RGBA8888 (byte order R,G,B,A)
  Camera cam;
  Sky sky;
  float tanH = 1, tanV = 1, focal = 1, cx = 0, cy = 0, fogS = 70, fogInv = 1.f / 480.f, shadowStr = 0.5f, winOn = 0.2f;
  Col fogCol, glassCol, winLight;

  void init(int w, int h);
  void begin(const Camera& c, const Sky& s);
  void end();                               // rasterise everything submitted since begin()

  // --- 3D submission (world space). Normals are outward; back faces are culled.
  void quad(V3 a, V3 b, V3 c, V3 d, V3 n, Mat m, Col base, uint32_t seed = 0, bool twoSided = false);
  void solid(const V3 q[8], Col base, Mat m = M_FLAT, uint32_t seed = 0);   // convex hexahedron (bottom 0-3, top 4-7)
  void box(V3 c, V3 r, V3 u, V3 f, V3 half, Col col, Mat m = M_FLAT);
  void decal(V3 a, V3 b, V3 c, V3 d, DecalKind k, Col col, float strength);
  void sprite(V3 pos, float radius, Col col, float intensity, bool additive = true);
  bool project(V3 p, float& sx, float& sy, float& z) const;
  bool inView(V3 c, float rad, float maxDist) const;   // bounding-sphere frustum test

  // --- 2D overlay (call after end()); colours are 0xRRGGBB display values
  void rect(int x, int y, int w, int h, uint32_t rgb, float a = 1);
  void disc(float x, float y, float r, uint32_t rgb, float a = 1);
  void ring(float x, float y, float r, float th, uint32_t rgb, float a = 1);
  void line(float x0, float y0, float x1, float y1, uint32_t rgb, float a = 1);
  void tri2(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t rgb, float a = 1);
  void text(int x, int y, const char* s, int scale, uint32_t rgb, int align = 0, bool shadow = true);
  int textW(const char* s, int scale) const;

 private:
  std::vector<STri> opq, shd, add;
  std::vector<SSpr> spr;
  std::vector<float> zb;
  std::vector<uint8_t> mask, vig;
  void poly(std::vector<STri>& L, const CV* in, int n, uint8_t mat, Col base, Col lit, uint32_t seed);
  void pushTri(std::vector<STri>& L, const CV& a, const CV& b, const CV& c, uint8_t mat, Col base, Col lit, uint32_t seed);
  void band(int y0, int y1);
  void skyFill(int y0, int y1);
  template <int M> void drawOne(const STri& t, int y0, int y1);
  void put(int x, int y, uint32_t rgb, float a);
};
