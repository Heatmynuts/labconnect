/*
 * LabConnect Hub CDO - firmware universel pour M5Stack ATOM Lite.
 *
 * Au démarrage, le firmware identifie la balance avec I10 (Mettler), puis
 * ?ID (A&D). Après validation de CDO02..CDO06, il expose ce nom en Bluetooth
 * Classic SPP et devient un pont strictement transparent entre Optimu et la
 * balance. Les poids, statuts, unités, espaces et terminaisons ne sont jamais
 * parsés ni reconstruits.
 *
 * Préparation obligatoire de toutes les balances : 9600/8N1, sans contrôle
 * de flux, terminaison CRLF. Les A&D doivent également utiliser le format MT.
 */

#include <Arduino.h>
#include <BluetoothSerial.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_gap_bt_api.h>
#include <esp32-hal-rgb-led.h>

#include "CdoIdentity.h"
#include "web_ui.h"

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is disabled in the selected ESP32 board configuration.
#endif

#if !defined(CONFIG_BT_SPP_ENABLED)
#error Bluetooth Classic SPP is unavailable. Select M5Atom / ATOM Lite, not AtomS3.
#endif

namespace {

constexpr uint32_t DEBUG_BAUD = 115200;
constexpr uint32_t BALANCE_BAUD = 9600;
constexpr uint32_t BALANCE_SERIAL_CONFIG = SERIAL_8N1;
#ifndef LABCONNECT_SWAP_BALANCE_UART
#define LABCONNECT_SWAP_BALANCE_UART 0
#endif

#if LABCONNECT_SWAP_BALANCE_UART
constexpr int BALANCE_RX_PIN = 19;
constexpr int BALANCE_TX_PIN = 22;
#else
constexpr int BALANCE_RX_PIN = 22;
constexpr int BALANCE_TX_PIN = 19;
#endif
constexpr int LED_PIN = 27;
constexpr int BUTTON_PIN = 39;

constexpr size_t BRIDGE_BUFFER_SIZE = 256;
constexpr size_t IDENTITY_LINE_SIZE = 128;
constexpr size_t LOG_CAPACITY = 72;
constexpr size_t LOG_DATA_SIZE = 96;
constexpr uint32_t IDENTITY_REPLY_TIMEOUT_MS = 1200;
constexpr uint32_t IDENTITY_RETRY_DELAY_MS = 1500;
constexpr uint32_t IDENTITY_LINE_GAP_MS = 300;
constexpr uint32_t ACTIVITY_LED_MS = 90;
constexpr uint32_t LED_REFRESH_MS = 20;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 30;
constexpr uint32_t MULTI_CLICK_TIMEOUT_MS = 900;
constexpr uint32_t PAIRING_LONG_PRESS_MS = 2500;
constexpr uint32_t PAIRING_MODE_MS = 120000;
constexpr uint32_t PAIRING_BLINK_MS = 350;
constexpr uint32_t READY_BLINK_MS = 700;

constexpr uint32_t COLOR_IDENTIFYING = 0x241000;
constexpr uint32_t COLOR_ERROR = 0x280000;
constexpr uint32_t COLOR_READY = 0x202020;
constexpr uint32_t COLOR_CONNECTED = 0x000020;
constexpr uint32_t COLOR_ACTIVITY = 0x002400;
constexpr uint32_t COLOR_PAIRING = 0x000038;

HardwareSerial BalanceSerial(1);
BluetoothSerial SerialBT;
WebServer Web(80);

uint8_t bridgeBuffer[BRIDGE_BUFFER_SIZE];
char identityLine[IDENTITY_LINE_SIZE] = {};
size_t identityLineLength = 0;
uint32_t identityLastByteAt = 0;

enum class IdentificationPhase : uint8_t {
  SendMettler,
  WaitMettler,
  SendAD,
  WaitAD,
  RetryDelay,
  Complete,
};

enum class TrafficDirection : uint8_t {
  BalanceToPc,
  PcToBalance,
  IdentificationFromBalance,
  IdentificationToBalance,
};

struct TrafficEvent {
  uint32_t sequence = 0;
  uint32_t timestamp = 0;
  TrafficDirection direction = TrafficDirection::BalanceToPc;
  uint8_t length = 0;
  uint8_t data[LOG_DATA_SIZE] = {};
};

TrafficEvent trafficLog[LOG_CAPACITY];
size_t trafficHead = 0;
size_t trafficCount = 0;
uint32_t nextSequence = 1;

IdentificationPhase identificationPhase = IdentificationPhase::SendMettler;
uint32_t identificationDeadline = 0;
uint32_t identificationAttempts = 0;
bool identificationError = false;
String lastIdentificationLine;
String lastIdentificationError;
const CdoBalanceDefinition *detectedBalance = nullptr;
bool bluetoothStarted = false;
bool bluetoothFault = false;
bool previousClientState = false;

bool wifiEnabled = false;
bool webRoutesConfigured = false;
String diagnosticSsid;

bool pairingMode = false;
uint32_t pairingEndsAt = 0;
uint32_t lastActivityAt = 0;
uint32_t lastLedRefreshAt = 0;
uint32_t displayedColor = UINT32_MAX;

bool buttonRawPressed = false;
bool buttonStablePressed = false;
bool buttonLongPressHandled = false;
uint8_t buttonClickCount = 0;
uint32_t buttonChangedAt = 0;
uint32_t buttonPressedAt = 0;
uint32_t lastButtonClickAt = 0;

const char *manufacturerName(CdoManufacturer manufacturer) {
  return manufacturer == CdoManufacturer::Mettler ? "Mettler" : "A&D";
}

const char *directionName(TrafficDirection direction) {
  switch (direction) {
    case TrafficDirection::BalanceToPc: return "rx";
    case TrafficDirection::PcToBalance: return "tx";
    case TrafficDirection::IdentificationFromBalance: return "id-rx";
    case TrafficDirection::IdentificationToBalance: return "id-tx";
  }
  return "?";
}

void setLed(uint32_t color) {
  if (displayedColor == color) return;
  displayedColor = color;
  rgbLedWrite(
      LED_PIN,
      static_cast<uint8_t>(color >> 16),
      static_cast<uint8_t>(color >> 8),
      static_cast<uint8_t>(color));
}

void noteActivity() {
  lastActivityAt = millis();
}

void recordTraffic(
    TrafficDirection direction,
    const uint8_t *data,
    size_t length) {
  while (length > 0) {
    const size_t partLength = min(length, LOG_DATA_SIZE);
    TrafficEvent &event = trafficLog[trafficHead];
    event.sequence = nextSequence++;
    event.timestamp = millis();
    event.direction = direction;
    event.length = static_cast<uint8_t>(partLength);
    memcpy(event.data, data, partLength);
    trafficHead = (trafficHead + 1) % LOG_CAPACITY;
    if (trafficCount < LOG_CAPACITY) ++trafficCount;
    data += partLength;
    length -= partLength;
  }
}

String chipSuffix() {
  char suffix[7];
  snprintf(
      suffix,
      sizeof(suffix),
      "%06X",
      static_cast<unsigned int>(ESP.getEfuseMac() & 0xFFFFFF));
  return String(suffix);
}

void clearIdentityLine() {
  identityLineLength = 0;
  identityLine[0] = '\0';
  identityLastByteAt = 0;
}

void drainBalanceInput() {
  while (BalanceSerial.available()) BalanceSerial.read();
  clearIdentityLine();
}

void stopPairingMode();

bool startBluetooth() {
  if (detectedBalance == nullptr) return false;

  Serial.printf("[Bluetooth] Starting SPP as %s\n", detectedBalance->id);
  SerialBT.enableSSP(false, false);
  if (!SerialBT.begin(detectedBalance->id, false, false)) {
    Serial.println("[Bluetooth] SPP initialization failed");
    bluetoothFault = true;
    return false;
  }
  SerialBT.setTimeout(5);
  esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
  bluetoothStarted = true;
  previousClientState = false;
  return true;
}

void acceptIdentity(
    const CdoBalanceDefinition *candidate,
    CdoManufacturer respondingManufacturer) {
  if (candidate == nullptr) return;
  if (candidate->manufacturer != respondingManufacturer) {
    lastIdentificationError = String(candidate->id) +
        " reçu par le mauvais protocole (attendu " +
        manufacturerName(candidate->manufacturer) + ")";
    identificationError = true;
    Serial.printf("[Identification] Rejected: %s\n", lastIdentificationError.c_str());
    return;
  }

  detectedBalance = candidate;
  identificationPhase = IdentificationPhase::Complete;
  identificationError = false;
  lastIdentificationError = "";
  clearIdentityLine();
  Serial.printf(
      "[Identification] %s / %s / desired COM%u\n",
      candidate->id,
      candidate->model,
      candidate->desiredComPort);
  startBluetooth();
}

void processIdentificationLine(CdoManufacturer respondingManufacturer) {
  if (identityLineLength == 0) return;
  identityLine[identityLineLength] = '\0';
  lastIdentificationLine = String(identityLine);
  recordTraffic(
      TrafficDirection::IdentificationFromBalance,
      reinterpret_cast<const uint8_t *>(identityLine),
      identityLineLength);
  Serial.printf("[Identification] Balance > %s\n", identityLine);

  char id[6] = {};
  const CdoBalanceDefinition *candidate =
      extractCdoBalance(identityLine, identityLineLength, id);
  clearIdentityLine();
  if (candidate != nullptr) acceptIdentity(candidate, respondingManufacturer);
}

void readIdentificationBytes() {
  if (identificationPhase != IdentificationPhase::WaitMettler &&
      identificationPhase != IdentificationPhase::WaitAD) {
    return;
  }

  const CdoManufacturer manufacturer =
      identificationPhase == IdentificationPhase::WaitMettler
          ? CdoManufacturer::Mettler
          : CdoManufacturer::AD;

  while (BalanceSerial.available() && detectedBalance == nullptr) {
    const uint8_t byte = static_cast<uint8_t>(BalanceSerial.read());
    identityLastByteAt = millis();
    if (byte == '\r' || byte == '\n') {
      processIdentificationLine(manufacturer);
    } else if (identityLineLength < IDENTITY_LINE_SIZE - 1) {
      identityLine[identityLineLength++] = static_cast<char>(byte);
    } else {
      lastIdentificationError = "Réponse d’identification trop longue";
      identificationError = true;
      clearIdentityLine();
    }
  }

  if (identityLineLength > 0 &&
      millis() - identityLastByteAt >= IDENTITY_LINE_GAP_MS) {
    processIdentificationLine(manufacturer);
  }
}

void sendIdentificationCommand(
    const char *command,
    TrafficDirection direction) {
  drainBalanceInput();
  const size_t commandLength = strlen(command);
  BalanceSerial.write(
      reinterpret_cast<const uint8_t *>(command), commandLength);
  recordTraffic(
      direction,
      reinterpret_cast<const uint8_t *>(command), commandLength);
  Serial.print("[Identification] Balance < ");
  Serial.print(command);
  noteActivity();
}

void updateIdentification() {
  if (detectedBalance != nullptr || bluetoothFault) return;
  readIdentificationBytes();
  if (detectedBalance != nullptr) return;

  const uint32_t now = millis();
  switch (identificationPhase) {
    case IdentificationPhase::SendMettler:
      if (static_cast<int32_t>(now - identificationDeadline) >= 0) {
        sendIdentificationCommand(
            "I10\r\n", TrafficDirection::IdentificationToBalance);
        identificationPhase = IdentificationPhase::WaitMettler;
        identificationDeadline = now + IDENTITY_REPLY_TIMEOUT_MS;
      }
      break;

    case IdentificationPhase::WaitMettler:
      if (static_cast<int32_t>(now - identificationDeadline) >= 0) {
        clearIdentityLine();
        identificationPhase = IdentificationPhase::SendAD;
      }
      break;

    case IdentificationPhase::SendAD:
      sendIdentificationCommand(
          "?ID\r\n", TrafficDirection::IdentificationToBalance);
      identificationPhase = IdentificationPhase::WaitAD;
      identificationDeadline = now + IDENTITY_REPLY_TIMEOUT_MS;
      break;

    case IdentificationPhase::WaitAD:
      if (static_cast<int32_t>(now - identificationDeadline) >= 0) {
        ++identificationAttempts;
        identificationError = true;
        lastIdentificationError =
            "Aucun identifiant CDO valide; nouvel essai en cours";
        Serial.printf(
            "[Identification] Attempt %lu failed; retrying\n",
            static_cast<unsigned long>(identificationAttempts));
        clearIdentityLine();
        identificationPhase = IdentificationPhase::RetryDelay;
        identificationDeadline = now + IDENTITY_RETRY_DELAY_MS;
      }
      break;

    case IdentificationPhase::RetryDelay:
      if (static_cast<int32_t>(now - identificationDeadline) >= 0) {
        identificationPhase = IdentificationPhase::SendMettler;
      }
      break;

    case IdentificationPhase::Complete:
      break;
  }
}

void forwardBalanceToBluetooth() {
  const int available = BalanceSerial.available();
  if (available <= 0) return;

  size_t count = 0;
  while (count < BRIDGE_BUFFER_SIZE && BalanceSerial.available()) {
    bridgeBuffer[count++] = static_cast<uint8_t>(BalanceSerial.read());
  }
  if (count == 0) return;

  // Always drain the UART. When no PC is connected the bytes are deliberately
  // discarded, so an old measurement can never be delivered on reconnection.
  if (SerialBT.hasClient()) SerialBT.write(bridgeBuffer, count);
  recordTraffic(TrafficDirection::BalanceToPc, bridgeBuffer, count);
  noteActivity();
}

void forwardBluetoothToBalance() {
  if (!SerialBT.hasClient()) return;

  size_t count = 0;
  while (count < BRIDGE_BUFFER_SIZE && SerialBT.available()) {
    bridgeBuffer[count++] = static_cast<uint8_t>(SerialBT.read());
  }
  if (count == 0) return;

  BalanceSerial.write(bridgeBuffer, count);
  recordTraffic(TrafficDirection::PcToBalance, bridgeBuffer, count);
  noteActivity();
}

void appendJsonString(String &json, const String &value) {
  json += '"';
  for (size_t index = 0; index < value.length(); ++index) {
    const uint8_t c = static_cast<uint8_t>(value[index]);
    if (c == '"' || c == '\\') {
      json += '\\';
      json += static_cast<char>(c);
    } else if (c == '\r') {
      json += "\\r";
    } else if (c == '\n') {
      json += "\\n";
    } else if (c == '\t') {
      json += "\\t";
    } else if (c >= 32 && c <= 126) {
      json += static_cast<char>(c);
    }
  }
  json += '"';
}

void appendJsonText(String &json, const uint8_t *data, size_t length) {
  static const char hex[] = "0123456789ABCDEF";
  json += '"';
  for (size_t index = 0; index < length; ++index) {
    const uint8_t value = data[index];
    if (value == '\r') json += "\\r";
    else if (value == '\n') json += "\\n";
    else if (value == '\t') json += "\\t";
    else if (value == '"' || value == '\\') {
      json += '\\';
      json += static_cast<char>(value);
    } else if (value >= 32 && value <= 126) {
      json += static_cast<char>(value);
    } else {
      json += "\\x";
      json += hex[value >> 4];
      json += hex[value & 0x0F];
    }
  }
  json += '"';
}

void appendJsonHex(String &json, const uint8_t *data, size_t length) {
  static const char hex[] = "0123456789ABCDEF";
  json += '"';
  for (size_t index = 0; index < length; ++index) {
    if (index) json += ' ';
    json += hex[data[index] >> 4];
    json += hex[data[index] & 0x0F];
  }
  json += '"';
}

void sendWebPage() {
  constexpr size_t CHUNK_SIZE = 1024;
  const size_t totalLength = strlen_P(LABCONNECT_CDO_WEB_UI);
  Web.setContentLength(totalLength);
  Web.send(200, "text/html; charset=utf-8", "");
  for (size_t offset = 0; offset < totalLength; offset += CHUNK_SIZE) {
    const size_t length = min(CHUNK_SIZE, totalLength - offset);
    Web.sendContent_P(LABCONNECT_CDO_WEB_UI + offset, length);
  }
}

String identificationStatus() {
  if (detectedBalance != nullptr) {
    return String(detectedBalance->id) + " validé via " +
        manufacturerName(detectedBalance->manufacturer);
  }
  if (bluetoothFault) return "Erreur Bluetooth";
  if (lastIdentificationError.length()) return lastIdentificationError;
  return "Recherche I10 puis ?ID";
}

void handleStatus() {
  const uint32_t after = Web.hasArg("after")
      ? static_cast<uint32_t>(strtoul(Web.arg("after").c_str(), nullptr, 10))
      : 0;

  String json;
  json.reserve(8192);
  json = "{\"id\":";
  appendJsonString(json, detectedBalance ? String(detectedBalance->id) : "");
  json += ",\"model\":";
  appendJsonString(json, detectedBalance ? String(detectedBalance->model) : "");
  json += ",\"uart\":\"9600/8N1 · CRLF\"";
  json += ",\"btStarted\":";
  json += bluetoothStarted ? "true" : "false";
  json += ",\"btClient\":";
  json += (bluetoothStarted && SerialBT.hasClient()) ? "true" : "false";
  json += ",\"identification\":";
  appendJsonString(json, identificationStatus());
  json += ",\"identificationError\":";
  json += identificationError || bluetoothFault ? "true" : "false";
  json += ",\"lastIdentificationLine\":";
  appendJsonString(json, lastIdentificationLine);
  json += ",\"ssid\":";
  appendJsonString(json, diagnosticSsid);
  json += ",\"events\":[";

  bool first = true;
  size_t sent = 0;
  const size_t oldest =
      (trafficHead + LOG_CAPACITY - trafficCount) % LOG_CAPACITY;
  for (size_t index = 0; index < trafficCount && sent < 24; ++index) {
    const TrafficEvent &event = trafficLog[(oldest + index) % LOG_CAPACITY];
    if (event.sequence <= after) continue;
    if (!first) json += ',';
    first = false;
    ++sent;
    json += "{\"seq\":";
    json += event.sequence;
    json += ",\"ms\":";
    json += event.timestamp;
    json += ",\"dir\":\"";
    json += directionName(event.direction);
    json += "\",\"text\":";
    appendJsonText(json, event.data, event.length);
    json += ",\"hex\":";
    appendJsonHex(json, event.data, event.length);
    json += '}';
  }
  json += "]}";
  Web.send(200, "application/json; charset=utf-8", json);
}

void configureWebRoutes() {
  if (webRoutesConfigured) return;
  webRoutesConfigured = true;
  Web.on("/", HTTP_GET, sendWebPage);
  Web.on("/api/status", HTTP_GET, handleStatus);
  Web.onNotFound([]() {
    if (Web.uri().startsWith("/api/")) {
      Web.send(404, "application/json", "{\"error\":\"Lecture seule\"}");
    } else {
      Web.sendHeader("Location", "/", true);
      Web.send(302, "text/plain", "");
    }
  });
}

void enableWebInterface() {
  if (wifiEnabled) return;
  configureWebRoutes();
  diagnosticSsid = detectedBalance
      ? String(detectedBalance->id) + "-Diag"
      : "CDO-DIAG-" + chipSuffix().substring(2);
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  if (!WiFi.softAP(diagnosticSsid.c_str(), "labconnect")) {
    Serial.println("[Diagnostic] Wi-Fi access point failed");
    WiFi.mode(WIFI_OFF);
    return;
  }
  Web.begin();
  wifiEnabled = true;
  Serial.printf(
      "[Diagnostic] http://%s/ on %s (password: labconnect)\n",
      WiFi.softAPIP().toString().c_str(),
      diagnosticSsid.c_str());
}

void disableWebInterface() {
  if (!wifiEnabled) return;
  Web.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  wifiEnabled = false;
  Serial.println("[Diagnostic] Wi-Fi disabled");
}

void toggleWebInterface() {
  if (wifiEnabled) disableWebInterface();
  else enableWebInterface();
}

void stopPairingMode() {
  if (!pairingMode || !bluetoothStarted) return;
  esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
  pairingMode = false;
  pairingEndsAt = 0;
  Serial.println("[Bluetooth] Pairing disabled");
}

void startPairingMode() {
  if (!bluetoothStarted) {
    Serial.println("[Bluetooth] Pairing unavailable until identification succeeds");
    return;
  }
  if (SerialBT.hasClient()) {
    Serial.println("[Bluetooth] Pairing ignored: Optimu is connected");
    return;
  }
  const esp_err_t result =
      esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
  if (result != ESP_OK) {
    Serial.printf("[Bluetooth] Pairing failed: %d\n", static_cast<int>(result));
    return;
  }
  pairingMode = true;
  pairingEndsAt = millis() + PAIRING_MODE_MS;
  Serial.println("[Bluetooth] Pairing enabled for 120 seconds");
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
      ++buttonClickCount;
      if (buttonClickCount == 3) {
        buttonClickCount = 0;
        toggleWebInterface();
      }
    }
  }

  if (buttonStablePressed && !buttonLongPressHandled &&
      now - buttonPressedAt >= PAIRING_LONG_PRESS_MS) {
    buttonLongPressHandled = true;
    buttonClickCount = 0;
    startPairingMode();
  }
  // One or two clicks deliberately have no effect.
  if (buttonClickCount > 0 &&
      now - lastButtonClickAt > MULTI_CLICK_TIMEOUT_MS) {
    buttonClickCount = 0;
  }
}

