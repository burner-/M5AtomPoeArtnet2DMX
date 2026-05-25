#pragma once

#include <Arduino.h>
#include <IPAddress.h>

struct AppConfig {
  char hostname[33];
  char wifiSsid[33];
  char wifiPassword[65];
  bool ethernetDhcp;
  char ethernetIp[16];
  char ethernetSubnet[16];
  char ethernetGateway[16];
  char ethernetDns[16];
  char shortName[18];
  char longName[64];
  uint8_t artnetNet;
  uint8_t artnetSubnet;
  uint8_t artnetUniverse;
  uint16_t dmxStartAddress;
  bool dmxInputEnabled;
  bool artnetUnicast;
  char artnetTargetIp[16];
};

void applyDefaultConfig(AppConfig &config);
bool loadConfig(AppConfig &config);
bool saveConfig(const AppConfig &config);
bool configToJson(const AppConfig &config, String &json);
bool updateConfigFromJson(AppConfig &config, const String &json, String &error);
uint16_t artnetPortAddress(const AppConfig &config);
bool parseConfiguredIp(const char *value, IPAddress &ip);
