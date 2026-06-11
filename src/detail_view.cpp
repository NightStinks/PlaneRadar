#include "views.h"
#include <math.h>

#define CX 120
#define CY 120

// Draw a top-down airplane silhouette centred at (cx,cy), nose pointing toward
// `heading_rad` (0 = screen up). Built from filled triangles in a local frame
// where +ly is forward (nose) and +lx is starboard (right wing), then rotated.
static void draw_plane(lgfx::LGFX_Sprite *spr, int cx, int cy,
                       float scale, float heading_rad, uint32_t col) {
    const float s = sinf(heading_rad);
    const float c = cosf(heading_rad);

    // local (lx, ly) -> screen. forward=(s,-c), starboard=(c,s)
    auto px = [&](float lx, float ly) { return cx + (int)lroundf((lx * c + ly * s) * scale); };
    auto py = [&](float lx, float ly) { return cy + (int)lroundf((lx * s - ly * c) * scale); };

    // Fuselage (nose → tail), as two triangles forming a slim diamond
    spr->fillTriangle(px(0, 10),  py(0, 10),
                      px(-1.6f, -9), py(-1.6f, -9),
                      px(1.6f, -9),  py(1.6f, -9), col);
    spr->fillTriangle(px(-1.6f, 3), py(-1.6f, 3),
                      px(1.6f, 3),   py(1.6f, 3),
                      px(0, 10),     py(0, 10), col);

    // Main wings (swept back)
    spr->fillTriangle(px(0, 2.5f),  py(0, 2.5f),
                      px(-12, -3.5f), py(-12, -3.5f),
                      px(0, -3),     py(0, -3), col);
    spr->fillTriangle(px(0, 2.5f),  py(0, 2.5f),
                      px(12, -3.5f),  py(12, -3.5f),
                      px(0, -3),     py(0, -3), col);

    // Tailplane (small swept stabilisers)
    spr->fillTriangle(px(0, -5),    py(0, -5),
                      px(-5.5f, -9), py(-5.5f, -9),
                      px(0, -9),     py(0, -9), col);
    spr->fillTriangle(px(0, -5),    py(0, -5),
                      px(5.5f, -9),  py(5.5f, -9),
                      px(0, -9),     py(0, -9), col);
}

void detail_view_draw(lgfx::LGFX_Sprite *spr,
                      const NearestAircraft &nearest,
                      const RouteInfo &route,
                      int screen_bearing) {
    spr->fillScreen(TFT_BLACK);

    if (!nearest.valid) {
        spr->setFont(&lgfx::fonts::Font4);
        spr->setTextColor(0x4208);
        spr->setTextDatum(lgfx::middle_center);
        spr->drawString("No Flights", CX, CY);
        return;
    }

    // ── Callsign ─────────────────────────────────────────────────────────
    spr->setFont(&lgfx::fonts::Font4);
    spr->setTextColor(TFT_WHITE);
    spr->setTextDatum(lgfx::top_center);
    spr->drawString(nearest.callsign, CX, 28);

    // ── Airline ──────────────────────────────────────────────────────────
    if (route.valid && route.airline[0]) {
        spr->setFont(&lgfx::fonts::Font2);
        spr->setTextColor(0x7BEF);
        spr->setTextDatum(lgfx::top_center);
        spr->drawString(route.airline, CX, 56);
    }

    // ── Plane arrow: points toward the aircraft relative to where you face ──
    // "up" on the display = the direction the screen faces (screen_bearing),
    // so the plane silhouette points the way you'd turn to spot it.
    const int pcx = CX, pcy = 120;
    const int ring_r = 40;
    float adj = (nearest.bearing_deg - screen_bearing) * DEG_TO_RAD;

    spr->drawCircle(pcx, pcy, ring_r, 0x2104);          // faint guide ring
    spr->fillCircle(pcx, pcy - ring_r, 2, 0x4208);      // tick at "up"

    draw_plane(spr, pcx, pcy, 1.7f, adj, TFT_CYAN);

    // ── Route ─────────────────────────────────────────────────────────────
    if (route.valid && route.origin_iata[0] && route.dest_iata[0]) {
        char route_str[20];
        snprintf(route_str, sizeof(route_str), "%s \xc2\xbb %s",
                 route.origin_iata, route.dest_iata);  // "LHR » JFK"
        spr->setFont(&lgfx::fonts::Font2);
        spr->setTextColor(TFT_CYAN);
        spr->setTextDatum(lgfx::top_center);
        spr->drawString(route_str, CX, 168);
    }

    // ── Distance + altitude ───────────────────────────────────────────────
    char dist_str[24];
    if (nearest.altitude_ft > 100) {
        snprintf(dist_str, sizeof(dist_str), "%.0fkm  FL%d",
                 nearest.distance_km, (int)(nearest.altitude_ft / 100));
    } else {
        snprintf(dist_str, sizeof(dist_str), "%.0fkm", nearest.distance_km);
    }
    spr->setFont(&lgfx::fonts::Font2);
    spr->setTextColor(0xAEBF);
    spr->setTextDatum(lgfx::top_center);
    spr->drawString(dist_str, CX, 190);

    // ── Speed ─────────────────────────────────────────────────────────────
    if (nearest.speed_kts > 10) {
        char spd_str[16];
        snprintf(spd_str, sizeof(spd_str), "%dkts", (int)nearest.speed_kts);
        spr->setFont(&lgfx::fonts::Font0);
        spr->setTextColor(0x4208);
        spr->setTextDatum(lgfx::top_center);
        spr->drawString(spd_str, CX, 212);
    }
}
