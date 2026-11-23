// NEON CITY - 3D drawing of vehicles, people, pickups, particles
#include "game.h"

namespace {
struct Basis { V3 r, u, f; };
Basis makeBasis(float yaw, float pitch, float roll) {
  V3 f0{std::sin(yaw), 0, std::cos(yaw)}, r0{-std::cos(yaw), 0, std::sin(yaw)}, u0{0, 1, 0};
  V3 f = f0 * std::cos(pitch) + u0 * std::sin(pitch);
  V3 u1 = cross(r0, f);
  Basis b;
  b.r = r0 * std::cos(roll) - u1 * std::sin(roll);
  b.u = u1 * std::cos(roll) + r0 * std::sin(roll);
  b.f = f;
  return b;
}
inline V2 fwdOf(float yaw) { return {std::sin(yaw), std::cos(yaw)}; }
inline V2 rtOf(float yaw) { return {-std::cos(yaw), std::sin(yaw)}; }
}  // namespace

void Game::drawCar(Renderer& R, const Vehicle& v) {
  const CarDef& D = CARS[v.type];
  const float night = R.sky.night;
  Basis B = makeBasis(v.yaw, v.pitch, v.roll);
  V3 o{v.pos.x, 0.03f, v.pos.z};
  auto P = [&](float x, float y, float z) { return o + B.r * x + B.u * y + B.f * z; };
  const bool wreck = v.st == VS_WRECK;
  const bool lit = (v.st == VS_AI || v.st == VS_PLAYER);
  Col body = wreck ? rgb(36, 32, 30) : v.color;
  Col glass = wreck ? rgb(14, 12, 12) : rgb(28, 40, 62);
  Col trim = rgb(30, 31, 35);
  const float hl = D.len * 0.5f, hw = D.wid * 0.5f;
  auto frus = [&](float y0, float y1, float bw, float bz0, float bz1, float tw, float tz0, float tz1, Col c) {
    V3 q[8] = {P(-bw, y0, bz0), P(bw, y0, bz0), P(bw, y0, bz1), P(-bw, y0, bz1), P(-tw, y1, tz0), P(tw, y1, tz0), P(tw, y1, tz1), P(-tw, y1, tz1)};
    R.solid(q, c);
  };
  auto bxs = [&](V3 c, V3 half, Col col, Mat m = M_FLAT) { R.box(c, B.r, B.u, B.f, half, col, m); };

  V2 f2 = fwdOf(v.yaw), r2 = rtOf(v.yaw);
  {  // soft ground shadow
    float sl = hl * 1.12f, sw = hw * 1.2f, y = CURB + 0.03f;
    V3 c{v.pos.x, y, v.pos.z}, F{f2.x * sl, 0, f2.z * sl}, Rr{r2.x * sw, 0, r2.z * sw};
    R.decal(c - F - Rr, c + F - Rr, c + F + Rr, c - F + Rr, D_SHADOW_BLOB, Col{}, 0.9f);
  }

  frus(D.y0, D.bodyTop, hw, -hl, hl, hw * 0.92f, -hl + D.tail, hl - D.nose, body);                       // body
  float cw = hw * 0.9f, ctw = hw * 0.78f;
  frus(D.bodyTop, D.cabTop, cw, D.cb0, D.cb1, ctw, D.ct0, D.ct1, glass);                                  // cabin glass
  frus(D.cabTop - 0.02f, D.cabTop + 0.04f, ctw + 0.02f, D.ct0 - 0.03f, D.ct1 + 0.03f, ctw + 0.02f, D.ct0 - 0.03f, D.ct1 + 0.03f, body);   // roof
  bxs(P(0, D.y0 + 0.14f, hl - 0.03f), {hw * 0.96f, 0.12f, 0.07f}, trim);                                  // bumpers
  bxs(P(0, D.y0 + 0.14f, -hl + 0.03f), {hw * 0.96f, 0.12f, 0.07f}, trim);

  for (int i = 0; i < 4; i++) {                                                                           // wheels
    float sx = (i & 1) ? 1.f : -1.f, sz = (i & 2) ? 1.f : -1.f;
    V3 wf = B.f, wr = B.r;
    if (sz > 0) { float cs = std::cos(v.steer), sn = std::sin(v.steer); wf = B.f * cs - B.r * sn; wr = B.r * cs + B.f * sn; }
    R.box(P(sx * (hw - 0.1f), D.wr, sz * D.wb * 0.5f), wr, B.u, wf, {0.13f, D.wr, D.wr}, rgb(20, 20, 22));
  }

  if (!wreck) {                                                                                           // lights
    float hy = D.y0 + (D.bodyTop - D.y0) * 0.55f;
    float zf = hl - D.nose * 0.55f + 0.015f, zr = -hl + D.tail * 0.55f - 0.015f;
    bool braking = v.brake > 0.15f || (v.throttle < -0.1f && v.lastVf > 0.5f);
    Col headC = Col{1.0f, 0.97f, 0.85f} * (1.0f + 1.4f * night);
    Col tailC = braking ? Col{2.6f, 0.15f, 0.1f} : Col{1.2f + 0.6f * night, 0.08f, 0.06f};
    for (int sg = -1; sg <= 1; sg += 2) {
      bxs(P(sg * (hw - 0.4f), hy, zf), {0.22f, 0.07f, 0.03f}, headC, M_EMIT);
      bxs(P(sg * (hw - 0.35f), hy + 0.02f, zr), {0.24f, 0.06f, 0.03f}, tailC, M_EMIT);
      if (lit && night > 0.2f) R.sprite(P(sg * (hw - 0.4f), hy, hl + 0.25f), 0.6f, {1.0f, 0.95f, 0.8f}, 0.8f * night);
      if (lit && (braking || night > 0.2f)) R.sprite(P(sg * (hw - 0.35f), hy, zr - 0.2f), 0.45f, {1.0f, 0.1f, 0.05f}, (braking ? 0.9f : 0.35f) * (0.4f + 0.6f * night));
    }
    if (lit && night > 0.2f) {                                                                            // head-light pool on the road
      V3 c{v.pos.x + f2.x * (hl + 6.f), CURB + 0.05f, v.pos.z + f2.z * (hl + 6.f)}, F{f2.x * 6.5f, 0, f2.z * 6.5f}, Rr{r2.x * 4.5f, 0, r2.z * 4.5f};
      R.decal(c - F - Rr, c + F - Rr, c + F + Rr, c - F + Rr, D_GLOW, {1.0f, 0.92f, 0.75f}, 0.4f * night);
    }
  }
  if (v.type == 3 && !wreck) {                                                                            // hypercar: wing + neon underglow
    bxs(P(0, D.bodyTop + 0.34f, -hl + 0.45f), {hw * 0.88f, 0.025f, 0.2f}, body * 0.6f);
    for (int sg = -1; sg <= 1; sg += 2) bxs(P(sg * hw * 0.45f, D.bodyTop + 0.17f, -hl + 0.45f), {0.03f, 0.17f, 0.06f}, trim);
    bxs(P(0, D.y0 + 0.06f, hl - 0.05f), {hw * 0.9f, 0.03f, 0.12f}, trim);
    V3 c{v.pos.x, CURB + 0.06f, v.pos.z}, F{f2.x * (hl + 0.7f), 0, f2.z * (hl + 0.7f)}, Rr{r2.x * (hw + 0.7f), 0, r2.z * (hw + 0.7f)};
    R.decal(c - F - Rr, c + F - Rr, c + F + Rr, c - F + Rr, D_GLOW, {0.1f, 0.75f, 1.0f}, 0.3f + 0.6f * night);
  }
  if (v.type == 2 && !wreck) {                                                                            // muscle: hood scoop + stripes
    float z1 = hl - D.nose - 0.05f;
    bxs(P(0, D.bodyTop + 0.07f, (D.cb1 + z1) * 0.5f + 0.1f), {0.28f, 0.07f, 0.3f}, trim);
    Col st = rgb(235, 235, 235);
    for (int sg = -1; sg <= 1; sg += 2) {
      float zc = (D.cb1 + 0.05f + z1) * 0.5f, zh = (z1 - D.cb1 - 0.05f) * 0.5f;
      bxs(P(sg * 0.32f, D.bodyTop + 0.012f, zc), {0.1f, 0.006f, zh}, st);
      float tz0 = -hl + D.tail + 0.05f, tz1 = D.cb0 - 0.05f;
      bxs(P(sg * 0.32f, D.bodyTop + 0.012f, (tz0 + tz1) * 0.5f), {0.1f, 0.006f, (tz1 - tz0) * 0.5f}, st);
    }
  }
}

