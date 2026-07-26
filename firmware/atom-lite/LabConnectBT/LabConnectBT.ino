/*
 * LabConnect BT
 *
 * Transparent bidirectional bridge:
 *   A&D balance RS-232 <-> Atom Lite UART <-> Bluetooth Classic SPP <-> Windows COM
 *
 * Target: M5Stack ATOM Lite (ESP32-PICO-D4).
 * Do not build this firmware for an AtomS3: the ESP32-S3 has no Bluetooth Classic SPP.
 *
 * The M5Stack RS-232 adapter provides the RS-232 level conversion. Never connect
 * an RS-232 signal directly to an ESP32 GPIO.
 */

#include <Arduino.h>
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BluetoothSerial.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp32-hal-rgb-led.h>

#include "web_ui.h"

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is disabled in the selected ESP32 board configuration.
#endif

#if !defined(CONFIG_BT_SPP_ENABLED)
#error Bluetooth Classic SPP is unavailable. Select the original M5Stack ATOM board, not an AtomS3.
#endif

namespace {

constexpr uint32_t DEBUG_BAUD = 115200;

constexpr int BALANCE_RX_PIN = 22;
constexpr int BALANCE_TX_PIN = 19;
constexpr int LED_PIN = 27;
constexpr int BUTTON_PIN = 39;

constexpr size_t BRIDGE_BUFFER_SIZE = 256;
constexpr uint32_t ACTIVITY_LED_MS = 80;
constexpr uint32_t LED_REFRESH_MS = 20;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 30;
constexpr uint32_t MULTI_CLICK_TIMEOUT_MS = 900;
constexpr uint32_t PAIRING_LONG_PRESS_MS = 2500;
constexpr uint32_t PAIRING_MODE_MS = 120000;
constexpr uint32_t PAIRING_BLINK_MS = 350;
constexpr uint32_t BLE_RECONNECT_MODE_MS = 30000;
constexpr size_t LOG_CAPACITY = 72;
constexpr size_t LOG_DATA_SIZE = 96;
constexpr size_t BLE_NOTIFY_CHUNK_SIZE = 20;
constexpr size_t BLE_RX_QUEUE_SIZE = 512;

constexpr char BLE_SERVICE_UUID[] = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
constexpr char BLE_RX_UUID[] = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";
constexpr char BLE_TX_UUID[] = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";

constexpr uint32_t COLOR_DISCONNECTED = 0x180000;
constexpr uint32_t COLOR_CONNECTED = 0x000018;
constexpr uint32_t COLOR_ACTIVITY = 0x001800;
constexpr uint32_t COLOR_PAIRING = 0x000030;

HardwareSerial BalanceSerial(1);
BluetoothSerial SerialBT;
BLEServer *BleServer = nullptr;
BLECharacteristic *BleTxCharacteristic = nullptr;
BLEAdvertising *BleAdvertising = nullptr;
Preferences Settings;
WebServer Web(80);
QueueHandle_t bleRxQueue = nullptr;

uint8_t bridgeBuffer[BRIDGE_BUFFER_SIZE];

struct DeviceConfig {
  String bluetoothName;
  String wifiName;
  String wifiPassword;
  uint32_t baud = 2400;
  uint8_t dataBits = 7;
  uint8_t parity = 1;  // 0=None, 1=Even, 2=Odd.
  uint8_t stopBits = 1;
  bool swapRxTx = false;
};

DeviceConfig config;
bool previousClientState = false;
uint32_t lastActivityAt = 0;
uint32_t lastLedRefreshAt = 0;
uint32_t displayedColor = UINT32_MAX;
uint32_t restartAt = 0;
bool wifiEnabled = false;
bool webRoutesConfigured = false;
bool buttonRawPressed = false;
bool buttonStablePressed = false;
uint8_t buttonClickCount = 0;
uint32_t buttonChangedAt = 0;
uint32_t lastButtonClickAt = 0;
uint32_t buttonPressedAt = 0;
bool buttonLongPressHandled = false;
bool pairingMode = false;
uint32_t pairingEndsAt = 0;
volatile bool bleClientConnected = false;
volatile bool bleDisconnectedEvent = false;
bool bleReconnectAdvertising = false;
uint32_t bleReconnectEndsAt = 0;

enum class TrafficDirection : uint8_t {
  BalanceToPc,
  PcToBalance,
  WebToBalance,
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

void noteActivity();

class LabConnectBleServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *server) override {
    bleClientConnected = true;
  }

