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

const char* AP_SSID = "LabConnect-Print";
const char* AP_PASSWORD = "labconnect";

IPAddress localIp(192, 168, 4, 1);
IPAddress gateway(192, 168, 4, 1);
IPAddress subnet(255, 255, 255, 0);

const uint8_t BALANCE_RX_PIN = 5;
const uint8_t BALANCE_TX_PIN = 6;

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

Preferences prefs;

// ---------------------------------------------------------------------------
// Hardware
// ---------------------------------------------------------------------------

HardwareSerial BalanceSerial(1);
WebSocketsServer webSocket(80, "/ws");

String lineBuffer;
String serialAdminBuffer;
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
uint8_t connectedClients = 0;
bool screenDirty = true;

const uint32_t IDENTITY_QUERY_DELAY_MS = 700;
const uint32_t IDENTITY_QUERY_TIMEOUT_MS = 1800;
const uint32_t SCREEN_RENDER_INTERVAL_MS = 250;
const uint32_t DEVICE_INFO_BROADCAST_INTERVAL_MS = 2500;

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
  prefs.end();

  const BalanceProfile* p = findProfile(id);
  activeBrand = p ? p : &profiles[0];
}

void saveBrand(const BalanceProfile* profile) {
  prefs.begin("labconnect", false);
  prefs.putString("brand", profile->id);
  prefs.end();
}

void applyBrand(const BalanceProfile* profile) {
  activeBrand = profile;
  saveBrand(profile);

  BalanceSerial.end();
  delay(50);
  BalanceSerial.begin(activeBrand->baudRate, activeBrand->serialConfig, BALANCE_RX_PIN, BALANCE_TX_PIN);

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
  nextIdentityQueryAt = millis() + 1200;
  screenDirty = true;

  if (connectedClients > 0) {
    broadcastDeviceInfo();
  }
}

// ---------------------------------------------------------------------------
// Setup & loop
// ---------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(300);

  initScreen();
  loadBrand();

  BalanceSerial.begin(activeBrand->baudRate, activeBrand->serialConfig, BALANCE_RX_PIN, BALANCE_TX_PIN);

  if (strlen(activeBrand->cmdInit) > 0) {
    delay(300);
    sendBalanceCommand(activeBrand->cmdInit);
  }

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(localIp, gateway, subnet);
  WiFi.softAP(AP_SSID, AP_PASSWORD);

  webSocket.begin();
  webSocket.onEvent(onWebSocketEvent);

  Serial.println("LabConnect AtomS3 bridge ready");
  Serial.print("Brand: ");
  Serial.print(activeBrand->name);
  Serial.print(" (");
  Serial.print(serialConfigLabel());
  Serial.println(")");
  Serial.print("AP: ");
  Serial.println(AP_SSID);
  Serial.println("WS: ws://192.168.4.1/ws");

  screenDirty = true;
  nextIdentityQueryAt = millis() + 1200;
}

void loop() {
  AtomS3.update();
  webSocket.loop();
  readBalance();
  readSerialAdmin();
  runIdentityQuery();
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

    if (lineBuffer.length() < 96) {
      lineBuffer += c;
    }
  }
}

void handleBalanceLine(const String& line) {
  if (captureIdentityResponse(line)) {
    screenDirty = true;
    return;
  }

  lastWeightLine = compactLine(line, 26);
  lastStreamAt = millis();
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
  String displayName = String(activeBrand->name) + " " + model;

  String payload = "{\"type\":\"device\",\"kind\":\"balance\",\"name\":\"";
  payload += escapeJson(displayName);
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
    pendingQuery = QUERY_NONE;
    nextIdentityQueryAt = millis();
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
    response += ",serial=" + serialConfigLabel();
    response += ",tare=" + String(activeBrand->cmdTare);
    response += ",zero=" + String(activeBrand->cmdZero);
    response += ",weight=" + String(activeBrand->cmdWeight);
    response += ",print=" + String(activeBrand->cmdPrint);
    Serial.println(response);
    webSocket.sendTXT(clientId, response);
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

  if (cmd == "config") {
    Serial.println("--- LabConnect Config ---");
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

  if (cmd == "reboot") {
    Serial.println("Rebooting...");
    delay(200);
    ESP.restart();
    return;
  }

  if (cmd == "help") {
    Serial.println("Commands: brand, brand <id>, brands, config, cmd <raw>, reboot, help");
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
  BalanceSerial.print(command);
  BalanceSerial.print("\r\n");
}

// ---------------------------------------------------------------------------
// Identity queries (skipped for brands with empty query commands)
// ---------------------------------------------------------------------------

void runIdentityQuery() {
  if (pendingQuery != QUERY_NONE) {
    if (millis() - pendingQueryStartedAt > IDENTITY_QUERY_TIMEOUT_MS) {
      pendingQuery = QUERY_NONE;
      nextIdentityQueryAt = millis() + IDENTITY_QUERY_DELAY_MS;
    }
    return;
  }

  if (millis() < nextIdentityQueryAt) {
    return;
  }

  if (balanceType == "-" && strlen(activeBrand->cmdQueryType) > 0) {
    sendIdentityQuery(QUERY_TYPE, activeBrand->cmdQueryType);
    return;
  }

  if (balanceSerial == "-" && strlen(activeBrand->cmdQuerySerial) > 0) {
    sendIdentityQuery(QUERY_SERIAL, activeBrand->cmdQuerySerial);
    return;
  }

  if (balanceId == "-" && strlen(activeBrand->cmdQueryId) > 0) {
    sendIdentityQuery(QUERY_ID, activeBrand->cmdQueryId);
  }
}

void sendIdentityQuery(IdentityQuery query, const char* command) {
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
  screen.drawString("LabConnect Print", 4, 5);

  screen.setTextColor(TFT_WHITE, TFT_BLACK);
  screen.setTextSize(2);
  screen.drawString(compactLine(lastWeightLine, 12), 4, 24);

  screen.setTextSize(1);
  screen.setTextColor(TFT_YELLOW, TFT_BLACK);
  String brandLine = String(activeBrand->name) + " " + serialConfigLabel();
  screen.drawString(compactLine(brandLine, 22), 4, 46);

  screen.setTextColor(TFT_CYAN, TFT_BLACK);
  screen.drawString("IP " + WiFi.softAPIP().toString(), 4, 58);

  screen.setTextColor(connectedClients > 0 ? TFT_GREEN : TFT_ORANGE, TFT_BLACK);
  screen.drawString("SUNMI " + String(connectedClients), 4, 70);

  screen.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  screen.drawString("TYPE " + balanceType, 4, 84);
  screen.drawString("SN   " + balanceSerial, 4, 96);
  screen.drawString("ID   " + balanceId, 4, 108);

  if (millis() - lastStreamAt < 1500) {
    screen.fillCircle(119, 119, 4, TFT_GREEN);
  } else {
    screen.drawCircle(119, 119, 4, TFT_DARKGREY);
  }

  screen.pushSprite(0, 0);
}
