#include "spawns.h"
#include "../dllmain.h"
#include "../MinHook.h"
#include <vector>

static std::vector<SnipeSpawnPoint*> injectedSpawnPoints = {};
static bool IsInjectedSpawnPoint(const SnipeSpawnPoint* sp)
{
    for (SnipeSpawnPoint* injected : injectedSpawnPoints)
        if (injected == sp)
            return true;
    return false;
}

#ifdef DEBUG_LOGGING
static void PrintSpawnPoint(const char* tag, const SnipeSpawnPoint* spawnPoint)
{
    printf("%s sp=0x%p uid=0x%08X inj=%d pos=(%.3f, %.3f, %.3f) posture=0x%X team=0x%X mode=0x%X\n",
        tag,
        spawnPoint,
        spawnPoint->uid,
        IsInjectedSpawnPoint(spawnPoint) ? 1 : 0,
        spawnPoint->positionX, spawnPoint->positionY, spawnPoint->positionZ,
        spawnPoint->posture,
        spawnPoint->teamMask,
        spawnPoint->gameModeMask);
}
#endif

double __cdecl SpawnPointScore_Detour(SnipeSpawnPoint* spawnPoint, void* actor)
{
    const double base = spawnPointScore(spawnPoint, actor);
    if (IsInjectedSpawnPoint(spawnPoint))
    {
        const double boosted = clamp(base * 1.5, 0.5, 1.0);
#ifdef DEBUG_LOGGING
        printf("[SPAWN_SCORE] uid=0x%08X injected base=%.6f boosted=%.6f\n", spawnPoint->uid, base, boosted);
#endif
        return boosted;
    }

#ifdef DEBUG_LOGGING
    //printf("[SPAWN_SCORE]============VANILLA============\n");
    //printf("[SPAWN_SCORE] vanilla base=%.6f\n", base);
    //PrintSpawnPoint("[SPAWN_SCORE]", spawnPoint);
    //printf("[SPAWN_SCORE]=============VANILLA END=============\n");
#endif
    return base;
}

const std::vector<spawnCoords> fuelDumpSpawns = {
    { 107.83f, -6.45f, -89.65f, russiaSpawn, crouch, 120.f },
    { 257.87f, 9.7f, -137.52f, germanySpawn, crouch, -60.f }
};
const std::vector<spawnCoords> ubahnSpawns = {
    { -55.64f, -15.5f, -71.16f, russiaSpawn, prone, 90.f },
    { 76.52f, -15.2f, -80.22f, germanySpawn, prone, -90.f },
    { -37.73f, -15.5f, 54.42f, russiaSpawn, prone, 180.f },
    { 40.64f, -15.71f, -39.19f, germanySpawn, prone, -90.f }
};
const std::vector<spawnCoords> tempelhofSpawns = {
    { 95.72f, -7.48f, 27.88f, russiaSpawn, stand, -90.f },
    { -98.89f, -19.11f, -40.56f, germanySpawn, prone, 60.f }
};

bool customSpawns = false;
static const std::vector<spawnCoords>* activeSpawns = nullptr;
static bool spawnsInjected = false;
static bool spawnInjectionPending = false;
static uint32_t asuraFileLoadDepth = 0;

static void InjectPendingSpawns()
{
    if (!spawnInjectionPending || !activeSpawns || spawnsInjected)
        return;

    spawnInjectionPending = false;

    for (const spawnCoords& entry : *activeSpawns)
    {
        SnipeSpawnPoint* newSpawn = static_cast<SnipeSpawnPoint*>(operatorNew(spawnPointSize));
        if (!newSpawn)
            continue;

        newSpawn = spawnPointDefaultCtor(newSpawn);
        if (!newSpawn)
            continue;

        newSpawn->positionX = entry.x;
        newSpawn->positionY = entry.y;
        newSpawn->positionZ = entry.z;
        const Asura_Vector_3 direction = SpawnDirectionFromDegrees(entry.yawDegrees, entry.pitchDegrees);
        newSpawn->directionX = direction.x;
        newSpawn->directionY = direction.y;
        newSpawn->directionZ = direction.z;
        newSpawn->posture = entry.posture;
        newSpawn->teamMask = entry.teamMask;
        newSpawn->gameModeMask = 0x18;
        injectedSpawnPoints.push_back(newSpawn);
    }

    spawnsInjected = true;
}

uint8_t __cdecl AsuraFileLoad_Detour(uint32_t fileHandle)
{
    ++asuraFileLoadDepth;
    const uint8_t result = asuraFileLoad(fileHandle);
    --asuraFileLoadDepth;

    if (result && asuraFileLoadDepth == 0)
        InjectPendingSpawns();

    return result;
}

void* __fastcall SpawnPointInit_Detour(SnipeSpawnPoint* self, void* /*edx*/, int a2, int** a3)
{
    if (customSpawns)
    {
        if (strcmp(curLevel, "01b.pc") == 0)
            activeSpawns = &fuelDumpSpawns;
        if (strcmp(curLevel, "04a.pc") == 0)
            activeSpawns = &ubahnSpawns;
        if (strcmp(curLevel, "08d.pc") == 0)
            activeSpawns = &tempelhofSpawns;
    }

    static bool isInHook = false;
    if (isInHook)
        return spawnPointInit(self, a2, a3);

    isInHook = true;

    void* sp = spawnPointInit(self, a2, a3);

    if (activeSpawns && !spawnsInjected)
        spawnInjectionPending = true;

    isInHook = false;
    return sp;
}

void* __fastcall SpawnPointErase_Detour(void* self, void* /*edx*/, uint8_t flags)
{
    if (spawnsInjected)
    {
        std::vector<SnipeSpawnPoint*> toDelete;
        toDelete.swap(injectedSpawnPoints);

        for (SnipeSpawnPoint* sp : toDelete)
        {
            if (!sp)
                continue;

            if (sp == self)
                continue;

            spawnPointErase(sp, 1);
        }

        activeSpawns = nullptr;
        spawnInjectionPending = false;
        spawnsInjected = false;
    }
    else if (spawnInjectionPending)
    {
        activeSpawns = nullptr;
        spawnInjectionPending = false;
    }

    return spawnPointErase(self, flags);
}

void hookSpawns()
{
    MH_CreateHook(reinterpret_cast<void*>(SpawnPointScoreAddr),
        SpawnPointScore_Detour, reinterpret_cast<void**>(&spawnPointScore));

    MH_CreateHook(reinterpret_cast<void*>(SpawnPointInitAddr),
        SpawnPointInit_Detour, reinterpret_cast<void**>(&spawnPointInit));

    MH_CreateHook(reinterpret_cast<void*>(AsuraFileLoadAddr),
        AsuraFileLoad_Detour, reinterpret_cast<void**>(&asuraFileLoad));

    MH_CreateHook(reinterpret_cast<void*>(SpawnPointEraseAddr),
        SpawnPointErase_Detour, reinterpret_cast<void**>(&spawnPointErase));
}
