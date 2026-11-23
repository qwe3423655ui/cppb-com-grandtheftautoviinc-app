#include "world.h"

int sigState(int ni, int nj, int d, bool left, float time) {
  (void)left;                                   // two-phase signal: left turns are permissive (they yield to oncoming traffic)
  float t = std::fmod(time + float((ni * 7 + nj * 13) % 26), 26.f);
  float u = t - ((d & 1) ? 0.f : 13.f);         // N/S green first, then E/W
  if (u < 0.f) u += 26.f;
  if (u < 10.f) return 2;
  if (u < 12.f) return 1;
  return 0;
}

Sky makeSky(float hour, float time) {
  Sky s;
  float phi = (hour - 6.f) / 24.f * 2.f * PI;
  V3 sun = norm(V3{std::cos(phi), std::sin(phi), -0.35f});
  float el = sun.y;
  float dayK = smooth(-0.06f, 0.22f, el);
  float warm = sat(1.f - std::fabs(el) * 3.2f) * smooth(-0.14f, 0.02f, el);
  s.sunDir = sun; s.time = time;
  s.night = 1.f - smooth(-0.12f, 0.12f, el);
  s.zenith = mix(mix(rgb(6, 9, 24), rgb(58, 108, 200), dayK), rgb(70, 90, 150), warm * 0.6f);
  s.horizon = mix(mix(rgb(16, 22, 44), rgb(176, 204, 232), dayK), rgb(255, 150, 96), warm * 0.85f);
  Col sc = mix(Col{1.15f, 1.05f, 0.88f}, Col{1.25f, 0.62f, 0.30f}, warm);
  float sunI = smooth(-0.02f, 0.2f, el);
  V3 moon = norm(V3{-sun.x, std::max(0.6f, -sun.y), -sun.z});
  s.lightDir = norm(moon * (1.f - sunI) + sun * sunI);
  s.lightCol = sc * sunI + Col{0.10f, 0.15f, 0.30f} * (1.f - sunI);
  s.sunCol = sc;
  s.sunVis = smooth(-0.10f, 0.0f, el);
  s.ambSky = mix(mix(rgb(40, 55, 105) * 0.8f, rgb(130, 160, 215) * 0.62f, dayK), rgb(190, 130, 120) * 0.45f, warm * 0.5f);
  s.ambGround = mix(rgb(22, 24, 36) * 0.9f, rgb(105, 95, 80) * 0.9f, dayK);
  s.cloud = mix(mix(rgb(30, 36, 60), rgb(238, 238, 244), dayK), rgb(255, 170, 130), warm * 0.8f);
  s.shadowK = 0.5f * smooth(0.03f, 0.25f, el);
  return s;
}

static const Col PAL_DOWN[] = {rgb(150, 160, 172), rgb(118, 134, 152), rgb(176, 172, 162), rgb(96, 112, 128), rgb(140, 152, 146), rgb(186, 186, 192)};
static const Col PAL_MID[] = {rgb(192, 176, 150), rgb(172, 122, 102), rgb(160, 166, 172), rgb(202, 192, 172), rgb(150, 132, 116)};
static const Col PAL_RES[] = {rgb(212, 192, 160), rgb(188, 142, 122), rgb(202, 202, 192), rgb(162, 186, 170), rgb(222, 206, 182), rgb(172, 152, 142)};

