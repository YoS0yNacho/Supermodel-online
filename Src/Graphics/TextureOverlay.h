/**
 ** Supermodel
 ** A Sega Model Series Arcade Emulator.
 **
 ** This file is part of Supermodel.
 **
 ** Supermodel is free software: you can redistribute it and/or modify it under
 ** the terms of the GNU General Public License as published by the Free
 ** Software Foundation, either version 3 of the License, or (at your option)
 ** any later version.
 **
 ** Supermodel is distributed in the hope that it will be useful, but WITHOUT
 ** ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 ** FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 ** more details.
 **
 ** You should have received a copy of the GNU General Public License along
 ** with Supermodel.  If not, see <http://www.gnu.org/licenses/>.
 **/

/*
 * TextureOverlay.h
 *
 * An auxiliary, read-only texture page that sits beside the two hardware
 * texture pages. It is not visible to the emulated program: it exists only so
 * the renderers can draw a known set of models with texture data the game
 * never loads into texture RAM.
 *
 * Why: Scud Race Plus keeps one 2048x2048 texture RAM layout per course and
 * the four secret vehicles (bus, tank, cat, rocket) only have their textures
 * in the Super Beginner set. When the session-only secret-car patch lets them
 * race on other courses, the hardware draws whatever that course loaded into
 * the same texel rectangles. Writing the missing texels into texture RAM is
 * not an option, because every free-looking cell is used by something later
 * in the race or in link play. Instead, the vehicle's texture rectangles are
 * copied out of the ROM into this page at build time, and the renderers
 * redirect the textured polygons of those models to it.
 *
 * Layout: ram[] is one Real3D page (2048x1024 texels) in the usual layout:
 * level 0 occupies the left half (x < 1024) and the mipmap levels occupy the
 * mipmap regions (mipXBase/mipYBase) exactly as texture RAM does, so the
 * renderers can treat it as a third page.
 *
 * Models are identified by their 24-bit VROM address (the address the culling
 * node points at). Rectangles are keyed by the polygon header's texture
 * position (x, y with page*1024 added, width, height). Texture offsets from
 * culling nodes are not applied to overlay models (the targeted models do
 * not use them).
 */

#ifndef INCLUDED_TEXTUREOVERLAY_H
#define INCLUDED_TEXTUREOVERLAY_H

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct TextureOverlay
{
  static constexpr unsigned kWidth = 2048;
  static constexpr unsigned kHeight = 1024;

  struct Position
  {
    uint16_t x;
    uint16_t y;
  };

  // kWidth*kHeight texels, Real3D page layout (level 0 and mipmap regions).
  std::vector<uint16_t> ram;

  // 24-bit VROM model addresses whose textured polygons are redirected.
  std::unordered_set<uint32_t> models;

  // Source rectangle -> position of its copy in ram[] (level 0).
  std::unordered_map<uint64_t, Position> rects;

  // Source rectangle key. y includes page*1024, like Legacy3D's texture base.
  static uint64_t Key(unsigned x, unsigned y, unsigned w, unsigned h)
  {
    return (uint64_t(x & 2047) << 36) | (uint64_t(y & 2047) << 24) | (uint64_t(w & 2047) << 12) | uint64_t(h & 2047);
  }

  bool Empty() const
  {
    return ram.empty() || models.empty() || rects.empty();
  }

  bool HasModel(uint32_t addr) const
  {
    return models.find(addr & 0x00FFFFFF) != models.end();
  }

  bool Lookup(unsigned x, unsigned y, unsigned w, unsigned h, unsigned &dx, unsigned &dy) const
  {
    auto it = rects.find(Key(x, y, w, h));
    if (it == rects.end())
      return false;
    dx = it->second.x;
    dy = it->second.y;
    return true;
  }
};

#endif  // INCLUDED_TEXTUREOVERLAY_H
