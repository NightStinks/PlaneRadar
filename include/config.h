#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

#define CONFIG_VERSION 1

struct AppConfig {
    int   version;
    char  wifi_ssid[64];
    char  wifi_password[64];
    float home_lat;
    float home_lon;
    int   radius_nm;
    int   screen_bearing;
};

void config_reset(AppConfig &cfg);
bool config_load(AppConfig &cfg);
void config_save(const AppConfig &cfg);
void config_erase();
bool config_is_configured(const AppConfig &cfg);
bool config_has_location(const AppConfig &cfg);
void config_to_json(const AppConfig &cfg, JsonDocument &doc, bool mask_secrets = false);
bool config_from_json(const JsonDocument &doc, AppConfig &cfg);
