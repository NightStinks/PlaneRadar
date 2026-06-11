#pragma once
#include <LovyanGFX.hpp>
#include "flight.h"
#include "enrichment.h"

#define RADAR_RADIUS 108   // pixels from center to outer ring edge

void radar_view_draw(lgfx::LGFX_Sprite *spr,
                     const RadarAircraft *ac, int count,
                     float nearest_bearing, float nearest_dist_km,
                     int display_range_nm, int screen_bearing,
                     const char *empty_msg = "No aircraft");

void detail_view_draw(lgfx::LGFX_Sprite *spr,
                      const NearestAircraft &nearest,
                      const RouteInfo &route,
                      int screen_bearing);
