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

bool config_has_location(const AppConfig &cfg) {
    return cfg.home_lat != 0.0f || cfg.home_lon != 0.0f;
}