  void onDisconnect(BLEServer *server) override {
    bleClientConnected = false;
    bleDisconnectedEvent = true;
  }
};

class LabConnectBleRxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *characteristic) override {
    const String value = characteristic->getValue();
    if (bleRxQueue == nullptr) return;
    for (size_t i = 0; i < value.length(); i++) {
      const uint8_t byte = static_cast<uint8_t>(value.charAt(i));
      xQueueSend(bleRxQueue, &byte, 0);
    }
  }
};

// Arduino-ESP32 3.3.x needs an explicit callback object on the notifying
// characteristic when Classic SPP and BLE share Bluedroid. Without it, macOS
// can subscribe successfully but incoming UART data is not delivered.
class LabConnectBleTxCallbacks : public BLECharacteristicCallbacks {};

String makeDefaultBluetoothName() {
  const uint32_t suffix = static_cast<uint32_t>(ESP.getEfuseMac() & 0xFFFF);
  char name[24];
  snprintf(name, sizeof(name), "LabConnect-BT-%04X", static_cast<unsigned int>(suffix));
  return String(name);
}

String makeDefaultWifiName(const String &bluetoothName) {
  return bluetoothName + "-WiFi";
}

bool isValidName(const String &value, size_t maxLength) {
  if (value.isEmpty() || value.length() > maxLength) return false;
  for (size_t i = 0; i < value.length(); i++) {
    const uint8_t c = static_cast<uint8_t>(value.charAt(i));
    if (c < 32 || c > 126) return false;
  }
  return true;
}

void setDefaultConfig() {
  config.bluetoothName = makeDefaultBluetoothName();
  config.wifiName = makeDefaultWifiName(config.bluetoothName);
  config.wifiPassword = "labconnect";
  config.baud = 2400;
  config.dataBits = 7;
  config.parity = 1;
  config.stopBits = 1;
  config.swapRxTx = false;
}

void validateConfig() {
  const String defaultBluetoothName = makeDefaultBluetoothName();
  if (!isValidName(config.bluetoothName, 28)) {
    config.bluetoothName = defaultBluetoothName;
  }
  if (!isValidName(config.wifiName, 32)) {
    config.wifiName = makeDefaultWifiName(config.bluetoothName);
  }
  if (config.wifiPassword.length() < 8 || config.wifiPassword.length() > 63) {
    config.wifiPassword = "labconnect";
  }
  if (config.baud < 300 || config.baud > 115200) config.baud = 2400;
  if (config.dataBits != 7 && config.dataBits != 8) config.dataBits = 7;
  if (config.parity > 2) config.parity = 1;
  if (config.stopBits != 1 && config.stopBits != 2) config.stopBits = 1;
}

void loadConfig() {
  setDefaultConfig();
  Settings.begin("labconnect-bt", true);
  config.bluetoothName = Settings.getString("bt-name", config.bluetoothName);
  config.wifiName = Settings.getString("wifi-name", config.wifiName);
  config.wifiPassword = Settings.getString("wifi-pass", config.wifiPassword);
  config.baud = Settings.getULong("baud", config.baud);
  config.dataBits = Settings.getUChar("bits", config.dataBits);
  config.parity = Settings.getUChar("parity", config.parity);
  config.stopBits = Settings.getUChar("stop", config.stopBits);
  config.swapRxTx = Settings.getBool("swap", config.swapRxTx);
  Settings.end();
  validateConfig();
}

void saveConfig() {
  Settings.begin("labconnect-bt", false);
  Settings.putString("bt-name", config.bluetoothName);
  Settings.putString("wifi-name", config.wifiName);
  Settings.putString("wifi-pass", config.wifiPassword);
  Settings.putULong("baud", config.baud);
  Settings.putUChar("bits", config.dataBits);
  Settings.putUChar("parity", config.parity);
  Settings.putUChar("stop", config.stopBits);
  Settings.putBool("swap", config.swapRxTx);
  Settings.end();
}

uint32_t serialConfig() {
  const bool twoStops = config.stopBits == 2;
  if (config.dataBits == 7) {
    if (config.parity == 1) return twoStops ? SERIAL_7E2 : SERIAL_7E1;
    if (config.parity == 2) return twoStops ? SERIAL_7O2 : SERIAL_7O1;
    return twoStops ? SERIAL_7N2 : SERIAL_7N1;
  }
  if (config.parity == 1) return twoStops ? SERIAL_8E2 : SERIAL_8E1;
  if (config.parity == 2) return twoStops ? SERIAL_8O2 : SERIAL_8O1;
  return twoStops ? SERIAL_8N2 : SERIAL_8N1;
}

