// LabConnect Hub - Sunmi V3
// AtomS3 BLE node: configurable RS232 balance bridge for the Sunmi app.

#include <Arduino.h>
#include <ArduinoJson.h>
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <M5Unified.h>
#include <Preferences.h>

static const char *SERVICE_UUID = "6f7d0001-6c63-4c42-4855-422d53554e4d";
static const char *VALUE_UUID = "6f7d0002-6c63-4c42-4855-422d53554e4d";
static const char *COMMAND_UUID = "6f7d0003-6c63-4c42-4855-422d53554e4d";
static const char *INFO_UUID = "6f7d0004-6c63-4c42-4855-422d53554e4d";

#define FW_VARIANT_CODE "ATOM_BLE_GENERIC"
#define SCREEN_W 128
#define SCREEN_H 128

static const uint16_t COLOR_BG = 0xF7BE;
static const uint16_t COLOR_SURFACE = 0xFFFF;
static const uint16_t COLOR_TEXT = 0x0000;
static const uint16_t COLOR_TEXT_DIM = 0x8C92;
static const uint16_t COLOR_PRIMARY = 0x041F;
static const uint16_t COLOR_SUCCESS = 0x36E5;
static const uint16_t COLOR_WARNING = 0xFCA0;

enum Brand : uint8_t {
  BRAND_AD = 0,
  BRAND_METTLER = 1,
  BRAND_UNKNOWN = 255
};

enum LineEnding : uint8_t {
  END_CR = 0,
  END_LF = 1,
  END_CRLF = 2
};

struct NodeConfig {
  char name[32] = "Node BLE";
  char typeName[32] = "";
  char balanceId[24] = "?";
  char balanceSerial[24] = "?";
  uint8_t brand = BRAND_AD;
  uint32_t baud = 2400;
  uint8_t parity = 1;       // 0 none, 1 even, 2 odd
  uint8_t dataBits = 7;
  uint8_t stopBits = 1;
  uint8_t rxPin = 5;
  uint8_t txPin = 6;
  bool swapRxTx = false;
  char pollCmd[16] = "Q";
  char zeroCmd[16] = "Z";
  uint16_t lineTimeout = 300;
  LineEnding ending = END_CRLF;
};

HardwareSerial BalanceSerial(1);
Preferences prefs;
M5Canvas sprite(&M5.Display);

BLEServer *bleServer = nullptr;
BLECharacteristic *valueChar = nullptr;
BLECharacteristic *infoChar = nullptr;

NodeConfig cfg;
String lineBuffer;
String latestValue;
String screenValue;
String pendingCmd;
uint32_t lastByteAt = 0;
uint32_t pendingCmdAt = 0;
bool bleConnected = false;
bool screenDirty = true;
uint32_t lastScreenAt = 0;
uint32_t lastCommandFlashAt = 0;
bool commandFlashVisible = false;
bool autoIdentityQueued = true;
uint32_t autoIdentityAt = 0;

static const uint32_t ID_TIMEOUT_MS = 1800;
static const uint32_t SCREEN_REFRESH_MS = 120;
static const uint32_t AUTO_IDENTITY_DELAY_MS = 1200;

const char *brandName(uint8_t brand) {
  switch (brand) {
    case BRAND_AD: return "A&D";
    case BRAND_METTLER: return "Mettler";
    default: return "?";
  }
}

bool isIdentityCommand(const String &command) {
  return command == "?TN" || command == "?SN" || command == "?ID" || command == "I10";
}

bool isLikelyWeightLine(const String &line) {
  String upper = line;
  upper.toUpperCase();
  upper.trim();
  return upper.startsWith("ST,") ||
         upper.startsWith("US,") ||
         upper.startsWith("OL,") ||
         upper.startsWith("S ") ||
         upper.indexOf(" G") >= 0 ||
         upper.endsWith("G");
}

