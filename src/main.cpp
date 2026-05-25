#include <Arduino.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <Ethernet_Generic.h>
#include <EthernetUdp.h>
#include <LittleFS.h>
#include <SPI.h>
#ifdef htons
#undef htons
#endif
#ifdef ntohs
#undef ntohs
#endif
#ifdef htonl
#undef htonl
#endif
#ifdef ntohl
#undef ntohl
#endif
#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_dmx.h>

#include "Config.h"
#include "Hardware.h"

namespace {
constexpr uint32_t ETHERNET_DHCP_TIMEOUT_MS = 8000;
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 12000;
constexpr uint32_t DMX_OUTPUT_INTERVAL_MS = 25;
constexpr uint32_t DMX_INPUT_MIN_INTERVAL_MS = 20;
constexpr uint32_t STATUS_PRINT_INTERVAL_MS = 10000;
constexpr uint32_t SETUP_BUTTON_HOLD_MS = 1200;
constexpr uint32_t WIFI_SCAN_MAX_MS_PER_CHANNEL = 120;
constexpr uint8_t ARTNET_PROTOCOL_VERSION_HI = 0;
constexpr uint8_t ARTNET_PROTOCOL_VERSION_LO = 14;
constexpr uint16_t ARTNET_OP_POLL = 0x2000;
constexpr uint16_t ARTNET_OP_POLL_REPLY = 0x2100;
constexpr uint16_t ARTNET_OP_DMX = 0x5000;
constexpr uint16_t MDNS_CLASS_IN = 0x0001;
constexpr uint16_t MDNS_CLASS_FLUSH_IN = 0x8001;
const IPAddress MDNS_MULTICAST(224, 0, 0, 251);
const IPAddress FALLBACK_AP_IP(192, 168, 4, 1);

AppConfig config;

class CompatibleEthernetServer : public EthernetServer {
 public:
  explicit CompatibleEthernetServer(uint16_t port) : EthernetServer(port) {}
  void begin(uint16_t port = 0) override {
    (void)port;
    EthernetServer::begin();
  }
};

CompatibleEthernetServer ethernetHttpServer(HTTP_PORT);
WiFiServer wifiHttpServer(HTTP_PORT);
EthernetUDP ethernetArtNetUdp;
WiFiUDP wifiArtNetUdp;
EthernetUDP ethernetMdnsUdp;
WiFiUDP wifiMdnsUdp;
DNSServer dnsServer;

uint8_t ethernetMac[6];
uint8_t dmxBuffer[DMX_PACKET_SIZE];
uint8_t artnetSequence = 1;
uint32_t rebootAtMs = 0;
uint32_t lastDmxOutputMs = 0;
uint32_t lastDmxInputMs = 0;
uint32_t lastStatusPrintMs = 0;
uint32_t setupButtonPressedAtMs = 0;
bool dmxInstalled = false;
bool setupButtonWasPressed = false;
bool setupButtonHandledForPress = false;

struct RuntimeState {
  bool ethernetOnline = false;
  bool wifiStationOnline = false;
  bool wifiApOnline = false;
  bool ethernetArtNetStarted = false;
  bool wifiArtNetStarted = false;
  bool ethernetMdnsStarted = false;
  bool wifiMdnsStarted = false;
  uint32_t artDmxReceived = 0;
  uint32_t artDmxSent = 0;
  uint32_t artPollReceived = 0;
  uint32_t dmxFramesIn = 0;
  uint32_t dmxFramesOut = 0;
  uint32_t configSaves = 0;
} runtime;

struct HttpRequest {
  String method;
  String path;
  String body;
};

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>M5AtomPoeArtNet2DMX</title>
<style>
:root{color-scheme:light dark;--bg:#f5f7fb;--panel:#fff;--text:#17202a;--muted:#5d6b7a;--line:#d8dee8;--accent:#0078d4;--accent2:#0a8754;--danger:#ba1a1a}
@media (prefers-color-scheme:dark){:root{--bg:#11161d;--panel:#18202a;--text:#eef3f8;--muted:#aab6c3;--line:#33404f;--accent:#5eb1ff;--accent2:#45c486;--danger:#ff8a80}}
*{box-sizing:border-box}body{margin:0;font:15px/1.45 system-ui,-apple-system,Segoe UI,Roboto,Arial,sans-serif;background:var(--bg);color:var(--text)}
header{display:flex;align-items:center;justify-content:space-between;gap:16px;padding:18px 20px;border-bottom:1px solid var(--line);background:var(--panel);position:sticky;top:0;z-index:2}
h1{font-size:18px;margin:0;font-weight:700;letter-spacing:0}main{max-width:1100px;margin:0 auto;padding:20px;display:grid;gap:18px}
.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:18px}.panel{background:var(--panel);border:1px solid var(--line);border-radius:8px;padding:16px}
h2{font-size:15px;margin:0 0 14px}.row{display:grid;grid-template-columns:180px 1fr;gap:12px;align-items:center;margin:10px 0}
label{color:var(--muted)}input,select{width:100%;height:38px;border:1px solid var(--line);border-radius:6px;background:transparent;color:var(--text);padding:7px 9px;font:inherit}
input[type=checkbox]{width:18px;height:18px}.check{display:flex;align-items:center;gap:10px}.actions{display:flex;gap:10px;flex-wrap:wrap}
button{height:38px;border:1px solid var(--line);border-radius:6px;background:var(--panel);color:var(--text);padding:0 12px;font:inherit;cursor:pointer}
button.primary{border-color:var(--accent);background:var(--accent);color:white}button.good{border-color:var(--accent2);background:var(--accent2);color:white}
button.danger{border-color:var(--danger);color:var(--danger)}.status{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:10px}
.metric{border:1px solid var(--line);border-radius:8px;padding:10px;min-height:64px}.metric b{display:block;font-size:18px}.metric span{color:var(--muted)}
.combo{display:flex;gap:8px}.combo input{min-width:0}.combo button{flex:0 0 auto}.message{min-height:22px;color:var(--muted)}.message.error{color:var(--danger)}.hidden{display:none}
@media (max-width:760px){header{align-items:flex-start;flex-direction:column}.grid{grid-template-columns:1fr}.row{grid-template-columns:1fr;gap:6px}.status{grid-template-columns:1fr}}
</style>
</head>
<body>
<header><h1>M5AtomPoeArtNet2DMX</h1><div class="actions"><button id="backup">Backup</button><button id="restoreBtn">Restore</button><button class="danger" id="reboot">Reboot</button></div></header>
<main>
<section class="panel">
<h2>Status</h2>
<div class="status" id="status"></div>
</section>
<form id="settings" class="grid">
<section class="panel">
<h2>Device</h2>
<div class="row"><label for="hostname">Hostname</label><input id="hostname" maxlength="32"></div>
<div class="row"><label for="wifiSsid">WiFi SSID</label><div class="combo"><input id="wifiSsid" maxlength="32" autocomplete="off" list="wifiNetworks"><button type="button" id="scanWifi">Scan</button></div><datalist id="wifiNetworks"></datalist></div>
<div class="row"><label for="wifiPassword">WiFi password</label><input id="wifiPassword" maxlength="64" type="password" autocomplete="new-password"></div>
</section>
<section class="panel">
<h2>Ethernet</h2>
<div class="row"><label for="ethernetMode">Mode</label><select id="ethernetMode"><option value="dhcp">DHCP</option><option value="static">Static</option></select></div>
<div class="row"><label for="ethernetIp">IP address</label><input id="ethernetIp" inputmode="numeric" pattern="[0-9.]*"></div>
<div class="row"><label for="ethernetSubnet">Subnet mask</label><input id="ethernetSubnet" inputmode="numeric" pattern="[0-9.]*"></div>
<div class="row"><label for="ethernetGateway">Gateway</label><input id="ethernetGateway" inputmode="numeric" pattern="[0-9.]*"></div>
<div class="row"><label for="ethernetDns">DNS server</label><input id="ethernetDns" inputmode="numeric" pattern="[0-9.]*"></div>
</section>
<section class="panel">
<h2>Art-Net</h2>
<div class="row"><label for="shortName">Short name</label><input id="shortName" maxlength="17"></div>
<div class="row"><label for="longName">Long name</label><input id="longName" maxlength="63"></div>
<div class="row"><label for="artnetNet">Net</label><input id="artnetNet" type="number" min="0" max="127"></div>
<div class="row"><label for="artnetSubnet">Subnet</label><input id="artnetSubnet" type="number" min="0" max="15"></div>
<div class="row"><label for="artnetUniverse">Universe</label><input id="artnetUniverse" type="number" min="0" max="15"></div>
</section>
<section class="panel">
<h2>DMX</h2>
<div class="row"><label for="dmxMode">Mode</label><select id="dmxMode"><option value="output">Art-Net to DMX</option><option value="input">DMX to Art-Net</option></select></div>
<div class="row"><label for="dmxStartAddress">Start address</label><input id="dmxStartAddress" type="number" min="1" max="512"></div>
</section>
<section class="panel">
<h2>Transmit</h2>
<div class="row"><label for="artnetUnicast">Mode</label><select id="artnetUnicast"><option value="false">Broadcast</option><option value="true">Unicast</option></select></div>
<div class="row"><label for="artnetTargetIp">Target IP</label><input id="artnetTargetIp" inputmode="numeric" pattern="[0-9.]*"></div>
<div class="actions"><button class="primary" type="submit">Save settings</button></div>
<div class="message" id="message"></div>
</section>
</form>
<input class="hidden" type="file" id="restoreFile" accept="application/json,.json">
</main>
<script>
const ids=["hostname","wifiSsid","wifiPassword","ethernetIp","ethernetSubnet","ethernetGateway","ethernetDns","shortName","longName","artnetNet","artnetSubnet","artnetUniverse","dmxStartAddress","artnetTargetIp"];
const msg=document.getElementById("message");
function setMsg(t,e=false){msg.textContent=t;msg.className=e?"message error":"message"}
async function api(path,opt={}){const r=await fetch(path,opt);const text=await r.text();let data={};try{data=text?JSON.parse(text):{}}catch(e){data={raw:text}}if(!r.ok)throw new Error(data.error||r.statusText);return data}
function esc(s){return String(s).replace(/[&<>"']/g,c=>({"&":"&amp;","<":"&lt;",">":"&gt;","\"":"&quot;","'":"&#39;"}[c]))}
function ethernetFields(){return["ethernetIp","ethernetSubnet","ethernetGateway","ethernetDns"]}
function updateEthernetFields(){const dhcp=document.getElementById("ethernetMode").value==="dhcp";ethernetFields().forEach(id=>document.getElementById(id).disabled=dhcp)}
function fill(c){ids.forEach(id=>{document.getElementById(id).value=c[id]??""});document.getElementById("ethernetMode").value=c.ethernetDhcp?"dhcp":"static";document.getElementById("dmxMode").value=c.dmxInputEnabled?"input":"output";document.getElementById("artnetUnicast").value=c.artnetUnicast?"true":"false";updateEthernetFields()}
function collect(){const c={};ids.forEach(id=>{const el=document.getElementById(id);c[id]=el.type==="number"?Number(el.value):el.value});c.ethernetDhcp=document.getElementById("ethernetMode").value==="dhcp";c.dmxInputEnabled=document.getElementById("dmxMode").value==="input";c.artnetUnicast=document.getElementById("artnetUnicast").value==="true";return c}
async function load(){fill(await api("/api/config"));await status()}
async function status(){const s=await api("/api/status");document.getElementById("status").innerHTML=[
["Ethernet",s.network.ethernetIp||s.network.ethernetMode],["WiFi",s.network.wifiIp||s.network.apIp||"setup"],["mDNS",s.hostname+".local"],
["ArtDmx RX",s.counters.artDmxReceived],["ArtDmx TX",s.counters.artDmxSent],["DMX",s.mode]
].map(x=>`<div class="metric"><b>${x[1]}</b><span>${x[0]}</span></div>`).join("")}
document.getElementById("settings").addEventListener("submit",async e=>{e.preventDefault();try{const r=await api("/api/config",{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify(collect())});setMsg(r.restartRecommended?"Settings saved; reboot applies network changes":"Settings saved");await status()}catch(err){setMsg(err.message,true)}})
document.getElementById("ethernetMode").onchange=updateEthernetFields
document.getElementById("scanWifi").onclick=async()=>{try{setMsg("Scanning WiFi");const data=await api("/api/wifi/scan");const list=document.getElementById("wifiNetworks");list.innerHTML=data.networks.map(n=>`<option value="${esc(n.ssid)}">${n.rssi} dBm, channel ${n.channel}</option>`).join("");setMsg(`Found ${data.networks.length} WiFi networks`)}catch(err){setMsg(err.message,true)}}
document.getElementById("backup").onclick=()=>{location.href="/api/backup"}
document.getElementById("restoreBtn").onclick=()=>document.getElementById("restoreFile").click()
document.getElementById("restoreFile").onchange=async e=>{const f=e.target.files[0];if(!f)return;try{await api("/api/restore",{method:"POST",headers:{"Content-Type":"application/json"},body:await f.text()});setMsg("Backup restored");await load()}catch(err){setMsg(err.message,true)}}
document.getElementById("reboot").onclick=async()=>{try{await api("/api/reboot",{method:"POST"});setMsg("Reboot requested")}catch(err){setMsg(err.message,true)}}
load();setInterval(status,3000);
</script>
</body>
</html>
)rawliteral";

void copyMacFromChip() {
  uint64_t chipId = ESP.getEfuseMac();
  ethernetMac[0] = 0x02;
  ethernetMac[1] = 0x4D;
  ethernetMac[2] = 0x35;
  ethernetMac[3] = static_cast<uint8_t>((chipId >> 16) & 0xFF);
  ethernetMac[4] = static_cast<uint8_t>((chipId >> 8) & 0xFF);
  ethernetMac[5] = static_cast<uint8_t>(chipId & 0xFF);
}

IPAddress broadcastFor(IPAddress ip, IPAddress mask) {
  IPAddress out;
  for (uint8_t i = 0; i < 4; ++i) {
    out[i] = static_cast<uint8_t>((ip[i] & mask[i]) | (~mask[i]));
  }
  return out;
}

IPAddress primaryIp() {
  if (runtime.ethernetOnline) {
    return Ethernet.localIP();
  }
  if (runtime.wifiStationOnline) {
    return WiFi.localIP();
  }
  if (runtime.wifiApOnline) {
    return WiFi.softAPIP();
  }
  return IPAddress(0, 0, 0, 0);
}

IPAddress primaryBroadcastIp() {
  if (runtime.ethernetOnline) {
    return broadcastFor(Ethernet.localIP(), Ethernet.subnetMask());
  }
  if (runtime.wifiStationOnline) {
    return broadcastFor(WiFi.localIP(), WiFi.subnetMask());
  }
  if (runtime.wifiApOnline) {
    return IPAddress(192, 168, 4, 255);
  }
  return IPAddress(255, 255, 255, 255);
}

bool isSetupButtonPressed() {
  return digitalRead(SETUP_BUTTON_PIN) == LOW;
}

bool ethernetLinkUsable() {
  if (Ethernet.hardwareStatus() == EthernetNoHardware) {
    return false;
  }
  if (Ethernet.localIP() == IPAddress(0, 0, 0, 0)) {
    return false;
  }
  return Ethernet.linkStatus() != LinkOFF;
}

String ipToString(IPAddress ip) {
  if (ip == IPAddress(0, 0, 0, 0)) {
    return "";
  }
  return ip.toString();
}

void beginDmx() {
  if (dmxInstalled) {
    dmx_driver_delete(DMX_NUM_1);
    dmxInstalled = false;
  }

  dmx_config_t dmxConfig = DMX_CONFIG_DEFAULT;
  dmx_personality_t personalities[] = {{1, "DMX512"}};
  int personalityCount = sizeof(personalities) / sizeof(personalities[0]);
  dmx_driver_install(DMX_NUM_1, &dmxConfig, personalities, personalityCount);
  dmx_set_pin(DMX_NUM_1, DMX_TX_PIN, DMX_RX_PIN, DMX_PIN_NO_CHANGE);
  memset(dmxBuffer, 0, sizeof(dmxBuffer));
  dmxInstalled = true;
}

void startEthernetServices() {
  ethernetHttpServer.begin();
  runtime.ethernetArtNetStarted = ethernetArtNetUdp.begin(ARTNET_PORT) == 1;
  runtime.ethernetMdnsStarted = ethernetMdnsUdp.beginMulticast(MDNS_MULTICAST, MDNS_PORT) == 1;
}

void startWiFiServices() {
  wifiHttpServer.begin();
  if (!runtime.wifiArtNetStarted) {
    runtime.wifiArtNetStarted = wifiArtNetUdp.begin(ARTNET_PORT) == 1;
  }
  if (!runtime.wifiMdnsStarted) {
    runtime.wifiMdnsStarted = wifiMdnsUdp.beginMulticast(MDNS_MULTICAST, MDNS_PORT) == 1;
  }
}

void beginEthernet() {
  SPI.begin(ETH_SCK_PIN, ETH_MISO_PIN, ETH_MOSI_PIN, ETH_CS_PIN);
  Ethernet.init(ETH_CS_PIN);
  Ethernet.setHostname(config.hostname);
  if (config.ethernetDhcp) {
    Serial.println("Starting W5500 Ethernet DHCP");
    int dhcpResult = Ethernet.begin(ethernetMac, ETHERNET_DHCP_TIMEOUT_MS);
    runtime.ethernetOnline = dhcpResult == 1 && ethernetLinkUsable();
  } else {
    IPAddress ip;
    IPAddress dns;
    IPAddress gateway;
    IPAddress subnet;
    parseConfiguredIp(config.ethernetIp, ip);
    parseConfiguredIp(config.ethernetDns, dns);
    parseConfiguredIp(config.ethernetGateway, gateway);
    parseConfiguredIp(config.ethernetSubnet, subnet);
    Serial.println("Starting W5500 Ethernet static IPv4");
    Ethernet.begin(ethernetMac, ip, dns, gateway, subnet);
    delay(250);
    runtime.ethernetOnline = ethernetLinkUsable();
  }
  if (runtime.ethernetOnline) {
    startEthernetServices();
    Serial.print("Ethernet IP: ");
    Serial.println(Ethernet.localIP());
  } else {
    Serial.println("Ethernet setup produced no active link");
  }
}

void beginWiFiStation() {
  if (strlen(config.wifiSsid) == 0) {
    return;
  }

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(config.hostname);
  WiFi.begin(config.wifiSsid, config.wifiPassword);
  Serial.print("Starting WiFi station");
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  runtime.wifiStationOnline = WiFi.status() == WL_CONNECTED;
  if (runtime.wifiStationOnline) {
    startWiFiServices();
    Serial.print("WiFi IP: ");
    Serial.println(WiFi.localIP());
  }
}

void beginAccessPoint() {
  if (runtime.wifiApOnline) {
    return;
  }
  WiFi.persistent(false);
  WiFi.mode(runtime.wifiStationOnline ? WIFI_AP_STA : WIFI_AP);
  WiFi.softAPConfig(FALLBACK_AP_IP, FALLBACK_AP_IP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(config.hostname);
  runtime.wifiApOnline = true;
  startWiFiServices();
  dnsServer.start(53, "*", WiFi.softAPIP());
  Serial.print("Setup AP IP: ");
  Serial.println(WiFi.softAPIP());
}

void beginNetwork(bool setupPortalRequested) {
  beginEthernet();
  if (!runtime.ethernetOnline) {
    beginWiFiStation();
  }
  if (setupPortalRequested) {
    beginAccessPoint();
  }
}

void maintainNetwork() {
  if (runtime.ethernetOnline) {
    Ethernet.maintain();
  }
  if (runtime.wifiApOnline) {
    dnsServer.processNextRequest();
  }
}

void handleSetupButton() {
  bool pressed = isSetupButtonPressed();
  uint32_t now = millis();
  if (pressed && !setupButtonWasPressed) {
    setupButtonPressedAtMs = now;
    setupButtonHandledForPress = false;
  }
  if (pressed && !setupButtonHandledForPress && now - setupButtonPressedAtMs >= SETUP_BUTTON_HOLD_MS) {
    beginAccessPoint();
    setupButtonHandledForPress = true;
  }
  if (!pressed) {
    setupButtonHandledForPress = false;
  }
  setupButtonWasPressed = pressed;
}

bool readHttpRequest(Client &client, HttpRequest &request) {
  client.setTimeout(1200);
  String line = client.readStringUntil('\n');
  line.trim();
  int methodEnd = line.indexOf(' ');
  int pathEnd = line.indexOf(' ', methodEnd + 1);
  if (methodEnd <= 0 || pathEnd <= methodEnd) {
    return false;
  }
  request.method = line.substring(0, methodEnd);
  request.path = line.substring(methodEnd + 1, pathEnd);
  int queryStart = request.path.indexOf('?');
  if (queryStart >= 0) {
    request.path = request.path.substring(0, queryStart);
  }

  int contentLength = 0;
  while (client.connected()) {
    String header = client.readStringUntil('\n');
    header.trim();
    if (header.length() == 0) {
      break;
    }
    String lower = header;
    lower.toLowerCase();
    if (lower.startsWith("content-length:")) {
      contentLength = lower.substring(15).toInt();
    }
  }

  request.body = "";
  if (contentLength > 0 && contentLength < 8192) {
    request.body.reserve(contentLength);
    uint32_t start = millis();
    while (request.body.length() < static_cast<size_t>(contentLength) && millis() - start < 2000) {
      while (client.available() && request.body.length() < static_cast<size_t>(contentLength)) {
        request.body += static_cast<char>(client.read());
      }
      delay(1);
    }
  }
  return true;
}

void sendHttpResponse(Client &client, int code, const char *status, const char *contentType, const String &body,
                      const char *extraHeaders = nullptr) {
  client.print("HTTP/1.1 ");
  client.print(code);
  client.print(' ');
  client.println(status);
  client.print("Content-Type: ");
  client.println(contentType);
  client.println("Cache-Control: no-store");
  client.println("Connection: close");
  if (extraHeaders != nullptr) {
    client.print(extraHeaders);
  }
  client.print("Content-Length: ");
  client.println(body.length());
  client.println();
  client.print(body);
}

void sendJsonError(Client &client, int code, const char *status, const String &message) {
  JsonDocument doc;
  doc["error"] = message;
  String body;
  serializeJson(doc, body);
  sendHttpResponse(client, code, status, "application/json", body);
}

String buildStatusJson() {
  JsonDocument doc;
  doc["hostname"] = config.hostname;
  doc["mode"] = config.dmxInputEnabled ? "DMX to Art-Net" : "Art-Net to DMX";
  doc["artnetPortAddress"] = artnetPortAddress(config);
  JsonObject network = doc["network"].to<JsonObject>();
  network["ethernetIp"] = runtime.ethernetOnline ? Ethernet.localIP().toString() : "";
  network["ethernetMode"] = config.ethernetDhcp ? "DHCP" : "Static";
  network["ethernetLink"] = runtime.ethernetOnline ? "up" : "setup";
  network["wifiIp"] = runtime.wifiStationOnline ? WiFi.localIP().toString() : "";
  network["apIp"] = runtime.wifiApOnline ? WiFi.softAPIP().toString() : "";
  network["setupPortal"] = runtime.wifiApOnline;
  network["primaryIp"] = primaryIp().toString();
  JsonObject counters = doc["counters"].to<JsonObject>();
  counters["artDmxReceived"] = runtime.artDmxReceived;
  counters["artDmxSent"] = runtime.artDmxSent;
  counters["artPollReceived"] = runtime.artPollReceived;
  counters["dmxFramesIn"] = runtime.dmxFramesIn;
  counters["dmxFramesOut"] = runtime.dmxFramesOut;
  counters["configSaves"] = runtime.configSaves;
  String body;
  serializeJson(doc, body);
  return body;
}

String encryptionTypeName(wifi_auth_mode_t authMode) {
  switch (authMode) {
    case WIFI_AUTH_OPEN:
      return "open";
    case WIFI_AUTH_WEP:
      return "WEP";
    case WIFI_AUTH_WPA_PSK:
      return "WPA";
    case WIFI_AUTH_WPA2_PSK:
      return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:
      return "WPA/WPA2";
    case WIFI_AUTH_WPA2_ENTERPRISE:
      return "WPA2 Enterprise";
    default:
      return "secured";
  }
}

String buildWifiScanJson() {
  wifi_mode_t previousMode = WiFi.getMode();
  bool restoreOff = previousMode == WIFI_OFF;
  if (previousMode == WIFI_OFF) {
    WiFi.mode(WIFI_STA);
  } else if (previousMode == WIFI_AP) {
    WiFi.mode(WIFI_AP_STA);
  }

  int16_t count = WiFi.scanNetworks(false, true, false, WIFI_SCAN_MAX_MS_PER_CHANNEL);
  JsonDocument doc;
  JsonArray networks = doc["networks"].to<JsonArray>();
  if (count > 0) {
    for (int16_t i = 0; i < count; ++i) {
      JsonObject item = networks.add<JsonObject>();
      item["ssid"] = WiFi.SSID(i);
      item["rssi"] = WiFi.RSSI(i);
      item["channel"] = WiFi.channel(i);
      item["encryption"] = encryptionTypeName(static_cast<wifi_auth_mode_t>(WiFi.encryptionType(i)));
    }
  }
  doc["count"] = networks.size();
  WiFi.scanDelete();

  if (restoreOff && !runtime.wifiStationOnline && !runtime.wifiApOnline) {
    WiFi.mode(WIFI_OFF);
  }

  String body;
  serializeJson(doc, body);
  return body;
}

void handleConfigPost(Client &client, const String &body) {
  AppConfig next = config;
  String error;
  if (!updateConfigFromJson(next, body, error)) {
    sendJsonError(client, 400, "Bad Request", error);
    return;
  }
  config = next;
  if (!saveConfig(config)) {
    sendJsonError(client, 500, "Internal Server Error", "Configuration save failed");
    return;
  }
  runtime.configSaves++;
  beginDmx();

  JsonDocument doc;
  doc["saved"] = true;
  doc["restartRecommended"] = true;
  String response;
  serializeJson(doc, response);
  sendHttpResponse(client, 200, "OK", "application/json", response);
}

void handleHttp(Client &client) {
  HttpRequest request;
  if (!readHttpRequest(client, request)) {
    client.stop();
    return;
  }

  if (request.method == "GET" && (request.path == "/" || request.path == "/index.html" ||
                                  request.path == "/generate_204" || request.path == "/hotspot-detect.html")) {
    sendHttpResponse(client, 200, "OK", "text/html; charset=utf-8", FPSTR(INDEX_HTML));
  } else if (request.method == "GET" && request.path == "/api/config") {
    String body;
    configToJson(config, body);
    sendHttpResponse(client, 200, "OK", "application/json", body);
  } else if (request.method == "POST" && request.path == "/api/config") {
    handleConfigPost(client, request.body);
  } else if (request.method == "GET" && request.path == "/api/status") {
    sendHttpResponse(client, 200, "OK", "application/json", buildStatusJson());
  } else if (request.method == "GET" && request.path == "/api/wifi/scan") {
    sendHttpResponse(client, 200, "OK", "application/json", buildWifiScanJson());
  } else if (request.method == "GET" && request.path == "/api/backup") {
    String body;
    configToJson(config, body);
    sendHttpResponse(client, 200, "OK", "application/json", body,
                     "Content-Disposition: attachment; filename=\"m5atompoe-artnet2dmx-config.json\"\r\n");
  } else if (request.method == "POST" && request.path == "/api/restore") {
    handleConfigPost(client, request.body);
  } else if (request.method == "POST" && request.path == "/api/reboot") {
    rebootAtMs = millis() + 800;
    sendHttpResponse(client, 200, "OK", "application/json", "{\"reboot\":true}");
  } else {
    sendHttpResponse(client, 200, "OK", "text/html; charset=utf-8", FPSTR(INDEX_HTML));
  }

  delay(1);
  client.stop();
}

template <typename TServer>
void pollHttpServer(TServer &server) {
  auto client = server.available();
  if (client) {
    handleHttp(client);
  }
}

void writeDnsName(uint8_t *out, size_t &offset, size_t maxLen, const String &name) {
  int start = 0;
  while (start < name.length()) {
    int dot = name.indexOf('.', start);
    if (dot < 0) {
      dot = name.length();
    }
    int labelLen = dot - start;
    if (labelLen > 0 && offset + labelLen + 1 < maxLen) {
      out[offset++] = static_cast<uint8_t>(labelLen);
      for (int i = start; i < dot; ++i) {
        out[offset++] = static_cast<uint8_t>(name[i]);
      }
    }
    start = dot + 1;
  }
  if (offset < maxLen) {
    out[offset++] = 0;
  }
}

void write16(uint8_t *out, size_t &offset, uint16_t value) {
  out[offset++] = static_cast<uint8_t>((value >> 8) & 0xFF);
  out[offset++] = static_cast<uint8_t>(value & 0xFF);
}

void write32(uint8_t *out, size_t &offset, uint32_t value) {
  out[offset++] = static_cast<uint8_t>((value >> 24) & 0xFF);
  out[offset++] = static_cast<uint8_t>((value >> 16) & 0xFF);
  out[offset++] = static_cast<uint8_t>((value >> 8) & 0xFF);
  out[offset++] = static_cast<uint8_t>(value & 0xFF);
}

String readDnsName(const uint8_t *packet, int packetLen, int &offset, int depth = 0) {
  if (depth > 6 || offset >= packetLen) {
    return "";
  }
  String name;
  while (offset < packetLen) {
    uint8_t len = packet[offset++];
    if (len == 0) {
      break;
    }
    if ((len & 0xC0) == 0xC0) {
      if (offset >= packetLen) {
        break;
      }
      int ptr = ((len & 0x3F) << 8) | packet[offset++];
      name += readDnsName(packet, packetLen, ptr, depth + 1);
      break;
    }
    if (offset + len > packetLen) {
      break;
    }
    if (name.length() > 0) {
      name += ".";
    }
    for (uint8_t i = 0; i < len; ++i) {
      name += static_cast<char>(tolower(packet[offset++]));
    }
  }
  return name;
}

void addARecord(uint8_t *out, size_t &offset, const String &hostName, IPAddress ip) {
  writeDnsName(out, offset, 512, hostName);
  write16(out, offset, 1);
  write16(out, offset, MDNS_CLASS_FLUSH_IN);
  write32(out, offset, 120);
  write16(out, offset, 4);
  for (uint8_t i = 0; i < 4; ++i) {
    out[offset++] = ip[i];
  }
}

void addPtrRecord(uint8_t *out, size_t &offset, const String &serviceType, const String &instanceName) {
  writeDnsName(out, offset, 512, serviceType);
  write16(out, offset, 12);
  write16(out, offset, MDNS_CLASS_IN);
  write32(out, offset, 120);
  size_t lenOffset = offset;
  write16(out, offset, 0);
  size_t rdataStart = offset;
  writeDnsName(out, offset, 512, instanceName);
  uint16_t rdataLen = static_cast<uint16_t>(offset - rdataStart);
  out[lenOffset] = static_cast<uint8_t>((rdataLen >> 8) & 0xFF);
  out[lenOffset + 1] = static_cast<uint8_t>(rdataLen & 0xFF);
}

void addSrvRecord(uint8_t *out, size_t &offset, const String &instanceName, const String &hostName) {
  writeDnsName(out, offset, 512, instanceName);
  write16(out, offset, 33);
  write16(out, offset, MDNS_CLASS_FLUSH_IN);
  write32(out, offset, 120);
  size_t lenOffset = offset;
  write16(out, offset, 0);
  size_t rdataStart = offset;
  write16(out, offset, 0);
  write16(out, offset, 0);
  write16(out, offset, HTTP_PORT);
  writeDnsName(out, offset, 512, hostName);
  uint16_t rdataLen = static_cast<uint16_t>(offset - rdataStart);
  out[lenOffset] = static_cast<uint8_t>((rdataLen >> 8) & 0xFF);
  out[lenOffset + 1] = static_cast<uint8_t>(rdataLen & 0xFF);
}

void addTxtRecord(uint8_t *out, size_t &offset, const String &instanceName) {
  const char txt[] = "path=/";
  writeDnsName(out, offset, 512, instanceName);
  write16(out, offset, 16);
  write16(out, offset, MDNS_CLASS_FLUSH_IN);
  write32(out, offset, 120);
  write16(out, offset, sizeof(txt));
  out[offset++] = sizeof(txt) - 1;
  memcpy(out + offset, txt, sizeof(txt) - 1);
  offset += sizeof(txt) - 1;
}

void sendMdnsResponse(UDP &udp, IPAddress ip) {
  uint8_t out[512] = {0};
  size_t offset = 0;
  String hostName = String(config.hostname) + ".local";
  String serviceType = "_http._tcp.local";
  String instanceName = String(config.hostname) + "._http._tcp.local";

  write16(out, offset, 0);
  write16(out, offset, 0x8400);
  write16(out, offset, 0);
  write16(out, offset, 4);
  write16(out, offset, 0);
  write16(out, offset, 0);
  addARecord(out, offset, hostName, ip);
  addPtrRecord(out, offset, serviceType, instanceName);
  addSrvRecord(out, offset, instanceName, hostName);
  addTxtRecord(out, offset, instanceName);

  udp.beginPacket(MDNS_MULTICAST, MDNS_PORT);
  udp.write(out, offset);
  udp.endPacket();
}

void processMdns(UDP &udp, IPAddress ip) {
  int packetSize = udp.parsePacket();
  if (packetSize <= 0 || packetSize > 512) {
    return;
  }
  uint8_t packet[512];
  int len = udp.read(packet, min(packetSize, 512));
  if (len < 12) {
    return;
  }
  uint16_t qdCount = (packet[4] << 8) | packet[5];
  int offset = 12;
  String hostName = String(config.hostname) + ".local";
  String serviceType = "_http._tcp.local";
  String instanceName = String(config.hostname) + "._http._tcp.local";
  bool shouldReply = false;
  for (uint16_t i = 0; i < qdCount && offset < len; ++i) {
    String qName = readDnsName(packet, len, offset);
    if (offset + 4 > len) {
      break;
    }
    uint16_t qType = (packet[offset] << 8) | packet[offset + 1];
    offset += 4;
    qName.toLowerCase();
    shouldReply = shouldReply || qName == hostName || qName == serviceType || qName == instanceName || qType == 255;
  }
  if (shouldReply) {
    sendMdnsResponse(udp, ip);
  }
}

void writeArtNetHeader(uint8_t *packet, uint16_t opcode) {
  memcpy(packet, "Art-Net", 7);
  packet[7] = 0;
  packet[8] = static_cast<uint8_t>(opcode & 0xFF);
  packet[9] = static_cast<uint8_t>((opcode >> 8) & 0xFF);
}

void sendArtPollReply(UDP &udp, IPAddress destination, IPAddress localIp) {
  uint8_t reply[239] = {0};
  writeArtNetHeader(reply, ARTNET_OP_POLL_REPLY);
  for (uint8_t i = 0; i < 4; ++i) {
    reply[10 + i] = localIp[i];
  }
  reply[14] = 0x36;
  reply[15] = 0x19;
  reply[16] = 0x00;
  reply[17] = 0x01;
  reply[18] = config.artnetNet;
  reply[19] = config.artnetSubnet;
  reply[20] = 0xFF;
  reply[21] = 0xFF;
  reply[23] = 0xD0;
  strlcpy(reinterpret_cast<char *>(reply + 26), config.shortName, 18);
  strlcpy(reinterpret_cast<char *>(reply + 44), config.longName, 64);
  strlcpy(reinterpret_cast<char *>(reply + 108), "#0001 Online", 64);
  reply[173] = 0x01;
  reply[174] = 0xC0;
  reply[186] = config.artnetUniverse;
  reply[190] = config.artnetUniverse;
  reply[200] = 0;
  reply[201] = 0;
  reply[202] = 0;
  reply[212] = 0x00;
  memcpy(reply + 213, ethernetMac, sizeof(ethernetMac));
  for (uint8_t i = 0; i < 4; ++i) {
    reply[219 + i] = localIp[i];
  }
  reply[223] = 1;
  reply[224] = 0x0E;

  udp.beginPacket(destination, ARTNET_PORT);
  udp.write(reply, sizeof(reply));
  udp.endPacket();
}

void copyArtDmxToDmx(const uint8_t *packet, uint16_t packetLen) {
  if (packetLen < 18) {
    return;
  }
  uint16_t portAddress = packet[14] | (static_cast<uint16_t>(packet[15]) << 8);
  if (portAddress != artnetPortAddress(config)) {
    return;
  }
  uint16_t dmxLength = (static_cast<uint16_t>(packet[16]) << 8) | packet[17];
  if (dmxLength > 512) {
    dmxLength = 512;
  }
  if (18 + dmxLength > packetLen) {
    return;
  }
  uint16_t startSlot = config.dmxStartAddress;
  uint16_t writable = 513 - startSlot;
  uint16_t copyLen = min(dmxLength, writable);
  memcpy(dmxBuffer + startSlot, packet + 18, copyLen);
  dmxBuffer[0] = 0;
  runtime.artDmxReceived++;
}

void processArtNetPacket(UDP &udp, IPAddress localIp) {
  int packetSize = udp.parsePacket();
  if (packetSize <= 0) {
    return;
  }
  if (packetSize > 600) {
    while (udp.available()) {
      udp.read();
    }
    return;
  }

  uint8_t packet[600];
  int len = udp.read(packet, min(packetSize, 600));
  if (len < 10 || memcmp(packet, "Art-Net", 7) != 0 || packet[7] != 0) {
    return;
  }
  uint16_t opcode = packet[8] | (static_cast<uint16_t>(packet[9]) << 8);
  if (opcode == ARTNET_OP_POLL) {
    runtime.artPollReceived++;
    sendArtPollReply(udp, udp.remoteIP(), localIp);
  } else if (opcode == ARTNET_OP_DMX && !config.dmxInputEnabled) {
    copyArtDmxToDmx(packet, len);
  }
}

void sendArtDmxPacket(UDP &udp, IPAddress targetIp, const uint8_t *slots, uint16_t slotCount) {
  uint16_t length = slotCount;
  if (length < 2) {
    length = 2;
  }
  if (length > 512) {
    length = 512;
  }
  if (length % 2 != 0) {
    length++;
  }

  uint8_t packet[530] = {0};
  writeArtNetHeader(packet, ARTNET_OP_DMX);
  packet[10] = ARTNET_PROTOCOL_VERSION_HI;
  packet[11] = ARTNET_PROTOCOL_VERSION_LO;
  packet[12] = artnetSequence++;
  packet[13] = 0;
  uint16_t portAddress = artnetPortAddress(config);
  packet[14] = static_cast<uint8_t>(portAddress & 0xFF);
  packet[15] = static_cast<uint8_t>((portAddress >> 8) & 0xFF);
  packet[16] = static_cast<uint8_t>((length >> 8) & 0xFF);
  packet[17] = static_cast<uint8_t>(length & 0xFF);
  memcpy(packet + 18, slots, min<uint16_t>(slotCount, length));

  udp.beginPacket(targetIp, ARTNET_PORT);
  udp.write(packet, 18 + length);
  udp.endPacket();
  runtime.artDmxSent++;
}

void sendDmxAsArtNet() {
  IPAddress targetIp;
  if (config.artnetUnicast) {
    parseConfiguredIp(config.artnetTargetIp, targetIp);
  } else {
    targetIp = primaryBroadcastIp();
  }

  uint16_t startSlot = config.dmxStartAddress;
  uint16_t slotCount = 513 - startSlot;
  if (runtime.ethernetOnline && runtime.ethernetArtNetStarted) {
    sendArtDmxPacket(ethernetArtNetUdp, targetIp, dmxBuffer + startSlot, slotCount);
  } else if ((runtime.wifiStationOnline || runtime.wifiApOnline) && runtime.wifiArtNetStarted) {
    sendArtDmxPacket(wifiArtNetUdp, targetIp, dmxBuffer + startSlot, slotCount);
  }
}

void handleDmx() {
  if (!dmxInstalled) {
    return;
  }
  uint32_t now = millis();
  if (config.dmxInputEnabled) {
    if (now - lastDmxInputMs < DMX_INPUT_MIN_INTERVAL_MS) {
      return;
    }
    dmx_packet_t packet;
    if (dmx_receive(DMX_NUM_1, &packet, 0) > 0 && packet.err == DMX_OK) {
      dmx_read(DMX_NUM_1, dmxBuffer, DMX_PACKET_SIZE);
      runtime.dmxFramesIn++;
      lastDmxInputMs = now;
      sendDmxAsArtNet();
    }
  } else if (now - lastDmxOutputMs >= DMX_OUTPUT_INTERVAL_MS) {
    dmx_write(DMX_NUM_1, dmxBuffer, DMX_PACKET_SIZE);
    dmx_send(DMX_NUM_1);
    lastDmxOutputMs = now;
    runtime.dmxFramesOut++;
  }
}

void printStatus() {
  uint32_t now = millis();
  if (now - lastStatusPrintMs < STATUS_PRINT_INTERVAL_MS) {
    return;
  }
  lastStatusPrintMs = now;
  Serial.print("Mode=");
  Serial.print(config.dmxInputEnabled ? "DMX input" : "DMX output");
  Serial.print(" IP=");
  Serial.print(primaryIp());
  Serial.print(" ArtDmxRx=");
  Serial.print(runtime.artDmxReceived);
  Serial.print(" ArtDmxTx=");
  Serial.println(runtime.artDmxSent);
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("M5AtomPoeArtNet2DMX starting");

  pinMode(SETUP_BUTTON_PIN, INPUT);
  copyMacFromChip();
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed");
  }
  if (!loadConfig(config)) {
    applyDefaultConfig(config);
    saveConfig(config);
  }

  beginDmx();
  beginNetwork(isSetupButtonPressed());
}

void loop() {
  handleSetupButton();
  maintainNetwork();

  if (runtime.ethernetOnline) {
    pollHttpServer(ethernetHttpServer);
  }
  if (runtime.wifiStationOnline || runtime.wifiApOnline) {
    pollHttpServer(wifiHttpServer);
  }
  if (runtime.ethernetOnline && runtime.ethernetArtNetStarted) {
    processArtNetPacket(ethernetArtNetUdp, Ethernet.localIP());
  }
  if ((runtime.wifiStationOnline || runtime.wifiApOnline) && runtime.wifiArtNetStarted) {
    processArtNetPacket(wifiArtNetUdp, runtime.wifiStationOnline ? WiFi.localIP() : WiFi.softAPIP());
  }
  if (runtime.ethernetOnline && runtime.ethernetMdnsStarted) {
    processMdns(ethernetMdnsUdp, Ethernet.localIP());
  }
  if ((runtime.wifiStationOnline || runtime.wifiApOnline) && runtime.wifiMdnsStarted) {
    processMdns(wifiMdnsUdp, runtime.wifiStationOnline ? WiFi.localIP() : WiFi.softAPIP());
  }

  handleDmx();
  printStatus();

  if (rebootAtMs > 0 && millis() >= rebootAtMs) {
    ESP.restart();
  }
}
