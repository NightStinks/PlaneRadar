#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>

#include "config.h"
#include "display.h"
#include "flight.h"
#include "enrichment.h"
#include "views.h"
#include "ota.h"
#include "setup_portal.h"
#include "web_server.h"

// ── Hardware ──────────────────────────────────────────────────────────────────
#define BOOT_PIN 9   // active LOW, internal pull-up

// ── Tuning ───────────────────────────────────────────────────────────────────
#define POLL_INTERVAL_MS   5000
#define SHORT_PRESS_MAX_MS  500
#define LONG_PRESS_MIN_MS  1000
#define WIFI_CONNECT_TIMEOUT_MS 15000

static const int RANGE_PRESETS[] = {5, 10, 20, 50};
static const int NUM_RANGES      = 4;

// ── State ─────────────────────────────────────────────────────────────────────
static AppConfig       cfg;
static int             range_idx  = 1;    // default 10nm
static bool            view_radar = true;

static NearestAircraft s_nearest  = {};
static RadarAircraft   s_all[MAX_RADAR_AIRCRAFT] = {};
static int             s_count    = 0;
static RouteInfo       s_route    = {};
static char            s_last_cs[16] = {};

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

static void show_portal_screen() {
    lgfx::LGFX_Sprite *spr = display_get_sprite();
    spr->fillScreen(TFT_BLACK);
    spr->setTextDatum(lgfx::top_center);
    spr->setTextColor(TFT_WHITE);
    spr->setFont(&lgfx::fonts::Font4);
    spr->drawString("PlaneRadar", 120, 58);
    spr->setFont(&lgfx::fonts::Font2);
    spr->setTextColor(0x7BEF);
    spr->drawString("Connect to Wi-Fi:", 120, 106);
    spr->setTextColor(TFT_CYAN);
    spr->drawString("PlaneRadar-Setup", 120, 128);
    spr->setFont(&lgfx::fonts::Font0);
    spr->setTextColor(0x4208);
    spr->drawString("to configure", 120, 156);
    display_push();
}

static void show_configure_screen() {
    lgfx::LGFX_Sprite *spr = display_get_sprite();
    spr->fillScreen(TFT_BLACK);
    spr->setTextDatum(lgfx::top_center);
    spr->setTextColor(TFT_WHITE);
    spr->setFont(&lgfx::fonts::Font2);
    spr->drawString("Visit in browser:", 120, 90);
    spr->setTextColor(TFT_CYAN);
    spr->drawString("planeradar.local", 120, 116);
    spr->setFont(&lgfx::fonts::Font0);
    spr->setTextColor(0x4208);
    spr->drawString("to finish setup", 120, 144);
    display_push();
}

// ── WiFi ──────────────────────────────────────────────────────────────────────

static bool wifi_connect() {
    Serial.printf("[wifi] connecting to %s\n", cfg.wifi_ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(cfg.wifi_ssid, cfg.wifi_password);

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > WIFI_CONNECT_TIMEOUT_MS) return false;
        delay(250);
    }
    Serial.printf("[wifi] connected: %s\n", WiFi.localIP().toString().c_str());
    return true;
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
        s_route      = {};
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

    if (!LittleFS.begin(true)) {
        Serial.println("[boot] LittleFS mount failed");
    }

    config_load(cfg);

    // Hold BOOT at power-on > 3s → factory reset
    if (digitalRead(BOOT_PIN) == LOW) {
        show_status("Hold to reset...");
        delay(3000);
        if (digitalRead(BOOT_PIN) == LOW) {
            show_status("Resetting...");
            config_erase();
            config_reset(cfg);
            delay(500);
            ESP.restart();
        }
    }

    // Phase 1: no WiFi credentials → captive portal (does not return)
    if (!config_is_configured(cfg)) {
        show_portal_screen();
        setup_portal_run(cfg);
        return;
    }

    // Connect to WiFi
    show_status("Connecting...", cfg.wifi_ssid);
    if (!wifi_connect()) {
        Serial.println("[wifi] connect failed — clearing credentials, restarting portal");
        show_status("WiFi failed", "reconfiguring...");
        delay(1500);
        config_erase();
        config_reset(cfg);
        show_portal_screen();
        setup_portal_run(cfg);
        return;
    }

    // Start mDNS + web config server
    web_server_init(cfg);

    // OTA check
    show_status("Checking for", "update...");
    {
        lgfx::LGFX_Sprite *spr = display_get_sprite();
        ota_check([spr](int cur, int total) {
            spr->fillScreen(TFT_BLACK);
            spr->setTextDatum(lgfx::top_center);
            spr->setFont(&lgfx::fonts::Font2);
            spr->setTextColor(TFT_WHITE);
            spr->drawString("Updating...", 120, 72);
            spr->setFont(&lgfx::fonts::Font0);
            spr->setTextColor(0x4208);
            spr->drawString("do not power off", 120, 102);
            if (total > 0) {
                int bar_w = (int)(198.0f * cur / total);
                spr->drawRect(20, 122, 200, 14, 0x31A6);
                spr->fillRect(21, 123, bar_w, 12, TFT_CYAN);
                char pct[8];
                snprintf(pct, sizeof(pct), "%d%%", 100 * cur / total);
                spr->setFont(&lgfx::fonts::Font2);
                spr->setTextColor(TFT_WHITE);
                spr->drawString(pct, 120, 144);
            }
            display_push();
        });
    }

    // Phase 2: WiFi connected but no location set → wait for web config
    if (!config_has_location(cfg)) {
        show_configure_screen();
        for (;;) delay(10);  // web server handles config saves + reboots
    }

    // Fully configured — start radar
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