bool isBalanceCommandError(const String &line) {
  String upper = line;
  upper.toUpperCase();
  upper.trim();
  return upper.startsWith("EC") ||
         upper == "ES" ||
         upper == "ET" ||
         upper == "EL";
}

String printableText(const char *raw, const char *fallback = "-") {
  if (!raw || !raw[0]) return String(fallback);
  String out;
  for (size_t i = 0; raw[i] && out.length() < 18; i++) {
    uint8_t c = (uint8_t)raw[i];
    if (c >= 32 && c <= 126) out += (char)c;
  }
  out.trim();
  return out.length() ? out : String(fallback);
}

String printableLine(const String &raw, const char *fallback = "----") {
  String out;
  for (size_t i = 0; i < raw.length() && out.length() < 44; i++) {
    uint8_t c = (uint8_t)raw.charAt(i);
    if (c >= 32 && c <= 126) out += (char)c;
  }
  out.trim();
  return out.length() ? out : String(fallback);
}

String chipSuffix() {
  uint64_t mac = ESP.getEfuseMac();
  char out[7];
  snprintf(out, sizeof(out), "%06X", (uint32_t)(mac & 0xFFFFFF));
  return String(out);
}

String defaultBleName() {
  return "LC-Atom-" + chipSuffix().substring(2);
}

uint32_t serialConfig() {
  const bool twoStops = cfg.stopBits == 2;
  if (cfg.dataBits == 7) {
    if (cfg.parity == 1) return twoStops ? SERIAL_7E2 : SERIAL_7E1;
    if (cfg.parity == 2) return twoStops ? SERIAL_7O2 : SERIAL_7O1;
    return twoStops ? SERIAL_7N2 : SERIAL_7N1;
  }
  if (cfg.parity == 1) return twoStops ? SERIAL_8E2 : SERIAL_8E1;
  if (cfg.parity == 2) return twoStops ? SERIAL_8O2 : SERIAL_8O1;
  return twoStops ? SERIAL_8N2 : SERIAL_8N1;
}

void compactLine(const char *raw, char *out, size_t outSize) {
  if (!out || outSize == 0) return;
  out[0] = 0;
  if (!raw) return;

  bool prevSpace = true;
  size_t o = 0;
  for (size_t i = 0; raw[i] && o < outSize - 1; i++) {
    char c = raw[i];
    if (c == '\r' || c == '\n' || c == '\t') c = ' ';
    if (c == ' ') {
      if (!prevSpace && o > 0) out[o++] = ' ';
      prevSpace = true;
    } else {
      out[o++] = c;
      prevSpace = false;
    }
  }
  while (o > 0 && out[o - 1] == ' ') o--;
  out[o] = 0;
  if (!out[0]) strlcpy(out, "?", outSize);
}

bool isAllZeroIdentity(const char *value) {
  if (!value || !*value) return false;
  bool hasDigit = false;
  for (size_t i = 0; value[i]; i++) {
    char c = value[i];
    if (c == ' ' || c == '-' || c == '_' || c == '.') continue;
    if (c != '0') return false;
    hasDigit = true;
  }
  return hasDigit;
}

bool extractAdIdentity(const char *raw, const char *prefix, char *out, size_t outSize) {
  if (!raw || !prefix || !out || outSize == 0) return false;
  char compact[96];
  compactLine(raw, compact, sizeof(compact));
  size_t prefixLen = strlen(prefix);
  if (strncasecmp(compact, prefix, prefixLen) != 0) return false;
  const char *value = compact + prefixLen;
  while (*value == ',' || *value == ':' || *value == '=' || *value == ' ') value++;
  if (!*value) return false;
  strlcpy(out, value, outSize);
  return true;
}

bool extractQuotedIdentity(const char *raw, char *out, size_t outSize) {
  if (!raw || !out || outSize == 0) return false;
  const char *start = strchr(raw, '"');
  if (!start) return false;
  start++;
  const char *end = strchr(start, '"');
  if (!end || end <= start) return false;
  size_t len = (size_t)(end - start);
  if (len >= outSize) len = outSize - 1;
  memcpy(out, start, len);
  out[len] = 0;
  return len > 0;
}

