#pragma once

#include <WiFi.h>
#include <WiFiUdp.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <WebSocketsServer.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>

#include "GloveDevice.h"

class GloveApp {
public:
  bool begin();
  void loop();

private:
  void httpServeClient(WiFiClient client);
  void onWsEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length);
  void handleCommand(uint8_t num, const char* cmd, JsonVariant data);
  void sendJson(uint8_t client, JsonDocument& doc);
  void broadcast(const String& payload);

  GloveDevice device;
  WiFiServer httpServer{80};
  WebSocketsServer ws{81};
  volatile bool otaInProgress = false;
};
