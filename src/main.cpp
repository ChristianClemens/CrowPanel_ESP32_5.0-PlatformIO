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
String bearer = "oh.ESPAnzeige.eJK1Z47f7e1KBGLqOJXayr8cM8dhaUIk8lnhgJvD6J01uSkd2L2ruyJDp391nOKtQFGa8NsVM9XgOflC1w";
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

// WiFi Reconnect-Management
static unsigned long last_wifi_check = 0;
static int wifi_reconnect_attempts = 0;
static bool wifi_force_reconnect = false;

void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_START:
      Serial.println("[WiFi] STA Started");
      WiFi.begin(ssid, password);
      break;
      
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.printf("[WiFi] ✓ Verbunden: %s (RSSI: %d dBm)\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
      wifi_reconnect_attempts = 0;
      wifi_force_reconnect = false;
      break;
      
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      {
        uint8_t reason = info.wifi_sta_disconnected.reason;
        Serial.printf("[WiFi] ⚠ Disconnected - Reason: %d (", reason);
        
        switch (reason) {
          case WIFI_REASON_NO_AP_FOUND:
            Serial.print("Access Point not found"); break;
          case WIFI_REASON_AUTH_FAIL:
            Serial.print("Authentication failed"); break;
          case WIFI_REASON_ASSOC_LEAVE:
            Serial.print("Association leave"); break;
          case WIFI_REASON_ASSOC_TOOMANY:
            Serial.print("Too many associations"); break;
          case WIFI_REASON_NOT_AUTHED:
            Serial.print("Not authenticated"); break;
          case WIFI_REASON_NOT_ASSOCED:
            Serial.print("Not associated"); break;
          case WIFI_REASON_ASSOC_FAIL:
            Serial.print("Association failed"); break;
          case WIFI_REASON_HANDSHAKE_TIMEOUT:
            Serial.print("Handshake timeout"); break;
          case WIFI_REASON_CONNECTION_FAIL:
            Serial.print("Connection failed"); break;
          case WIFI_REASON_AP_TSF_RESET:
            Serial.print("AP TSF reset"); break;
          case WIFI_REASON_ROAMING:
            Serial.print("Roaming"); break;
          case WIFI_REASON_BEACON_TIMEOUT:
            Serial.print("Beacon timeout - weak signal!"); break;
          default:
            Serial.printf("Unknown reason %d", reason); break;
        }
        Serial.println(")");
        
        wifi_reconnect_attempts++;
        
        // Intelligente Reconnect-Strategie
        if (wifi_reconnect_attempts < 5) {
          Serial.printf("[WiFi] Reconnect attempt %d/5 in 2 seconds...\n", wifi_reconnect_attempts);
          vTaskDelay(pdMS_TO_TICKS(2000));
          WiFi.begin(ssid, password);
        } else if (wifi_reconnect_attempts == 5) {
          Serial.println("[WiFi] 🔄 Full WiFi restart after 5 failed attempts");
          WiFi.disconnect(true);
          vTaskDelay(pdMS_TO_TICKS(1000));
          WiFi.mode(WIFI_OFF);
          vTaskDelay(pdMS_TO_TICKS(1000));
          Serial.println("[WiFi] Waiting for auto-restart...");
        }
      }
      break;
      
    case ARDUINO_EVENT_WIFI_STA_AUTHMODE_CHANGE:
      Serial.println("[WiFi] Auth mode changed");
      break;
      
    default:
      Serial.printf("[WiFi] Event: %d\n", event);
      break;
  }
}