void World::gen(uint32_t seed) {
  Rng rng; rng.s = seed * 2654435761u + 12345u; if (!rng.s) rng.s = 1;
  for (int i = 0; i < NB; i++)
    for (int j = 0; j < NB; j++) {
      Block& B = blocks[i][j];
      B = Block();
      B.x0 = roadC(i) + HALFR; B.x1 = roadC(i + 1) - HALFR; B.z0 = roadC(j) + HALFR; B.z1 = roadC(j + 1) - HALFR;
      float cd = std::hypot(i - 3.5f, j - 3.5f);
      uint8_t kind = 0;
      if (i == 4 && j == 3) kind = 2;
      else if (i == 2 && j == 4) kind = 3;
      else if (cd > 1.6f && rng.f() < 0.12f) kind = 1;
      B.kind = kind;
      float L = B.x1 - B.x0, cxm = (B.x0 + B.x1) * 0.5f, czm = (B.z0 + B.z1) * 0.5f;

      // street furniture along the four kerbs
      for (int side = 0; side < 4; side++) {
        V2 o, al, in;
        switch (side) {
          case 0: o = {B.x0, B.z0}; al = {1, 0}; in = {0, 1}; break;
          case 1: o = {B.x0, B.z1}; al = {1, 0}; in = {0, -1}; break;
          case 2: o = {B.x0, B.z0}; al = {0, 1}; in = {1, 0}; break;
          default: o = {B.x1, B.z0}; al = {0, 1}; in = {-1, 0}; break;
        }
        size_t lampStart = B.lamps.size();
        for (float t = 9 + rng.f() * 6; t < L - 6; t += 26) {
          V2 p = o + al * t + in * 1.0f;
          B.lamps.push_back({p, in * -1.f});
          B.obs.push_back({p.x, p.z, 0.3f, 0});
        }
        for (float t = 4 + rng.f() * 3; t < L - 3; t += 11.5f) {
          V2 p = o + al * t + in * 1.9f;
          bool near = false;
          for (size_t k = lampStart; k < B.lamps.size(); k++) { V2 d = B.lamps[k].p - p; if (len(d) < 3.4f) near = true; }
          if (!near && rng.f() < 0.8f) B.obs.push_back({p.x, p.z, 0.5f, 1});
        }
      }

      if (kind == 1) {                              // park: scattered trees
        for (int k = 0; k < 22; k++) B.obs.push_back({rng.r(B.x0 + 6, B.x1 - 6), rng.r(B.z0 + 6, B.z1 - 6), 0.6f, 1});
      } else if (kind >= 2) {                       // plaza with a single shop at the north kerb
        Building b;
        b.x0 = cxm - 16; b.x1 = cxm + 16; b.z0 = B.z0 + SIDEW + 1; b.z1 = B.z0 + SIDEW + 15;
        b.h = 7.5f; b.wall = kind == 2 ? rgb(214, 140, 50) : rgb(150, 50, 46); b.roof = rgb(70, 70, 74);
        b.seed = rng.u(); b.tiers = 1; b.setback = 0; b.antenna = false;
        B.bld.push_back(b);
        (kind == 2 ? garage : gunShop) = {cxm, czm + 8};
      } else {
        float ix0 = B.x0 + SIDEW, ix1 = B.x1 - SIDEW, iz0 = B.z0 + SIDEW, iz1 = B.z1 - SIDEW;
        int cols = 1 + rng.i(3), rows = 1 + rng.i(3);
        if (cd < 1.9f) { cols = 2; rows = 2 + rng.i(2); }
        float cw = (ix1 - ix0) / cols, ch = (iz1 - iz0) / rows;
        for (int c = 0; c < cols; c++)
          for (int r = 0; r < rows; r++) {
            if (cols * rows > 2 && rng.f() < 0.06f) continue;
            Building b;
            b.x0 = ix0 + c * cw + 1.0f; b.x1 = ix0 + (c + 1) * cw - 1.0f;
            b.z0 = iz0 + r * ch + 1.0f; b.z1 = iz0 + (r + 1) * ch - 1.0f;
            b.h = cd < 1.9f ? rng.r(45, 115) : (cd < 3.3f ? rng.r(20, 55) : rng.r(8, 26));
            const Col* pal = cd < 1.9f ? PAL_DOWN : (cd < 3.3f ? PAL_MID : PAL_RES);
            int pn = cd < 1.9f ? 6 : (cd < 3.3f ? 5 : 6);
            b.wall = pal[rng.i(pn)] * rng.r(0.85f, 1.1f);
            b.roof = rgb(84, 84, 90) * rng.r(0.8f, 1.2f);
            b.seed = rng.u();
            b.tiers = b.h > 55 ? 3 : (b.h > 30 ? 2 : 1);
            float mind = std::min(b.x1 - b.x0, b.z1 - b.z0);
            b.setback = b.tiers > 1 ? std::max(0.f, std::min(rng.r(2.0f, 3.6f), (mind - 7.f) / (2.f * (b.tiers - 1)))) : 0.f;
            b.antenna = b.h > 45 && rng.f() < 0.7f;
            B.bld.push_back(b);
          }
      }
    }
  spawn = {-31.f, -12.f};
  // keep the spawn point and the camera run-up free of trees
  for (int i = 0; i < NB; i++)
    for (int j = 0; j < NB; j++) {
      auto& o = blocks[i][j].obs;
      o.erase(std::remove_if(o.begin(), o.end(), [&](const Obst& t) { return t.kind == 1 && t.x > spawn.x - 12.f && t.x < spawn.x + 6.f && std::fabs(t.z - spawn.z) < 5.f; }), o.end());
    }
}

