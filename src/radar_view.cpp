#include "views.h"
#include <math.h>

#define CX 120
#define CY 120
#define NM_TO_KM 1.852f

static void draw_aircraft(lgfx::LGFX_Sprite *spr, int x, int y, float track_deg, bool nearest) {
    float rad  = track_deg * DEG_TO_RAD;
    float size = nearest ? 7.0f : 5.0f;
    uint32_t col = nearest ? TFT_WHITE : TFT_GREEN;

    int x0 = x + (int)(size * sinf(rad));
    int y0 = y - (int)(size * cosf(rad));
    int x1 = x + (int)(size * 0.55f * sinf(rad + 2.45f));
    int y1 = y - (int)(size * 0.55f * cosf(rad + 2.45f));
    int x2 = x + (int)(size * 0.55f * sinf(rad - 2.45f));
    int y2 = y - (int)(size * 0.55f * cosf(rad - 2.45f));

    spr->fillTriangle(x0, y0, x1, y1, x2, y2, col);
}

void radar_view_draw(lgfx::LGFX_Sprite *spr,
                     const RadarAircraft *ac, int count,
                     float nearest_bearing, float nearest_dist_km,
                     int display_range_nm, int screen_bearing) {
    spr->fillScreen(TFT_BLACK);

    float range_km = display_range_nm * NM_TO_KM;

    // Range rings at 25%, 50%, 75%, 100%
    uint32_t ring_col = 0x0340;  // very dark green
    for (int i = 1; i <= 4; i++) {
        spr->drawCircle(CX, CY, RADAR_RADIUS * i / 4, ring_col);
    }

    // Cardinal tick at "up" (screen_bearing direction = top of display)
    float up_rad = 0.0f;  // "up" is always 12-o'clock in screen space
    int tick_x = CX + (int)((RADAR_RADIUS + 4) * sinf(up_rad));
    int tick_y = CY - (int)((RADAR_RADIUS + 4) * cosf(up_rad));
    spr->fillCircle(tick_x, tick_y, 2, 0x7BEF);

    // Range label at bottom-right of outer ring
    char range_label[10];
    snprintf(range_label, sizeof(range_label), "%dnm", display_range_nm);
    spr->setFont(&lgfx::fonts::Font0);
    spr->setTextColor(0x4208);  // dim gray-green
    spr->setTextDatum(lgfx::bottom_right);
    spr->drawString(range_label, CX + RADAR_RADIUS, CY + RADAR_RADIUS);

    // Draw aircraft
    for (int i = 0; i < count; i++) {
        float adj_bearing = (ac[i].bearing_deg - screen_bearing) * DEG_TO_RAD;
        float adj_track   = ac[i].track_deg - screen_bearing;
        bool is_nearest   = (nearest_dist_km >= 0 &&
                             fabsf(ac[i].bearing_deg - nearest_bearing) < 1.0f &&
                             fabsf(ac[i].distance_km - nearest_dist_km) < 0.5f);

        if (ac[i].distance_km > range_km) {
            // Beyond range — dot at rim
            int rx = CX + (int)((RADAR_RADIUS - 4) * sinf(adj_bearing));
            int ry = CY - (int)((RADAR_RADIUS - 4) * cosf(adj_bearing));
            spr->fillCircle(rx, ry, 2, 0x31A6);  // dim teal
        } else {
            float frac = ac[i].distance_km / range_km;
            int rx = CX + (int)(frac * RADAR_RADIUS * sinf(adj_bearing));
            int ry = CY - (int)(frac * RADAR_RADIUS * cosf(adj_bearing));
            draw_aircraft(spr, rx, ry, adj_track, is_nearest);
        }
    }

    // "No aircraft" label
    if (count == 0) {
        spr->setFont(&lgfx::fonts::Font2);
        spr->setTextColor(0x4208);
        spr->setTextDatum(lgfx::middle_center);
        spr->drawString("No aircraft", CX, CY);
    }
}
