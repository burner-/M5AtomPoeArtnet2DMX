#include "Config.h"

#include <ArduinoJson.h>
#include <LittleFS.h>

namespace {
constexpr char CONFIG_PATH[] = "/config.json";
constexpr char CONFIG_TMP_PATH[] = "/config.tmp";

void copyText(char *dest, size_t destSize, const char *src) {
  if (destSize == 0) {
    return;
  }
  if (src == nullptr) {
    src = "";
  }
  strlcpy(dest, src, destSize);
}

uint32_t clampUint(uint32_t value, uint32_t minValue, uint32_t maxValue) {
  if (value < minValue) {
    return minValue;
  }
  if (value > maxValue) {
    return maxValue;
  }
  return value;
}

String normalizedHostname(const char *input) {
  String out;
  if (input != nullptr) {
    for (const char *p = input; *p != '\0' && out.length() < 32; ++p) {
      char c = static_cast<char>(tolower(*p));
      bool isAlphaNum = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
      if (isAlphaNum || c == '-') {
        out += c;
      }
    }
  }

  while (out.startsWith("-")) {
    out.remove(0, 1);
  }
  while (out.endsWith("-")) {
    out.remove(out.length() - 1);
  }
  if (out.length() == 0) {
    out = "m5atompoe-artnet2dmx";
  }
  return out;
}

bool writeStringToFile(const char *path, const String &data) {
  File file = LittleFS.open(path, "w");
  if (!file) {
    return false;
  }
  size_t written = file.print(data);
  file.close();
  return written == data.length();
}

void writeConfigDocument(const AppConfig &config, JsonDocument &doc) {
  doc["hostname"] = config.hostname;
  doc["wifiSsid"] = config.wifiSsid;
  doc["wifiPassword"] = config.wifiPassword;
  doc["ethernetDhcp"] = config.ethernetDhcp;
  doc["ethernetIp"] = config.ethernetIp;
  doc["ethernetSubnet"] = config.ethernetSubnet;
  doc["ethernetGateway"] = config.ethernetGateway;
  doc["ethernetDns"] = config.ethernetDns;
  doc["shortName"] = config.shortName;
  doc["longName"] = config.longName;
  doc["artnetNet"] = config.artnetNet;
  doc["artnetSubnet"] = config.artnetSubnet;
  doc["artnetUniverse"] = config.artnetUniverse;
  doc["dmxStartAddress"] = config.dmxStartAddress;
  doc["dmxInputEnabled"] = config.dmxInputEnabled;
  doc["artnetUnicast"] = config.artnetUnicast;
  doc["artnetTargetIp"] = config.artnetTargetIp;
}

void applyDocument(AppConfig &config, JsonDocument &doc) {
  if (doc["hostname"].is<const char *>()) {
    String host = normalizedHostname(doc["hostname"].as<const char *>());
    copyText(config.hostname, sizeof(config.hostname), host.c_str());
  }
  if (doc["wifiSsid"].is<const char *>()) {
    copyText(config.wifiSsid, sizeof(config.wifiSsid), doc["wifiSsid"].as<const char *>());
  }
  if (doc["wifiPassword"].is<const char *>()) {
    copyText(config.wifiPassword, sizeof(config.wifiPassword), doc["wifiPassword"].as<const char *>());
  }
  if (doc["ethernetDhcp"].is<bool>()) {
    config.ethernetDhcp = doc["ethernetDhcp"].as<bool>();
  }
  if (doc["ethernetIp"].is<const char *>()) {
    copyText(config.ethernetIp, sizeof(config.ethernetIp), doc["ethernetIp"].as<const char *>());
  }
  if (doc["ethernetSubnet"].is<const char *>()) {
    copyText(config.ethernetSubnet, sizeof(config.ethernetSubnet), doc["ethernetSubnet"].as<const char *>());
  }
  if (doc["ethernetGateway"].is<const char *>()) {
    copyText(config.ethernetGateway, sizeof(config.ethernetGateway), doc["ethernetGateway"].as<const char *>());
  }
  if (doc["ethernetDns"].is<const char *>()) {
    copyText(config.ethernetDns, sizeof(config.ethernetDns), doc["ethernetDns"].as<const char *>());
  }
  if (doc["shortName"].is<const char *>()) {
    copyText(config.shortName, sizeof(config.shortName), doc["shortName"].as<const char *>());
  }
  if (doc["longName"].is<const char *>()) {
    copyText(config.longName, sizeof(config.longName), doc["longName"].as<const char *>());
  }
  if (doc["artnetNet"].is<uint32_t>()) {
    config.artnetNet = static_cast<uint8_t>(clampUint(doc["artnetNet"].as<uint32_t>(), 0, 127));
  }
  if (doc["artnetSubnet"].is<uint32_t>()) {
    config.artnetSubnet = static_cast<uint8_t>(clampUint(doc["artnetSubnet"].as<uint32_t>(), 0, 15));
  }
  if (doc["artnetUniverse"].is<uint32_t>()) {
    config.artnetUniverse = static_cast<uint8_t>(clampUint(doc["artnetUniverse"].as<uint32_t>(), 0, 15));
  }
  if (doc["dmxStartAddress"].is<uint32_t>()) {
    config.dmxStartAddress = static_cast<uint16_t>(clampUint(doc["dmxStartAddress"].as<uint32_t>(), 1, 512));
  }
  if (doc["dmxInputEnabled"].is<bool>()) {
    config.dmxInputEnabled = doc["dmxInputEnabled"].as<bool>();
  }
  if (doc["artnetUnicast"].is<bool>()) {
    config.artnetUnicast = doc["artnetUnicast"].as<bool>();
  }
  if (doc["artnetTargetIp"].is<const char *>()) {
    copyText(config.artnetTargetIp, sizeof(config.artnetTargetIp), doc["artnetTargetIp"].as<const char *>());
  }
}
}  // namespace

