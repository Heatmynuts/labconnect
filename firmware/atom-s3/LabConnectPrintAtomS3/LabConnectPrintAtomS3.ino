#include <M5AtomS3.h>
#include <WiFi.h>
#include <WebSocketsServer.h>
#include <Preferences.h>

// LabConnect Print - AtomS3 bridge
// Multi-brand balance support via RS-232
//
// Brand stored in NVS, set via admin commands (not exposed to end users).
// Admin WS:     admin:brand:<id> | admin:brand | admin:brands | admin:cmd:<raw> | admin:reboot
// Admin Serial:  brand <id>      | brand       | brands       | cmd <raw>       | reboot
//
// Board: ESP32-S3 / M5Stack AtomS3
// Arduino libs: M5AtomS3, WebSockets (Markus Sattler)

const char* AP_SSID_BASE = "LabConnect-Print";
const char* AP_PASSWORD = "labconnect";

IPAddress localIp(192, 168, 4, 1);
IPAddress gateway(192, 168, 4, 1);
IPAddress subnet(255, 255, 255, 0);

const uint8_t BALANCE_RX_PIN = 5;
const uint8_t BALANCE_TX_PIN = 6;

const uint16_t MAX_LINE_LENGTH = 128;
const uint32_t LINE_TIMEOUT_MS = 300;
const uint32_t STREAM_STALE_MS = 1800;

// ---------------------------------------------------------------------------
// Balance brand profiles
// ---------------------------------------------------------------------------

struct BalanceProfile {
  const char* id;
  const char* name;
  uint32_t baudRate;
  uint32_t serialConfig;
  const char* cmdTare;
  const char* cmdClearTare;
  const char* cmdZero;
  const char* cmdWeight;
  const char* cmdPrint;
  const char* cmdQueryType;
  const char* cmdQuerySerial;
  const char* cmdQueryId;
  const char* cmdInit;
};

// Serial configs: A&D 2400/7E1, Sartorius 9600/8O1, Shimadzu 9600/7E1, others 9600/8N1
// Empty command string = not supported for this brand.
// cmdInit is sent once after serial port init (e.g. Sartorius continuous mode).
const BalanceProfile profiles[] = {
  //  id              name               baud   config       tare  clearTare  zero  weight  print  qType   qSerial  qId    init
  { "and",           "A&D",             2400,  SERIAL_7E1,  "T",  "PT:0 g",  "RZ", "Q",   "P",   "?TN",  "?SN",   "?ID", ""     },
  { "mettler",       "Mettler Toledo",  9600,  SERIAL_8N1,  "T",  "",        "Z",  "S",   "P",   "I1",   "I2",    "I3",  ""     },
  { "sartorius",     "Sartorius",       9600,  SERIAL_8O1,  "T",  "",        "Z",  "P",   "P",   "",     "",      "",    "CONT" },
  { "ohaus",         "Ohaus",           9600,  SERIAL_8N1,  "T",  "",        "Z",  "IP",  "P",   "I1",   "I2",    "",    ""     },
  { "kern",          "Kern",            9600,  SERIAL_8N1,  "T",  "",        "Z",  "S",   "P",   "",     "",      "",    ""     },
  { "shimadzu",      "Shimadzu",        9600,  SERIAL_7E1,  "T",  "",        "Z",  "Q",   "P",   "",     "",      "",    ""     },
  { "precisa",       "Precisa",         9600,  SERIAL_8N1,  "T",  "",        "Z",  "SI",  "P",   "",     "",      "",    ""     },
  { "precia-molen",  "Precia Molen",    9600,  SERIAL_8N1,  "T",  "",        "Z",  "S",   "P",   "",     "",      "",    ""     },
  { "bizerba",       "Bizerba",         9600,  SERIAL_8N1,  "T",  "",        "Z",  "W",   "P",   "",     "",      "",    ""     },
  { "dini",          "Dini Argeo",      9600,  SERIAL_8N1,  "T",  "",        "Z",  "S",   "P",   "",     "",      "",    ""     },
};

const uint8_t PROFILE_COUNT = sizeof(profiles) / sizeof(profiles[0]);
const BalanceProfile* activeBrand = &profiles[0];

enum LineEnding : uint8_t {
  END_CR,
  END_LF,
  END_CRLF
};

struct ScanProfile {
  const char* profileId;
  uint32_t baudRate;
  uint32_t serialConfig;
  const char* command;
  LineEnding ending;
  uint16_t timeoutMs;
  const char* label;
};

const ScanProfile scanProfiles[] = {
  { "and",      2400,  SERIAL_7E1, "Q",    END_CRLF, 500, "A&D 2400/7E1" },
  { "mettler",  9600,  SERIAL_8N1, "SI",   END_CRLF, 650, "SICS 9600/8N1" },
  { "kern",     9600,  SERIAL_8N1, "W",    END_CRLF, 650, "Kern 9600/8N1" },
  { "ohaus",    9600,  SERIAL_8N1, "IP",   END_CRLF, 650, "Ohaus 9600/8N1" },
  { "dini",     9600,  SERIAL_8N1, "READ", END_CRLF, 650, "Dini 9600/8N1" },
  { "sartorius",9600,  SERIAL_8O1, "\x1bP", END_CRLF, 750, "Sartorius 9600/8O1" },
  { "precisa",  9600,  SERIAL_8N1, "PRT",  END_CRLF, 750, "Precisa 9600/8N1" },
  { "shimadzu", 1200,  SERIAL_8N1, "D05",  END_CR,   750, "Shimadzu 1200/8N1" },
  { "mettler",  4800,  SERIAL_8N1, "SI",   END_CRLF, 750, "SICS 4800/8N1" },
  { "and",      4800,  SERIAL_7E1, "Q",    END_CRLF, 650, "A&D 4800/7E1" },
  { "and",      9600,  SERIAL_7E1, "Q",    END_CRLF, 650, "A&D 9600/7E1" },
  { "kern",     4800,  SERIAL_8N1, "W",    END_CRLF, 750, "Kern 4800/8N1" },
  { "ohaus",    4800,  SERIAL_8N1, "IP",   END_CRLF, 750, "Ohaus 4800/8N1" },
};

