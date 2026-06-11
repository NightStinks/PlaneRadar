#pragma once
#include <Arduino.h>

#define MAX_RADAR_AIRCRAFT 60

struct NearestAircraft {
    bool  valid;
    char  callsign[16];
    char  registration[12];
    char  type[8];
    float lat, lon;
    float distance_km;
    float bearing_deg;   // from home to aircraft
    float track_deg;     // aircraft heading
    float altitude_ft;
    float speed_kts;
};

struct RadarAircraft {
    float bearing_deg;   // from home
    float distance_km;
    float track_deg;
};

void  flight_set_home(float lat, float lon, int radius_nm);

// Poll adsb.lol. Fills out_nearest (invalid if none) and out_all[0..out_count-1].
// out_all must point to a MAX_RADAR_AIRCRAFT array.
bool  flight_poll(NearestAircraft &out_nearest, RadarAircraft *out_all, int &out_count);

float haversine_km(float lat1, float lon1, float lat2, float lon2);
float bearing_deg(float lat1, float lon1, float lat2, float lon2);
