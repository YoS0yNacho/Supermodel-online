// Read-only native RX copy-context validator. Not packet freshness.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INCLUDED_SGT24H_COPY_CONTEXT_H
#define INCLUDED_SGT24H_COPY_CONTEXT_H
#include "Profile.h"
namespace Sgt24h {
constexpr uint32_t RxCopyReturnPC=0xb9678;
constexpr uint32_t RxCopyFinalLoadPC=CopyCodeRAM+(0xbb720-CopyCodeROM);
// Observes the final word read of the last ldq, not copy completion.
// Caller must separately gate ROM revision, CPU, linked session and scene.
inline bool MatchRxCopy(unsigned local,unsigned count,uint32_t pc,uint32_t rip,
                        uint32_t source,uint32_t destination,uint32_t address,
                        uint32_t mask,unsigned& seat) {
 if(count<2 || count>Cabinets || local>=count || pc!=RxCopyFinalLoadPC ||
    rip!=RxCopyReturnPC || mask!=0xffffffffu)return false;
 constexpr uint32_t first=0x1a12380;
 if(source<first)return false;
 const uint32_t offset=source-first;
 if(offset%RowBytes || offset/RowBytes>=count)return false;
 const unsigned candidate=RowSeat(local,offset/RowBytes,count);
 if(destination!=RxActors+candidate*CabinetRecordStride || address!=source+RowBytes-4)return false;
 seat=candidate;return true;
}
}
#endif
