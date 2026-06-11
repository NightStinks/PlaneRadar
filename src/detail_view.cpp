#include "views.h"
#include <math.h>

#define CX 120
#define CY 120

static void draw_bearing_arrow(lgfx::LGFX_Sprite *spr, float bearing_deg, int screen_bearing) {
    float adj = (bearing_deg - screen_bearing) * DEG_TO_RAD;
    const int r = 28;
    const int ox = CX, oy = 118;

    spr->drawCircle(ox, oy, r, 0x4208);  // dim ring

    // North tick (direction the display faces = top of screen)
    spr->fillCircle(ox, oy - r, 2, 0xFFE0);  // yellow dot at "up"

    // Arrow line from center
    int ax = ox + (int)(r * sinf(adj));
    int ay = oy - (int)(r * cosf(adj));
    spr->drawLine(ox, oy, ax, ay, TFT_WHITE);

    // Arrowhead
    int h1x = ax + (int)(5 * sinf(adj + 2.45f));
    int h1y = ay - (int)(5 * cosf(adj + 2.45f));
    int h2x = ax + (int)(5 * sinf(adj - 2.45f));
    int h2y = ay - (int)(5 * cosf(adj - 2.45f));
    spr->fillTriangle(ax, ay, h1x, h1y, h2x, h2y, TFT_WHITE);
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
    spr->drawString(nearest.callsign, CX, 32);

    // ── Airline ──────────────────────────────────────────────────────────
    if (route.valid && route.airline[0]) {
        spr->setFont(&lgfx::fonts::Font0);
        spr->setTextColor(0x7BEF);
        spr->setTextDatum(lgfx::top_center);
        spr->drawString(route.airline, CX, 60);
    }

    // ── Bearing arrow ─────────────────────────────────────────────────────
    draw_bearing_arrow(spr, nearest.bearing_deg, screen_bearing);

    // ── Route ─────────────────────────────────────────────────────────────
    if (route.valid && route.origin_iata[0] && route.dest_iata[0]) {
        char route_str[20];
        snprintf(route_str, sizeof(route_str), "%s \xc2\xbb %s",
                 route.origin_iata, route.dest_iata);  // "LHR » JFK"
        spr->setFont(&lgfx::fonts::Font2);
        spr->setTextColor(TFT_CYAN);
        spr->setTextDatum(lgfx::top_center);
        spr->drawString(route_str, CX, 154);
    }

    // ── Distance + altitude ───────────────────────────────────────────────
    char dist_str[24];
    if (nearest.altitude_ft > 100) {
        snprintf(dist_str, sizeof(dist_str), "%.1fkm  FL%d",
                 nearest.distance_km, (int)(nearest.altitude_ft / 100));
    } else {
        snprintf(dist_str, sizeof(dist_str), "%.1fkm", nearest.distance_km);
    }
    spr->setFont(&lgfx::fonts::Font0);
    spr->setTextColor(0x7BEF);
    spr->setTextDatum(lgfx::top_center);
    spr->drawString(dist_str, CX, 178);

    // ── Speed ─────────────────────────────────────────────────────────────
    if (nearest.speed_kts > 10) {
        char spd_str[16];
        snprintf(spd_str, sizeof(spd_str), "%dkts", (int)nearest.speed_kts);
        spr->setFont(&lgfx::fonts::Font0);
        spr->setTextColor(0x4208);
        spr->setTextDatum(lgfx::top_center);
        spr->drawString(spd_str, CX, 195);
    }
}
