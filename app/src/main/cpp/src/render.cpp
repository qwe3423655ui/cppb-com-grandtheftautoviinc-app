#include "render.h"
#include <cctype>
#include <thread>

// ---------------------------------------------------------------- tables
static float g_noise[64 * 64];
static bool g_ready = false;
static void initTables() {
  if (g_ready) return;
  g_ready = true;
  Rng r; r.s = 12345u;
  for (float& f : g_noise) f = r.f();
}
static float vnoise(float x, float y) {
  int xi = (int)std::floor(x), yi = (int)std::floor(y);
  float fx = x - xi, fy = y - yi;
  fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
  auto n = [&](int a, int b) { return g_noise[((b & 63) << 6) | (a & 63)]; };
  return lerpf(lerpf(n(xi, yi), n(xi + 1, yi), fx), lerpf(n(xi, yi + 1), n(xi + 1, yi + 1), fx), fy);
}
static inline float fract(float x) { return x - std::floor(x); }
static inline uint32_t pack(Col c) {
  uint32_t r = (uint32_t)(std::sqrt(sat(c.r)) * 255.f + .5f), g = (uint32_t)(std::sqrt(sat(c.g)) * 255.f + .5f),
           b = (uint32_t)(std::sqrt(sat(c.b)) * 255.f + .5f);
  return 0xFF000000u | (b << 16) | (g << 8) | r;
}

Camera makeCamera(V3 pos, float yaw, float pitch, float fovy) {
  Camera c;
  c.pos = pos; c.fovy = fovy;
  c.fwd = {std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
  c.right = {-std::cos(yaw), 0, std::sin(yaw)};
  c.up = cross(c.right, c.fwd);
  return c;
}

// ---------------------------------------------------------------- setup
void Renderer::init(int w, int h) {
  initTables();
  W = w; H = h;
  px.assign(size_t(w) * h, 0xFF000000u);
  zb.assign(size_t(w) * h, 0.f);
  mask.assign(size_t(w) * h, 0);
  vig.resize(size_t(w) * h);
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      float nx = (x + .5f) / w * 2 - 1, ny = (y + .5f) / h * 2 - 1;
      vig[size_t(y) * w + x] = (uint8_t)(255.f * clampf(1.f - 0.26f * (nx * nx * 0.7f + ny * ny * 0.9f), 0.f, 1.f));
    }
  unsigned hc = std::thread::hardware_concurrency();
  threads = hc >= 4 ? 4 : (hc >= 2 ? 2 : 1);
}

void Renderer::begin(const Camera& c, const Sky& s) {
  cam = c; sky = s;
  focal = H * 0.5f / std::tan(cam.fovy * 0.5f);
  cx = W * 0.5f; cy = H * 0.5f;
  tanV = std::tan(cam.fovy * 0.5f); tanH = tanV * W / H;
  opq.clear(); shd.clear(); add.clear(); spr.clear();
  fogCol = sky.horizon;
  glassCol = mix(sky.horizon, sky.zenith, 0.35f) * 0.6f;
  winOn = 0.07f + 0.55f * sky.night;
  winLight = {1.35f, 0.9f, 0.5f};
  shadowStr = sky.shadowK;
}

// ---------------------------------------------------------------- submission
static int clipNear(const CV* in, int n, CV* out, float zn) {
  int m = 0;
  for (int i = 0; i < n; i++) {
    const CV& a = in[i];
    const CV& b = in[i + 1 == n ? 0 : i + 1];
    bool ai = a.z >= zn, bi = b.z >= zn;
    if (ai) out[m++] = a;
    if (ai != bi) {
      float t = (zn - a.z) / (b.z - a.z);
      out[m++] = {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, zn, a.u + (b.u - a.u) * t, a.v + (b.v - a.v) * t};
    }
  }
  return m;
}

