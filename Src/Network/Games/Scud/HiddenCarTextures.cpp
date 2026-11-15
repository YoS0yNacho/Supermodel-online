// Scud Race Plus Rev A: secret-vehicle textures for courses that do not load them.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "HiddenCarTextures.h"
#include "Graphics/TextureOverlay.h"
#include <algorithm>
#include <cstring>

namespace ScudPlusHiddenCars {
namespace {

/*
 * Texture sets (verified on the owner's Export Rev A EPRs). The per-course
 * texture lists live in banked CROM bank 0 at 0x38040 (Night), 0x38194,
 * 0x382E8, 0x3843C (Medium) and 0x38590 (Super Beginner): 12-byte entries
 * (bank-relative source address FFxxxxxx, Real3D texture header, length)
 * terminated by FFFFFFFF. Each source points at a block header (type word 2,
 * compressed length) followed at +0x10 by LZSS data that decompresses to the
 * exact texture FIFO payload (8x8-tiled 16-bit texels, level 0 then mipmaps).
 */
constexpr uint32_t kBankSize        = 0x800000;
constexpr uint32_t kBankedBase      = 0x800000;        // banked CROM follows the fixed 8 MB
constexpr uint32_t kSuperBeginnerList = 0x38590;       // in bank 0
constexpr uint32_t kSuperBeginnerBank = 4;             // the list's sources live in bank 4
constexpr uint32_t kFirstHeader     = 0x00F6F040;      // page 1 (0,0) 256x256 16-bit with mipmaps
constexpr unsigned kExpectedBlocks  = 28;
constexpr unsigned kMaxBlocks       = 64;

// Real3D mipmap layout (see Model3/Real3D.cpp)
constexpr int mipXBase[]  = { 0, 1024, 1536, 1792, 1920, 1984, 2016, 2032, 2040, 2044, 2046, 2047 };
constexpr int mipYBase[]  = { 0, 512, 768, 896, 960, 992, 1008, 1016, 1020, 1022, 1023 };
constexpr int mipDivisor[] = { 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024 };

constexpr unsigned decode8x8[64] =
{
   1, 0, 5, 4, 9, 8,13,12,   3, 2, 7, 6,11,10,15,14,
  17,16,21,20,25,24,29,28,  19,18,23,22,27,26,31,30,
  33,32,37,36,41,40,45,44,  35,34,39,38,43,42,47,46,
  49,48,53,52,57,56,61,60,  51,50,55,54,59,58,63,62
};
constexpr unsigned decode8x4[32] = { 1,0,5,4, 3,2,7,6, 9,8,13,12, 11,10,15,14, 17,16,21,20, 19,18,23,22, 25,24,29,28, 27,26,31,30 };
constexpr unsigned decode8x2[16] = { 1,0, 3,2, 5,4, 7,6, 9,8, 11,10, 13,12, 15,14 };

struct RectEntry
{
  uint16_t x, y, w, h;   // source (y includes page*1024)
  uint16_t dx, dy;       // copy in the overlay page (level 0, always x < 1024)
};

// Source rectangles (x, y incl. page*1024, w, h) -> overlay position (dx, dy).
// They are the texture rectangles of the vehicle models below whose Super
// Beginner content is unique to that course (reflection maps, seats and other
// shared assets that every course loads with its own copy stay in texture
// RAM), packed into the left half of the overlay page so their mipmaps fall
// into the mipmap regions. The models are every VROM model of the game's
// vehicle scene-graph templates (CROM bank 0) that samples one of these
// rectangles: all 24 running-cat animation frames, the cat's face, the rocket
// and tank at every level of detail, the rocket rider's animation and the bus.
static constexpr RectEntry kRects[] = {
  {    0,  128, 128,  64,  832,    0 },  // bus
  {    0,  192,  64,  32,  384,  384 },  // bus
  {    0,  224,  64,  32,  448,  384 },  // bus
  {    0,  256, 128,  64,  256,  256 },  // tank
  {    0,  320, 128,  64,  384,  256 },  // tank
  {    0,  384,  64, 128,  288,    0 },  // tank
  {   64,  192,  64,  64,  960,    0 },  // bus
  {   64,  384,  64, 128,  352,    0 },  // tank
  {  128,  128,  64, 128,  416,    0 },  // bus
  {  128,  256,  64,  64,  512,  256 },  // tank
  {  128,  320,  64,  32,  512,  384 },  // tank
  {  128,  352,  64,  32,  576,  384 },  // tank
  {  128,  384,  64,  64,  576,  256 },  // tank
  {  128,  448,  64,  64,  640,  256 },  // tank
  {  192,  128,  64, 128,  480,    0 },  // bus
  {  192,  256,  64,  64,  704,  256 },  // tank
  {  192,  320,  32,  64,  256,  320 },  // bus
  {  192,  384,  64, 128,  544,    0 },  // tank
  {  224,  320,  32,  64,  288,  320 },  // tank
  {  448,    0,  64,  64,  768,  256 },  // bus
  {  448,   64,  64,  64,  832,  256 },  // bus
  {  512,    0, 128, 256,    0,    0 },  // cat
  {  576,    0,  32, 256,  128,    0 },  // cat
  {  640,    0,  64, 128,  608,    0 },  // cat
  {  640,  128,  64,  64,  896,  256 },  // cat
  {  640,  608, 128,  32,  864,  320 },  // bus
  {  896,  512,  64,  64,  960,  256 },  // rocket
  {  928,  736,  32,  32,  992,  320 },  // rocket
  {  992,  192,  32,  32,  896,  384 },  // bus
  { 1152,  256,  64, 128,  672,    0 },  // rocket
  { 1152,  256, 128, 128,  160,    0 },  // rocket
  { 1216,  192,  64,  32,  640,  384 },  // tank
  { 1216,  224,  64,  32,  704,  384 },  // tank
  { 1216,  256,  64,  64,    0,  320 },  // rocket
  { 1216,  320,  64,  64,   64,  320 },  // rocket
  { 1408,    0,  64,  64,  128,  320 },  // rocket
  { 1408,   64,  64,  64,  192,  320 },  // rocket
  { 1408,  128,  32,  32,  928,  384 },  // rocket
  { 1408,  160,  32,  32,  960,  384 },  // bus,rocket
  { 1408,  192,  64,  32,  768,  384 },  // rocket
  { 1408,  224,  64,  32,  832,  384 },  // rocket
  { 1408,  480, 128,  32,    0,  384 },  // cat
  { 1440,  128,  32,  32,  992,  384 },  // rocket
  { 1440,  160,  32,  32,    0,  416 },  // rocket
  { 1472,    0,  64, 128,  736,    0 },  // rocket
  { 1472,  128,  32, 128,  800,    0 },  // rocket
  { 1504,  128,  32,  64,  320,  320 },  // rocket
  { 1536,   64, 256,  64,    0,  256 },  // rocket
  { 1536,  448, 256,  32,  352,  320 },  // bus
  { 1536,  480, 256,  32,  608,  320 },  // bus
  { 1664,  384, 128,  32,  128,  384 },  // bus
  { 1664,  416, 128,  32,  256,  384 },  // bus
};

// bus: 6 models
static constexpr uint32_t kModels_bus[] = {
  0xB919C0, 0xB92184, 0xB92484, 0xB92666, 0xB92848, 0xB92A2A,
};
// tank: 12 models
static constexpr uint32_t kModels_tank[] = {
  0xB6E8C9, 0xB6F048, 0xB6F340, 0xB6F737, 0xB6F99B, 0xB703A0, 0xB775FE, 0xB77D7D,
  0xB81C07, 0xB82386, 0xB83117, 0xB8367C,
};
// cat: 510 models
static constexpr uint32_t kModels_cat[] = {
  0xAC7311, 0xAC7D37, 0xAC7EEC, 0xAC9197, 0xACA442, 0xAD47DE, 0xAE5167, 0xAE5290,
  0xAE579F, 0xAE591B, 0xAE5E8A, 0xAE5F3F, 0xAE6042, 0xAE611D, 0xAE6194, 0xAE62D9,
  0xAE63F9, 0xAE64A7, 0xAE6526, 0xAE65DB, 0xAE66DE, 0xAE67B9, 0xAE6830, 0xAE6975,
  0xAE6A95, 0xAE6B43, 0xAE6BC2, 0xAE6BD9, 0xAE6D02, 0xAE7211, 0xAE738D, 0xAE78FC,
  0xAE79B1, 0xAE7AB4, 0xAE7B8F, 0xAE7C06, 0xAE7D4B, 0xAE7E6B, 0xAE7F19, 0xAE7F98,
  0xAE804D, 0xAE8150, 0xAE822B, 0xAE82A2, 0xAE83E7, 0xAE8507, 0xAE85B5, 0xAE8634,
  0xAE864B, 0xAE8774, 0xAE8C83, 0xAE8DFF, 0xAE936E, 0xAE9423, 0xAE9526, 0xAE9601,
  0xAE9678, 0xAE97BD, 0xAE98DD, 0xAE998B, 0xAE9A0A, 0xAE9ABF, 0xAE9BC2, 0xAE9C9D,
  0xAE9D14, 0xAE9E59, 0xAE9F79, 0xAEA027, 0xAEA0A6, 0xAEA0BD, 0xAEA1E6, 0xAEA6F5,
  0xAEA871, 0xAEADE0, 0xAEAE95, 0xAEAF98, 0xAEB073, 0xAEB0EA, 0xAEB22F, 0xAEB34F,
  0xAEB3FD, 0xAEB47C, 0xAEB531, 0xAEB634, 0xAEB70F, 0xAEB786, 0xAEB8CB, 0xAEB9EB,
  0xAEBA99, 0xAEBB18, 0xAEBB2F, 0xAEBC58, 0xAEC167, 0xAEC2E3, 0xAEC852, 0xAEC907,
  0xAECA0A, 0xAECAE5, 0xAECB5C, 0xAECCA1, 0xAECDC1, 0xAECE6F, 0xAECEEE, 0xAECFA3,
  0xAED0A6, 0xAED181, 0xAED1F8, 0xAED33D, 0xAED45D, 0xAED50B, 0xAED58A, 0xAED5A1,
  0xAED6CA, 0xAEDBD9, 0xAEDD55, 0xAEE2C4, 0xAEE379, 0xAEE47C, 0xAEE557, 0xAEE5CE,
  0xAEE713, 0xAEE833, 0xAEE8E1, 0xAEE960, 0xAEEA15, 0xAEEB18, 0xAEEBF3, 0xAEEC6A,
  0xAEEDAF, 0xAEEECF, 0xAEEF7D, 0xAEEFFC, 0xAEF013, 0xAEF13C, 0xAEF64B, 0xAEF7C7,
  0xAEFD36, 0xAEFDEB, 0xAEFEEE, 0xAEFFC9, 0xAF0040, 0xAF0185, 0xAF02A5, 0xAF0353,
  0xAF03D2, 0xAF0487, 0xAF058A, 0xAF0665, 0xAF06DC, 0xAF0821, 0xAF0941, 0xAF09EF,
  0xAF0A6E, 0xAF0A85, 0xAF0BAE, 0xAF10BD, 0xAF1239, 0xAF17A8, 0xAF185D, 0xAF1960,
  0xAF1A3B, 0xAF1AB2, 0xAF1BF7, 0xAF1D17, 0xAF1DC5, 0xAF1E44, 0xAF1EF9, 0xAF1FFC,
  0xAF20D7, 0xAF214E, 0xAF2293, 0xAF23B3, 0xAF2461, 0xAF24E0, 0xAF24F7, 0xAF2620,
  0xAF2B2F, 0xAF2CAB, 0xAF321A, 0xAF32CF, 0xAF33D2, 0xAF34AD, 0xAF3524, 0xAF3669,
  0xAF3789, 0xAF3837, 0xAF38B6, 0xAF396B, 0xAF3A6E, 0xAF3B49, 0xAF3BC0, 0xAF3D05,
  0xAF3E25, 0xAF3ED3, 0xAF3F52, 0xAF3F69, 0xAF4092, 0xAF45A1, 0xAF471D, 0xAF4C8C,
  0xAF4D41, 0xAF4E44, 0xAF4F1F, 0xAF4F96, 0xAF50DB, 0xAF51FB, 0xAF52A9, 0xAF5328,
  0xAF53DD, 0xAF54E0, 0xAF55BB, 0xAF5632, 0xAF5777, 0xAF5897, 0xAF5945, 0xAF59C4,
  0xAF59DB, 0xAF5B04, 0xAF6013, 0xAF618F, 0xAF66FE, 0xAF67B3, 0xAF68B6, 0xAF6991,
  0xAF6A08, 0xAF6B4D, 0xAF6C6D, 0xAF6D1B, 0xAF6D9A, 0xAF6E4F, 0xAF6F52, 0xAF702D,
  0xAF70A4, 0xAF71E9, 0xAF7309, 0xAF73B7, 0xAF7436, 0xAF744D, 0xAF7576, 0xAF7A85,
  0xAF7C01, 0xAF8170, 0xAF8225, 0xAF8328, 0xAF8403, 0xAF847A, 0xAF85BF, 0xAF86DF,
  0xAF878D, 0xAF880C, 0xAF88C1, 0xAF89C4, 0xAF8A9F, 0xAF8B16, 0xAF8C5B, 0xAF8D7B,
  0xAF8E29, 0xAF8EA8, 0xAF8EBF, 0xAF8FE8, 0xAF94F7, 0xAF9673, 0xAF9BE2, 0xAF9C97,
  0xAF9D9A, 0xAF9E75, 0xAF9EEC, 0xAFA031, 0xAFA151, 0xAFA1FF, 0xAFA27E, 0xAFA333,
  0xAFA436, 0xAFA511, 0xAFA588, 0xAFA6CD, 0xAFA7ED, 0xAFA89B, 0xAFA91A, 0xAFA931,
  0xAFAA5A, 0xAFAF69, 0xAFB0E5, 0xAFB654, 0xAFB709, 0xAFB80C, 0xAFB8E7, 0xAFB95E,
  0xAFBAA3, 0xAFBBC3, 0xAFBC71, 0xAFBCF0, 0xAFBDA5, 0xAFBEA8, 0xAFBF83, 0xAFBFFA,
  0xAFC13F, 0xAFC25F, 0xAFC30D, 0xAFC38C, 0xAFC3A3, 0xAFC4CC, 0xAFC9DB, 0xAFCB57,
  0xAFD0C6, 0xAFD17B, 0xAFD27E, 0xAFD359, 0xAFD3D0, 0xAFD515, 0xAFD635, 0xAFD6E3,
  0xAFD762, 0xAFD817, 0xAFD91A, 0xAFD9F5, 0xAFDA6C, 0xAFDBB1, 0xAFDCD1, 0xAFDD7F,
  0xAFDDFE, 0xAFDE15, 0xAFDF3E, 0xAFE44D, 0xAFE5C9, 0xAFEB38, 0xAFEBED, 0xAFECF0,
  0xAFEDCB, 0xAFEE42, 0xAFEF87, 0xAFF0A7, 0xAFF155, 0xAFF1D4, 0xAFF289, 0xAFF38C,
  0xAFF467, 0xAFF4DE, 0xAFF623, 0xAFF743, 0xAFF7F1, 0xAFF870, 0xAFF887, 0xAFF9B0,
  0xAFFEBF, 0xB0003B, 0xB005AA, 0xB0065F, 0xB00762, 0xB0083D, 0xB008B4, 0xB009F9,
  0xB00B19, 0xB00BC7, 0xB00C46, 0xB00CFB, 0xB00DFE, 0xB00ED9, 0xB00F50, 0xB01095,
  0xB011B5, 0xB01263, 0xB012E2, 0xB012F9, 0xB01422, 0xB01931, 0xB01AAD, 0xB0201C,
  0xB020D1, 0xB021D4, 0xB022AF, 0xB02326, 0xB0246B, 0xB0258B, 0xB02639, 0xB026B8,
  0xB0276D, 0xB02870, 0xB0294B, 0xB029C2, 0xB02B07, 0xB02C27, 0xB02CD5, 0xB02D54,
  0xB02D6B, 0xB02E94, 0xB033A3, 0xB0351F, 0xB03A8E, 0xB03B43, 0xB03C46, 0xB03D21,
  0xB03D98, 0xB03EDD, 0xB03FFD, 0xB040AB, 0xB0412A, 0xB041DF, 0xB042E2, 0xB043BD,
  0xB04434, 0xB04579, 0xB04699, 0xB04747, 0xB047C6, 0xB047DD, 0xB04906, 0xB04E15,
  0xB04F91, 0xB05500, 0xB055B5, 0xB056B8, 0xB05793, 0xB0580A, 0xB0594F, 0xB05A6F,
  0xB05B1D, 0xB05B9C, 0xB05C51, 0xB05D54, 0xB05E2F, 0xB05EA6, 0xB05FEB, 0xB0610B,
  0xB061B9, 0xB06238, 0xB0624F, 0xB06378, 0xB06887, 0xB06A03, 0xB06F72, 0xB07027,
  0xB0712A, 0xB07205, 0xB0727C, 0xB073C1, 0xB074E1, 0xB0758F, 0xB0760E, 0xB076C3,
  0xB077C6, 0xB078A1, 0xB07918, 0xB07A5D, 0xB07B7D, 0xB07C2B, 0xB07CAA, 0xB07CC1,
  0xB07DEA, 0xB082F9, 0xB08475, 0xB089E4, 0xB08A99, 0xB08B9C, 0xB08C77, 0xB08CEE,
  0xB08E33, 0xB08F53, 0xB09001, 0xB09080, 0xB09135, 0xB09238, 0xB09313, 0xB0938A,
  0xB094CF, 0xB095EF, 0xB0969D, 0xB0971C, 0xB09733, 0xB0985C, 0xB09D6B, 0xB09EE7,
  0xB0A456, 0xB0A50B, 0xB0A60E, 0xB0A6E9, 0xB0A760, 0xB0A8A5, 0xB0A9C5, 0xB0AA73,
  0xB0AAF2, 0xB0ABA7, 0xB0ACAA, 0xB0AD85, 0xB0ADFC, 0xB0AF41, 0xB0B061, 0xB0B10F,
  0xB0B18E, 0xB0B1A5, 0xB0B2CE, 0xB0B7DD, 0xB0B959, 0xB0BEC8, 0xB0BF7D, 0xB0C080,
  0xB0C15B, 0xB0C1D2, 0xB0C317, 0xB0C437, 0xB0C4E5, 0xB0C564, 0xB0C619, 0xB0C71C,
  0xB0C7F7, 0xB0C86E, 0xB0C9B3, 0xB0CAD3, 0xB0CB81, 0xB0CC00,
};
// rocket: 45 models
static constexpr uint32_t kModels_rocket[] = {
  0xA66FBC, 0xA676D6, 0xA67E3F, 0xA689B4, 0xA68E8A, 0xA69624, 0xA699E1, 0xA69D38,
  0xA6A9C2, 0xA6AD8C, 0xA6B119, 0xA6B470, 0xA6B7FD, 0xA6BBC7, 0xA6C095, 0xA6C45F,
  0xA6C7B6, 0xA6D2AA, 0xA6D674, 0xA6DA31, 0xA6EC7C, 0xA6F03A, 0xA6F46F, 0xA7080D,
  0xA710B3, 0xA72081, 0xB2FF90, 0xB301AE, 0xB47BAC, 0xB48300, 0xB487F5, 0xB488F0,
  0xB48BC9, 0xB49132, 0xB494B7, 0xB49789, 0xB49A5B, 0xB49D2D, 0xB66F58, 0xB67D57,
  0xB68841, 0xB68CFB, 0xB68EDD, 0xB690BF, 0xB692A1,
};

// CROM helpers: the buffer holds host-endian 32-bit words, so byte i of the
// big-endian ROM stream is at i^3.
struct Crom
{
  const uint8_t* p;
  size_t size;
  bool Has(size_t addr, size_t len) const { return addr <= size && len <= size - addr; }
  uint8_t Byte(size_t addr) const { return p[addr ^ 3]; }
  uint32_t Word(size_t addr) const { uint32_t v; std::memcpy(&v, p + addr, sizeof(v)); return v; }
};

// A 2048x2048 texture sheet (both pages) plus the 32x32-texel cells its level 0 covers.
struct Sheet
{
  std::vector<uint16_t> texels;
  std::vector<uint8_t> covered;   // 64x64 cells
  bool Covered(unsigned x, unsigned y, unsigned w, unsigned h) const
  {
    for (unsigned cy = y / 32; cy < (y + h + 31) / 32; cy++)
      for (unsigned cx = x / 32; cx < (x + w + 31) / 32; cx++)
        if (cx >= 64 || cy >= 64 || !covered[cy * 64 + cx])
          return false;
    return true;
  }
};

// Decodes one 16-bit texture upload (level 0 and, for type 0, its mipmaps)
// into the sheet, like CReal3D::UploadTexture/StoreTexture.
bool StoreUpload(uint32_t header, const std::vector<uint8_t>& fifo, Sheet& sheet)
{
  const unsigned x = 32 * (header & 0x3F), y = 32 * ((header >> 7) & 0x1F);
  const unsigned page = (header >> 20) & 1;
  unsigned width = 32 << ((header >> 14) & 7), height = 32 << ((header >> 17) & 7);
  const unsigned type = (header >> 24) & 0xFF;
  const bool sixteenBit = (header >> 23) & 1;
  if (!sixteenBit || type > 1)
    return false;
  // The decompressed block is byte for byte what the Real3D sees in its
  // texture FIFO (verified against live uploads), so read it as the FIFO's
  // 16-bit halves and let the tile tables do the rest.
  std::vector<uint16_t> texels(fifo.size() / 2);
  std::memcpy(texels.data(), fifo.data(), texels.size() * sizeof(uint16_t));
  size_t pos = 0;
  auto store = [&](unsigned xPos, unsigned yPos, unsigned w, unsigned h)
  {
    const unsigned tileX = std::min(8u, w), tileY = std::min(8u, h);
    const unsigned* decode = (tileX == 8) ? decode8x8 : (tileX == 4) ? decode8x4 : (tileX == 2) ? decode8x2 : nullptr;
    if (!decode || pos + size_t(w) * h > texels.size())
      return false;
    for (unsigned ty = yPos; ty < yPos + h; ty += tileY)
      for (unsigned tx = xPos; tx < xPos + w; tx += tileX)
      {
        for (unsigned yy = 0; yy < tileY; yy++)
          for (unsigned xx = 0; xx < tileX; xx++)
            sheet.texels[size_t(ty + yy) * 2048 + tx + xx] = texels[pos + decode[yy * tileX + xx]];
        pos += tileY * tileX;
      }
    return true;
  };
  if (!store(x, y + page * 1024, width, height))
    return false;
  for (unsigned cy = (y + page * 1024) / 32; cy < (y + page * 1024 + height) / 32; cy++)
    for (unsigned cx = x / 32; cx < (x + width) / 32; cx++)
      sheet.covered[cy * 64 + cx] = 1;
  if (type == 0)
  {
    for (int i = 1; width >= 4 && height >= 4; i++)
    {
      const unsigned xPos = mipXBase[i] + x / mipDivisor[i], yPos = mipYBase[i] + y / mipDivisor[i];
      width /= 2; height /= 2;
      if (!store(xPos, yPos + page * 1024, width, height))
        return false;
    }
  }
  return true;
}

bool Fail(std::string* why, const char* text)
{
  if (why) *why = text;
  return false;
}

// Decodes the Super Beginner set into a 2048x2048 sheet (both pages, mipmaps included).
bool DecodeSuperBeginnerSet(const Crom& crom, Sheet& sheet, std::string* why)
{
  const size_t list = kBankedBase + kSuperBeginnerList;
  const size_t bank = kBankedBase + size_t(kSuperBeginnerBank) * kBankSize;
  if (!crom.Has(list, 12 * (kMaxBlocks + 1)) || !crom.Has(bank, kBankSize))
    return Fail(why, "CROM too small");
  if (crom.Word(list + 4) != kFirstHeader)
    return Fail(why, "texture list not where expected");
  sheet.texels.assign(size_t(2048) * 2048, 0);
  sheet.covered.assign(64 * 64, 0);
  std::vector<uint8_t> fifo;
  unsigned blocks = 0;
  for (size_t e = list; ; e += 12)
  {
    const uint32_t src = crom.Word(e), header = crom.Word(e + 4);
    if (src == 0xFFFFFFFF)
      break;
    if (++blocks > kMaxBlocks || (src >> 24) != 0xFF)
      return Fail(why, "unexpected texture list entry");
    const size_t block = bank + (src & 0x7FFFFF);
    if (!crom.Has(block, 16) || crom.Word(block) != 2)
      return Fail(why, "unexpected texture block header");
    const uint32_t clen = crom.Word(block + 4);
    if (!crom.Has(block + 0x10, clen))
      return Fail(why, "texture block out of range");
    fifo.clear();
    DecompressLzss([&](size_t i) { return crom.Byte(block + 0x10 + i); }, clen, fifo);
    if (!StoreUpload(header, fifo, sheet))
      return Fail(why, "texture block did not decode");
  }
  if (blocks != kExpectedBlocks)
    return Fail(why, "unexpected texture list length");
  return true;
}

void CopyRect(const Sheet& sheet, TextureOverlay& ov, const RectEntry& r)
{
  const unsigned page = r.y / 1024, sy = r.y % 1024;
  unsigned w = r.w, h = r.h;
  // Level 0, then each mipmap level in its region (the same integer layout the hardware uses)
  for (int level = 0; level < 11 && w >= 2 && h >= 2; level++)
  {
    const unsigned srcX = mipXBase[level] + r.x / mipDivisor[level];
    const unsigned srcY = page * 1024 + mipYBase[level] + sy / mipDivisor[level];
    const unsigned dstX = mipXBase[level] + r.dx / mipDivisor[level];
    const unsigned dstY = mipYBase[level] + r.dy / mipDivisor[level];
    if (srcX + w > 2048 || srcY + h > 2048 || dstX + w > TextureOverlay::kWidth || dstY + h > TextureOverlay::kHeight)
      break;
    for (unsigned yy = 0; yy < h; yy++)
      std::memcpy(&ov.ram[size_t(dstY + yy) * TextureOverlay::kWidth + dstX], &sheet.texels[size_t(srcY + yy) * 2048 + srcX], w * sizeof(uint16_t));
    if (w < 4 || h < 4)
      break;
    w /= 2; h /= 2;
  }
}

}  // namespace

Result Build(const char* game, const uint8_t* crom, size_t cromSize, std::shared_ptr<const TextureOverlay>& overlay, std::string* why)
{
  overlay.reset();
  if (!game || std::strcmp(game, "scudplus"))
  {
    if (why) *why = "not Scud Race Plus";
    return Result::WrongGame;
  }
  if (!crom)
  {
    if (why) *why = "no CROM";
    return Result::BadRom;
  }
  Sheet sheet;
  if (!DecodeSuperBeginnerSet(Crom{crom, cromSize}, sheet, why))
    return Result::BadRom;

  auto ov = std::make_shared<TextureOverlay>();
  ov->ram.assign(size_t(TextureOverlay::kWidth) * TextureOverlay::kHeight, 0);
  for (const RectEntry& r : kRects)
  {
    // Rectangles outside the Super Beginner set come from texture lists every
    // course loads; they are left in texture RAM.
    if (!sheet.Covered(r.x, r.y, r.w, r.h))
      continue;
    CopyRect(sheet, *ov, r);
    ov->rects[TextureOverlay::Key(r.x, r.y, r.w, r.h)] = TextureOverlay::Position{ r.dx, r.dy };
  }
  for (uint32_t m : kModels_bus) ov->models.insert(m);
  for (uint32_t m : kModels_tank) ov->models.insert(m);
  for (uint32_t m : kModels_cat) ov->models.insert(m);
  for (uint32_t m : kModels_rocket) ov->models.insert(m);
  overlay = std::move(ov);
  if (why) why->clear();
  return Result::Built;
}

}  // namespace ScudPlusHiddenCars
