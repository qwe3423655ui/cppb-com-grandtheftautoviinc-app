#include "game.h"

// ================================================================ data tables
const CarDef CARS[4] = {
  // name       len    wid    y0     body   cab    nose   tail   cb0     cb1    ct0     ct1     wb     wr     vmax  acc   grip  hp     mass    value
  {"PIXIE",   3.70f, 1.68f, 0.26f, 0.86f, 1.46f, 0.30f, 0.12f, -1.55f, 0.65f, -1.35f,  0.05f, 2.30f, 0.30f, 32.f, 8.5f, 7.5f, 90.f,  900.f,  450},
  {"ORION",   4.60f, 1.82f, 0.27f, 0.90f, 1.42f, 0.35f, 0.20f, -1.60f, 0.90f, -1.05f,  0.30f, 2.75f, 0.33f, 42.f, 11.f, 7.5f, 120.f, 1400.f, 900},
  {"BRUTE",   4.90f, 1.96f, 0.28f, 0.98f, 1.42f, 0.45f, 0.15f, -1.65f, 0.55f, -1.05f, -0.05f, 2.90f, 0.36f, 52.f, 15.f, 6.5f, 160.f, 1800.f, 2000},
  {"SPECTER", 4.50f, 2.02f, 0.22f, 0.72f, 1.10f, 0.75f, 0.20f, -1.25f, 0.95f, -0.75f,  0.15f, 2.70f, 0.34f, 85.f, 20.f, 8.5f, 110.f, 1300.f, 6500},
};
const Weapon WEAP[4] = {
  {"FISTS",   0,    0,  0,   14, 1, 2.2f,  0.f,    1.8f,  true},
  {"PISTOL",  300,  24, 120, 30, 1, 3.5f,  0.015f, 80.f,  false},
  {"SMG",     1200, 90, 360, 12, 1, 13.f,  0.06f,  65.f,  false},
  {"SHOTGUN", 1800, 16, 64,  15, 8, 1.1f,  0.14f,  32.f,  false},
};
static const Col CAR_COL[9] = {rgb(196, 40, 44), rgb(34, 92, 190), rgb(232, 232, 236), rgb(32, 32, 36), rgb(226, 170, 34), rgb(44, 140, 84), rgb(150, 152, 160), rgb(230, 112, 32), rgb(122, 56, 160)};
static const Col SPC_COL[5] = {rgb(24, 24, 28), rgb(236, 236, 242), rgb(210, 20, 44), rgb(30, 170, 210), rgb(250, 200, 0)};
static const Col SHIRTS[8] = {rgb(200, 60, 60), rgb(60, 110, 200), rgb(230, 200, 60), rgb(70, 160, 90), rgb(220, 220, 225), rgb(150, 80, 170), rgb(240, 130, 50), rgb(50, 50, 56)};
static const Col SKINS[5] = {rgb(240, 200, 170), rgb(214, 160, 120), rgb(170, 118, 84), rgb(120, 80, 56), rgb(90, 58, 40)};

std::string fmtMoney(int v) {
  std::string s = std::to_string(v < 0 ? -v : v);
  for (int i = (int)s.size() - 3; i > 0; i -= 3) s.insert(i, ",");
  return (v < 0 ? "-$" : "$") + s;
}

static const float START_YAW = 1.22f;   // camera looks east along the pavement at the start

// ================================================================ helpers
static inline V2 fwdOf(float yaw) { return {std::sin(yaw), std::cos(yaw)}; }
static inline V2 rtOf(float yaw) { return {-std::cos(yaw), std::sin(yaw)}; }
static V2 stopPoint(int ni, int nj, int d, int lane) { return nodePos(ni, nj) - dirOf(d) * STOPD + rightOf(d) * LANE[lane]; }
static V2 exitPoint(int ni, int nj, int d, int lane) { return nodePos(ni, nj) + dirOf(d) * STOPD + rightOf(d) * LANE[lane]; }
static float carRadius(const CarDef& d) { return d.wid * 0.5f * 0.96f; }
static void carCircles(const Vehicle& v, V2 out[3], float& r) {
  const CarDef& d = CARS[v.type];
  r = carRadius(d);
  V2 f = fwdOf(v.yaw);
  float off = d.len * 0.5f - r;
  out[0] = v.pos - f * off; out[1] = v.pos; out[2] = v.pos + f * off;
}
static float maxSteer(const CarDef& d, float vf) { float k = std::fabs(vf) / (0.42f * d.vmax); return 0.62f / (1.f + k * k); }
static int chooseMove(Rng& rng, int ni, int nj, int d) {          // 0 straight, 1 right, 2 left
  float w[3] = {nodeOk(ni + DX[d], nj + DZ[d]) ? 0.6f : 0.f, nodeOk(ni + DX[(d + 1) & 3], nj + DZ[(d + 1) & 3]) ? 0.26f : 0.f,
                nodeOk(ni + DX[(d + 3) & 3], nj + DZ[(d + 3) & 3]) ? 0.14f : 0.f};
  float sum = w[0] + w[1] + w[2];
  if (sum <= 0) return 0;
  float r = rng.f() * sum;
  for (int i = 0; i < 3; i++) { if (r < w[i]) return i; r -= w[i]; }
  return 0;
}
static V2 perimPoint(const Block& B, float off, float s, V2* dirOut) {
  float x0 = B.x0 + off, x1 = B.x1 - off, z0 = B.z0 + off, z1 = B.z1 - off, w = x1 - x0, h = z1 - z0, per = 2 * (w + h);
  s = std::fmod(s, per); if (s < 0) s += per;
  V2 d;
  if (s < w) { d = {1, 0}; if (dirOut) *dirOut = d; return {x0 + s, z0}; }
  s -= w;
  if (s < h) { d = {0, 1}; if (dirOut) *dirOut = d; return {x1, z0 + s}; }
  s -= h;
  if (s < w) { d = {-1, 0}; if (dirOut) *dirOut = d; return {x1 - s, z1}; }
  s -= w;
  d = {0, -1}; if (dirOut) *dirOut = d;
  return {x0, z1 - s};
}

void Game::notify(const std::string& s, uint32_t col) {
  notes.push_back({s, 3.6f, col});
  if (notes.size() > 4) notes.erase(notes.begin());
}
void Game::emit(uint8_t kind, V3 p, V3 v, float life, float size, Col c) {
  if (parts.size() > 900) return;
  Part q; q.kind = kind; q.p = p; q.v = v; q.life = q.max = life; q.size = size; q.c = c;
  parts.push_back(q);
}

