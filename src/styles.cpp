#include "styles.h"
#include "ext/string_hash.h"
#include "io.h"
#include "ext/stb_image_write.h"
#include <filesystem>
#include <format>
#include <vector>

constexpr uint32_t kNumColorsPerPalette = 256;
constexpr uint32_t kPalettesPerPalettePage = 64;
constexpr uint32_t kNumPhysicalPalettes = 16384;

enum SurfaceType : uint8_t {
  SurfaceType_Grass = 0,
  SurfaceType_RoadSpecial = 1,
  SurfaceType_Water = 2,
  SurfaceType_Electrified = 3,
  SurfaceType_ElectrifiedPlatform = 4,
  SurfaceType_WoodFloor = 5,
  SurfaceType_MetalFloor = 6,
  SurfaceType_MetalWall = 7,
  SurfaceType_GrassWall = 8,

  SurfaceType_Count
};

struct FontBase {
  uint16_t num_characters_per_font;
  std::vector<uint16_t> base;
};

struct SpriteDelta {
  uint16_t sprite;
  std::vector<uint16_t> sizes;
};

struct MapObject {
  uint8_t model;
  uint8_t sprites;
};

struct Sprite {
  uint32_t offset; // Offset relative to the start of the sprite graphics data
  uint8_t width;
  uint8_t height;
  uint16_t pad;
};

struct PaletteCounts {
  uint16_t tile;
  uint16_t sprite;
  uint16_t car_remap;
  uint16_t ped_remap;
  uint16_t code_object_remap;
  uint16_t map_object_remap;
  uint16_t user_remap;
  uint16_t font_remap;
};

struct DoorInfo {
  int8_t relativeX;           // X position relative to the center of the car.
  int8_t relativeY;           // Y position relative to the center of the car.
};

struct CarInfo {
  uint8_t model;              // Car model number.
  uint8_t sprite;             // Relative car sprite number.
  uint8_t width;              // Width of the car in pixels. Might be different than the sprite width (collision detection).
  uint8_t height;             // Height of the car in pixels. Might be different than the sprite height (collision detection).
  uint8_t num_remaps;
  uint8_t passengers;         // Number of passengers the car can carry.
  uint8_t wreck;              // Wreck graphic number to use when this car is wrecked (0-8, or 99 if can't wreck).
  uint8_t rating;             // Quality rating for this car used to decide how often it is created in different areas of the city.
  int8_t front_wheel_offset;    // Distance from the center of the car to the front axle.
  int8_t rear_wheel_offset;     // Distance from the center of the car to the back axle.
  int8_t front_window_offset;   // Distance from the center of the car to the front window.
  int8_t rear_window_offset;    // Distance from the center of the car to the back window.
  uint8_t info_flags;
  uint8_t info_flags2;
  std::vector<uint8_t> remap; // Virtual palette numbers, representing all of the alternative palettes which can sensibly be applied to this car. Note that these palette numbers are relative to the start of the car remap palette area.
  uint8_t num_doors;
  std::vector<DoorInfo> doors;
};

struct Surface {
  std::vector<uint16_t> tiles;
};

struct ChunkType {
  char name[4];
};

constexpr size_t kVirtualPaletteTableSize = 16384;

struct VirtualPaletteTable {
  uint16_t map[kVirtualPaletteTableSize]; // Virtual index to 'physical' index.
};
static_assert(sizeof(VirtualPaletteTable) == kVirtualPaletteTableSize * sizeof(uint16_t));

static VirtualPaletteTable read_virtual_palette_table(Reader& r) {
  VirtualPaletteTable result;
  r.read_many<uint16_t>(result.map, kVirtualPaletteTableSize);
  return result;
}

struct Color {
  uint8_t r, g, b, a;
};
static_assert(sizeof(Color) == 4);

// kPaletteSize is the number of colors stored in a single palette.
constexpr size_t kPhysicalPaletteSize = 256;

struct PhysicalPalette {
  Color colors[kPhysicalPaletteSize];
};

using PhysicalPalettes = std::vector<PhysicalPalette>;

