#pragma once
#include <LovyanGFX.hpp>

#define PIN_SCK   4   // SCL
#define PIN_MOSI  3   // SDA
#define PIN_CS    1
#define PIN_DC   10
#define PIN_RST   0
#define PIN_BL   -1   // backlight wired to VCC directly — always on

void display_init();
lgfx::LGFX_Sprite* display_get_sprite();
void display_push();
