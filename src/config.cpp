#include "config.h"
#include <Preferences.h>

static const char *NVS_NS = "planeradar";

void config_reset(AppConfig &cfg) {
    memset(&cfg, 0, sizeof(cfg));
    cfg.version   = CONFIG_VERSION;
    cfg.radius_nm = 50;
}

bool config_load(AppConfig &cfg) {
    Preferences prefs;
    prefs.begin(NVS_NS, true);

    if (!prefs.isKey("version")) {
        prefs.end();
        config_reset(cfg);
        return false;
    }

    cfg.version        = prefs.getInt("version",    CONFIG_VERSION);
    cfg.home_lat       = prefs.getFloat("home_lat", 0.0f);
    cfg.home_lon       = prefs.getFloat("home_lon", 0.0f);
    cfg.radius_nm      = prefs.getInt("radius_nm",  50);
    cfg.screen_bearing = prefs.getInt("screen_bear",0);
    prefs.getString("wifi_ssid", cfg.wifi_ssid,     sizeof(cfg.wifi_ssid));
    prefs.getString("wifi_pass", cfg.wifi_password, sizeof(cfg.wifi_password));
    prefs.end();
    return true;
}

void config_save(const AppConfig &cfg) {
    Preferences prefs;
    prefs.begin(NVS_NS, false);
    prefs.putInt("version",      cfg.version);
    prefs.putFloat("home_lat",   cfg.home_lat);
    prefs.putFloat("home_lon",   cfg.home_lon);
    prefs.putInt("radius_nm",    cfg.radius_nm);
    prefs.putInt("screen_bear",  cfg.screen_bearing);
    prefs.putString("wifi_ssid", cfg.wifi_ssid);
    prefs.putString("wifi_pass", cfg.wifi_password);
    prefs.end();
}

void config_erase() {
    Preferences prefs;
    prefs.begin(NVS_NS, false);
    prefs.clear();
    prefs.end();
}

bool config_is_configured(const AppConfig &cfg) {
    return cfg.wifi_ssid[0] != '\0';
}

bool config_has_location(const AppConfig &cfg) {
    return cfg.home_lat != 0.0f || cfg.home_lon != 0.0f;
}

void config_to_json(const AppConfig &cfg, JsonDocument &doc, bool mask_secrets) {
    doc["version"]        = cfg.version;
    doc["wifi_ssid"]      = cfg.wifi_ssid;
    doc["wifi_password"]  = mask_secrets ? "********" : cfg.wifi_password;
    doc["home_lat"]       = cfg.home_lat;
    doc["home_lon"]       = cfg.home_lon;
    doc["radius_nm"]      = cfg.radius_nm;
    doc["screen_bearing"] = cfg.screen_bearing;
}

bool config_from_json(const JsonDocument &doc, AppConfig &cfg) {
    cfg.version        = doc["version"]        | CONFIG_VERSION;
    cfg.home_lat       = doc["home_lat"]       | 0.0f;
    cfg.home_lon       = doc["home_lon"]       | 0.0f;
    cfg.radius_nm      = doc["radius_nm"]      | 30;
    cfg.screen_bearing = doc["screen_bearing"] | 0;
    strlcpy(cfg.wifi_ssid,     doc["wifi_ssid"]     | "", sizeof(cfg.wifi_ssid));
    strlcpy(cfg.wifi_password, doc["wifi_password"] | "", sizeof(cfg.wifi_password));
    return cfg.wifi_ssid[0] != '\0';
}
