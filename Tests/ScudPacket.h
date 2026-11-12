// Synthetic, ROM-free Scud / Scud Plus packets matching ROM 0xFFF5B080..0xFFF5B8AC.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INCLUDED_TEST_SCUD_PACKET_H
#define INCLUDED_TEST_SCUD_PACKET_H
#include "Network/Games/Scud/Prediction.h"

inline void ScudChecksum(std::array<uint8_t, Scud::PacketSize>& p) {
  uint32_t sum = 0;
  for (unsigned i = 4; i < 0x40; i += 4)
    sum += Scud::Read32(p.data(), i);
  Scud::Write32(p.data(), 0x40, sum);
}

// ROM 0xFFF5B690..0xFFF5B7B4 assigns human car ids 0..humans-1 with coordinator
// `authority` at id 0 (`players[0] = authority`) and remaining cabinets in order.
inline Scud::Routing ScudRoute(unsigned local, unsigned machines = 2,
                               unsigned humans = 0, unsigned authority = 1) {
  if (humans == 0) humans = machines;
  Scud::Routing r;
  r.local = local;
  r.machines = machines;
  r.humans = uint8_t(humans);
  r.authority = uint8_t(authority);
  r.total = 40;
  r.valid = true;
  unsigned idx = 0;
  if (idx < humans)
    r.players[idx++] = uint8_t(authority);
  for (unsigned node = 1; node <= machines && idx < humans; ++node) {
    if (node == authority) continue;
    r.players[idx++] = uint8_t(node);
  }
  while (idx < humans) {
    r.players[idx++] = uint8_t(local);
  }
  for (unsigned i = humans; i < 40; ++i)
    r.cpu[i] = true;
  return r;
}

inline unsigned ScudCarIdForOwner(const Scud::Routing& r, unsigned owner) {
  for (unsigned i = 0; i < r.humans; ++i)
    if (r.players[i] == owner) return i;
  return 0;
}

// Every participating cabinet stamps 0x1C with `authority` (`players[0]`, ROM
// 0xFFF5B7D0 / 0xFFF5B8AC) and 0x48 with its car id `0..humans-1`.
inline std::array<uint8_t, Scud::PacketSize> ScudPacket(
    unsigned owner, unsigned frame, unsigned humans = 2, unsigned authority = 1) {
  const auto route = ScudRoute(owner, std::max(humans, std::max(owner, authority)), humans, authority);
  const unsigned carId = ScudCarIdForOwner(route, owner);
  std::array<uint8_t, Scud::PacketSize> p{};
  p[0x1f ^ 2] = p[0x21 ^ 2] = 21;
  p[0x20 ^ 2] = 2;
  p[0x1c ^ 2] = uint8_t(authority);
  p[0x0f ^ 2] = uint8_t(humans);
  Scud::Write32(p.data(), 0x38, frame);
  ScudChecksum(p);
  Scud::Write32(p.data(), 0x44, 0x1a01);
  p[0x48 ^ 2] = uint8_t(carId);
  Scud::WriteFloat(p.data(), 0x60, float(owner * 100 + frame * 0.05));
  Scud::WriteFloat(p.data(), 0x64, 20.0f);
  Scud::WriteFloat(p.data(), 0x68, float(owner * 20));
  for (unsigned i = humans; i < 40; ++i) {
    Scud::Write16(p.data(), 0x124 + 4 * i, 0x841);
    Scud::Write16(p.data(), 0x126 + 4 * i, uint16_t(100 + i + frame / 3));
  }
  return p;
}
#endif