void applyDefaultConfig(AppConfig &config) {
  copyText(config.hostname, sizeof(config.hostname), "m5atompoe-artnet2dmx");
  copyText(config.wifiSsid, sizeof(config.wifiSsid), "");
  copyText(config.wifiPassword, sizeof(config.wifiPassword), "");
  config.ethernetDhcp = true;
  copyText(config.ethernetIp, sizeof(config.ethernetIp), "192.168.1.50");
  copyText(config.ethernetSubnet, sizeof(config.ethernetSubnet), "255.255.255.0");
  copyText(config.ethernetGateway, sizeof(config.ethernetGateway), "192.168.1.1");
  copyText(config.ethernetDns, sizeof(config.ethernetDns), "192.168.1.1");
  copyText(config.shortName, sizeof(config.shortName), "M5 Atom DMX");
  copyText(config.longName, sizeof(config.longName), "M5AtomPoeArtNet2DMX");
  config.artnetNet = 0;
  config.artnetSubnet = 0;
  config.artnetUniverse = 0;
  config.dmxStartAddress = 1;
  config.dmxInputEnabled = false;
  config.artnetUnicast = false;
  copyText(config.artnetTargetIp, sizeof(config.artnetTargetIp), "2.0.0.1");
}

bool loadConfig(AppConfig &config) {
  applyDefaultConfig(config);
  if (!LittleFS.exists(CONFIG_PATH)) {
    return saveConfig(config);
  }

  File file = LittleFS.open(CONFIG_PATH, "r");
  if (!file) {
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, file);
  file.close();
  if (err) {
    return false;
  }

  applyDocument(config, doc);
  IPAddress ip;
  if (!parseConfiguredIp(config.artnetTargetIp, ip)) {
    copyText(config.artnetTargetIp, sizeof(config.artnetTargetIp), "2.0.0.1");
  }
  if (!parseConfiguredIp(config.ethernetIp, ip)) {
    copyText(config.ethernetIp, sizeof(config.ethernetIp), "192.168.1.50");
  }
  if (!parseConfiguredIp(config.ethernetSubnet, ip)) {
    copyText(config.ethernetSubnet, sizeof(config.ethernetSubnet), "255.255.255.0");
  }
  if (!parseConfiguredIp(config.ethernetGateway, ip)) {
    copyText(config.ethernetGateway, sizeof(config.ethernetGateway), "192.168.1.1");
  }
  if (!parseConfiguredIp(config.ethernetDns, ip)) {
    copyText(config.ethernetDns, sizeof(config.ethernetDns), "192.168.1.1");
  }
  return true;
}

bool saveConfig(const AppConfig &config) {
  String json;
  if (!configToJson(config, json)) {
    return false;
  }
  if (!writeStringToFile(CONFIG_TMP_PATH, json)) {
    return false;
  }
  LittleFS.remove(CONFIG_PATH);
  return LittleFS.rename(CONFIG_TMP_PATH, CONFIG_PATH);
}

bool configToJson(const AppConfig &config, String &json) {
  JsonDocument doc;
  writeConfigDocument(config, doc);
  json = "";
  serializeJsonPretty(doc, json);
  return json.length() > 0;
}

bool updateConfigFromJson(AppConfig &config, const String &json, String &error) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    error = String("JSON parse error: ") + err.c_str();
    return false;
  }

  AppConfig next = config;
  applyDocument(next, doc);

  IPAddress target;
  if (!parseConfiguredIp(next.artnetTargetIp, target)) {
    error = "artnetTargetIp must contain an IPv4 address";
    return false;
  }
  if (!next.ethernetDhcp) {
    if (!parseConfiguredIp(next.ethernetIp, target)) {
      error = "ethernetIp must contain an IPv4 address";
      return false;
    }
    if (!parseConfiguredIp(next.ethernetSubnet, target)) {
      error = "ethernetSubnet must contain an IPv4 address";
      return false;
    }
    if (!parseConfiguredIp(next.ethernetGateway, target)) {
      error = "ethernetGateway must contain an IPv4 address";
      return false;
    }
    if (!parseConfiguredIp(next.ethernetDns, target)) {
      error = "ethernetDns must contain an IPv4 address";
      return false;
    }
  }
  String host = normalizedHostname(next.hostname);
  copyText(next.hostname, sizeof(next.hostname), host.c_str());

  config = next;
  error = "";
  return true;
}

uint16_t artnetPortAddress(const AppConfig &config) {
  return (static_cast<uint16_t>(config.artnetNet & 0x7F) << 8) |
         (static_cast<uint16_t>(config.artnetSubnet & 0x0F) << 4) |
         (config.artnetUniverse & 0x0F);
}

bool parseConfiguredIp(const char *value, IPAddress &ip) {
  if (value == nullptr || value[0] == '\0') {
    return false;
  }
  return ip.fromString(value);
}
