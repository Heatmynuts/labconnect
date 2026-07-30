/*
 * LabConnect Hub CDO - Atom Lite
 *
 * Balance RS232 <-> M5Stack Atom Lite <-> Bluetooth Classic SPP <-> Windows COM.
 *
 * Client CDO:
 * - 5 balances, 2 Mettler and 3 A&D.
 * - One firmware for every Atom Lite.
 * - Windows/Optimu sends MT-SICS-like commands over the Bluetooth COM port.
 *
 * Required board: M5Stack "M5Atom" / Atom Lite, not AtomS3.
 * AtomS3 cannot expose a Bluetooth Classic SPP COM port.
 */

#include <Arduino.h>
#include <BluetoothSerial.h>
#include <esp_gap_bt_api.h>
#include <esp32-hal-rgb-led.h>

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is disabled in the selected ESP32 board configuration.
#endif

#if !defined(CONFIG_BT_SPP_ENABLED)
#error Bluetooth Classic SPP is unavailable. Select M5Atom / Atom Lite, not AtomS3.
#endif

namespace {

constexpr uint32_t DEBUG_BAUD = 115200;

// M5Stack Atom RS232 adapter pins used by the existing LabConnectBT firmware.
constexpr int BALANCE_RX_PIN = 22;
constexpr int BALANCE_TX_PIN = 19;
constexpr int LED_PIN = 27;
constexpr int BUTTON_PIN = 39;

constexpr uint32_t IDENT_TIMEOUT_MS = 1800;
constexpr uint32_t WEIGHT_TIMEOUT_MS = 1600;
constexpr uint32_t LINE_GAP_MS = 80;
constexpr uint32_t LED_ACTIVITY_MS = 90;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 30;
constexpr uint32_t MULTI_CLICK_TIMEOUT_MS = 900;
constexpr uint32_t PAIRING_LONG_PRESS_MS = 3000;
constexpr uint32_t PAIRING_MODE_MS = 120000;
constexpr uint32_t PAIRING_BLINK_MS = 350;
constexpr size_t MAX_LINE = 160;

constexpr uint32_t COLOR_BOOT = 0x120012;
constexpr uint32_t COLOR_WAITING = 0x180000;
constexpr uint32_t COLOR_CONNECTED = 0x000018;
constexpr uint32_t COLOR_ACTIVITY = 0x001800;
constexpr uint32_t COLOR_PAIRING = 0x000030;
constexpr uint32_t COLOR_WARN = 0x181000;
constexpr uint32_t COLOR_CDO03 = 0x000030;
constexpr uint32_t COLOR_CDO04 = 0x180018;
constexpr uint32_t COLOR_CDO05 = 0x003000;
constexpr uint32_t COLOR_CDO06 = 0x002424;

enum class Protocol : uint8_t {
  Unknown,
  Mettler,
  AD,
};

struct SerialProfile {
  uint32_t baud = 9600;
  uint32_t config = SERIAL_8N1;
  const char *label = "9600/8N1";
};

struct BalanceState {
  Protocol protocol = Protocol::Unknown;
  String id = "";
  String bluetoothName = "";
  uint8_t decimals = 3;
  SerialProfile serial;
  String latestLine = "";
  String latestReply = "";
};

HardwareSerial BalanceSerial(1);
BluetoothSerial SerialBT;
BalanceState balance;

String pcLine;
String balanceLine;
uint32_t balanceLastByteAt = 0;
uint32_t lastLedAt = 0;
uint32_t shownColor = UINT32_MAX;
bool previousClientState = false;
bool buttonRawPressed = false;
bool buttonStablePressed = false;
bool buttonLongPressHandled = false;
uint8_t buttonClickCount = 0;
uint32_t buttonChangedAt = 0;
uint32_t buttonPressedAt = 0;
uint32_t lastButtonClickAt = 0;
bool pairingMode = false;
uint32_t pairingEndsAt = 0;
uint32_t restartAt = 0;

uint32_t identityColor() {
  if (balance.id == "CDO03") return COLOR_CDO03;
  if (balance.id == "CDO04") return COLOR_CDO04;
  if (balance.id == "CDO05") return COLOR_CDO05;
  if (balance.id == "CDO06") return COLOR_CDO06;
  return balance.id.length() ? COLOR_WAITING : COLOR_WARN;
}

SerialProfile mettlerProfile() {
  return {9600, SERIAL_8N1, "9600/8N1"};
}

SerialProfile adProfile() {
  return {2400, SERIAL_7E1, "2400/7E1"};
}

const char *protocolName(Protocol protocol) {
  switch (protocol) {
    case Protocol::Mettler: return "Mettler";
    case Protocol::AD: return "A&D";
    default: return "Unknown";
  }
}

void setLed(uint32_t color) {
  if (color == shownColor) return;
  shownColor = color;
  rgbLedWrite(
      LED_PIN,
      static_cast<uint8_t>(color >> 16),
      static_cast<uint8_t>(color >> 8),
      static_cast<uint8_t>(color));
}

void noteActivity() {
  lastLedAt = millis();
  setLed(COLOR_ACTIVITY);
}

String chipSuffix() {
  char suffix[7];
  snprintf(suffix, sizeof(suffix), "%06X", static_cast<uint32_t>(ESP.getEfuseMac() & 0xFFFFFF));
  return String(suffix);
}

String trimCopy(String value) {
  value.trim();
  return value;
}

String compactSpaces(const String &raw) {
  String out;
  out.reserve(raw.length());
  bool prevSpace = true;
  for (size_t i = 0; i < raw.length(); i++) {
    char c = raw.charAt(i);
    if (c == '\r' || c == '\n' || c == '\t') c = ' ';
    if (c <= 32) {
      if (!prevSpace) out += ' ';
      prevSpace = true;
    } else {
      out += c;
      prevSpace = false;
    }
  }
  out.trim();
  return out;
}

bool isAllZeros(const String &value) {
  bool sawDigit = false;
  for (size_t i = 0; i < value.length(); i++) {
    char c = value.charAt(i);
    if (c >= '0' && c <= '9') {
      sawDigit = true;
      if (c != '0') return false;
    } else if (c != ' ' && c != '-' && c != '_' && c != ',') {
      return false;
    }
  }
  return sawDigit;
}

String normalizeCdoId(String raw) {
  raw.trim();
  raw.replace("\"", "");
  raw.replace("'", "");
  raw.replace(" ", "");
  raw.replace("_", "");
  raw.replace("-", "");
  raw.toUpperCase();
  if (raw.startsWith("CDO")) raw = raw.substring(3);

  String digits;
  for (size_t i = 0; i < raw.length(); i++) {
    char c = raw.charAt(i);
    if (c >= '0' && c <= '9') digits += c;
  }
  while (digits.length() > 1 && digits.charAt(0) == '0') digits.remove(0, 1);
  if (digits.length() == 0 || isAllZeros(digits)) return "";
  if (digits.length() == 1) digits = "0" + digits;
  if (digits.length() > 2) digits = digits.substring(digits.length() - 2);
  return "CDO" + digits;
}

uint8_t decimalsFromId(const String &cdoId) {
  if (!cdoId.startsWith("CDO") || cdoId.length() < 5) return 3;
  const int value = cdoId.substring(cdoId.length() - 2).toInt();
  if (value > 0 && value <= 9) return static_cast<uint8_t>(value);
  return 3;
}

void applyDetectedIdentity(Protocol protocol, const String &cdoId, const SerialProfile &profile) {
  balance.protocol = protocol;
  balance.id = cdoId;
  balance.decimals = decimalsFromId(cdoId);
  balance.serial = profile;
  balance.bluetoothName = cdoId.length() ? cdoId : ("CDO-Atom-" + chipSuffix().substring(2));
}

void beginBalanceSerial(const SerialProfile &profile) {
  BalanceSerial.end();
  delay(40);
  BalanceSerial.setRxBufferSize(512);
  BalanceSerial.setTimeout(10);
  BalanceSerial.begin(profile.baud, profile.config, BALANCE_RX_PIN, BALANCE_TX_PIN);
  while (BalanceSerial.available()) BalanceSerial.read();
  Serial.printf("RS232 %s RX=%d TX=%d\n", profile.label, BALANCE_RX_PIN, BALANCE_TX_PIN);
}

void clearBalanceInput() {
  while (BalanceSerial.available()) BalanceSerial.read();
  balanceLine = "";
}

void writeBalanceCommand(const String &command) {
  Serial.print("BALANCE < ");
  Serial.println(command);
  BalanceSerial.print(command);
  BalanceSerial.print("\r\n");
  BalanceSerial.flush();
  noteActivity();
}

bool readBalanceLine(String &line, uint32_t timeoutMs) {
  line = "";
  const uint32_t start = millis();
  uint32_t lastByte = millis();
  while (millis() - start < timeoutMs) {
    while (BalanceSerial.available()) {
      char c = static_cast<char>(BalanceSerial.read());
      lastByte = millis();
      if (c == '\r' || c == '\n') {
        if (line.length() > 0) {
          line = compactSpaces(line);
          Serial.print("BALANCE > ");
          Serial.println(line);
          balance.latestLine = line;
          noteActivity();
          return true;
        }
      } else if (line.length() < MAX_LINE) {
        line += c;
      }
    }
    if (line.length() > 0 && millis() - lastByte > LINE_GAP_MS) {
      line = compactSpaces(line);
      Serial.print("BALANCE > ");
      Serial.println(line);
      balance.latestLine = line;
      noteActivity();
      return true;
    }
    delay(1);
  }
  return false;
}

bool isMettlerError(const String &line) {
  String upper = trimCopy(line);
  upper.toUpperCase();
  return upper == "ES" || upper == "ET" || upper == "EL" || upper.startsWith("I10 I") || upper.startsWith("I10 L");
}

bool parseMettlerId(const String &line, String &cdoId) {
  String compact = compactSpaces(line);
  String upper = compact;
  upper.toUpperCase();
  if (!upper.startsWith("I10")) return false;

  const int firstQuote = compact.indexOf('"');
  const int secondQuote = firstQuote >= 0 ? compact.indexOf('"', firstQuote + 1) : -1;
  if (firstQuote < 0 || secondQuote <= firstQuote) return false;

  cdoId = normalizeCdoId(compact.substring(firstQuote + 1, secondQuote));
  return cdoId.length() > 0;
}

bool parseAdId(const String &line, String &cdoId) {
  String compact = compactSpaces(line);
  String upper = compact;
  upper.toUpperCase();
  if (!upper.startsWith("ID")) return false;

  int comma = compact.indexOf(',');
  String raw = comma >= 0 ? compact.substring(comma + 1) : compact.substring(2);
  raw.trim();
  if (raw.length() == 0 || isAllZeros(raw)) return false;
  cdoId = normalizeCdoId(raw);
  return cdoId.length() > 0;
}

bool detectMettler() {
  beginBalanceSerial(mettlerProfile());
  writeBalanceCommand("I10");

  String line;
  const uint32_t start = millis();
  while (millis() - start < IDENT_TIMEOUT_MS) {
    if (!readBalanceLine(line, 250)) continue;
    String cdoId;
    if (parseMettlerId(line, cdoId)) {
      applyDetectedIdentity(Protocol::Mettler, cdoId, mettlerProfile());
      return true;
    }
    if (isMettlerError(line)) return false;
  }
  return false;
}

bool detectAd() {
  beginBalanceSerial(adProfile());
  writeBalanceCommand("?ID");

  String line;
  const uint32_t start = millis();
  while (millis() - start < IDENT_TIMEOUT_MS) {
    if (!readBalanceLine(line, 250)) continue;
    String cdoId;
    if (parseAdId(line, cdoId)) {
      applyDetectedIdentity(Protocol::AD, cdoId, adProfile());
      return true;
    }
  }

  applyDetectedIdentity(Protocol::AD, "", adProfile());
  return false;
}

void detectBalance() {
  Serial.println("Detecting balance: Mettler I10, then A&D ?ID");
  setLed(COLOR_BOOT);
  if (detectMettler()) return;
  detectAd();
}

bool startsLikeMettlerWeight(const String &line) {
  String upper = trimCopy(line);
  upper.toUpperCase();
  return upper.startsWith("S ") || upper.startsWith("SI ");
}

bool parseAdWeight(const String &line, String &reply) {
  String compact = compactSpaces(line);
  String upper = compact;
  upper.toUpperCase();
  if (!(upper.startsWith("ST") || upper.startsWith("US") || upper.startsWith("OL"))) return false;

  String status = upper.startsWith("ST") ? "S" : upper.startsWith("US") ? "D" : "I";
  String unit = "g";
  String value;

  int searchFrom = 0;
  int comma = compact.indexOf(',');
  if (comma >= 0) {
    int secondComma = compact.indexOf(',', comma + 1);
    searchFrom = secondComma >= 0 ? secondComma + 1 : comma + 1;
  }

  int start = -1;
  for (int i = searchFrom; i < static_cast<int>(compact.length()); i++) {
    const char c = compact.charAt(i);
    if (c == '+' || c == '-' || c == '.' || (c >= '0' && c <= '9')) {
      start = i;
      break;
    }
  }
  if (start < 0) return false;

  int end = start;
  while (end < static_cast<int>(compact.length())) {
    char c = compact.charAt(end);
    if (!((c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.')) break;
    end++;
  }
  value = compact.substring(start, end);
  value.trim();
  if (value.length() == 0 || value == "+" || value == "-" || value == ".") return false;

  String tail = compact.substring(end);
  tail.replace(",", " ");
  tail.trim();
  if (tail.length() > 0) {
    int space = tail.indexOf(' ');
    unit = space >= 0 ? tail.substring(0, space) : tail;
    unit.trim();
  }

  const double numeric = atof(value.c_str());
  char number[32];
  dtostrf(numeric, 0, balance.decimals, number);
  String formatted = String(number);
  formatted.trim();
  if (!formatted.startsWith("-") && !formatted.startsWith("+")) formatted = "+" + formatted;

  reply = "S " + status + " " + formatted + " " + unit;
  return true;
}

bool normalizeWeightReply(const String &line, String &reply) {
  if (startsLikeMettlerWeight(line)) {
    reply = compactSpaces(line);
    return true;
  }
  return parseAdWeight(line, reply);
}

bool requestWeight(String &reply) {
  String line;

  if (balance.protocol == Protocol::Mettler) {
    clearBalanceInput();
    writeBalanceCommand("SI");
    const uint32_t start = millis();
    while (millis() - start < WEIGHT_TIMEOUT_MS) {
      if (readBalanceLine(line, 250) && normalizeWeightReply(line, reply)) return true;
    }
    return false;
  }

  clearBalanceInput();
  writeBalanceCommand("SI");
  uint32_t start = millis();
  while (millis() - start < 700) {
    if (readBalanceLine(line, 220) && normalizeWeightReply(line, reply)) return true;
  }

  clearBalanceInput();
  writeBalanceCommand("Q");
  start = millis();
  while (millis() - start < WEIGHT_TIMEOUT_MS) {
    if (readBalanceLine(line, 250) && normalizeWeightReply(line, reply)) return true;
  }
  return false;
}

bool requestRawCommand(const String &command, String &reply) {
  clearBalanceInput();
  writeBalanceCommand(command);
  String line;
  if (!readBalanceLine(line, WEIGHT_TIMEOUT_MS)) return false;
  reply = line;
  return true;
}

void replyToPc(const String &value) {
  Serial.print("PC > ");
  Serial.println(value);
  SerialBT.print(value);
  SerialBT.print("\r\n");
  noteActivity();
}

void handlePcCommand(String command) {
  command.trim();
  if (command.length() == 0) return;
  Serial.print("PC < ");
  Serial.println(command);

  String upper = command;
  upper.toUpperCase();

  if (upper == "SI" || upper == "S") {
    String reply;
    if (requestWeight(reply)) {
      balance.latestReply = reply;
      replyToPc(reply);
    } else {
      replyToPc("S I");
    }
    return;
  }

  if (upper == "I10") {
    const String id = balance.id.length() ? balance.id : "UNKNOWN";
    replyToPc("I10 A \"" + id + "\"");
    return;
  }

  if (upper == "?ID") {
    const String id = balance.id.startsWith("CDO") ? balance.id.substring(3) : balance.id;
    replyToPc("ID," + (id.length() ? id : "000000"));
    return;
  }

  if (upper == "T" || upper == "Z") {
    String raw;
    if (requestRawCommand(upper, raw)) replyToPc(raw);
    else replyToPc("OK");
    return;
  }

  if (upper == "LCINFO") {
    replyToPc(String("LCINFO ") + protocolName(balance.protocol) + " " + balance.id +
              " " + balance.serial.label + " BT=" + balance.bluetoothName);
    return;
  }

  String raw;
  if (requestRawCommand(command, raw)) replyToPc(raw);
  else replyToPc("ERR TIMEOUT");
}

void readPcCommands() {
  while (SerialBT.available()) {
    char c = static_cast<char>(SerialBT.read());
    if (c == '\r' || c == '\n') {
      if (pcLine.length() > 0) {
        handlePcCommand(pcLine);
        pcLine = "";
      }
    } else if (pcLine.length() < MAX_LINE) {
      pcLine += c;
    }
  }
}

void readBalanceBackground() {
  while (BalanceSerial.available()) {
    char c = static_cast<char>(BalanceSerial.read());
    balanceLastByteAt = millis();
    if (c == '\r' || c == '\n') {
      if (balanceLine.length() > 0) {
        balance.latestLine = compactSpaces(balanceLine);
        String normalized;
        if (normalizeWeightReply(balance.latestLine, normalized)) {
          balance.latestReply = normalized;
        }
        Serial.print("BALANCE stream > ");
        Serial.println(balance.latestLine);
        balanceLine = "";
      }
    } else if (balanceLine.length() < MAX_LINE) {
      balanceLine += c;
    }
  }

  if (balanceLine.length() > 0 && millis() - balanceLastByteAt > LINE_GAP_MS) {
    balance.latestLine = compactSpaces(balanceLine);
    String normalized;
    if (normalizeWeightReply(balance.latestLine, normalized)) {
      balance.latestReply = normalized;
    }
    Serial.print("BALANCE stream > ");
    Serial.println(balance.latestLine);
    balanceLine = "";
  }
}

void performLocalWeightTest() {
  Serial.println("BUTTON single click: local SI test");
  String reply;
  if (requestWeight(reply)) {
    balance.latestReply = reply;
    Serial.print("BUTTON SI OK > ");
    Serial.println(reply);
    setLed(identityColor());
    lastLedAt = millis();
  } else {
    Serial.println("BUTTON SI timeout");
    setLed(COLOR_WARN);
    lastLedAt = millis();
  }
}

void stopPairingMode() {
  if (!pairingMode) return;
  esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
  pairingMode = false;
  pairingEndsAt = 0;
  Serial.println("Bluetooth pairing mode: disabled");
}

void startPairingMode() {
  if (SerialBT.hasClient()) {
    Serial.println("Bluetooth pairing ignored: client already connected");
    return;
  }
  const esp_err_t result =
      esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
  if (result != ESP_OK) {
    Serial.printf("Bluetooth pairing mode failed: %d\n", static_cast<int>(result));
    return;
  }
  pairingMode = true;
  pairingEndsAt = millis() + PAIRING_MODE_MS;
  Serial.println("Bluetooth pairing mode: enabled for 120 seconds");
}

void togglePairingMode() {
  if (pairingMode) stopPairingMode();
  else startPairingMode();
}

void scheduleRedetectRestart() {
  Serial.println("BUTTON triple click: restart for balance identification");
  setLed(COLOR_BOOT);
  restartAt = millis() + 250;
}

void flushButtonClicksIfReady() {
  if (buttonClickCount == 0) return;
  if (millis() - lastButtonClickAt <= MULTI_CLICK_TIMEOUT_MS) return;

  const uint8_t clicks = buttonClickCount;
  buttonClickCount = 0;
  if (clicks == 1) {
    performLocalWeightTest();
  }
}

void updateButton() {
  const uint32_t now = millis();
  const bool rawPressed = digitalRead(BUTTON_PIN) == LOW;

  if (rawPressed != buttonRawPressed) {
    buttonRawPressed = rawPressed;
    buttonChangedAt = now;
  }

  if (rawPressed != buttonStablePressed &&
      now - buttonChangedAt >= BUTTON_DEBOUNCE_MS) {
    buttonStablePressed = rawPressed;
    if (buttonStablePressed) {
      buttonPressedAt = now;
      buttonLongPressHandled = false;
    } else if (!buttonLongPressHandled) {
      if (buttonClickCount > 0 &&
          now - lastButtonClickAt > MULTI_CLICK_TIMEOUT_MS) {
        buttonClickCount = 0;
      }
      lastButtonClickAt = now;
      buttonClickCount++;
      if (buttonClickCount == 3) {
        buttonClickCount = 0;
        scheduleRedetectRestart();
      }
    }
  }

  if (buttonStablePressed && !buttonLongPressHandled &&
      now - buttonPressedAt >= PAIRING_LONG_PRESS_MS) {
    buttonLongPressHandled = true;
    buttonClickCount = 0;
    togglePairingMode();
  }

  flushButtonClicksIfReady();
}

void updateLed() {
  if (millis() - lastLedAt < LED_ACTIVITY_MS) return;
  bool connected = SerialBT.hasClient();
  if (connected != previousClientState) {
    previousClientState = connected;
    Serial.printf("Bluetooth client %s\n", connected ? "connected" : "disconnected");
  }
  if (pairingMode) {
    setLed((millis() / PAIRING_BLINK_MS) % 2 ? COLOR_PAIRING : 0x000000);
  } else {
    setLed(connected ? COLOR_CONNECTED : identityColor());
  }
}

void startBluetooth() {
  SerialBT.enableSSP(false, false);
  if (!SerialBT.begin(balance.bluetoothName, false, false)) {
    Serial.println("Bluetooth SPP init failed");
    while (true) {
      setLed((millis() / 250) % 2 ? COLOR_WARN : 0x000000);
      delay(10);
    }
  }
  SerialBT.setTimeout(10);
  esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
}

}  // namespace

void setup() {
  Serial.begin(DEBUG_BAUD);
  delay(80);
  Serial.println();
  Serial.println("LabConnect Hub CDO Atom Lite boot");

  pinMode(BUTTON_PIN, INPUT);
  setLed(COLOR_BOOT);
  detectBalance();
  startBluetooth();

  Serial.println("LabConnect Hub CDO ready");
  Serial.printf("Protocol: %s\n", protocolName(balance.protocol));
  Serial.printf("Balance ID: %s\n", balance.id.length() ? balance.id.c_str() : "?");
  Serial.printf("Decimals: %u\n", balance.decimals);
  Serial.printf("Bluetooth COM name: %s\n", balance.bluetoothName.c_str());
  Serial.println("Windows command: SI");
}

void loop() {
  readPcCommands();
  readBalanceBackground();
  updateButton();
  if (pairingMode && static_cast<int32_t>(millis() - pairingEndsAt) >= 0) {
    stopPairingMode();
  }
  if (restartAt != 0 && static_cast<int32_t>(millis() - restartAt) >= 0) {
    ESP.restart();
  }
  updateLed();
  delay(1);
}
