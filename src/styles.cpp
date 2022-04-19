#include "styles.h"
#include "io.h"
#include "ext/string_hash.h"

struct ChunkType {
  char name[4];
};

constexpr size_t kVirtualPaletteTableSize = 16384;

struct VirtualPaletteTable {
  uint16_t map[kVirtualPaletteTableSize]; // Virtual index to 'physical' index.
};
static_assert(sizeof(VirtualPaletteTable) == kVirtualPaletteTableSize * sizeof(uint16_t));

static VirtualPaletteTable read_virtual_palette_table(Reader& r, size_t chunk_size) {
  assert(chunk_size == sizeof(VirtualPaletteTable));

  VirtualPaletteTable result;
  r.read_many<uint16_t>(result.map, kVirtualPaletteTableSize);
  return result;
}

// kPaletteSize is the number of colors stored in a single palette.
constexpr size_t kPhysicalPaletteSize = 256;

struct PhysicalPalette {
  Color colors[kPhysicalPaletteSize];
};

using PhysicalPalettes = std::vector<PhysicalPalette>;

static PhysicalPalettes read_physical_palettes(Reader& r, size_t chunk_size) {
  assert(chunk_size % sizeof(PhysicalPalette) == 0);

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

using SpriteStore = std::vector<uint8_t>;

static SpriteStore read_sprite_store(Reader& r, size_t chunk_size) {
  SpriteStore result(chunk_size);
  r.read_many<uint8_t>(result.data(), chunk_size);
  return result;
}

struct GTASprite {
  uint32_t offset; // sprite store offset
  uint8_t width;
  uint8_t height;
};

using GTASprites = std::vector<GTASprite>;

static GTASprites read_sprites(Reader& r, size_t chunk_size) {
  assert(chunk_size % 8 == 0);

  struct GTASpriteTransfer {
    uint32_t offset; // sprite store offset
    uint8_t  width;
    uint8_t  height;
    uint16_t pad;
  };

  size_t count = chunk_size / sizeof(GTASpriteTransfer);

  GTASprites result(count);

  for (size_t i = 0; i < count; ++i) {
    auto s = r.read<GTASpriteTransfer>();
    result[i] = GTASprite{
      .offset = s.offset,
      .width = s.width,
      .height = s.height,
    };
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

static SpriteBases read_sprite_bases(Reader& r, size_t chunk_size) {
  assert(chunk_size == 12);

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

static PaletteBases read_palette_bases(Reader& r, size_t chunk_size) {
  assert(chunk_size == 16);

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

using CarModelNumber = uint8_t;

std::vector<CarModelNumber> read_recyclable_cars(Reader& r, size_t chunk_size) {
  assert(chunk_size <= 64);

  constexpr size_t kMaxCars = 64;

  std::vector<CarModelNumber> result;
  for (size_t i = 0; i < kMaxCars; ++i) {
    auto value = r.read<CarModelNumber>();
    if (value == 255) break;

    result.push_back(value);
  }
  return result;
}

using DeltaStore = std::vector<uint8_t>;

static DeltaStore read_delta_store(Reader& r, size_t chunk_size) {
  DeltaStore result(chunk_size);
  r.read_many<uint8_t>(result.data(), chunk_size);
  return result;
}

// DeltaSet uses the same palette as the sprite.
struct DeltaSet {
  uint16_t sprite; // sprite number
  std::vector<uint16_t> sizes; // size in bytes of each of the deltas in this set
};

using Deltas = std::vector<DeltaSet>;

static Deltas read_deltas(Reader& r, size_t chunk_size) {
  Deltas result;

  size_t bytes = 0;
  while (bytes < chunk_size) {
    auto sprite = r.read<uint16_t>();
    auto count = r.read<uint8_t>();
    r.skip(sizeof(uint8_t));

    DeltaSet set = {.sprite = sprite};
    set.sizes.resize(count);

    for (size_t i = 0; i < count; ++i) {
      set.sizes[i] = r.read<uint16_t>();
    }

    result.push_back(set);

    bytes += 4 + count * sizeof(uint16_t);
  }

  return result;
}

struct FontBase {
  uint16_t offset;
  uint16_t count;
};

using FontBases = std::vector<FontBase>;

static FontBases read_font_bases(Reader& r, size_t chunk_size) {
  uint16_t count = r.read<uint16_t>();

  std::vector<uint16_t> counts(count);
  r.read_many<uint16_t>(counts.data(), count);

  uint16_t offset = 0;
  auto next_base = [&offset](uint16_t count) {
    FontBase base = {.offset = offset, .count = count};
    offset += count;
    return base;
  };

  FontBases result(count);
  for (size_t i = 0; i < count; ++i) {
    result[i] = next_base(counts[i]);
  }
  return result;
}

struct MapObject {
  uint8_t model; // object model number
  uint8_t sprites; // number of sprites stored for this model
};

using MapObjects = std::vector<MapObject>;

static MapObjects read_map_objects(Reader& r, size_t chunk_size) {
  assert(chunk_size % sizeof(MapObject) == 0);
  size_t count = chunk_size / sizeof(MapObject);

  MapObjects result(count);
  r.read_many<MapObject>(result.data(), count);
  return result;
}

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

using SurfaceTiles = std::vector<uint16_t>;
using Surfaces = std::vector<SurfaceTiles>;

static Surfaces read_surface_tiles(Reader& r, size_t chunk_size) {
  uint8_t type = 0;
  size_t bytes = 0;

  Surfaces result(SurfaceType_Count);

  while (type < SurfaceType_Count && bytes < chunk_size) {
    SurfaceTiles tiles;

    while (bytes < chunk_size) {
      uint16_t value = r.read<uint16_t>();
      bytes += sizeof(uint16_t);
      if (value == 0) break;

      tiles.push_back(value);
    }

    result[type] = tiles;
    ++type;
  }

  return result;
}

struct Door {
  int8_t relativeX;           // X position relative to the center of the car.
  int8_t relativeY;           // Y position relative to the center of the car.
};

struct Car {
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
  std::vector<Door> doors;
};

using Cars = std::vector<Car>;

static Cars read_cars(Reader& r, size_t chunk_size) {
  Cars result;

  size_t bytes = 0;
  while (bytes < chunk_size) {
    Car car = { };
    car.model = r.read<uint8_t>();
    car.sprite = r.read<uint8_t>();
    car.width = r.read<uint8_t>();
    car.height = r.read<uint8_t>();
    car.num_remaps = r.read<uint8_t>();
    car.passengers = r.read<uint8_t>();
    car.wreck = r.read<uint8_t>();
    car.rating = r.read<uint8_t>();
    car.front_wheel_offset = r.read<int8_t>();
    car.rear_wheel_offset = r.read<int8_t>();
    car.front_window_offset = r.read<int8_t>();
    car.rear_window_offset = r.read<int8_t>();
    car.info_flags = r.read<uint8_t>();
    car.info_flags2 = r.read<uint8_t>();

    car.remap.resize(car.num_remaps);
    r.read_many<uint8_t>(car.remap.data(), car.num_remaps);

    car.num_doors = r.read<uint8_t>();

    car.doors.resize(car.num_doors);
    r.read_many<Door>(car.doors.data(), car.num_doors);

    result.push_back(car);

    bytes += 15 + car.num_remaps * sizeof(uint8_t) + car.num_doors * sizeof(Door);
  }

  return result;
}

struct DeltaStoreEntry {
  uint16_t offset;
  uint8_t length;
  #pragma warning(suppress:4200)
  uint8_t data[];
};

static std::vector<Sprite> import_sprites(
  const SpriteStore& store,
  const GTASprites& sprites,
  const PaletteBases& palette_bases,
  const VirtualPaletteTable& vtable,
  const PhysicalPalettes& palettes)
{
  constexpr size_t kPageSize = 256;

  std::vector<Sprite> result(sprites.size());

  for (size_t i = 0; i < sprites.size(); ++i) {
    auto& src = sprites[i];
    auto& dest = result[i];

    dest.width = src.width;
    dest.height = src.height;
    dest.pixels.resize(src.width * src.height);

    size_t virtual_palette_index = palette_bases.sprite.offset + i;
    size_t physical_palette_index = vtable.map[virtual_palette_index];
    auto& palette = palettes[physical_palette_index];

    size_t xoffset = src.offset % kPageSize;
    size_t yoffset = src.offset / kPageSize;

    for (size_t y = 0; y < src.height; ++y) {
      for (size_t x = 0; x < src.width; ++x) {
        auto color_index = store[x + xoffset + (y + yoffset) * kPageSize];
        dest.pixels[x + y * src.width] = palette.colors[color_index];
      }
    }
  }

  return result;
}

static std::vector<Sprite> import_tiles(
  const Tiles& tiles,
  const PaletteBases& palette_bases,
  const VirtualPaletteTable& vtable,
  const PhysicalPalettes& palettes)
{
  std::vector<Sprite> result(tiles.size());

  for (size_t i = 0; i < tiles.size(); ++i) {
    auto& src = tiles[i];
    auto& dest = result[i];

    dest.width = kTileDim;
    dest.height = kTileDim;
    dest.pixels.resize(kTileDim * kTileDim);

    size_t virtual_palette_index = palette_bases.tile.offset + i;
    size_t physical_palette_index = vtable.map[virtual_palette_index];
    auto& palette = palettes[physical_palette_index];

    for (size_t px = 0; px < kTileDim * kTileDim; ++px) {
      auto color_index = src.colors[px];
      dest.pixels[px] = palette.colors[color_index];
    }
  }

  return result;
}

static std::vector<Sprite> import_deltas(
  const std::vector<Sprite>& sprites,
  const DeltaStore& store,
  const Deltas& deltas,
  const PaletteBases& palette_bases,
  const VirtualPaletteTable& vtable,
  const PhysicalPalettes& palettes)
{
  std::vector<Sprite> result;

  size_t store_offset = 0;

  for (auto& set : deltas) {
    size_t virtual_palette_index = palette_bases.sprite.offset + set.sprite;
    size_t physical_palette_index = vtable.map[virtual_palette_index];
    auto& palette = palettes[physical_palette_index];

    size_t delta_index = 0;
    for (auto size : set.sizes) {
      auto sprite = sprites[set.sprite];

      size_t bytes = 0;
      uint32_t position = 0;

      while (bytes < size) {
        auto entry = reinterpret_cast<const DeltaStoreEntry*>(store.data() + store_offset);

        position += entry->offset;
        size_t x = position % 256;
        size_t y = position / 256;
        position += entry->length;

        for (size_t j = 0; j < entry->length; ++j) {
          auto color_index = entry->data[j];
          sprite.pixels[x + j + y * sprite.width] = palette.colors[color_index];
        }

        bytes += 3 + entry->length;
        store_offset += 3 + entry->length;
      }

      result.push_back(sprite);
    }
  }

  return result;
}

static std::vector<uint16_t> import_delta_sprites(const Deltas& deltas) {
  std::vector<uint16_t> result;

 for (auto& d : deltas) {
   for (auto _ : d.sizes) {
     result.push_back(d.sprite);
   }
 }

  return result;
}

bool Styles::load(const char* filename) {
  File f;
  if (!f.open("../../../../data/wil.sty")) {
    return false;
  }

  auto fsize = f.size();
  std::vector<uint8_t> buf(fsize);
  if (!f.read(buf.data(), fsize)) {
    return false;
  }
  f.close();

  Reader r(buf.data(), buf.size());

  char magic[4];
  r.read_many<char>(magic, 4);
  if (memcmp(magic, "GBST", 4) != 0) {
    // @TODO: Error not a valid GBH style file.
    return false;
  }

  r.skip(sizeof(uint16_t)); // skip version

  VirtualPaletteTable vtable;
  PhysicalPalettes palettes;
  PaletteBases palette_bases;
  SpriteBases sprite_bases;
  SpriteStore sprite_store;
  GTASprites sprites;
  DeltaStore delta_store;
  Deltas deltas;
  Tiles tiles;
  FontBases font_bases;
  MapObjects map_objects;
  Surfaces surfaces;
  std::vector<CarModelNumber> recyclable_cars;
  Cars cars;

  while (!r.done()) {
    auto chunk_type = r.read<ChunkType>();
    auto chunk_size = r.read<uint32_t>();

    switch (shash(chunk_type.name, 4).value()) {
      case shash("PALX").value():
        vtable = read_virtual_palette_table(r, chunk_size);
        break;

      case shash("PPAL").value():
        palettes = read_physical_palettes(r, chunk_size);
        break;

      case shash("PALB").value():
        palette_bases = read_palette_bases(r, chunk_size);
        break;

      case shash("SPRB").value():
        sprite_bases = read_sprite_bases(r, chunk_size);
        break;

      case shash("TILE").value():
        tiles = read_tiles(r, chunk_size);
        break;

      case shash("SPRG").value():
        sprite_store = read_sprite_store(r, chunk_size);
        break;

      case shash("SPRX").value():
        sprites = read_sprites(r, chunk_size);
        break;

      case shash("DELS").value():
        delta_store = read_delta_store(r, chunk_size);
        break;

      case shash("DELX").value():
        deltas = read_deltas(r, chunk_size);
        break;

      case shash("FONB").value():
        font_bases = read_font_bases(r, chunk_size);
        break;

      case shash("CARI").value():
        cars = read_cars(r, chunk_size);
        break;

      case shash("OBJI").value():
        map_objects = read_map_objects(r, chunk_size);
        break;

      case shash("PSXT").value(): // PSX tiles
        assert(!"TODO");
        r.skip(chunk_size);
        break;

      case shash("RECY").value():
        recyclable_cars = read_recyclable_cars(r, chunk_size);
        break;

      case shash("SPEC").value():
        surfaces = read_surface_tiles(r, chunk_size);
        break;

      default:
        // @TODO: Error unknown chunk type...
        break;
    }
  }

  this->sprites = import_sprites(sprite_store, sprites, palette_bases, vtable, palettes);
  this->tiles = import_tiles(tiles, palette_bases, vtable, palettes);
  this->deltas = import_deltas(this->sprites, delta_store, deltas, palette_bases, vtable, palettes);
  this->delta_sprites = import_delta_sprites(deltas);

  return true;
}
