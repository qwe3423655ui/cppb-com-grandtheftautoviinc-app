// NEON CITY - touch input, HUD, minimap
#include "game.h"

namespace {
struct Btn { float x, y, r; };
Btn btnFire(int W, int H) { return {W - 66.f, H - 78.f, 38.f}; }
Btn btnUse(int W, int H) { return {W - 152.f, H - 42.f, 28.f}; }
Btn btnSwap(int W, int H) { return {W - 40.f, H - 160.f, 24.f}; }
bool inBtn(const Btn& b, float x, float y) { float dx = x - b.x, dy = y - b.y, r = b.r + 6.f; return dx * dx + dy * dy <= r * r; }
constexpr float STICK_R = 44.f;
}  // namespace

void Game::touch(int id, int phase, float x, float y) {
  if (phase == 0) {
    Touch* t = nullptr;
    for (auto& q : touches) if (q.id < 0) { t = &q; break; }
    if (!t) return;
    bool stickTaken = false;
    for (auto& q : touches) if (q.id >= 0 && q.role == 1) stickTaken = true;
    *t = Touch();
    t->id = id; t->x = t->sx = t->lx = x; t->y = t->sy = t->ly = y;
    if (inBtn(btnFire(W, H), x, y)) t->role = 2;
    else if (inBtn(btnUse(W, H), x, y)) { t->role = 3; useEdge = true; }
    else if (P.car < 0 && inBtn(btnSwap(W, H), x, y)) { t->role = 4; swapEdge = true; }
    else if (x < W * 0.45f && y > H * 0.28f && !stickTaken) t->role = 1;
    else t->role = 5;
    return;
  }
  for (auto& q : touches) {
    if (q.id != id) continue;
    if (phase == 1) {
      q.x = x; q.y = y;
      if (q.role == 5) { camAccX += x - q.lx; camAccY += y - q.ly; q.lx = x; q.ly = y; }
    } else q.id = -1;
  }
}

void Game::gatherInput() {
  in = Input();
  in.mx = kb.mx; in.my = kb.my; in.fire = kb.fire; in.brake = kb.brake; in.use = kb.use; in.swap = kb.swap;
  in.camDX = kb.camDX; in.camDY = kb.camDY;
  kb.use = kb.swap = false; kb.camDX = kb.camDY = 0;
  bool held = false;
  for (const Touch& t : touches) {
    if (t.id < 0) continue;
    if (t.role == 1) {
      float dx = (t.x - t.sx) / STICK_R, dy = -(t.y - t.sy) / STICK_R, m = std::sqrt(dx * dx + dy * dy);
      if (m > 0.12f) {
        float k = std::min(1.f, (m - 0.12f) / 0.88f) / m;
        in.mx += dx * k; in.my += dy * k;
      }
    }
    if (t.role == 2) held = true;
  }
  in.mx = clampf(in.mx, -1.f, 1.f); in.my = clampf(in.my, -1.f, 1.f);
  if (held) { in.fire = true; in.brake = true; }
  in.use = in.use || useEdge; in.swap = in.swap || swapEdge;
  useEdge = swapEdge = false;
  in.camDX += camAccX; in.camDY += camAccY;
  camAccX = camAccY = 0;
}

