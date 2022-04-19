#include "styles.h"

#include <vector>

#include "ext/string_hash.h"
#include "io.h"

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

struct PaletteIndex {
  uint16_t physical_palette[16384];  // Mapping of a virtual palette number to physical palette number.
};

struct Palette {
  uint32_t colors[256];
};

struct PalettedTile {
  uint8_t color_indices[64 * 64];  // tile width * tile height
};

struct SpriteCounts {
  uint16_t car;
  uint16_t ped;
  uint16_t code_object;
  uint16_t map_object;
  uint16_t user;
  uint16_t font;
};

struct FontBase {
  uint16_t num_characters_per_font;
  std::vector<uint16_t> base;
};

struct SpriteDelta {
  uint16_t sprite;
  std::vector<uint16_t> sizes;
};

struct ObjectInfo {
    uint8_t model;      // Object model number.
    uint8_t sprites;    // Number of sprites stored for this model.
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

struct StyleFileHeader {
  char file_type[4];
  uint16_t version;
};

struct ChunkType {
  char name[4];
};

void read_styles(const char* filename) {
  File f;
  if (!f.open("../../../../data/wil.sty")) {
    return;
  }

  auto size = f.size();

  std::vector<uint8_t> buf(size);
  f.read(buf.data(), size);
  f.close();

  Reader r(buf.data(), buf.size());

  auto header = r.read<StyleFileHeader>();
  if (memcmp(header.file_type, "GBST", 4) != 0) {
    // @TODO: Error not a valid GBH style file.
    return;
  }

  PaletteIndex palette_index;
  std::vector<Palette> palettes;
  std::vector<PalettedTile> paletted_tiles;
  SpriteCounts sprite_counts;
  FontBase font_base;
  std::vector<SpriteDelta> sprite_deltas;
  std::vector<uint8_t> delta_store;
  std::vector<ObjectInfo> object_infos;
  std::vector<uint8_t> recyclable_cars;
  std::vector<uint8_t> sprite_data_store;
  std::vector<Sprite> sprites;
  PaletteCounts palette_counts;
  std::vector<CarInfo> cars;
  Surface surfaces[SurfaceType_Count];

  while (!r.done()) {
    auto type = r.read<ChunkType>();
    auto size = r.read<uint32_t>();

    switch (shash(type.name, 4).value()) {
      case shash("PALX").value(): {  // Palette index
        assert(size == sizeof(PaletteIndex));
        r.read_many<uint16_t>(palette_index.physical_palette, 16384);
        break;
      }

      case shash("PPAL").value(): {  // Physical palettes
        // page := 64 palettes
        // palette := 256 dword colors
        // color := byte order BGRA
        //
        // Within a page palettes are stored interleaved, i.e.
        // C0P0   - C0P1   - ... - C0P63
        // C1P0   - C1P1   - ... - C1P63
        // ...
        // C255P0 - C255P1 - ... - C255P63

        assert(size % sizeof(Palette) == 0);

        size_t count = size / sizeof(Palette);
        size_t pages = count / 64;

        palettes.resize(count);

        for (size_t page = 0; page < pages; ++page) {
          for (size_t color = 0; color < 256; ++color) {
            for (size_t palette = 0; palette < 64; ++palette) {
              size_t palette_index = page * 64 + palette;
              palettes[palette_index].colors[color] = r.read<uint32_t>();  // @TODO: Convert to RGBA
            }
          }
        }
        break;
      }

      case shash("PALB").value():  // Palette base
        assert(size == sizeof(PaletteCounts));
        palette_counts = r.read<PaletteCounts>();
        break;

      case shash("TILE").value(): {  // Tiles
        // tile size := 64x64 uint8_t
        // 992 tiles in 63 pages
        // page := 256x256 pixels
        // virtual palette number = tile number + tile palette base

        constexpr size_t tile_width = 64;
        constexpr size_t tile_height = 64;
        constexpr size_t page_width_in_pixels = 256;
        constexpr size_t page_height_in_pixels = 256;
        constexpr size_t page_width_in_tiles = page_width_in_pixels / tile_width;
        constexpr size_t page_height_in_tiles = page_height_in_pixels / tile_height;

        size_t count = size / (tile_width * tile_height);
        paletted_tiles.resize(count);

        const uint8_t* data = r.get_ptr<uint8_t>();
        r.skip(size);

        for (size_t tile_index = 0; tile_index < count; ++tile_index) {
          auto& tile = paletted_tiles[tile_index];

          for (size_t y = 0; y < 64; ++y) {
            for (size_t x = 0; x < 64; ++x) {
              // All tiles are 64x64. Tiles grouped into pages. Each page is 256 x 256 pixels.
              size_t tile_row = tile_index / 4;  // tile page height in tiles := 4
              size_t tile_col = tile_index % 4;  // tile page width in tiles := 4

              size_t idx = x + tile_col * tile_width + (y + tile_row * tile_height) * page_width_in_pixels;
              tile.color_indices[x + y * tile_width] = data[idx];
            }
          }
        }
        break;
      }

      case shash("SPRG").value(): // Sprite graphics
        sprite_data_store.resize(size);
        r.read_many<uint8_t>(sprite_data_store.data(), size);
        break;

      case shash("SPRX").value(): { // Sprite index
        assert(size % sizeof(Sprite) == 0);
        size_t count = size / sizeof(Sprite);
        sprites.resize(count);
        r.read_many<Sprite>(sprites.data(), count);
        break;
      }

      case shash("SPRB").value(): { // Sprite base index
        assert(size == sizeof(SpriteCounts));
        sprite_counts = r.read<SpriteCounts>();
        break;
      }

      case shash("DELS").value(): // Delta store
        delta_store.resize(size);
        r.read_many<uint8_t>(delta_store.data(), size);
        break;

      case shash("DELX").value(): { // Delta index
        size_t bytes = 0;
        while (bytes < size) {
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
        while (bytes < size) {
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
        assert(size % sizeof(ObjectInfo) == 0);
        size_t count = size / sizeof(ObjectInfo);

        object_infos.resize(count);
        r.read_many<ObjectInfo>(object_infos.data(), count);
        break;
      }

      case shash("PSXT").value(): // PSX tiles
        break;

      case shash("RECY").value(): // Car recycling info
        assert(size <= 64);

        for (size_t i = 0; i < size; ++i) {
          uint8_t value = r.read<uint8_t>();
          if (value == 255) break;

          recyclable_cars.push_back(value);
        }
        break;

      case shash("SPEC").value(): { // Spec... surface behavior
        uint8_t type = 0;
        size_t bytes = 0;

        while (type < SurfaceType_Count && bytes < size) {
          Surface surface;

          while (bytes < size) {
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
}