const uint8_t SCAN_PROFILE_COUNT = sizeof(scanProfiles) / sizeof(scanProfiles[0]);

enum ScanState : uint8_t {
  SCAN_OFF,
  SCAN_BOOT_WAIT,
  SCAN_CONFIGURE,
  SCAN_SETTLE,
  SCAN_WAIT_RESPONSE,
  SCAN_RETRY_DELAY,
  SCAN_DONE,
  SCAN_FAILED
};

Preferences prefs;

// ---------------------------------------------------------------------------
// Hardware
// ---------------------------------------------------------------------------

HardwareSerial BalanceSerial(1);
WebSocketsServer webSocket(80, "/ws");

String lineBuffer;
String serialAdminBuffer;
String nodeId;
String nodeName;
String apSsid;
String lastWeightLine = "En attente";
String balanceType = "-";
String balanceSerial = "-";
String balanceId = "-";

LGFX_Sprite screen(&M5.Lcd);

enum IdentityQuery {
  QUERY_NONE,
  QUERY_TYPE,
  QUERY_SERIAL,
  QUERY_ID
};

IdentityQuery pendingQuery = QUERY_NONE;
uint32_t nextIdentityQueryAt = 0;
uint32_t pendingQueryStartedAt = 0;
uint32_t nextScreenRenderAt = 0;
uint32_t nextDeviceInfoBroadcastAt = 0;
uint32_t lastStreamAt = 0;
uint32_t lastLineCharAt = 0;
uint32_t scanStateSince = 0;
uint32_t nextAutoScanCheckAt = 0;
uint8_t connectedClients = 0;
uint8_t scanProfileIndex = 0;
uint8_t scanAttempt = 0;
bool screenDirty = true;
bool streamWasActive = false;
bool identityRequested = false;
bool triedQueryType = false;
bool triedQuerySerial = false;
bool triedQueryId = false;
bool autoDetectEnabled = true;
ScanState scanState = SCAN_OFF;

const uint32_t IDENTITY_QUERY_DELAY_MS = 700;
const uint32_t IDENTITY_QUERY_TIMEOUT_MS = 1800;
const uint32_t SCREEN_RENDER_INTERVAL_MS = 250;
const uint32_t DEVICE_INFO_BROADCAST_INTERVAL_MS = 2500;
const uint32_t AUTOSCAN_BOOT_DELAY_MS = 2400;
const uint32_t AUTOSCAN_SETTLE_MS = 90;
const uint32_t AUTOSCAN_RETRY_DELAY_MS = 100;

// ---------------------------------------------------------------------------
// Brand helpers
// ---------------------------------------------------------------------------

const BalanceProfile* findProfile(const String& id) {
  for (uint8_t i = 0; i < PROFILE_COUNT; i++) {
    if (id == profiles[i].id) return &profiles[i];
  }
  return nullptr;
}

String serialConfigLabel() {
  String label = String(activeBrand->baudRate);
  label += " ";
  switch (activeBrand->serialConfig) {
    case SERIAL_7E1: label += "7E1"; break;
    case SERIAL_8O1: label += "8O1"; break;
    case SERIAL_8N1: label += "8N1"; break;
    case SERIAL_7N1: label += "7N1"; break;
    case SERIAL_8E1: label += "8E1"; break;
    default:         label += "8N1"; break;
  }
  return label;
}

void loadBrand() {
  prefs.begin("labconnect", true);
  String id = prefs.getString("brand", "and");
  nodeId = prefs.getString("nodeId", "");
  nodeName = prefs.getString("nodeName", "");
  apSsid = prefs.getString("apSsid", "");
  autoDetectEnabled = prefs.getBool("autoScan", true);
  prefs.end();

  const BalanceProfile* p = findProfile(id);
  activeBrand = p ? p : &profiles[0];

  if (nodeId.length() == 0) {
    nodeId = defaultNodeId();
  }
  if (nodeName.length() == 0) {
    nodeName = "Balance " + nodeId;
  }
  if (apSsid.length() == 0) {
    apSsid = AP_SSID_BASE;
  }
}

void saveBrand(const BalanceProfile* profile) {
  prefs.begin("labconnect", false);
  prefs.putString("brand", profile->id);
  prefs.end();
}

void saveNodeIdentity() {
  prefs.begin("labconnect", false);
  prefs.putString("nodeId", nodeId);
  prefs.putString("nodeName", nodeName);
  prefs.putString("apSsid", apSsid);
  prefs.putBool("autoScan", autoDetectEnabled);
  prefs.end();
}

void applyBrand(const BalanceProfile* profile) {
  activeBrand = profile;
  saveBrand(profile);

  configureBalanceSerial(activeBrand->baudRate, activeBrand->serialConfig);

  if (strlen(activeBrand->cmdInit) > 0) {
    delay(300);
    sendBalanceCommand(activeBrand->cmdInit);
  }

  balanceType = "-";
  balanceSerial = "-";
  balanceId = "-";
  lastWeightLine = "En attente";
  lineBuffer = "";
  pendingQuery = QUERY_NONE;
  identityRequested = false;
  triedQueryType = false;
  triedQuerySerial = false;
  triedQueryId = false;
  nextIdentityQueryAt = 0;
  screenDirty = true;

  if (connectedClients > 0) {
    broadcastDeviceInfo();
  }
}

void configureBalanceSerial(uint32_t baudRate, uint32_t serialConfig) {
  BalanceSerial.end();
  delay(4);
  BalanceSerial.begin(baudRate, serialConfig, BALANCE_RX_PIN, BALANCE_TX_PIN);
  while (BalanceSerial.available()) {
    BalanceSerial.read();
  }
  lineBuffer = "";
  lastLineCharAt = 0;
}

