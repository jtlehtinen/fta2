#pragma once

#include <stdint.h>
#include <vector>

struct Color {
  uint8_t r, g, b, a;
};
static_assert(sizeof(Color) == 4);

// kPaletteSize is the number of colors stored in a single palette.
constexpr size_t kPaletteSize = 256;

struct Palette {
  Color colors[kPaletteSize];
};

struct Sprites {
};

struct Styles {
  std::vector<Palette> palettes;
};

void read_styles(const char* filename);
