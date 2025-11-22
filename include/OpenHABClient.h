#include <WiFi.h>
#include <HTTPClient.h>


class OpenHABClient {
  private:
    String serverIP;
    int port;
    String token; // optional für Authentifizierung

  public:
    OpenHABClient(const String& ip, int port = 8080, const String& bearerToken = "")
      : serverIP(ip), port(port), token(bearerToken) {}

    bool setItemState(const String& itemName, const String& state) {
      if (WiFi.status() != WL_CONNECTED) return false;

      HTTPClient http;
      String url = "http://" + serverIP + ":" + String(port) + "/rest/items/" + itemName + "/state";
      http.begin(url);
      http.addHeader("Content-Type", "text/plain");

      if (token.length() > 0)
        http.addHeader("Authorization", "Bearer " + token);

      int httpCode = http.PUT(state);
      http.end();
      return httpCode == 200 || httpCode == 202;
    }

    String getItemState(const String& itemName) {
      if (WiFi.status() != WL_CONNECTED) return "";

      HTTPClient http;
      String url = "http://" + serverIP + ":" + String(port) + "/rest/items/" + itemName + "/state";
      http.begin(url);

      if (token.length() > 0)
        http.addHeader("Authorization", "Bearer " + token);

      int httpCode = http.GET();
      String payload = (httpCode == 200) ? http.getString() : "";
      http.end();
      return payload;
    }

    float getItemStateFloat(const String& itemName) {
      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[OpenHAB] WiFi not connected for " + itemName);
        return NAN;
      }

      HTTPClient http;
      String url = "http://" + serverIP + ":" + String(port) + "/rest/items/" + itemName + "/state";
      http.begin(url);

      if (token.length() > 0)
        http.addHeader("Authorization", "Bearer " + token);

      int httpCode = http.GET();
      String payload = "";
      
      if (httpCode == 200) {
        payload = http.getString();
        payload.trim(); // Entferne Whitespace
      } else {
        Serial.println("[OpenHAB] HTTP Error " + String(httpCode) + " for " + itemName);
        http.end();
        return NAN;
      }
      
      http.end();
      
      // Debug-Ausgabe
      Serial.println("[OpenHAB] " + itemName + " raw response: '" + payload + "'");
      
      // Prüfe auf OpenHAB-spezifische ungültige Zustände
      if (payload.length() == 0 || payload == "NULL" || payload == "UNDEF" || payload == "-") {
        Serial.println("[OpenHAB] Invalid state for " + itemName + ": '" + payload + "'");
        return NAN;
      }

      // Konvertiere zu Float
      float result = payload.toFloat();
      
      // toFloat() gibt 0.0 zurück bei ungültigen Strings, aber auch bei echten Nullwerten
      // Prüfe ob der String wirklich eine Zahl repräsentiert
      if (result == 0.0 && payload != "0" && payload != "0.0" && payload != "0.00") {
        // Prüfe ob der String tatsächlich eine gültige Zahl ist
        bool isValidNumber = false;
        for (int i = 0; i < payload.length(); i++) {
          char c = payload[i];
          if (isdigit(c) || c == '.' || c == '-' || c == '+') {
            if (isdigit(c)) isValidNumber = true;
          } else {
            isValidNumber = false;
            break;
          }
        }
        
        if (!isValidNumber) {
          Serial.println("[OpenHAB] Invalid number format for " + itemName + ": '" + payload + "'");
          return NAN;
        }
      }

      return result;
    }

      // Liefert ein Array von Floats aus dem State eines Items, das ein Array als String liefert (z.B. "[1.0,2.0,3.0]")
      // Die Anzahl der gefundenen Werte wird in count gespeichert
      // Gibt nullptr zurück, wenn keine Verbindung besteht oder keine Werte gefunden wurden
      float* getItemStateFloatArray(const String& itemName, int& count) {
        count = 0;
        if (WiFi.status() != WL_CONNECTED) return nullptr;

        HTTPClient http;
        String url = "http://" + serverIP + ":" + String(port) + "/rest/items/" + itemName + "/state";
        http.begin(url);

        if (token.length() > 0)
        http.addHeader("Authorization", "Bearer " + token);

        int httpCode = http.GET();
        String payload = (httpCode == 200) ? http.getString() : "";
        http.end();

        Serial.println("Item: " + itemName + " State: " + payload);


        // Prüfe auf gültiges Array-Format
        // Erlaube auch Werte ohne eckige Klammern (reines Komma-separiertes Array)
        String values = payload;
        if (payload.length() >= 2 && payload[0] == '[' && payload[payload.length() - 1] == ']') {
          values = payload.substring(1, payload.length() - 1);
        }

        // Zähle die Anzahl der Werte
        int valueCount = 1;
        for (int i = 0; i < values.length(); ++i) {
          if (values[i] == ',') valueCount++;
        }

        float* floatArray = new float[valueCount];
        int idx = 0;
        int lastPos = 0;
        for (int i = 0; i <= values.length(); ++i) {
          if (i == values.length() || values[i] == ',') {
            String numStr = values.substring(lastPos, i);
            numStr.trim();
            floatArray[idx++] = numStr.toFloat();
            lastPos = i + 1;
          }
        }
        count = valueCount;
        return floatArray;
      }

};
