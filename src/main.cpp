#include <lvgl.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <WiFi.h>
#include <OpenHABClient.h>

#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include "ui.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TFT_BL 2

// ====================== OpenHAB ======================
IPAddress openhabServer(192, 168, 2, 1);
String bearer = "oh.Anzeigeprojekt.HOeAmT73Y13JzEsITBVLuoXNSQTyRkruv4CiNl6catA2KhbZUHg2V0zbFfkbUXqZHZCjg8T4DE6rfCNi97Jg";
OpenHABClient openHABClient(openhabServer.toString(), 8080, bearer);

// ====================== LovyanGFX Panel ======================
class LGFX : public lgfx::LGFX_Device {
public:
  lgfx::Bus_RGB     _bus_instance;
  lgfx::Panel_RGB   _panel_instance;
  LGFX(void) {
    { auto cfg = _bus_instance.config();
      cfg.panel = &_panel_instance;
      cfg.pin_d0  = GPIO_NUM_8;   cfg.pin_d1  = GPIO_NUM_3;   cfg.pin_d2  = GPIO_NUM_46;
      cfg.pin_d3  = GPIO_NUM_9;   cfg.pin_d4  = GPIO_NUM_1;   cfg.pin_d5  = GPIO_NUM_5;
      cfg.pin_d6  = GPIO_NUM_6;   cfg.pin_d7  = GPIO_NUM_7;   cfg.pin_d8  = GPIO_NUM_15;
      cfg.pin_d9  = GPIO_NUM_16;  cfg.pin_d10 = GPIO_NUM_4;   cfg.pin_d11 = GPIO_NUM_45;
      cfg.pin_d12 = GPIO_NUM_48;  cfg.pin_d13 = GPIO_NUM_47;  cfg.pin_d14 = GPIO_NUM_21;
      cfg.pin_d15 = GPIO_NUM_14;
      cfg.pin_henable = GPIO_NUM_40; cfg.pin_vsync = GPIO_NUM_41; cfg.pin_hsync = GPIO_NUM_39;
      cfg.pin_pclk = GPIO_NUM_0; cfg.freq_write = 15000000;
      cfg.hsync_polarity = 0; cfg.hsync_front_porch = 8; cfg.hsync_pulse_width = 4; cfg.hsync_back_porch = 43;
      cfg.vsync_polarity = 0; cfg.vsync_front_porch = 8; cfg.vsync_pulse_width = 4; cfg.vsync_back_porch = 12;
      cfg.pclk_active_neg = 1; cfg.de_idle_high = 0; cfg.pclk_idle_high = 0;
      _bus_instance.config(cfg);
    }
    { auto cfg = _panel_instance.config();
      cfg.memory_width = 800; cfg.memory_height = 480; cfg.panel_width = 800; cfg.panel_height = 480;
      cfg.offset_x = 0; cfg.offset_y = 0;
      _panel_instance.config(cfg);
    }
    _panel_instance.setBus(&_bus_instance);
    setPanel(&_panel_instance);
  }
};
LGFX lcd;

// ====================== WLAN ======================
const char* ssid = "WLANC_IOT";
const char* password = "clemensiot!";
#include "touch.h"

// ====================== LVGL Display Driver ======================
static uint32_t screenWidth, screenHeight;
static lv_disp_draw_buf_t draw_buf;
static lv_color_t disp_draw_buf[800 * 480 / 10];
static lv_disp_drv_t disp_drv;

/* Display flushing */
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);
#if (LV_COLOR_16_SWAP != 0)
  lcd.pushImageDMA(area->x1, area->y1, w, h, (lgfx::rgb565_t*)&color_p->full);
#else
  lcd.pushImageDMA(area->x1, area->y1, w, h, (lgfx::rgb565_t*)&color_p->full);
#endif
  lv_disp_flush_ready(disp);
}