bool extractSicsI10(const char *raw, char *out, size_t outSize) {
  if (!raw || !out || outSize == 0) return false;
  char compact[96];
  compactLine(raw, compact, sizeof(compact));
  for (size_t i = 0; compact[i]; i++) {
    if (compact[i] >= 'a' && compact[i] <= 'z') compact[i] = (char)(compact[i] - 'a' + 'A');
  }
  if (strncmp(compact, "I10_A", 5) != 0) return false;
  return extractQuotedIdentity(raw, out, outSize);
}

bool extractClientBalanceId(const char *raw, char *out, size_t outSize) {
  if (!raw || !out || outSize < 3) return false;
  char compact[64];
  compactLine(raw, compact, sizeof(compact));
  for (size_t i = 0; compact[i]; i++) {
    if (compact[i] >= 'a' && compact[i] <= 'z') compact[i] = (char)(compact[i] - 'a' + 'A');
  }

  const char *search = strstr(compact, "CDO");
  if (!search) search = compact;
  for (size_t i = 0; search[i] && search[i + 1]; i++) {
    if (isDigit(search[i]) && isDigit(search[i + 1])) {
      out[0] = search[i];
      out[1] = search[i + 1];
      out[2] = 0;
      return true;
    }
  }
  return false;
}

String cdoLabel() {
  char id[24];
  if (extractClientBalanceId(cfg.balanceId, id, sizeof(id))) {
    return "CDO " + String(id);
  }
  return "CDO ?";
}

void markScreenDirty() {
  screenDirty = true;
}

void drawCenteredFitted(const String &text, int cx, int cy, int maxWidth, uint16_t color) {
  sprite.setTextDatum(middle_center);
  sprite.setTextColor(color);

  sprite.setFont(&fonts::FreeSansBold12pt7b);
  if (sprite.textWidth(text) <= maxWidth) {
    sprite.drawString(text, cx, cy);
    sprite.setTextDatum(top_left);
    return;
  }

  sprite.setFont(&fonts::FreeSansBold9pt7b);
  if (sprite.textWidth(text) <= maxWidth) {
    sprite.drawString(text, cx, cy);
    sprite.setTextDatum(top_left);
    return;
  }

  sprite.setFont(&fonts::FreeSans9pt7b);
  sprite.drawString(text, cx, cy);
  sprite.setTextDatum(top_left);
}

String serialSummary() {
  uint8_t dataBits = (cfg.dataBits == 7 || cfg.dataBits == 8) ? cfg.dataBits : 8;
  uint8_t stopBits = (cfg.stopBits == 2) ? 2 : 1;
  uint8_t parity = cfg.parity <= 2 ? cfg.parity : 0;
  return String(cfg.baud) + "/" + String(dataBits) +
         (parity == 1 ? "E" : parity == 2 ? "O" : "N") +
         String(stopBits);
}