// ================================================================ setup / spawning
void Game::init(int w, int h, uint32_t seed) {
  rng.s = seed * 747796405u + 2891336453u; if (!rng.s) rng.s = 1;
  world.gen(seed);
  W = w; H = h;
  cars.clear(); cars.reserve(160); peds.clear(); peds.reserve(100); picks.clear(); parts.clear(); tracers.clear(); notes.clear();
  P = Player();
  P.pos = {-31.f, -12.0f};
  V2 n; world.resolve(P.pos, 0.45f, n);
  camYaw = START_YAW; camPitch = 0.3f; time = 0; hour = 17.4f;
  P.yaw = camYaw;
  spawnVehicle(3, {-32.f, -8.3f}, -PI * 0.5f, VS_PARKED);     // the hyper-fast one, right next to the spawn
  spawnVehicle(1, {-20.f, -8.3f}, -PI * 0.5f, VS_PARKED);
  spawnVehicle(2, {-44.f, -8.3f}, -PI * 0.5f, VS_PARKED);
  spawnVehicle(0, {-56.f, -8.3f}, -PI * 0.5f, VS_PARKED);
  for (int i = 0; i < 22; i++) spawnParked(P.pos, 280);
  for (int i = 0; i < 26; i++) spawnAI(35, 240, false);
  for (int i = 0; i < 28; i++) spawnPed(false);
  for (int i = 0; i < 26; i++) spawnPickup();
  camFocus = {P.pos.x, 1.55f, P.pos.z};
  camPos = {P.pos.x, 3.f, P.pos.z - 5.f};
  notify("STEAL A CAR  -  SELL IT AT THE GARAGE", 0xFFD84A);
}
void Game::resize(int w, int h) { W = w; H = h; }

int Game::spawnVehicle(int type, V2 pos, float yaw, VState st) {
  int idx = -1;
  for (size_t i = 0; i < cars.size(); i++) if (cars[i].st == VS_FREE) { idx = (int)i; break; }
  if (idx < 0) { if (cars.size() >= cars.capacity()) return -1; cars.emplace_back(); idx = (int)cars.size() - 1; }
  Vehicle& v = cars[idx];
  v = Vehicle();
  v.type = type; v.st = st; v.pos = pos; v.yaw = yaw; v.hp = CARS[type].hp;
  v.color = type == 3 ? SPC_COL[rng.i(5)] : CAR_COL[rng.i(9)];
  v.cruise = rng.r(9.5f, 14.5f);
  return idx;
}

bool Game::spawnAI(float minD, float maxD, bool hidden) {
  for (int tries = 0; tries < 30; tries++) {
    int axis = rng.i(2), k = rng.i(NR), s = rng.i(NB), sg = rng.i(2);
    int d = axis == 0 ? (sg ? 0 : 2) : (sg ? 1 : 3);
    int ni = axis == 0 ? (d == 0 ? s + 1 : s) : k, nj = axis == 0 ? k : (d == 1 ? s + 1 : s);
    int move = chooseMove(rng, ni, nj, d);
    int lane = move == 2 ? 0 : (move == 1 ? 1 : rng.i(2));
    V2 a = axis == 0 ? nodePos(s, k) : nodePos(k, s), b = axis == 0 ? nodePos(s + 1, k) : nodePos(k, s + 1);
    V2 al = axis == 0 ? V2{1, 0} : V2{0, 1};
    V2 p0 = a + al * (STOPD + 4.f), p1 = b - al * (STOPD + 4.f);
    V2 pos = p0 + (p1 - p0) * rng.r(0.1f, 0.9f) + rightOf(d) * LANE[lane];
    float dist = len(pos - P.pos);
    if (dist < minD || dist > maxD) continue;
    if (hidden) {
      V2 to = norm(pos - V2{camPos.x, camPos.z}), cf = norm(V2{std::sin(camYaw), std::cos(camYaw)});
      if (dot(to, cf) > 0.35f && dist < 170.f) continue;
    }
    bool clear = true;
    for (const Vehicle& o : cars) if (o.st != VS_FREE && len(o.pos - pos) < 10.f) { clear = false; break; }
    if (!clear) continue;
    float r = rng.f();
    int type = r < 0.38f ? 0 : (r < 0.74f ? 1 : (r < 0.93f ? 2 : 3));
    int idx = spawnVehicle(type, pos, yawOf(d), VS_AI);
    if (idx < 0) return false;
    Vehicle& v = cars[idx];
    v.d = d; v.lane = lane; v.ni = ni; v.nj = nj; v.move = move;
    v.vel = fwdOf(v.yaw) * (v.cruise * 0.85f);
    return true;
  }
  return false;
}

int Game::spawnParked(V2 nearP, float radius) {
  for (int tries = 0; tries < 30; tries++) {
    int axis = rng.i(2), k = rng.i(NR), s = rng.i(NB), sg = rng.i(2);
    int d = axis == 0 ? (sg ? 0 : 2) : (sg ? 1 : 3);
    V2 a = axis == 0 ? nodePos(s, k) : nodePos(k, s), b = axis == 0 ? nodePos(s + 1, k) : nodePos(k, s + 1);
    V2 al = axis == 0 ? V2{1, 0} : V2{0, 1};
    V2 p0 = a + al * (STOPD + 5.f), p1 = b - al * (STOPD + 5.f);
    V2 pos = p0 + (p1 - p0) * rng.f() + rightOf(d) * 8.3f;
    if (len(pos - nearP) > radius || len(pos - P.pos) < 12.f) continue;
    bool clear = true;
    for (const Vehicle& o : cars) if (o.st != VS_FREE && len(o.pos - pos) < 6.5f) { clear = false; break; }
    if (!clear) continue;
    float r = rng.f();
    return spawnVehicle(r < 0.34f ? 0 : (r < 0.68f ? 1 : (r < 0.92f ? 2 : 3)), pos, yawOf(d) + rng.r(-0.03f, 0.03f), VS_PARKED);
  }
  return -1;
}

bool Game::spawnPed(bool hidden) {
  for (int tries = 0; tries < 30; tries++) {
    int i = rng.i(NB), j = rng.i(NB);
    const Block& B = world.blocks[i][j];
    float off = rng.r(2.3f, 2.8f), per = 2.f * ((B.x1 - B.x0 - 2 * off) + (B.z1 - B.z0 - 2 * off)), s = rng.f() * per;
    V2 p = perimPoint(B, off, s, nullptr);
    float dist = len(p - P.pos);
    if (dist < 18.f || dist > 230.f) continue;
    if (hidden) {
      V2 to = norm(p - V2{camPos.x, camPos.z}), cf = norm(V2{std::sin(camYaw), std::cos(camYaw)});
      if (dot(to, cf) > 0.35f && dist < 150.f) continue;
    }
    int idx = -1;
    for (size_t q = 0; q < peds.size(); q++) if (!peds[q].alive) { idx = (int)q; break; }
    if (idx < 0) { if (peds.size() >= peds.capacity()) return false; peds.emplace_back(); idx = (int)peds.size() - 1; }
    Ped& pd = peds[idx];
    pd = Ped();
    pd.alive = true; pd.bi = i; pd.bj = j; pd.off = off; pd.s = s; pd.dirSign = rng.i(2) ? 1 : -1; pd.pos = p;
    pd.speed = rng.f() < 0.12f ? rng.r(2.4f, 3.2f) : rng.r(1.1f, 1.7f);
    pd.shirt = SHIRTS[rng.i(8)]; pd.pants = rng.f() < 0.5f ? rgb(48, 56, 90) : rgb(60, 60, 64); pd.skin = SKINS[rng.i(5)];
    pd.t = rng.r(0, 6.f);
    return true;
  }
  return false;
}