// ---------------------------------------------------------------- HUD
void Game::drawHUD(Renderer& R) {
  char buf[96];
  if (P.hurtCd > 0.f && P.deadT <= 0.f) R.rect(0, 0, W, H, 0xC01010, 0.16f * std::min(1.f, P.hurtCd * 2.f));
  if (P.deadT > 0.f) {
    R.rect(0, 0, W, H, 0x300000, 0.5f);
    R.text(W / 2, H / 2 - 16, "KNOCKED OUT", 4, 0xFF4848, 1);
  }

  // ---- minimap (north-up, +-120 m around the player)
  const int mS = 96, mX = 8, mY = 8;
  const float range = 120.f, sc = mS / (2.f * range);
  V2 mc = P.pos;
  auto mapX = [&](float x) { return mX + mS * 0.5f + (x - mc.x) * sc; };
  auto mapY = [&](float z) { return mY + mS * 0.5f + (z - mc.z) * sc; };
  auto rectClip = [&](float x0, float y0, float x1, float y1, uint32_t col, float a) {
    int ax = std::max(mX, (int)std::floor(x0)), ay = std::max(mY, (int)std::floor(y0));
    int bx = std::min(mX + mS, (int)std::ceil(x1)), by = std::min(mY + mS, (int)std::ceil(y1));
    if (ax < bx && ay < by) R.rect(ax, ay, bx - ax, by - ay, col, a);
  };
  R.rect(mX, mY, mS, mS, 0x0d131b, 0.62f);
  for (int k = 0; k < NR; k++) {
    float c = roadC(k);
    rectClip(mapX(-BOUND), mapY(c - HALFR), mapX(BOUND), mapY(c + HALFR), 0x5b6573, 0.95f);
    rectClip(mapX(c - HALFR), mapY(-BOUND), mapX(c + HALFR), mapY(BOUND), 0x5b6573, 0.95f);
  }
  for (const Pickup& k : picks)
    if (k.alive) { float x = mapX(k.pos.x), y = mapY(k.pos.z); if (x > mX && x < mX + mS && y > mY && y < mY + mS) R.rect((int)x - 1, (int)y - 1, 2, 2, 0x56ff7a, 0.95f); }
  for (const Vehicle& v : cars)
    if (v.st == VS_AI || v.st == VS_PARKED) { float x = mapX(v.pos.x), y = mapY(v.pos.z); if (x > mX && x < mX + mS && y > mY && y < mY + mS) R.rect((int)x, (int)y, 1, 1, v.type == 3 ? 0x40d8ff : 0xc8ccd4, 0.8f); }
  auto marker = [&](V2 w, uint32_t col) {
    float x = mapX(w.x), y = mapY(w.z), cx = mX + mS * 0.5f, cy = mY + mS * 0.5f;
    float dx = x - cx, dy = y - cy, lim = mS * 0.5f - 5.f;
    float m = std::max(std::fabs(dx), std::fabs(dy));
    if (m > lim) { dx *= lim / m; dy *= lim / m; }
    R.disc(cx + dx, cy + dy, 3.2f, 0x000000, 0.7f);
    R.disc(cx + dx, cy + dy, 2.3f, col, 1.f);
  };
  marker(world.garage, 0xFFD21E);
  marker(world.gunShop, 0xFF4A26);
  {
    float cx = mX + mS * 0.5f, cy = mY + mS * 0.5f, hx = std::sin(P.yaw), hz = std::cos(P.yaw);
    R.tri2(cx + hx * 6, cy + hz * 6, cx - hx * 3 - hz * 3.5f, cy - hz * 3 + hx * 3.5f, cx - hx * 3 + hz * 3.5f, cy - hz * 3 - hx * 3.5f, 0xFFFFFF, 1.f);
  }
  R.rect(mX - 1, mY - 1, mS + 2, 1, 0xFFFFFF, 0.5f); R.rect(mX - 1, mY + mS, mS + 2, 1, 0xFFFFFF, 0.5f);
  R.rect(mX - 1, mY, 1, mS, 0xFFFFFF, 0.5f); R.rect(mX + mS, mY, 1, mS, 0xFFFFFF, 0.5f);
  int hh = (int)hour, mm = (int)((hour - hh) * 60.f);
  std::snprintf(buf, sizeof buf, "%02d:%02d", hh, mm);
  R.text(mX, mY + mS + 5, buf, 2, 0xC4D0E2);

  // ---- money / weapon
  R.text(W - 10, 10, fmtMoney(P.money).c_str(), 3, 0x7CFF8A, 2);
  if (P.car < 0) {
    const Weapon& wp = WEAP[P.weapon];
    if (wp.melee) std::snprintf(buf, sizeof buf, "%s", wp.name);
    else std::snprintf(buf, sizeof buf, "%s  %d", wp.name, P.ammo[P.weapon]);
    R.text(W - 10, 38, buf, 2, 0xFFFFFF, 2);
  } else {
    R.text(W - 10, 38, CARS[cars[P.car].type].name, 2, 0xFFD84A, 2);
  }

  // ---- health bars
  R.rect(10, H - 16, 86, 8, 0x000000, 0.55f);
  R.rect(11, H - 15, (int)(84.f * clampf(P.hp / 100.f, 0.f, 1.f)), 6, 0xE04848);
  if (P.car >= 0) {
    const Vehicle& v = cars[P.car];
    R.rect(10, H - 27, 86, 8, 0x000000, 0.55f);
    R.rect(11, H - 26, (int)(84.f * clampf(v.hp / CARS[v.type].hp, 0.f, 1.f)), 6, 0xF0A030);
    float sp = len(v.vel) * 3.6f;
    std::snprintf(buf, sizeof buf, "%d", (int)(sp + 0.5f));
    R.text(W / 2, H - 42, buf, 4, 0xFFFFFF, 1);
    R.text(W / 2, H - 14, "KM/H", 1, 0xB8C4D6, 1);
  }

  // ---- prompts, notifications, world labels
  if (!prompt.empty() && P.deadT <= 0.f) R.text(W / 2, H - 78, prompt.c_str(), 2, 0xFFE27A, 1);
  for (size_t i = 0; i < notes.size(); i++) R.text(W / 2, 12 + (int)i * 16, notes[i].text.c_str(), 2, notes[i].col, 1);
  auto label = [&](V2 p, const char* name, uint32_t col) {
    float sx, sy, z;
    if (!R.project({p.x, 9.f, p.z}, sx, sy, z)) return;
    float d = len(p - P.pos);
    if (sx < 10 || sx > W - 10 || sy < 10 || sy > H - 20 || d < 12.f) return;
    std::snprintf(buf, sizeof buf, "%dM", (int)d);
    R.text((int)sx, (int)sy, name, 2, col, 1);
    R.text((int)sx, (int)sy + 15, buf, 1, 0xFFFFFF, 1);
  };
  label(world.garage, "GARAGE", 0xFFD21E);
  label(world.gunShop, "GUN SHOP", 0xFF6A40);
  if (time < 14.f && P.deadT <= 0.f) {
    R.text(W / 2, (int)(H * 0.58f), "LEFT STICK: MOVE    DRAG: LOOK    USE: ENTER CAR", 1, 0xE6EBF2, 1);
    R.text(W / 2, (int)(H * 0.58f) + 12, "DRIVE THE CYAN SPECTER - IT IS FAST", 1, 0x7FE0FF, 1);
  }
}