void drawScreen() {
  sprite.fillSprite(COLOR_BG);
  sprite.setTextDatum(top_left);

  sprite.fillRoundRect(4, 4, SCREEN_W - 8, 24, 7, COLOR_SURFACE);
  sprite.setFont(&fonts::FreeSansBold12pt7b);
  sprite.setTextColor(COLOR_TEXT);
  sprite.setTextDatum(middle_center);
  sprite.drawString(cdoLabel(), SCREEN_W / 2, 17);
  sprite.fillCircle(SCREEN_W - 13, 16, 4, bleConnected ? COLOR_SUCCESS : COLOR_WARNING);

  sprite.fillRoundRect(4, 34, SCREEN_W - 8, 58, 8, COLOR_SURFACE);
  if (millis() - lastCommandFlashAt < 450) {
    sprite.fillRoundRect(6, 36, SCREEN_W - 12, 54, 7, 0xE71F);
  }
  drawCenteredFitted(screenValue.length() ? screenValue : String("----"),
                     SCREEN_W / 2, 63, SCREEN_W - 16,
                     screenValue.length() ? COLOR_TEXT : COLOR_TEXT_DIM);

  sprite.fillRoundRect(4, 96, SCREEN_W - 8, 28, 7, COLOR_SURFACE);
  sprite.setFont(&fonts::FreeSans9pt7b);
  sprite.setTextColor(COLOR_TEXT_DIM);

  sprite.setTextDatum(middle_center);
  String type = printableText(cfg.typeName);
  if (type.length() > 16) type = type.substring(0, 16);
  sprite.drawString(type, SCREEN_W / 2, 104);

  sprite.setTextDatum(top_left);
  String brand = String(brandName(cfg.brand));
  if (brand.length() > 8) brand = brand.substring(0, 8);
  sprite.drawString(brand, 10, 113);
  sprite.setTextDatum(top_right);
  String status = bleConnected ? "APP" : "NO APP";
  sprite.drawString(status, SCREEN_W - 10, 113);

  sprite.pushSprite(0, 0);
}

void refreshScreenIfNeeded() {
  bool shouldFlash = millis() - lastCommandFlashAt < 450;
  if (shouldFlash != commandFlashVisible) {
    commandFlashVisible = shouldFlash;
    screenDirty = true;
  }
  if (!screenDirty) return;
  if (millis() - lastScreenAt < SCREEN_REFRESH_MS) return;
  lastScreenAt = millis();
  screenDirty = false;
  drawScreen();
}

void applyClientBrandMapping() {
  char id[8];
  if (!extractClientBalanceId(cfg.balanceId, id, sizeof(id))) return;
  if (!strcmp(id, "02") || !strcmp(id, "03") || !strcmp(id, "05")) cfg.brand = BRAND_AD;
  if (!strcmp(id, "04") || !strcmp(id, "06")) cfg.brand = BRAND_METTLER;
}

void configureBalanceSerial() {
  int rx = cfg.swapRxTx ? cfg.txPin : cfg.rxPin;
  int tx = cfg.swapRxTx ? cfg.rxPin : cfg.txPin;
  BalanceSerial.end();
  delay(3);
  BalanceSerial.begin(cfg.baud, serialConfig(), rx, tx);
  while (BalanceSerial.available()) BalanceSerial.read();
  lineBuffer = "";
  Serial.printf("RS232 %lu/%u%c%u RX=%d TX=%d\n",
                (unsigned long)cfg.baud,
                cfg.dataBits,
                cfg.parity == 1 ? 'E' : cfg.parity == 2 ? 'O' : 'N',
                cfg.stopBits,
                rx,
                tx);
  markScreenDirty();
}

void loadConfig() {
  prefs.begin("sunmi-node", true);
  prefs.getString("name", cfg.name, sizeof(cfg.name));
  prefs.getString("type", cfg.typeName, sizeof(cfg.typeName));
  prefs.getString("bid", cfg.balanceId, sizeof(cfg.balanceId));
  prefs.getString("sn", cfg.balanceSerial, sizeof(cfg.balanceSerial));
  cfg.brand = prefs.getUChar("brand", cfg.brand);
  cfg.baud = prefs.getULong("baud", cfg.baud);
  cfg.parity = prefs.getUChar("parity", cfg.parity);
  cfg.dataBits = prefs.getUChar("dbits", cfg.dataBits);
  cfg.stopBits = prefs.getUChar("sbits", cfg.stopBits);
  cfg.rxPin = prefs.getUChar("rxpin", cfg.rxPin);
  cfg.txPin = prefs.getUChar("txpin", cfg.txPin);
  cfg.swapRxTx = prefs.getBool("swap", cfg.swapRxTx);
  prefs.getString("poll", cfg.pollCmd, sizeof(cfg.pollCmd));
  prefs.getString("zero", cfg.zeroCmd, sizeof(cfg.zeroCmd));
  cfg.lineTimeout = prefs.getUShort("timeout", cfg.lineTimeout);
  cfg.ending = (LineEnding)prefs.getUChar("ending", cfg.ending);
  prefs.end();

  if (!cfg.name[0]) strlcpy(cfg.name, defaultBleName().c_str(), sizeof(cfg.name));
  if (!cfg.pollCmd[0]) strlcpy(cfg.pollCmd, cfg.brand == BRAND_METTLER ? "SI" : "Q", sizeof(cfg.pollCmd));
  if (!cfg.zeroCmd[0]) strlcpy(cfg.zeroCmd, cfg.brand == BRAND_METTLER ? "Z" : "Z", sizeof(cfg.zeroCmd));
}