static PhysicalPalettes read_physical_palettes(Reader& r, size_t chunk_size) {
  // Each page contains 64 palettes. Each palette
  // contains 256 dword colors. Color byte order
  // is BGRA.
  //
  // Within a page palettes are stored interleaved, i.e.
  // C0P0   - C0P1   - ... - C0P63
  // C1P0   - C1P1   - ... - C1P63
  // ...
  // C255P0 - C255P1 - ... - C255P63
  //
  // Where CiPi, is ith color of ith palette.

  auto convert = [](uint32_t color) {
    // @TODO: palette contains no alpha?
    return Color{
      .r = (color >> 16) & 0xff,
      .g = (color >>  8) & 0xff,
      .b = (color >>  0) & 0xff,
      .a = 0xff,
    };
  };

  constexpr size_t kPageSize = 64;
  size_t count = chunk_size / sizeof(PhysicalPalette);
  size_t pages = count / kPageSize;

  PhysicalPalettes result(count);

  for (size_t page = 0; page < pages; ++page) {
    for (size_t color = 0; color < kPhysicalPaletteSize; ++color) {
      for (size_t palette = 0; palette < kPageSize; ++palette) {
        size_t idx = page * kPageSize + palette;
        result[idx].colors[color] = convert(r.read<uint32_t>());
      }
    }
  }

  return result;
}

constexpr size_t kTileDim = 64;

struct Tile {
  uint8_t colors[kTileDim * kTileDim];
};

using Tiles = std::vector<Tile>;

static Tiles read_tiles(Reader& r, size_t chunk_size) {
  constexpr size_t kPageDimPixels = 256;
  constexpr size_t kPageDimTiles = kPageDimPixels / kTileDim;
  const size_t count = chunk_size / sizeof(Tile);

  const uint8_t* data = r.get_ptr<uint8_t>();
  r.skip(chunk_size);

  Tiles result(count);
  for (size_t tile = 0; tile < count; ++tile) {
    for (size_t y = 0; y < kTileDim; ++y) {
      for (size_t x = 0; x < kTileDim; ++x) {
        size_t row = tile / kPageDimTiles;
        size_t col = tile % kPageDimTiles;

        size_t idx = x + col * kTileDim + (y + row * kTileDim) * kPageDimPixels;
        result[tile].colors[x + y * kTileDim] = data[idx];
      }
    }
  }
  return result;
}

struct SpriteBase {
  uint16_t offset;
  uint16_t count;
};

struct SpriteBases {
  SpriteBase car;
  SpriteBase ped;
  SpriteBase code; // code object
  SpriteBase map;  // map object
  SpriteBase user;
  SpriteBase font;
};

static SpriteBases read_sprite_bases(Reader& r) {
  struct {
    uint16_t car;
    uint16_t ped;
    uint16_t code; // code object
    uint16_t map;  // map object
    uint16_t user;
    uint16_t font;
  } counts;

  counts.car  = r.read<uint16_t>();
  counts.ped  = r.read<uint16_t>();
  counts.code = r.read<uint16_t>();
  counts.map  = r.read<uint16_t>();
  counts.user = r.read<uint16_t>();
  counts.font = r.read<uint16_t>();

  uint16_t offset = 0;
  auto next_base = [&offset](uint16_t count) {
    SpriteBase base = {.offset = offset, .count = count};
    offset += count;
    return base;
  };

  SpriteBases result = { };
  result.car  = next_base(counts.car);
  result.ped  = next_base(counts.ped);
  result.code = next_base(counts.code);
  result.map  = next_base(counts.map);
  result.user = next_base(counts.user);
  result.font = next_base(counts.font);
  return result;
}

struct PaletteBase {
  uint16_t offset;
  uint16_t count;
};

struct PaletteBases {
  PaletteBase tile;
  PaletteBase sprite;
  PaletteBase car;  // car remap
  PaletteBase ped;  // ped remap
  PaletteBase code; // code object remap
  PaletteBase map;  // map object remap
  PaletteBase user; // user remap
  PaletteBase font; // font remap
};

