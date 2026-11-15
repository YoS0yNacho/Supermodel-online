// ROM-free checks for the Scud Race Plus secret-vehicle texture overlay builder. GPL-3.0-or-later
#include "Network/Games/Scud/HiddenCarTextures.h"
#include "Graphics/TextureOverlay.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

// Supermodel's CROM buffer: host-endian 32-bit words, so byte i of the ROM stream lives at i^3.
struct FakeCrom {
  std::vector<uint8_t> bytes;
  explicit FakeCrom(size_t size) : bytes(size, 0) {}
  void Byte(size_t addr, uint8_t v) { bytes[addr ^ 3] = v; }
  void Word(size_t addr, uint32_t v) { std::memcpy(&bytes[addr], &v, 4); }
};

constexpr uint32_t kBanked = 0x800000, kBank = 0x800000;

// LZSS with literals only: a 0xFF flag byte announces eight literal bytes.
std::vector<uint8_t> LiteralLzss(const std::vector<uint8_t>& raw) {
  std::vector<uint8_t> out;
  for (size_t i = 0; i < raw.size(); i += 8) {
    out.push_back(0xFF);
    for (size_t k = 0; k < 8; ++k) out.push_back(i + k < raw.size() ? raw[i + k] : 0);
  }
  return out;
}

// Texture FIFO payload for a 256x256 16-bit texture with mipmaps, 8x8 tiles, where the
// texel at (x, y) of level L is value(L, x, y). Tiles are stored the way the Real3D
// reads them back: within a tile, the FIFO's 16-bit halves follow the decode8x8 table.
std::vector<uint8_t> Fifo(uint16_t (*value)(int, unsigned, unsigned)) {
  static const unsigned dec[64] = {
    1,0,5,4,9,8,13,12, 3,2,7,6,11,10,15,14, 17,16,21,20,25,24,29,28, 19,18,23,22,27,26,31,30,
    33,32,37,36,41,40,45,44, 35,34,39,38,43,42,47,46, 49,48,53,52,57,56,61,60, 51,50,55,54,59,58,63,62 };
  std::vector<uint16_t> halves;
  unsigned size = 256;
  for (int level = 0; size >= 2; ++level, size /= 2) {
    const unsigned tile = size >= 8 ? 8 : size;
    for (unsigned ty = 0; ty < size; ty += tile)
      for (unsigned tx = 0; tx < size; tx += tile) {
        std::vector<uint16_t> t(tile * tile);
        for (unsigned yy = 0; yy < tile; ++yy)
          for (unsigned xx = 0; xx < tile; ++xx) {
            unsigned idx;
            if (tile == 8) idx = dec[yy * 8 + xx];
            else if (tile == 4) { static const unsigned d4[16] = {1,0,5,4, 3,2,7,6, 9,8,13,12, 11,10,15,14}; idx = d4[yy * 4 + xx]; }
            else { static const unsigned d2[4] = {1,0, 3,2}; idx = d2[yy * 2 + xx]; }
            t[idx] = value(level, tx + xx, ty + yy);
          }
        halves.insert(halves.end(), t.begin(), t.end());
      }
  }
  // the block's bytes are the FIFO's host 16-bit halves in memory order
  std::vector<uint8_t> bytes(halves.size() * 2);
  std::memcpy(bytes.data(), halves.data(), bytes.size());
  return bytes;
}

uint16_t Pattern(int level, unsigned x, unsigned y) { return uint16_t(0x8000 | (level << 12) | ((y & 0x3F) << 6) | (x & 0x3F)); }

