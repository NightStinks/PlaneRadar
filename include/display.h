#pragma once
#include <LovyanGFX.hpp>

// Verify these pins against your ESP32-Plane-Radar wiring diagram
#define PIN_SCK   6
#define PIN_MOSI  7
#define PIN_CS   10
#define PIN_DC    2
#define PIN_RST   3
#define PIN_BL    1

void display_init();
lgfx::LGFX_Sprite* display_get_sprite();
void display_push();