// ---------------------------------------------------------------------------
// Setup & loop
// ---------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(300);

  initScreen();
  loadBrand();

  configureBalanceSerial(activeBrand->baudRate, activeBrand->serialConfig);

  if (strlen(activeBrand->cmdInit) > 0) {
    delay(300);
    sendBalanceCommand(activeBrand->cmdInit);
  }

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(localIp, gateway, subnet);
  WiFi.softAP(apSsid.c_str(), AP_PASSWORD);

  webSocket.begin();
  webSocket.onEvent(onWebSocketEvent);

  Serial.println("LabConnect AtomS3 bridge ready");
  Serial.print("Brand: ");
  Serial.print(activeBrand->name);
  Serial.print(" (");
  Serial.print(serialConfigLabel());
  Serial.println(")");
  Serial.print("AP: ");
  Serial.println(apSsid);
  Serial.println("WS: ws://192.168.4.1/ws");

  screenDirty = true;
  startIdentityQueries();
  nextAutoScanCheckAt = millis() + AUTOSCAN_BOOT_DELAY_MS;
}

void loop() {
  AtomS3.update();
  webSocket.loop();
  readBalance();
  readSerialAdmin();
  runAutoDetect();
  startAutoDetectIfNeeded();
  runIdentityQuery();
  flushStaleBalanceLine();
  updateStreamState();
  broadcastDeviceInfoIfNeeded();
  renderScreenIfNeeded();
}

// ---------------------------------------------------------------------------
// Balance serial read
// ---------------------------------------------------------------------------

void readBalance() {
  while (BalanceSerial.available()) {
    char c = (char)BalanceSerial.read();

    if (c == '\r' || c == '\n') {
      if (lineBuffer.length() > 0) {
        handleBalanceLine(lineBuffer);
        lineBuffer = "";
      }
      continue;
    }

    lastLineCharAt = millis();
    if (lineBuffer.length() < MAX_LINE_LENGTH) {
      lineBuffer += c;
    } else {
      handleBalanceLine(lineBuffer);
      lineBuffer = "";
    }
  }
}

void flushStaleBalanceLine() {
  if (lineBuffer.length() == 0) {
    return;
  }
  if (millis() - lastLineCharAt < LINE_TIMEOUT_MS) {
    return;
  }
  handleBalanceLine(lineBuffer);
  lineBuffer = "";
}

void handleBalanceLine(const String& line) {
  if (handleScanLine(line)) {
    return;
  }

  if (captureIdentityResponse(line)) {
    screenDirty = true;
    return;
  }

  detectBrandFromStream(line);
  lastWeightLine = compactLine(line, 26);
  lastStreamAt = millis();
  if (!streamWasActive) {
    streamWasActive = true;
    broadcastDeviceInfo();
  }
  screenDirty = true;
  broadcastRawLine(line);
}

void broadcastRawLine(const String& line) {
  Serial.print("BALANCE > ");
  Serial.println(line);
  String payload = line;
  webSocket.broadcastTXT(payload);
}

// ---------------------------------------------------------------------------
// Device info JSON (factored)
// ---------------------------------------------------------------------------

String buildDeviceInfoPayload() {
  String model = balanceType == "-" ? "Balance" : balanceType;
  String displayName = balanceType == "-" ? nodeName : String(activeBrand->name) + " " + model;

  String payload = "{\"type\":\"device\",\"kind\":\"balance\",\"name\":\"";
  payload += escapeJson(displayName);
  payload += "\",\"nodeId\":\"";
  payload += escapeJson(nodeId);
  payload += "\",\"nodeName\":\"";
  payload += escapeJson(nodeName);
  payload += "\",\"macAddress\":\"";
  payload += escapeJson(WiFi.softAPmacAddress());
  payload += "\",\"ssid\":\"";
  payload += escapeJson(apSsid);
  payload += "\",\"model\":\"";
  payload += escapeJson(balanceType);
  payload += "\",\"serialNumber\":\"";
  payload += escapeJson(balanceSerial);
  payload += "\",\"deviceId\":\"";
  payload += escapeJson(balanceId);
  payload += "\",\"brand\":\"";
  payload += escapeJson(activeBrand->id);
  payload += "\",\"brandName\":\"";
  payload += escapeJson(activeBrand->name);
  payload += "\",\"transport\":\"AtomS3 Wi-Fi\",\"ipAddress\":\"";
  payload += WiFi.softAPIP().toString();
  payload += "\",\"serial\":\"";
  payload += escapeJson(serialConfigLabel());
  payload += "\",\"streamActive\":";
  payload += isStreamActive() ? "true" : "false";
  payload += ",\"autoDetect\":";
  payload += autoDetectEnabled ? "true" : "false";
  payload += ",\"scanState\":\"";
  payload += escapeJson(scanStateLabel());
  payload += ",\"lastValue\":\"";
  payload += escapeJson(lastWeightLine);
  payload += "\"}";
  return payload;
}

void broadcastDeviceInfo() {
  String payload = buildDeviceInfoPayload();
  webSocket.broadcastTXT(payload);
  nextDeviceInfoBroadcastAt = millis() + DEVICE_INFO_BROADCAST_INTERVAL_MS;
}

void sendDeviceInfo(uint8_t clientId) {
  String payload = buildDeviceInfoPayload();
  webSocket.sendTXT(clientId, payload);
}

void broadcastDeviceInfoIfNeeded() {
  if (connectedClients == 0 || millis() < nextDeviceInfoBroadcastAt) {
    return;
  }
  if (balanceType == "-" && balanceSerial == "-" && balanceId == "-") {
    nextDeviceInfoBroadcastAt = millis() + DEVICE_INFO_BROADCAST_INTERVAL_MS;
    return;
  }
  broadcastDeviceInfo();
}

// ---------------------------------------------------------------------------
// WebSocket events
// ---------------------------------------------------------------------------

