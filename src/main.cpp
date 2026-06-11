#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>

#include "config.h"
#include "display.h"
#include "flight.h"
#include "enrichment.h"
#include "views.h"

// ── Hardware ──────────────────────────────────────────────────────────────────
#define BOOT_PIN 9   // active LOW, internal pull-up

// ── Tuning ───────────────────────────────────────────────────────────────────
#define POLL_INTERVAL_MS   5000
#define SHORT_PRESS_MAX_MS  500
#define LONG_PRESS_MIN_MS  1000

static const int RANGE_PRESETS[]  = {5, 10, 20, 50};
static const int NUM_RANGES       = 4;

// ── State ─────────────────────────────────────────────────────────────────────
static AppConfig        cfg;
static int              range_idx  = 1;    // default 10nm
static bool             view_radar = true;

static NearestAircraft  s_nearest  = {};
static RadarAircraft    s_all[MAX_RADAR_AIRCRAFT] = {};
static int              s_count    = 0;
static RouteInfo        s_route    = {};
static char             s_last_cs[16] = {};

// ── Display helpers ───────────────────────────────────────────────────────────

static void redraw() {
    lgfx::LGFX_Sprite *spr = display_get_sprite();
    if (view_radar) {
        radar_view_draw(spr, s_all, s_count,
                        s_nearest.valid ? s_nearest.bearing_deg : -1.0f,
                        s_nearest.valid ? s_nearest.distance_km : -1.0f,
                        RANGE_PRESETS[range_idx], cfg.screen_bearing);
    } else {
        detail_view_draw(spr, s_nearest, s_route, cfg.screen_bearing);
    }
    display_push();
}

static void show_setup_screen(const char *ap_name) {
    lgfx::LGFX_Sprite *spr = display_get_sprite();
    spr->fillScreen(TFT_BLACK);
    spr->setTextDatum(lgfx::top_center);
    spr->setTextColor(TFT_WHITE);
    spr->setFont(&lgfx::fonts::Font4);
    spr->drawString("PlaneRadar", 120, 68);
    spr->setFont(&lgfx::fonts::Font2);
    spr->setTextColor(0x7BEF);
    spr->drawString("Connect to Wi-Fi:", 120, 110);
    spr->setTextColor(TFT_CYAN);
    spr->drawString(ap_name, 120, 134);
    spr->setFont(&lgfx::fonts::Font0);
    spr->setTextColor(0x4208);
    spr->drawString("to configure", 120, 160);
    display_push();
}

static void show_status(const char *line1, const char *line2 = nullptr) {
    lgfx::LGFX_Sprite *spr = display_get_sprite();
    spr->fillScreen(TFT_BLACK);
    spr->setTextDatum(lgfx::middle_center);
    spr->setTextColor(0x7BEF);
    spr->setFont(&lgfx::fonts::Font2);
    spr->drawString(line1, 120, line2 ? 105 : 120);
    if (line2) {
        spr->setFont(&lgfx::fonts::Font0);
        spr->setTextColor(0x4208);
        spr->drawString(line2, 120, 135);
    }
    display_push();
}

// ── Flight polling ────────────────────────────────────────────────────────────

static void poll_flight() {
    bool found = flight_poll(s_nearest, s_all, s_count);

    if (found && strcmp(s_nearest.callsign, s_last_cs) != 0) {
        strlcpy(s_last_cs, s_nearest.callsign, sizeof(s_last_cs));
        s_route = {};
        enrichment_lookup(s_nearest.callsign, s_route);
    }
    if (!found) {
        s_route    = {};
        s_last_cs[0] = '\0';
    }
}

// ── Button handling ───────────────────────────────────────────────────────────

static bool     btn_last     = HIGH;
static uint32_t btn_press_ms = 0;
static bool     btn_handled  = false;

static void button_tick() {
    bool btn = digitalRead(BOOT_PIN);

    if (btn == LOW && btn_last == HIGH) {
        btn_press_ms = millis();
        btn_handled  = false;
    }

    if (btn == LOW && !btn_handled) {
        uint32_t held = millis() - btn_press_ms;
        if (held >= LONG_PRESS_MIN_MS) {
            // Long press: cycle range (radar view only)
            if (view_radar) {
                range_idx = (range_idx + 1) % NUM_RANGES;
                redraw();
            }
            btn_handled = true;
        }
    }

    if (btn == HIGH && btn_last == LOW && !btn_handled) {
        uint32_t held = millis() - btn_press_ms;
        if (held < SHORT_PRESS_MAX_MS) {
            // Short press: toggle view
            view_radar = !view_radar;
            redraw();
        }
    }

    btn_last = btn;
}