void updateLed() {
  const uint32_t now = millis();
  if (now - lastLedRefreshAt < LED_REFRESH_MS) return;
  lastLedRefreshAt = now;

  if (bluetoothFault) {
    setLed((now / 250) % 2 ? COLOR_ERROR : 0x000000);
    return;
  }
  if (detectedBalance == nullptr) {
    if (identificationError) {
      setLed((now / 400) % 2 ? COLOR_ERROR : 0x000000);
    } else {
      setLed(COLOR_IDENTIFYING);
    }
    return;
  }

  const bool connected = bluetoothStarted && SerialBT.hasClient();
  if (connected != previousClientState) {
    previousClientState = connected;
    Serial.printf("[Bluetooth] PC %s\n", connected ? "connected" : "disconnected");
    if (connected) stopPairingMode();
  }

  if (pairingMode) {
    setLed((now / PAIRING_BLINK_MS) % 2 ? COLOR_PAIRING : 0x000000);
  } else if (now - lastActivityAt < ACTIVITY_LED_MS) {
    setLed(COLOR_ACTIVITY);
  } else {
    setLed(connected ? COLOR_CONNECTED
                     : ((now / READY_BLINK_MS) % 2 ? COLOR_READY : 0x000000));
  }
}

}  // namespace

void setup() {
  Serial.begin(DEBUG_BAUD);
  Serial.setTimeout(5);
  delay(50);
  Serial.println();
  Serial.println("LabConnect Hub CDO universal firmware");
  Serial.println("Target: M5Stack ATOM Lite / M5Atom / ESP32 3.3.8");

  pinMode(BUTTON_PIN, INPUT);
  setLed(COLOR_IDENTIFYING);

  BalanceSerial.setRxBufferSize(512);
  BalanceSerial.setTimeout(5);
  BalanceSerial.begin(
      BALANCE_BAUD,
      BALANCE_SERIAL_CONFIG,
      BALANCE_RX_PIN,
      BALANCE_TX_PIN);
  Serial.printf(
      "RS-232: 9600/8N1, no flow control, RX=%d TX=%d\n",
      BALANCE_RX_PIN,
      BALANCE_TX_PIN);

  WiFi.mode(WIFI_OFF);
  identificationDeadline = millis() + 250;
  Serial.println("Identification: I10 then ?ID until CDO02..CDO06 is found");
  Serial.println("Button: hold 2.5 s for pairing; triple-click for read-only diagnostics");
}

void loop() {
  if (detectedBalance == nullptr) {
    updateIdentification();
  } else if (bluetoothStarted) {
    // Alternate directions on every loop and preserve every byte unchanged.
    forwardBalanceToBluetooth();
    forwardBluetoothToBalance();
  }

  updateButton();
  if (pairingMode &&
      static_cast<int32_t>(millis() - pairingEndsAt) >= 0) {
    stopPairingMode();
  }
  if (wifiEnabled) Web.handleClient();
  updateLed();
  delay(1);
}
