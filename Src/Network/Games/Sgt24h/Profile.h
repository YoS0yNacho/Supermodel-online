// Super GT 24h verified geometry; experimental online integration.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INCLUDED_SGT24H_PROFILE_H
#define INCLUDED_SGT24H_PROFILE_H
#include <cstdint>
namespace Sgt24h {
constexpr uint32_t ProgramCRC=0x22d76fe3;
constexpr unsigned ProgramBytes=0x100000;
// Service handlers 0xbbc90 / 0xbbd20: zero-based IDs 0..3, link max 2..4.
constexpr unsigned Cabinets=4;
// 0xb80b4..0xb80c4: frame offset = 7<<7; frame size = 7<<9.
constexpr unsigned FrameBytes=0xe00, RowBytes=0x380;
constexpr unsigned Tx=0x2000, Rx=Tx+RowBytes;
// 0xb9614: native RX copy rows rotate beginning with the next cabinet.
// These are CABINET records; do not confuse them with individual cars.
constexpr unsigned CabinetRecordStride=0x47c;
constexpr unsigned ActorStride=CabinetRecordStride; // historical alias
// Three repeated state areas in each actual car actor. Verified by the
// initialization copies at 0x25f60 and serializer/deserializer offsets.
constexpr uint32_t CarActorBase=0x517c50;
constexpr unsigned CarActorStride=0x1728, CarStateBytes=0x7b8;
// Linked initialization 0x25130..0x251e4 assigns these roles; other modes
// can override them. A type value alone is NOT a race/ownership validator.
constexpr unsigned LocalActorType=0, LinkedPeerActorType=1, CompactActorType=2;
constexpr uint32_t LocalActorIndex=0x510e60, SelectedCabinet=0x510dd0;
constexpr unsigned BaseState=0, TransmittedState=CarStateBytes;
constexpr unsigned ReceivedState=2*CarStateBytes, StatePosition=0x28;
// Full decoder saves its slot selector in transmitted metadata. The frame
// loop later copies it to received metadata. Not a freshness/ownership gate.
constexpr unsigned StateCabinetSlot=0x7b4;
constexpr unsigned DecodedCabinetSlot=TransmittedState+StateCabinetSlot;
constexpr unsigned ReceivedCabinetSlot=ReceivedState+StateCabinetSlot;
static_assert(DecodedCabinetSlot==0xf6c && ReceivedCabinetSlot==0x1724);
static_assert(CarActorStride==3*CarStateBytes);
// 0x56590 uploads the plain 0x380-byte copier, not a pose codec.
constexpr uint32_t CopyCodeROM=0xbb550, CopyCodeRAM=0x21be00;
constexpr unsigned CopiedActorBytes=0x380;
// Zero-based native RX rotation: next seat first, own echo last.
constexpr unsigned RowSeat(unsigned local,unsigned row,unsigned count) {
 return count>=2 && count<=Cabinets && local<count && row<count
        ? (local+row+1)%count : Cabinets;
}
constexpr uint32_t TxActors=0x511230, RxActors=0x512420;
// Runtime settings copied at 0x71940; these are RAM, NOT EEPROM offsets.
constexpr uint32_t LinkDisabled=0x510958, Seat=0x51095c;
constexpr uint32_t Master=0x510dc4, LinkCount=0x510dc8;
// Return sentinel 0 for invalid geometry; no access to emulator memory.
constexpr unsigned EchoOffset(unsigned count) {
 return count>=2 && count<=Cabinets ? Rx+(count-1)*RowBytes : 0;
}
}
#endif