static PaletteBases read_palette_bases(Reader& r) {
  struct {
    uint16_t tile;
    uint16_t sprite;
    uint16_t car;  // car remap
    uint16_t ped;  // ped remap
    uint16_t code; // code object remap
    uint16_t map;  // map object remap
    uint16_t user; // user remap
    uint16_t font; // font remap
  } counts;

  counts.tile    = r.read<uint16_t>();
  counts.sprite  = r.read<uint16_t>();
  counts.car     = r.read<uint16_t>();
  counts.ped     = r.read<uint16_t>();
  counts.code    = r.read<uint16_t>();
  counts.map     = r.read<uint16_t>();
  counts.user    = r.read<uint16_t>();
  counts.font    = r.read<uint16_t>();

  uint16_t offset = 0;
  auto next_base = [&offset](uint16_t count) {
    PaletteBase base = {.offset = offset, .count = count};
    offset += count;
    return base;
  };

  PaletteBases result = { };
  result.tile    = next_base(counts.tile);
  result.sprite  = next_base(counts.sprite);
  result.car     = next_base(counts.car);
  result.ped     = next_base(counts.ped);
  result.code    = next_base(counts.code);
  result.map     = next_base(counts.map);
  result.user    = next_base(counts.user);
  result.font    = next_base(counts.font);
  return result;
}