char parityName() {
  return config.parity == 1 ? 'E' : config.parity == 2 ? 'O' : 'N';
}

int balanceRxPin() {
  return config.swapRxTx ? BALANCE_TX_PIN : BALANCE_RX_PIN;
}

int balanceTxPin() {
  return config.swapRxTx ? BALANCE_RX_PIN : BALANCE_TX_PIN;
}

void recordTraffic(TrafficDirection direction, const uint8_t *data, size_t length) {
  while (length > 0) {
    const size_t partLength = min(length, LOG_DATA_SIZE);
    TrafficEvent &event = trafficLog[trafficHead];
    event.sequence = nextSequence++;
    event.timestamp = millis();
    event.direction = direction;
    event.length = static_cast<uint8_t>(partLength);
    memcpy(event.data, data, partLength);

    trafficHead = (trafficHead + 1) % LOG_CAPACITY;
    if (trafficCount < LOG_CAPACITY) trafficCount++;
    data += partLength;
    length -= partLength;
  }
}

const char *directionName(TrafficDirection direction) {
  switch (direction) {
    case TrafficDirection::BalanceToPc: return "rx";
    case TrafficDirection::PcToBalance: return "tx";
    case TrafficDirection::WebToBalance: return "web";
  }
  return "?";
}

void appendJsonText(String &json, const uint8_t *data, size_t length) {
  static const char hex[] = "0123456789ABCDEF";
  json += '"';
  for (size_t i = 0; i < length; i++) {
    const uint8_t value = data[i];
    if (value == '\r') json += "\\\\r";
    else if (value == '\n') json += "\\\\n";
    else if (value == '\t') json += "\\\\t";
    else if (value == '"' || value == '\\') {
      json += '\\';
      json += static_cast<char>(value);
    } else if (value >= 32 && value <= 126) {
      json += static_cast<char>(value);
    } else {
      json += "\\\\x";
      json += hex[value >> 4];
      json += hex[value & 0x0F];
    }
  }
  json += '"';
}

void appendJsonHex(String &json, const uint8_t *data, size_t length) {
  static const char hex[] = "0123456789ABCDEF";
  json += '"';
  for (size_t i = 0; i < length; i++) {
    if (i) json += ' ';
    json += hex[data[i] >> 4];
    json += hex[data[i] & 0x0F];
  }
  json += '"';
}

void appendJsonString(String &json, const String &value) {
  json += '"';
  for (size_t i = 0; i < value.length(); i++) {
    const char c = value.charAt(i);
    if (c == '"' || c == '\\') json += '\\';
    json += c;
  }
  json += '"';
}

void handleGetConfig() {
  String json;
  json.reserve(384);
  json = "{\"bluetoothName\":";
  appendJsonString(json, config.bluetoothName);
  json += ",\"wifiName\":";
  appendJsonString(json, config.wifiName);
  json += ",\"wifiPassword\":";
  appendJsonString(json, config.wifiPassword);
  json += ",\"baud\":";
  json += config.baud;
  json += ",\"dataBits\":";
  json += static_cast<unsigned int>(config.dataBits);
  json += ",\"parity\":";
  json += static_cast<unsigned int>(config.parity);
  json += ",\"stopBits\":";
  json += static_cast<unsigned int>(config.stopBits);
  json += ",\"swapRxTx\":";
  json += config.swapRxTx ? "true" : "false";
  json += ",\"physicalRx\":";
  json += BALANCE_RX_PIN;
  json += ",\"physicalTx\":";
  json += BALANCE_TX_PIN;
  json += '}';
  Web.send(200, "application/json", json);
}

void sendConfigError(const char *message) {
  String json = "{\"error\":";
  appendJsonString(json, String(message));
  json += '}';
  Web.send(400, "application/json", json);
}

bool parseUnsignedArg(const char *name, uint32_t &value) {
  if (!Web.hasArg(name)) return false;
  const String input = Web.arg(name);
  if (input.isEmpty()) return false;
  char *end = nullptr;
  const unsigned long parsed = strtoul(input.c_str(), &end, 10);
  if (*end != '\0') return false;
  value = static_cast<uint32_t>(parsed);
  return true;
}

