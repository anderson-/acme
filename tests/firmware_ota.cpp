#include <AcmeOTA.h>
#include <cassert>

std::string request(AcmeOTAClass &ota, const std::string &wire) {
  incoming = std::make_shared<ClientState>();
  incoming->input = wire;
  ESP.restarted = false;
  ota.handle();
  return incoming->output;
}

int main() {
  AcmeOTAClass ota;
  assert(ota.begin("test"));
  auto info = request(ota, "GET /info HTTP/1.1\r\nHost: test\r\n\r\n");
  assert(info.find("200 OK") != std::string::npos);
  assert(info.find("\"id\":\"aabbccddeeff\"") != std::string::npos);
  assert(info.find("\"ota_protocol\":\"http-v1\"") != std::string::npos);
  std::string image(3072, 'x');
  auto success = request(ota, "POST /update HTTP/1.1\r\nContent-Length: 3072\r\n\r\n"+image);
  assert(success.find("200 OK") != std::string::npos && ESP.restarted);
  assert(Update.data.size() == image.size() && Update.largestChunk <= 1024);
  auto truncated = request(ota, "POST /update HTTP/1.1\r\nContent-Length: 10\r\n\r\nshort");
  assert(truncated.find("500 Error") != std::string::npos && !ESP.restarted && !Update.begun);
  auto empty = request(ota, "POST /update HTTP/1.1\r\nContent-Length: 0\r\n\r\n");
  assert(empty.find("400 Error") != std::string::npos && !ESP.restarted);
  Update.failWrite = true;
  auto failed = request(ota, "POST /update HTTP/1.1\r\nContent-Length: 3\r\n\r\nabc");
  assert(failed.find("500 Error") != std::string::npos && !ESP.restarted);
  Update.failWrite = false;
  auto fs = request(ota, "POST /update-fs HTTP/1.1\r\nContent-Length: 3\r\n\r\nabc");
  assert(fs.find("200 OK") != std::string::npos && ESP.restarted && Update.command == 100);
  auto chunked = request(ota, "POST /update HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n");
  assert(chunked.find("400 Error") != std::string::npos && !ESP.restarted);
}
