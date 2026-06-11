#pragma once
#include <Arduino.h>

struct RouteInfo {
    bool valid;
    char origin_iata[8];   // "LHR"
    char dest_iata[8];     // "JFK"
    char airline[64];      // "British Airways"
};

// Fetch route info for a callsign via adsbdb.com.
// Returns false on error or if no route data (VFR/private).
bool enrichment_lookup(const char *callsign, RouteInfo &out);