void handleSaveConfig() {
  if (!Web.hasArg("bluetoothName") || !Web.hasArg("wifiName") ||
      !Web.hasArg("wifiPassword")) {
    sendConfigError("Paramètres incomplets");
    return;
  }

  DeviceConfig candidate = config;
  candidate.bluetoothName = Web.arg("bluetoothName");
  candidate.bluetoothName.trim();
  candidate.wifiName = Web.arg("wifiName");
  candidate.wifiName.trim();
  candidate.wifiPassword = Web.arg("wifiPassword");

  uint32_t value = 0;
  if (!parseUnsignedArg("baud", value) || value < 300 || value > 115200) {
    sendConfigError("Débit série invalide (300 à 115200 bauds)");
    return;
  }
  candidate.baud = value;
  if (!parseUnsignedArg("dataBits", value) || (value != 7 && value != 8)) {
    sendConfigError("Nombre de bits invalide");
    return;
  }
  candidate.dataBits = static_cast<uint8_t>(value);
  if (!parseUnsignedArg("parity", value) || value > 2) {
    sendConfigError("Parité invalide");
    return;
  }
  candidate.parity = static_cast<uint8_t>(value);
  if (!parseUnsignedArg("stopBits", value) || (value != 1 && value != 2)) {
    sendConfigError("Nombre de bits d’arrêt invalide");
    return;
  }
  candidate.stopBits = static_cast<uint8_t>(value);
  candidate.swapRxTx = Web.hasArg("swapRxTx") && Web.arg("swapRxTx") == "1";

  if (!isValidName(candidate.bluetoothName, 28)) {
    sendConfigError("Nom Bluetooth invalide (1 à 28 caractères ASCII)");
    return;
  }
  if (!isValidName(candidate.wifiName, 32)) {
    sendConfigError("Nom Wi-Fi invalide (1 à 32 caractères ASCII)");
    return;
  }
  if (candidate.wifiPassword.length() < 8 || candidate.wifiPassword.length() > 63) {
    sendConfigError("Le mot de passe Wi-Fi doit contenir 8 à 63 caractères");
    return;
  }

  config = candidate;
  saveConfig();
  Web.send(200, "application/json", "{\"ok\":true,\"restart\":true}");
  restartAt = millis() + 1200;
}

void handleResetConfig() {
  setDefaultConfig();
  saveConfig();
  Web.send(200, "application/json", "{\"ok\":true,\"restart\":true}");
  restartAt = millis() + 1200;
}

