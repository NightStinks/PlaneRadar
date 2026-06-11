#pragma once
#include <Arduino.h>

#define CONFIG_VERSION 1

struct AppConfig {
    int   version;
    char  wifi_ssid[64];
    char  wifi_password[64];
    float home_lat;
    float home_lon;
    int   radius_nm;       // ADS-B fetch radius (nm) — use 50 for wide coverage
    int   screen_bearing;  // compass direction the display faces, 0-359
};

void config_reset(AppConfig &cfg);
bool config_load(AppConfig &cfg);
void config_save(const AppConfig &cfg);
void config_erase();
bool config_has_location(const AppConfig &cfg);
