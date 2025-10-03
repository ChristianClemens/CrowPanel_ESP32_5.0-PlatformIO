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
      if (WiFi.status() != WL_CONNECTED) return 0.0;

      HTTPClient http;
      String url = "http://" + serverIP + ":" + String(port) + "/rest/items/" + itemName + "/state";
      http.begin(url);

      if (token.length() > 0)
        http.addHeader("Authorization", "Bearer " + token);

      int httpCode = http.GET();
      String payload = (httpCode == 200) ? http.getString() : "";
      http.end();
      // gebe die Payload per Serial aus mit dem itemName
      Serial.println("Item: " + itemName + " State: " + payload);
            

      // Formatiere die Playlod um konvertriere zu Flaot
      float retunvalue = 0.0;
      if (payload.length() > 0) {
        retunvalue = payload.toFloat();
      } 

      return retunvalue;
    }


};
