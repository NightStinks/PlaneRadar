#include "flight.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <math.h>

static float s_home_lat  = 0.0f;
static float s_home_lon  = 0.0f;
static int   s_radius_nm = 50;

void flight_set_home(float lat, float lon, int radius_nm) {
    s_home_lat  = lat;
    s_home_lon  = lon;
    s_radius_nm = radius_nm;
}

float haversine_km(float lat1, float lon1, float lat2, float lon2) {
    float dlat = (lat2 - lat1) * DEG_TO_RAD;
    float dlon = (lon2 - lon1) * DEG_TO_RAD;
    float a = sinf(dlat / 2) * sinf(dlat / 2)
            + cosf(lat1 * DEG_TO_RAD) * cosf(lat2 * DEG_TO_RAD)
            * sinf(dlon / 2) * sinf(dlon / 2);
    return 6371.0f * 2.0f * asinf(sqrtf(a));
}

float bearing_deg(float lat1, float lon1, float lat2, float lon2) {
    float dlon  = (lon2 - lon1) * DEG_TO_RAD;
    float lat1r = lat1 * DEG_TO_RAD;
    float lat2r = lat2 * DEG_TO_RAD;
    float y = sinf(dlon) * cosf(lat2r);
    float x = cosf(lat1r) * sinf(lat2r) - sinf(lat1r) * cosf(lat2r) * cosf(dlon);
    return fmodf((atan2f(y, x) / DEG_TO_RAD) + 360.0f, 360.0f);
}

// Retry the GET while the connection is being refused / not yet established.
// On the ESP32-C3 the first TLS handshake to the API sometimes fails
// transiently; the reference firmware retries instead of giving up. Returns
// the HTTP status code (>0) or a negative HTTPClient error.
static int get_with_retry(HTTPClient &http) {
    http.setConnectTimeout(2000);
    const uint32_t deadline = millis() + 10000;
    int code = 0;
    while (millis() < deadline) {
        code = http.GET();
        if (code > 0) return code;                       // got an HTTP status
        if (code != HTTPC_ERROR_CONNECTION_REFUSED &&
            code != HTTPC_ERROR_CONNECTION_LOST &&
            code != HTTPC_ERROR_NOT_CONNECTED) {
            return code;                                 // hard error — don't spin
        }
        delay(50);
    }
    return code;
}

// Read the entire response body into `payload`, waiting properly for every
// byte. Streaming deserializeJson straight off a TLS socket on the C3 can hit
// a momentary available()==0 mid-transfer and abort with IncompleteInput —
// which looks like "no aircraft". Reading the whole body first (the approach
// the proven reference firmware uses) avoids that.
static bool read_body(HTTPClient &http, String &payload) {
    WiFiClient *stream = http.getStreamPtr();
    if (!stream) return false;

    const int content_length = http.getSize();
    if (content_length > 0) {
        payload.reserve(content_length + 1);   // exact size — no realloc spikes
    } else {
        payload.reserve(96 * 1024);             // chunked: pre-size to avoid doubling
    }

    uint8_t buf[512];
    const uint32_t deadline = millis() + 12000;
    while (millis() < deadline) {
        int avail = stream->available();
        if (avail > 0) {
            int to_read = avail > (int)sizeof(buf) ? (int)sizeof(buf) : avail;
            int n = stream->readBytes(buf, to_read);
            if (n > 0) payload.concat((const char *)buf, n);
        }
        if (content_length > 0 && (int)payload.length() >= content_length) break;
        if (!http.connected() && stream->available() <= 0) break;
        delay(1);
    }
    return payload.length() > 0;
}