// ---------------------------------------------------------------- collisions
bool World::resolve(V2& p, float r, V2& nOut) const {
  bool hit = false;
  auto push = [&](float nx, float nz, float pen) { p.x += nx * pen; p.z += nz * pen; nOut = {nx, nz}; hit = true; };
  if (p.x < -BOUND + r) push(1, 0, -BOUND + r - p.x);
  if (p.x > BOUND - r) push(-1, 0, p.x - (BOUND - r));
  if (p.z < -BOUND + r) push(0, 1, -BOUND + r - p.z);
  if (p.z > BOUND - r) push(0, -1, p.z - (BOUND - r));
  int bi = (int)std::floor((p.x - roadC(0)) / PITCH), bj = (int)std::floor((p.z - roadC(0)) / PITCH);
  for (int i = std::max(0, bi - 1); i <= std::min(NB - 1, bi + 1); i++)
    for (int j = std::max(0, bj - 1); j <= std::min(NB - 1, bj + 1); j++) {
      const Block& B = blocks[i][j];
      if (p.x < B.x0 - r - 1 || p.x > B.x1 + r + 1 || p.z < B.z0 - r - 1 || p.z > B.z1 + r + 1) continue;
      for (const Building& b : B.bld) {
        float cx = clampf(p.x, b.x0, b.x1), cz = clampf(p.z, b.z0, b.z1);
        float dx = p.x - cx, dz = p.z - cz, d2 = dx * dx + dz * dz;
        if (d2 >= r * r) continue;
        if (d2 > 1e-8f) { float d = std::sqrt(d2); push(dx / d, dz / d, r - d); }
        else {
          float l = p.x - b.x0, rr = b.x1 - p.x, t = p.z - b.z0, bt = b.z1 - p.z, m = std::min({l, rr, t, bt});
          if (m == l) push(-1, 0, l + r); else if (m == rr) push(1, 0, rr + r); else if (m == t) push(0, -1, t + r); else push(0, 1, bt + r);
        }
      }
      for (const Obst& o : B.obs) {
        float dx = p.x - o.x, dz = p.z - o.z, rr = r + o.r, d2 = dx * dx + dz * dz;
        if (d2 >= rr * rr) continue;
        float d = std::sqrt(d2);
        if (d < 1e-4f) push(1, 0, rr); else push(dx / d, dz / d, rr - d);
      }
    }
  return hit;
}