void read_styles(const char* filename) {
  File f;
  if (!f.open("../../../../data/wil.sty")) {
    return;
  }

  auto fsize = f.size();
  std::vector<uint8_t> buf(fsize);
  if (!f.read(buf.data(), fsize)) {
    return;
  }
  f.close();

  Reader r(buf.data(), buf.size());

  char magic[4];
  r.read_many<char>(magic, 4);
  if (memcmp(magic, "GBST", 4) != 0) {
    // @TODO: Error not a valid GBH style file.
    return;
  }

  r.skip(sizeof(uint16_t)); // skip version

  FontBase font_base;
  std::vector<SpriteDelta> sprite_deltas;
  std::vector<uint8_t> delta_store;
  std::vector<MapObject> object_infos;
  std::vector<uint8_t> recyclable_cars;
  std::vector<uint8_t> sprite_data_store;
  std::vector<Sprite> sprites;
  std::vector<CarInfo> cars;
  Surface surfaces[SurfaceType_Count];

  VirtualPaletteTable vtable;
  PhysicalPalettes palettes;
  PaletteBases palette_bases;
  SpriteBases sprite_bases;
  Tiles tiles;

  while (!r.done()) {
    auto chunk_type = r.read<ChunkType>();
    auto chunk_size = r.read<uint32_t>();

    switch (shash(chunk_type.name, 4).value()) {
      case shash("PALX").value(): {
        assert(chunk_size == sizeof(VirtualPaletteTable));
        vtable = read_virtual_palette_table(r);
        break;
      }

      case shash("PPAL").value():
        assert(chunk_size % sizeof(PhysicalPalette) == 0);
        palettes = read_physical_palettes(r, chunk_size);
        break;

      case shash("PALB").value():
        assert(chunk_size == 16);
        palette_bases = read_palette_bases(r);
        break;

      case shash("SPRB").value():
        assert(chunk_size == 12);
        sprite_bases = read_sprite_bases(r);
        break;

      case shash("TILE").value():
        tiles = read_tiles(r, chunk_size);
        break;

      case shash("SPRG").value(): // Sprite graphics
        sprite_data_store.resize(chunk_size);
        r.read_many<uint8_t>(sprite_data_store.data(), chunk_size);
        break;

      case shash("SPRX").value(): { // Sprite index
        assert(chunk_size % sizeof(Sprite) == 0);
        size_t count = chunk_size / sizeof(Sprite);
        sprites.resize(count);
        r.read_many<Sprite>(sprites.data(), count);
        break;
      }

      case shash("DELS").value(): // Delta store
        delta_store.resize(chunk_size);
        r.read_many<uint8_t>(delta_store.data(), chunk_size);
        break;

      case shash("DELX").value(): { // Delta index
        size_t bytes = 0;
        while (bytes < chunk_size) {
          SpriteDelta delta;
          delta.sprite = r.read<uint16_t>();
          uint8_t count = r.read<uint8_t>();
          r.skip(sizeof(uint8_t));

          for (size_t i = 0; i < count; ++i) {
            delta.sizes.push_back(r.read<uint16_t>());
          }

          sprite_deltas.push_back(delta);
          bytes += 4 + count * sizeof(uint16_t);
        }
        break;
      }

      case shash("FONB").value(): {  // Font base
        uint16_t count = r.read<uint16_t>();
        font_base.num_characters_per_font = count;
        font_base.base.resize(count);
        r.read_many<uint16_t>(font_base.base.data(), count);
        break;
      }

      case shash("CARI").value(): { // Car info
        size_t bytes = 0;
        while (bytes < chunk_size) {
          CarInfo info = { };
          info.model = r.read<uint8_t>();
          info.sprite = r.read<uint8_t>();
          info.width = r.read<uint8_t>();
          info.height = r.read<uint8_t>();
          info.num_remaps = r.read<uint8_t>();
          info.passengers = r.read<uint8_t>();
          info.wreck = r.read<uint8_t>();
          info.rating = r.read<uint8_t>();
          info.front_wheel_offset = r.read<int8_t>();
          info.rear_wheel_offset = r.read<int8_t>();
          info.front_window_offset = r.read<int8_t>();
          info.rear_window_offset = r.read<int8_t>();
          info.info_flags = r.read<uint8_t>();
          info.info_flags2 = r.read<uint8_t>();

          info.remap.resize(info.num_remaps);
          r.read_many<uint8_t>(info.remap.data(), info.num_remaps);

          info.num_doors = r.read<uint8_t>();

          info.doors.resize(info.num_doors);
          r.read_many<DoorInfo>(info.doors.data(), info.num_doors);

          cars.push_back(info);

          bytes += 15 + info.num_remaps * sizeof(uint8_t) + info.num_doors * sizeof(DoorInfo);
        }
        break;
      }

      case shash("OBJI").value(): { // Map object info
        assert(chunk_size % sizeof(MapObject) == 0);
        size_t count = chunk_size / sizeof(MapObject);

        object_infos.resize(count);
        r.read_many<MapObject>(object_infos.data(), count);
        break;
      }

      case shash("PSXT").value(): // PSX tiles
        assert(!"TODO");
        r.skip(chunk_size);
        break;

      case shash("RECY").value(): // Car recycling info
        assert(chunk_size <= 64);

        for (size_t i = 0; i < chunk_size; ++i) {
          uint8_t value = r.read<uint8_t>();
          if (value == 255) break;

          recyclable_cars.push_back(value);
        }
        break;

      case shash("SPEC").value(): { // Spec... surface behavior
        uint8_t type = 0;
        size_t bytes = 0;

        while (type < SurfaceType_Count && bytes < chunk_size) {
          Surface surface;

          while (bytes < chunk_size) {
            uint16_t value = r.read<uint16_t>();
            bytes += sizeof(uint16_t);
            if (value == 0) break;

            surface.tiles.push_back(value);
          }

          surfaces[type] = surface;
          ++type;
        }
        break;
      }

      default:
        // @TODO: Error unknown chunk type...
        break;
    }
  }

  { // Dump palettes
    int width = static_cast<int>(kPhysicalPaletteSize);
    int height = static_cast<int>(palettes.size());
    stbi_write_png("palettes.png", width, height, 4, palettes.data(), sizeof(PhysicalPalette));
  }

  { // Dump tiles
    if (!std::filesystem::exists("tiles")) {
      std::filesystem::create_directory("tiles");
    }

    std::vector<Color> buf(kTileDim * kTileDim);

    size_t virtual_palette_index = palette_bases.tile.offset;

    for (const auto& tile : tiles) {
      size_t physical_palette_index = vtable.map[virtual_palette_index];
      auto& palette = palettes[physical_palette_index];

      for (size_t i = 0; i < kTileDim * kTileDim; ++i) {
        uint8_t color_index = tile.colors[i];
        buf[i] = palette.colors[color_index];
      }

      auto filename = std::format("tiles/tile{}.png", virtual_palette_index);
      stbi_write_png(filename.c_str(), kTileDim, kTileDim, 4, buf.data(), kTileDim * sizeof(Color));

      virtual_palette_index++;
    }
  }

  { // Dump sprites
    if (!std::filesystem::exists("sprites")) {
      std::filesystem::create_directory("sprites");
    }
  }
}
