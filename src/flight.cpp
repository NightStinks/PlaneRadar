#include "flight.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <math.h>

static float s_home_lat  = 0.0f;
static float s_home_lon  = 0.0f;
static int   s_radius_nm = 50;
static bool  s_fetch_ok  = false;
static void (*s_tick_fn)() = nullptr;   // pumped during blocking waits (button)

void flight_set_home(float lat, float lon, int radius_nm) {
    s_home_lat  = lat;
    s_home_lon  = lon;
    s_radius_nm = radius_nm;
}

void flight_set_tick_fn(void (*fn)()) { s_tick_fn = fn; }

bool flight_fetch_ok() { return s_fetch_ok; }

static inline void pump() { if (s_tick_fn) s_tick_fn(); }

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
// The first TLS handshake on the C3 sometimes fails transiently. Kept short
// (4 s) so a genuinely down network doesn't freeze the UI. Returns the HTTP
// status code (>0) or a negative HTTPClient error.
static int get_with_retry(HTTPClient &http) {
    http.setConnectTimeout(2000);
    const uint32_t deadline = millis() + 4000;
    int code = 0;
    while (millis() < deadline) {
        code = http.GET();
        if (code > 0) return code;                       // got an HTTP status
        if (code != HTTPC_ERROR_CONNECTION_REFUSED &&
            code != HTTPC_ERROR_CONNECTION_LOST &&
            code != HTTPC_ERROR_NOT_CONNECTED) {
            return code;                                 // hard error — don't spin
        }
        pump();
        delay(50);
    }
    return code;
}

// A Stream wrapper whose read() BLOCKS until a byte arrives (or the socket
// truly closes / times out). ESP32's WiFiClient::read() is non-blocking and
// returns -1 the instant no byte is buffered — feeding that straight into
// deserializeJson makes the parser see a false EOF mid-transfer on the slower
// C3 and abort with IncompleteInput ("no signal"). Wrapping the client this
// way lets us stream-parse with an ArduinoJson filter (tiny RAM, no 75 KB
// body buffer to allocate) AND read reliably. The button is pumped while we
// wait so the view stays responsive during a fetch.
class BlockingClientStream : public Stream {
 public:
    BlockingClientStream(WiFiClient *c, uint32_t timeout_ms)
        : c_(c), timeout_ms_(timeout_ms) {}

    int available() override { return c_->available(); }
    int peek() override      { return c_->peek(); }
    size_t write(uint8_t) override { return 0; }

    int read() override {
        // Throttled button poll even while data is flowing fast, so a quick
        // tap during the bulk transfer isn't missed.
        uint32_t now = millis();
        if (now - last_pump_ >= 40) { pump(); last_pump_ = now; }

        const uint32_t deadline = now + timeout_ms_;
        for (;;) {
            int b = c_->read();
            if (b >= 0) return b;
            if (!c_->connected() && c_->available() <= 0) return -1;  // real EOF
            if (millis() > deadline) return -1;                       // stalled
            pump();
            delay(1);
        }
    }

    size_t readBytes(char *buf, size_t len) {
        size_t got = 0;
        while (got < len) {
            int b = read();
            if (b < 0) break;
            buf[got++] = (char)b;
        }
        return got;
    }

 private:
    WiFiClient *c_;
    uint32_t    timeout_ms_;
    uint32_t    last_pump_ = 0;
};

bool flight_poll(NearestAircraft &out_nearest, RadarAircraft *out_all, int &out_count) {
    out_count = 0;
    out_nearest.valid = false;
    s_fetch_ok = false;

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

    // Only keep the fields we use. The [0] template is applied to every element
    // of the "ac" array — the parsed doc stays a few KB regardless of how many
    // aircraft the API returns, so no 75 KB body buffer is needed.
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

    // Stream-parse straight off the socket through the blocking wrapper.
    WiFiClient *raw = http.getStreamPtr();
    if (!raw) {
        Serial.println("[flight] no stream");
        http.end();
        return false;
    }
    BlockingClientStream bs(raw, 5000);

    JsonDocument doc;
    DeserializationError err = deserializeJson(
        doc, bs, DeserializationOption::Filter(filter));
    http.end();

    if (err) {
        Serial.printf("[flight] JSON error: %s (heap %u)\n", err.c_str(), ESP.getFreeHeap());
        return false;
    }

    // HTTP + parse succeeded — the network path is healthy regardless of count.
    s_fetch_ok = true;

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
