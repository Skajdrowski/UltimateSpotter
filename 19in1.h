#pragma once
#include <stdint.h>

extern bool unlockMaps;
extern void listMaps();

constexpr uintptr_t MpMapTableBaseAddr = 0x757B28;
constexpr uint8_t MpMapRecordSize = 0x28;
constexpr uint8_t MpMapRecordCount = 34;
constexpr uint8_t MpMapRecordFlagsOffset = 0x20;