static bool rayBox(V2 o, V2 d, float x0, float z0, float x1, float z1, float& tOut) {
  float tmin = 0, tmax = 1e9f;
  for (int a = 0; a < 2; a++) {
    float oo = a ? o.z : o.x, dd = a ? d.z : d.x, lo = a ? z0 : x0, hi = a ? z1 : x1;
    if (std::fabs(dd) < 1e-8f) { if (oo < lo || oo > hi) return false; }
    else {
      float t1 = (lo - oo) / dd, t2 = (hi - oo) / dd;
      if (t1 > t2) std::swap(t1, t2);
      tmin = std::max(tmin, t1); tmax = std::min(tmax, t2);
      if (tmin > tmax) return false;
    }
  }
  tOut = tmin;
  return true;
}

float World::rayWorld(V2 o, V2 dir, float maxT, bool withObs) const {
  float best = maxT;
  V2 e = o + dir * maxT;
  float bx0 = std::min(o.x, e.x) - 1, bx1 = std::max(o.x, e.x) + 1, bz0 = std::min(o.z, e.z) - 1, bz1 = std::max(o.z, e.z) + 1;
  for (int i = 0; i < NB; i++)
    for (int j = 0; j < NB; j++) {
      const Block& B = blocks[i][j];
      if (B.x1 < bx0 || B.x0 > bx1 || B.z1 < bz0 || B.z0 > bz1) continue;
      for (const Building& b : B.bld) { float t; if (rayBox(o, dir, b.x0, b.z0, b.x1, b.z1, t) && t < best) best = t; }
      if (withObs) for (const Obst& ob : B.obs) {
        V2 oc = o - V2{ob.x, ob.z};
        float bq = dot(oc, dir), cq = dot(oc, oc) - ob.r * ob.r, disc = bq * bq - cq;
        if (disc >= 0) { float t = -bq - std::sqrt(disc); if (t >= 0 && t < best) best = t; }
      }
    }
  return best;
}

// ---------------------------------------------------------------- drawing
static void drawBuilding(Renderer& R, const Building& b, float time) {
  const V3 up{0, 1, 0};
  float hs[3] = {b.h, 0, 0};
  if (b.tiers == 2) { hs[0] = b.h * 0.62f; hs[1] = b.h * 0.38f; }
  if (b.tiers == 3) { hs[0] = b.h * 0.5f; hs[1] = b.h * 0.3f; hs[2] = b.h * 0.2f; }
  float y = CURB, x0 = b.x0, x1 = b.x1, z0 = b.z0, z1 = b.z1;
  for (int t = 0; t < b.tiers; t++) {
    float ins = b.setback * t;
    x0 = b.x0 + ins; x1 = b.x1 - ins; z0 = b.z0 + ins; z1 = b.z1 - ins;
    float y1 = y + hs[t];
    R.quad({x0, y, z0}, {x1, y, z0}, {x1, y1, z0}, {x0, y1, z0}, {0, 0, -1}, M_FACADE, b.wall, b.seed);
    R.quad({x0, y, z1}, {x0, y1, z1}, {x1, y1, z1}, {x1, y, z1}, {0, 0, 1}, M_FACADE, b.wall, b.seed);
    R.quad({x0, y, z0}, {x0, y1, z0}, {x0, y1, z1}, {x0, y, z1}, {-1, 0, 0}, M_FACADE, b.wall, b.seed);
    R.quad({x1, y, z0}, {x1, y, z1}, {x1, y1, z1}, {x1, y1, z0}, {1, 0, 0}, M_FACADE, b.wall, b.seed);
    R.quad({x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1}, up, M_FLAT, b.roof);
    y = y1;
  }
  // rooftop clutter (only when the roof is reasonably close)
  V3 dc = V3{(x0 + x1) * 0.5f, y, (z0 + z1) * 0.5f} - R.cam.pos;
  if (dot(dc, dc) < 160.f * 160.f) {
    Rng rr; rr.s = b.seed | 1u;
    int n = 1 + rr.i(3);
    for (int k = 0; k < n; k++) {
      float hx = rr.r(0.8f, 1.8f), hz = rr.r(0.8f, 1.8f);
      float px = rr.r(x0 + hx + 0.8f, std::max(x0 + hx + 0.9f, x1 - hx - 0.8f)), pz = rr.r(z0 + hz + 0.8f, std::max(z0 + hz + 0.9f, z1 - hz - 0.8f));
      R.box({px, y + 0.7f, pz}, {1, 0, 0}, up, {0, 0, 1}, {hx, 0.7f, hz}, rgb(128, 130, 134));
    }
  }
  if (b.antenna) {
    V3 top{(x0 + x1) * 0.5f, y, (z0 + z1) * 0.5f};
    R.box(top + V3{0, 6, 0}, {1, 0, 0}, up, {0, 0, 1}, {0.18f, 6.f, 0.18f}, rgb(70, 70, 76));
    if (R.sky.night > 0.3f && std::fmod(time + b.seed % 7, 1.6f) < 0.8f) R.sprite(top + V3{0, 12.3f, 0}, 2.2f, {1.f, 0.12f, 0.06f}, 1.4f);
  }
}