void Game::drawPerson(Renderer& R, V2 pos, float yaw, float phase, Col shirt, Col pants, Col skin, bool down, bool gun) {
  V3 f{std::sin(yaw), 0, std::cos(yaw)}, r{-std::cos(yaw), 0, std::sin(yaw)}, u{0, 1, 0};
  V3 o{pos.x, 0.03f, pos.z};
  {
    float y = CURB + 0.03f;
    R.decal({pos.x - 0.6f, y, pos.z - 0.6f}, {pos.x + 0.6f, y, pos.z - 0.6f}, {pos.x + 0.6f, y, pos.z + 0.6f}, {pos.x - 0.6f, y, pos.z + 0.6f}, D_SHADOW_BLOB, Col{}, 0.8f);
  }
  if (down) {                                                         // lying on the ground
    R.box(o + V3{0, 0.16f, 0} + f * 0.1f, r, u, f, {0.25f, 0.15f, 0.3f}, shirt);
    R.box(o + V3{0, 0.14f, 0} - f * 0.65f, r, u, f, {0.17f, 0.12f, 0.4f}, pants);
    R.box(o + V3{0, 0.14f, 0} + f * 0.55f, r, u, f, {0.12f, 0.12f, 0.12f}, skin);
    return;
  }
  float sw = std::sin(phase) * 0.7f;
  auto limb = [&](V3 hip, float ang, float length, float hx, Col c) {         // hangs from `hip`, swings forward for ang > 0
    V3 uu = u * std::cos(ang) - f * std::sin(ang), ff = f * std::cos(ang) + u * std::sin(ang);
    R.box(hip - uu * (length * 0.5f), r, uu, ff, {hx, length * 0.5f, hx}, c);
  };
  limb(o + V3{0, 0.92f, 0} + r * 0.11f, sw, 0.92f, 0.09f, pants);
  limb(o + V3{0, 0.92f, 0} - r * 0.11f, -sw, 0.92f, 0.09f, pants);
  R.box(o + V3{0, 1.22f, 0}, r, u, f, {0.24f, 0.3f, 0.13f}, shirt);
  R.box(o + V3{0, 1.66f, 0}, r, u, f, {0.12f, 0.13f, 0.12f}, skin);
  limb(o + V3{0, 1.48f, 0} - r * 0.34f, sw, 0.62f, 0.07f, shirt);
  if (gun) {
    limb(o + V3{0, 1.48f, 0} + r * 0.34f, 1.5f, 0.62f, 0.07f, shirt);
    R.box(o + V3{0, 1.45f, 0} + r * 0.34f + f * 0.62f, r, u, f, {0.04f, 0.07f, 0.17f}, rgb(28, 28, 30));
  } else limb(o + V3{0, 1.48f, 0} + r * 0.34f, -sw, 0.62f, 0.07f, shirt);
}

