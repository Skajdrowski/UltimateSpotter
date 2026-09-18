#pragma once
#include "Hooks/misc.h"
#include "Hooks/players.h"
#include "Hooks/inventories.h"

//#define DEBUG_LOGGING

constexpr uintptr_t ChatCooldownAddr = 0x4832D0;
constexpr uintptr_t DirectInputAddr = 0x40AC10;
constexpr uintptr_t PlayerFetchAddr = 0x6080C0;
constexpr uintptr_t IPJoinSendAddr = 0x446790;
constexpr uintptr_t PlayerConstructorAddr = 0x56F390;
constexpr uintptr_t PlayerGetCoordsAddr = 0x533880;
constexpr uintptr_t InventoryAssignAddr = 0x519C90;
constexpr uintptr_t AutoBalanceUpdateAddr = 0x617E20;
constexpr uintptr_t LoadingFlagAddr = 0x7A35A9;
constexpr uintptr_t SpawnPointScoreAddr = 0x61D500;
constexpr uintptr_t SpawnPointInitAddr = 0x591A40;
constexpr uintptr_t SpawnPointEraseAddr = 0x591D20;
constexpr uintptr_t SpawnPointEligibleAddr = 0x591700;
constexpr uintptr_t LobbyTemplateCopyAddr = 0x4B4490;
constexpr uintptr_t ScorePacketBuilderAddr = 0x4A8560;
constexpr uintptr_t ScoreCursorAddr = 0x7AE790;
constexpr uintptr_t ScoreValidSlotsAddr = 0x7AE610;

inline wchar_t iniPath[260]{};
inline bool isHost;
inline const char* curLevel = nullptr;