static void shadowOf(Renderer& R, const Building& b, V2 sunOff) {
  float hs[3] = {b.h, 0, 0};
  if (b.tiers == 2) { hs[0] = b.h * 0.62f; hs[1] = b.h * 0.38f; }
  if (b.tiers == 3) { hs[0] = b.h * 0.5f; hs[1] = b.h * 0.3f; hs[2] = b.h * 0.2f; }
  float hTop = 0;
  const float y = CURB + 0.03f;
  for (int t = 0; t < b.tiers; t++) {
    hTop += hs[t];
    float ins = b.setback * t;
    V2 c[4] = {{b.x0 + ins, b.z0 + ins}, {b.x1 - ins, b.z0 + ins}, {b.x1 - ins, b.z1 - ins}, {b.x0 + ins, b.z1 - ins}};
    V2 sh = sunOff * hTop;
    float l = len(sh);
    if (l > 70.f) sh = sh * (70.f / l);
    for (int k = 0; k < 4; k++) {
      V2 p = c[k], q = c[(k + 1) & 3];
      R.decal({p.x, y, p.z}, {q.x, y, q.z}, {q.x + sh.x, y, q.z + sh.z}, {p.x + sh.x, y, p.z + sh.z}, D_SHADOW_HARD, Col{}, 1.f);
    }
    R.decal({c[0].x + sh.x, y, c[0].z + sh.z}, {c[1].x + sh.x, y, c[1].z + sh.z}, {c[2].x + sh.x, y, c[2].z + sh.z}, {c[3].x + sh.x, y, c[3].z + sh.z}, D_SHADOW_HARD, Col{}, 1.f);
  }
}