void Renderer::pushTri(std::vector<STri>& L, const CV& a, const CV& b, const CV& c, uint8_t mat, Col base, Col lit, uint32_t seed) {
  STri t;
  const CV* v[3] = {&a, &b, &c};
  float iz[3], uz[3], vz[3];
  for (int i = 0; i < 3; i++) {
    iz[i] = 1.f / v[i]->z;
    t.x[i] = cx + focal * v[i]->x * iz[i];
    t.y[i] = cy - focal * v[i]->y * iz[i];
    uz[i] = v[i]->u * iz[i];
    vz[i] = v[i]->v * iz[i];
  }
  float xmin = std::min({t.x[0], t.x[1], t.x[2]}), xmax = std::max({t.x[0], t.x[1], t.x[2]});
  t.ymin = std::min({t.y[0], t.y[1], t.y[2]}); t.ymax = std::max({t.y[0], t.y[1], t.y[2]});
  if (!(xmax > 0 && xmin < W && t.ymax > 0 && t.ymin < H)) return;   // also rejects NaN
  float area = (t.x[1] - t.x[0]) * (t.y[2] - t.y[0]) - (t.x[2] - t.x[0]) * (t.y[1] - t.y[0]);
  if (std::fabs(area) < 1e-3f) return;
  float inv = 1.f / area;
  auto grad = [&](const float* f, float& gx, float& gy) {
    float d1 = f[1] - f[0], d2 = f[2] - f[0];
    gx = (d1 * (t.y[2] - t.y[0]) - d2 * (t.y[1] - t.y[0])) * inv;
    gy = (d2 * (t.x[1] - t.x[0]) - d1 * (t.x[2] - t.x[0])) * inv;
  };
  t.x0 = t.x[0]; t.y0 = t.y[0]; t.iz0 = iz[0]; t.uz0 = uz[0]; t.vz0 = vz[0];
  grad(iz, t.dizx, t.dizy); grad(uz, t.duzx, t.duzy); grad(vz, t.dvzx, t.dvzy);
  t.cr = base.r; t.cg = base.g; t.cb = base.b; t.lr = lit.r; t.lg = lit.g; t.lb = lit.b;
  t.seed = seed; t.mat = mat;
  L.push_back(t);
}

void Renderer::poly(std::vector<STri>& L, const CV* in, int n, uint8_t mat, Col base, Col lit, uint32_t seed) {
  CV out[8];
  int m = clipNear(in, n, out, 0.15f);
  for (int i = 1; i + 1 < m; i++) pushTri(L, out[0], out[i], out[i + 1], mat, base, lit, seed);
}

void Renderer::quad(V3 a, V3 b, V3 c, V3 d, V3 n, Mat m, Col base, uint32_t seed, bool two) {
  if (!two && dot(n, cam.pos - a) <= 0.f) return;
  Col lit{1, 1, 1};
  if (m != M_EMIT) {
    float nl = std::max(0.f, dot(n, sky.lightDir));
    lit = mix(sky.ambGround, sky.ambSky, 0.5f + 0.5f * n.y) + sky.lightCol * nl;
  }
  V3 P[4] = {a, b, c, d};
  CV v[4];
  for (int i = 0; i < 4; i++) {
    V3 dd = P[i] - cam.pos;
    v[i] = {dot(dd, cam.right), dot(dd, cam.up), dot(dd, cam.fwd), 0, 0};
    if (m == M_ROADX || m == M_ROADZ || m == M_ISECT || m == M_PAVE || m == M_GRASS) { v[i].u = P[i].x; v[i].v = P[i].z; }
    else if (m == M_FACADE) { v[i].u = std::fabs(n.x) > std::fabs(n.z) ? P[i].z : P[i].x; v[i].v = P[i].y; }
  }
  if (m == M_FLAT || m == M_EMIT) { base = base * lit; lit = {1, 1, 1}; }
  poly(opq, v, 4, m, base, lit, seed);
}

void Renderer::solid(const V3 q[8], Col base, Mat m, uint32_t seed) {
  static const int F[6][4] = {{0, 1, 2, 3}, {4, 5, 6, 7}, {0, 1, 5, 4}, {1, 2, 6, 5}, {2, 3, 7, 6}, {3, 0, 4, 7}};
  V3 cen;
  for (int i = 0; i < 8; i++) cen = cen + q[i];
  cen = cen * 0.125f;
  for (const auto& f : F) {
    V3 a = q[f[0]], b = q[f[1]], c = q[f[2]], d = q[f[3]];
    V3 n = cross(b - a, d - a);
    float l = len(n);
    if (l < 1e-7f) continue;
    n = n * (1.f / l);
    if (dot(n, (a + b + c + d) * 0.25f - cen) < 0) n = n * -1.f;
    quad(a, b, c, d, n, m, base, seed);
  }
}

