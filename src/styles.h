#pragma once

#include <stdint.h>
#include <vector>

struct Color {
  uint8_t r, g, b, a;
};
static_assert(sizeof(Color) == 4);

struct Sprite {
  std::vector<Color> pixels;
  uint32_t width;
  uint32_t height;
};

struct Styles {
  std::vector<Sprite> sprites;
  std::vector<Sprite> tiles;

  std::vector<Sprite> deltas;
  std::vector<uint16_t> delta_sprites; // @TODO: better name, delta to which sprite the delta applies to

  bool load(const char* filename);
};
