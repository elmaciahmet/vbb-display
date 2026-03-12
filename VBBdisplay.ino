/*
    VBB Display
    Board: Arduino Nano ESP32
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <LiquidCrystal.h>
#include <Preferences.h>

// PREFERENCES
Preferences pref;   // speicherinstanz

// LCD INIT (RS,  E, D4, D5, D6 , D7 )
LiquidCrystal lcd(D4, D6, D2, D3, A6, A7);

// GLOBAL SETTINGS
String ssid = "";
String pass = "";
String stationId = "";

// Serial buffer
String inputString = "";
bool stringComplete = false;

void setup() {
    Serial.begin(115200);
    lcd.begin(16, 2);

    delay(2000);

    pref.begin("vbb-config", false);    // namespace "vbb-config", false = r/w

    // read config
    ssid        = pref.getString("ssid", "");  // (key, default)
    pass        = pref.getString("pass", "");
    stationId   = pref.getString("stationId", "900120003"); // ostkreuz

    Serial.println("Trying to load WiFi preferences...");

    lcd.print("Booting...");
    Serial.println("\n--- VBB MONITOR CLI ---");
    Serial.println("Type 'help' for commands.");

    if (ssid != "" && pass != "") {
        connectWiFi();
    } else {
        Serial.println("No WiFi config found. Use 'wifi set ssid ...'");
        lcd.clear();
        lcd.print("No WiFi Config");
        lcd.setCursor(0, 1);
        lcd.print("Check Serial");
    }
}

void loop() {

    handleSerial();

    if (stringComplete) {
        parseCommand(inputString);
        inputString = "";       // reset command
        stringComplete = false;
    }

    if (WiFi.status() == WL_CONNECTED) {
        getVbbData();
        delay(5000);
    } else {
        lcd.clear();
        lcd.print("WiFi Error!");
        delay(1000);
    }
}

void getVbbData() {
    HTTPClient http;

    // api call: nur ein ergebnis, zeithorizont: 60 min
    String url = "https://v6.vbb.transport.rest/stops/" + stationId + "/departures?results=1&duration=60";

    // user agent
    http.setUserAgent("Mozilla/5.0 (ESP32)");
    http.begin(url);

    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();

        // json buffer berechnen
        // 4 kB annahme
        DynamicJsonDocument doc(4096);
        DeserializationError error = deserializeJson(doc, payload);

        if (!error) {
            // navigate json
            JsonObject dep = doc["departures"][0];

            const char* lineName = dep["line"]["name"]; // "271"
            const char* dest     = dep["direction"];    // "U Rudow"
            int delaySec         = dep["delay"];        // Verspätung in sec
            String plannedWhen   = dep["plannedWhen"];  // geplante Ankunft
            String actualWhen    = dep["when"];         // Realzeit

            lcd.clear();

            // ZEILE 1: Linie und Ziel
            lcd.setCursor(0, 0);
            lcd.print(lineName);
            lcd.print(" -> ");
            // ziel auf 10 zeichen kürzen
            String destShort = String(dest).substring(0, 10);
            lcd.print(destShort);

            // ZEILE 2: Status
            lcd.setCursor(0, 1);
            lcd.print("Delay: ");

            if (delaySec == 0) {
                lcd.print("on time");
            } else if (delaySec > 0) {
                lcd.print("+");
                lcd.print(delaySec / 60);
                lcd.print(" min");
            } else {
                lcd.print(delaySec / 60);   // delaySec bereits negativ, also kein "-" vorher printen
                lcd.print(" min");
            }
        } else {
            Serial.print("JSON Error:");
            Serial.println(error.c_str());
            lcd.setCursor(0, 1);
            lcd.print("JSON Error");
        }
    } else {
        Serial.printf("HTTP Error: %d\n", httpCode);
        lcd.setCursor(0, 1);
        lcd.print("HTTP ERR:");
        lcd.print(httpCode);
    }

    http.end();
}

String queryStation(String stationInput) {
    HTTPClient query;
    WiFiClientSecure client; // Wichtig für HTTPS
    client.setInsecure();    // Zertifikate ignorieren (einfacher für Breadboard-Projekte)

    // 1. URL ENCODING (Leerzeichen fixen)
    // Wir arbeiten auf einer Kopie, damit der Original-String nicht verändert wird
    String encodedStation = stationInput;
    encodedStation.replace(" ", "%20");
    encodedStation.replace("ä", "ae"); // Basic Umlaut Handling (API ist tolerant)
    encodedStation.replace("ö", "oe");
    encodedStation.replace("ü", "ue");
    encodedStation.replace("ß", "ss");

    // Debugging: Sehen, was wir wirklich senden
    Serial.print("Querying API for: ");
    Serial.println(encodedStation);

    String url = "https://v6.vbb.transport.rest/locations?poi=false&addresses=false&query=" + encodedStation + "&results=1";

    query.setUserAgent("Mozilla/5.0 (ESP32)");
    // HTTPS Secure Client nutzen (besser als direkt query.begin(url))
    query.begin(client, url); 

    int httpCode = query.GET();

    if (httpCode == HTTP_CODE_OK) {
        String payload = query.getString();
        
        // JSON Parsen
        DynamicJsonDocument doc(4096);
        DeserializationError error = deserializeJson(doc, payload);

        if (!error) {
            // SICHERHEITSCHECK: Ist das Array leer?
            if (doc.size() == 0) {
                Serial.println("API returned 0 results. Check spelling.");
                lcd.clear();
                lcd.print("Station not found");
                delay(2000);
                query.end();
                return stationId; // Alten Wert behalten
            }

            // KORREKTUR: Zugriff auf Array Index 0 (nicht String "0")
            JsonObject stat = doc[0];
            
            // Prüfen ob ID vorhanden ist
            if (stat.containsKey("id")) {
                String newId = stat["id"].as<String>();
                String foundName = stat["name"].as<String>();
                
                Serial.println("Found: " + foundName);
                query.end();
                return newId;
            } else {
                 Serial.println("JSON object has no ID.");
            }
        } else {
            Serial.print("JSON Error: ");
            Serial.println(error.c_str());
            lcd.setCursor(0, 1);
            lcd.print("JSON Parse Err");
            delay(2000);
        }
    } else {
        Serial.printf("HTTP Error: %d\n", httpCode);
        Serial.println("URL was: " + url); // Hilft beim Debuggen
        lcd.setCursor(0, 1);
        lcd.print("HTTP Err: ");
        lcd.print(httpCode);
        delay(2000);
    }

    query.end();
    return stationId; // Fallback: Alte ID zurückgeben
}

// helper functions

void connectWiFi() {
    lcd.clear();
    lcd.print("Connecting to:");
    lcd.setCursor(0, 1);
    lcd.print(ssid);

    Serial.print("Connecting to: ");
    Serial.println(ssid);

    WiFi.begin(ssid.c_str(), pass.c_str());

    int tryCount = 0;
    while (WiFi.status() != WL_CONNECTED && tryCount < 20) { // timeout nach 10 sek
        delay(500);
        Serial.print(".");
        tryCount++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nConnected.");
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());
        lcd.clear();
        lcd.print("Online.");
    } else {
        Serial.println("\nConnect Failed. Check credentials.");
        lcd.clear();
        lcd.print("WiFi Fail");
    }
}

void handleSerial() {
    while (Serial.available()) {
        char inChar = (char) Serial.read();
        if (inChar == '\n') {
            stringComplete = true;
        } else if (inChar != '\r') {    // cr ignorieren
            inputString += inChar;
        }
    }
}

void parseCommand(String cmd) {
    cmd.trim(); // Leerzeichen am Anfang/Ende weg

    if (cmd.equalsIgnoreCase("help")) {
        Serial.println("Available Commands:");
        Serial.println("  wifi set ssid \"NAME\"");
        Serial.println("  wifi set pass \"PASSWORD\"");
        Serial.println("  wifi show");
        Serial.println("  station qns \"STATION\"");
        Serial.println("  station set \"STATIONID\"");
        Serial.println("  station show");
        Serial.println("  reboot");
        return;
    }

    if (cmd.startsWith("wifi set ssid ")) {
        // String zerlegen: Alles nach "wifi set ssid " extrahieren
        // Wir entfernen Anführungszeichen, falls der User welche nutzt
        String newSSID = cmd.substring(14); 
        newSSID.replace("\"", ""); // Quotes entfernen

        pref.putString("ssid", newSSID); // Speichern im Flash!
        ssid = newSSID; // Variable updaten
        Serial.println("SSID saved: " + newSSID);
        Serial.println("Reboot for changes to take effect. Either type 'reboot' or press the reboot button on the Arduino.");
    }

    else if (cmd.startsWith("wifi set pass ")) {
        String newPass = cmd.substring(14);
        newPass.replace("\"", "");

        pref.putString("pass", newPass); // Speichern
        pass = newPass;
        Serial.println("Password saved.");
        Serial.println("Reboot for changes to take effect. Either type 'reboot' or press the reboot button on the Arduino.");
    }

    else if (cmd.equalsIgnoreCase("wifi show")) {
        Serial.println("Current Config:");
        Serial.println("SSID: " + ssid);
        Serial.println("Pass: ******"); // Sicherheitshalber nicht anzeigen
    }

    else if (cmd.startsWith("station qns ")) {
        String searchName = cmd.substring(12);
        searchName.replace("\"", ""); // Anführungszeichen entfernen
        searchName.trim(); // Leerzeichen am Anfang/Ende weg

        lcd.clear();
        lcd.print("Searching...");

        // Alte ID merken, um zu prüfen, ob sich was geändert hat
        String oldId = stationId;
        String newId = queryStation(searchName);

        if (newId != oldId) {
            // Erfolg!
            pref.putString("stationId", newId);
            stationId = newId;
            
            Serial.printf("New Station ID saved: %s\n", newId.c_str());
            lcd.clear();
            lcd.print("Saved ID:");
            lcd.setCursor(0, 1);
            lcd.print(newId);
            delay(2000);
            
            // Update erzwingen, damit man sofort Ergebnisse sieht
            getVbbData(); 
        } else {
            // Fehlgeschlagen (queryStation hat alte ID returned)
            Serial.println("Station Update aborted.");
            // LCD Fehlermeldung wurde schon in queryStation angezeigt
        }
    }

    else if (cmd.startsWith("station set ")) {
        String newStation = cmd.substring(12);
        newStation.replace("\"", "");

        pref.putString("stationId", newStation); // Speichern
        stationId = newStation;
        Serial.println("Station saved.");
    }

    else if (cmd.equalsIgnoreCase("station show")) {
        Serial.println("Current Station:");
        Serial.println("StationId: " + stationId);
    }

    else if (cmd.equalsIgnoreCase("reboot")) {
        Serial.println("Rebooting...");
        delay(500);
        ESP.restart(); // Software Reset
    }
  
    else {
        Serial.println("Unknown command: " + cmd);
    }
}
