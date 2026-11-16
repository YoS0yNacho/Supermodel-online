// Experimental Scud Race Plus 16-player PPC patch (research build).
// SPDX-License-Identifier: GPL-3.0-or-later
// Optional 16-cabinet mode for the exact Export Rev A scudplus CROM; every
// patch site is validated (expected opcode + full fixed-CROM CRC32) before
// any write, so a mismatching ROM is refused instead of partially patched.
//
// What the ROM patch does (RAM pc = 0xFFF10000 + fixed-CROM offset):
//  * link boot caps (FFF61B4C/FFF61B60) accept cabinet index/total up to 16;
//  * remote-car import loop, link-lost autopilot fill and the stack log are
//    widened from 8 to 16 player cars; the car-order table moves to free ROM;
//  * the 0x40-byte CommRAM entry header only carries 8 roster ids, 8 table
//    bytes and an 8-bit mask, so the roster is extended: the own TX copy at
//    RAM 0x10F100 (ids) / 0x10F110 (table) / 0x10F120 (16-bit mask) is sent
//    in the +0x300 slot extension by a cave at FFFD1000, received by a cave
//    at FFFD1080 into 0x10F12F (count) / 0x10F130 / 0x10F140 / 0x10F150, and
//    every consumer of the old 8-entry arrays (0x10F027/0x10F028/0x10F03A)
//    is redirected there;
//  * the course-vote "already counted" byte mask becomes a 32-bit word.
// Generator with per-site notes: romtest/sp16/gen_patch.py (+ ppcasm.py).
#ifndef SCUD_SIXTEEN_PLAYER_RESEARCH_H
#define SCUD_SIXTEEN_PLAYER_RESEARCH_H
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace ScudResearch {
inline constexpr bool Enabled=true; // compiled capability, NOT runtime selection
inline constexpr unsigned Capacity=16, StockCapacity=8;
inline constexpr uint64_t Guid=0x5bf177da3487dULL, StockGuid=0x5bf177da34877ULL;
inline constexpr const char* Title="Scud Race Plus";
inline constexpr const char* SixteenTitle="Scud Race Plus - 16 Player";
template<class Config> inline bool Selected(const Config& config){return config["NetScudplusSixteen"].template ValueAsDefault<bool>(false);}
inline constexpr unsigned CabinetCapacity(bool sixteen){return sixteen?Capacity:StockCapacity;}
inline constexpr uint64_t LinkGuid(bool sixteen){return sixteen?Guid:StockGuid;}
// Native roster layout: the stock ROM keeps the human count at 0x10F027 and
// the owner map at 0x10F028 (8 bytes); the patched ROM mirrors the count to
// 0x10F12F and keeps the 16-entry owner map at 0x10F130.
inline constexpr uint32_t StockRosterIds=0x10f028u, WideRosterIds=0x10f130u;
inline constexpr uint32_t RosterIds(bool sixteen){return sixteen?WideRosterIds:StockRosterIds;}
}
namespace Scud { namespace SixteenPlayerPpcResearch {
enum class Result { Disabled, UnsupportedGame, WrongSize, UnexpectedOpcode, WrongCromCRC, Applied };
struct Site { uint32_t pc, expected, replacement; };
inline constexpr uint32_t FixedCromBase=0xffe00000u, FixedCromSize=0x200000u;
inline constexpr uint32_t CromCRC32 = 0x2906b2c7u;
inline constexpr const char* CromSHA256 = "2464c2f9328377642bc2c5839f98208e601aca4aec9d6260174f873d829cf2e6";
inline constexpr std::array<Site, 260> Sites = {{
  {0xfff61b4cu, 0x554006f9u, 0x554006f7u}, // own index-1: test only bit 0x10
  {0xfff61b60u, 0x554006f9u, 0x554006f7u}, // total-1: test only bit 0x10
  {0xfff607f8u, 0x3b200008u, 0x3b200010u}, // remote car import loop: 16 player cars
  {0xfff61180u, 0x3a400007u, 0x3a40000fu}, // link lost: 15 other player cars -> autopilot
  {0xfff4c3a0u, 0x2c0a0008u, 0x2c0a0010u}, // node index < 16
  {0xfff4c5a8u, 0x2c0a0008u, 0x2c0a0010u}, // node index < 16
  {0xfff4c790u, 0x2c0a0008u, 0x2c0a0010u}, // node index < 16
  {0xfff606d0u, 0x94f80004u, 0x60000000u}, // stack log
  {0xfff606d4u, 0x94d80004u, 0x60000000u}, // stack log
  {0xfff28838u, 0x3c800006u, 0x3c80fffdu}, // car order table hi
  {0xfff2883cu, 0x388418d0u, 0x38840000u}, // car order table lo
  {0xfffd0000u, 0xffffffffu, 0x00010203u},
  {0xfffd0004u, 0xffffffffu, 0x04050607u},
  {0xfffd0008u, 0xffffffffu, 0x08090a0bu},
  {0xfffd000cu, 0xffffffffu, 0x0c0d0e0fu},
  {0xfffd0010u, 0xffffffffu, 0x10111213u},
  {0xfffd0014u, 0xffffffffu, 0x14151617u},
  {0xfffd0018u, 0xffffffffu, 0x18191a1bu},
  {0xfffd001cu, 0xffffffffu, 0x1c1d1e1fu},
  {0xfffd0020u, 0xffffffffu, 0x20212223u},
  {0xfffd0024u, 0xffffffffu, 0x24252627u},
  {0xfffd0028u, 0xffffffffu, 0x28292a2bu},
  {0xfffd002cu, 0xffffffffu, 0x01000203u},
  {0xfffd0030u, 0xffffffffu, 0x04050607u},
  {0xfffd0034u, 0xffffffffu, 0x08090a0bu},
  {0xfffd0038u, 0xffffffffu, 0x0c0d0e0fu},
  {0xfffd003cu, 0xffffffffu, 0x10111213u},
  {0xfffd0040u, 0xffffffffu, 0x14151617u},
  {0xfffd0044u, 0xffffffffu, 0x18191a1bu},
  {0xfffd0048u, 0xffffffffu, 0x1c1d1e1fu},
  {0xfffd004cu, 0xffffffffu, 0x20212223u},
  {0xfffd0050u, 0xffffffffu, 0x24252627u},
  {0xfffd0054u, 0xffffffffu, 0x28292a2bu},
  {0xfffd0058u, 0xffffffffu, 0x02010003u},
  {0xfffd005cu, 0xffffffffu, 0x04050607u},
  {0xfffd0060u, 0xffffffffu, 0x08090a0bu},
  {0xfffd0064u, 0xffffffffu, 0x0c0d0e0fu},
  {0xfffd0068u, 0xffffffffu, 0x10111213u},
  {0xfffd006cu, 0xffffffffu, 0x14151617u},
  {0xfffd0070u, 0xffffffffu, 0x18191a1bu},
  {0xfffd0074u, 0xffffffffu, 0x1c1d1e1fu},
  {0xfffd0078u, 0xffffffffu, 0x20212223u},
  {0xfffd007cu, 0xffffffffu, 0x24252627u},
  {0xfffd0080u, 0xffffffffu, 0x28292a2bu},
  {0xfffd0084u, 0xffffffffu, 0x03010200u},
  {0xfffd0088u, 0xffffffffu, 0x04050607u},
  {0xfffd008cu, 0xffffffffu, 0x08090a0bu},
  {0xfffd0090u, 0xffffffffu, 0x0c0d0e0fu},
  {0xfffd0094u, 0xffffffffu, 0x10111213u},
  {0xfffd0098u, 0xffffffffu, 0x14151617u},
  {0xfffd009cu, 0xffffffffu, 0x18191a1bu},
  {0xfffd00a0u, 0xffffffffu, 0x1c1d1e1fu},
  {0xfffd00a4u, 0xffffffffu, 0x20212223u},
  {0xfffd00a8u, 0xffffffffu, 0x24252627u},
  {0xfffd00acu, 0xffffffffu, 0x28292a2bu},
  {0xfffd00b0u, 0xffffffffu, 0x04010203u},
  {0xfffd00b4u, 0xffffffffu, 0x00050607u},
  {0xfffd00b8u, 0xffffffffu, 0x08090a0bu},
  {0xfffd00bcu, 0xffffffffu, 0x0c0d0e0fu},
  {0xfffd00c0u, 0xffffffffu, 0x10111213u},
  {0xfffd00c4u, 0xffffffffu, 0x14151617u},
  {0xfffd00c8u, 0xffffffffu, 0x18191a1bu},
  {0xfffd00ccu, 0xffffffffu, 0x1c1d1e1fu},
  {0xfffd00d0u, 0xffffffffu, 0x20212223u},
  {0xfffd00d4u, 0xffffffffu, 0x24252627u},
  {0xfffd00d8u, 0xffffffffu, 0x28292a2bu},
  {0xfffd00dcu, 0xffffffffu, 0x05010203u},
  {0xfffd00e0u, 0xffffffffu, 0x04000607u},
  {0xfffd00e4u, 0xffffffffu, 0x08090a0bu},
  {0xfffd00e8u, 0xffffffffu, 0x0c0d0e0fu},
  {0xfffd00ecu, 0xffffffffu, 0x10111213u},
  {0xfffd00f0u, 0xffffffffu, 0x14151617u},
  {0xfffd00f4u, 0xffffffffu, 0x18191a1bu},
  {0xfffd00f8u, 0xffffffffu, 0x1c1d1e1fu},
  {0xfffd00fcu, 0xffffffffu, 0x20212223u},
  {0xfffd0100u, 0xffffffffu, 0x24252627u},
  {0xfffd0104u, 0xffffffffu, 0x28292a2bu},
  {0xfffd0108u, 0xffffffffu, 0x06010203u},
  {0xfffd010cu, 0xffffffffu, 0x04050007u},
  {0xfffd0110u, 0xffffffffu, 0x08090a0bu},
  {0xfffd0114u, 0xffffffffu, 0x0c0d0e0fu},
  {0xfffd0118u, 0xffffffffu, 0x10111213u},
  {0xfffd011cu, 0xffffffffu, 0x14151617u},
  {0xfffd0120u, 0xffffffffu, 0x18191a1bu},
  {0xfffd0124u, 0xffffffffu, 0x1c1d1e1fu},
  {0xfffd0128u, 0xffffffffu, 0x20212223u},
  {0xfffd012cu, 0xffffffffu, 0x24252627u},
  {0xfffd0130u, 0xffffffffu, 0x28292a2bu},
  {0xfffd0134u, 0xffffffffu, 0x07010203u},
  {0xfffd0138u, 0xffffffffu, 0x04050600u},
  {0xfffd013cu, 0xffffffffu, 0x08090a0bu},
  {0xfffd0140u, 0xffffffffu, 0x0c0d0e0fu},
  {0xfffd0144u, 0xffffffffu, 0x10111213u},
  {0xfffd0148u, 0xffffffffu, 0x14151617u},
  {0xfffd014cu, 0xffffffffu, 0x18191a1bu},
  {0xfffd0150u, 0xffffffffu, 0x1c1d1e1fu},
  {0xfffd0154u, 0xffffffffu, 0x20212223u},
  {0xfffd0158u, 0xffffffffu, 0x24252627u},
  {0xfffd015cu, 0xffffffffu, 0x28292a2bu},
  {0xfffd0160u, 0xffffffffu, 0x08010203u},
  {0xfffd0164u, 0xffffffffu, 0x04050607u},
  {0xfffd0168u, 0xffffffffu, 0x00090a0bu},
  {0xfffd016cu, 0xffffffffu, 0x0c0d0e0fu},
  {0xfffd0170u, 0xffffffffu, 0x10111213u},
  {0xfffd0174u, 0xffffffffu, 0x14151617u},
  {0xfffd0178u, 0xffffffffu, 0x18191a1bu},
  {0xfffd017cu, 0xffffffffu, 0x1c1d1e1fu},
  {0xfffd0180u, 0xffffffffu, 0x20212223u},
  {0xfffd0184u, 0xffffffffu, 0x24252627u},
  {0xfffd0188u, 0xffffffffu, 0x28292a2bu},
  {0xfffd018cu, 0xffffffffu, 0x09010203u},
  {0xfffd0190u, 0xffffffffu, 0x04050607u},
  {0xfffd0194u, 0xffffffffu, 0x08000a0bu},
  {0xfffd0198u, 0xffffffffu, 0x0c0d0e0fu},
  {0xfffd019cu, 0xffffffffu, 0x10111213u},
  {0xfffd01a0u, 0xffffffffu, 0x14151617u},
  {0xfffd01a4u, 0xffffffffu, 0x18191a1bu},
  {0xfffd01a8u, 0xffffffffu, 0x1c1d1e1fu},
  {0xfffd01acu, 0xffffffffu, 0x20212223u},
  {0xfffd01b0u, 0xffffffffu, 0x24252627u},
  {0xfffd01b4u, 0xffffffffu, 0x28292a2bu},
  {0xfffd01b8u, 0xffffffffu, 0x0a010203u},
  {0xfffd01bcu, 0xffffffffu, 0x04050607u},
  {0xfffd01c0u, 0xffffffffu, 0x0809000bu},
  {0xfffd01c4u, 0xffffffffu, 0x0c0d0e0fu},
  {0xfffd01c8u, 0xffffffffu, 0x10111213u},
  {0xfffd01ccu, 0xffffffffu, 0x14151617u},
  {0xfffd01d0u, 0xffffffffu, 0x18191a1bu},
  {0xfffd01d4u, 0xffffffffu, 0x1c1d1e1fu},
  {0xfffd01d8u, 0xffffffffu, 0x20212223u},
  {0xfffd01dcu, 0xffffffffu, 0x24252627u},
  {0xfffd01e0u, 0xffffffffu, 0x28292a2bu},
  {0xfffd01e4u, 0xffffffffu, 0x0b010203u},
  {0xfffd01e8u, 0xffffffffu, 0x04050607u},
  {0xfffd01ecu, 0xffffffffu, 0x08090a00u},
  {0xfffd01f0u, 0xffffffffu, 0x0c0d0e0fu},
  {0xfffd01f4u, 0xffffffffu, 0x10111213u},
  {0xfffd01f8u, 0xffffffffu, 0x14151617u},
  {0xfffd01fcu, 0xffffffffu, 0x18191a1bu},
  {0xfffd0200u, 0xffffffffu, 0x1c1d1e1fu},
  {0xfffd0204u, 0xffffffffu, 0x20212223u},
  {0xfffd0208u, 0xffffffffu, 0x24252627u},
  {0xfffd020cu, 0xffffffffu, 0x28292a2bu},
  {0xfffd0210u, 0xffffffffu, 0x0c010203u},
  {0xfffd0214u, 0xffffffffu, 0x04050607u},
  {0xfffd0218u, 0xffffffffu, 0x08090a0bu},
  {0xfffd021cu, 0xffffffffu, 0x000d0e0fu},
  {0xfffd0220u, 0xffffffffu, 0x10111213u},
  {0xfffd0224u, 0xffffffffu, 0x14151617u},
  {0xfffd0228u, 0xffffffffu, 0x18191a1bu},
  {0xfffd022cu, 0xffffffffu, 0x1c1d1e1fu},
  {0xfffd0230u, 0xffffffffu, 0x20212223u},
  {0xfffd0234u, 0xffffffffu, 0x24252627u},
  {0xfffd0238u, 0xffffffffu, 0x28292a2bu},
  {0xfffd023cu, 0xffffffffu, 0x0d010203u},
  {0xfffd0240u, 0xffffffffu, 0x04050607u},
  {0xfffd0244u, 0xffffffffu, 0x08090a0bu},
  {0xfffd0248u, 0xffffffffu, 0x0c000e0fu},
  {0xfffd024cu, 0xffffffffu, 0x10111213u},
  {0xfffd0250u, 0xffffffffu, 0x14151617u},
  {0xfffd0254u, 0xffffffffu, 0x18191a1bu},
  {0xfffd0258u, 0xffffffffu, 0x1c1d1e1fu},
  {0xfffd025cu, 0xffffffffu, 0x20212223u},
  {0xfffd0260u, 0xffffffffu, 0x24252627u},
  {0xfffd0264u, 0xffffffffu, 0x28292a2bu},
  {0xfffd0268u, 0xffffffffu, 0x0e010203u},
  {0xfffd026cu, 0xffffffffu, 0x04050607u},
  {0xfffd0270u, 0xffffffffu, 0x08090a0bu},
  {0xfffd0274u, 0xffffffffu, 0x0c0d000fu},
  {0xfffd0278u, 0xffffffffu, 0x10111213u},
  {0xfffd027cu, 0xffffffffu, 0x14151617u},
  {0xfffd0280u, 0xffffffffu, 0x18191a1bu},
  {0xfffd0284u, 0xffffffffu, 0x1c1d1e1fu},
  {0xfffd0288u, 0xffffffffu, 0x20212223u},
  {0xfffd028cu, 0xffffffffu, 0x24252627u},
  {0xfffd0290u, 0xffffffffu, 0x28292a2bu},
  {0xfffd0294u, 0xffffffffu, 0x0f010203u},
  {0xfffd0298u, 0xffffffffu, 0x04050607u},
  {0xfffd029cu, 0xffffffffu, 0x08090a0bu},
  {0xfffd02a0u, 0xffffffffu, 0x0c0d0e00u},
  {0xfffd02a4u, 0xffffffffu, 0x10111213u},
  {0xfffd02a8u, 0xffffffffu, 0x14151617u},
  {0xfffd02acu, 0xffffffffu, 0x18191a1bu},
  {0xfffd02b0u, 0xffffffffu, 0x1c1d1e1fu},
  {0xfffd02b4u, 0xffffffffu, 0x20212223u},
  {0xfffd02b8u, 0xffffffffu, 0x24252627u},
  {0xfffd02bcu, 0xffffffffu, 0x28292a2bu},
  {0xfff614dcu, 0x3bbe000cu, 0x3bad7100u}, // roster ids -> TX ext
  {0xfff60f84u, 0x3b7e0000u, 0x3b6d7110u}, // node table -> TX ext
  {0xfff60f38u, 0x987e0031u, 0xb06d7120u}, // roster mask 16-bit -> TX ext
  {0xfff60f44u, 0x3b0d7027u, 0x3b0d712fu}, // master build reads RX count/ids
  {0xfff60554u, 0x4bfb21a1u, 0x4bf80aadu}, // send: header + ext
  {0xfff60558u, 0x90df003cu, 0x60000000u}, // checksum stored in cave
  {0xfff60dbcu, 0x3b9a000bu, 0x4bf802c5u}, // recv roster ext
  {0xfff60dc0u, 0x3b6d7027u, 0x60000000u},
  {0xfff60dc4u, 0x8bfc0000u, 0x60000000u},
  {0xfff60dc8u, 0x83dc0001u, 0x60000000u},
  {0xfff60dccu, 0x83bc0005u, 0x60000000u},
  {0xfff60dd0u, 0x9bfb0000u, 0x60000000u},
  {0xfff60dd4u, 0x93db0001u, 0x60000000u},
  {0xfff60dd8u, 0x93bb0005u, 0x60000000u},
  {0xfff60ddcu, 0x3bba0000u, 0x60000000u},
  {0xfff60de0u, 0x3b8d703au, 0x60000000u},
  {0xfff60de4u, 0x83fd0000u, 0x60000000u},
  {0xfff60de8u, 0x83dd0004u, 0x60000000u},
  {0xfff60decu, 0x93fc0000u, 0x60000000u},
  {0xfff60df0u, 0x93dc0004u, 0x60000000u},
  {0xfff60e04u, 0x8bba0031u, 0xa3ad7150u}, // mask from RX ext
  {0xfff60e78u, 0x3b7b0001u, 0x3b6d7130u}, // own-index search walks RX ids
  {0xfffd1000u, 0xffffffffu, 0x7c0802a6u}, // cave
  {0xfffd1004u, 0xffffffffu, 0x3821fff0u}, // cave
  {0xfffd1008u, 0xffffffffu, 0x90010000u}, // cave
  {0xfffd100cu, 0xffffffffu, 0x480316e9u}, // cave
  {0xfffd1010u, 0xffffffffu, 0x90df003cu}, // cave
  {0xfffd1014u, 0xffffffffu, 0x880d7a9bu}, // cave
  {0xfffd1018u, 0xffffffffu, 0x980d7122u}, // cave
  {0xfffd101cu, 0xffffffffu, 0x387f0300u}, // cave
  {0xfffd1020u, 0xffffffffu, 0x388d7100u}, // cave
  {0xfffd1024u, 0xffffffffu, 0x38a00009u}, // cave
  {0xfffd1028u, 0xffffffffu, 0x480316cdu}, // cave
  {0xfffd102cu, 0xffffffffu, 0x80010000u}, // cave
  {0xfffd1030u, 0xffffffffu, 0x7c0803a6u}, // cave
  {0xfffd1034u, 0xffffffffu, 0x38210010u}, // cave
  {0xfffd1038u, 0xffffffffu, 0x4e800020u}, // cave
  {0xfffd1080u, 0xffffffffu, 0x7c0802a6u}, // cave
  {0xfffd1084u, 0xffffffffu, 0x3821fff0u}, // cave
  {0xfffd1088u, 0xffffffffu, 0x90010000u}, // cave
  {0xfffd108cu, 0xffffffffu, 0x3b8d7ad0u}, // cave
  {0xfffd1090u, 0xffffffffu, 0x7f9cd050u}, // cave
  {0xfffd1094u, 0xffffffffu, 0x579cd1beu}, // cave
  {0xfffd1098u, 0xffffffffu, 0x1f9c0400u}, // cave
  {0xfffd109cu, 0xffffffffu, 0x3c9cc000u}, // cave
  {0xfffd10a0u, 0xffffffffu, 0x38840804u}, // cave
  {0xfffd10a4u, 0xffffffffu, 0x386d7130u}, // cave
  {0xfffd10a8u, 0xffffffffu, 0x38a00009u}, // cave
  {0xfffd10acu, 0xffffffffu, 0x48031649u}, // cave
  {0xfffd10b0u, 0xffffffffu, 0x8bed7152u}, // cave
  {0xfffd10b4u, 0xffffffffu, 0x9bed7027u}, // cave
  {0xfffd10b8u, 0xffffffffu, 0x9bed712fu}, // cave
  {0xfffd10bcu, 0xffffffffu, 0x80010000u}, // cave
  {0xfffd10c0u, 0xffffffffu, 0x7c0803a6u}, // cave
  {0xfffd10c4u, 0xffffffffu, 0x38210010u}, // cave
  {0xfffd10c8u, 0xffffffffu, 0x4e800020u}, // cave
  {0xfff28084u, 0x394d7027u, 0x394d712fu}, // count+ids walker
  {0xfff28148u, 0x394d7027u, 0x394d712fu}, // count+ids walker
  {0xfff2893cu, 0x394d7027u, 0x394d712fu}, // count+ids walker
  {0xfff60e70u, 0x3b6d7027u, 0x3b6d712fu}, // count+ids walker
  {0xfff288bcu, 0x886d7028u, 0x886d7130u},
  {0xfff39f88u, 0x888d7028u, 0x888d7130u},
  {0xfff60450u, 0x8bed7028u, 0x8bed7130u},
  {0xfff6082cu, 0x3b6d7028u, 0x3b6d7130u},
  {0xfff60b9cu, 0x3b2d7028u, 0x3b2d7130u},
  {0xfff61168u, 0x8bcd7028u, 0x8bcd7130u},
  {0xfff61174u, 0x9bed7028u, 0x9bed7130u},
  {0xfff61008u, 0x3b6d703au, 0x3b6d7140u}, // slave table read
  {0xfff27eecu, 0x38ea000bu, 0x38ed712fu}, // course vote tally walks RX count+ids
  {0xfff28010u, 0x392a000bu, 0x392d712fu}, // all-selected check walks RX count+ids
  {0xfff27ad0u, 0x986dcf48u, 0x906d7158u}, // voted mask reset (32-bit)
  {0xfff27f20u, 0x888dcf48u, 0x808d7158u}, // voted mask load (32-bit)
  {0xfff27f34u, 0x988dcf48u, 0x908d7158u}, // voted mask store (32-bit)
}};
inline uint32_t ReadBE(const uint8_t* p) {
  return (uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|p[3];
}
inline void WriteBE(uint8_t* p, uint32_t word) {
  p[0]=uint8_t(word>>24);p[1]=uint8_t(word>>16);p[2]=uint8_t(word>>8);p[3]=uint8_t(word);
}
inline uint32_t CRC32(const uint8_t* bytes, size_t size) {
  const auto table=[] {
    std::array<uint32_t,256> t{};
    for(uint32_t i=0;i<256;++i) {
      uint32_t v=i;
      for(unsigned bit=0;bit<8;++bit)v=(v>>1)^((v&1)?0xedb88320u:0u);
      t[i]=v;
    }
    return t;
  }();
  uint32_t crc=0xffffffffu;
  for(size_t i=0;i<size;++i)crc=(crc>>8)^table[(crc^bytes[i])&255];
  return ~crc;
}
// `bytes` is the 2 MB fixed CROM (big-endian, exactly as loaded before FlipEndian32).
inline Result ApplyBigEndianCrom(uint8_t* bytes, size_t size, const char* game,
                                bool explicitlyEnabled=false) {
  if(!explicitlyEnabled)return Result::Disabled;
  if(!game||std::strcmp(game,"scudplus"))return Result::UnsupportedGame;
  if(!bytes||size!=FixedCromSize)return Result::WrongSize;
  // Validate all patch sites before writing, so mismatches cannot partially patch ROM.
  bool patched=true;
  for(const auto& site:Sites) {
    if(site.pc<FixedCromBase||site.pc-FixedCromBase+4>FixedCromSize)return Result::UnexpectedOpcode;
    patched=patched && ReadBE(bytes+(site.pc-FixedCromBase))==site.replacement;
  }
  if(patched)return Result::Applied; // the loader may run twice on the same image
  for(const auto& site:Sites)
    if(ReadBE(bytes+(site.pc-FixedCromBase))!=site.expected)return Result::UnexpectedOpcode;
  if(CRC32(bytes,size)!=CromCRC32)return Result::WrongCromCRC;
  for(const auto& site:Sites)WriteBE(bytes+(site.pc-FixedCromBase),site.replacement);
  return Result::Applied;
}
}} // namespace Scud::SixteenPlayerPpcResearch
#endif