void saveConfig() {
  prefs.begin("sunmi-node", false);
  prefs.putString("name", cfg.name);
  prefs.putString("type", cfg.typeName);
  prefs.putString("bid", cfg.balanceId);
  prefs.putString("sn", cfg.balanceSerial);
  prefs.putUChar("brand", cfg.brand);
  prefs.putULong("baud", cfg.baud);
  prefs.putUChar("parity", cfg.parity);
  prefs.putUChar("dbits", cfg.dataBits);
  prefs.putUChar("sbits", cfg.stopBits);
  prefs.putUChar("rxpin", cfg.rxPin);
  prefs.putUChar("txpin", cfg.txPin);
  prefs.putBool("swap", cfg.swapRxTx);
  prefs.putString("poll", cfg.pollCmd);
  prefs.putString("zero", cfg.zeroCmd);
  prefs.putUShort("timeout", cfg.lineTimeout);
  prefs.putUChar("ending", cfg.ending);
  prefs.end();
}

void purgeConfig() {
  prefs.begin("sunmi-node", false);
  prefs.clear();
  prefs.end();
}

String compactInfoPayload() {
  String payload;
  payload.reserve(180);
  payload += "fw=" FW_VARIANT_CODE;
  payload += ";name=" + String(cfg.name);
  payload += ";id=" + cdoLabel();
  payload += ";rawid=" + String(cfg.balanceId);
  payload += ";type=" + String(cfg.typeName[0] ? cfg.typeName : "?");
  payload += ";brand=" + String(brandName(cfg.brand));
  payload += ";sn=" + String(cfg.balanceSerial[0] ? cfg.balanceSerial : "?");
  payload += ";baud=" + String(cfg.baud);
  payload += ";ser=" + String(cfg.dataBits) + (cfg.parity == 1 ? "E" : cfg.parity == 2 ? "O" : "N") + String(cfg.stopBits);
  payload += ";last=" + latestValue;
  return payload;
}

void notifyInfo() {
  if (!infoChar) return;
  String payload = compactInfoPayload();
  infoChar->setValue(payload.c_str());
  if (bleConnected) infoChar->notify();
  markScreenDirty();
}

void notifyValue(const String &line) {
  latestValue = line;
  screenValue = printableLine(line);
  if (valueChar && bleConnected && line.length() > 0) {
    valueChar->setValue(line.c_str());
    valueChar->notify();
  }
  notifyInfo();
  markScreenDirty();
}

void writeBalanceCommand(const String &command) {
  if (command.length() == 0) return;
  BalanceSerial.print(command);
  if (cfg.ending == END_CR || cfg.ending == END_CRLF) BalanceSerial.write('\r');
  if (cfg.ending == END_LF || cfg.ending == END_CRLF) BalanceSerial.write('\n');
  BalanceSerial.flush();
}

void startBalanceCommand(String command, const char *origin = "ATOM") {
  command.trim();
  if (command.length() == 0) return;
  pendingCmd = command;
  pendingCmdAt = millis();
  lastCommandFlashAt = millis();
  markScreenDirty();
  Serial.print(origin);
  Serial.print(" CMD < ");
  Serial.println(command);
  writeBalanceCommand(command);
}