void World::draw(Renderer& R, float time) const {
  const Camera& cam = R.cam;
  const Sky& sky = R.sky;
  const V3 up{0, 1, 0}, X{1, 0, 0}, Z{0, 0, 1};
  const float night = sky.night;
  const float lampOn = sat(night * 1.6f);
  static const Col asphalt = rgb(62, 64, 70), pave = rgb(170, 168, 162), grass = rgb(78, 128, 64), curbc = rgb(150, 148, 142);

  const float T = 4000.f;
  R.quad({-T, -0.3f, -T}, {T, -0.3f, -T}, {T, -0.3f, T}, {-T, -0.3f, T}, up, M_GRASS, rgb(88, 118, 66));

  // perimeter barrier
  static const Col barrier = rgb(172, 172, 170);
  R.box({0, 0.6f, -BOUND - 0.5f}, X, up, Z, {BOUND + 1, 0.6f, 0.45f}, barrier);
  R.box({0, 0.6f, BOUND + 0.5f}, X, up, Z, {BOUND + 1, 0.6f, 0.45f}, barrier);
  R.box({-BOUND - 0.5f, 0.6f, 0}, X, up, Z, {0.45f, 0.6f, BOUND + 1}, barrier);
  R.box({BOUND + 0.5f, 0.6f, 0}, X, up, Z, {0.45f, 0.6f, BOUND + 1}, barrier);

  // roads + intersections
  for (int k = 0; k < NR; k++) {
    float c = roadC(k);
    for (int s = 0; s < NB; s++) {
      float a = roadC(s) + HALFR, b = roadC(s + 1) - HALFR, m = (a + b) * 0.5f;
      if (R.inView({m, 0, c}, 32, 520)) R.quad({a, 0, c - HALFR}, {b, 0, c - HALFR}, {b, 0, c + HALFR}, {a, 0, c + HALFR}, up, M_ROADX, asphalt);
      if (R.inView({c, 0, m}, 32, 520)) R.quad({c - HALFR, 0, a}, {c + HALFR, 0, a}, {c + HALFR, 0, b}, {c - HALFR, 0, b}, up, M_ROADZ, asphalt);
    }
    for (int i = 0; i < NR; i++) {
      float x = roadC(i);
      if (R.inView({x, 0, c}, 14, 520)) R.quad({x - HALFR, 0, c - HALFR}, {x + HALFR, 0, c - HALFR}, {x + HALFR, 0, c + HALFR}, {x - HALFR, 0, c + HALFR}, up, M_ISECT, asphalt);
    }
  }

  // sun shadow offset per metre of height
  const bool sunShadows = sky.sunDir.y > 0.07f && sky.shadowK > 0.05f;
  V2 sunOff = sunShadows ? V2{-sky.sunDir.x / sky.sunDir.y, -sky.sunDir.z / sky.sunDir.y} : V2{};

  // blocks
  for (int i = 0; i < NB; i++)
    for (int j = 0; j < NB; j++) {
      const Block& B = blocks[i][j];
      float cx = (B.x0 + B.x1) * 0.5f, cz = (B.z0 + B.z1) * 0.5f;
      if (!R.inView({cx, 35, cz}, 90, 540)) continue;
      float dist = std::hypot(cx - cam.pos.x, cz - cam.pos.z);
      R.quad({B.x0, CURB, B.z0}, {B.x1, CURB, B.z0}, {B.x1, CURB, B.z1}, {B.x0, CURB, B.z1}, up, B.kind == 1 ? M_GRASS : M_PAVE, B.kind == 1 ? grass : pave);
      R.quad({B.x0, 0, B.z0}, {B.x1, 0, B.z0}, {B.x1, CURB, B.z0}, {B.x0, CURB, B.z0}, {0, 0, -1}, M_FLAT, curbc);
      R.quad({B.x0, 0, B.z1}, {B.x0, CURB, B.z1}, {B.x1, CURB, B.z1}, {B.x1, 0, B.z1}, {0, 0, 1}, M_FLAT, curbc);
      R.quad({B.x0, 0, B.z0}, {B.x0, CURB, B.z0}, {B.x0, CURB, B.z1}, {B.x0, 0, B.z1}, {-1, 0, 0}, M_FLAT, curbc);
      R.quad({B.x1, 0, B.z0}, {B.x1, 0, B.z1}, {B.x1, CURB, B.z1}, {B.x1, CURB, B.z0}, {1, 0, 0}, M_FLAT, curbc);
      if (B.kind == 1 && ((i + j) & 1) == 0) {      // pond
        Col wc = mix(sky.horizon, sky.zenith, 0.4f) * 0.55f;
        R.quad({cx - 9, CURB + 0.03f, cz - 6}, {cx + 9, CURB + 0.03f, cz - 6}, {cx + 9, CURB + 0.03f, cz + 6}, {cx - 9, CURB + 0.03f, cz + 6}, up, M_EMIT, wc);
      }
      for (const Building& b : B.bld) {
        V3 bc{(b.x0 + b.x1) * 0.5f, b.h * 0.5f, (b.z0 + b.z1) * 0.5f};
        if (!R.inView(bc, std::max(len(V2{b.x1 - b.x0, b.z1 - b.z0}) * 0.5f, b.h * 0.5f), 540)) continue;
        drawBuilding(R, b, time);
        if (sunShadows && dist < 170) shadowOf(R, b, sunOff);
      }
      // plaza signs
      if (B.kind >= 2) {
        const Building& b = B.bld[0];
        Col sc = B.kind == 2 ? Col{2.2f, 1.7f, 0.3f} : Col{2.4f, 0.4f, 0.25f};
        R.box({(b.x0 + b.x1) * 0.5f, b.h + 1.2f, (b.z0 + b.z1) * 0.5f}, X, up, Z, {10, 1.2f, 0.4f}, sc, M_EMIT);
      }
      // lamps and trees (near the camera only)
      if (dist < 175) {
        for (const Lamp& l : B.lamps) {
          if (!R.inView({l.p.x, 4, l.p.z}, 4, 175)) continue;
          R.box({l.p.x, 3.6f, l.p.z}, X, up, Z, {0.09f, 3.6f, 0.09f}, rgb(60, 62, 66));
          float ax = std::fabs(l.arm.x) > 0.5f ? 1.0f : 0.06f, az = std::fabs(l.arm.z) > 0.5f ? 1.0f : 0.06f;
          R.box({l.p.x + l.arm.x * 1.0f, 7.2f, l.p.z + l.arm.z * 1.0f}, X, up, Z, {ax, 0.06f, az}, rgb(60, 62, 66));
          V3 head{l.p.x + l.arm.x * 2.0f, 7.0f, l.p.z + l.arm.z * 2.0f};
          R.box(head, X, up, Z, {0.4f, 0.1f, 0.4f}, Col{1.5f, 1.15f, 0.7f} * (0.25f + 0.75f * lampOn), M_EMIT);
          if (lampOn > 0.05f) {
            R.sprite(head - V3{0, 0.2f, 0}, 2.6f, {1.0f, 0.78f, 0.45f}, 0.9f * lampOn);
            R.decal({head.x - 9, 0.25f, head.z - 9}, {head.x + 9, 0.25f, head.z - 9}, {head.x + 9, 0.25f, head.z + 9}, {head.x - 9, 0.25f, head.z + 9}, D_GLOW, {1.0f, 0.72f, 0.38f}, 0.55f * lampOn);
          }
        }
        for (const Obst& o : B.obs) {
          if (o.kind != 1 || !R.inView({o.x, 3, o.z}, 4, 160)) continue;
          uint32_t h = hash32((uint32_t)(int)(o.x * 10) * 73856093u ^ (uint32_t)(int)(o.z * 10) * 19349663u);
          Col leaf = (h & 7) == 0 ? rgb(196, 120, 40) : rgb(52 + h % 30, 108 + (h >> 8) % 50, 50 + (h >> 16) % 25);
          float s = o.r > 0.55f ? 1.25f : 1.f;
          R.box({o.x, 1.0f, o.z}, X, up, Z, {0.18f, 1.0f, 0.18f}, rgb(92, 66, 46));
          V3 q1[8] = {{o.x - 1.5f * s, 1.6f, o.z - 1.5f * s}, {o.x + 1.5f * s, 1.6f, o.z - 1.5f * s}, {o.x + 1.5f * s, 1.6f, o.z + 1.5f * s}, {o.x - 1.5f * s, 1.6f, o.z + 1.5f * s},
                      {o.x - 1.9f * s, 2.9f, o.z - 1.9f * s}, {o.x + 1.9f * s, 2.9f, o.z - 1.9f * s}, {o.x + 1.9f * s, 2.9f, o.z + 1.9f * s}, {o.x - 1.9f * s, 2.9f, o.z + 1.9f * s}};
          V3 q2[8] = {q1[4], q1[5], q1[6], q1[7],
                      {o.x - 0.7f * s, 4.7f, o.z - 0.7f * s}, {o.x + 0.7f * s, 4.7f, o.z - 0.7f * s}, {o.x + 0.7f * s, 4.7f, o.z + 0.7f * s}, {o.x - 0.7f * s, 4.7f, o.z + 0.7f * s}};
          R.solid(q1, leaf);
          R.solid(q2, leaf * 1.12f);
        }
      }
    }

  // traffic signals
  for (int i = 0; i < NR; i++)
    for (int j = 0; j < NR; j++) {
      V2 n = nodePos(i, j);
      if (!R.inView({n.x, 4, n.z}, 18, 150)) continue;
      for (int d = 0; d < 4; d++) {
        V2 c = n + dirOf(d) * 10.9f + rightOf(d) * 10.9f;
        V2 f = dirOf(d) * -1.f;                      // head faces the approaching traffic
        R.box({c.x, 2.4f, c.z}, X, up, Z, {0.1f, 2.4f, 0.1f}, rgb(55, 57, 60));
        bool alongX = std::fabs(f.x) > 0.5f;
        V3 hh = alongX ? V3{0.15f, 0.55f, 0.2f} : V3{0.2f, 0.55f, 0.15f};
        V3 hc{c.x, 5.0f, c.z};
        R.box(hc, X, up, Z, hh, rgb(30, 31, 34));
        int st = sigState(i, j, d, false, time);
        const Col lc[3] = {{2.0f, 0.12f, 0.08f}, {2.0f, 1.4f, 0.1f}, {0.1f, 1.8f, 0.5f}};
        for (int k = 0; k < 3; k++) {
          bool on = (k == 0 && st == 0) || (k == 1 && st == 1) || (k == 2 && st == 2);
          V3 lp{hc.x + f.x * (hh.x + 0.03f), hc.y + (k == 0 ? 0.35f : (k == 1 ? 0.0f : -0.35f)), hc.z + f.z * (hh.z + 0.03f)};
          R.box(lp, X, up, Z, alongX ? V3{0.03f, 0.14f, 0.14f} : V3{0.14f, 0.14f, 0.03f}, on ? lc[k] : lc[k] * 0.07f, M_EMIT);
          if (on) R.sprite(lp + V3{f.x * 0.2f, 0, f.z * 0.2f}, 0.9f, {lc[k].r * 0.5f, lc[k].g * 0.5f, lc[k].b * 0.5f}, 0.9f);
        }
      }
    }

  // mission markers (garage / gun shop)
  struct Mk { V2 p; Col c; };
  const Mk mk[2] = {{garage, {1.0f, 0.82f, 0.15f}}, {gunShop, {1.0f, 0.3f, 0.15f}}};
  for (const Mk& m : mk) {
    if (!R.inView({m.p.x, 10, m.p.z}, 40, 420)) continue;
    float pulse = 0.75f + 0.25f * std::sin(time * 3.f);
    float y = CURB + 0.04f, r = 8.f;
    R.decal({m.p.x - r, y, m.p.z - r}, {m.p.x + r, y, m.p.z - r}, {m.p.x + r, y, m.p.z + r}, {m.p.x - r, y, m.p.z + r}, D_RING, m.c, pulse);
    R.decal({m.p.x - r, y, m.p.z - r}, {m.p.x + r, y, m.p.z - r}, {m.p.x + r, y, m.p.z + r}, {m.p.x - r, y, m.p.z + r}, D_GLOW, m.c, 0.22f * pulse);
    R.decal({m.p.x - 2.4f, y, m.p.z}, {m.p.x + 2.4f, y, m.p.z}, {m.p.x + 2.4f, 70, m.p.z}, {m.p.x - 2.4f, 70, m.p.z}, D_BEAM, m.c, 0.9f * pulse);
    R.decal({m.p.x, y, m.p.z - 2.4f}, {m.p.x, y, m.p.z + 2.4f}, {m.p.x, 70, m.p.z + 2.4f}, {m.p.x, 70, m.p.z - 2.4f}, D_BEAM, m.c, 0.9f * pulse);
  }
}