void Renderer::box(V3 c, V3 r, V3 u, V3 f, V3 h, Col col, Mat m) {
  static const float sx[4] = {-1, 1, 1, -1}, sz[4] = {-1, -1, 1, 1};
  V3 q[8];
  int k = 0;
  for (int yy = 0; yy < 2; yy++)
    for (int i = 0; i < 4; i++) q[k++] = c + r * (sx[i] * h.x) + u * ((yy ? 1.f : -1.f) * h.y) + f * (sz[i] * h.z);
  solid(q, col, m);
}

void Renderer::decal(V3 a, V3 b, V3 c, V3 d, DecalKind k, Col col, float strength) {
  V3 P[4] = {a, b, c, d};
  float U[4] = {-1, 1, 1, -1}, Vv[4] = {k == D_BEAM ? 0.f : -1.f, k == D_BEAM ? 0.f : -1.f, 1, 1};
  CV v[4];
  for (int i = 0; i < 4; i++) {
    V3 dd = P[i] - cam.pos;
    v[i] = {dot(dd, cam.right), dot(dd, cam.up), dot(dd, cam.fwd), U[i], Vv[i]};
  }
  bool isShadow = (k == D_SHADOW_BLOB || k == D_SHADOW_HARD);
  poly(isShadow ? shd : add, v, 4, k, col, Col{strength, strength, strength}, 0);
}

void Renderer::sprite(V3 pos, float radius, Col col, float intensity, bool additive) {
  V3 dd = pos - cam.pos;
  float z = dot(dd, cam.fwd);
  if (z < 0.6f) return;
  float iz = 1.f / (z - 0.3f);
  float x = cx + focal * dot(dd, cam.right) / z, y = cy - focal * dot(dd, cam.up) / z;
  float r = std::min(260.f, radius * focal / z);
  if (r < 0.7f || x + r < 0 || x - r > W || y + r < 0 || y - r > H) return;
  spr.push_back({x, y, r, iz, col.r, col.g, col.b, intensity, additive});
}

bool Renderer::project(V3 p, float& sx, float& sy, float& z) const {
  V3 dd = p - cam.pos;
  z = dot(dd, cam.fwd);
  if (z < 0.3f) return false;
  sx = cx + focal * dot(dd, cam.right) / z;
  sy = cy - focal * dot(dd, cam.up) / z;
  return true;
}

bool Renderer::inView(V3 c, float rad, float maxDist) const {
  V3 d = c - cam.pos;
  float zc = dot(d, cam.fwd);
  if (zc < -rad || dot(d, d) > (maxDist + rad) * (maxDist + rad)) return false;
  float zz = std::max(zc, 0.f);
  if (std::fabs(dot(d, cam.right)) > zz * tanH + rad * 1.4f) return false;
  if (std::fabs(dot(d, cam.up)) > zz * tanV + rad * 1.4f) return false;
  return true;
}

// ---------------------------------------------------------------- scanline walker
template <class F>
static inline void walk(const STri& t, int y0, int y1, int W, F&& f) {
  int ya = std::max(y0, (int)std::ceil(t.ymin - 0.5f));
  int yb = std::min(y1 - 1, (int)std::ceil(t.ymax - 0.5f) - 1);
  for (int y = ya; y <= yb; y++) {
    float yc = y + 0.5f, xl = 1e30f, xr = -1e30f;
    for (int e = 0; e < 3; e++) {
      int j = e == 2 ? 0 : e + 1;
      float ay = t.y[e], by = t.y[j];
      if ((ay <= yc && yc < by) || (by <= yc && yc < ay)) {
        float x = t.x[e] + (yc - ay) * (t.x[j] - t.x[e]) / (by - ay);
        xl = std::min(xl, x); xr = std::max(xr, x);
      }
    }
    if (xl > xr) continue;
    float fxs = std::ceil(xl - 0.5f), fxe = std::ceil(xr - 0.5f) - 1.f;
    if (fxs < 0) fxs = 0;
    if (fxe > W - 1) fxe = (float)(W - 1);
    int xs = (int)fxs, xe = (int)fxe;
    if (xs > xe) continue;
    float iz = t.iz0 + t.dizy * (yc - t.y0) + t.dizx * (xs + 0.5f - t.x0);
    for (int x = xs; x <= xe; x++, iz += t.dizx) f(x, y, iz);
  }
}

