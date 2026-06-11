#include "ota.h"

#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

#ifndef APP_VERSION
#define APP_VERSION "0.0.0-dev"
#endif

#ifndef OTA_VERSION_URL
#define OTA_VERSION_URL ""
#endif

bool ota_check(OtaProgressFn on_progress) {
    const char *version_url = OTA_VERSION_URL;
    if (!version_url || version_url[0] == '\0') return false;

    Serial.printf("[ota] checking %s\n", version_url);

    WiFiClientSecure vc;
    vc.setInsecure();

    HTTPClient http;
    http.begin(vc, version_url);
    http.setTimeout(8000);
    int code = http.GET();
    if (code != 200) {
        Serial.printf("[ota] version fetch HTTP %d\n", code);
        http.end();
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, http.getStream());
    http.end();
    if (err) return false;

    const char *remote_ver = doc["version"] | "";
    const char *fw_url     = doc["url"]     | "";

    if (!remote_ver[0] || !fw_url[0]) return false;

    if (strcmp(remote_ver, APP_VERSION) == 0) {
        Serial.printf("[ota] up to date (%s)\n", APP_VERSION);
        return false;
    }

    Serial.printf("[ota] update: %s -> %s\n", APP_VERSION, remote_ver);
    if (on_progress) on_progress(0, 0);

    httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    if (on_progress) {
        httpUpdate.onProgress([on_progress](int cur, int total) {
            on_progress(cur, total);
        });
    }

    WiFiClientSecure uc;
    uc.setInsecure();

    // On HTTP_UPDATE_OK the device reboots — control does not return here
    t_httpUpdate_return ret = httpUpdate.update(uc, fw_url);
    if (ret == HTTP_UPDATE_FAILED) {
        Serial.printf("[ota] failed: %s\n", httpUpdate.getLastErrorString().c_str());
    }
    return false;
}