void BuildRom(FakeCrom& crom, unsigned blocks) {
  // The 28 Super Beginner entries, in the game's order (page, x, y of each 256x256 block)
  static const int layout[28][3] = {
    {1,0,0},{0,0,0},{0,256,0},{0,512,0},{0,768,0},{0,0,256},{0,256,256},{0,512,256},{0,768,256},
    {0,1024,0},{1,1280,0},{0,1280,0},{0,1536,0},{0,1792,0},{0,1024,256},{0,1280,256},{0,1536,256},{0,1792,256},
    {0,0,512},{1,256,512},{0,256,512},{1,512,512},{0,512,512},{0,768,512},{0,0,768},{0,256,768},{0,512,768},{0,768,768} };
  const std::vector<uint8_t> payload = Fifo(Pattern);
  const std::vector<uint8_t> packed = LiteralLzss(payload);
  size_t src = 0;
  size_t entry = kBanked + 0x38590;
  for (unsigned b = 0; b < blocks; ++b, entry += 12) {
    const uint32_t header = 0x00E03040u | (layout[b][0] << 20) | (3 << 17) | (3 << 14) | ((layout[b][2] / 32) << 7) | (layout[b][1] / 32);
    crom.Word(entry, 0xFF000000u | uint32_t(src));
    crom.Word(entry + 4, header);
    crom.Word(entry + 8, uint32_t(payload.size()));
    const size_t block = kBanked + 4 * kBank + src;
    crom.Word(block, 2);
    crom.Word(block + 4, uint32_t(packed.size()));
    for (size_t i = 0; i < packed.size(); ++i) crom.Byte(block + 0x10 + i, packed[i]);
    src += 0x10 + packed.size();
    src = (src + 3) & ~size_t(3);
  }
  crom.Word(entry, 0xFFFFFFFFu);
}

}  // namespace