static int addPickup(std::vector<Pickup>& picks, V2 pos, int value) {
  for (size_t i = 0; i < picks.size(); i++) if (!picks[i].alive) { picks[i] = {true, pos, value, 0}; return (int)i; }
  picks.push_back({true, pos, value, 0});
  return (int)picks.size() - 1;
}
void Game::spawnPickup() {
  for (int tries = 0; tries < 30; tries++) {
    int i = rng.i(NB), j = rng.i(NB);
    const Block& B = world.blocks[i][j];
    float per = 2.f * ((B.x1 - B.x0 - 5.f) + (B.z1 - B.z0 - 5.f));
    V2 p = perimPoint(B, 2.5f, rng.f() * per, nullptr);
    if (len(p - P.pos) < 25.f || len(p - P.pos) > 300.f) continue;
    addPickup(picks, p, 50 + 25 * rng.i(7));
    return;
  }
}

// ================================================================ main update
void Game::update(float dt) {
  dt = std::min(dt, 0.05f);
  time += dt;
  if (!timeFrozen) hour = std::fmod(hour + dt / 40.f, 24.f);
  gatherInput();
  updatePlayer(dt);
  for (size_t i = 0; i < cars.size(); i++) if (cars[i].st == VS_AI) aiDrive(cars[i], (int)i, dt);
  int n = std::max(1, (int)std::ceil(dt / 0.0167f));
  float h = dt / n;
  for (int k = 0; k < n; k++) { updateVehicles(h); collide(h); }
  if (P.car >= 0) { P.pos = cars[P.car].pos; P.yaw = cars[P.car].yaw; }
  updatePeds(dt);
  updateFX(dt);
  manageSpawns(dt);
  updateCamera(dt);
}

void Game::manageSpawns(float dt) {
  spawnT -= dt;
  if (spawnT > 0) return;
  spawnT = 0.35f;
  int ai = 0, live = 0;
  for (Vehicle& v : cars) {
    if (v.st == VS_FREE) continue;
    live++;
    float d = len(v.pos - P.pos);
    if (v.st == VS_AI) { ai++; if (d > 290.f) { v.st = VS_FREE; ai--; } }
    else if (v.st == VS_WRECK && v.burn <= 0 && d > 140.f) v.st = VS_FREE;
  }
  if (ai < 26 && live < 120) spawnAI(70.f, 230.f, true);
  int np = 0;
  for (const Ped& p : peds) if (p.alive) np++;
  if (np < 30) spawnPed(true);
}

// ================================================================ player
int Game::nearestCar(float reach) {
  int best = -1;
  float bd = reach;
  for (size_t i = 0; i < cars.size(); i++) {
    const Vehicle& v = cars[i];
    if (v.st != VS_PARKED && v.st != VS_AI) continue;
    if (len(v.vel) > 4.f) continue;
    const CarDef& D = CARS[v.type];
    V2 rel = P.pos - v.pos, f = fwdOf(v.yaw), r = rtOf(v.yaw);
    float dx = std::max(std::fabs(dot(rel, r)) - D.wid * 0.5f, 0.f), dz = std::max(std::fabs(dot(rel, f)) - D.len * 0.5f, 0.f);
    float d = std::sqrt(dx * dx + dz * dz);
    if (d < bd) { bd = d; best = (int)i; }
  }
  return best;
}

void Game::enterOrUse() {
  if (P.car >= 0) {
    Vehicle& v = cars[P.car];
    const CarDef& D = CARS[v.type];
    if (len(v.pos - world.garage) < 9.5f && len(v.vel) < 5.f) {
      int value = (int)(D.value * (0.35f + 0.65f * clampf(v.hp / D.hp, 0.f, 1.f)));
      P.money += value;
      notify(std::string("SOLD ") + D.name + "  +" + fmtMoney(value), 0x6CFF7A);
      P.pos = v.pos + rtOf(v.yaw) * 3.f;
      v.st = VS_FREE; P.car = -1;
      return;
    }
    exitCar();
    return;
  }
  if (len(P.pos - world.gunShop) < 9.5f) {
    const Weapon& cw = WEAP[P.weapon];
    if (!cw.melee && P.ammo[P.weapon] < cw.maxAmmo) {               // refill current weapon
      int cost = cw.price / 5;
      if (P.money < cost) { notify("NOT ENOUGH CASH", 0xFF7070); return; }
      P.money -= cost; P.ammo[P.weapon] = std::min(cw.maxAmmo, P.ammo[P.weapon] + cw.ammoPack);
      notify(std::string("AMMO ") + cw.name + "  -" + fmtMoney(cost), 0xFFD84A);
      return;
    }
    for (int w = 1; w < 4; w++) if (!P.owned[w]) {                   // buy next weapon
      if (P.money < WEAP[w].price) { notify("NOT ENOUGH CASH", 0xFF7070); return; }
      P.money -= WEAP[w].price; P.owned[w] = true; P.ammo[w] = WEAP[w].ammoPack; P.weapon = w;
      notify(std::string("BOUGHT ") + WEAP[w].name, 0xFFD84A);
      return;
    }
    notify("ALL WEAPONS OWNED  -  AMMO FULL", 0xBBBBBB);
    return;
  }
  int k = nearestCar(3.0f);
  if (k < 0) return;
  Vehicle& v = cars[k];
  const CarDef& D = CARS[v.type];
  if (v.st == VS_AI) {                                               // carjack: driver jumps out and runs
    V2 door = v.pos + rtOf(v.yaw) * (-(D.wid * 0.5f + 0.7f));
    for (size_t q = 0; q < peds.size() + 1; q++) {
      if (q == peds.size()) { if (peds.size() >= peds.capacity()) break; peds.emplace_back(); }
      if (!peds[q].alive) {
        Ped& pd = peds[q];
        pd = Ped(); pd.alive = true; pd.pos = door; pd.st = 1; pd.hp = 40;
        pd.fleeDir = norm(door - P.pos); pd.shirt = SHIRTS[rng.i(8)]; pd.pants = rgb(60, 60, 64); pd.skin = SKINS[rng.i(5)]; pd.speed = 5.f;
        break;
      }
    }
    notify(std::string("CARJACKED ") + D.name + "!", 0xFF9A3C);
  } else notify(std::string("STOLEN ") + D.name + (v.type == 3 ? "  -  HOLD ON!" : ""), 0xFF9A3C);
  v.st = VS_PLAYER; v.throttle = v.brake = v.steerIn = 0; v.hand = false; v.planned = false; v.inside = false;
  P.car = k; P.vel = {};
}

