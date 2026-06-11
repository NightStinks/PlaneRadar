#include "web_server.h"
#include "ota.h"

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

static AsyncWebServer server(80);
static AppConfig *s_cfg = nullptr;

static void handle_get_config(AsyncWebServerRequest *req) {
    JsonDocument doc;
    config_to_json(*s_cfg, doc, true);
    String body;
    serializeJson(doc, body);
    req->send(200, "application/json", body);
}

static void handle_post_config(AsyncWebServerRequest *req,
                                uint8_t *data, size_t len,
                                size_t index, size_t total) {
    static String buf;
    if (index == 0) buf = "";
    buf += String((char *)data, len);
    if (index + len < total) return;

    JsonDocument doc;
    if (deserializeJson(doc, buf)) {
        req->send(400, "application/json", "{\"error\":\"invalid json\"}");
        return;
    }

    char old_pass[sizeof(s_cfg->wifi_password)];
    strlcpy(old_pass, s_cfg->wifi_password, sizeof(old_pass));

    config_from_json(doc, *s_cfg);

    if (strcmp(s_cfg->wifi_password, "********") == 0)
        strlcpy(s_cfg->wifi_password, old_pass, sizeof(s_cfg->wifi_password));

    config_save(*s_cfg);

    bool reboot = doc["reboot"] | false;
    req->send(200, "application/json", "{\"ok\":true}");
    if (reboot) { delay(300); ESP.restart(); }
}

static void handle_get_status(AsyncWebServerRequest *req) {
    JsonDocument doc;
    doc["version"]    = APP_VERSION;
    doc["hostname"]   = "planeradar.local";
    doc["ip"]         = WiFi.localIP().toString();
    doc["rssi"]       = WiFi.RSSI();
    doc["configured"] = config_has_location(*s_cfg);
    String body;
    serializeJson(doc, body);
    req->send(200, "application/json", body);
}

void web_server_init(AppConfig &cfg) {
    s_cfg = &cfg;

    if (!MDNS.begin("planeradar")) {
        Serial.println("[web] mDNS start failed");
    } else {
        Serial.println("[web] mDNS: http://planeradar.local");
    }

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send(LittleFS, "/index.html", "text/html");
    });

    server.on("/config", HTTP_GET, [](AsyncWebServerRequest *req) {
        handle_get_config(req);
    });

    server.on("/config", HTTP_POST,
        [](AsyncWebServerRequest *req) {},
        nullptr,
        [](AsyncWebServerRequest *req, uint8_t *data, size_t len,
           size_t index, size_t total) {
            handle_post_config(req, data, len, index, total);
        }
    );

    server.on("/status", HTTP_GET, [](AsyncWebServerRequest *req) {
        handle_get_status(req);
    });

    // POST /ota — trigger OTA check in a background task
    server.on("/ota", HTTP_POST, [](AsyncWebServerRequest *req) {
        req->send(200, "application/json", "{\"ok\":true}");
        xTaskCreate([](void *) {
            delay(300);  // let the HTTP response flush before we block the TCP stack
            ota_check(nullptr);
            vTaskDelete(nullptr);
        }, "ota_web", 16384, nullptr, 1, nullptr);
    });

    server.onNotFound([](AsyncWebServerRequest *req) {
        req->redirect("/");
    });

    server.begin();
    MDNS.addService("http", "tcp", 80);
    Serial.printf("[web] Config server on http://%s/\n", WiFi.localIP().toString().c_str());
}
