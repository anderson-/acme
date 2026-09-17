#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <WebSocketsServer.h>
#include <SPIFFS.h>

const char* ssid = STASSID;
const char* password = STAPSK;

const int LED_PIN = 5;
bool ledState = false;

WiFiServer httpServer(80);
WebSocketsServer webSocket = WebSocketsServer(81);

void handleHTTPClient(WiFiClient client) {
  // Read HTTP request line
  String request = client.readStringUntil('\r');
  request.trim();

  Serial.println("HTTP Request: " + request);

  // Skip the rest of the HTTP headers
  while (client.available()) {
    String line = client.readStringUntil('\r');
    if (line == "\n" || line.length() == 0) {
      break; // End of headers
    }
  }

  if (request.startsWith("GET / ")) {
    // Serve index.html
    File file = SPIFFS.open("/index.html", "r");
    if (file) {
      client.println("HTTP/1.1 200 OK");
      client.println("Content-Type: text/html");
      client.println("Connection: close");
      client.println();

      while (file.available()) {
        client.write(file.read());
      }
      file.close();
    } else {
      client.println("HTTP/1.1 404 Not Found");
      client.println("Content-Type: text/plain");
      client.println("Connection: close");
      client.println();
      client.println("File not found");
    }
  } else {
    client.println("HTTP/1.1 404 Not Found");
    client.println("Content-Type: text/plain");
    client.println("Connection: close");
    client.println();
    client.println("Not Found");
  }

  client.flush();
  client.stop();
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.printf("[%u] Disconnected!\n", num);
      break;
    case WStype_CONNECTED: {
      IPAddress ip = webSocket.remoteIP(num);
      Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);

      // Send current LED state to new client
      String response = ledState ? "LED_ON" : "LED_OFF";
      webSocket.sendTXT(num, response);
      break;
    }
    case WStype_TEXT:
      Serial.printf("[%u] get Text: %s\n", num, payload);

      String command = String((char*)payload);
      command.trim();

      if (command == "LED_ON") {
        ledState = true;
        digitalWrite(LED_PIN, HIGH);
        webSocket.broadcastTXT("LED_ON");
        Serial.println("LED turned ON");
      } else if (command == "LED_OFF") {
        ledState = false;
        digitalWrite(LED_PIN, LOW);
        webSocket.broadcastTXT("LED_OFF");
        Serial.println("LED turned OFF");
      }
      break;
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("Booting");


  // Initialize SPIFFS
  if(!SPIFFS.begin(true)){
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  // Initialize LED pin
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Port defaults to 3232
  // ArduinoOTA.setPort(3232);

  // Hostname defaults to esp3232-[MAC]
  // ArduinoOTA.setHostname("myesp32");

  // No authentication by default
  // ArduinoOTA.setPassword("admin");

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  ArduinoOTA
    .onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else // U_SPIFFS
        type = "filesystem";

      // Stop servers during OTA to prevent interference
      webSocket.close();
      httpServer.stop();

      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      if (type == "filesystem") {
        SPIFFS.end();
      }
      Serial.println("Start updating " + type);
    })
    .onEnd([]() {
      Serial.println("\nEnd");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    })
    .onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });

  ArduinoOTA.begin();

  // Setup WebSocket server
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  // Setup HTTP server
  httpServer.begin();

  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.println("WebSocket server started on port 81");
  Serial.println("HTTP server started on port 80");
}

void loop() {
  ArduinoOTA.handle();
  webSocket.loop();

  // Handle HTTP clients
  WiFiClient client = httpServer.available();
  if (client) {
    handleHTTPClient(client);
  }
}