void Game::exitCar() {
  Vehicle& v = cars[P.car];
  if (len(v.vel) > 9.f) { notify("SLOW DOWN FIRST", 0xFF7070); return; }
  const CarDef& D = CARS[v.type];
  P.pos = v.pos + rtOf(v.yaw) * (-(D.wid * 0.5f + 0.8f));
  V2 n; world.resolve(P.pos, 0.45f, n);
  P.yaw = v.yaw; P.vel = {};
  v.st = VS_PARKED; v.throttle = 0; v.brake = 1; v.steerIn = 0;
  P.car = -1;
}

void Game::killPlayer() {
  if (P.deadT > 0) return;
  P.deadT = 3.6f; P.hp = 0;
  notify("KNOCKED OUT", 0xFF5050);
  if (P.car >= 0) { cars[P.car].st = VS_PARKED; cars[P.car].throttle = 0; cars[P.car].brake = 1; P.car = -1; }
  P.vel = {};
  trauma = 1.f;
}

void Game::updatePlayer(float dt) {
  P.muzzle = std::max(0.f, P.muzzle - dt);
  P.hurtCd = std::max(0.f, P.hurtCd - dt);
  if (P.deadT > 0) {
    P.deadT -= dt;
    if (P.deadT <= 0) {
      int fee = std::min(P.money, 200);
      P.money -= fee; P.hp = 100; P.pos = world.spawn; P.yaw = START_YAW; camYaw = START_YAW;
      V2 n; world.resolve(P.pos, 0.45f, n);
      camFocus = {P.pos.x, 1.55f, P.pos.z};
      notify("HOSPITAL FEE  -" + fmtMoney(fee), 0xFF7070);
    }
    return;
  }
  if (P.hp < 100.f && P.hurtCd <= 0.f) P.hp = std::min(100.f, P.hp + 1.5f * dt);
  if (P.hp <= 0.f) { killPlayer(); return; }
  if (in.use) enterOrUse();
  if (in.swap && P.car < 0)
    for (int k = 1; k <= 4; k++) { int w = (P.weapon + k) % 4; if (P.owned[w]) { P.weapon = w; break; } }

  prompt.clear(); promptKind = 0;
  if (P.car >= 0) {
    Vehicle& v = cars[P.car];
    float sy = std::fabs(in.my) < 0.08f ? 0.f : in.my, sx = std::fabs(in.mx) < 0.06f ? 0.f : in.mx;
    v.throttle = clampf(sy, -1.f, 1.f); v.steerIn = clampf(sx * 1.25f, -1.f, 1.f); v.hand = in.brake; v.brake = 0;
    if (len(v.pos - world.garage) < 9.5f && len(v.vel) < 5.f) {
      const CarDef& D = CARS[v.type];
      promptKind = 2;
      prompt = std::string("SELL ") + D.name + "  +" + fmtMoney((int)(D.value * (0.35f + 0.65f * clampf(v.hp / D.hp, 0.f, 1.f))));
    }
    return;
  }

  // ---- on foot
  V2 fc = fwdOf(camYaw), rc = rtOf(camYaw);
  V2 wish = fc * in.my + rc * in.mx;
  float mag = std::min(1.f, len(wish));
  V2 wd = mag > 0.01f ? wish * (1.f / len(wish)) : V2{};
  P.vel = P.vel + (wd * (mag * 6.0f) - P.vel) * std::min(1.f, dt * 12.f);
  P.pos = P.pos + P.vel * dt;
  V2 n;
  if (world.resolve(P.pos, 0.42f, n)) { float vn = dot(P.vel, n); if (vn < 0) P.vel = P.vel - n * vn; }
  for (Vehicle& v : cars) {                                          // cars are solid; fast ones hurt
    if (v.st == VS_FREE) continue;
    V2 c[3]; float r; carCircles(v, c, r);
    float sp = len(v.vel);
    for (int k = 0; k < 3; k++) {
      V2 d = P.pos - c[k];
      float dd = len(d);
      if (dd < r + 0.42f && dd > 1e-4f) {
        P.pos = P.pos + d * ((r + 0.42f - dd) / dd);
        if (sp > 3.5f && P.hurtCd <= 0.f) { P.hp -= sp * 2.4f; P.hurtCd = 0.5f; P.vel = v.vel * 0.5f; trauma = std::min(1.f, trauma + 0.4f); }
      }
    }
  }
  float sp = len(P.vel);
  if (sp > 0.4f && !in.fire) P.yaw += wrapPi(std::atan2(P.vel.x, P.vel.z) - P.yaw) * std::min(1.f, dt * 14.f);
  P.walk += sp * dt * 1.7f;

  if (len(P.pos - world.gunShop) < 9.5f) {
    const Weapon& cw = WEAP[P.weapon];
    promptKind = 3;
    if (!cw.melee && P.ammo[P.weapon] < cw.maxAmmo) prompt = std::string("AMMO ") + cw.name + "  " + fmtMoney(cw.price / 5);
    else {
      int nw = -1;
      for (int w = 1; w < 4; w++) if (!P.owned[w]) { nw = w; break; }
      prompt = nw < 0 ? "ALL WEAPONS OWNED" : std::string("BUY ") + WEAP[nw].name + "  " + fmtMoney(WEAP[nw].price);
    }
  } else {
    int k = nearestCar(3.0f);
    if (k >= 0) { promptKind = 1; prompt = std::string(cars[k].st == VS_AI ? "CARJACK " : "ENTER ") + CARS[cars[k].type].name; }
  }
  firePlayer(dt);
  if (P.hp <= 0.f) killPlayer();
}

// ================================================================ combat
void Game::hurtPed(Ped& p, float dmg, V2 from) {
  if (p.st == 2) return;
  p.hp -= dmg;
  if (p.hp <= 0.f) {
    p.st = 2; p.stT = 0; p.vel = from * 3.5f;
    if (rng.f() < 0.85f) addPickup(picks, p.pos + from * 0.6f, 15 + 10 * rng.i(10));
    for (int i = 0; i < 6; i++) emit(1, {p.pos.x, 0.9f, p.pos.z}, {rng.r(-1, 1), rng.r(0.5f, 1.5f), rng.r(-1, 1)}, 0.7f, 0.35f, {0.6f, 0.6f, 0.6f});
  } else {
    p.st = 1; p.stT = 0;
    V2 away = norm(p.pos - P.pos);
    p.fleeDir = norm(away + V2{rng.r(-0.5f, 0.5f), rng.r(-0.5f, 0.5f)});
    p.speed = 5.2f;
  }
}

void Game::damageCar(int idx, float dmg) {
  Vehicle& v = cars[idx];
  if (v.st == VS_WRECK || v.st == VS_FREE) return;
  v.hp -= dmg;
  if (v.hp <= 0.f) explode(idx);
}

