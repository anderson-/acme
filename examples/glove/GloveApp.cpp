#include "GloveApp.h"

#define min(a, b) ((a) < (b) ? (a) : (b))

const char* ssid = STASSID;
const char* password = STAPSK;

bool GloveApp::begin() {
  Serial.begin(115200);
  Serial.println("Booting Glove...");

  pinMode(PIN_BUTTON, INPUT_PULLUP);
  hotspotMode = digitalRead(PIN_BUTTON) == LOW;

  device.beginRingOnly();
  device.showStatus(GloveDevice::StatusStage::Boot);

  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS mount failed");
  }

  if (hotspotMode) {
    String apSsid = String(ssid) + "glove";
    WiFi.mode(WIFI_AP);
    WiFi.softAP(apSsid.c_str(), password);
    device.showStatus(GloveDevice::StatusStage::Hotspot);
    Serial.print("Hotspot: ");
    Serial.print(apSsid);
    Serial.print(" | pass: ");
    Serial.println(password);

    // Start DNS server for captive portal
    dnsServer.start(53, "*", WiFi.softAPIP());
    Serial.print("Captive portal ready at: ");
    Serial.println(WiFi.softAPIP());
  } else {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    device.showStatus(GloveDevice::StatusStage::WifiConnecting);
    while (WiFi.status() != WL_CONNECTED) {
      device.tickRing(millis());
      delay(60);
    }
    device.showStatus(GloveDevice::StatusStage::WifiConnected);
  }

  ArduinoOTA.onStart([&]() {
    Serial.println("OTA: Starting...");
    otaInProgress = true;
    for (uint8_t i = 0; i < WEBSOCKETS_SERVER_CLIENT_MAX; i++) {
      ws.disconnect(i);
    }
    ws.close();
    httpServer.stop();
    device.showOtaProgress(0);
  });
  ArduinoOTA.onEnd([&]() {
    Serial.println("OTA: Complete!");
    device.showOtaComplete();
    unsigned long startTime = millis();
    while (millis() - startTime < 2500) {
      device.tickRing(millis());
      delay(50);
    }
    otaInProgress = false;
  });
  ArduinoOTA.onProgress([&](unsigned int progress, unsigned int total) {
    static uint8_t lastPercent = 255;
    uint8_t percent = (progress * 100) / total;
    if (percent != lastPercent) {
      Serial.printf("OTA: %u%%\n", percent);
      device.showOtaProgress(percent);
      lastPercent = percent;
    }
    device.tickRing(millis());
  });
  ArduinoOTA.onError([&](ota_error_t error) {
    Serial.printf("OTA Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
    device.showOtaError();
    unsigned long startTime = millis();
    while (millis() - startTime < 2500) {
      device.tickRing(millis());
      delay(50);
    }
    otaInProgress = false;
  });
  ArduinoOTA.setHostname("glove");
  ArduinoOTA.begin();

  ws.begin();
  ws.enableHeartbeat(5000, 3000, 2);  // ping every 5s, timeout 3s, 2 retries
  ws.onEvent([&](uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
    onWsEvent(num, type, payload, length);
  });
  httpServer.begin();

  device.setSender([&](const String& json) {
    String copy = json;
    ws.broadcastTXT(copy);
  });
  device.begin();
  device.showStatus(GloveDevice::StatusStage::Ready);

  Serial.print("WiFi ready: ");
  Serial.println(WiFi.localIP());
  Serial.println("HTTP :80 | WS :81");
  return true;
}

void GloveApp::loop() {
  ArduinoOTA.handle();
  if (otaInProgress) {
    device.tickRing(millis());  // Keep LED ring animating during OTA
    yield();
    return;
  }

  if (hotspotMode) {
    dnsServer.processNextRequest();
  } else if (WiFi.status() != WL_CONNECTED) {
    static unsigned long lastReconnect = 0;
    if (millis() - lastReconnect > 5000) {
      Serial.println("WiFi disconnected, reconnecting...");
      WiFi.reconnect();
      lastReconnect = millis();
    }
    yield();
    return;
  }

  ws.loop();
  device.tick();

  WiFiClient client = httpServer.available();
  if (client) {
    client.setTimeout(100);
    httpServeClient(client);
  }

  yield();
}