void setBrandDefaults(uint8_t brand) {
  cfg.brand = brand;
  if (brand == BRAND_METTLER) {
    cfg.baud = 9600;
    cfg.parity = 0;
    cfg.dataBits = 8;
    cfg.stopBits = 1;
    cfg.ending = END_CRLF;
    strlcpy(cfg.pollCmd, "SI", sizeof(cfg.pollCmd));
  } else {
    cfg.baud = 2400;
    cfg.parity = 1;
    cfg.dataBits = 7;
    cfg.stopBits = 1;
    cfg.ending = END_CRLF;
    strlcpy(cfg.pollCmd, "Q", sizeof(cfg.pollCmd));
  }
}

void applyConfigJson(const String &json) {
  StaticJsonDocument<384> doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    notifyValue(String("ERR config json: ") + err.c_str());
    return;
  }

  if (doc.containsKey("brand")) {
    String brand = doc["brand"].as<String>();
    brand.toLowerCase();
    if (brand.indexOf("mettler") >= 0) setBrandDefaults(BRAND_METTLER);
    else if (brand.indexOf("a&d") >= 0 || brand.indexOf("and") >= 0 || brand.indexOf("ad") >= 0) setBrandDefaults(BRAND_AD);
  }
  if (doc.containsKey("br")) setBrandDefaults(doc["br"].as<uint8_t>());
  if (doc.containsKey("name")) strlcpy(cfg.name, doc["name"].as<const char *>(), sizeof(cfg.name));
  if (doc.containsKey("type")) strlcpy(cfg.typeName, doc["type"].as<const char *>(), sizeof(cfg.typeName));
  if (doc.containsKey("id")) strlcpy(cfg.balanceId, doc["id"].as<const char *>(), sizeof(cfg.balanceId));
  if (doc.containsKey("sn")) strlcpy(cfg.balanceSerial, doc["sn"].as<const char *>(), sizeof(cfg.balanceSerial));
  if (doc.containsKey("baud")) cfg.baud = doc["baud"].as<uint32_t>();
  if (doc.containsKey("parity")) cfg.parity = doc["parity"].as<uint8_t>();
  if (doc.containsKey("dbits")) cfg.dataBits = doc["dbits"].as<uint8_t>();
  if (doc.containsKey("sbits")) cfg.stopBits = doc["sbits"].as<uint8_t>();
  if (doc.containsKey("rx")) cfg.rxPin = doc["rx"].as<uint8_t>();
  if (doc.containsKey("tx")) cfg.txPin = doc["tx"].as<uint8_t>();
  if (doc.containsKey("swap")) cfg.swapRxTx = doc["swap"].as<int>() != 0;
  if (doc.containsKey("poll")) strlcpy(cfg.pollCmd, doc["poll"].as<const char *>(), sizeof(cfg.pollCmd));
  if (doc.containsKey("zero")) strlcpy(cfg.zeroCmd, doc["zero"].as<const char *>(), sizeof(cfg.zeroCmd));
  if (doc.containsKey("timeout")) cfg.lineTimeout = doc["timeout"].as<uint16_t>();
  if (doc.containsKey("ending")) cfg.ending = (LineEnding)doc["ending"].as<uint8_t>();

  applyClientBrandMapping();
  saveConfig();
  configureBalanceSerial();
  notifyValue("CFG OK");
  markScreenDirty();
}

void handleWriteId(String wantedId) {
  wantedId.trim();
  if (wantedId.length() == 0) return;
  wantedId.replace("\r", "");
  wantedId.replace("\n", "");
  strlcpy(cfg.balanceId, wantedId.c_str(), sizeof(cfg.balanceId));
  applyClientBrandMapping();
  saveConfig();

  if (cfg.brand == BRAND_METTLER) {
    startBalanceCommand("I10 \"" + wantedId + "\"", "SUNMI");
  } else {
    notifyValue("ID saved locally; A&D write command not configured");
  }
}