// ── Arduino entry points ──────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    Serial.println("\n[boot] PlaneRadar");

    pinMode(BOOT_PIN, INPUT_PULLUP);
    display_init();
    config_load(cfg);

    // Hold BOOT at power-on > 3s → factory reset
    if (digitalRead(BOOT_PIN) == LOW) {
        show_status("Hold to reset...");
        delay(3000);
        if (digitalRead(BOOT_PIN) == LOW) {
            show_status("Resetting...");
            config_erase();
            WiFiManager wm;
            wm.resetSettings();
            delay(500);
            ESP.restart();
        }
    }

    // ── WiFiManager ───────────────────────────────────────────────────────────
    // Pre-fill custom params from saved config so they survive reconnects
    char lat_buf[16]  = "", lon_buf[16] = "", bear_buf[6] = "0", rad_buf[6] = "50";
    if (config_has_location(cfg)) {
        snprintf(lat_buf,  sizeof(lat_buf),  "%.5f", cfg.home_lat);
        snprintf(lon_buf,  sizeof(lon_buf),  "%.5f", cfg.home_lon);
        snprintf(bear_buf, sizeof(bear_buf), "%d",   cfg.screen_bearing);
        snprintf(rad_buf,  sizeof(rad_buf),  "%d",   cfg.radius_nm);
    }

    WiFiManagerParameter p_lat ("lat",     "Home Latitude",          lat_buf,  15);
    WiFiManagerParameter p_lon ("lon",     "Home Longitude",         lon_buf,  15);
    WiFiManagerParameter p_bear("bearing", "Screen Bearing (0=N up)",bear_buf,  4);
    WiFiManagerParameter p_rad ("radius",  "Fetch Radius NM",        rad_buf,   4);

    WiFiManager wm;
    wm.addParameter(&p_lat);
    wm.addParameter(&p_lon);
    wm.addParameter(&p_bear);
    wm.addParameter(&p_rad);
    wm.setConfigPortalTimeout(300);  // 5 min then reboot if no one connects

    const char *AP_NAME = "PlaneRadar-Setup";
    show_setup_screen(AP_NAME);

    bool connected = wm.autoConnect(AP_NAME);

    if (!connected) {
        show_status("WiFi failed", "Restarting...");
        delay(2000);
        ESP.restart();
    }

    // Merge custom params — only update if user supplied valid values
    float new_lat  = atof(p_lat.getValue());
    float new_lon  = atof(p_lon.getValue());
    int   new_bear = atoi(p_bear.getValue());
    int   new_rad  = atoi(p_rad.getValue());

    if (new_lat != 0.0f || new_lon != 0.0f) {
        cfg.home_lat       = new_lat;
        cfg.home_lon       = new_lon;
        cfg.screen_bearing = new_bear;
        cfg.radius_nm      = (new_rad >= 5 && new_rad <= 200) ? new_rad : 50;
        strlcpy(cfg.wifi_ssid, WiFi.SSID().c_str(), sizeof(cfg.wifi_ssid));
        config_save(cfg);
        Serial.printf("[cfg] saved lat=%.5f lon=%.5f bear=%d rad=%d\n",
                      cfg.home_lat, cfg.home_lon, cfg.screen_bearing, cfg.radius_nm);
    }

    if (!config_has_location(cfg)) {
        show_status("Set location", "reconnect portal");
        for (;;) delay(1000);  // stuck until user resets and reconfigures
    }

    // ── Ready ──────────────────────────────────────────────────────────────────
    Serial.printf("[wifi] %s\n", WiFi.localIP().toString().c_str());
    flight_set_home(cfg.home_lat, cfg.home_lon, cfg.radius_nm);

    show_status("Searching...");
    poll_flight();
    redraw();
}

static uint32_t last_poll = 0;

void loop() {
    button_tick();

    uint32_t now = millis();
    if (now - last_poll >= POLL_INTERVAL_MS || last_poll == 0) {
        last_poll = now;
        poll_flight();
        redraw();
    }

    delay(20);
}