void GloveApp::httpServeClient(WiFiClient client) {
  unsigned long start = millis();
  while (!client.available() && millis() - start < 100) {
    yield();
  }
  if (!client.available()) {
    client.stop();
    return;
  }

  String request = client.readStringUntil('\r');
  request.trim();

  unsigned long headerStart = millis();
  while (client.available() && millis() - headerStart < 200) {
    String line = client.readStringUntil('\r');
    if (line == "\n" || line.length() == 0) break;
    yield();
  }
  String path = "/";
  if (request.startsWith("GET ")) {
    int pathStart = 4;
    int pathEnd = request.indexOf(' ', pathStart);
    path = request.substring(pathStart, pathEnd);
  }

  if (hotspotMode) {
    if (path == "/" || path == "/index.html") {
    } else if (path == "/generate_204" || path == "/connecttest.txt" ||
               path == "/hotspot-detect.html" || path == "/library/test/success.html" ||
               path == "/ncsi.txt") {
      client.println("HTTP/1.1 302 Found");
      client.println("Location: /");
      client.println("Connection: close");
      client.println();
      client.stop();
      return;
    } else if (path.endsWith(".css") || path.endsWith(".js") || path.endsWith(".svg") ||
               path.endsWith(".ico") || path.endsWith(".json") || path.endsWith(".html")) {
    } else {
      client.println("HTTP/1.1 302 Found");
      client.println("Location: /");
      client.println("Connection: close");
      client.println();
      client.stop();
      return;
    }
  }

  if (path == "/") path = "/index.html";

  String contentType = "text/plain";
  if (path.endsWith(".html")) contentType = "text/html";
  else if (path.endsWith(".js")) contentType = "application/javascript";
  else if (path.endsWith(".css")) contentType = "text/css";
  else if (path.endsWith(".json")) contentType = "application/json";

  File file = SPIFFS.open(path, "r");
  if (!file) {
    client.println("HTTP/1.1 404 Not Found");
    client.println("Content-Type: text/plain");
    client.println("Connection: close");
    client.println();
    client.println("404");
    client.stop();
    return;
  }

  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: " + contentType);
  client.print("Content-Length: ");
  client.println(file.size());
  client.println("Connection: close");
  client.println();

  const size_t chunk = 512;
  uint8_t buf[chunk];
  while (file.available()) {
    if (otaInProgress) {
      file.close();
      client.stop();
      return;
    }
    size_t n = file.read(buf, min(chunk, (size_t)file.available()));
    client.write(buf, n);
    yield();
  }
  file.close();
  client.flush();
  client.stop();
}

void GloveApp::handleCommand(uint8_t num, const char* cmd, JsonVariant data) {
  StaticJsonDocument<256> ack;
  ack["type"] = "response";
  ack["cmd"] = cmd;
  ack["ok"] = true;

  if (strcmp(cmd, "send_message") == 0) {
    device.onMessageFromWeb(data["text"].as<String>());
  } else if (strcmp(cmd, "set_mode") == 0) {
    device.setMode((GloveDevice::Mode)(data["mode"] | 0));
  } else if (strcmp(cmd, "set_threshold") == 0) {
    device.setThreshold(data["value"] | 1);
  } else if (strcmp(cmd, "set_timing") == 0) {
    device.setTimings(data["on"] | DEFAULT_STEP_ON_MS,
                      data["off"] | DEFAULT_STEP_OFF_MS,
                      data["gap"] | DEFAULT_LETTER_GAP_MS);
  } else if (strcmp(cmd, "set_animation") == 0) {
    String name = data["name"] | "off";
    uint32_t color = data["color"] | 0x22C1B0;
    uint16_t speed = data["speed"] | 120;
    uint8_t count = data["count"] | 0;
    device.setAnimation(name, color, speed, count);
  } else if (strcmp(cmd, "play_letter") == 0) {
    String s = data["symbol"] | "";
    if (s.length() > 0) device.playLetter(s[0]);
  } else if (strcmp(cmd, "request_alphabet") == 0) {
    device.sendAlphabet();
  } else if (strcmp(cmd, "request_status") == 0) {
    device.requestState();
    device.sendAlphabet();
  } else if (strcmp(cmd, "set_output") == 0) {
    uint16_t mask = data["mask"] | 0;
    device.writeOutputs(mask);
  } else if (strcmp(cmd, "set_debug_streaming") == 0) {
    bool enabled = data["enabled"] | false;
    device.setDebugStreaming(enabled);
  } else if (strcmp(cmd, "upload_config") == 0) {
    JsonObject obj = data.as<JsonObject>();
    bool ok = device.importGestures(obj);
    ack["ok"] = ok;
    if (!ok) ack["error"] = "invalid_gestures";
  } else if (strcmp(cmd, "reset_gestures") == 0) {
    bool ok = device.resetToDefaults();
    ack["ok"] = ok;
    if (!ok) ack["error"] = "reset_failed";
  } else if (strcmp(cmd, "ping") == 0) {
    StaticJsonDocument<128> pong;
    pong["type"] = "pong";
    pong["ts"] = (uint32_t)millis();
    sendJson(num, pong);
    return;
  } else {
    ack["ok"] = false;
    ack["error"] = "unknown_cmd";
  }
  sendJson(num, ack);
}

void GloveApp::onWsEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      Serial.printf("[%u] disconnected\n", num);
      break;
    case WStype_CONNECTED: {
      IPAddress ip = ws.remoteIP(num);
      Serial.printf("[%u] connected from %s\n", num, ip.toString().c_str());
      device.requestState();
      device.sendAlphabet();
      break;
    }
    case WStype_TEXT: {
      DynamicJsonDocument doc(6144);
      if (deserializeJson(doc, payload, length)) return;
      const char* cmd = doc["cmd"];
      if (!cmd) return;
      handleCommand(num, cmd, doc);
      break;
    }
    default:
      break;
  }
}

void GloveApp::sendJson(uint8_t client, JsonDocument& doc) {
  String json;
  serializeJson(doc, json);
  ws.sendTXT(client, json);
}

void GloveApp::broadcast(const String& payload) {
  String copy = payload;
  ws.broadcastTXT(copy);
}