// ---- Stabiles WLAN-Setup mit erweiterten Einstellungen ----
void wifi_init_stable() {
  Serial.println("[WiFi] Initializing WiFi...");
  
  // Vollständiger WiFi-Reset
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  vTaskDelay(pdMS_TO_TICKS(500));
  
  WiFi.persistent(false);              // Keine Speicherung in Flash
  WiFi.mode(WIFI_STA);                 // Station Mode
  WiFi.setSleep(WIFI_PS_NONE);         // Kein Power Save 
  WiFi.setHostname("esp32s3-panel");
  
  // Erweiterte WiFi-Einstellungen für Stabilität
  WiFi.setTxPower(WIFI_POWER_19_5dBm); // Maximale Sendeleistung
  WiFi.setAutoReconnect(true);         // Automatischer Reconnect
  
  // Länder-spezifische Einstellungen
  wifi_country_t country = {"DE", 1, 13, WIFI_COUNTRY_POLICY_MANUAL};
  esp_wifi_set_country(&country);
  
  // Erweiterte ESP32-spezifische Einstellungen
  esp_wifi_set_ps(WIFI_PS_NONE);      // Power Save komplett aus
  
  // Event Handler registrieren
  WiFi.onEvent(onWiFiEvent);
  
  Serial.printf("[WiFi] Connecting to '%s'...\n", ssid);
  WiFi.begin(ssid, password);

  // Warte auf Verbindung mit detailliertem Status
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    vTaskDelay(pdMS_TO_TICKS(500));
    Serial.print(".");
    attempts++;
    
    if (attempts == 10) {
      Serial.println();
      Serial.println("[WiFi] Taking longer than expected, checking signal...");
    }
  }
  
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WiFi] ✓ Connected successfully!\n");
    Serial.printf("[WiFi] IP: %s, RSSI: %d dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
  } else {
    Serial.printf("[WiFi] ✗ Initial connection failed (Status: %d)\n", WiFi.status());
  }
  
  wifi_reconnect_attempts = 0;
}

// WiFi-Gesundheitsüberwachung
void wifi_health_check() {
  static unsigned long last_health_check = 0;
  static int last_rssi = 0;
  
  if (millis() - last_health_check > 60000) { // Alle 60 Sekunden
    last_health_check = millis();
    
    if (WiFi.isConnected()) {
      int current_rssi = WiFi.RSSI();
      
      if (current_rssi < -90) {
        Serial.printf("[WiFi] ⚠ Weak signal: %d dBm (critical!)\n", current_rssi);
      } else if (current_rssi < -70) {
        Serial.printf("[WiFi] ⚠ Weak signal: %d dBm\n", current_rssi);
      }
      
      // Drastische Signaländerung?
      if (abs(current_rssi - last_rssi) > 20 && last_rssi != 0) {
        Serial.printf("[WiFi] ⚠ Signal changed dramatically: %d → %d dBm\n", last_rssi, current_rssi);
      }
      
      last_rssi = current_rssi;
      
      // Prüfe auf "Zombie"-Verbindung (verbunden aber keine Kommunikation)
      if (current_rssi < -95) {
        Serial.println("[WiFi] 🔄 Signal too weak, forcing reconnect...");
        wifi_force_reconnect = true;
        WiFi.disconnect();
      }
    } else {
      Serial.println("[WiFi] ✗ Not connected during health check");
    }
  }
}

// Kleine Pause zwischen HTTP-Calls, damit der Stack atmen kann
static inline void net_yield(uint32_t ms=15) { 
  vTaskDelay(pdMS_TO_TICKS(ms)); 
  wifi_health_check(); // WiFi-Gesundheit prüfen
}

// OpenHAB fetch mit robuster Fehlerbehandlung
bool oh_get_float(const String& item, float& out) {
  static int consecutive_failures = 0;
  
  for (int attempt = 0; attempt < 3; ++attempt) {
    if (!WiFi.isConnected()) {
      Serial.println("[OH] WiFi disconnected during " + item + " fetch");
      return false;
    }
    
    float v = openHABClient.getItemStateFloat(item);
    
    // Debug mit mehr Kontext
    if (!isnan(v)) {
      Serial.println("[OH] ✓ " + item + ": " + String(v));
      out = v;
      consecutive_failures = 0;
      return true;
    } else {
      Serial.println("[OH] ✗ " + item + ": invalid (NaN) (" + String(v) + ") attempt " + String(attempt + 1));
    }
    
    net_yield(50 + (attempt * 100)); // Längere Wartezeit bei Wiederholungsversuchen
  }
  
  consecutive_failures++;
  Serial.println("[OH] ⚠ Failed to fetch " + item + " after 3 attempts (consecutive fails: " + String(consecutive_failures) + ")");
  
  // Nach 10 aufeinanderfolgenden Fehlern OpenHAB-Client neu initialisieren
  if (consecutive_failures >= 10) {
    Serial.println("[OH] 🔄 Reinitializing OpenHAB client due to consecutive failures");
    openHABClient = OpenHABClient(openhabServer.toString(), 8080, bearer);
    consecutive_failures = 0;
    net_yield(1000); // 1 Sekunde Pause nach Neuinitialisierung
  }
  
  return false;
}

