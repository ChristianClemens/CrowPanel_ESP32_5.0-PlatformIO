#include <lvgl.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <WiFi.h>
#include <OpenHABClient.h>

#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include "Anzeige/ui.h"

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
#include <esp_wifi.h>

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
//void ensureWifi() {
//  if (WiFi.status() == WL_CONNECTED) return;
//  WiFi.mode(WIFI_STA);
//  WiFi.begin(ssid, password);
//  uint32_t t0 = millis();
//  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < 15000) { delay(300); }
// }

void onWiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_START:
     WiFi.begin(ssid, password);
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.printf("[WiFi] Verbunden: %s\n", WiFi.localIP().toString().c_str());
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.println("[WiFi] Verbindung verloren → Reconnect...");
      WiFi.disconnect(true, false);
      WiFi.begin(ssid, password);
      break;
    default:
      break;
  }
}

// ---- Stabiles WLAN-Setup ----
void wifi_init_stable() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);                // keine Stromspar-Disconnects
  WiFi.setHostname("esp32s3-panel");
  WiFi.setTxPower(WIFI_POWER_19_5dBm); // volle Leistung

  wifi_country_t country = {"DE", 1, 13, WIFI_COUNTRY_POLICY_MANUAL};
  esp_wifi_set_country(&country);

  WiFi.onEvent(onWiFiEvent);
  WiFi.begin(ssid, password);

  wl_status_t res = (wl_status_t)WiFi.waitForConnectResult(8000);
  if (res != WL_CONNECTED)
    Serial.printf("[WiFi] Erstverbindung fehlgeschlagen (%d)\n", res);
}

// Kleine Pause zwischen HTTP-Calls, damit der Stack atmen kann
static inline void net_yield(uint32_t ms=15) { vTaskDelay(pdMS_TO_TICKS(ms)); }