void onWebSocketEvent(uint8_t clientId, WStype_t type, uint8_t* payload, size_t length) {
  if (type == WStype_CONNECTED) {
    IPAddress ip = webSocket.remoteIP(clientId);
    connectedClients++;
    screenDirty = true;
    Serial.printf("Client %u connected: %s\n", clientId, ip.toString().c_str());
    webSocket.sendTXT(clientId, "ATOM_READY");
    sendDeviceInfo(clientId);
    return;
  }

  if (type == WStype_DISCONNECTED) {
    if (connectedClients > 0) {
      connectedClients--;
    }
    screenDirty = true;
    Serial.printf("Client %u disconnected\n", clientId);
    return;
  }

  if (type != WStype_TEXT) {
    return;
  }

  String message;
  for (size_t i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  message.trim();
  handleCommand(message, clientId);
}

// ---------------------------------------------------------------------------
// Command dispatch (app commands via JSON indexOf + admin prefix)
// ---------------------------------------------------------------------------

void handleCommand(const String& message, uint8_t clientId) {
  if (message.startsWith("admin:")) {
    handleAdminCommand(message.substring(6), clientId);
    return;
  }

  if (message.indexOf("clear-tare") >= 0) {
    if (strlen(activeBrand->cmdClearTare) > 0) {
      sendBalanceCommand(activeBrand->cmdClearTare);
    }
    return;
  }

  if (message.indexOf("tare") >= 0) {
    sendBalanceCommand(activeBrand->cmdTare);
    return;
  }

  if (message.indexOf("zero") >= 0) {
    sendBalanceCommand(activeBrand->cmdZero);
    return;
  }

  if (message.indexOf("request-weight") >= 0 || message.indexOf("weight") >= 0) {
    sendBalanceCommand(activeBrand->cmdWeight);
    return;
  }

  if (message.indexOf("print") >= 0) {
    sendBalanceCommand(activeBrand->cmdPrint);
    return;
  }

  if (message.indexOf("device-info") >= 0) {
    broadcastDeviceInfo();
    return;
  }

  if (message.indexOf("identify") >= 0) {
    startIdentityQueries();
    screenDirty = true;
    return;
  }

  Serial.print("Unknown WS command: ");
  Serial.println(message);
}

// ---------------------------------------------------------------------------
// Admin commands (WS: admin:xxx / Serial: xxx)
// ---------------------------------------------------------------------------

void handleAdminCommand(const String& command, uint8_t clientId) {
  if (command == "brand" || command == "brand:") {
    String response = "brand:" + String(activeBrand->id) + " (" + activeBrand->name + " " + serialConfigLabel() + ")";
    Serial.println(response);
    webSocket.sendTXT(clientId, response);
    return;
  }

  if (command.startsWith("brand:")) {
    String brandId = command.substring(6);
    brandId.trim();
    const BalanceProfile* p = findProfile(brandId);
    if (!p) {
      String response = "error:unknown brand '" + brandId + "'";
      Serial.println(response);
      webSocket.sendTXT(clientId, response);
      return;
    }
    applyBrand(p);
    String response = "ok:brand set to " + String(p->name) + " (" + serialConfigLabel() + ")";
    Serial.println(response);
    webSocket.sendTXT(clientId, response);
    return;
  }

  if (command == "brands") {
    String response = "brands:";
    for (uint8_t i = 0; i < PROFILE_COUNT; i++) {
      if (i > 0) response += ",";
      response += profiles[i].id;
    }
    Serial.println(response);
    webSocket.sendTXT(clientId, response);
    return;
  }

  if (command.startsWith("cmd:")) {
    String raw = command.substring(4);
    raw.trim();
    if (raw.length() > 0) {
      sendBalanceCommand(raw.c_str());
      String response = "ok:sent '" + raw + "'";
      Serial.println(response);
      webSocket.sendTXT(clientId, response);
    }
    return;
  }

  if (command == "config") {
    String response = "config:brand=" + String(activeBrand->id);
    response += ",name=" + String(activeBrand->name);
    response += ",nodeId=" + nodeId;
    response += ",nodeName=" + nodeName;
    response += ",ssid=" + apSsid;
    response += ",autoscan=" + String(autoDetectEnabled ? "on" : "off");
    response += ",serial=" + serialConfigLabel();
    response += ",tare=" + String(activeBrand->cmdTare);
    response += ",zero=" + String(activeBrand->cmdZero);
    response += ",weight=" + String(activeBrand->cmdWeight);
    response += ",print=" + String(activeBrand->cmdPrint);
    Serial.println(response);
    webSocket.sendTXT(clientId, response);
    return;
  }

  if (command == "scan") {
    startAutoDetect(true);
    webSocket.sendTXT(clientId, "ok:autoscan started");
    return;
  }

  if (command == "autoscan:on" || command == "autoscan:off") {
    autoDetectEnabled = command.endsWith(":on");
    saveNodeIdentity();
    webSocket.sendTXT(clientId, autoDetectEnabled ? "ok:autoscan on" : "ok:autoscan off");
    return;
  }

  if (command.startsWith("name:")) {
    String value = command.substring(5);
    value.trim();
    if (value.length() > 0) {
      nodeName = compactLine(value, 28);
      saveNodeIdentity();
      broadcastDeviceInfo();
      webSocket.sendTXT(clientId, "ok:name set");
    }
    return;
  }

  if (command.startsWith("ssid:")) {
    String value = command.substring(5);
    value.trim();
    if (value.length() > 0) {
      apSsid = compactLine(value, 31);
      saveNodeIdentity();
      webSocket.sendTXT(clientId, "ok:ssid saved, reboot required");
    }
    return;
  }

  if (command == "reboot") {
    String response = "ok:rebooting";
    Serial.println(response);
    webSocket.sendTXT(clientId, response);
    delay(300);
    ESP.restart();
    return;
  }

  String response = "error:unknown admin command '" + command + "'";
  Serial.println(response);
  webSocket.sendTXT(clientId, response);
}

// ---------------------------------------------------------------------------
// Serial admin (USB Serial Monitor)
// ---------------------------------------------------------------------------

void readSerialAdmin() {
  while (Serial.available()) {
    char c = (char)Serial.read();

    if (c == '\r' || c == '\n') {
      if (serialAdminBuffer.length() > 0) {
        handleSerialAdmin(serialAdminBuffer);
        serialAdminBuffer = "";
      }
      continue;
    }

    if (serialAdminBuffer.length() < 64) {
      serialAdminBuffer += c;
    }
  }
}

void handleSerialAdmin(const String& input) {
  String cmd = input;
  cmd.trim();

  if (cmd == "brand") {
    Serial.print("Brand: ");
    Serial.print(activeBrand->name);
    Serial.print(" (");
    Serial.print(activeBrand->id);
    Serial.print(") ");
    Serial.println(serialConfigLabel());
    return;
  }

  if (cmd.startsWith("brand ")) {
    String brandId = cmd.substring(6);
    brandId.trim();
    const BalanceProfile* p = findProfile(brandId);
    if (!p) {
      Serial.print("Unknown brand: ");
      Serial.println(brandId);
      Serial.print("Available: ");
      for (uint8_t i = 0; i < PROFILE_COUNT; i++) {
        if (i > 0) Serial.print(", ");
        Serial.print(profiles[i].id);
      }
      Serial.println();
      return;
    }
    applyBrand(p);
    Serial.print("Brand set to: ");
    Serial.print(p->name);
    Serial.print(" (");
    Serial.print(serialConfigLabel());
    Serial.println(")");
    return;
  }

  if (cmd == "brands") {
    Serial.println("Available brands:");
    for (uint8_t i = 0; i < PROFILE_COUNT; i++) {
      Serial.print("  ");
      Serial.print(profiles[i].id);
      Serial.print(" -> ");
      Serial.print(profiles[i].name);
      Serial.print(" (");
      Serial.print(profiles[i].baudRate);
      Serial.print(" ");
      switch (profiles[i].serialConfig) {
        case SERIAL_7E1: Serial.print("7E1"); break;
        case SERIAL_8O1: Serial.print("8O1"); break;
        case SERIAL_8N1: Serial.print("8N1"); break;
        default:         Serial.print("???"); break;
      }
      Serial.println(")");
    }
    return;
  }

  if (cmd.startsWith("cmd ")) {
    String raw = cmd.substring(4);
    raw.trim();
    if (raw.length() > 0) {
      sendBalanceCommand(raw.c_str());
      Serial.print("Sent: ");
      Serial.println(raw);
    }
    return;
  }

  if (cmd == "identify") {
    Serial.println("Starting one-shot identity query...");
    startIdentityQueries();
    return;
  }

  if (cmd == "config") {
    Serial.println("--- LabConnect Config ---");
    Serial.print("Node:    ");
    Serial.print(nodeName);
    Serial.print(" / ");
    Serial.println(nodeId);
    Serial.print("SSID:    ");
    Serial.println(apSsid);
    Serial.print("AutoScan:");
    Serial.println(autoDetectEnabled ? " on" : " off");
    Serial.print("Brand:   ");
    Serial.print(activeBrand->name);
    Serial.print(" (");
    Serial.print(activeBrand->id);
    Serial.println(")");
    Serial.print("Serial:  ");
    Serial.println(serialConfigLabel());
    Serial.print("Tare:    ");
    Serial.println(activeBrand->cmdTare);
    Serial.print("ClrTare: ");
    Serial.println(strlen(activeBrand->cmdClearTare) > 0 ? activeBrand->cmdClearTare : "(none)");
    Serial.print("Zero:    ");
    Serial.println(activeBrand->cmdZero);
    Serial.print("Weight:  ");
    Serial.println(activeBrand->cmdWeight);
    Serial.print("Print:   ");
    Serial.println(activeBrand->cmdPrint);
    Serial.print("Init:    ");
    Serial.println(strlen(activeBrand->cmdInit) > 0 ? activeBrand->cmdInit : "(none)");
    Serial.print("QType:   ");
    Serial.println(strlen(activeBrand->cmdQueryType) > 0 ? activeBrand->cmdQueryType : "(none)");
    Serial.print("QSerial: ");
    Serial.println(strlen(activeBrand->cmdQuerySerial) > 0 ? activeBrand->cmdQuerySerial : "(none)");
    Serial.print("QId:     ");
    Serial.println(strlen(activeBrand->cmdQueryId) > 0 ? activeBrand->cmdQueryId : "(none)");
    Serial.print("Clients: ");
    Serial.println(connectedClients);
    Serial.println("-------------------------");
    return;
  }

  if (cmd == "scan") {
    Serial.println("Starting serial auto-detect...");
    startAutoDetect(true);
    return;
  }

  if (cmd == "autoscan on" || cmd == "autoscan off") {
    autoDetectEnabled = cmd.endsWith("on");
    saveNodeIdentity();
    Serial.print("AutoScan ");
    Serial.println(autoDetectEnabled ? "enabled" : "disabled");
    return;
  }

  if (cmd.startsWith("name ")) {
    String value = cmd.substring(5);
    value.trim();
    if (value.length() > 0) {
      nodeName = compactLine(value, 28);
      saveNodeIdentity();
      Serial.print("Node name set to: ");
      Serial.println(nodeName);
      broadcastDeviceInfo();
    }
    return;
  }

  if (cmd.startsWith("ssid ")) {
    String value = cmd.substring(5);
    value.trim();
    if (value.length() > 0) {
      apSsid = compactLine(value, 31);
      saveNodeIdentity();
      Serial.print("SSID saved, reboot required: ");
      Serial.println(apSsid);
    }
    return;
  }

  if (cmd == "reboot") {
    Serial.println("Rebooting...");
    delay(200);
    ESP.restart();
    return;
  }

  if (cmd == "help") {
    Serial.println("Commands: brand, brand <id>, brands, config, identify, scan, autoscan on|off, name <label>, ssid <ssid>, cmd <raw>, reboot, help");
    return;
  }

  Serial.print("Unknown: ");
  Serial.println(cmd);
  Serial.println("Type 'help' for commands.");
}

// ---------------------------------------------------------------------------
// Balance serial write
// ---------------------------------------------------------------------------

void sendBalanceCommand(const char* command) {
  Serial.print("BALANCE < ");
  Serial.println(command);
  sendBalanceCommandWithEnding(command, END_CRLF);
}

void sendBalanceCommandWithEnding(const char* command, LineEnding ending) {
  if (!command || strlen(command) == 0) {
    return;
  }
  BalanceSerial.print(command);
  if (ending == END_CR || ending == END_CRLF) {
    BalanceSerial.write('\r');
  }
  if (ending == END_LF || ending == END_CRLF) {
    BalanceSerial.write('\n');
  }
  BalanceSerial.flush();
}

// ---------------------------------------------------------------------------
// Auto-detect serial profile
// ---------------------------------------------------------------------------

void startAutoDetect(bool force) {
  if (!force && !autoDetectEnabled) {
    return;
  }
  if (!force && lastStreamAt > 0 && millis() - lastStreamAt < STREAM_STALE_MS) {
    return;
  }

  Serial.println("AUTOSCAN started");
  pendingQuery = QUERY_NONE;
  identityRequested = false;
  scanProfileIndex = 0;
  scanAttempt = 0;
  scanState = SCAN_BOOT_WAIT;
  scanStateSince = millis();
  screenDirty = true;
  broadcastDeviceInfo();
}

void startAutoDetectIfNeeded() {
  if (!autoDetectEnabled || scanState != SCAN_OFF || millis() < nextAutoScanCheckAt) {
    return;
  }
  if (lastStreamAt > 0 && millis() - lastStreamAt < STREAM_STALE_MS) {
    nextAutoScanCheckAt = millis() + 4000;
    return;
  }
  startAutoDetect(false);
}

void runAutoDetect() {
  if (scanState == SCAN_OFF || scanState == SCAN_DONE || scanState == SCAN_FAILED) {
    return;
  }

  uint32_t now = millis();
  if (scanState == SCAN_BOOT_WAIT) {
    if (now - scanStateSince >= AUTOSCAN_BOOT_DELAY_MS) {
      scanState = SCAN_CONFIGURE;
      scanStateSince = now;
    }
    return;
  }

  if (scanProfileIndex >= SCAN_PROFILE_COUNT) {
    finishAutoDetectFailed();
    return;
  }

  const ScanProfile& profile = scanProfiles[scanProfileIndex];

  if (scanState == SCAN_CONFIGURE) {
    Serial.print("AUTOSCAN ");
    Serial.print(scanProfileIndex + 1);
    Serial.print("/");
    Serial.print(SCAN_PROFILE_COUNT);
    Serial.print(" ");
    Serial.println(profile.label);
    configureBalanceSerial(profile.baudRate, profile.serialConfig);
    scanState = SCAN_SETTLE;
    scanStateSince = now;
    screenDirty = true;
    return;
  }

  if (scanState == SCAN_SETTLE && now - scanStateSince >= AUTOSCAN_SETTLE_MS) {
    Serial.print("AUTOSCAN < ");
    Serial.println(printableCommand(profile.command));
    sendBalanceCommandWithEnding(profile.command, profile.ending);
    scanState = SCAN_WAIT_RESPONSE;
    scanStateSince = now;
    return;
  }

  if (scanState == SCAN_WAIT_RESPONSE && now - scanStateSince > profile.timeoutMs) {
    if (scanAttempt == 0) {
      scanAttempt = 1;
      scanState = SCAN_RETRY_DELAY;
      scanStateSince = now;
      return;
    }
    advanceScanProfile();
    return;
  }

  if (scanState == SCAN_RETRY_DELAY && now - scanStateSince >= AUTOSCAN_RETRY_DELAY_MS) {
    Serial.print("AUTOSCAN retry < ");
    Serial.println(printableCommand(profile.command));
    sendBalanceCommandWithEnding(profile.command, profile.ending);
    scanState = SCAN_WAIT_RESPONSE;
    scanStateSince = now;
  }
}

bool handleScanLine(const String& line) {
  if (scanState != SCAN_WAIT_RESPONSE) {
    return false;
  }

  const ScanProfile& profile = scanProfiles[scanProfileIndex];
  String detectedProfileId;
  uint8_t score = classifyWeightResponse(line, profile, detectedProfileId);

  Serial.print("AUTOSCAN > score=");
  Serial.print(score);
  Serial.print(" line=");
  Serial.println(line);

  if (score < 60) {
    return true;
  }

  const BalanceProfile* detected = findProfile(detectedProfileId.length() > 0 ? detectedProfileId : String(profile.profileId));
  if (!detected) {
    advanceScanProfile();
    return true;
  }

  finishAutoDetect(profile, detected, line, score);
  return true;
}

void advanceScanProfile() {
  scanProfileIndex++;
  scanAttempt = 0;
  if (scanProfileIndex >= SCAN_PROFILE_COUNT) {
    finishAutoDetectFailed();
    return;
  }
  scanState = SCAN_CONFIGURE;
  scanStateSince = millis();
}

void finishAutoDetect(const ScanProfile& scanProfile, const BalanceProfile* detected, const String& firstLine, uint8_t score) {
  Serial.print("AUTOSCAN detected ");
  Serial.print(detected->name);
  Serial.print(" score=");
  Serial.println(score);

  activeBrand = detected;
  saveBrand(detected);
  configureBalanceSerial(scanProfile.baudRate, scanProfile.serialConfig);

  lastWeightLine = compactLine(firstLine, 26);
  lastStreamAt = millis();
  scanState = SCAN_DONE;
  scanStateSince = millis();
  screenDirty = true;
  broadcastRawLine(firstLine);
  broadcastDeviceInfo();
  startIdentityQueries();
}

void finishAutoDetectFailed() {
  Serial.println("AUTOSCAN failed; keeping saved profile");
  scanState = SCAN_FAILED;
  scanStateSince = millis();
  configureBalanceSerial(activeBrand->baudRate, activeBrand->serialConfig);
  nextAutoScanCheckAt = millis() + 15000;
  screenDirty = true;
  broadcastDeviceInfo();
}

uint8_t classifyWeightResponse(const String& rawLine, const ScanProfile& expected, String& detectedProfileId) {
  String text = rawLine;
  text.trim();
  if (text.length() == 0) return 0;

  String upper = text;
  upper.toUpperCase();
  upper.replace("_", " ");

  if (upper.indexOf("ERR") >= 0 || upper == "OK") return 0;
  if (!containsWeightNumber(upper)) return 0;

  if ((upper.startsWith("ST,") || upper.startsWith("US,") || upper.startsWith("OL,")) &&
      (upper.indexOf(",GS,") >= 0 || upper.indexOf(",NT,") >= 0 || upper.indexOf(",G") >= 0)) {
    detectedProfileId = "and";
    return 96;
  }

  if (upper.startsWith("S S ") || upper.startsWith("S D ") || upper.startsWith("S I ") || upper.startsWith("SI ")) {
    detectedProfileId = "mettler";
    return 90;
  }

  if (upper.startsWith("W ")) {
    detectedProfileId = "kern";
    return 88;
  }

  if (String(expected.profileId) == "sartorius" || String(expected.profileId) == "ohaus" ||
      String(expected.profileId) == "dini" || String(expected.profileId) == "precisa" ||
      String(expected.profileId) == "shimadzu") {
    detectedProfileId = expected.profileId;
    return 76;
  }

  detectedProfileId = expected.profileId;
  return 62;
}

bool containsWeightNumber(const String& text) {
  bool hasDigit = false;
  for (uint16_t i = 0; i < text.length(); i++) {
    if (isDigit(text.charAt(i))) {
      hasDigit = true;
      break;
    }
  }
  if (!hasDigit) return false;
  return text.indexOf(" G") >= 0 || text.indexOf(",G") >= 0 ||
         text.indexOf("KG") >= 0 || text.indexOf(" MG") >= 0 ||
         text.indexOf("LB") >= 0 || text.indexOf("OZ") >= 0 ||
         text.indexOf(" CT") >= 0 || text.indexOf(" N") >= 0;
}

void detectBrandFromStream(const String& line) {
  String detectedProfileId;
  ScanProfile current = { activeBrand->id, activeBrand->baudRate, activeBrand->serialConfig, activeBrand->cmdWeight, END_CRLF, 500, activeBrand->name };
  uint8_t score = classifyWeightResponse(line, current, detectedProfileId);
  if (score < 85 || detectedProfileId.length() == 0 || detectedProfileId == activeBrand->id) {
    return;
  }

  const BalanceProfile* detected = findProfile(detectedProfileId);
  if (!detected) {
    return;
  }

  Serial.print("STREAM detected brand ");
  Serial.println(detected->name);
  activeBrand = detected;
  saveBrand(detected);
  broadcastDeviceInfo();
}

String printableCommand(const char* command) {
  String text;
  for (const char* p = command; p && *p; p++) {
    uint8_t c = (uint8_t)*p;
    if (c < 32 || c > 126) {
      char buffer[5];
      snprintf(buffer, sizeof(buffer), "\\x%02X", c);
      text += buffer;
    } else {
      text += (char)c;
    }
  }
  return text;
}

// ---------------------------------------------------------------------------
// Identity queries (skipped for brands with empty query commands)
// ---------------------------------------------------------------------------

void startIdentityQueries() {
  identityRequested = true;
  pendingQuery = QUERY_NONE;
  triedQueryType = false;
  triedQuerySerial = false;
  triedQueryId = false;
  nextIdentityQueryAt = millis();
}

void runIdentityQuery() {
  if (scanState == SCAN_BOOT_WAIT || scanState == SCAN_CONFIGURE || scanState == SCAN_SETTLE ||
      scanState == SCAN_WAIT_RESPONSE || scanState == SCAN_RETRY_DELAY) {
    return;
  }

  if (!identityRequested) {
    return;
  }

  if (pendingQuery != QUERY_NONE) {
    if (millis() - pendingQueryStartedAt > IDENTITY_QUERY_TIMEOUT_MS) {
      Serial.println("BALANCE ID timeout");
      pendingQuery = QUERY_NONE;
      nextIdentityQueryAt = millis() + IDENTITY_QUERY_DELAY_MS;
    }
    return;
  }

  if (millis() < nextIdentityQueryAt) {
    return;
  }

  if (!triedQueryType && balanceType == "-" && strlen(activeBrand->cmdQueryType) > 0) {
    sendIdentityQuery(QUERY_TYPE, activeBrand->cmdQueryType);
    return;
  }

  if (!triedQuerySerial && balanceSerial == "-" && strlen(activeBrand->cmdQuerySerial) > 0) {
    sendIdentityQuery(QUERY_SERIAL, activeBrand->cmdQuerySerial);
    return;
  }

  if (!triedQueryId && balanceId == "-" && strlen(activeBrand->cmdQueryId) > 0) {
    sendIdentityQuery(QUERY_ID, activeBrand->cmdQueryId);
    return;
  }

  identityRequested = false;
  broadcastDeviceInfo();
}

void sendIdentityQuery(IdentityQuery query, const char* command) {
  if (query == QUERY_TYPE) {
    triedQueryType = true;
  } else if (query == QUERY_SERIAL) {
    triedQuerySerial = true;
  } else if (query == QUERY_ID) {
    triedQueryId = true;
  }
  pendingQuery = query;
  pendingQueryStartedAt = millis();
  Serial.print("BALANCE ID < ");
  Serial.println(command);
  BalanceSerial.print(command);
  BalanceSerial.print("\r\n");
}

bool captureIdentityResponse(const String& rawLine) {
  if (pendingQuery == QUERY_NONE) {
    return false;
  }

  String value = rawLine;
  value.trim();
  if (value.length() == 0) {
    return false;
  }

  if (isLikelyWeightLine(value)) {
    return false;
  }

  value = cleanIdentityValue(pendingQuery, value);

  if (pendingQuery == QUERY_TYPE) {
    balanceType = compactLine(value, 18);
  } else if (pendingQuery == QUERY_SERIAL) {
    balanceSerial = compactLine(value, 18);
  } else if (pendingQuery == QUERY_ID) {
    balanceId = compactLine(value, 18);
  }

  Serial.print("BALANCE ID > ");
  Serial.println(value);
  pendingQuery = QUERY_NONE;
  nextIdentityQueryAt = millis() + IDENTITY_QUERY_DELAY_MS;
  broadcastDeviceInfo();
  return true;
}

String cleanIdentityValue(IdentityQuery query, const String& rawValue) {
  String value = rawValue;
  value.trim();

  String upper = value;
  upper.toUpperCase();

  const char* prefixes[] = {
    "TN,", "SN,", "ID,", "?TN,", "?SN,", "?ID,",
    "TN ", "SN ", "ID ", "?TN ", "?SN ", "?ID ",
    "TYPE:", "SERIAL:", "SERIAL NO.:", "S/N:", "ID:"
  };

  for (uint8_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
    String prefix = prefixes[i];
    if (upper.startsWith(prefix)) {
      value = value.substring(prefix.length());
      value.trim();
      break;
    }
  }

  if (query == QUERY_TYPE) {
    value.replace("A&D", "");
    value.trim();
  }

  return value;
}

// ---------------------------------------------------------------------------
// Weight line detection (multi-brand)
// ---------------------------------------------------------------------------

bool isLikelyWeightLine(const String& line) {
  String upper = line;
  upper.trim();
  upper.toUpperCase();
  if (upper.length() == 0) return false;

  // A&D: ST,+001248.52 g / US,+001248.48 g / SD,... / QT,...
  if (upper.startsWith("ST,") || upper.startsWith("ST ") ||
      upper.startsWith("US,") || upper.startsWith("US ") ||
      upper.startsWith("SD,") || upper.startsWith("SD ") ||
      upper.startsWith("QT,") || upper.startsWith("QT ")) return true;

  // MT-SICS (Mettler, Ohaus, Kern): S S / S D / S I + space
  if (upper.startsWith("S S ") || upper.startsWith("S D ") ||
      upper.startsWith("S I ")) return true;

  // Sartorius SBI: lines with leading spaces then sign+digits+unit
  if (upper.startsWith("N  ") || upper.startsWith("S  ")) return true;

  // Generic: ends with common weight unit
  if (upper.endsWith(" G") || upper.endsWith(" KG") || upper.endsWith(" MG") ||
      upper.endsWith(" LB") || upper.endsWith(" OZ") || upper.endsWith(" CT")) return true;

  return false;
}

// ---------------------------------------------------------------------------
// Utility
// ---------------------------------------------------------------------------

String escapeJson(const String& value) {
  String escaped;
  for (size_t i = 0; i < value.length(); i++) {
    char c = value.charAt(i);
    if (c == '"' || c == '\\') {
      escaped += '\\';
    }
    escaped += c;
  }
  return escaped;
}

String defaultNodeId() {
  uint64_t mac = ESP.getEfuseMac();
  uint16_t suffix = (uint16_t)(mac & 0xFFFF);
  char buffer[5];
  snprintf(buffer, sizeof(buffer), "%04X", suffix);
  return String(buffer);
}

bool isStreamActive() {
  return lastStreamAt > 0 && millis() - lastStreamAt < STREAM_STALE_MS;
}

bool isScanning() {
  return scanState == SCAN_BOOT_WAIT || scanState == SCAN_CONFIGURE || scanState == SCAN_SETTLE ||
         scanState == SCAN_WAIT_RESPONSE || scanState == SCAN_RETRY_DELAY;
}

String scanStateLabel() {
  switch (scanState) {
    case SCAN_OFF: return "off";
    case SCAN_BOOT_WAIT: return "boot-wait";
    case SCAN_CONFIGURE: return "configure";
    case SCAN_SETTLE: return "settle";
    case SCAN_WAIT_RESPONSE: return "wait-response";
    case SCAN_RETRY_DELAY: return "retry";
    case SCAN_DONE: return "done";
    case SCAN_FAILED: return "failed";
    default: return "unknown";
  }
}

void updateStreamState() {
  bool active = isStreamActive();
  if (streamWasActive == active) {
    return;
  }
  streamWasActive = active;
  screenDirty = true;
  if (!active) {
    broadcastDeviceInfo();
  }
}

String compactLine(const String& value, uint8_t maxLength) {
  String compact = value;
  compact.replace("\r", " ");
  compact.replace("\n", " ");
  compact.trim();
  while (compact.indexOf("  ") >= 0) {
    compact.replace("  ", " ");
  }
  if (compact.length() > maxLength) {
    compact = compact.substring(0, maxLength);
  }
  return compact;
}

// ---------------------------------------------------------------------------
// Screen
// ---------------------------------------------------------------------------

void initScreen() {
  AtomS3.begin(false);
  M5.Lcd.setRotation(0);
  M5.Lcd.setBrightness(96);
  screen.setColorDepth(8);
  screen.createSprite(128, 128);
  screen.setTextDatum(TL_DATUM);
  renderScreen(true);
}

void renderScreenIfNeeded() {
  if (!screenDirty && millis() < nextScreenRenderAt) {
    return;
  }
  if (millis() < nextScreenRenderAt) {
    return;
  }
  renderScreen(false);
}

void renderScreen(bool force) {
  (void)force;
  nextScreenRenderAt = millis() + SCREEN_RENDER_INTERVAL_MS;
  screenDirty = false;

  screen.fillSprite(TFT_BLACK);

  screen.fillRect(0, 0, 128, 18, TFT_DARKGREEN);
  screen.setTextColor(TFT_WHITE, TFT_DARKGREEN);
  screen.setTextSize(1);
  screen.drawString(compactLine(nodeName, 20), 4, 5);

  screen.setTextColor(TFT_WHITE, TFT_BLACK);
  screen.setTextSize(2);
  if (isScanning()) {
    screen.drawString("SCAN", 4, 24);
  } else {
    screen.drawString(compactLine(lastWeightLine, 12), 4, 24);
  }

  screen.setTextSize(1);
  screen.setTextColor(TFT_YELLOW, TFT_BLACK);
  String brandLine = String(activeBrand->name) + " " + serialConfigLabel();
  if (isScanning() && scanProfileIndex < SCAN_PROFILE_COUNT) {
    brandLine = String(scanProfiles[scanProfileIndex].label);
  }
  screen.drawString(compactLine(brandLine, 22), 4, 46);

  screen.setTextColor(TFT_CYAN, TFT_BLACK);
  screen.drawString(compactLine(apSsid, 22), 4, 58);

  screen.setTextColor(connectedClients > 0 ? TFT_GREEN : TFT_ORANGE, TFT_BLACK);
  String clientLine = String("APP ") + connectedClients + "  IP " + WiFi.softAPIP().toString();
  screen.drawString(compactLine(clientLine, 22), 4, 70);

  screen.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  screen.drawString("TYPE " + balanceType, 4, 84);
  screen.drawString("SN   " + balanceSerial, 4, 96);
  screen.drawString("ID   " + balanceId, 4, 108);

  if (isStreamActive()) {
    screen.fillCircle(119, 119, 4, TFT_GREEN);
  } else {
    screen.drawCircle(119, 119, 4, TFT_DARKGREY);
  }

  screen.pushSprite(0, 0);
}
