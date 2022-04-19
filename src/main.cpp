#include "styles.h"
#include "ext/stb_image_write.h"
#include <Windows.h>
#include <assert.h>
#include <filesystem>
#include <format>
#include <string>

LRESULT CALLBACK Win32WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  LRESULT result = 0;

  switch (message) {
    case WM_CLOSE:
      DestroyWindow(window);
      break;

    case WM_DESTROY:
      PostQuitMessage(0);
      break;

    case WM_PAINT: {
      RECT cr;
      GetClientRect(window, &cr);

      int width = cr.right - cr.left;
      int height = cr.bottom - cr.top;

      PAINTSTRUCT ps;
      HDC dc = BeginPaint(window, &ps);
      PatBlt(dc, 0, 0, width, height, BLACKNESS);
      EndPaint(window, &ps);
      break;
    }

    default:
      result = DefWindowProcW(window, message, wparam, lparam);
      break;
  }

  return result;
}

#if 0
auto create_sprite_filename = [](SpriteBases& bases, size_t sprite_index) {
  if (sprite_index >= bases.car.offset && sprite_index < bases.ped.offset) {
    return std::format("sprites/car_{}.png", sprite_index - bases.car.offset);
  }

  if (sprite_index >= bases.ped.offset && sprite_index < bases.code.offset) {
    return std::format("sprites/ped_{}.png", sprite_index - bases.ped.offset);
  }

  if (sprite_index >= bases.code.offset && sprite_index < bases.map.offset) {
    return std::format("sprites/code_{}.png", sprite_index - bases.code.offset);
  }

  if (sprite_index >= bases.map.offset && sprite_index < bases.user.offset) {
    return std::format("sprites/map_{}.png", sprite_index - bases.map.offset);
  }

  if (sprite_index >= bases.user.offset && sprite_index < bases.font.offset) {
    return std::format("sprites/user_{}.png", sprite_index - bases.user.offset);
  }

  return std::format("sprites/font_{}.png", sprite_index - bases.font.offset);
};
#endif

static void dump_sprites(const std::vector<Sprite>& sprites) {
  std::filesystem::create_directory("sprites");

  for (size_t i = 0; i < sprites.size(); ++i) {
    auto& s = sprites[i];
    auto filename = std::format("sprites/sprite_{}.png", i);
    stbi_write_png(filename.c_str(), s.width, s.height, 4, s.pixels.data(), 4 * s.width);
  }
}

static void dump_tiles(const std::vector<Sprite>& tiles) {
  std::filesystem::create_directory("tiles");

  for (size_t i = 0; i < tiles.size(); ++i) {
    auto& s = tiles[i];
    auto filename = std::format("tiles/tile_{}.png", i);
    stbi_write_png(filename.c_str(), s.width, s.height, 4, s.pixels.data(), 4 * s.width);
  }
}

static void dump_deltas(const std::vector<Sprite>& deltas, const std::vector<uint16_t>& delta_sprites) {
  assert(deltas.size() == delta_sprites.size());

  std::filesystem::create_directory("deltas");

  size_t delta_index = 0;
  size_t delta_sprite = 0;

  for (size_t i = 0; i < deltas.size(); ++i) {
    if (delta_sprite != delta_sprites[i]) {
      delta_sprite = delta_sprites[i];
      delta_index = 0;
    }

    auto& s = deltas[i];
    auto filename = std::format("deltas/delta_{}_{}.png", delta_sprite, delta_index++);
    stbi_write_png(filename.c_str(), s.width, s.height, 4, s.pixels.data(), 4 * s.width);
  }
}

int CALLBACK wWinMain(HINSTANCE instance, HINSTANCE previousInstance, LPWSTR cmdLine, int showCode) {
  wchar_t exe[MAX_PATH] = { };
  DWORD len = GetModuleFileNameW(nullptr, exe, static_cast<DWORD>(std::size(exe)));
  while (len > 0) {
    if (exe[--len] == '\\')
      break;
  }
  exe[len] = 0;
  SetCurrentDirectoryW(exe);

  Styles styles;
  if (styles.load("../../../../data/wil.sty")) {
    //dump_sprites(styles.sprites);
    //dump_tiles(styles.tiles);
    dump_deltas(styles.deltas, styles.delta_sprites);
  }

#if 0
  WNDCLASSW wc = {};
  wc.hInstance = instance;
  wc.hIcon = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(1));
  wc.lpfnWndProc = Win32WindowProc;
  wc.lpszClassName = L"clazz";
  RegisterClassW(&wc);

  HWND window = CreateWindowW(L"clazz", L"fta2", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1280, 720, nullptr, nullptr, instance, nullptr);
  if (window) {
    ShowWindow(window, SW_SHOW);

    MSG msg = { };
    while (msg.message != WM_QUIT) {
      if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
      }
    }
  }
#endif

  return 0;
}