// OpenHAB fetch mit 1x Retry
bool oh_get_float(const String& item, float& out) {
  for (int attempt = 0; attempt < 2; ++attempt) {
    float v = openHABClient.getItemStateFloat(item);
    
    //debug
    Serial.println("Item: " + item + " Value: " + String(v));
    
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

static lv_chart_series_t* s_warmwasser = nullptr; // PRIMARY_Y

static lv_coord_t strompreiseHeute_array[96];
static lv_coord_t strompreiseMorgen_array[96];

static lv_coord_t stromverbrauch_array[96];
static lv_coord_t warmwassergrad_array[96];

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

  lv_coord_t strompreiseHeute[96];
  lv_coord_t strompreiseMorgen[96];
  lv_coord_t strompreisMax;
  lv_coord_t verbrauch15[96];

  lv_coord_t vmaxGrafik; // max. Verbrauch in den letzten 60 Minuten
  float preisaktuell;
  float verbrauch; // aktueller Verbrauch in Watt
  lv_coord_t verbrauch60min[60];
  lv_coord_t vmaxGrafikverbrauch; // max. Verbrauch in den letzten 60 Minuten
  
  //Stromtageskosten
  float stromtageskosten = 0.0;
  //Aktueller Verbrauch in Watt
  float aktuellerVerbrauch = 0.0;
  
  float stromeinspeisung = 0.0; // in KW
  float stromverbrauch = 0.0; // in KW

  int batteryLevel = 0; // in Prozent
  float batteryVerbrauch = 0.0; // in Watt

  float warmwassergrad = 0.0; // in Grad Celsius
  lv_coord_t Warmwassergrad_array[96];


  String WPTag = ""; // Heizprogramm der Wärmepumpe für heute
  String WPNacht = ""; // Heizprogramm der Wärmepumpe für die Nacht
  String WPStatus = ""; // Aktives Heizprogramm der Wärmepumpe für heute
  float WPWatt = 0.0; // Aktives Heizprogramm der Wärmepumpe für die Nacht

  bool ok; // mind. etwas erfolgreich?
} data_packet_t;

static volatile bool g_data_ready = false;
static data_packet_t g_data;
static portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;

// ====================== Hintergrund-Task: OpenHAB-Fetch ======================

const uint32_t UPDATE_MS_OK   = 30000;
const uint32_t UPDATE_MS_FAIL = 10000;

void data_task(void* pv) {
  uint32_t nextWait = UPDATE_MS_OK;

  for (;;) {
    if (!WiFi.isConnected()) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    // Kann ich feststellen welech Sceen aktuell angezeigt wird?
    

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
        for (int i = 0; i < 96; ++i) local.strompreiseHeute[i] = round_up_step(arr[i], 1); // Eurocent
        local.ok = true;
    }
    arr = openHABClient.getItemStateFloatArray("Strompreisverlauf", count);
    Serial.println("Count: " + String(count));
    if (arr && count == 96) {
        for (int i = 0; i < 96; ++i) local.strompreiseHeute[i] = round_up_step(arr[i], 1); // Eurocent
        local.ok = true;
    }
    
    count = 0;
    arr = openHABClient.getItemStateFloatArray("tempHistoryItem", count);
    Serial.println("Count: " + String(count));
    if (arr && count > 0) {
      int n = count;
      if (n > 96) n = 96;

      // Fülle vorhandene Werte
      for (int i = 0; i < n; ++i) {
        local.Warmwassergrad_array[i] = (lv_coord_t)arr[i]; // Grad Celsius (Ganzzahl)
      }

    
      for (int i = n; i < 96; ++i) {
        local.Warmwassergrad_array[i] = 0; // Rest mit 0 auffüllen
      }

      local.ok = true;
    } else {
      Serial.println("tempHistoryItem: keine oder ungueltige Daten");
      // optional: mit 0 auffüllen
      for (int i = 0; i < 96; ++i) local.Warmwassergrad_array[i] = 0;
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
    
    if (oh_get_float("Strompreis_Current", t)) { 
        local.preisaktuell = t; 
        local.ok = true; 
     
    } else {
        Serial.println("Failed to fetch Strompreis_Current");
    }

    if (oh_get_float("kwverbraucht", t)) { 
        local.verbrauch = t;
        local.stromverbrauch = t; 
        local.ok = true; 
//    Serial.println("Fetched kwverbraucht: " + String(t));
    } else {
        Serial.println("Failed to fetch kwverbraucht");
    }

    // get Float Items
    if (oh_get_float("SMA_Batt_SoC", t)) { 
        local.batteryLevel = (int)t; 
        local.ok = true;
    } 
    else {
        Serial.println("Failed to fetch batteryLevel");
    }

    if (oh_get_float("SMA_Batt_Leistung", t)) { 
        local.batteryVerbrauch = t; 
        local.ok = true;
    } 
    else {
        Serial.println("Failed to fetch batteryVerbrauch");
    }


  //  float stromtageskosten = 0.0; Item Stromtageskosten
    if (oh_get_float("Stromtageskosten", t)) { 
        local.stromtageskosten = t; 
        local.ok = true;
    } 
    else {
        Serial.println("Failed to fetch Stromtageskosten");
    }

  //  float aktuellerVerbrauch = 0.0; Item  StromAktuell
    if (oh_get_float("StromAktuell", t)) { 
        local.aktuellerVerbrauch = t; 
        local.ok = true;
    } 
    else {
        Serial.println("Failed to fetch StromAktuell");
    }


  //  float stromeinspeisung = 0.0; // in KW kwproduziert
    if (oh_get_float("kwproduziert", t)) { 
        local.stromeinspeisung = t; 
        local.ok = true;
    } 
    else {
        Serial.println("Failed to fetch kwproduziert");
    }


// float warmwassergrad = 0.0; // in Grad Celsius Item Temp_Sensor6
    if (oh_get_float("Temp_Sensor6", t)) { 
        local.warmwassergrad = t; 
        local.ok = true;
    } 
    else {
        Serial.println("Failed to fetch Temp_Sensor6");
    }

  // String WPTag = ""; // Heizprogramm der Wärmepumpe für heute Item CheapestTimes_Day
    {
        String s = openHABClient.getItemState("CheapestTimes_Day");
        if (s.length() > 0) { local.WPTag = s; local.ok = true; } 
        else {
            Serial.println("Failed to fetch CheapestTimes_Day");
        }
    }
  // String WPNacht = ""; // Heizprogramm der Wärmepumpe für die Nacht
    {
        String s = openHABClient.getItemState("CheapestTimes_Night");
        if (s.length() > 0) { local.WPNacht = s; local.ok = true; } 
        else {
            Serial.println("Failed to fetch CheapestTimes_Night");
        }
    }

  
  // String WPStatus = ""; // Aktives Heizprogramm der Wärmepumpe für heute  Item Relay2
    {
        String s = openHABClient.getItemState("Relay2");
        if (s.length() > 0) {
            if (s == "ON")
            {
                local.WPStatus = "Abgeschaltet";
                /* code */
            }
            else if (s == "OFF")
            {
                local.WPStatus = "Eingeschaltet";
            }
            else
            {
                local.WPStatus = s; // Sonstige Zustände direkt übernehmen  

            }

            
             local.ok = true; } 
        else {
            Serial.println("Failed to fetch Relay2");
        }
    }
  // float WPWatt = 0.0; // Aktives Heizprogramm der Wärmepumpe für die Nacht ITEm kwwp
    if (oh_get_float("WPAktuell", t)) { 
        local.WPWatt = t; 
        local.ok = true;
    } else {
        Serial.println("Failed to fetch kwwp");
    }

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

  wifi_init_stable();  // stabile WLAN-Initialisierung

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

  s_warmwasser = lv_chart_add_series(uic_GrafikWW, lv_color_hex(0xFF0000), LV_CHART_AXIS_PRIMARY_Y); // Rot für Warmwasser
  lv_chart_set_ext_y_array(uic_GrafikWW, s_warmwasser, warmwassergrad_array); // X-Achse automatisch

  // Next-Button Event (ui_btnNext oder ui_btn_Next)
  lv_obj_t* nextBtn = ui_btnNext ? ui_btnNext : ui_btn_Next;
  if (nextBtn) {
    lv_obj_add_flag(nextBtn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_foreground(nextBtn);
    lv_obj_add_event_cb(nextBtn, on_next, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_pad_all(nextBtn, 10, 0);
  }

  // Hintergrund-Task (Core 0: WiFi-Stack)
  xTaskCreatePinnedToCore(data_task, "data_task", 12288, nullptr, 1, nullptr, 1);

 

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
      warmwassergrad_array[i] = local.Warmwassergrad_array[i];
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


    lv_chart_refresh(uic_Grafikverbrauch);



    if (local.vmaxGrafik > y2_max) {
      y2_max = round_up_step(local.vmaxGrafik, 200);
      lv_chart_set_range(uic_Grafik, LV_CHART_AXIS_SECONDARY_Y, 0, y2_max);
    }
   
      lv_chart_refresh(uic_Grafik);

  
   // Serial.println("Verbrauch: " + String(local.verbrauch) + " Preis: " + String(local.preisaktuell));
    
    lv_label_set_text(uic_kwheute,  String(local.verbrauch, 2).c_str());
    lv_label_set_text(uic_Preis, String(local.preisaktuell, 2).c_str());
    lv_label_set_text(uic_aktuell, String(local.aktuellerVerbrauch, 0).c_str());
    lv_label_set_text(uic_tageskosten, String(local.stromtageskosten, 2).c_str());
    lv_label_set_text(uic_kwproduziert, String(local.stromeinspeisung, 2).c_str());
    lv_label_set_text(uic_kwverbraucht, String(local.stromverbrauch, 2).c_str());
    lv_label_set_text(uic_TempWarmwasser, String(local.warmwassergrad, 2).c_str());
    lv_label_set_text(uic_WPTag, local.WPTag.c_str());  
    lv_label_set_text(uic_WPNacht, local.WPNacht.c_str());
    lv_label_set_text(uic_WPStatus, local.WPStatus.c_str());
    lv_label_set_text(uic_WPWatt, String(local.WPWatt, 2).c_str());
  
    // Setze in der Bar uic_Netz local aktuellen Verbrauch in Watt  als Value
    if (uic_Netz)
    {
        
      
      lv_bar_set_value(uic_Netz, (int)local.aktuellerVerbrauch, LV_ANIM_ON);

        if (local.aktuellerVerbrauch > 0)
        {
          // Dann rot
          lv_obj_set_style_bg_grad_color(uic_Netz, lv_color_hex(0xFF0000), LV_PART_INDICATOR | LV_STATE_DEFAULT);
       
        }
        else
        {
          // Dann grün
          lv_obj_set_style_bg_grad_color(uic_Netz, lv_color_hex(0x00FF00), LV_PART_INDICATOR | LV_STATE_DEFAULT);
        }
        // Fülle uci_S4Netz mit dem aktuellen Verbrauch in Watt eränze die Einheit " W"
        if (ui_Screen4 && uic_Netz)
        {
            lv_label_set_text_fmt(ui_S4Netz, "%d Watt", (int)local.aktuellerVerbrauch);
        } 
    }

    if(uic_Bat)
    {
        
         
        if (uic_Bat)
        {
        if (local.batteryVerbrauch < 0)
        { // Dann rot


          lv_obj_set_style_bg_color(uic_Bat, lv_color_hex(0xFF0000), LV_PART_INDICATOR | LV_STATE_DEFAULT);
          lv_bar_set_value(uic_Bat, (int)local.batteryVerbrauch * -1, LV_ANIM_ON);
        }
        else
        { // Dann grün

          lv_obj_set_style_bg_grad_color(uic_Bat, lv_color_hex(0x00FF00), LV_PART_INDICATOR | LV_STATE_DEFAULT);
          lv_bar_set_value(uic_Bat, (int)local.batteryVerbrauch, LV_ANIM_ON);
        }
            lv_label_set_text_fmt(uic_s4Bat, "%d Watt", (int)local.batteryVerbrauch);
        } 
    }

    if(uic_Bateriestatus)
    {
        lv_bar_set_value(uic_Bateriestatus, local.batteryLevel, LV_ANIM_ON);
        // Fülle uci_Batterie mit dem aktuellen Batteriestand in Prozent
        if (uic_Bateriestatus)
        {
            lv_label_set_text_fmt(uic_S4Bateriestatus, "%d %%", local.batteryLevel);
        } 
    }

    


    // wpWatt in der Bar uic_WP
    if (uic_WPP)
    {
        lv_bar_set_value(uic_WPP, (int)local.WPWatt, LV_ANIM_ON);
        // Fülle uci_S4WP mit dem aktuellen WP Watt
        if (ui_Screen4 && uic_WPP)
        {
            lv_label_set_text_fmt(uic_s4WP, "%d Watt", (int)local.WPWatt);
        }

      }
    // kwverbraucht in der Bar uic_Verbrauch
    if (uic_Verbrauch)
    {
        lv_bar_set_value(uic_Verbrauch, (int)local.stromverbrauch, LV_ANIM_ON);
        // Fülle uci_S4Verbrauch mit dem aktuellen Verbrauch in Watt
        if (uic_Verbrauch)
        {
            lv_label_set_text_fmt(uic_s4Bezug, "%d KW", (int)local.stromverbrauch);
        } 
      }
    // kvproduziert in der Bar uic_Einspeisung
    if (uic_Eingespeist)
    {
        lv_bar_set_value(uic_Eingespeist, (int)local.stromeinspeisung, LV_ANIM_ON);
     // Fülle uci_S4Einspeisung mit dem aktuellen Verbrauch in Watt
        if (uic_Eingespeist)
        {
            lv_label_set_text_fmt(uic_S4Eing, "%d KW", (int)local.stromeinspeisung);
        } 
      }
    
}

  delay(2); // reaktiv bleiben
}
