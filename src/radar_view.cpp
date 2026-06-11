#include "views.h"
#include <math.h>

#define CX 120
#define CY 120
#define NM_TO_KM 1.852f

// Heading triangle: nose points toward `heading_rad` (0 = screen up).
static void draw_aircraft(lgfx::LGFX_Sprite *spr, int x, int y,
                          float heading_rad, bool nearest) {
    const float s = sinf(heading_rad);
    const float c = cosf(heading_rad);
    const float nose = nearest ? 9.0f : 6.5f;   // forward length
    const float tail = nearest ? 4.5f : 3.5f;   // back length
    const float half = nearest ? 4.5f : 3.5f;   // half wing span
    const uint32_t col = nearest ? TFT_WHITE : TFT_CYAN;

    int tipx = x + (int)lroundf(s * nose);
    int tipy = y - (int)lroundf(c * nose);
    int bx   = x - (int)lroundf(s * tail);
    int by   = y + (int)lroundf(c * tail);
    int wx   = (int)lroundf(c * half);
    int wy   = (int)lroundf(s * half);

    spr->fillTriangle(tipx, tipy, bx + wx, by + wy, bx - wx, by - wy, col);
}

void radar_view_draw(lgfx::LGFX_Sprite *spr,
                     const RadarAircraft *ac, int count,
                     float nearest_bearing, float nearest_dist_km,
                     int display_range_nm, int screen_bearing,
                     const char *empty_msg) {
    spr->fillScreen(TFT_BLACK);

    const float range_km = display_range_nm * NM_TO_KM;
    const uint32_t ring_col = 0x0320;   // dark green grid

    // Concentric range rings + crosshairs
    for (int i = 1; i <= 4; i++) {
        spr->drawCircle(CX, CY, RADAR_RADIUS * i / 4, ring_col);
    }
    spr->drawLine(CX - RADAR_RADIUS, CY, CX + RADAR_RADIUS, CY, ring_col);
    spr->drawLine(CX, CY - RADAR_RADIUS, CX, CY + RADAR_RADIUS, ring_col);

    // Cardinal labels, rotated so "up" = the direction the display faces.
    spr->setFont(&lgfx::fonts::Font2);
    spr->setTextDatum(lgfx::middle_center);
    spr->setTextColor(0x52AA);
    const char *cards[4] = {"N", "E", "S", "W"};
    const int   card_deg[4] = {0, 90, 180, 270};
    for (int i = 0; i < 4; i++) {
        float a = (card_deg[i] - screen_bearing) * DEG_TO_RAD;
        int lx = CX + (int)lroundf((RADAR_RADIUS - 12) * sinf(a));
        int ly = CY - (int)lroundf((RADAR_RADIUS - 12) * cosf(a));
        spr->drawString(cards[i], lx, ly);
    }

    // Range label (bottom-right inside outer ring)
    char range_label[10];
    snprintf(range_label, sizeof(range_label), "%dnm", display_range_nm);
    spr->setFont(&lgfx::fonts::Font0);
    spr->setTextColor(0x4208);
    spr->setTextDatum(lgfx::bottom_right);
    spr->drawString(range_label, CX + RADAR_RADIUS - 6, CY + RADAR_RADIUS - 4);

    // Aircraft
    for (int i = 0; i < count; i++) {
        float adj_bearing = (ac[i].bearing_deg - screen_bearing) * DEG_TO_RAD;
        float adj_track   = (ac[i].track_deg - screen_bearing) * DEG_TO_RAD;
        bool is_nearest   = (nearest_dist_km >= 0 &&
                             fabsf(ac[i].bearing_deg - nearest_bearing) < 1.0f &&
                             fabsf(ac[i].distance_km - nearest_dist_km) < 0.5f);

        if (ac[i].distance_km > range_km) {
            // Beyond range — dim dot at the rim, correct bearing
            int rx = CX + (int)lroundf((RADAR_RADIUS - 3) * sinf(adj_bearing));
            int ry = CY - (int)lroundf((RADAR_RADIUS - 3) * cosf(adj_bearing));
            spr->fillCircle(rx, ry, 2, 0x31A6);
        } else {
            float frac = ac[i].distance_km / range_km;
            int rx = CX + (int)lroundf(frac * RADAR_RADIUS * sinf(adj_bearing));
            int ry = CY - (int)lroundf(frac * RADAR_RADIUS * cosf(adj_bearing));
            draw_aircraft(spr, rx, ry, adj_track, is_nearest);
        }
    }

    // Centre dot (your location)
    spr->fillCircle(CX, CY, 3, 0xFFE0);

    // Empty state
    if (count == 0) {
        spr->setFont(&lgfx::fonts::Font2);
        spr->setTextColor(0x4208);
        spr->setTextDatum(lgfx::middle_center);
        spr->drawString(empty_msg, CX, CY + 40);
    }
}