// ====================== Weak-Platzhalter (keine Erstellung!) ======================
#ifdef __cplusplus
extern "C" {
#endif
__attribute__((weak)) lv_obj_t* ui_btnNext  = nullptr;
__attribute__((weak)) lv_obj_t* ui_btn_Next = nullptr;
__attribute__((weak)) lv_obj_t* ui_Screen1  = nullptr;
__attribute__((weak)) lv_obj_t* ui_Screen2  = nullptr;
#ifdef __cplusplus
}
#endif

// ====================== UI-Objekte (aus ui.h) ======================
extern lv_obj_t* uic_Grafik;
extern lv_obj_t* ui_lblip;
extern lv_obj_t* uic_aktuell;
extern lv_obj_t* uic_Preis;

// ====================== Helfer ======================
void ensureWifi() {
  if (WiFi.status() == WL_CONNECTED) return;
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < 15000) { delay(300); }
}

// Kleine Pause zwischen HTTP-Calls, damit der Stack atmen kann
static inline void net_yield(uint32_t ms=15) { vTaskDelay(pdMS_TO_TICKS(ms)); }

// OpenHAB fetch mit 1x Retry
bool oh_get_float(const String& item, float& out) {
  for (int attempt = 0; attempt < 2; ++attempt) {
    float v = openHABClient.getItemStateFloat(item);
    if (!isnan(v)) { out = v; return true; }
    net_yield(30);
  }
  return false;
}

void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
  if (touch_has_signal()) {
    if (touch_touched()) {
      data->state = LV_INDEV_STATE_PR;
      data->point.x = touch_last_x;
      data->point.y = touch_last_y;
    } else if (touch_released()) {
      data->state = LV_INDEV_STATE_REL;
    }
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

// ====================== Chart/Serien/Arrays ======================
static lv_chart_series_t* s_prices = nullptr;   // PRIMARY_Y
static lv_chart_series_t* s_power  = nullptr;   // SECONDARY_Y

static lv_chart_series_t* s_verbrauch = nullptr; // PRIMARY_Y
static lv_chart_series_t* s_einspeisung = nullptr; // PRIMARY_Y

static lv_coord_t strompreise_array[96];
static lv_coord_t stromverbrauch_array[96];

static lv_coord_t stromverbrauch60min_array[60];
static lv_coord_t stromeinspeisung60min_array[60];

static lv_coord_t y2_max = 1000; // Start-Range 0..1000 (Wh)
static lv_coord_t y2_maxVerbrauch = 1000; // Start-Range 0..1000 (Wh)

static inline lv_coord_t round_up_step(lv_coord_t x, lv_coord_t step) { return ((x + step - 1) / step) * step; }

// ====================== Next-Button Callback ======================
static bool s_on_screen1 = true;
static void on_next(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (ui_Screen1 && ui_Screen2) {
    if (s_on_screen1) lv_scr_load_anim(ui_Screen2, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, true);
    else              lv_scr_load_anim(ui_Screen1, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, true);
    s_on_screen1 = !s_on_screen1;
  }
}

// ====================== Datenpuffer (Task -> UI) ======================
typedef struct {
  lv_coord_t prices15[96];
  lv_coord_t verbrauch15[96];
  lv_coord_t vmaxGrafik; // max. Verbrauch in den letzten 60 Minuten
  float preisaktuell;
  float verbrauch; // aktueller Verbrauch in Watt
  
  lv_coord_t verbrauch60min[60];
  lv_coord_t vmaxGrafikverbrauch; // max. Verbrauch in den letzten 60 Minuten
  
  bool ok; // mind. etwas erfolgreich?
} data_packet_t;

static volatile bool g_data_ready = false;
static data_packet_t g_data;
static portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;

// ====================== Hintergrund-Task: OpenHAB-Fetch ======================
const uint32_t UPDATE_MS = 10000;

void data_task(void* pv) {
for (;;) {
    // WLAN sicherstellen
    ensureWifi();
    if (WiFi.status() != WL_CONNECTED) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        continue;
    }

    data_packet_t local{}; local.ok = false;
    // Item stromtagesverbrauch_15 gibt  96 Werte als Array zurück jeder 15 Minuten ein Wert ab 00:00
    int count = 0;
    float* arr = openHABClient.getItemStateFloatArray("stromtagesverbrauch_15", count);
    Serial.println("Count: " + String(count));
    if (arr && count == 96) {
        local.vmaxGrafik = 0;

        for (int i = 0; i < 96; ++i) {
            local.verbrauch15[i] = round_up_step(arr[i] * 1000.0 * 4, 10); // kWh -> Wh *4 da 15min
            if (local.verbrauch15[i] > local.vmaxGrafik) local.vmaxGrafik = local.verbrauch15[i];
        }
        local.ok = true;
    }
    //  Item Strompreisverlauf gibt  96 Werte als Array zurück jeder 15 Minuten ein Wert ab 00:00
    count = 0;
    arr = openHABClient.getItemStateFloatArray("Strompreisverlauf", count);
    Serial.println("Count: " + String(count));
    if (arr && count == 96) {
        for (int i = 0; i < 96; ++i) local.prices15[i] = round_up_step(arr[i], 1); // Eurocent
        local.ok = true;
    }

    // Imt stromtagesverbrauch_60min gibt  60 Werte der letzen 60 Minuten zurück
    count = 0;
    arr = openHABClient.getItemStateFloatArray("stromstundenverbrauch", count);
    Serial.println("Count: " + String(count));
    if (arr && count == 60) {
        local.vmaxGrafikverbrauch = 0;
        for (int i = 0; i < 60; ++i) {
            local.verbrauch60min[i] = arr[i]; // kWh -> Wh
            if (abs(local.verbrauch60min[i]) > local.vmaxGrafikverbrauch) local.vmaxGrafikverbrauch = abs(local.verbrauch60min[i]);
        }
        local.ok = true;
    }

     net_yield();

    // ---- aktuelle Labels ----
    float t;
    
    if (oh_get_float("Spotpreis_Current", t)) { local.preisaktuell = t; local.ok = true; }
    if (oh_get_float("kwverbraucht", t)) { local.verbrauch = t * 1000.0; local.ok = true; }
    

     net_yield();
      // ---- Übergabe an UI (nur wenn zumindest etwas ok) ----
    taskENTER_CRITICAL(&g_mux);
        g_data = local;
        g_data_ready = local.ok;
    taskEXIT_CRITICAL(&g_mux);

    // Warte 30 Sekunden bis zum nächsten Durchlauf
    vTaskDelay(pdMS_TO_TICKS(30000));
}
}

