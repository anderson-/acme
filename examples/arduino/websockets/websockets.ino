#include <AcmeOTA.h>
#include <WebSocketsServer.h>
#include <SPIFFS.h>

const char* ssid = STASSID;
const char* password = STAPSK;

const int LED_PIN = 5;
bool ledState = false;

AcmeOTAClass ota;
WebSocketsServer webSocket = WebSocketsServer(81);

bool handleHTTPClient(WiFiClient &client, const String &method, const String &path) {
  if (method != "GET" || path != "/") return false;
  File file = SPIFFS.open("/index.html", "r");
  if (!file) {
    AcmeOTAClass::reply(client, 404, "text/plain", "file not found\n");
    return true;
  }
  client.printf("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %u\r\nConnection: close\r\n\r\n", (unsigned)file.size());
  uint8_t buffer[1024];
  while (file.available()) {
    size_t count = file.read(buffer, sizeof(buffer));
    client.write(buffer, count);
  }
  file.close();
  return true;
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

  ota.onRequest = handleHTTPClient;
  ota.beforeUpdate = [](bool filesystem) {
    webSocket.close();
    if (filesystem) SPIFFS.end();
  };
  ota.afterUpdate = [](bool filesystem, bool success) {
    if (!success) {
      if (filesystem) SPIFFS.begin(false);
      webSocket.begin();
      webSocket.onEvent(webSocketEvent);
    }
  };
  if (!ota.begin("acme-websockets")) {
    Serial.println("mDNS initialization failed");
    return;
  }

  // Setup WebSocket server
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);


  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.println("WebSocket server started on port 81");
  Serial.println("HTTP server started on port 80");
}

void loop() {
  ota.handle();
  webSocket.loop();
}