void Game::explode(int idx) {
  Vehicle& v = cars[idx];
  bool wasPlayer = v.st == VS_PLAYER;
  V2 pos = v.pos;
  v.st = VS_WRECK; v.hp = 0; v.burn = 14.f; v.throttle = 0; v.brake = 1; v.steerIn = 0;
  v.color = rgb(34, 30, 28);
  V3 c{pos.x, 0.8f, pos.z};
  for (int i = 0; i < 16; i++) emit(2, c, {rng.r(-5, 5), rng.r(2, 8), rng.r(-5, 5)}, rng.r(0.5f, 1.1f), rng.r(1.2f, 2.6f), {1.0f, rng.r(0.4f, 0.7f), 0.15f});
  for (int i = 0; i < 14; i++) emit(0, c, {rng.r(-12, 12), rng.r(2, 12), rng.r(-12, 12)}, rng.r(0.4f, 0.9f), 0.35f, {1.0f, 0.8f, 0.4f});
  for (int i = 0; i < 6; i++) emit(1, c, {rng.r(-2, 2), rng.r(2, 5), rng.r(-2, 2)}, rng.r(2.f, 3.5f), rng.r(1.5f, 2.8f), {0.12f, 0.12f, 0.12f});
  trauma = std::min(1.f, trauma + (len(pos - P.pos) < 40.f ? 0.9f : 0.2f));
  for (Ped& p : peds) if (p.alive && p.st != 2 && len(p.pos - pos) < 8.f) hurtPed(p, 999.f, norm(p.pos - pos));
  if (wasPlayer) {
    P.pos = pos + V2{2.f, 0.f}; P.car = -1; P.vel = {}; P.hp -= 45.f; P.hurtCd = 1.f;
    V2 n; world.resolve(P.pos, 0.45f, n);
    notify("CAR DESTROYED!", 0xFF5050);
  } else if (P.car < 0 && len(P.pos - pos) < 8.f) { P.hp -= 45.f * (1.f - len(P.pos - pos) / 8.f); P.hurtCd = 1.f; }
  for (size_t i = 0; i < cars.size(); i++) {
    if ((int)i == idx || cars[i].st == VS_FREE || cars[i].st == VS_WRECK) continue;
    float d = len(cars[i].pos - pos);
    if (d < 9.f) { cars[i].vel = cars[i].vel + norm(cars[i].pos - pos) * (6.f * (1.f - d / 9.f)); damageCar((int)i, 55.f * (1.f - d / 9.f)); }
  }
}

bool Game::hitScan(V2 o, V2 dir, float range, float dmg, bool melee, V3* endOut) {
  float best = range;
  int kind = 0, idx = -1;
  if (!melee) { float tw = world.rayWorld(o, dir, range); if (tw < best) { best = tw; kind = 3; } }
  auto rayCircle = [&](V2 c, float r) -> float {
    V2 oc = o - c;
    float b = dot(oc, dir), cq = dot(oc, oc) - r * r, disc = b * b - cq;
    if (disc < 0) return 1e9f;
    float t = -b - std::sqrt(disc);
    return t >= 0 ? t : 1e9f;
  };
  for (size_t i = 0; i < peds.size(); i++) {
    if (!peds[i].alive || peds[i].st == 2) continue;
    float t = rayCircle(peds[i].pos, 0.45f);
    if (t < best) { best = t; kind = 1; idx = (int)i; }
  }
  if (!melee)
    for (size_t i = 0; i < cars.size(); i++) {
      if (cars[i].st == VS_FREE || cars[i].st == VS_PLAYER) continue;
      V2 c[3]; float r; carCircles(cars[i], c, r);
      for (int k = 0; k < 3; k++) { float t = rayCircle(c[k], r); if (t < best) { best = t; kind = 2; idx = (int)i; } }
    }
  V2 hit = o + dir * best;
  if (endOut) *endOut = {hit.x, 1.2f, hit.z};
  if (kind == 1) { hurtPed(peds[idx], dmg, dir); if (!melee) emit(0, {hit.x, 1.1f, hit.z}, {0, 1, 0}, 0.15f, 0.2f, {1.0f, 0.6f, 0.4f}); }
  else if (kind == 2) { damageCar(idx, dmg * 0.3f); for (int i = 0; i < 4; i++) emit(0, {hit.x, 1.0f, hit.z}, {rng.r(-4, 4), rng.r(1, 5), rng.r(-4, 4)}, 0.3f, 0.18f, {1.0f, 0.85f, 0.5f}); }
  else if (kind == 3) for (int i = 0; i < 4; i++) emit(0, {hit.x, 1.2f, hit.z}, {rng.r(-4, 4) - dir.x * 3.f, rng.r(1, 5), rng.r(-4, 4) - dir.z * 3.f}, 0.3f, 0.18f, {1.0f, 0.85f, 0.5f});
  return kind != 0;
}

void Game::firePlayer(float dt) {
  P.fireCd -= dt;
  if (!in.fire || P.fireCd > 0.f) return;
  const Weapon& Wp = WEAP[P.weapon];
  if (!Wp.melee && P.ammo[P.weapon] <= 0) { P.fireCd = 0.4f; notify("OUT OF AMMO - FIND THE GUN SHOP", 0xFF7070); return; }
  P.fireCd = 1.f / Wp.rate;
  V2 face = fwdOf(camYaw), aim = face;
  float bestA = 0.5f;
  const Ped* tgt = nullptr;
  for (const Ped& p : peds) {
    if (!p.alive || p.st == 2) continue;
    V2 to = p.pos - P.pos;
    float dist = len(to);
    if (dist < 0.4f || dist > Wp.range * 0.9f) continue;
    float a = std::acos(clampf(dot(norm(to), face), -1.f, 1.f));
    if (a < bestA) { bestA = a; tgt = &p; }
  }
  if (tgt) aim = norm(tgt->pos - P.pos);
  P.yaw = std::atan2(aim.x, aim.z);
  P.muzzle = 0.07f;
  V2 rr = rtOf(P.yaw);
  V3 mz{P.pos.x + aim.x * 0.8f + rr.x * 0.25f, 1.25f, P.pos.z + aim.z * 0.8f + rr.z * 0.25f};
  if (Wp.melee) {
    hitScan(P.pos, aim, Wp.range, (float)Wp.dmg, true, nullptr);
  } else {
    P.ammo[P.weapon]--;
    for (int k = 0; k < Wp.pellets; k++) {
      float a = (rng.f() * 2.f - 1.f) * Wp.spread, cs = std::cos(a), sn = std::sin(a);
      V2 d{aim.x * cs - aim.z * sn, aim.x * sn + aim.z * cs};
      V3 end;
      hitScan(P.pos + aim * 0.4f, d, Wp.range, (float)Wp.dmg, false, &end);
      tracers.push_back({mz, end, 0.07f});
    }
    trauma = std::min(1.f, trauma + (P.weapon == 3 ? 0.18f : 0.05f));
    emit(0, mz, {aim.x * 3, 0.5f, aim.z * 3}, 0.08f, 0.35f, {1.0f, 0.8f, 0.4f});
  }
  for (Ped& p : peds)                                                   // gunfire scares everyone nearby
    if (p.alive && p.st == 0 && len(p.pos - P.pos) < 32.f) { p.st = 1; p.stT = 0; p.speed = 5.f; p.fleeDir = norm(norm(p.pos - P.pos) + V2{rng.r(-0.6f, 0.6f), rng.r(-0.6f, 0.6f)}); }
}

