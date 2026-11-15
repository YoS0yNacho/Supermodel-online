// Scud Race Plus Rev A: secret-vehicle textures for courses that do not load them.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INCLUDED_SCUDPLUS_HIDDEN_CAR_TEXTURES_H
#define INCLUDED_SCUDPLUS_HIDDEN_CAR_TEXTURES_H
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct TextureOverlay;

/*
 * The four secret vehicles (bus, tank, cat, rocket) only have textures in the
 * Super Beginner course's texture set; every other course loads different
 * texels into the same rectangles, so with the secret-car gate patched they
 * race in the wrong skin. Build() decodes the Super Beginner set straight
 * from the banked CROM (the same LZSS blocks the game uploads), copies the
 * vehicles' rectangles into a TextureOverlay page and lists the vehicle
 * models, so the renderers can draw those models from the overlay on any
 * course. Nothing the emulated program sees changes.
 */
namespace ScudPlusHiddenCars {

enum class Result { WrongGame, BadRom, Built };

// crom: Supermodel's CROM buffer (fixed 8 MB followed by the banked region,
// host-endian 32-bit words). Returns Built and fills `overlay` on success.
// `why` receives a short reason on failure.
Result Build(const char* game, const uint8_t* crom, size_t cromSize, std::shared_ptr<const TextureOverlay>& overlay, std::string* why = nullptr);

// Exposed for tests: Okumura LZSS as used by the game's texture loader
// (4096-byte zero-filled ring, start index 0xFEE, 2-byte threshold).
// `readByte(i)` returns compressed byte i; `n` is the compressed length.
template <typename ReadByte>
void DecompressLzss(ReadByte readByte, size_t n, std::vector<uint8_t>& out)
{
  uint8_t ring[4096] = {};
  unsigned r = 0xFEE;
  unsigned flags = 0;
  size_t i = 0;
  for (;;)
  {
    flags >>= 1;
    if (!(flags & 0x100))
    {
      if (i >= n) break;
      flags = readByte(i++) | 0xFF00;
    }
    if (flags & 1)
    {
      if (i >= n) break;
      const uint8_t c = readByte(i++);
      out.push_back(c);
      ring[r] = c; r = (r + 1) & 0xFFF;
    }
    else
    {
      if (i + 1 >= n) break;
      const unsigned b1 = readByte(i), b2 = readByte(i + 1);
      i += 2;
      const unsigned pos = b1 | ((b2 & 0xF0) << 4);
      const unsigned len = (b2 & 0x0F) + 3;
      for (unsigned k = 0; k < len; ++k)
      {
        const uint8_t c = ring[(pos + k) & 0xFFF];
        out.push_back(c);
        ring[r] = c; r = (r + 1) & 0xFFF;
      }
    }
  }
}

}  // namespace ScudPlusHiddenCars
#endif