void Game::draw(Renderer& R) {
  Sky sky = makeSky(hour, time);
  float sh = trauma * trauma * 0.3f;
  V3 cp = camPos + V3{rng.r(-sh, sh), rng.r(-sh, sh), rng.r(-sh, sh)};
  Camera cam = makeCamera(cp, camYaw, -camPitch, fov);
  R.begin(cam, sky);
  world.draw(R, time);

  for (const Vehicle& v : cars) {
    if (v.st == VS_FREE || !R.inView({v.pos.x, 1.0f, v.pos.z}, 4.5f, 240.f)) continue;
    drawCar(R, v);
  }
  for (const Ped& p : peds) {
    if (!p.alive || !R.inView({p.pos.x, 1.0f, p.pos.z}, 1.5f, 170.f)) continue;
    drawPerson(R, p.pos, p.yaw, p.t * 6.f, p.shirt, p.pants, p.skin, p.st == 2, false);
  }
  if (P.car < 0) {
    const Weapon& wp = WEAP[P.weapon];
    drawPerson(R, P.pos, P.yaw, P.walk * 2.2f, rgb(222, 86, 40), rgb(40, 48, 84), rgb(214, 160, 120), P.deadT > 0.f, !wp.melee && (in.fire || P.muzzle > 0.f));
    if (P.muzzle > 0.f) {
      V2 a = fwdOf(P.yaw), rr = rtOf(P.yaw);
      R.sprite({P.pos.x + a.x * 0.95f + rr.x * 0.25f, 1.28f, P.pos.z + a.z * 0.95f + rr.z * 0.25f}, 0.9f, {1.0f, 0.8f, 0.4f}, 1.5f);
    }
  }
  for (const Pickup& k : picks) {
    if (!k.alive || !R.inView({k.pos.x, 1.0f, k.pos.z}, 1.5f, 190.f)) continue;
    float bob = 0.95f + 0.12f * std::sin(k.t * 3.f), ang = k.t * 2.f;
    V3 f{std::sin(ang), 0, std::cos(ang)}, r{-std::cos(ang), 0, std::sin(ang)}, up{0, 1, 0};
    R.box({k.pos.x, bob, k.pos.z}, r, up, f, {0.34f, 0.09f, 0.18f}, rgb(60, 180, 80));
    R.box({k.pos.x, bob + 0.095f, k.pos.z}, r, up, f, {0.12f, 0.02f, 0.19f}, rgb(235, 235, 200));
    R.sprite({k.pos.x, bob, k.pos.z}, 0.9f, {0.2f, 1.0f, 0.4f}, 0.6f);
  }
  for (const Part& q : parts) {
    float t = q.life / q.max;
    if (q.kind == 0) R.sprite(q.p, q.size, q.c, t, true);
    else if (q.kind == 2) R.sprite(q.p, q.size * (0.6f + 0.6f * t), q.c, t * 1.2f, true);
    else R.sprite(q.p, q.size * (1.6f - t), q.c, 0.55f * t + 0.1f, false);
  }
  for (const Tracer& t : tracers) {
    V3 d = t.b - t.a;
    if (len(d) < 0.1f) continue;
    V3 side = norm(cross(d, cam.pos - t.a)) * 0.04f;
    R.decal(t.a - side, t.a + side, t.b + side, t.b - side, D_FLAT, {1.0f, 0.85f, 0.45f}, 0.9f * (t.life / 0.07f));
  }
  R.end();
  drawHUD(R);
  if (showTouch) drawTouchUI(R);
}
