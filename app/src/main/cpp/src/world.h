// NEON CITY - procedural downtown: 8x8 blocks, 9x9 signalised intersections
#pragma once
#include "render.h"

constexpr int NB = 8, NR = NB + 1;                       // blocks / roads per axis
constexpr float PITCH = 75.f, HALFR = 9.5f, BOUND = 322.f, SIDEW = 3.f, CURB = 0.18f, STOPD = 14.6f;
static const int DX[4] = {1, 0, -1, 0}, DZ[4] = {0, 1, 0, -1};   // 0=E(+x) 1=S(+z) 2=W 3=N
static const float LANE[2] = {2.0f, 5.4f};                         // lane centre offsets from the road axis (right-hand traffic)

inline float roadC(int k) { return float(k - NB / 2) * PITCH; }
inline V2 nodePos(int i, int j) { return {roadC(i), roadC(j)}; }
inline V2 dirOf(int d) { return {(float)DX[d], (float)DZ[d]}; }
inline V2 rightOf(int d) { return {-(float)DZ[d], (float)DX[d]}; }
inline float yawOf(int d) { return std::atan2((float)DX[d], (float)DZ[d]); }
inline bool nodeOk(int i, int j) { return i >= 0 && j >= 0 && i < NR && j < NR; }

struct Building { float x0, z0, x1, z1, h; Col wall, roof; uint32_t seed; int tiers; float setback; bool antenna; };
struct Obst { float x, z, r; uint8_t kind; };            // 0 = lamp post, 1 = tree
struct Lamp { V2 p, arm; };
struct Block {
  float x0 = 0, z0 = 0, x1 = 0, z1 = 0;
  uint8_t kind = 0;                                       // 0 buildings, 1 park, 2 garage plaza, 3 gun-shop plaza
  std::vector<Building> bld;
  std::vector<Obst> obs;
  std::vector<Lamp> lamps;
};

int sigState(int ni, int nj, int d, bool left, float time);   // 0 red, 1 yellow, 2 green
Sky makeSky(float hour, float time);                           // sun/moon, colours, fog for a time of day (0..24)

class World {
 public:
  Block blocks[NB][NB];
  V2 garage, gunShop, spawn;
  void gen(uint32_t seed);
  void draw(Renderer& R, float time) const;
  bool resolve(V2& p, float r, V2& n) const;              // push a circle out of buildings/obstacles; true if touched
  float rayWorld(V2 o, V2 dir, float maxT, bool withObs = true) const;   // distance to first building/obstacle (or maxT)
};
