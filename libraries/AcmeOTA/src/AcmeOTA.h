#pragma once

#include <Arduino.h>
#if defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <Updater.h>
#else
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Update.h>
#endif

#ifndef ACME_FIRMWARE_VERSION
#define ACME_FIRMWARE_VERSION __DATE__ " " __TIME__
#endif

// All uploads use raw application/octet-stream bodies with Content-Length.
// Applications must unmount their filesystem before a filesystem upload.
class AcmeOTAClass {
 public:
  using RequestHandler = bool (*)(WiFiClient &, const String &, const String &);
  using BeforeUpdate = void (*)(bool filesystem);
  using AfterUpdate = void (*)(bool filesystem, bool success);

  bool begin(const char *name, uint16_t port = 80) {
    id_ = WiFi.macAddress();
    id_.replace(":", "");
    id_.toLowerCase();
    hostname_ = String(name) + "-" + id_;
    if (!MDNS.begin(hostname_.c_str())) return false;
    MDNS.addService("arduino", "tcp", port);
    MDNS.addServiceTxt("arduino", "tcp", "acme", "1");
    MDNS.addServiceTxt("arduino", "tcp", "ota_protocol", "http-v1");
    MDNS.addServiceTxt("arduino", "tcp", "id", id_.c_str());
    server_ = new WiFiServer(port);
    server_->begin();
    return true;
  }

  const String &hostname() const { return hostname_; }
  RequestHandler onRequest = nullptr;
  BeforeUpdate beforeUpdate = nullptr;
  AfterUpdate afterUpdate = nullptr;

  static void reply(WiFiClient &client, int status, const char *type, const String &body) {
    client.printf("HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %u\r\nConnection: close\r\n\r\n",
                  status, status == 200 ? "OK" : "Error", type, (unsigned)body.length());
    client.print(body);
  }

  void handle() {
#if defined(ESP8266)
    MDNS.update();
#endif
    if (!server_) return;
    WiFiClient client = server_->available();
    if (!client) return;
    client.setTimeout(1000);
    String request = client.readStringUntil('\n');
    request.trim();
    int separator = request.indexOf(' ');
    int end = request.indexOf(' ', separator + 1);
    if (separator <= 0 || end <= separator) {
      reply(client, 400, "text/plain", "invalid request\n");
      client.stop();
      return;
    }
    String method = request.substring(0, separator);
    String path = request.substring(separator + 1, end);
    size_t length = 0;
    bool complete = false;
    bool unsupported = false;
    size_t headerBytes = request.length();
    unsigned long started = millis();
    while (client.connected() && millis() - started < 5000 && headerBytes < 4096) {
      if (!client.available()) { delay(1); continue; }
      String line = client.readStringUntil('\n');
      headerBytes += line.length();
      line.trim();
      if (line.length() == 0) { complete = true; break; }
      line.toLowerCase();
      if (line.startsWith("content-length:")) {
        String value = line.substring(15);
        value.trim();
        char *tail;
        unsigned long parsed = strtoul(value.c_str(), &tail, 10);
        if (!value.length() || *tail || parsed > 0x7fffffffUL) unsupported = true;
        else length = parsed;
      }
      if (line.startsWith("transfer-encoding:")) unsupported = true;
    }
    if (!complete || unsupported) {
      reply(client, 400, "text/plain", "invalid headers; Content-Length is required for uploads\n");
    } else if (method == "GET" && path == "/info") {
#if defined(ESP8266)
      const char *platform = "arduino-esp8266";
#else
      const char *platform = "arduino-esp32";
#endif
      String info = "{\"acme\":1,\"ota_protocol\":\"http-v1\",\"id\":\"" + id_ +
          "\",\"hostname\":\"" + hostname_ + ".local\",\"platform\":\"" + platform +
          "\",\"version\":\"" + ACME_FIRMWARE_VERSION + "\"}\n";
      reply(client, 200, "application/json", info);
    } else if (method == "POST" && (path == "/update" || path == "/update-fs")) {
      upload(client, length, path == "/update-fs");
    } else if (!onRequest || !onRequest(client, method, path)) {
      reply(client, 404, "text/plain", "not found\n");
    }
    client.stop();
  }

 private:
  WiFiServer *server_ = nullptr;
  String id_;
  String hostname_;

  void upload(WiFiClient &client, size_t length, bool filesystem) {
    if (!length) {
      reply(client, 400, "text/plain", "empty upload\n");
      return;
    }
    if (beforeUpdate) beforeUpdate(filesystem);
#if defined(ESP8266)
    const int command = filesystem ? U_FS : U_FLASH;
#else
    const int command = filesystem ? U_SPIFFS : U_FLASH;
#endif
    bool success = Update.begin(length, command);
    size_t remaining = length;
    uint8_t buffer[1024];
    unsigned long lastData = millis();
    while (success && remaining && (client.connected() || client.available())) {
      int available = client.available();
      if (!available) {
        if (millis() - lastData > 10000) { success = false; break; }
        delay(1);
        continue;
      }
      size_t count = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
      if ((size_t)available < count) count = available;
      int received = client.read(buffer, count);
      if (received <= 0 || Update.write(buffer, received) != (size_t)received) {
        success = false;
        break;
      }
      remaining -= received;
      lastData = millis();
      yield();
    }
    success = success && remaining == 0;
    if (success) success = Update.end();
    else {
#if defined(ESP8266)
      Update.end();  // ESP8266 resets incomplete updates in end(false).
#else
      Update.abort();
#endif
    }
    reply(client, success ? 200 : 500, "text/plain",
          success ? "update accepted; rebooting\n" : "update failed\n");
    client.stop();
    if (afterUpdate) afterUpdate(filesystem, success);
    if (success) { delay(300); ESP.restart(); }
  }
};