void handleControlCommand(String command) {
  if (command == "LC:INFO") {
    notifyInfo();
  } else if (command == "LC:READ_ID") {
    startBalanceCommand(cfg.brand == BRAND_METTLER ? "I10" : "?ID", "SUNMI");
  } else if (command == "LC:READ_ALL") {
    if (cfg.brand == BRAND_METTLER) startBalanceCommand("I10", "SUNMI");
    else startBalanceCommand("?TN", "SUNMI");
  } else if (command.startsWith("LC:WRITE_ID:")) {
    handleWriteId(command.substring(strlen("LC:WRITE_ID:")));
  } else if (command.startsWith("LC:CFG:")) {
    applyConfigJson(command.substring(strlen("LC:CFG:")));
  } else if (command == "LC:PURGE") {
    notifyValue("PURGE OK");
    delay(100);
    purgeConfig();
    ESP.restart();
  } else if (command == "LC:RESTART") {
    notifyValue("RESTART");
    delay(100);
    ESP.restart();
  } else {
    notifyValue("ERR unknown LC command");
  }
}

class HubCommandCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *characteristic) override {
    String command = characteristic->getValue().c_str();
    command.replace("\r", "");
    command.replace("\n", "");
    command.trim();
    if (command.startsWith("LC:")) handleControlCommand(command);
    else startBalanceCommand(command, "SUNMI");
  }
};

class HubServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *server) override {
    bleConnected = true;
    Serial.println("BLE connected");
    notifyInfo();
    markScreenDirty();
  }

  void onDisconnect(BLEServer *server) override {
    bleConnected = false;
    Serial.println("BLE disconnected");
    markScreenDirty();
    delay(100);
    server->startAdvertising();
  }
};

bool updateIdentityFromLine(const char *line) {
  if (!line || pendingCmd.length() == 0) return false;

  char value[64];
  if (pendingCmd == "?TN" && extractAdIdentity(line, "TN", value, sizeof(value))) {
    strlcpy(cfg.typeName, value, sizeof(cfg.typeName));
  } else if (pendingCmd == "?SN" && extractAdIdentity(line, "SN", value, sizeof(value))) {
    strlcpy(cfg.balanceSerial, value, sizeof(cfg.balanceSerial));
  } else if (pendingCmd == "?ID" && extractAdIdentity(line, "ID", value, sizeof(value))) {
    if (isAllZeroIdentity(value)) strlcpy(cfg.balanceId, "?", sizeof(cfg.balanceId));
    else if (extractClientBalanceId(value, cfg.balanceId, sizeof(cfg.balanceId))) {}
    else strlcpy(cfg.balanceId, value, sizeof(cfg.balanceId));
  } else if (pendingCmd == "I10" && extractSicsI10(line, value, sizeof(value))) {
    strlcpy(cfg.balanceId, value, sizeof(cfg.balanceId));
  } else if (pendingCmd.startsWith("I10 \"")) {
    // A successful Mettler write is often acknowledged; the requested ID is already saved.
  } else {
    return false;
  }

  applyClientBrandMapping();
  saveConfig();
  notifyInfo();
  return true;
}

void continueReadAllSequence() {
  if (pendingCmd == "?TN") {
    startBalanceCommand("?SN", "ATOM");
  } else if (pendingCmd == "?SN") {
    startBalanceCommand("?ID", "ATOM");
  } else {
    pendingCmd = "";
  }
}

void onBalanceLine(const String &line) {
  if (line.length() == 0) return;
  Serial.print("BALANCE > ");
  Serial.println(line);

  String commandBeforeLine = pendingCmd;
  bool identityHandled = updateIdentityFromLine(line.c_str());
  notifyValue(line);

  if (identityHandled) {
    if (commandBeforeLine == "?TN" || commandBeforeLine == "?SN") continueReadAllSequence();
    else pendingCmd = "";
  } else if (isIdentityCommand(pendingCmd) && isBalanceCommandError(line)) {
    Serial.print("IDENTITY CMD ERROR ");
    Serial.println(pendingCmd);
    if (pendingCmd == "?TN" || pendingCmd == "?SN") continueReadAllSequence();
    else pendingCmd = "";
  } else if (isIdentityCommand(pendingCmd) && isLikelyWeightLine(line)) {
    // Balance en stream : on garde ?TN/?SN/?ID/I10 en attente jusqu'a la vraie reponse.
  } else if (!isIdentityCommand(pendingCmd)) {
    pendingCmd = "";
  }
}

