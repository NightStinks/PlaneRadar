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

bool flight_poll(NearestAircraft &out_nearest, RadarAircraft *out_all, int &out_count) {
    out_count = 0;
    out_nearest.valid = false;

    if (s_home_lat == 0.0f && s_home_lon == 0.0f) return false;

    char url[128];
    snprintf(url, sizeof(url),
        "https://api.adsb.lol/v2/lat/%.4f/lon/%.4f/dist/%d",
        s_home_lat, s_home_lon, s_radius_nm);

    WiFiClientSecure tls;
    tls.setInsecure();

    HTTPClient http;
    http.begin(tls, url);
    http.setTimeout(10000);
    http.addHeader("User-Agent", "PlaneRadar/1.0");

    int code = http.GET();
    if (code != 200) {
        Serial.printf("[flight] HTTP %d\n", code);
        http.end();
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, http.getStream());
    http.end();

    if (err) {
        Serial.printf("[flight] JSON error: %s\n", err.c_str());
        return false;
    }

    JsonArrayConst ac = doc["ac"].as<JsonArrayConst>();
    if (ac.isNull() || ac.size() == 0) return false;

    float best_dist = 1e9f;
    int   nearest_idx = -1;
    int   count = 0;

    for (JsonObjectConst aircraft : ac) {
        if (!aircraft["lat"].is<float>() || !aircraft["lon"].is<float>()) continue;

        float a_lat  = aircraft["lat"].as<float>();
        float a_lon  = aircraft["lon"].as<float>();
        float dist   = haversine_km(s_home_lat, s_home_lon, a_lat, a_lon);
        float bear   = bearing_deg(s_home_lat, s_home_lon, a_lat, a_lon);
        float track  = aircraft["track"] | 0.0f;

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