// ---------------------------------------------------------------- procedural materials
static const Col C_YELLOW = rgb(235, 185, 40), C_WHITE = rgb(228, 228, 222);

template <int M>
static inline Col shade(const Renderer& R, const STri& t, float u, float v, float d) {
  Col base{t.cr, t.cg, t.cb}, lit{t.lr, t.lg, t.lb};
  float k = sat(1.45f - d * (1.f / 130.f));          // detail fades with distance (anti-shimmer)
  if constexpr (M == M_ROADX || M == M_ROADZ || M == M_ISECT) {
    float a = (M == M_ROADZ) ? v : u, b = (M == M_ROADZ) ? u : v;
    uint32_t h = hash32((uint32_t)(int)std::floor(a * 0.8f) * 73856093u ^ (uint32_t)(int)std::floor(b * 0.8f) * 19349663u);
    Col c = base * (0.92f + 0.08f * float(h & 255) * (1.f / 255.f));
    if constexpr (M != M_ISECT) {
      float db = std::fabs(b - std::floor(b * (1.f / 75.f) + .5f) * 75.f);
      float da = std::fabs(a - std::floor(a * (1.f / 75.f) + .5f) * 75.f);
      bool yel = db > 0.14f && db < 0.46f, wht = false;
      if (!yel) {
        if (db > 3.62f && db < 3.84f && da > 16.f && fract(a * (1.f / 9.f)) < 0.55f) wht = true;
        else if (db > 7.0f && db < 7.2f) wht = true;
        else if (da > 10.5f && da < 13.5f && db < 8.8f && fract(db * (1.f / 1.5f)) < 0.55f) wht = true;
        else if (da > 14.0f && da < 14.55f && db > 0.6f && db < 7.0f) wht = true;
      }
      if (yel || wht) c = mix(c, yel ? C_YELLOW : C_WHITE, k);
    }
    return c * lit;
  } else if constexpr (M == M_PAVE) {
    uint32_t h = hash32((uint32_t)(int)std::floor(u * 0.625f) * 73856093u ^ (uint32_t)(int)std::floor(v * 0.625f) * 19349663u);
    Col c = base * (0.93f + 0.07f * float(h & 255) * (1.f / 255.f));
    if (fract(u * 0.625f) < 0.04f || fract(v * 0.625f) < 0.04f) c = mix(c, c * 0.78f, k);
    return c * lit;
  } else if constexpr (M == M_GRASS) {
    uint32_t h = hash32((uint32_t)(int)std::floor(u * 0.45f) * 73856093u ^ (uint32_t)(int)std::floor(v * 0.45f) * 19349663u);
    uint32_t h2 = hash32((uint32_t)(int)std::floor(u * 0.09f) * 83492791u ^ (uint32_t)(int)std::floor(v * 0.09f) * 2654435761u);
    float n = (0.82f + 0.36f * float(h & 255) * (1.f / 255.f)) * (0.88f + 0.24f * float(h2 & 255) * (1.f / 255.f));
    return base * (1.f + (n - 1.f) * k) * lit;
  } else {  // M_FACADE
    const float PU = 3.2f, PV = 3.6f;
    float cu = u * (1.f / PU), cv = v * (1.f / PV);
    int iu = (int)std::floor(cu), iv = (int)std::floor(cv);
    float fu = cu - iu, fv = cv - iv;
    Col wall = base * lit;
    Col avg = mix(wall, R.glassCol, 0.30f) + R.winLight * (0.12f * R.winOn);
    if (k <= 0.f) return avg;
    bool shop = v < 3.9f;
    bool win = shop ? (fu > 0.08f && fu < 0.92f && fv > 0.10f && fv < 0.72f) : (fu > 0.2f && fu < 0.8f && fv > 0.2f && fv < 0.8f);
    uint32_t h = hash32((uint32_t)(iu * 374761393 + iv * 668265263) ^ t.seed);
    Col c;
    if (win) {
      bool on = (h & 1023) < (uint32_t)(R.winOn * (shop ? 1.3f : 1.f) * 1023.f);
      if (on) c = R.winLight * ((0.35f + 0.65f * R.sky.night) * (0.65f + 0.6f * float((h >> 10) & 255) * (1.f / 255.f)));
      else c = R.glassCol * (0.55f + 0.55f * (1.f - fv)) + lit * 0.02f;
    } else {
      c = wall * (fv < 0.10f ? 0.8f : 1.f);
    }
    return avg + (c - avg) * k;
  }
}