void Game::drawTouchUI(Renderer& R) {
  float bx = 76.f, by = H - 84.f, kx = bx, ky = by;
  bool active = false, fireHeld = false, useHeld = false, swapHeld = false;
  for (const Touch& t : touches) {
    if (t.id < 0) continue;
    if (t.role == 1) {
      active = true; bx = t.sx; by = t.sy;
      float dx = t.x - t.sx, dy = t.y - t.sy, m = std::sqrt(dx * dx + dy * dy);
      if (m > STICK_R) { dx *= STICK_R / m; dy *= STICK_R / m; }
      kx = bx + dx; ky = by + dy;
    }
    if (t.role == 2) fireHeld = true;
    if (t.role == 3) useHeld = true;
    if (t.role == 4) swapHeld = true;
  }
  R.ring(bx, by, STICK_R - 2, 2, 0xFFFFFF, active ? 0.55f : 0.25f);
  R.disc(kx, ky, 16, 0xFFFFFF, active ? 0.5f : 0.22f);
  auto button = [&](Btn b, const char* label, int scale, bool pressed, uint32_t col, bool hot) {
    R.disc(b.x, b.y, b.r, col, pressed ? 0.55f : (hot ? 0.38f : 0.22f));
    R.ring(b.x, b.y, b.r - 1, 2, hot ? 0xFFE27A : 0xFFFFFF, pressed ? 0.9f : 0.5f);
    R.text((int)b.x, (int)b.y - 3 * scale, label, scale, 0xFFFFFF, 1);
  };
  if (P.car >= 0) button(btnFire(W, H), "DRIFT", 1, fireHeld, 0x3070D0, false);
  else button(btnFire(W, H), WEAP[P.weapon].melee ? "PUNCH" : "FIRE", 2, fireHeld, 0xD03030, false);
  const char* ul = "USE";
  if (P.car >= 0) ul = promptKind == 2 ? "SELL" : "EXIT";
  else if (promptKind == 3) ul = "BUY";
  else if (promptKind == 1) ul = prompt.compare(0, 7, "CARJACK") == 0 ? "JACK" : "ENTER";
  button(btnUse(W, H), ul, 1, useHeld, 0x30A060, promptKind != 0);
  if (P.car < 0) button(btnSwap(W, H), "GUN", 1, swapHeld, 0x808090, false);
}
