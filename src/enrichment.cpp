#include "enrichment.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

bool enrichment_lookup(const char *callsign, RouteInfo &out) {
    out = {};
    if (!callsign || callsign[0] == '\0') return false;

    char url[128];
    snprintf(url, sizeof(url), "https://api.adsbdb.com/v0/callsign/%s", callsign);

    WiFiClientSecure tls;
    tls.setInsecure();

    HTTPClient http;
    http.begin(tls, url);
    http.setTimeout(8000);
    http.addHeader("User-Agent", "PlaneRadar/1.0");

    int code = http.GET();
    if (code != 200) {
        Serial.printf("[enrich] HTTP %d for %s\n", code, callsign);
        http.end();
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, http.getStream());
    http.end();

    if (err) {
        Serial.printf("[enrich] JSON error: %s\n", err.c_str());
        return false;
    }

    JsonVariantConst route = doc["response"]["flightroute"];
    if (route.isNull() || route.is<bool>()) return false;

    strlcpy(out.origin_iata, route["origin"]["iata_code"]      | "", sizeof(out.origin_iata));
    strlcpy(out.dest_iata,   route["destination"]["iata_code"] | "", sizeof(out.dest_iata));
    strlcpy(out.airline,     route["airline"]["name"]          | "", sizeof(out.airline));

    out.valid = (out.origin_iata[0] != '\0' || out.airline[0] != '\0');
    return out.valid;
}
