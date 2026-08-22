  #include <WiFi.h>
  #include <WiFiMulti.h>
  #include <WiFiUdp.h>
  #include <ArduinoJson.h>
  #include <U8g2lib.h>

  WiFiMulti wifiMulti;
  #define LONG_PRESS_MS 900
  float currentPositionMM = 0; // mm
  //unsigned long b1_down_millis = 0, b2_down_millis = 0, b3_down_millis = 0;unsigned long b4_down_millis = 0, b5_down_millis = 0, b6_down_millis = 0;unsigned long b7_down_millis = 0, b8_down_millis = 0;
  //bool b1_was_pressed = false, b2_was_pressed = false, b3_was_pressed = false;bool b4_was_pressed = false, b5_was_pressed = false, b6_was_pressed = false;bool b7_was_pressed = false, b8_was_pressed = false;
  bool showPresetSaved = false;
  uint8_t savedPresetNum = 0;
  unsigned long presetSavedMillis = 0;
  unsigned long lastMoveSent = 0;
  const int MOVE_INTERVAL = 40;  // ms
  int strength = 0;
  // --- OLED SSD1309 (128x64, I2C, 0x3C)
  U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

  const char* udpAddress = "192.168.88.240"; // IP Raspberry Pi
  const int udpPort = 40000;
  uint8_t selectedPresetNum = 0;

  float lastBeepedPositionMM = -9999;

  WiFiUDP udp;
  WiFiUDP udpListen;
  const int udpListenPort = 40001;  // stejný jako v Pythonu

  const int JOY_X_PIN = 34; // ADC1_CH6
  const int JOY_Y_PIN = 35; // ADC1_CH7
  const int SW_PIN = 18;
  const int BTN1_PIN = 33;
  const int BTN2_PIN = 19;
  const int BTN3_PIN = 23;
  const int BTN4_PIN = 5;
  const int BTN5_PIN = 17;
  const int BTN6_PIN = 32;
  const int BTN7_PIN = 12;
  const int BTN8_PIN = 26;
  const int BUZZER_PIN = 4;

  bool b1_long_saved = false, b2_long_saved = false, b3_long_saved = false, b4_long_saved = false;
  bool b5_long_saved = false, b6_long_saved = false, b7_long_saved = false, b8_long_saved = false;
  unsigned long b1_down_millis = 0, b2_down_millis = 0, b3_down_millis = 0, b4_down_millis = 0;
  unsigned long b5_down_millis = 0, b6_down_millis = 0, b7_down_millis = 0, b8_down_millis = 0;
  bool b1_was_pressed = false, b2_was_pressed = false, b3_was_pressed = false, b4_was_pressed = false;
  bool b5_was_pressed = false, b6_was_pressed = false, b7_was_pressed = false, b8_was_pressed = false;


  int currentPreset = 0;
  int currentPosition = 0;
  const char* eyepieceNames[] = {
    "Reset",   // b1
    "bino24",  // b2
    "bino14",  // b3
    "bino20",  // b4
    "APM20",   // b5
    "ES14",    // b6
    "ES24",   // b7
    "APM5"     // b8
  };
  void beep(int ms = 200) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(ms);
    digitalWrite(BUZZER_PIN, LOW);
  }
  void setup() {
    Serial.begin(115200);
    pinMode(SW_PIN, INPUT_PULLUP);
    pinMode(BTN1_PIN, INPUT_PULLUP);
    pinMode(BTN2_PIN, INPUT_PULLUP);
    pinMode(BTN3_PIN, INPUT_PULLUP);
    pinMode(BTN4_PIN, INPUT_PULLUP);
    pinMode(BTN5_PIN, INPUT_PULLUP);
    pinMode(BTN6_PIN, INPUT_PULLUP);
    pinMode(BTN7_PIN, INPUT_PULLUP);
    pinMode(BTN8_PIN, INPUT_PULLUP);
    pinMode(BUZZER_PIN, OUTPUT);
    u8g2.begin();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.clearBuffer();
    u8g2.drawStr(0, 12, "Focuser Ovl. v1.0");
    u8g2.sendBuffer();

    wifiMulti.addAP("wlong", "kukurami");
    wifiMulti.addAP("hvezdarna", "okolikol");
    wifiMulti.addAP("stonehenge2.4", "okolikol");
    // můžeš přidat více WiFi sítí

    u8g2.clearBuffer();
    u8g2.drawStr(0, 12, "WiFi: Connecting...");
    u8g2.sendBuffer();

    while (wifiMulti.run() != WL_CONNECTED) {
      delay(500);
    }

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.setCursor(16, 10);
    u8g2.print(WiFi.SSID());
    u8g2.setCursor(0, 26);
    u8g2.print(WiFi.localIP());
    u8g2.sendBuffer();
    delay(1000);


    udpListen.begin(udpListenPort);


  }
  // Vykreslí ikonu WiFi signálu do zadané pozice
  void drawSmallWifiSignalIcon(U8G2 &u8g2, int x, int y, int level) {
  const int numBars = 4;
  const int w = 3;    // šířka sloupce
  const int s = 1;    // mezera mezi sloupci
  const int heights[numBars] = {4, 7, 10, 13}; // výšky sloupců

  for (int i = 0; i < numBars; i++) {
    int x0 = x + i * (w + s);
    int y0 = y - heights[i];
    if (i < level) {
      u8g2.drawBox(x0, y0, w, heights[i]);
    } else {
      u8g2.drawFrame(x0, y0, w, heights[i]);
    }
  }
}
//  void drawWifiSignalIcon(U8G2 &u8g2, int x, int y, int level) {
//    int w = 6;  // šířka sloupce
//    int s = 2;  // mezera mezi sloupci
//    for (int i = 0; i < 4; i++) {
//      int height = 6 + i * 5;   // výška sloupce
//      int x0 = x + i * (w + s);
//      int y0 = y - height;
//      if (i < level) {
//        u8g2.drawBox(x0, y0, w, height);
//      } else {
//        u8g2.drawFrame(x0, y0, w, height);
//      }
//    }
//  }
     void loop() {

     // --- DEBUG: Vypiš stav všech tlačítek ---
     static unsigned long last_debug = 0;
     if (millis() - last_debug > 1000) {  // každou sekundu
       last_debug = millis();
       Serial.printf("[DEBUG] Buttons: b1=%d, b2=%d, b3=%d, b4=%d, b5=%d, b6=%d, b7=%d, b8=%d\n", 
         !digitalRead(BTN1_PIN), !digitalRead(BTN2_PIN), !digitalRead(BTN3_PIN), !digitalRead(BTN4_PIN),
         !digitalRead(BTN5_PIN), !digitalRead(BTN6_PIN), !digitalRead(BTN7_PIN), !digitalRead(BTN8_PIN));
     }

    // --- PŘÍJEM POZICE PŘES UDP BROADCAST ---
    int packetSize = udpListen.parsePacket();
    if (packetSize) {
      char packetBuffer[128];
      int len = udpListen.read(packetBuffer, sizeof(packetBuffer) - 1);
      if (len > 0) packetBuffer[len] = 0;
      Serial.printf("[UDP RECEIVE] Received packet: %s\n", packetBuffer);
      StaticJsonDocument<128> doc;
      DeserializationError error = deserializeJson(doc, packetBuffer);
      if (!error) {
        if (doc.containsKey("pos")) {
          currentPosition = doc["pos"];
          Serial.printf("[UDP] Position sync: %ld\n", currentPosition);
        }
        if (doc.containsKey("pos_mm")) {
          currentPositionMM = doc["pos_mm"];
          Serial.printf("[UDP] Position MM sync: %.2f\n", currentPositionMM);
        }
      } else {
        Serial.printf("[UDP] JSON parse error: %s\n", error.c_str());
      }
    }

    // --- BEEPS při dosažení důležitých pozic ---
    float EPSILON = 0.02; // tolerance v mm (pro float porovnání)
    const float MAX_POS_MM = 42.0;

    if (abs(currentPositionMM - 0.0) < EPSILON && abs(lastBeepedPositionMM - 0.0) >= EPSILON) {
      beep(120); // Nula
      lastBeepedPositionMM = currentPositionMM;
    }
    else if (abs(currentPositionMM - MAX_POS_MM) < EPSILON && abs(lastBeepedPositionMM - MAX_POS_MM) >= EPSILON) {
      beep(120); // Max 42 mm
      lastBeepedPositionMM = currentPositionMM;
    }

         int joyx_raw = analogRead(JOY_X_PIN);
     int sw   = !digitalRead(SW_PIN);
     int b1   = !digitalRead(BTN1_PIN);
     int b2   = !digitalRead(BTN2_PIN);
     int b3   = !digitalRead(BTN3_PIN);
     int b4   = !digitalRead(BTN4_PIN);
     int b5   = !digitalRead(BTN5_PIN);
     int b6   = !digitalRead(BTN6_PIN);
     int b7   = !digitalRead(BTN7_PIN);
     int b8   = !digitalRead(BTN8_PIN);
     
     // --- DEBUG: Vypiš při změně tlačítek ---
     static int last_b1 = 0, last_b2 = 0;
     if (b1 != last_b1) {
       Serial.printf("[DEBUG] B1 changed: %d -> %d\n", last_b1, b1);
       last_b1 = b1;
     }
     if (b2 != last_b2) {
       Serial.printf("[DEBUG] B2 changed: %d -> %d\n", last_b2, b2);
       last_b2 = b2;
     }

    unsigned long now = millis();

         // Je joystick mimo deadzone?
           // SIMULACE: Zakomentujte joystick a použijte konstantní hodnotu pro test
      // strength = -3;  // Simulace pohybu doleva (IN)
      // strength = 3;   // Simulace pohybu doprava (OUT)
     
     if (strength != 0) {
      // Vyber krok podle síly výchylky
      int step = 1; // Default jemný krok
      if (abs(strength) == 1) step = 2;
      else if (abs(strength) == 2) step = 5;
      else if (abs(strength) == 3) step = 10;
      else if (abs(strength) == 4) step = 20;
      else if (abs(strength) == 5) step = 50;
      else if (abs(strength) == 6) step = 100;

      // Posílej každých 80 ms
      if (now - lastMoveSent >= MOVE_INTERVAL) {
        lastMoveSent = now;

        StaticJsonDocument<128> doc;
        doc["joyx"] = joyx_raw;
        doc["sw"] = sw;  // SW is already in main packet, but keep for consistency
        if (strength < 0) {
          doc["move_in"] = step;
        } else {
          doc["move_out"] = step;
        }
                 char buffer[128];
         size_t n = serializeJson(doc, buffer);
         Serial.printf("[UDP SEND] strength=%d, step=%d, JSON: %s\n", strength, step, buffer);
         udp.beginPacket(udpAddress, udpPort);
         udp.write((uint8_t*)buffer, n);
         udp.endPacket();
      }
    } else {
      // Pokud joystick v klidu, resetuj časovač, aby nedocházelo k zásekům při dalším pohybu
      lastMoveSent = now;
      
      // Posílej SW stav i když je joystick v klidu (každých 200ms)
      static unsigned long lastSwSent = 0;
      if (now - lastSwSent >= 200) {
        lastSwSent = now;
        
        StaticJsonDocument<128> doc;
        doc["joyx"] = joyx_raw;
        doc["sw"] = sw;
        
        char buffer[128];
        size_t n = serializeJson(doc, buffer);
        udp.beginPacket(udpAddress, udpPort);
        udp.write((uint8_t*)buffer, n);
        udp.endPacket();
      }
    }

         // --- Detekce dlouhého/krátkého stisku tlačítek ---
     // ----- B1 -----
     if (b1 && !b1_was_pressed) {
       // Tlačítko bylo stisknuto - začni měřit čas
       b1_down_millis = millis();
       b1_long_saved = false;
       Serial.printf("[DEBUG] B1 pressed down at %lu\n", b1_down_millis);
     }
     if (b1 && !b1_long_saved && (millis() - b1_down_millis > LONG_PRESS_MS)) {
       // Dlouhý stisk detekován - beep a označ jako dlouhý
       beep(200);
       b1_long_saved = true;
       Serial.printf("[DEBUG] B1 long press detected at %lu (dt=%lu)\n", millis(), millis() - b1_down_millis);
     }
     if (!b1 && b1_was_pressed) {
       // Tlačítko bylo uvolněno - rozhodni podle délky stisku
       unsigned long dt = millis() - b1_down_millis;
       Serial.printf("[DEBUG] B1 released at %lu (dt=%lu, long_saved=%d)\n", millis(), dt, b1_long_saved);
       if (b1_long_saved) {
         // Byl to dlouhý stisk - ulož preset
         showPresetSaved = true; savedPresetNum = 1; presetSavedMillis = millis();
         StaticJsonDocument<128> doc; doc["joyx"] = joyx_raw; doc["sw"] = sw; doc["b1_long"] = 1;
         char buffer[128]; size_t n = serializeJson(doc, buffer);
         Serial.printf("[UDP SEND] B1 long press, JSON: %s\n", buffer);
         udp.beginPacket(udpAddress, udpPort); udp.write((uint8_t*)buffer, n); udp.endPacket();
       } else if (dt > 30 && dt < LONG_PRESS_MS) {
         // Byl to krátký stisk - spusť preset
         selectedPresetNum = 1;
         StaticJsonDocument<128> doc; doc["joyx"] = joyx_raw; doc["sw"] = sw; doc["b1"] = 1;
         char buffer[128]; size_t n = serializeJson(doc, buffer);
         Serial.printf("[UDP SEND] B1 short press, JSON: %s\n", buffer);
         udp.beginPacket(udpAddress, udpPort); udp.write((uint8_t*)buffer, n); udp.endPacket();
       }
     }
     b1_was_pressed = b1;

         // ----- B2 -----
     if (b2 && !b2_was_pressed) {
       b2_down_millis = millis();
       b2_long_saved = false;
       Serial.printf("[DEBUG] B2 pressed down at %lu\n", b2_down_millis);
     }
     if (b2 && !b2_long_saved && (millis() - b2_down_millis > LONG_PRESS_MS)) {
       beep(200);
       b2_long_saved = true;
       Serial.printf("[DEBUG] B2 long press detected at %lu (dt=%lu)\n", millis(), millis() - b2_down_millis);
     }
     if (!b2 && b2_was_pressed) {
       unsigned long dt = millis() - b2_down_millis;
       Serial.printf("[DEBUG] B2 released at %lu (dt=%lu, long_saved=%d)\n", millis(), dt, b2_long_saved);
       if (b2_long_saved) {
         showPresetSaved = true; savedPresetNum = 2; presetSavedMillis = millis();
         StaticJsonDocument<128> doc; doc["joyx"] = joyx_raw; doc["sw"] = sw; doc["b2_long"] = 1;
         char buffer[128]; size_t n = serializeJson(doc, buffer);
         Serial.printf("[UDP SEND] B2 long press, JSON: %s\n", buffer);
         udp.beginPacket(udpAddress, udpPort); 
         int result = udp.write((uint8_t*)buffer, n); 
         udp.endPacket();
         Serial.printf("[UDP DEBUG] B2 long press sent: %d bytes\n", result);
       } else if (dt > 30 && dt < LONG_PRESS_MS) {
         selectedPresetNum = 2;
         StaticJsonDocument<128> doc; doc["joyx"] = joyx_raw; doc["sw"] = sw; doc["b2"] = 1;
         char buffer[128]; size_t n = serializeJson(doc, buffer);
         Serial.printf("[UDP SEND] B2 short press, JSON: %s\n", buffer);
         udp.beginPacket(udpAddress, udpPort); 
         int result = udp.write((uint8_t*)buffer, n); 
         udp.endPacket();
         Serial.printf("[UDP DEBUG] B2 short press sent: %d bytes\n", result);
       }
     }
     b2_was_pressed = b2;

         // ----- B3 -----
     if (b3 && !b3_was_pressed) {
       b3_down_millis = millis();
       b3_long_saved = false;
     }
     if (b3 && !b3_long_saved && (millis() - b3_down_millis > LONG_PRESS_MS)) {
       beep(200);
       b3_long_saved = true;
     }
     if (!b3 && b3_was_pressed) {
       unsigned long dt = millis() - b3_down_millis;
       if (b3_long_saved) {
         showPresetSaved = true; savedPresetNum = 3; presetSavedMillis = millis();
         StaticJsonDocument<128> doc; doc["joyx"] = joyx_raw; doc["sw"] = sw; doc["b3_long"] = 1;
         char buffer[128]; size_t n = serializeJson(doc, buffer);
         udp.beginPacket(udpAddress, udpPort); udp.write((uint8_t*)buffer, n); udp.endPacket();
       } else if (dt > 30 && dt < LONG_PRESS_MS) {
         selectedPresetNum = 3;
         StaticJsonDocument<128> doc; doc["joyx"] = joyx_raw; doc["sw"] = sw; doc["b3"] = 1;
         char buffer[128]; size_t n = serializeJson(doc, buffer);
         udp.beginPacket(udpAddress, udpPort); udp.write((uint8_t*)buffer, n); udp.endPacket();
       }
     }
     b3_was_pressed = b3;

     // ----- B4 -----
     if (b4 && !b4_was_pressed) {
       b4_down_millis = millis();
       b4_long_saved = false;
     }
     if (b4 && !b4_long_saved && (millis() - b4_down_millis > LONG_PRESS_MS)) {
       beep(200);
       b4_long_saved = true;
     }
     if (!b4 && b4_was_pressed) {
       unsigned long dt = millis() - b4_down_millis;
       if (b4_long_saved) {
         showPresetSaved = true; savedPresetNum = 4; presetSavedMillis = millis();
         StaticJsonDocument<128> doc; doc["joyx"] = joyx_raw; doc["sw"] = sw; doc["b4_long"] = 1;
         char buffer[128]; size_t n = serializeJson(doc, buffer);
         udp.beginPacket(udpAddress, udpPort); udp.write((uint8_t*)buffer, n); udp.endPacket();
       } else if (dt > 30 && dt < LONG_PRESS_MS) {
         selectedPresetNum = 4;
         StaticJsonDocument<128> doc; doc["joyx"] = joyx_raw; doc["sw"] = sw; doc["b4"] = 1;
         char buffer[128]; size_t n = serializeJson(doc, buffer);
         udp.beginPacket(udpAddress, udpPort); udp.write((uint8_t*)buffer, n); udp.endPacket();
       }
     }
     b4_was_pressed = b4;

     // ----- B5 -----
     if (b5 && !b5_was_pressed) {
       b5_down_millis = millis();
       b5_long_saved = false;
     }
     if (b5 && !b5_long_saved && (millis() - b5_down_millis > LONG_PRESS_MS)) {
       beep(200);
       b5_long_saved = true;
     }
     if (!b5 && b5_was_pressed) {
       unsigned long dt = millis() - b5_down_millis;
       if (b5_long_saved) {
         showPresetSaved = true; savedPresetNum = 5; presetSavedMillis = millis();
         StaticJsonDocument<128> doc; doc["joyx"] = joyx_raw; doc["sw"] = sw; doc["b5_long"] = 1;
         char buffer[128]; size_t n = serializeJson(doc, buffer);
         udp.beginPacket(udpAddress, udpPort); udp.write((uint8_t*)buffer, n); udp.endPacket();
       } else if (dt > 30 && dt < LONG_PRESS_MS) {
         selectedPresetNum = 5;
         StaticJsonDocument<128> doc; doc["joyx"] = joyx_raw; doc["sw"] = sw; doc["b5"] = 1;
         char buffer[128]; size_t n = serializeJson(doc, buffer);
         udp.beginPacket(udpAddress, udpPort); udp.write((uint8_t*)buffer, n); udp.endPacket();
       }
     }
     b5_was_pressed = b5;

     // ----- B6 -----
     if (b6 && !b6_was_pressed) {
       b6_down_millis = millis();
       b6_long_saved = false;
     }
     if (b6 && !b6_long_saved && (millis() - b6_down_millis > LONG_PRESS_MS)) {
       beep(200);
       b6_long_saved = true;
     }
     if (!b6 && b6_was_pressed) {
       unsigned long dt = millis() - b6_down_millis;
       if (b6_long_saved) {
         showPresetSaved = true; savedPresetNum = 6; presetSavedMillis = millis();
         StaticJsonDocument<128> doc; doc["joyx"] = joyx_raw; doc["sw"] = sw; doc["b6_long"] = 1;
         char buffer[128]; size_t n = serializeJson(doc, buffer);
         udp.beginPacket(udpAddress, udpPort); udp.write((uint8_t*)buffer, n); udp.endPacket();
       } else if (dt > 30 && dt < LONG_PRESS_MS) {
         selectedPresetNum = 6;
         StaticJsonDocument<128> doc; doc["joyx"] = joyx_raw; doc["sw"] = sw; doc["b6"] = 1;
         char buffer[128]; size_t n = serializeJson(doc, buffer);
         udp.beginPacket(udpAddress, udpPort); udp.write((uint8_t*)buffer, n); udp.endPacket();
       }
     }
     b6_was_pressed = b6;

     // ----- B7 -----
     if (b7 && !b7_was_pressed) {
       b7_down_millis = millis();
       b7_long_saved = false;
     }
     if (b7 && !b7_long_saved && (millis() - b7_down_millis > LONG_PRESS_MS)) {
       beep(200);
       b7_long_saved = true;
     }
     if (!b7 && b7_was_pressed) {
       unsigned long dt = millis() - b7_down_millis;
       if (b7_long_saved) {
         showPresetSaved = true; savedPresetNum = 7; presetSavedMillis = millis();
         StaticJsonDocument<128> doc; doc["joyx"] = joyx_raw; doc["sw"] = sw; doc["b7_long"] = 1;
         char buffer[128]; size_t n = serializeJson(doc, buffer);
         udp.beginPacket(udpAddress, udpPort); udp.write((uint8_t*)buffer, n); udp.endPacket();
       } else if (dt > 30 && dt < LONG_PRESS_MS) {
         selectedPresetNum = 7;
         StaticJsonDocument<128> doc; doc["joyx"] = joyx_raw; doc["sw"] = sw; doc["b7"] = 1;
         char buffer[128]; size_t n = serializeJson(doc, buffer);
         udp.beginPacket(udpAddress, udpPort); udp.write((uint8_t*)buffer, n); udp.endPacket();
       }
     }
     b7_was_pressed = b7;

     // ----- B8 -----
     if (b8 && !b8_was_pressed) {
       b8_down_millis = millis();
       b8_long_saved = false;
     }
     if (b8 && !b8_long_saved && (millis() - b8_down_millis > LONG_PRESS_MS)) {
       beep(200);
       b8_long_saved = true;
     }
     if (!b8 && b8_was_pressed) {
       unsigned long dt = millis() - b8_down_millis;
       if (b8_long_saved) {
         showPresetSaved = true; savedPresetNum = 8; presetSavedMillis = millis();
         StaticJsonDocument<128> doc; doc["joyx"] = joyx_raw; doc["sw"] = sw; doc["b8_long"] = 1;
         char buffer[128]; size_t n = serializeJson(doc, buffer);
         udp.beginPacket(udpAddress, udpPort); udp.write((uint8_t*)buffer, n); udp.endPacket();
       } else if (dt > 30 && dt < LONG_PRESS_MS) {
         selectedPresetNum = 8;
         StaticJsonDocument<128> doc; doc["joyx"] = joyx_raw; doc["sw"] = sw; doc["b8"] = 1;
         char buffer[128]; size_t n = serializeJson(doc, buffer);
         udp.beginPacket(udpAddress, udpPort); udp.write((uint8_t*)buffer, n); udp.endPacket();
       }
     }
     b8_was_pressed = b8;


    // --- UDP JSON packet (pouze joystick pro plynulý pohyb)
    StaticJsonDocument<128> doc;
    doc["joyx"] = joyx_raw;
    doc["sw"] = sw;  // Add SW button state to main packet
    // Tlačítka se posílají pouze při stisku, ne každý loop
    
    char buffer[128];
    size_t n = serializeJson(doc, buffer);
    
    udp.beginPacket(udpAddress, udpPort);
    udp.write((uint8_t*)buffer, n);
    udp.endPacket();

    // --- OLED výpis ---
    u8g2.clearBuffer();
    int wifi_level = 0;
    if (WiFi.status() == WL_CONNECTED) {
      int rssi = WiFi.RSSI();
      if      (rssi > -55) wifi_level = 3;
      else if (rssi > -70) wifi_level = 2;
      else if (rssi > -85) wifi_level = 1;
      else                 wifi_level = 0;
    }
    if (showPresetSaved) {
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_ncenB08_tr);
      u8g2.setCursor(16, 36);
      u8g2.printf("PRESET %d SAVED!", savedPresetNum);
      u8g2.sendBuffer();
      if (millis() - presetSavedMillis > 1200) {
        showPresetSaved = false;
      }
    } else {
      u8g2.setFont(u8g2_font_6x10_tr);
      u8g2.setCursor(0, 12);
      u8g2.print(WiFi.status() == WL_CONNECTED ? WiFi.SSID() : "NO WiFi");
      u8g2.setCursor(0, 26);
      if (WiFi.status() == WL_CONNECTED)
        u8g2.print(WiFi.localIP());
      else
        u8g2.print("Disconnected");

      // --- Trojúhelníkové šipky podle joysticku (osa X) ---
      int center = 1845;
      int deadzone = 180;
      int maxval = 2247;  // případně upravit podle svého joysticku
      int dx = joyx_raw - center;
      int strenght1 = 0;
      if (dx < -deadzone)
        strenght1 = map(dx, -maxval, -deadzone, -6, -1);
      else if (dx > deadzone)
        strenght1 = map(dx, deadzone, maxval, 1, 6);
      
             // Nastav strength pro pohyb
       if (dx < -deadzone)
         strength = map(dx, -maxval, -deadzone, -6, -1);
       else if (dx > deadzone)
         strength = map(dx, deadzone, maxval, 1, 6);
       else
         strength = 0;


      int xpos = 64, ypos = 40;
      int delta = 8;

      if (strenght1 < 0) {
        for (int i = 1; i <= -strenght1; ++i)
          u8g2.drawTriangle(xpos - delta * i, ypos, xpos - delta * i + 8, ypos - 6, xpos - delta * i + 8, ypos + 6);
      } else if (strenght1 > 0) {
        for (int i = 1; i <= strenght1; ++i)
          u8g2.drawTriangle(xpos + delta * i, ypos, xpos + delta * i - 8, ypos - 6, xpos + delta * i - 8, ypos + 6);
      }
      Serial.println(joyx_raw);
      // (střed: nezobrazuje se nic, nebo můžeš volitelně dát tečku)
      u8g2.drawCircle(xpos, ypos, 4);

      u8g2.setFont(u8g2_font_6x10_tr);
      u8g2.setCursor(0, 60);
      //u8g2.printf("Pos:%ld", currentPosition);
      Serial.printf("[DISPLAY] currentPositionMM: %.2f\n", currentPositionMM);
      u8g2.printf("Pos:%.2fmm", currentPositionMM);
      u8g2.setFont(u8g2_font_ncenB12_tr); // větší font
      char pbuf[16];
      //const char* eyepieceName = eyepieceNames[selectedPresetNum - 1]; // Pokud číslujete od 1!
      const char* eyepieceName;
      if (selectedPresetNum > 0 && selectedPresetNum <= 8) {
        eyepieceName = eyepieceNames[selectedPresetNum - 1];
      } else {
        eyepieceName = "-";
      }

      u8g2.drawStr(128 - u8g2.getStrWidth(eyepieceName), 64, eyepieceName);
      // Pravý dolní roh: X=128-šířka textu, Y=64 (nebo - pár pixelů podle fontu)
      uint8_t text_width = u8g2.getStrWidth(pbuf);
      u8g2.setCursor(128 - text_width - 2, 64); // malá mezera od pravého okraje
      u8g2.print(pbuf);
      // Zjisti sílu signálu
      int wifi_strength = 0;
      long rssi = WiFi.RSSI();
      if (rssi > -60)      wifi_strength = 4;
      else if (rssi > -70) wifi_strength = 3;
      else if (rssi > -80) wifi_strength = 2;
      else if (rssi > -90) wifi_strength = 1;
      else                 wifi_strength = 0;

      // Kresli WiFi signál do pravého horního rohu
      // Pro SSD1309 128x64 je roh např. x=100, y=20
//      drawWifiSignalIcon(u8g2, 104, 20, wifi_strength); // uprav X/Y podle rozložení
    drawSmallWifiSignalIcon(u8g2, 116, 16, wifi_strength); // např. x=116, y=16 (spodní okraj)

      u8g2.sendBuffer();
      delay(150);
    }
  }