bool flight_poll(NearestAircraft &out_nearest, RadarAircraft *out_all, int &out_count) {
    out_count = 0;
    out_nearest.valid = false;

    if (s_home_lat == 0.0f && s_home_lon == 0.0f) return false;

    // adsb.fi open data — same source the reference ESP32-Plane-Radar uses.
    char url[128];
    snprintf(url, sizeof(url),
        "https://opendata.adsb.fi/api/v3/lat/%.4f/lon/%.4f/dist/%d",
        s_home_lat, s_home_lon, s_radius_nm);

    WiFiClientSecure tls;
    tls.setInsecure();

    HTTPClient http;
    if (!http.begin(tls, url)) {
        Serial.println("[flight] http.begin failed");
        return false;
    }
    http.setTimeout(12000);
    http.addHeader("User-Agent", "PlaneRadar/1.0");

    Serial.printf("[flight] heap before fetch: %u\n", ESP.getFreeHeap());

    int code = get_with_retry(http);
    if (code != HTTP_CODE_OK) {
        Serial.printf("[flight] HTTP %d\n", code);
        http.end();
        return false;
    }

    // Pull the whole body into RAM, then close the socket before parsing so
    // the TLS buffers are freed and only the payload + parsed doc coexist.
    String payload;
    bool got = read_body(http, payload);
    http.end();
    if (!got) {
        Serial.println("[flight] empty response body");
        return false;
    }
    Serial.printf("[flight] body %u bytes, heap %u\n",
                  (unsigned)payload.length(), ESP.getFreeHeap());

    // Only keep the fields we use. The [0] template is applied to every
    // element of the "ac" array — this keeps the parsed doc tiny so it fits
    // alongside the async web server on the C3's no-PSRAM heap.
    JsonDocument filter;
    JsonObject fac = filter["ac"].add<JsonObject>();
    fac["lat"]         = true;
    fac["lon"]         = true;
    fac["flight"]      = true;
    fac["track"]       = true;
    fac["true_heading"]= true;
    fac["alt_baro"]    = true;
    fac["gs"]          = true;
    fac["r"]           = true;
    fac["t"]           = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(
        doc, payload, DeserializationOption::Filter(filter));

    if (err) {
        Serial.printf("[flight] JSON error: %s (heap %u)\n", err.c_str(), ESP.getFreeHeap());
        return false;
    }

    JsonArrayConst ac = doc["ac"].as<JsonArrayConst>();
    if (ac.isNull() || ac.size() == 0) {
        Serial.println("[flight] no aircraft in response");
        return false;
    }
    Serial.printf("[flight] %u aircraft in response\n", (unsigned)ac.size());

    float best_dist = 1e9f;
    int   nearest_idx = -1;
    int   count = 0;

    for (JsonObjectConst aircraft : ac) {
        if (!aircraft["lat"].is<float>() || !aircraft["lon"].is<float>()) continue;

        float a_lat  = aircraft["lat"].as<float>();
        float a_lon  = aircraft["lon"].as<float>();
        float dist   = haversine_km(s_home_lat, s_home_lon, a_lat, a_lon);
        float bear   = bearing_deg(s_home_lat, s_home_lon, a_lat, a_lon);
        float track  = aircraft["track"] | (aircraft["true_heading"] | 0.0f);

        if (dist < best_dist) {
            best_dist    = dist;
            nearest_idx  = count;

            // Fill nearest aircraft detail
            out_nearest.valid       = true;
            out_nearest.lat         = a_lat;
            out_nearest.lon         = a_lon;
            out_nearest.distance_km = dist;
            out_nearest.bearing_deg = bear;
            out_nearest.track_deg   = track;
            out_nearest.altitude_ft = aircraft["alt_baro"] | 0.0f;
            out_nearest.speed_kts   = aircraft["gs"]       | 0.0f;

            const char *raw_cs = aircraft["flight"] | "";
            strlcpy(out_nearest.callsign, raw_cs, sizeof(out_nearest.callsign));
            int len = strlen(out_nearest.callsign);
            while (len > 0 && out_nearest.callsign[len - 1] == ' ') out_nearest.callsign[--len] = '\0';
            if (len == 0) strlcpy(out_nearest.callsign, aircraft["r"] | "?", sizeof(out_nearest.callsign));

            strlcpy(out_nearest.registration, aircraft["r"] | "", sizeof(out_nearest.registration));
            strlcpy(out_nearest.type,         aircraft["t"] | "", sizeof(out_nearest.type));
        }

        if (count < MAX_RADAR_AIRCRAFT) {
            out_all[count].bearing_deg = bear;
            out_all[count].distance_km = dist;
            out_all[count].track_deg   = track;
            count++;
        }
    }

    out_count = count;
    (void)nearest_idx;  // nearest is identified by bearing+distance match in the view
    return out_nearest.valid;
}