void handleEvents() {
  const uint32_t after = Web.hasArg("after")
      ? static_cast<uint32_t>(strtoul(Web.arg("after").c_str(), nullptr, 10))
      : 0;

  String json;
  json.reserve(8192);
  json = "{\"bt\":";
  json += (SerialBT.hasClient() || bleClientConnected) ? "true" : "false";
  json += ",\"ssid\":";
  appendJsonString(json, config.wifiName);
  json += ",\"uart\":\"";
  json += config.baud;
  json += '/';
  json += static_cast<unsigned int>(config.dataBits);
  json += parityName();
  json += static_cast<unsigned int>(config.stopBits);
  json += "\",\"events\":[";

  bool first = true;
  const size_t oldest = (trafficHead + LOG_CAPACITY - trafficCount) % LOG_CAPACITY;
  size_t sent = 0;
  for (size_t i = 0; i < trafficCount && sent < 24; i++) {
    const TrafficEvent &event = trafficLog[(oldest + i) % LOG_CAPACITY];
    if (event.sequence <= after) continue;
    if (!first) json += ',';
    first = false;
    sent++;
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
  Web.send(200, "application/json", json);
}

bool parseHexCommand(const String &input, uint8_t *output, size_t &length) {
  length = 0;
  int highNibble = -1;
  for (size_t i = 0; i < input.length(); i++) {
    const char c = input.charAt(i);
    if (c == ' ' || c == ':' || c == '-' || c == '\t') continue;
    int value = -1;
    if (c >= '0' && c <= '9') value = c - '0';
    else if (c >= 'a' && c <= 'f') value = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') value = c - 'A' + 10;
    else return false;

    if (highNibble < 0) {
      highNibble = value;
    } else {
      if (length >= BRIDGE_BUFFER_SIZE - 2) return false;
      output[length++] = static_cast<uint8_t>((highNibble << 4) | value);
      highNibble = -1;
    }
  }
  return highNibble < 0 && length > 0;
}

void handleSendCommand() {
  if (!Web.hasArg("command")) {
    Web.send(400, "application/json", "{\"error\":\"Commande manquante\"}");
    return;
  }

  const String command = Web.arg("command");
  const String format = Web.arg("format");
  const String ending = Web.arg("ending");
  size_t length = 0;

  if (format == "hex") {
    if (!parseHexCommand(command, bridgeBuffer, length)) {
      Web.send(400, "application/json", "{\"error\":\"Hexadécimal invalide ou trop long\"}");
      return;
    }
  } else {
    if (command.length() > BRIDGE_BUFFER_SIZE - 2) {
      Web.send(400, "application/json", "{\"error\":\"Commande trop longue\"}");
      return;
    }
    length = command.length();
    memcpy(bridgeBuffer, command.c_str(), length);
  }

  if (ending == "cr" || ending == "crlf") bridgeBuffer[length++] = '\r';
  if (ending == "lf" || ending == "crlf") bridgeBuffer[length++] = '\n';

  const size_t written = BalanceSerial.write(bridgeBuffer, length);
  BalanceSerial.flush();
  recordTraffic(TrafficDirection::WebToBalance, bridgeBuffer, written);
  noteActivity();

  String response = "{\"bytes\":";
  response += written;
  response += '}';
  Web.send(written == length ? 200 : 500, "application/json", response);
}

void configureWebRoutes() {
  if (webRoutesConfigured) return;
  webRoutesConfigured = true;

  Web.on("/", HTTP_GET, []() {
    Web.send_P(200, "text/html; charset=utf-8", LABCONNECT_WEB_UI);
  });
  Web.on("/api/events", HTTP_GET, handleEvents);
  Web.on("/api/send", HTTP_POST, handleSendCommand);
  Web.on("/api/config", HTTP_GET, handleGetConfig);
  Web.on("/api/config", HTTP_POST, handleSaveConfig);
  Web.on("/api/config/reset", HTTP_POST, handleResetConfig);
  Web.onNotFound([]() {
    Web.sendHeader("Location", "/", true);
    Web.send(302, "text/plain", "");
  });
}

void enableWebInterface() {
  if (wifiEnabled) return;
  configureWebRoutes();
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  if (!WiFi.softAP(config.wifiName.c_str(), config.wifiPassword.c_str())) {
    Serial.println("Wi-Fi access point initialization failed");
    WiFi.mode(WIFI_OFF);
    return;
  }

  Web.begin();
  wifiEnabled = true;

  Serial.printf("Wi-Fi AP: %s\n", config.wifiName.c_str());
  Serial.printf("Wi-Fi password: %s\n", config.wifiPassword.c_str());
  Serial.printf("Web interface: http://%s/\n", WiFi.softAPIP().toString().c_str());
}

void disableWebInterface() {
  if (!wifiEnabled) return;
  Web.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  wifiEnabled = false;
  Serial.println("Wi-Fi/web: disabled");
}

void toggleWebInterface() {
  if (wifiEnabled) disableWebInterface();
  else enableWebInterface();
}

void stopPairingMode() {
  if (!pairingMode) return;
  esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
  if (BleAdvertising != nullptr) BleAdvertising->stop();
  pairingMode = false;
  pairingEndsAt = 0;
  Serial.println("Bluetooth pairing mode: disabled");
}

void startPairingMode() {
  if (SerialBT.hasClient() || bleClientConnected) {
    Serial.println("Bluetooth pairing mode ignored: a client is already connected");
    return;
  }
  const esp_err_t result =
      esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
  if (result != ESP_OK) {
    Serial.printf("Bluetooth pairing mode failed: %d\n", static_cast<int>(result));
    return;
  }
  pairingMode = true;
  bleReconnectAdvertising = false;
  bleReconnectEndsAt = 0;
  pairingEndsAt = millis() + PAIRING_MODE_MS;
  if (BleAdvertising != nullptr) BleAdvertising->start();
  Serial.println("Bluetooth pairing mode: enabled for 120 seconds");
}

void updateBleReconnectAdvertising() {
  if (bleDisconnectedEvent) {
    bleDisconnectedEvent = false;
    if (!pairingMode && BleAdvertising != nullptr) {
      BleAdvertising->start();
      bleReconnectAdvertising = true;
      bleReconnectEndsAt = millis() + BLE_RECONNECT_MODE_MS;
      Serial.println("Bluetooth Mac reconnect window: enabled for 30 seconds");
    }
  }

  if (bleClientConnected && bleReconnectAdvertising) {
    if (BleAdvertising != nullptr) BleAdvertising->stop();
    bleReconnectAdvertising = false;
    bleReconnectEndsAt = 0;
  } else if (bleReconnectAdvertising &&
             static_cast<int32_t>(millis() - bleReconnectEndsAt) >= 0) {
    if (BleAdvertising != nullptr) BleAdvertising->stop();
    bleReconnectAdvertising = false;
    bleReconnectEndsAt = 0;
    Serial.println("Bluetooth Mac reconnect window: disabled");
  }
}

bool startBleInterface() {
  const String bleName = config.bluetoothName + "-Mac";
  if (!BLEDevice::init(bleName)) return false;

  bleRxQueue = xQueueCreate(BLE_RX_QUEUE_SIZE, sizeof(uint8_t));
  if (bleRxQueue == nullptr) return false;

  BleServer = BLEDevice::createServer();
  if (BleServer == nullptr) return false;
  BleServer->setCallbacks(new LabConnectBleServerCallbacks());

  BLEService *service = BleServer->createService(BLE_SERVICE_UUID);
  if (service == nullptr) return false;

  BleTxCharacteristic = service->createCharacteristic(
      BLE_TX_UUID,
      BLECharacteristic::PROPERTY_NOTIFY);
  BleTxCharacteristic->setCallbacks(new LabConnectBleTxCallbacks());
  BleTxCharacteristic->addDescriptor(new BLE2902());

  BLECharacteristic *rxCharacteristic = service->createCharacteristic(
      BLE_RX_UUID,
      BLECharacteristic::PROPERTY_WRITE |
          BLECharacteristic::PROPERTY_WRITE_NR);
  rxCharacteristic->setCallbacks(new LabConnectBleRxCallbacks());

  service->start();
  BleAdvertising = BleServer->getAdvertising();
  BleAdvertising->addServiceUUID(BLE_SERVICE_UUID);
  BleAdvertising->setScanResponse(true);
  BleAdvertising->stop();
  return true;
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
  if (buttonClickCount > 0 &&
      now - lastButtonClickAt > MULTI_CLICK_TIMEOUT_MS) {
    buttonClickCount = 0;
  }
}

void setLed(uint32_t color) {
  if (color == displayedColor) return;
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

void updateStatus() {
  const uint32_t now = millis();
  if (now - lastLedRefreshAt < LED_REFRESH_MS) return;
  lastLedRefreshAt = now;

  const bool clientConnected = SerialBT.hasClient() || bleClientConnected;
  if (clientConnected != previousClientState) {
    previousClientState = clientConnected;
    Serial.printf("Bluetooth client %s\n", clientConnected ? "connected" : "disconnected");
    if (clientConnected) stopPairingMode();
  }

  if (pairingMode) {
    setLed((now / PAIRING_BLINK_MS) % 2 ? COLOR_PAIRING : 0x000000);
  } else if (now - lastActivityAt < ACTIVITY_LED_MS) {
    setLed(COLOR_ACTIVITY);
  } else {
    setLed(clientConnected ? COLOR_CONNECTED : COLOR_DISCONNECTED);
  }
}

void forwardBalanceToBluetooth() {
  const int available = BalanceSerial.available();
  if (available <= 0) return;

  const size_t requested =
      min(static_cast<size_t>(available), static_cast<size_t>(BRIDGE_BUFFER_SIZE));
  const size_t received = BalanceSerial.readBytes(bridgeBuffer, requested);
  if (received == 0) return;

  // Data emitted while no PC is connected is intentionally discarded. Keeping
  // it would deliver stale measurements when Windows reconnects.
  if (SerialBT.hasClient()) {
    SerialBT.write(bridgeBuffer, received);
  }
  if (bleClientConnected && BleTxCharacteristic != nullptr) {
    for (size_t offset = 0; offset < received; offset += BLE_NOTIFY_CHUNK_SIZE) {
      const size_t chunkLength = min(BLE_NOTIFY_CHUNK_SIZE, received - offset);
      BleTxCharacteristic->setValue(bridgeBuffer + offset, chunkLength);
      BleTxCharacteristic->notify();
    }
  }
  recordTraffic(TrafficDirection::BalanceToPc, bridgeBuffer, received);
  noteActivity();
}

void forwardBluetoothToBalance() {
  const int available = SerialBT.available();
  if (available <= 0) return;

  const size_t requested =
      min(static_cast<size_t>(available), static_cast<size_t>(BRIDGE_BUFFER_SIZE));
  const size_t received = SerialBT.readBytes(bridgeBuffer, requested);
  if (received == 0) return;

  BalanceSerial.write(bridgeBuffer, received);
  BalanceSerial.flush();
  recordTraffic(TrafficDirection::PcToBalance, bridgeBuffer, received);
  noteActivity();
}

void forwardBleToBalance() {
  if (bleRxQueue == nullptr) return;

  size_t received = 0;
  uint8_t byte = 0;
  while (received < BRIDGE_BUFFER_SIZE &&
         xQueueReceive(bleRxQueue, &byte, 0) == pdTRUE) {
    bridgeBuffer[received++] = byte;
  }
  if (received == 0) return;

  BalanceSerial.write(bridgeBuffer, received);
  BalanceSerial.flush();
  recordTraffic(TrafficDirection::PcToBalance, bridgeBuffer, received);
  noteActivity();
}

}  // namespace

void setup() {
  Serial.begin(DEBUG_BAUD);
  Serial.setTimeout(5);
  delay(50);
  Serial.println();
  Serial.println("LabConnect BT boot");

  setLed(COLOR_DISCONNECTED);
  pinMode(BUTTON_PIN, INPUT);
  loadConfig();

  Serial.println("[boot] UART balance: init");
  BalanceSerial.setRxBufferSize(512);
  BalanceSerial.setTimeout(5);
  BalanceSerial.begin(
      config.baud,
      serialConfig(),
      balanceRxPin(),
      balanceTxPin());
  Serial.println("[boot] UART balance: ready");

  // The ATOM Lite has no display or keyboard. Declaring NoInputNoOutput lets
  // Windows authenticate the pairing without waiting for an impossible
  // confirmation on the ATOM.
  Serial.println("[boot] Bluetooth SPP: init");
  SerialBT.enableSSP(false, false);
  if (!SerialBT.begin(config.bluetoothName, false, false)) {
    Serial.println("Bluetooth SPP initialization failed");
    while (true) {
      setLed((millis() / 250) % 2 ? COLOR_DISCONNECTED : 0x000000);
      delay(10);
    }
  }
  SerialBT.setTimeout(5);
  esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
  Serial.println("[boot] Bluetooth SPP: ready");

  Serial.println("[boot] Bluetooth Mac: init");
  if (!startBleInterface()) {
    Serial.println("Bluetooth Mac initialization failed");
  } else {
    Serial.println("[boot] Bluetooth Mac: ready");
  }

  WiFi.mode(WIFI_OFF);
  Serial.println("[boot] Wi-Fi/web: disabled (triple-click button to enable)");

  Serial.println();
  Serial.println("LabConnect BT ready");
  Serial.printf("Bluetooth name: %s\n", config.bluetoothName.c_str());
  Serial.println("Bluetooth mode: Classic SPP / Windows virtual COM");
  Serial.printf("Bluetooth Mac name: %s-Mac\n", config.bluetoothName.c_str());
  Serial.println("Bluetooth pairing: hold ATOM button for 2.5 seconds");
  Serial.printf(
      "Balance UART: %lu/%u%c%u RX=%d TX=%d%s\n",
      static_cast<unsigned long>(config.baud),
      config.dataBits,
      parityName(),
      config.stopBits,
      balanceRxPin(),
      balanceTxPin(),
      config.swapRxTx ? " (swapped)" : "");
  Serial.println("Web monitor: triple-click the ATOM button to enable Wi-Fi");
}

void loop() {
  // Alternate directions every loop. At 2400 baud, the 256-byte buffers provide
  // ample margin while preserving every byte and every original line ending.
  forwardBalanceToBluetooth();
  forwardBluetoothToBalance();
  forwardBleToBalance();
  updateButton();
  updateBleReconnectAdvertising();
  if (pairingMode &&
      static_cast<int32_t>(millis() - pairingEndsAt) >= 0) {
    stopPairingMode();
  }
  if (wifiEnabled) Web.handleClient();
  if (restartAt != 0 && static_cast<int32_t>(millis() - restartAt) >= 0) {
    ESP.restart();
  }
  updateStatus();
  delay(1);
}