// ================================================================ vehicles
void Game::updateVehicles(float dt) {
  for (Vehicle& v : cars) if (v.st != VS_FREE) stepVehicle(v, dt);
}

void Game::stepVehicle(Vehicle& v, float dt) {
  const CarDef& D = CARS[v.type];
  bool dead = v.st == VS_PARKED || v.st == VS_WRECK;
  float thr = dead ? 0.f : v.throttle, brk = dead ? 1.f : v.brake, sIn = dead ? 0.f : v.steerIn;
  bool hand = !dead && v.hand;
  V2 f = fwdOf(v.yaw), r = rtOf(v.yaw);
  float vf = dot(v.vel, f), vl = dot(v.vel, r);
  float a = 0;
  if (thr > 0) { if (vf >= 0) { float k = vf / D.vmax; a = D.acc * thr * std::max(0.f, 1.f - k * k); } else a = D.acc * 2.2f * thr; }
  else if (thr < 0) {
    if (vf > 0.6f) a = D.acc * 1.5f * thr;
    else { float k = -vf / (D.vmax * 0.25f); a = D.acc * 0.6f * thr * std::max(0.f, 1.f - k * k); }
  }
  vf += a * dt;
  vf *= std::exp(-0.03f * dt);
  if (thr == 0.f) { float rr = 0.6f * dt; vf = std::fabs(vf) <= rr ? 0.f : vf - (vf > 0 ? rr : -rr); }
  float b = 20.f * brk * dt;
  vf = std::fabs(vf) <= b ? 0.f : vf - (vf > 0 ? b : -b);
  float grip = dead ? 6.f : (hand ? 1.4f : D.grip);
  vl *= std::exp(-grip * dt);
  float ms = maxSteer(D, vf);
  v.steer += clampf(-sIn * ms - v.steer, -3.5f * dt, 3.5f * dt);
  float yawRate = vf / D.wb * std::tan(v.steer);
  if (hand) yawRate *= 1.5f;
  float yMax = D.grip * 4.5f * (hand ? 1.5f : 1.f) / std::max(std::fabs(vf), 4.f);
  yawRate = clampf(yawRate, -yMax, yMax);
  v.vel = f * vf + r * vl;
  v.yaw += yawRate * dt;
  v.pos = v.pos + v.vel * dt;
  float acc = (vf - v.lastVf) / std::max(dt, 1e-4f);
  v.lastVf = vf;
  float tp = clampf(-acc * 0.0035f, -0.06f, 0.06f), tr = clampf(yawRate * vf * 0.0055f, -0.08f, 0.08f);
  v.pitch += (tp - v.pitch) * std::min(1.f, dt * 8.f);
  v.roll += (tr - v.roll) * std::min(1.f, dt * 8.f);
}

float Game::obstacleLimit(const Vehicle& v, V2 fwd, V2 rt, float vf, int self) {
  if (v.ghost > 0.f) return 99.f;
  const CarDef& D = CARS[v.type];
  float best = 99.f, range = 7.f + std::max(vf, 0.f) * 1.4f;
  auto consider = [&](V2 pos, float halfLen, float halfW, float ov) {
    V2 rel = pos - v.pos;
    float fd = dot(rel, fwd), lat = std::fabs(dot(rel, rt));
    if (fd <= 0.f || fd > range + halfLen || lat > D.wid * 0.5f + halfW + 0.55f) return;
    float gap = fd - D.len * 0.5f - halfLen;
    best = std::min(best, std::sqrt(std::max(0.f, ov * ov + 2.f * 5.f * std::max(0.f, gap - 1.8f))));
  };
  for (size_t k = 0; k < cars.size(); k++) {
    if ((int)k == self || cars[k].st == VS_FREE) continue;
    const Vehicle& o = cars[k];
    consider(o.pos, CARS[o.type].len * 0.5f, CARS[o.type].wid * 0.5f, std::max(0.f, dot(o.vel, fwd)));
  }
  for (const Ped& p : peds) if (p.alive && p.st != 2) consider(p.pos, 0.35f, 0.35f, 0.f);
  if (P.car < 0 && P.deadT <= 0) consider(P.pos, 0.4f, 0.4f, 0.f);
  return best;
}

void Game::planPath(Vehicle& v) {
  int dOut = v.move == 0 ? v.d : (v.move == 1 ? (v.d + 1) & 3 : (v.d + 3) & 3);
  int mi = v.ni + DX[dOut], mj = v.nj + DZ[dOut];
  v.nextMove = nodeOk(mi, mj) ? chooseMove(rng, mi, mj, dOut) : 0;
  v.exitLane = v.nextMove == 2 ? 0 : (v.nextMove == 1 ? 1 : rng.i(2));
  v.dOut = dOut;
  V2 p0 = stopPoint(v.ni, v.nj, v.d, v.lane), p1 = exitPoint(v.ni, v.nj, dOut, v.exitLane), c;
  if (dOut == v.d) c = (p0 + p1) * 0.5f;
  else if ((v.d & 1) == 0) c = {p1.x, p0.z};
  else c = {p0.x, p1.z};
  v.pn = 6;
  for (int k = 0; k < 6; k++) { float t = (k + 1) / 6.f, u = 1.f - t; v.path[k] = p0 * (u * u) + c * (2.f * u * t) + p1 * (t * t); }
  v.pi = 0; v.planned = true;
}