template <int M>
void Renderer::drawOne(const STri& t, int y0, int y1) {
  float* zbuf = zb.data();
  uint32_t* pix = px.data();
  const int Wd = W;
  const float fS = fogS, fI = fogInv;
  const Col fc = fogCol;
  walk(t, y0, y1, Wd, [&](int x, int y, float iz) {
    int idx = y * Wd + x;
    if (iz <= zbuf[idx]) return;
    zbuf[idx] = iz;
    float d = 1.f / iz;
    Col c;
    if constexpr (M == M_FLAT || M == M_EMIT) c = {t.cr, t.cg, t.cb};
    else {
      float xc = x + 0.5f - t.x0, yc = y + 0.5f - t.y0;
      float uz = t.uz0 + t.duzx * xc + t.duzy * yc, vz = t.vz0 + t.dvzx * xc + t.dvzy * yc;
      c = shade<M>(*this, t, uz * d, vz * d, d);
    }
    float f = sat((d - fS) * fI);
    f *= f;
    c = {c.r + (fc.r - c.r) * f, c.g + (fc.g - c.g) * f, c.b + (fc.b - c.b) * f};
    pix[idx] = pack(c);
  });
}

// ---------------------------------------------------------------- sky
void Renderer::skyFill(int y0, int y1) {
  const float invF = 1.f / focal;
  for (int y = y0; y < y1; y++) {
    float ry = (cy - (y + 0.5f)) * invF;
    for (int x = 0; x < W; x++) {
      size_t idx = size_t(y) * W + x;
      if (zb[idx] > 0.f) continue;
      float rx = ((x + 0.5f) - cx) * invF;
      float il = 1.f / std::sqrt(1.f + rx * rx + ry * ry);
      V3 dir = (cam.fwd + cam.right * rx + cam.up * ry) * il;
      float e = dir.y;
      Col c = mix(sky.horizon, sky.zenith, std::sqrt(sat(e)));
      float sd = dot(dir, sky.sunDir);
      if (sd > 0.6f) {
        float s2 = sd * sd, s4 = s2 * s2, s8 = s4 * s4, s16 = s8 * s8, s32 = s16 * s16;
        float glow = s8 * 0.16f + s32 * 0.35f + (sd > 0.99985f ? 5.f : 0.f);
        c = c + sky.sunCol * (glow * sky.sunVis);
      }
      if (e > 0.03f) {
        float inv = 1.f / e;
        float cu = dir.x * inv * 0.5f + sky.time * 0.004f, cv = dir.z * inv * 0.5f;
        float n = vnoise(cu * 2.f, cv * 2.f) * 0.55f + vnoise(cu * 4.3f, cv * 4.3f) * 0.3f + vnoise(cu * 9.1f + 7.f, cv * 9.1f) * 0.15f;
        float cl = smooth(0.50f, 0.76f, n) * smooth(0.03f, 0.25f, e);
        c = mix(c, sky.cloud * (0.8f + 0.4f * sat(sd)), cl * 0.85f);
      }
      if (sky.night > 0.35f && e > 0.04f) {
        uint32_t h = hash32((uint32_t)(int)std::floor(dir.x * 170.f) * 73856093u ^ (uint32_t)(int)std::floor(dir.z * 170.f) * 19349663u ^ (uint32_t)(int)std::floor(dir.y * 170.f) * 83492791u);
        if ((h & 511) == 0) { float b = 0.5f + 0.5f * float((h >> 9) & 255) * (1.f / 255.f); c = c + Col{b, b, b * 1.1f} * ((sky.night - 0.35f) * 1.2f); }
      }
      px[idx] = pack(c);
    }
  }
}

