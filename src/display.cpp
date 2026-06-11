#include "display.h"

class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_GC9A01  _panel;
    lgfx::Bus_SPI       _bus;
    lgfx::Light_PWM     _light;

public:
    LGFX() {
        {
            auto cfg = _bus.config();
            cfg.spi_host   = SPI2_HOST;
            cfg.freq_write = 80000000;
            cfg.pin_sclk  = PIN_SCK;
            cfg.pin_mosi  = PIN_MOSI;
            cfg.pin_miso  = -1;
            cfg.pin_dc    = PIN_DC;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {
            auto cfg      = _panel.config();
            cfg.pin_cs    = PIN_CS;
            cfg.pin_rst   = PIN_RST;
            cfg.pin_busy  = -1;
            // If the image is upside-down, change offset_rotation to 2
            cfg.offset_rotation = 0;
            _panel.config(cfg);
        }
        // PIN_BL = -1 means backlight is wired directly to VCC
#if PIN_BL >= 0
        {
            auto cfg        = _light.config();
            cfg.pin_bl      = PIN_BL;
            cfg.invert      = false;
            cfg.freq        = 44100;
            cfg.pwm_channel = 7;
            _light.config(cfg);
            _panel.setLight(&_light);
        }
#endif
        setPanel(&_panel);
    }
};

static LGFX              lcd;
static lgfx::LGFX_Sprite canvas(&lcd);

void display_init() {
    lcd.init();
    lcd.setRotation(0);
    lcd.setBrightness(220);
    // 16-bit color, 240x240 = ~115KB heap — tight on C3 but workable
    canvas.setColorDepth(16);
    canvas.createSprite(240, 240);
    canvas.fillScreen(TFT_BLACK);
    canvas.pushSprite(0, 0);
}

lgfx::LGFX_Sprite* display_get_sprite() {
    return &canvas;
}

void display_push() {
    canvas.pushSprite(0, 0);
}