// ====================== v9: Tick-Callback auf millis (falls nötig) ======================
#if LVGL_VERSION_MAJOR >= 9
static uint32_t my_tick_cb(void){ return (uint32_t)millis(); }
#endif

// ====================== Setup / Loop ======================
void setup() {
  Serial.begin(9600);
  WiFi.setSleep(false);        // stabilere Latenz
  Wire.begin(19, 20);

  ensureWifi();

  pinMode(38, OUTPUT);
  digitalWrite(38, LOW);

  lcd.begin();
  lcd.fillScreen(TFT_BLACK);
  delay(200);

  lv_init();
  delay(100);
  touch_init();

#if LVGL_VERSION_MAJOR >= 9
  lv_tick_set_cb(my_tick_cb);  // v9: Tick aus millis
#endif

  screenWidth  = lcd.width();
  screenHeight = lcd.height();
  lv_disp_draw_buf_init(&draw_buf, disp_draw_buf, NULL, screenWidth * screenHeight / 10);

  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = screenWidth;
  disp_drv.ver_res = screenHeight;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touchpad_read;
  lv_indev_drv_register(&indev_drv);

#ifdef TFT_BL
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
#endif

  ui_init();
  lv_timer_handler();

  // Chart
  lv_chart_set_point_count(uic_Grafik, 96);
  s_prices = lv_chart_add_series(uic_Grafik, lv_color_hex(0x880404), LV_CHART_AXIS_PRIMARY_Y);
  s_power  = lv_chart_add_series(uic_Grafik, lv_color_hex(0x0000FF), LV_CHART_AXIS_SECONDARY_Y);
  
s_verbrauch = lv_chart_add_series(uic_Grafikverbrauch, lv_color_hex(0xFF0000), LV_CHART_AXIS_PRIMARY_Y); // Rot für Verbrauch
s_einspeisung = lv_chart_add_series(uic_Grafikverbrauch, lv_color_hex(0x00FF00), LV_CHART_AXIS_PRIMARY_Y); // Grün für Einspeisung

  lv_chart_set_ext_y_array(uic_Grafik, s_prices, strompreise_array);
  lv_chart_set_ext_y_array(uic_Grafik, s_power,  stromverbrauch_array);

    lv_chart_set_ext_y_array(uic_Grafikverbrauch, s_verbrauch, stromverbrauch60min_array);
    lv_chart_set_ext_y_array(uic_Grafikverbrauch, s_einspeisung, stromeinspeisung60min_array);

  lv_chart_set_range(uic_Grafik, LV_CHART_AXIS_SECONDARY_Y, 0, y2_max);

  // Next-Button Event (ui_btnNext oder ui_btn_Next)
  lv_obj_t* nextBtn = ui_btnNext ? ui_btnNext : ui_btn_Next;
  if (nextBtn) {
    lv_obj_add_flag(nextBtn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_foreground(nextBtn);
    lv_obj_add_event_cb(nextBtn, on_next, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_pad_all(nextBtn, 10, 0);
  }

  // Hintergrund-Task (Core 0: WiFi-Stack)
  xTaskCreatePinnedToCore(data_task, "data_task", 12288, nullptr, 1, nullptr, 0);

 

  // Ich brauch einen 

  Serial.println("Setup done");
}

void loop() {
  // UI bedienen
  lv_timer_handler();

  // neue Daten übernehmen (falls vorhanden)
  bool have = false; data_packet_t local{};
  taskENTER_CRITICAL(&g_mux);
    if (g_data_ready) { local = g_data; g_data_ready = false; have = true; }
  taskEXIT_CRITICAL(&g_mux);

  if (have) {
    if (ui_lblip) lv_label_set_text(ui_lblip, WiFi.localIP().toString().c_str());

    for (int i = 0; i < 96; ++i) {
      strompreise_array[i] = local.prices15[i];
      stromverbrauch_array[i] = local.verbrauch15[i];
    }
  
    for (int i = 0; i < 60; ++i) {

        if (local.verbrauch60min[i] < 0)
        {
            stromeinspeisung60min_array[i]  = abs(local.verbrauch60min[i]);
            stromverbrauch60min_array[i] = 0;
        }
        else
        {
            stromeinspeisung60min_array[i]  = 0;
            stromverbrauch60min_array[i] = local.verbrauch60min[i];
        }
       
    }

    

    //Ermittele Maxfür die 2 Grafik
    if (local.vmaxGrafikverbrauch > y2_maxVerbrauch) {
      y2_maxVerbrauch = round_up_step(local.vmaxGrafikverbrauch, 200);
      lv_chart_set_range(uic_Grafikverbrauch, LV_CHART_AXIS_PRIMARY_Y, 0, y2_maxVerbrauch);
      lv_chart_set_range(uic_Grafikverbrauch, LV_CHART_AXIS_SECONDARY_Y, 0, y2_maxVerbrauch);

    }

}
    lv_chart_refresh(uic_Grafikverbrauch);



    if (local.vmaxGrafik > y2_max) {
      y2_max = round_up_step(local.vmaxGrafik, 200);
      lv_chart_set_range(uic_Grafik, LV_CHART_AXIS_SECONDARY_Y, 0, y2_max);
    }
   
    
  

    if (uic_aktuell)  lv_label_set_text(uic_aktuell,  String(local.verbrauch,   2).c_str());
    if (uic_Preis) lv_label_set_text(uic_Preis, String(local.preisaktuell, 2).c_str());

    lv_chart_refresh(uic_Grafik);



  delay(2); // reaktiv bleiben
}