// ---------------------------------------------------------------- one horizontal band (thread-safe, rows [y0,y1))
void Renderer::band(int y0, int y1) {
  std::fill(zb.begin() + size_t(y0) * W, zb.begin() + size_t(y1) * W, 0.f);
  std::fill(mask.begin() + size_t(y0) * W, mask.begin() + size_t(y1) * W, (uint8_t)0);
  for (const STri& t : opq) {
    if (t.ymax < y0 || t.ymin > y1) continue;
    switch (t.mat) {
      case M_FLAT: drawOne<M_FLAT>(t, y0, y1); break;
      case M_EMIT: drawOne<M_EMIT>(t, y0, y1); break;
      case M_ROADX: drawOne<M_ROADX>(t, y0, y1); break;
      case M_ROADZ: drawOne<M_ROADZ>(t, y0, y1); break;
      case M_ISECT: drawOne<M_ISECT>(t, y0, y1); break;
      case M_PAVE: drawOne<M_PAVE>(t, y0, y1); break;
      case M_GRASS: drawOne<M_GRASS>(t, y0, y1); break;
      default: drawOne<M_FACADE>(t, y0, y1); break;
    }
  }
  skyFill(y0, y1);

  // soft shadows -> max-mask, applied once (no double darkening)
  for (const STri& t : shd) {
    if (t.ymax < y0 || t.ymin > y1) continue;
    const bool blob = t.mat == D_SHADOW_BLOB;
    const float strength = t.lr * 255.f;
    walk(t, y0, y1, W, [&](int x, int y, float iz) {
      size_t idx = size_t(y) * W + x;
      if (iz < zb[idx] * 0.998f) return;
      float a = 1.f;
      if (blob) {
        float d = 1.f / iz, xc = x + 0.5f - t.x0, yc = y + 0.5f - t.y0;
        float u = (t.uz0 + t.duzx * xc + t.duzy * yc) * d, v = (t.vz0 + t.dvzx * xc + t.dvzy * yc) * d;
        float q = u * u + v * v;
        if (q >= 1.f) return;
        a = 1.f - q; a = std::min(1.f, a * 1.6f);
      }
      uint8_t m8 = (uint8_t)std::min(255.f, a * strength);
      if (m8 > mask[idx]) mask[idx] = m8;
    });
  }
  const float sk = shadowStr / 255.f;
  for (int y = y0; y < y1; y++)
    for (int x = 0; x < W; x++) {
      size_t idx = size_t(y) * W + x;
      uint8_t m = mask[idx];
      if (!m) continue;
      float s = 1.f - m * sk;
      uint32_t p = px[idx];
      uint32_t r = (uint32_t)((p & 255) * s), g = (uint32_t)(((p >> 8) & 255) * s), b = (uint32_t)(((p >> 16) & 255) * s);
      px[idx] = 0xFF000000u | (b << 16) | (g << 8) | r;
    }

  // additive decals (glow pools, rings, beams, tracers)
  for (const STri& t : add) {
    if (t.ymax < y0 || t.ymin > y1) continue;
    walk(t, y0, y1, W, [&](int x, int y, float iz) {
      size_t idx = size_t(y) * W + x;
      if (iz < zb[idx] * 0.998f) return;
      float d = 1.f / iz, xc = x + 0.5f - t.x0, yc = y + 0.5f - t.y0;
      float u = (t.uz0 + t.duzx * xc + t.duzy * yc) * d, v = (t.vz0 + t.dvzx * xc + t.dvzy * yc) * d;
      float a;
      switch (t.mat) {
        case D_GLOW: { float q = u * u + v * v; if (q >= 1.f) return; a = (1.f - q) * (1.f - q); break; }
        case D_RING: { float r = std::sqrt(u * u + v * v); if (r > 1.f) return; a = sat(1.f - std::fabs(r - 0.9f) * 16.f) + (r < 0.9f ? 0.10f * (1.f - r) : 0.f); break; }
        case D_BEAM: { float q = 1.f - std::fabs(u); a = q * q * (1.f - v) * 0.8f; if (a <= 0) return; break; }
        default: a = 1.f;
      }
      a *= t.lr;
      uint32_t p = px[idx];
      float r = (p & 255) + t.cr * 255.f * a, g = ((p >> 8) & 255) + t.cg * 255.f * a, b = ((p >> 16) & 255) + t.cb * 255.f * a;
      px[idx] = 0xFF000000u | ((uint32_t)std::min(255.f, b) << 16) | ((uint32_t)std::min(255.f, g) << 8) | (uint32_t)std::min(255.f, r);
    });
  }

  // sprites (glows, smoke, muzzle flashes)
  for (const SSpr& s : spr) {
    int ya = std::max(y0, (int)std::floor(s.y - s.r)), yb = std::min(y1 - 1, (int)std::ceil(s.y + s.r));
    int xa = std::max(0, (int)std::floor(s.x - s.r)), xb = std::min(W - 1, (int)std::ceil(s.x + s.r));
    float ir2 = 1.f / (s.r * s.r);
    for (int y = ya; y <= yb; y++)
      for (int x = xa; x <= xb; x++) {
        float dx = x + 0.5f - s.x, dy = y + 0.5f - s.y, q = (dx * dx + dy * dy) * ir2;
        if (q >= 1.f) continue;
        size_t idx = size_t(y) * W + x;
        if (s.iz <= zb[idx]) continue;
        float a = (1.f - q) * (1.f - q) * s.a;
        uint32_t p = px[idx];
        float r = p & 255, g = (p >> 8) & 255, b = (p >> 16) & 255;
        if (s.add) { r += s.cr * 255.f * a; g += s.cg * 255.f * a; b += s.cb * 255.f * a; }
        else { a = sat(a); r += (s.cr * 255.f - r) * a; g += (s.cg * 255.f - g) * a; b += (s.cb * 255.f - b) * a; }
        px[idx] = 0xFF000000u | ((uint32_t)std::min(255.f, b) << 16) | ((uint32_t)std::min(255.f, g) << 8) | (uint32_t)std::min(255.f, r);
      }
  }

  // vignette
  for (int y = y0; y < y1; y++)
    for (int x = 0; x < W; x++) {
      size_t idx = size_t(y) * W + x;
      uint32_t p = px[idx], v = vig[idx];
      uint32_t r = ((p & 255) * v) >> 8, g = (((p >> 8) & 255) * v) >> 8, b = (((p >> 16) & 255) * v) >> 8;
      px[idx] = 0xFF000000u | (b << 16) | (g << 8) | r;
    }
}

