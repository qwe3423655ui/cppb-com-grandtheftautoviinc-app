// NEON CITY - game logic: cars, traffic, peds, weapons, money, HUD, touch controls
#pragma once
#include "world.h"

struct CarDef {
  const char* name;
  float len, wid, y0, bodyTop, cabTop, nose, tail, cb0, cb1, ct0, ct1, wb, wr;   // model
  float vmax, acc, grip, hp, mass;                                               // handling
  int value;                                                                     // chop-shop price
};
extern const CarDef CARS[4];                // 0 Pixie (compact) .. 3 Specter (hyper-fast)

enum VState : uint8_t { VS_FREE, VS_PARKED, VS_AI, VS_PLAYER, VS_WRECK };
struct Vehicle {
  int type = 0;
  VState st = VS_FREE;
  V2 pos, vel;
  float yaw = 0, steer = 0, steerIn = 0, throttle = 0, brake = 0, hp = 100, pitch = 0, roll = 0, lastVf = 0, burn = 0, smokeT = 0;
  bool hand = false;
  Col color;
  // traffic AI
  int ni = 0, nj = 0, d = 0, lane = 0, move = 0, nextMove = 0, dOut = 0, exitLane = 0;
  bool inside = false, planned = false;
  V2 path[6];
  int pn = 0, pi = 0;
  float cruise = 12, stuck = 0, ghost = 0;
};

struct Ped {
  bool alive = false;
  V2 pos, fleeDir, vel;
  float yaw = 0, hp = 40, t = 0, speed = 1.4f, s = 0, off = 2.5f, stT = 0;
  int st = 0;                               // 0 walk, 1 flee, 2 down
  int bi = 0, bj = 0, dirSign = 1;
  Col shirt, pants, skin;
};
struct Pickup { bool alive = false; V2 pos; int value = 100; float t = 0; };
struct Part { V3 p, v; float life = 0, max = 1, size = 1; Col c; uint8_t kind = 0; };   // 0 spark(add) 1 smoke 2 fire(add)
struct Tracer { V3 a, b; float life = 0; };
struct Note { std::string text; float t = 0; uint32_t col = 0xFFFFFF; };

struct Weapon { const char* name; int price, ammoPack, maxAmmo, dmg, pellets; float rate, spread, range; bool melee; };
extern const Weapon WEAP[4];
std::string fmtMoney(int v);

struct Player {
  V2 pos, vel;
  float yaw = 0, hp = 100, fireCd = 0, walk = 0, deadT = 0, muzzle = 0, hurtCd = 0;
  int money = 1000, weapon = 1, car = -1;
  bool owned[4] = {true, true, false, false};
  int ammo[4] = {0, 48, 0, 0};
};

struct Input {                               // one frame of control state
  float mx = 0, my = 0;                      // stick: my > 0 = forward / throttle
  bool fire = false, brake = false, use = false, swap = false;
  float camDX = 0, camDY = 0;                // camera drag in internal pixels
};
struct Touch { int id = -1, role = 0; float x = 0, y = 0, sx = 0, sy = 0, lx = 0, ly = 0; };

// Internal render size: fixed height (quality knob), width follows the screen aspect ratio.
inline void internalSize(int winW, int winH, int& w, int& h, int baseH = 360) {
  float aspect = winH > 0 ? (float)winW / (float)winH : 16.f / 9.f;
  h = baseH;
  w = (int)std::lround(h * aspect);
  w = std::max(baseH * 4 / 3, std::min(baseH * 5 / 2, w)) & ~3;
}

class Game {
 public:
  World world;
  Rng rng;
  int W = 640, H = 360;
  float time = 0, hour = 17.4f;
  std::vector<Vehicle> cars;
  std::vector<Ped> peds;
  std::vector<Pickup> picks;
  std::vector<Part> parts;
  std::vector<Tracer> tracers;
  std::vector<Note> notes;
  Player P;
  Input kb;                                  // keyboard / debug input (platform fills it)
  float camYaw = 0.3f, camPitch = 0.28f, fov = 1.05f;
  V3 camPos;
  bool timeFrozen = false, showTouch = true;

  void init(int w, int h, uint32_t seed = 1);
  void resize(int w, int h);
  void touch(int id, int phase, float x, float y);   // phase 0 down, 1 move, 2 up (internal pixel coords)
  void update(float dt);
  void draw(Renderer& R);

  // exposed for the headless tool
  int spawnVehicle(int type, V2 pos, float yaw, VState st);
  bool spawnAI(float minD, float maxD, bool hidden);
  int spawnParked(V2 near, float radius);
  bool spawnPed(bool hidden);
  void spawnPickup();
  void notify(const std::string& s, uint32_t col = 0xFFFFFF);

 private:
  Input in;
  Touch touches[6];
  bool useEdge = false, swapEdge = false;
  float spawnT = 0, trauma = 0, stickX = 0, stickY = 0, stickDX = 0, stickDY = 0, camAccX = 0, camAccY = 0;
  std::string useLabel = "USE";
  V3 camFocus;
  std::string prompt;
  int promptKind = 0;                        // 0 none, 1 enter, 2 sell, 3 shop

  void gatherInput();
  void updatePlayer(float dt);
  void updateVehicles(float dt);
  void stepVehicle(Vehicle& v, float dt);
  void aiDrive(Vehicle& v, int idx, float dt);
  void planPath(Vehicle& v);
  float obstacleLimit(const Vehicle& v, V2 fwd, V2 rt, float vf, int self);
  void collide(float dt);
  void updatePeds(float dt);
  void updateFX(float dt);
  void updateCamera(float dt);
  void manageSpawns(float dt);
  void firePlayer(float dt);
  bool hitScan(V2 o, V2 dir, float range, float dmg, bool fromPlayer, V3* endOut);
  void hurtPed(Ped& p, float dmg, V2 from);
  void damageCar(int idx, float dmg);
  void explode(int idx);
  void enterOrUse();
  void exitCar();
  int nearestCar(float reach);
  void emit(uint8_t kind, V3 p, V3 v, float life, float size, Col c);
  void killPlayer();

  void drawCar(Renderer& R, const Vehicle& v);
  void drawPerson(Renderer& R, V2 pos, float yaw, float phase, Col shirt, Col pants, Col skin, bool down, bool gun);
  void drawHUD(Renderer& R);
  void drawTouchUI(Renderer& R);
};