void Game::aiDrive(Vehicle& v, int idx, float dt) {
  const CarDef& D = CARS[v.type];
  V2 fwd = fwdOf(v.yaw), rt = rtOf(v.yaw);
  float vf = dot(v.vel, fwd), vdes = v.cruise;
  V2 target = v.pos + fwd * 10.f;
  if (!v.inside) {
    V2 dir = dirOf(v.d), stop = stopPoint(v.ni, v.nj, v.d, v.lane);
    float s = dot(v.pos - stop, dir);                                   // < 0 before the stop line
    if (!v.planned && s > -34.f) planPath(v);
    float L = clampf(5.f + 0.3f * std::fabs(vf), 6.f, 15.f);
    target = (s + L > 0.f && v.planned) ? v.path[0] : stop + dir * (s + L);
    float ds = -s;
    if (ds < 70.f) {
      int st = sigState(v.ni, v.nj, v.d, v.move == 2, time);
      float stopAt = ds - (D.len * 0.5f + 0.7f);
      bool must = st == 0 || (st == 1 && vf * vf / (2.f * 5.f) <= stopAt);
      if (must && stopAt > -3.f) vdes = std::min(vdes, std::sqrt(std::max(0.f, 2.f * 3.2f * std::max(stopAt, 0.f))));
    }
    if (v.move == 2 && ds < 30.f) {                                     // permissive left: yield to oncoming traffic
      V2 c = nodePos(v.ni, v.nj), rgt = rightOf(v.d);
      bool oncoming = false;
      for (size_t k = 0; k < cars.size() && !oncoming; k++) {
        const Vehicle& o = cars[k];
        if ((int)k == idx || o.st == VS_FREE || o.st == VS_PARKED || o.st == VS_WRECK) continue;
        V2 rel = o.pos - c;
        float along = dot(rel, dir), lat = dot(rel, rgt);
        if (along < -6.f || along > 48.f || std::fabs(lat) > 11.f) continue;
        if (dot(fwdOf(o.yaw), dir) > -0.6f) continue;                    // not heading towards us
        if (o.st == VS_AI && o.move == 2) continue;                      // opposing left turns do not cross
        if (len(o.vel) < 1.f && along > 12.f) continue;                  // queued far away
        oncoming = true;
      }
      if (oncoming) {
        float stopAt = ds - (D.len * 0.5f + 0.7f);
        if (stopAt > -3.f) vdes = std::min(vdes, std::sqrt(std::max(0.f, 2.f * 3.2f * std::max(stopAt, 0.f))));
      }
    }
    if (v.move != 0 && ds < 24.f) vdes = std::min(vdes, 8.f);
    if (s >= -0.2f && v.planned) { v.inside = true; v.pi = 0; }
  } else if (v.pi >= v.pn) {                                            // left the junction
    v.d = v.dOut; v.ni += DX[v.dOut]; v.nj += DZ[v.dOut]; v.lane = v.exitLane; v.move = v.nextMove;
    v.inside = false; v.planned = false; v.pn = 0; v.pi = 0;
  } else {
    if (len(v.path[v.pi] - v.pos) < std::max(3.f, 0.22f * std::fabs(vf))) v.pi++;
    if (v.pi < v.pn) target = v.path[v.pi];
    if (v.move != 0) vdes = std::min(vdes, 8.5f);
  }
  float obs = obstacleLimit(v, fwd, rt, vf, idx);
  bool blocked = obs < vdes - 0.5f && obs < 2.5f;
  vdes = std::min(vdes, obs);
  if (blocked && vf < 0.6f) v.stuck += dt; else v.stuck = std::max(0.f, v.stuck - dt * 2.f);
  if (v.stuck > 9.f) { v.ghost = 3.5f; v.stuck = 0; }
  if (v.ghost > 0.f) v.ghost -= dt;

  V2 to = target - v.pos;
  float Ld = std::max(3.f, len(to)), ang = wrapPi(std::atan2(to.x, to.z) - v.yaw);
  float delta = std::atan(D.wb * 2.f * std::sin(ang) / Ld);
  v.steerIn = clampf(-delta / maxSteer(D, vf), -1.f, 1.f);
  float dv = vdes - vf;
  if (dv > 0.f) { v.throttle = clampf(dv * 0.6f, 0.f, 0.85f); v.brake = 0.f; }
  else { v.throttle = 0.f; v.brake = clampf(-dv * 0.22f, 0.f, 0.5f); }
  if (vdes < 0.3f) { v.throttle = 0.f; v.brake = vf > 0.5f ? 0.6f : 0.9f; }
}

void Game::collide(float dt) {
  (void)dt;
  // cars vs world
  for (size_t i = 0; i < cars.size(); i++) {
    Vehicle& v = cars[i];
    if (v.st == VS_FREE) continue;
    V2 c[3]; float r; carCircles(v, c, r);
    bool hit = false; V2 nrm;
    for (int k = 0; k < 3; k++) {
      V2 p = c[k], n;
      if (world.resolve(p, r, n)) { V2 push = p - c[k]; v.pos = v.pos + push; for (int q = k + 1; q < 3; q++) c[q] = c[q] + push; nrm = n; hit = true; }
    }
    if (hit) {
      float vn = dot(v.vel, nrm);
      if (vn < 0) {
        v.vel = v.vel - nrm * (vn * 1.25f);
        float dmg = std::max(0.f, -vn - 3.5f) * 2.2f;
        if (dmg > 0.f) {
          if (v.st == VS_PLAYER) trauma = std::min(1.f, trauma + dmg * 0.02f);
          if (v.st != VS_PARKED) damageCar((int)i, dmg);
        }
      }
    }
  }
  // cars vs cars
  for (size_t a = 0; a < cars.size(); a++) {
    if (cars[a].st == VS_FREE) continue;
    for (size_t b = a + 1; b < cars.size(); b++) {
      if (cars[b].st == VS_FREE) continue;
      Vehicle &A = cars[a], &B = cars[b];
      const CarDef &DA = CARS[A.type], &DB = CARS[B.type];
      if (len(A.pos - B.pos) > (DA.len + DB.len) * 0.5f + 0.3f) continue;
      V2 ca[3], cb[3]; float ra, rb;
      carCircles(A, ca, ra); carCircles(B, cb, rb);
      float best = 0; V2 n;
      for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) {
        V2 d = cb[j] - ca[i];
        float dd = len(d), pen = ra + rb - dd;
        if (pen > best && dd > 1e-4f) { best = pen; n = d * (1.f / dd); }
      }
      if (best <= 0.f) continue;
      float ma = DA.mass * (A.st == VS_PARKED ? 1.6f : 1.f), mb = DB.mass * (B.st == VS_PARKED ? 1.6f : 1.f);
      A.pos = A.pos - n * (best * mb / (ma + mb)); B.pos = B.pos + n * (best * ma / (ma + mb));
      float vn = dot(B.vel - A.vel, n);
      if (vn < 0) {
        float j = -(1.f + 0.3f) * vn / (1.f / ma + 1.f / mb);
        A.vel = A.vel - n * (j / ma); B.vel = B.vel + n * (j / mb);
        float dmg = std::max(0.f, -vn - 3.f) * 2.2f;
        if (dmg > 0.f) {
          if (A.st == VS_PLAYER || B.st == VS_PLAYER) trauma = std::min(1.f, trauma + dmg * 0.02f);
          damageCar((int)a, dmg * mb / (ma + mb) * 2.f); damageCar((int)b, dmg * ma / (ma + mb) * 2.f);
        }
      }
    }
  }
}