void Renderer::end() {
  int n = std::max(1, std::min(threads, H / 32));
  if (n == 1) { band(0, H); return; }
  std::vector<std::thread> th;
  for (int i = 1; i < n; i++) th.emplace_back([this, i, n] { band(H * i / n, H * (i + 1) / n); });
  band(0, H / n);
  for (auto& t : th) t.join();
}

// ---------------------------------------------------------------- 2D overlay
void Renderer::put(int x, int y, uint32_t c, float a) {
  if (x < 0 || y < 0 || x >= W || y >= H || a <= 0.f) return;
  uint32_t& p = px[size_t(y) * W + x];
  uint32_t sr = (c >> 16) & 255, sg = (c >> 8) & 255, sb = c & 255;
  if (a >= 1.f) { p = 0xFF000000u | (sb << 16) | (sg << 8) | sr; return; }
  float dr = p & 255, dg = (p >> 8) & 255, db = (p >> 16) & 255;
  p = 0xFF000000u | ((uint32_t)(db + (sb - db) * a) << 16) | ((uint32_t)(dg + (sg - dg) * a) << 8) | (uint32_t)(dr + (sr - dr) * a);
}
void Renderer::rect(int x, int y, int w, int h, uint32_t c, float a) {
  for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) put(i, j, c, a);
}
void Renderer::disc(float x, float y, float r, uint32_t c, float a) {
  for (int j = (int)std::floor(y - r - 1); j <= (int)std::ceil(y + r + 1); j++)
    for (int i = (int)std::floor(x - r - 1); i <= (int)std::ceil(x + r + 1); i++) {
      float d = std::sqrt((i + .5f - x) * (i + .5f - x) + (j + .5f - y) * (j + .5f - y));
      put(i, j, c, a * sat(r + 0.5f - d));
    }
}
void Renderer::ring(float x, float y, float r, float th, uint32_t c, float a) {
  float o = r + th * 0.5f;
  for (int j = (int)std::floor(y - o - 1); j <= (int)std::ceil(y + o + 1); j++)
    for (int i = (int)std::floor(x - o - 1); i <= (int)std::ceil(x + o + 1); i++) {
      float d = std::sqrt((i + .5f - x) * (i + .5f - x) + (j + .5f - y) * (j + .5f - y));
      put(i, j, c, a * sat(th * 0.5f + 0.5f - std::fabs(d - r)));
    }
}
void Renderer::line(float x0, float y0, float x1, float y1, uint32_t c, float a) {
  int n = (int)std::max(std::fabs(x1 - x0), std::fabs(y1 - y0)) + 1;
  for (int i = 0; i <= n; i++) {
    float t = float(i) / n;
    put((int)std::floor(x0 + (x1 - x0) * t), (int)std::floor(y0 + (y1 - y0) * t), c, a);
  }
}
void Renderer::tri2(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t c, float a) {
  int xa = (int)std::floor(std::min({x0, x1, x2})), xb = (int)std::ceil(std::max({x0, x1, x2}));
  int ya = (int)std::floor(std::min({y0, y1, y2})), yb = (int)std::ceil(std::max({y0, y1, y2}));
  float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
  if (std::fabs(area) < 1e-4f) return;
  for (int j = ya; j <= yb; j++)
    for (int i = xa; i <= xb; i++) {
      float px_ = i + .5f, py_ = j + .5f;
      float w0 = ((x1 - px_) * (y2 - py_) - (x2 - px_) * (y1 - py_)) / area;
      float w1 = ((x2 - px_) * (y0 - py_) - (x0 - px_) * (y2 - py_)) / area;
      float w2 = 1.f - w0 - w1;
      if (w0 >= 0 && w1 >= 0 && w2 >= 0) put(i, j, c, a);
    }
}