void readBalanceNonBlocking() {
  while (BalanceSerial.available()) {
    char c = (char)BalanceSerial.read();
    lastByteAt = millis();

    if (c == '\r' || c == '\n') {
      if (lineBuffer.length() > 0) {
        onBalanceLine(lineBuffer);
        lineBuffer = "";
      }
    } else if (lineBuffer.length() < 180) {
      lineBuffer += c;
    }
  }

  if (lineBuffer.length() > 0 && millis() - lastByteAt > cfg.lineTimeout) {
    onBalanceLine(lineBuffer);
    lineBuffer = "";
  }
}

void runPendingTimeout() {
  if (pendingCmd.length() == 0) return;
  if (millis() - pendingCmdAt <= ID_TIMEOUT_MS) return;

  Serial.print("CMD TIMEOUT ");
  Serial.println(pendingCmd);
  if (pendingCmd == "?TN" || pendingCmd == "?SN") continueReadAllSequence();
  else pendingCmd = "";
  notifyInfo();
  markScreenDirty();
}

void runAutoIdentityQuery() {
  if (!autoIdentityQueued || pendingCmd.length() > 0 || millis() < autoIdentityAt) return;
  autoIdentityQueued = false;
  if (cfg.brand == BRAND_METTLER) {
    Serial.println("BOOT IDENTITY METTLER: I10");
    startBalanceCommand("I10", "ATOM");
  } else {
    Serial.println("BOOT IDENTITY A&D: ?TN -> ?SN -> ?ID");
    startBalanceCommand("?TN", "ATOM");
  }
}

void startBle() {
  BLEDevice::init(defaultBleName().c_str());
  BLEDevice::setMTU(185);

  bleServer = BLEDevice::createServer();
  bleServer->setCallbacks(new HubServerCallbacks());

  BLEService *service = bleServer->createService(SERVICE_UUID);

  valueChar = service->createCharacteristic(VALUE_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  valueChar->addDescriptor(new BLE2902());

  BLECharacteristic *commandChar = service->createCharacteristic(
      COMMAND_UUID,
      BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  commandChar->setCallbacks(new HubCommandCallbacks());

  infoChar = service->createCharacteristic(INFO_UUID,
                                           BLECharacteristic::PROPERTY_READ |
                                           BLECharacteristic::PROPERTY_NOTIFY);
  infoChar->addDescriptor(new BLE2902());
  infoChar->setValue(compactInfoPayload().c_str());

  service->start();

  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(SERVICE_UUID);
  advertising->setScanResponse(true);
  advertising->setMinPreferred(0x06);
  advertising->setMaxPreferred(0x12);
  BLEDevice::startAdvertising();

  Serial.print("BLE advertising ");
  Serial.println(defaultBleName());
}

void setup() {
  auto m5cfg = M5.config();
  M5.begin(m5cfg);
  M5.Display.setRotation(0);
  sprite.createSprite(SCREEN_W, SCREEN_H);
  sprite.setColorDepth(16);

  Serial.begin(115200);
  delay(200);

  Serial.println();
  Serial.println("LabConnect AtomS3 BLE node");

  loadConfig();
  configureBalanceSerial();
  autoIdentityAt = millis() + AUTO_IDENTITY_DELAY_MS;
  startBle();
  notifyInfo();
  drawScreen();
}

void loop() {
  M5.update();
  readBalanceNonBlocking();
  runPendingTimeout();
  runAutoIdentityQuery();
  refreshScreenIfNeeded();
}
