// Experimental Sega Rally 2 Export PPC patch installer.
// SPDX-License-Identifier: GPL-3.0-or-later
// Optional 8-player mode; exact-ROM checks validate all patch sites before writes.
#ifndef SRALLY2_EIGHT_PLAYER_PPC_RESEARCH_H
#define SRALLY2_EIGHT_PLAYER_PPC_RESEARCH_H
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace Srally2 { namespace EightPlayerPpcResearch {
enum class Result { Disabled, UnsupportedGame, WrongSize, UnexpectedOpcode, WrongCromCRC, Applied };
struct Site { uint32_t pc, expected, replacement; };
inline constexpr uint32_t CromCRC32 = 0xfabe41a9u;
// CROM SHA256 identity (metadata; the runtime guard below uses full CRC32 + every expected opcode).
inline constexpr const char* CromSHA256 = "2d88694b427c1014389949695f67b3ad7aac066649a8954ef879698cb4f512d7";
inline constexpr std::array<Site, 122> Sites = {{
  {0xff8055b4u, 0x81270080u, 0x89270080u}, // local selected-ID load: lwz -> lbz
  {0xff8186c0u, 0x816a0080u, 0x896a0080u}, // local selected-ID load: lwz -> lbz
  {0xff82aa2cu, 0x816a0080u, 0x896a0080u}, // local selected-ID load: lwz -> lbz
  {0xff8389c0u, 0x813e0080u, 0x893e0080u}, // local selected-ID load: lwz -> lbz
  {0xff8389ecu, 0x813e0080u, 0x893e0080u}, // local selected-ID load: lwz -> lbz
  {0xff83eb68u, 0x812a0080u, 0x892a0080u}, // local selected-ID load: lwz -> lbz
  {0xff83ec7cu, 0x812a0080u, 0x892a0080u}, // local selected-ID load: lwz -> lbz
  {0xff83fca0u, 0x812a0080u, 0x892a0080u}, // local selected-ID load: lwz -> lbz
  {0xff8403e8u, 0x812a0080u, 0x892a0080u}, // local selected-ID load: lwz -> lbz
  {0xff84c810u, 0x80be0080u, 0x88be0080u}, // local selected-ID load: lwz -> lbz
  {0xff8055f4u, 0x38e70004u, 0x38e70001u}, // local selected-list cursor: stride4 -> stride1
  {0xff8186f8u, 0x394a0004u, 0x394a0001u}, // local selected-list cursor: stride4 -> stride1
  {0xff82aa54u, 0x394a0004u, 0x394a0001u}, // local selected-list cursor: stride4 -> stride1
  {0xff838a1cu, 0x3bde0004u, 0x3bde0001u}, // local selected-list cursor: stride4 -> stride1
  {0xff83eb94u, 0x394a0004u, 0x394a0001u}, // local selected-list cursor: stride4 -> stride1
  {0xff83eca8u, 0x394a0004u, 0x394a0001u}, // local selected-list cursor: stride4 -> stride1
  {0xff83fcbcu, 0x394a0004u, 0x394a0001u}, // local selected-list cursor: stride4 -> stride1
  {0xff840410u, 0x394a0004u, 0x394a0001u}, // local selected-list cursor: stride4 -> stride1
  {0xff84c830u, 0x3bde0004u, 0x3bde0001u}, // local selected-list cursor: stride4 -> stride1
  {0xff84f1ccu, 0x39080004u, 0x39080001u}, // local selected-list cursor: stride4 -> stride1
  {0xff84f1acu, 0x91280080u, 0x99280080u}, // local selected-ID store: stw -> stb
  {0xff84f050u, 0x90090030u, 0x98090030u}, // TX first selected-ID store: stw -> stb
  {0xff84f070u, 0x38890004u, 0x38890001u}, // TX cursor starts at selected-list byte1
  {0xff84f0fcu, 0x90040030u, 0x98040030u}, // TX further selected-ID store: stw -> stb
  {0xff84f100u, 0x38840004u, 0x38840001u}, // TX selected-list cursor stride4 -> stride1
  {0xff84f198u, 0x5549103au, 0x7d495378u}, // RX list index: mr r9,r10 instead of index*4
  {0xff84f1a8u, 0x81290030u, 0x89290030u}, // RX selected-ID load: lwz -> lbz
  {0xff801f70u, 0x2c800005u, 0x2c800009u}, // Live Monitor comparison: reserve9, free role5
  {0xff804e44u, 0x2c800005u, 0x2c800009u}, // Live Monitor comparison: reserve9, free role5
  {0xff80d8d8u, 0x2c800005u, 0x2c800009u}, // Live Monitor comparison: reserve9, free role5
  {0xff80e8bcu, 0x2c800005u, 0x2c800009u}, // Live Monitor comparison: reserve9, free role5
  {0xff80feacu, 0x2c800005u, 0x2c800009u}, // Live Monitor comparison: reserve9, free role5
  {0xff838cd8u, 0x2c8b0005u, 0x2c8b0009u}, // Live Monitor comparison: reserve9, free role5
  {0xff838d80u, 0x2c8b0005u, 0x2c8b0009u}, // Live Monitor comparison: reserve9, free role5
  {0xff84422cu, 0x2c800005u, 0x2c800009u}, // Live Monitor comparison: reserve9, free role5
  {0xff844f28u, 0x2c800005u, 0x2c800009u}, // Live Monitor comparison: reserve9, free role5
  {0xff849dd4u, 0x2c800005u, 0x2c800009u}, // Live Monitor comparison: reserve9, free role5
  {0xff84a000u, 0x2c800005u, 0x2c800009u}, // Live Monitor comparison: reserve9, free role5
  {0xff87913cu, 0x2c800005u, 0x2c800009u}, // Live Monitor comparison: reserve9, free role5
  {0xff879b24u, 0x2c8b0005u, 0x2c8b0009u}, // Live Monitor comparison: reserve9, free role5
  {0xff8403f8u, 0x5529103au, 0x7d294b78u}, // race-order TX index: no word scaling
  {0xff840400u, 0x91690058u, 0x99690058u}, // race-order TX: stw -> stb
  {0xff8186d0u, 0x5569103au, 0x7d695b78u}, // race-order UI RX index: no word scaling
  {0xff8186e0u, 0x80090058u, 0x88090058u}, // race-order UI RX: lwz -> lbz
  {0xff82aa30u, 0x5569103au, 0x7d695b78u}, // race-order lookup RX index: no word scaling
  {0xff82aa38u, 0x80090058u, 0x88090058u}, // race-order lookup RX: lwz -> lbz
  {0xff82b1a4u, 0x5529103au, 0x7d294b78u}, // race-order local-player RX index: no word scaling
  {0xff82b1acu, 0x80090058u, 0x88090058u}, // race-order local-player RX: lwz -> lbz
  {0xff82b208u, 0x5529103au, 0x7d294b78u}, // race-order local-player text RX index: no word scaling
  {0xff82b210u, 0x80c90058u, 0x88c90058u}, // race-order local-player text RX: lwz -> lbz
  {0xff84adccu, 0x5529103au, 0x7d294b78u}, // race-order diagnostic RX index: no word scaling
  {0xff84add4u, 0x80c90058u, 0x88c90058u}, // race-order diagnostic RX: lwz -> lbz
  {0xff84ca94u, 0x809c0058u, 0x889c0058u}, // race-order per-slot diagnostic RX: lwz -> lbz
  {0xff84cab8u, 0x3b9c0804u, 0x3b9c0801u}, // diagnostic cursor: next segment plus one order byte
  {0xff818644u, 0x57bd07beu, 0x57bd077eu}, // displayed participant count: mask7, not3
  {0xff81863cu, 0x3bc00003u, 0x3bc00007u}, // UI cache clear loop: eight bytes
  {0xff818674u, 0x3800000cu, 0x38000007u}, // UI cache clear starts at byte7
  {0xff81867cu, 0x7d69012eu, 0x7d6901aeu}, // UI cache clear: stwx -> stbx
  {0xff818680u, 0x3000fffcu, 0x3000ffffu}, // UI cache clear index decrements by1
  {0xff8186e4u, 0x5400173au, 0x5400077eu}, // UI order stack index: low three bits, byte index
  {0xff8186e8u, 0x7d67012eu, 0x7d6701aeu}, // UI order stack write: stwx -> stbx
  {0xff818734u, 0x813a0000u, 0x893a0000u}, // UI order stack load: lwz -> lbz
  {0xff81874cu, 0x801b0000u, 0x881b0000u}, // UI cache load: lwz -> lbz
  {0xff818758u, 0x913b0000u, 0x993b0000u}, // UI cache store: stw -> stb
  {0xff81875cu, 0x813a0000u, 0x893a0000u}, // UI order stack reload: lwz -> lbz
  {0xff8187d8u, 0x3b5a0004u, 0x3b5a0001u}, // UI order stack cursor: byte stride
  {0xff8187dcu, 0x3b7b0004u, 0x3b7b0001u}, // UI cache cursor: byte stride
  {0xff8187f8u, 0x5680103au, 0x7e80a378u}, // UI local cache index: no word scaling
  {0xff8187fcu, 0x7d69012eu, 0x7d6901aeu}, // UI local cache store: stwx -> stbx
  {0xff840270u, 0x801e0080u, 0x881e0080u}, // linked car-map selected-ID load: lwz -> lbz
  {0xff840280u, 0x813e0080u, 0x893e0080u}, // linked car-map selected-ID reload: lwz -> lbz
  {0xff8402ccu, 0x3bde0004u, 0x3bde0001u}, // linked car-map selected-list cursor: byte stride
  {0xff831fe8u, 0x41860078u, 0x3d600023u}, // bounded native starting grid: preserve four lanes/offline; put slots5..8 eight units behind in matching lanes
  {0xff831fecu, 0x3d400023u, 0x396bcf14u}, // bounded native starting grid: preserve four lanes/offline; put slots5..8 eight units behind in matching lanes
  {0xff831ff0u, 0x394aee24u, 0x812b0054u}, // bounded native starting grid: preserve four lanes/offline; put slots5..8 eight units behind in matching lanes
  {0xff831ff4u, 0x800a0094u, 0x38e00000u}, // bounded native starting grid: preserve four lanes/offline; put slots5..8 eight units behind in matching lanes
  {0xff831ff8u, 0x1c000014u, 0x41860030u}, // bounded native starting grid: preserve four lanes/offline; put slots5..8 eight units behind in matching lanes
  {0xff831ffcu, 0x3d600023u, 0x3d400023u}, // bounded native starting grid: preserve four lanes/offline; put slots5..8 eight units behind in matching lanes
  {0xff832000u, 0x396bcf14u, 0x394aee24u}, // bounded native starting grid: preserve four lanes/offline; put slots5..8 eight units behind in matching lanes
  {0xff832004u, 0x812b0054u, 0x800a0094u}, // bounded native starting grid: preserve four lanes/offline; put slots5..8 eight units behind in matching lanes
  {0xff832008u, 0x3cc0000fu, 0x2c800004u}, // bounded native starting grid: preserve four lanes/offline; put slots5..8 eight units behind in matching lanes
  {0xff83200cu, 0x38c6e0c0u, 0x4184000cu}, // bounded native starting grid: preserve four lanes/offline; put slots5..8 eight units behind in matching lanes
  {0xff832010u, 0xc1860000u, 0x3000fffcu}, // bounded native starting grid: preserve four lanes/offline; put slots5..8 eight units behind in matching lanes
  {0xff832014u, 0x7d290214u, 0x38e00001u}, // bounded native starting grid: preserve four lanes/offline; put slots5..8 eight units behind in matching lanes
  {0xff832018u, 0x80090020u, 0x1c000014u}, // bounded native starting grid: preserve four lanes/offline; put slots5..8 eight units behind in matching lanes
  {0xff83201cu, 0x39290020u, 0x7d290214u}, // bounded native starting grid: preserve four lanes/offline; put slots5..8 eight units behind in matching lanes
  {0xff832020u, 0x81090004u, 0x39290020u}, // bounded native starting grid: preserve four lanes/offline; put slots5..8 eight units behind in matching lanes
  {0xff832024u, 0x80e90008u, 0x48000008u}, // bounded native starting grid: preserve four lanes/offline; put slots5..8 eight units behind in matching lanes
  {0xff832028u, 0x393e00fcu, 0x3929000cu}, // bounded native starting grid: preserve four lanes/offline; put slots5..8 eight units behind in matching lanes
  {0xff83202cu, 0x901e00fcu, 0x80090000u}, // bounded native starting grid: preserve four lanes/offline; put slots5..8 eight units behind in matching lanes
  {0xff832030u, 0x91090004u, 0x901e00fcu}, // bounded native starting grid: preserve four lanes/offline; put slots5..8 eight units behind in matching lanes
  {0xff832034u, 0x90e90008u, 0x80090004u}, // bounded native starting grid: preserve four lanes/offline; put slots5..8 eight units behind in matching lanes
  {0xff832038u, 0xd19e0114u, 0x901e0100u}, // bounded native starting grid: preserve four lanes/offline; put slots5..8 eight units behind in matching lanes
  {0xff83203cu, 0x800a0094u, 0x80090008u}, // bounded native starting grid: preserve four lanes/offline; put slots5..8 eight units behind in matching lanes
  {0xff832040u, 0x1c000014u, 0x901e0104u}, // bounded native starting grid: preserve four lanes/offline; put slots5..8 eight units behind in matching lanes
  {0xff832044u, 0x3cc0000fu, 0xa809000cu}, // bounded native starting grid: preserve four lanes/offline; put slots5..8 eight units behind in matching lanes
  {0xff832048u, 0x38c6e0d0u, 0x2c870000u}, // bounded native starting grid: preserve four lanes/offline; put slots5..8 eight units behind in matching lanes
  {0xff83204cu, 0x812b0054u, 0x41860034u}, // bounded native starting grid: preserve four lanes/offline; put slots5..8 eight units behind in matching lanes
  {0xff832050u, 0xc8060000u, 0x3cc04100u}, // bounded native starting grid: preserve four lanes/offline; put slots5..8 eight units behind in matching lanes
  {0xff832054u, 0x7d290214u, 0x90c10008u}, // bounded native starting grid: preserve four lanes/offline; put slots5..8 eight units behind in matching lanes
  {0xff832058u, 0xa809002cu, 0xc1a10008u}, // bounded native starting grid: preserve four lanes/offline; put slots5..8 eight units behind in matching lanes
  {0xff83205cu, 0x48000054u, 0x2c800000u}, // bounded native starting grid: preserve four lanes/offline; put slots5..8 eight units behind in matching lanes
  {0xff832060u, 0x3d400023u, 0x41860014u}, // bounded native starting grid: preserve four lanes/offline; put slots5..8 eight units behind in matching lanes
  {0xff832064u, 0x394acf14u, 0xc01e00fcu}, // bounded native starting grid: preserve four lanes/offline; put slots5..8 eight units behind in matching lanes
  {0xff832068u, 0x3cc0000fu, 0xec006828u}, // bounded native starting grid: preserve four lanes/offline; put slots5..8 eight units behind in matching lanes
  {0xff83206cu, 0x38c6e0c0u, 0xd01e00fcu}, // bounded native starting grid: preserve four lanes/offline; put slots5..8 eight units behind in matching lanes
  {0xff832070u, 0xc1860000u, 0x48000010u}, // bounded native starting grid: preserve four lanes/offline; put slots5..8 eight units behind in matching lanes
  {0xff832074u, 0x3cc0000fu, 0xc01e0104u}, // bounded native starting grid: preserve four lanes/offline; put slots5..8 eight units behind in matching lanes
  {0xff832078u, 0x812a0054u, 0xec006828u}, // bounded native starting grid: preserve four lanes/offline; put slots5..8 eight units behind in matching lanes
  {0xff83207cu, 0x38c6e0d0u, 0xd01e0104u}, // bounded native starting grid: preserve four lanes/offline; put slots5..8 eight units behind in matching lanes
  {0xff832080u, 0x3969000cu, 0x3cc0000fu}, // bounded native starting grid: preserve four lanes/offline; put slots5..8 eight units behind in matching lanes
  {0xff832084u, 0x8009000cu, 0x38c6e0c0u}, // bounded native starting grid: preserve four lanes/offline; put slots5..8 eight units behind in matching lanes
  {0xff832088u, 0x810b0004u, 0xc1860000u}, // bounded native starting grid: preserve four lanes/offline; put slots5..8 eight units behind in matching lanes
  {0xff83208cu, 0x816b0008u, 0xd19e0114u}, // bounded native starting grid: preserve four lanes/offline; put slots5..8 eight units behind in matching lanes
  {0xff832090u, 0x393e00fcu, 0x38c60010u}, // bounded native starting grid: preserve four lanes/offline; put slots5..8 eight units behind in matching lanes
  {0xff832094u, 0x901e00fcu, 0xc8060000u}, // bounded native starting grid: preserve four lanes/offline; put slots5..8 eight units behind in matching lanes
  {0xff832098u, 0x91090004u, 0x60000000u}, // bounded native starting grid: preserve four lanes/offline; put slots5..8 eight units behind in matching lanes
  {0xff83209cu, 0x91690008u, 0x60000000u}, // bounded native starting grid: preserve four lanes/offline; put slots5..8 eight units behind in matching lanes
  {0xff8320a0u, 0xd19e0114u, 0x60000000u}, // bounded native starting grid: preserve four lanes/offline; put slots5..8 eight units behind in matching lanes
  {0xff8320a4u, 0x812a0054u, 0x60000000u}, // bounded native starting grid: preserve four lanes/offline; put slots5..8 eight units behind in matching lanes
  {0xff8320a8u, 0xc8060000u, 0x60000000u}, // bounded native starting grid: preserve four lanes/offline; put slots5..8 eight units behind in matching lanes
  {0xff8320acu, 0xa8090018u, 0x60000000u}, // bounded native starting grid: preserve four lanes/offline; put slots5..8 eight units behind in matching lanes
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
inline Result ApplyBigEndianCrom(uint8_t* bytes, size_t size, const char* game,
                                bool explicitlyEnabled=false) {
  if(!explicitlyEnabled)return Result::Disabled;
  if(!game||std::strcmp(game,"srally2"))return Result::UnsupportedGame;
  if(!bytes||size!=0x800000)return Result::WrongSize;
  // Validate all patch sites before writing, so mismatches cannot partially patch ROM.
  for(const auto& site:Sites)
    if(ReadBE(bytes+(site.pc-0xff800000u))!=site.expected)return Result::UnexpectedOpcode;
  if(CRC32(bytes,size)!=CromCRC32)return Result::WrongCromCRC;
  for(const auto& site:Sites)WriteBE(bytes+(site.pc-0xff800000u),site.replacement);
  return Result::Applied;
}
}} // namespace Srally2::EightPlayerPpcResearch
#endif
