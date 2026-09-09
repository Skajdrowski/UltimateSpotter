#pragma once
#include <cstdint>
#include <cmath>

static constexpr double clamp(double v, double lo, double hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

struct SnipeSpawnPoint
{
    uint32_t vtable;
    uint32_t uid;
    uint16_t classification = 0x803A;
    uint16_t entityPadding;
    uint32_t entityFlags;
    float positionZ;
    float positionY;
    float positionX;
    float directionZ;
    float directionY;
    float directionX;
    int32_t spawnIndex;
    uint32_t posture;
    uint32_t teamMask;
    uint32_t gameModeMask;
    float cooldown = 5.f;
};

constexpr size_t spawnPointSize = sizeof(SnipeSpawnPoint);

using SpawnPointScoreFn = double(__cdecl*)(SnipeSpawnPoint*, void*);
static SpawnPointScoreFn spawnPointScore = nullptr;
double __cdecl SpawnPointScore_Detour(SnipeSpawnPoint* spawnPoint, void* actor);

struct spawnCoords
{
    float x;
    float y;
    float z;
    uint32_t teamMask;
    uint32_t posture;
    float yawDegrees = 0.0f;
    float pitchDegrees = 0.0f;
};

struct Asura_Vector_3
{
    float x, y, z;
};

static inline Asura_Vector_3 SpawnDirectionFromDegrees(float yawDegrees, float pitchDegrees)
{
    constexpr float degreesToRadians = 3.14159265358979323846f / 180.0f;
    const float pitch = pitchDegrees * degreesToRadians;
    const float yaw = yawDegrees * degreesToRadians;
    const float cosPitch = cosf(pitch);

    return { cosPitch * sinf(yaw), sinf(pitch), cosPitch * cosf(yaw) };
};

enum posture
{
    stand = 0x0,
    crouch = 0x1,
    prone = 0x2
};

enum teamSpawnMask
{
    germanySpawn = 0x3,
    russiaSpawn = 0x5
};

constexpr uintptr_t OperatorNewAddr = 0x6A6322;
using OperatorNewFn = void* (__cdecl*)(size_t);
static OperatorNewFn operatorNew = reinterpret_cast<OperatorNewFn>(OperatorNewAddr);

constexpr uintptr_t SpawnPointDefaultCtorAddr = 0x5919A0;
using SpawnPointDefaultCtorFn = SnipeSpawnPoint* (__thiscall*)(SnipeSpawnPoint*);
static SpawnPointDefaultCtorFn spawnPointDefaultCtor =
    reinterpret_cast<SpawnPointDefaultCtorFn>(SpawnPointDefaultCtorAddr);

using SpawnPointInitFn = void* (__thiscall*)(SnipeSpawnPoint*, int, int**);
static SpawnPointInitFn spawnPointInit = nullptr;
void* __fastcall SpawnPointInit_Detour(SnipeSpawnPoint* self, void* /*edx*/, int a2, int** a3);
extern bool customSpawns;

constexpr uintptr_t AsuraFileLoadAddr = 0x43F250;
using AsuraFileLoadFn = uint8_t(__cdecl*)(uint32_t);
static AsuraFileLoadFn asuraFileLoad = nullptr;
uint8_t __cdecl AsuraFileLoad_Detour(uint32_t fileHandle);

using SpawnPointEraseFn = void* (__thiscall*)(void*, uint8_t);
static SpawnPointEraseFn spawnPointErase = nullptr;
void* __fastcall SpawnPointErase_Detour(void* self, void* /*edx*/, uint8_t flags);

extern void hookSpawns();
