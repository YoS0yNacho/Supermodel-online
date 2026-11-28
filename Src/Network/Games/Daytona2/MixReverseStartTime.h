// Daytona USA 2 Power Edition: add time to the initial MIX Reverse segment only.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INCLUDED_DAYTONA2_MIX_REVERSE_START_TIME_H
#define INCLUDED_DAYTONA2_MIX_REVERSE_START_TIME_H
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Daytona2MixReverseStartTime {
// The game stores the active segment's remaining time in frames at 0x105010 and
// counts elapsed frames at 0x10500C. One frame is 1/57.52416 s; 3,452 frames
// therefore add just over 60 seconds. Checkpoint bonuses use a separate game
// path and are deliberately left untouched.
inline constexpr uint32_t RemainingAddress = 0x00105010;
inline constexpr uint32_t ElapsedAddress = 0x0010500C;
inline constexpr uint32_t CourseAddress = 0x00105FC4;
inline constexpr uint32_t RaceModeAddress = 0x001001A3;
inline constexpr uint32_t ReverseAddress = 0x00106214;
inline constexpr uint32_t ExtraFrames = 3452;
inline constexpr uint32_t InitialRemainingMin = 3000;
inline constexpr uint32_t InitialRemainingMax = 4000;
inline constexpr uint32_t InitialElapsedMax = 2;

enum class Result { WrongGame, NotReady, NotEligible, NotInitialTime, Applied };

inline uint32_t Read32(const uint8_t* ram, uint32_t address)
{
  uint32_t value;
  std::memcpy(&value, ram + address, sizeof(value));
  return value;
}

inline uint8_t Read8(const uint8_t* ram, uint32_t address)
{
  // Model 3 RAM is word-swapped in the host buffer; byte addresses are XOR 3.
  return ram[address ^ 3];
}

inline void Write32(uint8_t* ram, uint32_t address, uint32_t value)
{
  std::memcpy(ram + address, &value, sizeof(value));
}

inline Result Apply(const char* game, uint8_t* ram, size_t size)
{
  if (!game || std::strcmp(game, "dayto2pe")) return Result::WrongGame;
  if (!ram || size < ReverseAddress + sizeof(uint32_t)) return Result::NotReady;

  // MIX is course 0; Reverse is latched by car-select at 0x106214. GP/Endurance
  // use a nonzero mode byte and must retain their native restrictions.
  if (Read8(ram, CourseAddress) != 0 || Read8(ram, RaceModeAddress) != 0 ||
      Read32(ram, ReverseAddress) != 1)
    return Result::NotEligible;

  // RunMainBoardFrame calls this each frame. Only the initial timer window is
  // eligible; the elapsed-frame guard prevents touching later checkpoints.
  const uint32_t elapsed = Read32(ram, ElapsedAddress);
  const uint32_t remaining = Read32(ram, RemainingAddress);
  if (elapsed > InitialElapsedMax || remaining < InitialRemainingMin ||
      remaining > InitialRemainingMax || remaining > UINT32_MAX - ExtraFrames)
    return Result::NotInitialTime;

  Write32(ram, RemainingAddress, remaining + ExtraFrames);
  return Result::Applied;
}
}
#endif