// OpenHAB String fetch mit robuster Fehlerbehandlung
bool oh_get_string(const String& item, String& out) {
  static int consecutive_string_failures = 0;
  
  for (int attempt = 0; attempt < 3; ++attempt) {
    if (!WiFi.isConnected()) {
      Serial.println("[OH] WiFi disconnected during " + item + " string fetch");
      return false;
    }
    
    String s = openHABClient.getItemState(item);
    
    if (s.length() > 0 && s != "NULL" && s != "UNDEF") {
      Serial.println("[OH] ✓ " + item + ": '" + s + "'");
      out = s;
      consecutive_string_failures = 0;
      return true;
    } else {
      Serial.println("[OH] ✗ " + item + ": empty/invalid ('" + s + "') attempt " + String(attempt + 1));
    }
    
    net_yield(50 + (attempt * 100));
  }
  
  consecutive_string_failures++;
  Serial.println("[OH] ⚠ Failed to fetch string " + item + " after 3 attempts (consecutive fails: " + String(consecutive_string_failures) + ")");
  
  if (consecutive_string_failures >= 10) {
    Serial.println("[OH] 🔄 Reinitializing OpenHAB client due to string fetch failures");
    openHABClient = OpenHABClient(openhabServer.toString(), 8080, bearer);
    consecutive_string_failures = 0;
    net_yield(1000);
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
static lv_chart_series_t* s_pricesmorgen = nullptr;   // PRIMARY_Y
static lv_chart_series_t* s_power  = nullptr;   // SECONDARY_Y

static lv_chart_series_t* s_verbrauch = nullptr; // PRIMARY_Y
static lv_chart_series_t* s_einspeisung = nullptr; // PRIMARY_Y

static lv_chart_series_t* s_warmwasser = nullptr; // PRIMARY_Y

static lv_coord_t strompreiseHeute_array[96];


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

  lv_coord_t strompreisMax;
  lv_coord_t strompreisMin;
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
  int batteryReserveSoc = 0; // Reserve SoC in Prozent
  
  // SOC_15: 96 SOC-Werte für den Tag (alle 15 Minuten) - sichere Implementierung
  lv_coord_t soc15Values[96];
  bool soc15Updated = false;

  float warmwassergrad = 0.0; // in Grad Celsius



  String WPTag = ""; // Heizprogramm der Wärmepumpe für heute
  String WPNacht = ""; // Heizprogramm der Wärmepumpe für die Nacht
  String WPStatus = ""; // Aktives Heizprogramm der Wärmepumpe für heute
  float WPWatt = 0.0; // Aktives Heizprogramm der Wärmepumpe für die Nacht

  bool ok; // mind. etwas erfolgreich?
} data_packet_t;

static volatile bool g_data_ready = false;
static data_packet_t g_data;
static portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;

// Chart-Serie für SOC-Daten - sichere Implementierung
static lv_chart_series_t * g_soc_series = NULL;
static lv_coord_t g_soc_chart_data[96] = {0};
static bool g_chart_initialized = false;

// ====================== Sichere Chart-Initialisierung ======================
void init_batterie_chart() {
    if (g_chart_initialized) {
        return; // Bereits initialisiert - kein Log mehr
    }
    
    // Sichere Chart-Validierung
    if (!ui_BatterieChart) {
        Serial.println("[INIT] ui_BatterieChart nicht verfügbar");
        return;
    }
    
    if (!lv_obj_is_valid(ui_BatterieChart)) {
        Serial.println("[INIT] ui_BatterieChart ungültig");
        return;
    }
    
    try {
        // Chart-Konfiguration
        lv_chart_set_type(ui_BatterieChart, LV_CHART_TYPE_LINE);
        lv_chart_set_point_count(ui_BatterieChart, 96);
        
        // SOC-Serie hinzufügen
        g_soc_series = lv_chart_add_series(ui_BatterieChart, lv_palette_main(LV_PALETTE_GREEN), LV_CHART_AXIS_PRIMARY_Y);
        
        if (g_soc_series) {
            // Y-Achsen-Bereich für SOC (0-100%)
            lv_chart_set_range(ui_BatterieChart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
            
            // Initiale Daten setzen
            for (int i = 0; i < 96; i++) {
                g_soc_chart_data[i] = 0;
            }
            
            // Externe Daten-Array verknüpfen
            lv_chart_set_ext_y_array(ui_BatterieChart, g_soc_series, g_soc_chart_data);
            
            g_chart_initialized = true;
            Serial.println("[INIT] BatterieChart SOC-Serie sicher initialisiert");
        } else {
            Serial.println("[INIT-ERROR] Konnte SOC-Serie nicht erstellen");
        }
    } catch(...) {
        Serial.println("[INIT-ERROR] Chart-Initialisierung fehlgeschlagen");
    }
}

// ====================== Hintergrund-Task: OpenHAB-Fetch ======================


void data_task(void* pv) {
  // Zähler für verschiedene Update-Intervalle
  int cycle_counter = 0;
  unsigned long last_success_time = millis();
  int connection_check_counter = 0;
  
  for (;;) {
    // Erweiterte WiFi-Verbindungsprüfung
    if (!WiFi.isConnected()) {
      Serial.println("[DataTask] WiFi disconnected, waiting for reconnection...");
      vTaskDelay(pdMS_TO_TICKS(5000));
      continue;
    }
    
    // Periodische Verbindungsqualitätsprüfung mit erweiterten Diagnosen
    connection_check_counter++;
    if (connection_check_counter % 6 == 0) { // Alle Minute (6 * 10s)
      int rssi = WiFi.RSSI();
      uint32_t free_heap = ESP.getFreeHeap();
      
      Serial.printf("[DataTask] WiFi: RSSI=%d dBm, IP=%s, Heap=%d bytes, Reconnects=%d\n", 
                   rssi, WiFi.localIP().toString().c_str(), free_heap, wifi_reconnect_attempts);
      
      // Warne bei kritischem Signal
      if (rssi < -80) {
        Serial.printf("[DataTask] ⚠ Signal critical: %d dBm - potential connection issues!\n", rssi);
      }
      
      // Memory-Leak-Warnung
      if (free_heap < 50000) {
        Serial.printf("[DataTask] ⚠ Low memory: %d bytes remaining!\n", free_heap);
      }
      
      // Wenn mehr als 10 Minuten keine erfolgreichen Daten
      if (millis() - last_success_time > 600000) {
        Serial.println("[DataTask] ⚠ No successful data for >10min, forcing complete WiFi restart");
        wifi_force_reconnect = true;
        WiFi.disconnect();
        vTaskDelay(pdMS_TO_TICKS(1000));
        wifi_init_stable();
        vTaskDelay(pdMS_TO_TICKS(5000));
        last_success_time = millis(); // Reset timer
      }
      
      // Bei zu vielen Reconnects kompletten Neustart
      if (wifi_reconnect_attempts > 10) {
        Serial.println("[DataTask] 🔄 Too many WiFi reconnects - scheduling ESP restart in 30s");
        vTaskDelay(pdMS_TO_TICKS(30000));
        ESP.restart();
      }
    }

    cycle_counter++;
    data_packet_t local{}; local.ok = false;
    bool any_success_this_cycle = false;
    
    // Kopiere vorherige Werte als Fallback
    taskENTER_CRITICAL(&g_mux);
    local = g_data;
    taskEXIT_CRITICAL(&g_mux);
    
    Serial.printf("[DataTask] Cycle %d starting (WiFi RSSI: %d dBm)\n", cycle_counter, WiFi.RSSI());
    // ========== HISTORISCHE ARRAYS - nur alle 15 Minuten (90 Zyklen à 10s) ==========
    if (cycle_counter % 90 == 1) {
      Serial.println("[DataTask] Updating historical arrays...");
      
      // Item stromtagesverbrauch_15 gibt  96 Werte als Array zurück jeder 15 Minuten ein Wert ab 00:00
      int count = 0;
      float* arr = openHABClient.getItemStateFloatArray("stromtagesverbrauch_15", count);
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
      if (arr && count == 96) {
          for (int i = 0; i < 96; ++i) local.strompreiseHeute[i] = round_up_step(arr[i], 1); // Eurocent
          local.ok = true;
      }

      // SOC_Batt_15 gibt 96 SOC-Werte für den Tag zurück (alle 15 Minuten ab 00:00)
      count = 0;
      arr = openHABClient.getItemStateFloatArray("SOC_Batt_15", count);
      if (arr && count == 96) {
        // Sichere Datenübertragung mit Bereichsprüfung
        for (int i = 0; i < 96; ++i) {
          float val = arr[i];
          if (val >= 0.0 && val <= 100.0) {
            local.soc15Values[i] = (lv_coord_t)round(val);
          } else {
            local.soc15Values[i] = 0; // Fallback für ungültige Werte
          }
        }
        local.soc15Updated = true;
        local.ok = true;
        Serial.println("[DataTask] SOC_Batt_15 Batterieverlauf sicher aktualisiert");
      } else {
        Serial.printf("[DataTask] SOC_Batt_15 Fehler: count=%d\n", count);
      }

      count = 0;
      arr = openHABClient.getItemStateFloatArray("tempHistoryItem", count);
      if (arr && count > 0) {
        int n = count;
        if (n > 96) n = 96;
        // TODO: Add missing code here for processing tempHistoryItem
      }

      // Imt stromtagesverbrauch_60min gibt  60 Werte der letzen 60 Minuten zurück
      count = 0;
      arr = openHABClient.getItemStateFloatArray("stromstundenverbrauch", count);
      if (arr && count == 60) {
          local.vmaxGrafikverbrauch = 0;
          for (int i = 0; i < 60; ++i) {
              local.verbrauch60min[i] = arr[i]; // kWh -> Wh
              if (abs(local.verbrauch60min[i]) > local.vmaxGrafikverbrauch) local.vmaxGrafikverbrauch = abs(local.verbrauch60min[i]);
          }
          local.ok = true;
      }
      net_yield();
    }

    // ========== AKTUELLE WERTE - jeden Zyklus (10 Sekunden) ==========
    float t;
    
    // Häufig benötigte aktuelle Werte
    if (oh_get_float("kwverbraucht", t)) { 
        local.verbrauch = t;
        local.stromverbrauch = t; 
        local.ok = true;
        any_success_this_cycle = true;
    }
    
    if (oh_get_float("StromAktuell", t)) { 
        local.aktuellerVerbrauch = t; 
        local.ok = true;
        any_success_this_cycle = true;
    }
    
    if (oh_get_float("kwproduziert", t)) { 
        local.stromeinspeisung = t; 
        local.ok = true;
        any_success_this_cycle = true;
    }
    
    if (oh_get_float("SMA_Batt_SoC", t)) { 
        local.batteryLevel = (int)t; 
        local.ok = true;
        any_success_this_cycle = true;
    }
    
    if (oh_get_float("SMA_Batt_Leistung", t)) { 
        local.batteryVerbrauch = t; 
        local.ok = true;
        any_success_this_cycle = true;
    }
    
    if (oh_get_float("SMA_ReserveSoc", t)) { 
        local.batteryReserveSoc = (int)t; 
        local.ok = true;
        any_success_this_cycle = true;
    }
    
    net_yield();
    
    // ========== PREISINFORMATIONEN - alle 5 Minuten (30 Zyklen à 10s) ==========
    if (cycle_counter % 30 == 0) {
      Serial.println("[DataTask] Updating price information...");
      
      if (oh_get_float("Strompreis_Current", t)) { 
          local.preisaktuell = t; 
          local.ok = true;
          any_success_this_cycle = true;
      }
      
      if (oh_get_float("Stromtageskosten", t)) { 
          local.stromtageskosten = t; 
          local.ok = true;
          any_success_this_cycle = true;
      }
      
      if (oh_get_float("Temp_Sensor6", t)) { 
          local.warmwassergrad = t; 
          local.ok = true;
          any_success_this_cycle = true;
      }
      net_yield();
    }
    
    // ========== WÄRMEPUMPEN-DATEN - alle 2 Minuten (12 Zyklen à 10s) ==========
    if (cycle_counter % 12 == 0) {
      Serial.println("[DataTask] Updating heat pump data...");
      
      // Heizprogramm der Wärmepumpe für heute Item CheapestTimes_Day
      String s;
      if (oh_get_string("CheapestTimes_Day", s)) {
          local.WPTag = s;
          local.ok = true;
          any_success_this_cycle = true;
      }
      
      // Heizprogramm der Wärmepumpe für die Nacht
      if (oh_get_string("CheapestTimes_Night", s)) {
          local.WPNacht = s;
          local.ok = true;
          any_success_this_cycle = true;
      }
      
      // WP Status von Relay2
      if (oh_get_string("Relay2", s)) {
          if (s == "ON") {
              local.WPStatus = "Abgeschaltet";
          } else if (s == "OFF") {
              local.WPStatus = "Eingeschaltet";
          } else {
              local.WPStatus = s; // Sonstige Zustände direkt übernehmen  
          }
          local.ok = true;
          any_success_this_cycle = true;
      }
      
      // WP Watt
      if (oh_get_float("WPAktuell", t)) { 
          local.WPWatt = t; 
          local.ok = true;
          any_success_this_cycle = true;
      }
      net_yield();
    }

    net_yield();
    
    // Update Erfolgszeit wenn wir wenigstens etwas erhalten haben
    if (any_success_this_cycle) {
      last_success_time = millis();
      Serial.printf("[DataTask] ✓ Cycle %d completed successfully\n", cycle_counter);
    } else {
      Serial.printf("[DataTask] ✗ Cycle %d: no successful data fetched\n", cycle_counter);
    }
    
    // ---- Übergabe an UI (nur wenn zumindest etwas ok) ----
    taskENTER_CRITICAL(&g_mux);
        g_data = local;
        g_data_ready = local.ok;
    taskEXIT_CRITICAL(&g_mux);

    // Warte 10 Sekunden bis zum nächsten Durchlauf
    vTaskDelay(pdMS_TO_TICKS(10000));
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
  s_pricesmorgen = lv_chart_add_series(uic_Grafik, lv_color_hex(0x008800), LV_CHART_AXIS_PRIMARY_Y);

  s_power  = lv_chart_add_series(uic_Grafik, lv_color_hex(0x0000FF), LV_CHART_AXIS_SECONDARY_Y);
  
  s_verbrauch = lv_chart_add_series(uic_Grafikverbrauch, lv_color_hex(0xFF0000), LV_CHART_AXIS_PRIMARY_Y); // Rot für Verbrauch
  s_einspeisung = lv_chart_add_series(uic_Grafikverbrauch, lv_color_hex(0x00FF00), LV_CHART_AXIS_PRIMARY_Y); // Grün für Einspeisung

  lv_chart_set_ext_y_array(uic_Grafik, s_prices, strompreiseHeute_array);

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
  xTaskCreatePinnedToCore(data_task, "data_task", 12288, nullptr, 1, nullptr, 1);

 

  // Ich brauch einen 

  Serial.println("Setup done");
}

void loop() {
  // UI bedienen
  lv_timer_handler();

  // BatterieChart initialisieren (nur einmal)
  static bool chart_init_attempted = false;
  if (!chart_init_attempted) {
    chart_init_attempted = true;
    init_batterie_chart();
  }

  // neue Daten übernehmen (falls vorhanden)
  bool have = false; data_packet_t local{};
  taskENTER_CRITICAL(&g_mux);
    if (g_data_ready) { local = g_data; g_data_ready = false; have = true; }
  taskEXIT_CRITICAL(&g_mux);

  if (have) {
    if (ui_lblip) lv_label_set_text(ui_lblip, WiFi.localIP().toString().c_str());

    for (int i = 0; i < 96; ++i) {
      strompreiseHeute_array[i] = local.strompreiseHeute[i];
  

      stromverbrauch_array[i] = local.verbrauch15[i];
    

      // Ermittel Min und Max für den Strompreis
      if (local.strompreiseHeute[i] > local.strompreisMax) {
        local.strompreisMax = local.strompreiseHeute[i];
      }

     
      if (local.strompreiseHeute[i] < local.strompreisMin) {
        local.strompreisMin = local.strompreiseHeute[i];
      }

  
  
    }

    // Setze Min und Max für den Strompreis
    local.strompreisMin = local.strompreisMin == LV_COORD_MIN ? 0 : local.strompreisMin;
    local.strompreisMax = local.strompreisMax == LV_COORD_MAX ? 0 : local.strompreisMax;
    lv_chart_set_range(uic_Grafik, LV_CHART_AXIS_PRIMARY_Y, local.strompreisMin, local.strompreisMax);

    lv_chart_refresh(uic_Grafik);

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
        { // Laden - Grün (Batterie nimmt Energie auf)

          lv_obj_set_style_bg_color(uic_Bat, lv_color_hex(0x00FF00), LV_PART_INDICATOR | LV_STATE_DEFAULT);
          lv_bar_set_value(uic_Bat, (int)local.batteryVerbrauch * -1, LV_ANIM_ON);
        }
        else
        { // Entladen - Rot (Batterie gibt Energie ab)

          lv_obj_set_style_bg_color(uic_Bat, lv_color_hex(0xFF0000), LV_PART_INDICATOR | LV_STATE_DEFAULT);
          lv_bar_set_value(uic_Bat, (int)local.batteryVerbrauch, LV_ANIM_ON);
        }
            lv_label_set_text_fmt(uic_s4Bat, "%d Watt", (int)local.batteryVerbrauch);
        } 
    }

    // Batterie UI-Updates mit korrekten Element-Namen
    if (local.batteryLevel >= 0 && local.batteryLevel <= 100) {
        // Batterie-Status Bar mit Validierung (korrekte Namen: ui_Bateriestatus)
        if(ui_Bateriestatus && lv_obj_is_valid(ui_Bateriestatus)) {
            try {
                lv_bar_set_value(ui_Bateriestatus, local.batteryLevel, LV_ANIM_OFF);
                Serial.printf("[UI-OK] Batterie Bar (ui_Bateriestatus): %d%%\\n", local.batteryLevel);
            } catch(...) {
                Serial.println("[UI-ERROR] Batterie Bar update failed");
            }
        } else {
            Serial.println("[UI-WARNING] ui_Bateriestatus not available");
        }
        
        // Batterie-Status Label mit Validierung (korrekte Namen: ui_S4Bateriestatus)
        if (ui_S4Bateriestatus && lv_obj_is_valid(ui_S4Bateriestatus)) {
            try {
                lv_label_set_text_fmt(ui_S4Bateriestatus, "%d %%", local.batteryLevel);
                Serial.printf("[UI-OK] Batterie Label (ui_S4Bateriestatus): %d%%\\n", local.batteryLevel);
            } catch(...) {
                Serial.println("[UI-ERROR] Batterie Label update failed");
            }
        } else {
            Serial.println("[UI-WARNING] ui_S4Bateriestatus not available");
        }
    } else {
        Serial.printf("[UI-WARNING] Invalid battery level: %d%%\\n", local.batteryLevel);
    }
    
    // Debug: Batterie-Leistung im Serial ausgeben
    Serial.printf("[UI-DEBUG] Batterie: %d%%, Leistung: %.1fW\\n", local.batteryLevel, local.batteryVerbrauch);

    // Reserve SoC UI-Updates mit erweiterten Sicherheitsprüfungen
    if (local.batteryReserveSoc >= 0 && local.batteryReserveSoc <= 100) {
        // Bar-Update mit Validierung
        if(uic_resSoc && lv_obj_is_valid(uic_resSoc)) {
            try {
                lv_bar_set_value(uic_resSoc, local.batteryReserveSoc, LV_ANIM_OFF); // Ohne Animation
                Serial.printf("[UI-OK] Reserve SoC Bar: %d%%\n", local.batteryReserveSoc);
            } catch(...) {
                Serial.println("[UI-ERROR] Reserve SoC Bar update failed");
            }
        }
        
        // Label-Update mit Validierung  
        if(ui_resSoc && lv_obj_is_valid(ui_resSoc)) {
            try {
                lv_label_set_text_fmt(ui_resSoc, "%d %%", local.batteryReserveSoc);
                Serial.printf("[UI-OK] Reserve SoC Label: %d%%\n", local.batteryReserveSoc);
            } catch(...) {
                Serial.println("[UI-ERROR] Reserve SoC Label update failed");
            }
        }
    } else {
        Serial.printf("[UI-WARNING] Invalid Reserve SoC value: %d%%\n", local.batteryReserveSoc);
    }

    // BatterieChart mit SOC_Batt_15 Daten sicher aktualisieren
    if (local.soc15Updated && g_chart_initialized && g_soc_series) {
        if (ui_BatterieChart && lv_obj_is_valid(ui_BatterieChart)) {
            try {
                // Sichere Datenübertragung
                for (int i = 0; i < 96; i++) {
                    g_soc_chart_data[i] = local.soc15Values[i];
                }
                
                // Chart-Refresh ohne kritische Funktionen
                lv_obj_invalidate(ui_BatterieChart);
                
                Serial.println("[UI-OK] BatterieChart mit SOC_Batt_15 sicher aktualisiert");
            } catch(...) {
                Serial.println("[UI-ERROR] Chart-Update fehlgeschlagen");
            }
        }
        
        // Flag zurücksetzen
        taskENTER_CRITICAL(&g_mux);
        g_data.soc15Updated = false;
        taskEXIT_CRITICAL(&g_mux);
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