// 5x7 bitmap font (uppercase, digits, punctuation)
static const char FCH[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.,:-+$!?/%()<>=*'";
static const uint8_t FONT[][7] = {
  {0, 0, 0, 0, 0, 0, 0},
  {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}, {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E},
  {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}, {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}, {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10},
  {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}, {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E},
  {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}, {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}, {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F},
  {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}, {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11}, {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},
  {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}, {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}, {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11},
  {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}, {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}, {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},
  {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}, {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}, {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11},
  {0x11, 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04}, {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F},
  {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}, {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}, {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F},
  {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}, {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E},
  {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}, {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}, {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E},
  {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C},
  {0, 0, 0, 0, 0, 0x0C, 0x0C}, {0, 0, 0, 0, 0x0C, 0x04, 0x08}, {0, 0x0C, 0x0C, 0, 0x0C, 0x0C, 0}, {0, 0, 0, 0x1F, 0, 0, 0},
  {0, 0x04, 0x04, 0x1F, 0x04, 0x04, 0}, {0x04, 0x0F, 0x14, 0x0E, 0x05, 0x1E, 0x04}, {0x04, 0x04, 0x04, 0x04, 0x04, 0, 0x04},
  {0x0E, 0x11, 0x01, 0x02, 0x04, 0, 0x04}, {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10}, {0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03},
  {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02}, {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08}, {0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02},
  {0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08}, {0, 0, 0x1F, 0, 0x1F, 0, 0}, {0, 0x04, 0x15, 0x0E, 0x15, 0x04, 0}, {0x04, 0x04, 0x08, 0, 0, 0, 0}};
static_assert(sizeof(FONT) / 7 == sizeof(FCH) - 1, "font table must match character list");

int Renderer::textW(const char* s, int scale) const {
  int n = (int)std::strlen(s);
  return n ? n * 6 * scale - scale : 0;
}

void Renderer::text(int x, int y, const char* s, int scale, uint32_t col, int align, bool shadow) {
  int w = textW(s, scale);
  if (align == 1) x -= w / 2; else if (align == 2) x -= w;
  for (int pass = shadow ? 0 : 1; pass < 2; pass++) {
    int ox = pass == 0 ? scale : 0, oy = pass == 0 ? scale : 0;
    uint32_t c = pass == 0 ? 0x000000u : col;
    float a = pass == 0 ? 0.65f : 1.f;
    int cx_ = x;
    for (const char* p = s; *p; p++, cx_ += 6 * scale) {
      char ch = (char)std::toupper((unsigned char)*p);
      const char* f = std::strchr(FCH, ch);
      if (!f || !ch) continue;
      const uint8_t* g = FONT[f - FCH];
      for (int r = 0; r < 7; r++)
        for (int b = 0; b < 5; b++)
          if (g[r] & (0x10 >> b)) rect(cx_ + b * scale + ox, y + r * scale + oy, scale, scale, c, a);
    }
  }
}