// ================================================================ pedestrians, pickups, effects
void Game::updatePeds(float dt) {
  for (Ped& p : peds) {
    if (!p.alive) continue;
    p.t += dt * (p.st == 1 ? 2.2f : 1.f);
    if (p.st == 2) { p.stT += dt; p.pos = p.pos + p.vel * dt; p.vel = p.vel * std::exp(-6.f * dt); if (p.stT > 6.f) p.alive = false; continue; }
    if (p.st == 0) {
      const Block& B = world.blocks[p.bi][p.bj];
      p.s += p.dirSign * p.speed * dt;
      V2 d;
      p.pos = perimPoint(B, p.off, p.s, &d);
      p.yaw += wrapPi(std::atan2(d.x * p.dirSign, d.z * p.dirSign) - p.yaw) * std::min(1.f, dt * 8.f);
    } else {
      p.pos = p.pos + p.fleeDir * (p.speed * dt);
      V2 n; world.resolve(p.pos, 0.3f, n);
      p.yaw = std::atan2(p.fleeDir.x, p.fleeDir.z);
      p.stT += dt;
      if (p.stT > 12.f && len(p.pos - P.pos) > 45.f) p.alive = false;
    }
    for (Vehicle& v : cars) {                                           // run-over check
      if (v.st == VS_FREE || len(v.pos - p.pos) > 4.f) continue;
      float sp = len(v.vel);
      if (sp < 2.5f) continue;
      V2 c[3]; float r; carCircles(v, c, r);
      for (int k = 0; k < 3; k++) if (len(c[k] - p.pos) < r + 0.35f) { hurtPed(p, sp * 9.f, norm(v.vel)); p.vel = v.vel * 0.55f; break; }
    }
    if (len(p.pos - P.pos) > 240.f) p.alive = false;
  }
}

void Game::updateFX(float dt) {
  for (size_t i = 0; i < parts.size();) {
    Part& q = parts[i];
    q.life -= dt;
    if (q.life <= 0.f) { parts[i] = parts.back(); parts.pop_back(); continue; }
    q.p = q.p + q.v * dt;
    if (q.kind == 0) q.v.y -= 9.f * dt;
    else if (q.kind == 1) q.v = q.v * (1.f - dt * 0.7f);
    else q.v = q.v * (1.f - dt * 2.f);
    i++;
  }
  for (size_t i = 0; i < tracers.size();) { tracers[i].life -= dt; if (tracers[i].life <= 0) { tracers[i] = tracers.back(); tracers.pop_back(); } else i++; }
  for (size_t i = 0; i < notes.size();) { notes[i].t -= dt; if (notes[i].t <= 0) notes.erase(notes.begin() + i); else i++; }
  trauma = std::max(0.f, trauma - dt * 1.4f);

  for (Vehicle& v : cars) {                                             // fire & smoke
    if (v.st == VS_FREE) continue;
    const CarDef& D = CARS[v.type];
    v.smokeT -= dt;
    if (v.st == VS_WRECK && v.burn > 0.f) {
      v.burn -= dt;
      if (v.smokeT <= 0.f) {
        v.smokeT = 0.09f;
        V3 c{v.pos.x + rng.r(-0.8f, 0.8f), 0.9f, v.pos.z + rng.r(-1.2f, 1.2f)};
        emit(2, c, {rng.r(-0.5f, 0.5f), rng.r(1.5f, 3.f), rng.r(-0.5f, 0.5f)}, 0.5f, rng.r(0.8f, 1.5f), {1.0f, 0.5f, 0.12f});
        emit(1, c, {rng.r(-0.4f, 0.4f), rng.r(2.f, 3.5f), rng.r(-0.4f, 0.4f)}, 2.2f, 1.4f, {0.1f, 0.1f, 0.1f});
      }
    } else if (v.st != VS_WRECK && v.hp < D.hp * 0.35f && v.smokeT <= 0.f) {
      v.smokeT = 0.12f;
      V2 f = fwdOf(v.yaw);
      emit(1, {v.pos.x + f.x * D.len * 0.35f, 1.0f, v.pos.z + f.z * D.len * 0.35f}, {rng.r(-0.3f, 0.3f), 1.8f, rng.r(-0.3f, 0.3f)}, 1.6f, 0.9f, {0.2f, 0.2f, 0.2f});
    }
  }
  for (Pickup& k : picks) {                                             // cash
    if (!k.alive) continue;
    k.t += dt;
    float reach = P.car >= 0 ? 3.2f : 1.5f;
    if (P.deadT <= 0 && len(k.pos - P.pos) < reach) {
      k.alive = false; P.money += k.value;
      notify("+" + fmtMoney(k.value), 0x6CFF7A);
      int alive = 0;
      for (const Pickup& q : picks) if (q.alive) alive++;
      if (alive < 24) spawnPickup();
    }
  }
}

// ================================================================ camera
void Game::updateCamera(float dt) {
  bool drag = std::fabs(in.camDX) + std::fabs(in.camDY) > 0.01f;
  camYaw -= in.camDX * 0.0055f;
  camPitch = clampf(camPitch + in.camDY * 0.004f, 0.03f, 1.1f);
  V3 focus;
  float dist, sp = 0;
  if (P.car >= 0) {
    const Vehicle& v = cars[P.car];
    sp = len(v.vel);
    float ty = v.yaw;
    if (sp > 5.f) { float dv = wrapPi(std::atan2(v.vel.x, v.vel.z) - v.yaw); if (std::fabs(dv) < 1.9f) ty = v.yaw + dv * 0.45f; }
    if (!drag) {
      camYaw += wrapPi(ty - camYaw) * std::min(1.f, dt * (2.2f + sp * 0.03f));
      camPitch += (0.2f + std::min(sp, 80.f) * 0.0008f - camPitch) * std::min(1.f, dt * 1.5f);
    }
    focus = {v.pos.x, 1.2f, v.pos.z};
    dist = 7.4f + std::min(sp, 80.f) * 0.05f;
  } else {
    focus = {P.pos.x, 1.55f, P.pos.z};
    dist = 5.4f;
  }
  camFocus = camFocus + (focus - camFocus) * std::min(1.f, dt * (P.car >= 0 ? 16.f : 14.f));
  float cp = std::cos(camPitch), spn = std::sin(camPitch);
  V3 f{std::sin(camYaw) * cp, -spn, std::cos(camYaw) * cp};
  V2 hd{-f.x, -f.z};
  float hl = len(hd), dd = dist;
  if (hl > 1e-3f) {
    float maxT = dd * hl, t = world.rayWorld({camFocus.x, camFocus.z}, hd * (1.f / hl), maxT, false);
    if (t < maxT) dd = std::max(1.2f, dd * ((t - 0.7f) / maxT));
  }
  camPos = camFocus - f * dd;
  camPos.y = std::max(camPos.y, 0.8f);
  fov += (1.05f + std::min(sp, 85.f) * 0.0035f - fov) * std::min(1.f, dt * 3.f);
}