int main() {
  // LZSS: a back-reference copies from the ring (zero-filled, start 0xFEE)
  {
    const uint8_t packed[] = { 0x07, 'a', 'b', 'c', 0xEE, 0xF0 /* pos 0xFEE, len 3 */ };
    std::vector<uint8_t> out;
    ScudPlusHiddenCars::DecompressLzss([&](size_t i) { return packed[i]; }, 6, out);
    assert(out.size() == 6 && out[0] == 'a' && out[3] == 'a' && out[4] == 'b' && out[5] == 'c');
  }

  const size_t cromSize = kBanked + 5 * kBank;
  FakeCrom crom(cromSize);
  std::shared_ptr<const TextureOverlay> overlay;
  std::string why;

  // Other games and unexpected ROM contents are refused
  assert(ScudPlusHiddenCars::Build("scud", crom.bytes.data(), cromSize, overlay, &why) == ScudPlusHiddenCars::Result::WrongGame);
  assert(ScudPlusHiddenCars::Build("scudplus", crom.bytes.data(), cromSize, overlay, &why) == ScudPlusHiddenCars::Result::BadRom && !overlay);
  BuildRom(crom, 27);
  assert(ScudPlusHiddenCars::Build("scudplus", crom.bytes.data(), cromSize, overlay, &why) == ScudPlusHiddenCars::Result::BadRom && !overlay);
  BuildRom(crom, 28);
  assert(ScudPlusHiddenCars::Build("scudplus", crom.bytes.data(), 0x900000, overlay, &why) == ScudPlusHiddenCars::Result::BadRom);

  // A good ROM builds the overlay
  if (ScudPlusHiddenCars::Build("scudplus", crom.bytes.data(), cromSize, overlay, &why) != ScudPlusHiddenCars::Result::Built) std::fprintf(stderr, "why: %s\n", why.c_str());
  assert(ScudPlusHiddenCars::Build("scudplus", crom.bytes.data(), cromSize, overlay, &why) == ScudPlusHiddenCars::Result::Built && overlay);
  assert(overlay->ram.size() == size_t(TextureOverlay::kWidth) * TextureOverlay::kHeight);
  assert(!overlay->Empty());
  // rocket, bus, tank, all 24 running-cat frames (first and last), the rocket's far LOD and the rider
  assert(overlay->HasModel(0xB47BAC) && overlay->HasModel(0x01B47BAC) && overlay->HasModel(0xB919C0) && overlay->HasModel(0xB77D7D));
  assert(overlay->HasModel(0xAE5167) && overlay->HasModel(0xB0CC00) && overlay->HasModel(0xB012F9) && overlay->HasModel(0xB66F58) && overlay->HasModel(0xA66FBC));
  // not a model of the regular cars, the tank's reflection-map part or anything else
  assert(!overlay->HasModel(0xB0D377) && !overlay->HasModel(0xB24D91) && !overlay->HasModel(0x123456));
  unsigned px, py;
  // Vehicle rectangles inside the Super Beginner set are redirected ...
  assert(overlay->Lookup(1408, 0, 64, 64, px, py) && overlay->Lookup(1152, 256, 128, 128, px, py) && overlay->Lookup(0, 128, 128, 64, px, py));
  // ... shared assets every course loads its own copy of (reflection map, seats, grille, common lists) are not, nor is anything else
  assert(!overlay->Lookup(64, 1024 + 128, 64, 64, px, py) && !overlay->Lookup(1408, 1024, 64, 128, px, py) && !overlay->Lookup(1280, 1024 + 128, 256, 128, px, py));
  assert(!overlay->Lookup(0, 1024 + 960, 256, 64, px, py) && !overlay->Lookup(128, 1024 + 512, 128, 128, px, py) && !overlay->Lookup(0, 1024 + 576, 128, 64, px, py));
  assert(!overlay->Lookup(1408, 0, 64, 32, px, py) && !overlay->Lookup(1408, 32, 64, 64, px, py));
  assert(!overlay->Lookup(1024, 1024 + 256, 256, 128, px, py));
  assert(overlay->rects.size() == 52);
  assert(overlay->models.size() == 573);

  // Every rectangle lands in the left half, copies level 0 texel for texel and brings its mipmaps along
  const int mipXBase[] = { 0, 1024, 1536, 1792, 1920, 1984 };
  const int mipYBase[] = { 0, 512, 768, 896, 960, 992 };
  unsigned checked = 0;
  for (const auto& kv : overlay->rects) {
    const unsigned sx = unsigned(kv.first >> 36) & 2047, sy = unsigned(kv.first >> 24) & 2047;
    const unsigned w = unsigned(kv.first >> 12) & 2047, h = unsigned(kv.first) & 2047;
    const unsigned dx = kv.second.x, dy = kv.second.y;
    assert(dx + w <= 1024 && dy + h <= 1024 && (dx % 32) == 0 && (dy % 32) == 0);
    unsigned lx, ly;
    assert(overlay->Lookup(sx, sy, w, h, lx, ly) && lx == dx && ly == dy);
    const unsigned inPageY = sy % 1024;
    for (int level = 0; level < 3; ++level) {
      const unsigned d = 1u << level;
      for (unsigned yy = 0; yy < h / d; yy += 7)
        for (unsigned xx = 0; xx < w / d; xx += 5) {
          const unsigned ox = mipXBase[level] + dx / d + xx, oy = mipYBase[level] + dy / d + yy;
          // the source texel's position within its 256x256 block at this level
          const unsigned bx = ((sx % 256) / d + xx) % (256 / d), by = ((inPageY % 256) / d + yy) % (256 / d);
          if (overlay->ram[oy * TextureOverlay::kWidth + ox] != Pattern(level, bx, by)) { std::fprintf(stderr, "mismatch src(%u,%u %ux%u) dst(%u,%u) level %d at (%u,%u): got %04X want %04X (bx %u by %u)\n", sx, sy, w, h, dx, dy, level, xx, yy, overlay->ram[oy * TextureOverlay::kWidth + ox], Pattern(level, bx, by), bx, by); }
          assert(overlay->ram[oy * TextureOverlay::kWidth + ox] == Pattern(level, bx, by));
          ++checked;
        }
    }
  }
  assert(checked > 1000);

  // Rectangles do not overlap each other in the overlay page
  std::vector<uint8_t> used(1024 * 1024 / (32 * 32), 0);
  for (const auto& kv : overlay->rects) {
    const unsigned w = unsigned(kv.first >> 12) & 2047, h = unsigned(kv.first) & 2047;
    for (unsigned y = kv.second.y / 32; y < (kv.second.y + h) / 32; ++y)
      for (unsigned x = kv.second.x / 32; x < (kv.second.x + w) / 32; ++x) {
        assert(!used[y * 32 + x]);
        used[y * 32 + x] = 1;
      }
  }
  std::printf("ok: %zu rectangles, %zu models, %u texels checked\n", overlay->rects.size(), overlay->models.size(), checked);
  return 0;
}
