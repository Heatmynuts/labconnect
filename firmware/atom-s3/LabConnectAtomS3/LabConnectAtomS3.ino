#include <M5AtomS3.h>
#include <WiFi.h>
#include <WebSocketsServer.h>

// LabConnect Print - AtomS3 bridge
// Version: AtomS3 display + A&D identity queries (?TN / ?SN / ?ID)
// A&D profile:
// - tare: T
// - clear tare: PT:0 g
// - zero: RZ
// - request weight: Q
// Board: ESP32-S3 / M5Stack AtomS3
// Arduino libraries:
// - M5AtomS3 by M5Stack
// - WebSockets by Markus Sattler
//
// Wiring depends on your RS-232 level shifter.
// Do not connect RS-232 +/- voltage directly to ESP32 pins.

const char* AP_SSID = "LabConnect-Print";
const char* AP_PASSWORD = "labconnect";

IPAddress localIp(192, 168, 4, 1);
IPAddress gateway(192, 168, 4, 1);
IPAddress subnet(255, 255, 255, 0);

const uint8_t BALANCE_RX_PIN = 5;
const uint8_t BALANCE_TX_PIN = 6;

HardwareSerial BalanceSerial(1);
WebSocketsServer webSocket(80, "/ws");

String lineBuffer;
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

void setup() {
  Serial.begin(115200);
  delay(300);

  initScreen();

  BalanceSerial.begin(2400, SERIAL_7E1, BALANCE_RX_PIN, BALANCE_TX_PIN);

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(localIp, gateway, subnet);
  WiFi.softAP(AP_SSID, AP_PASSWORD);

  webSocket.begin();
  webSocket.onEvent(onWebSocketEvent);

  Serial.println("LabConnect AtomS3 bridge ready");
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
  runIdentityQuery();
  broadcastDeviceInfoIfNeeded();
  renderScreenIfNeeded();
}

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

void broadcastDeviceInfo() {
  String payload = "{\"type\":\"device\",\"kind\":\"balance\",\"name\":\"A&D ";
  payload += escapeJson(balanceType == "-" ? "FZ-i" : balanceType);
  payload += "\",\"model\":\"";
  payload += escapeJson(balanceType);
  payload += "\",\"serialNumber\":\"";
  payload += escapeJson(balanceSerial);
  payload += "\",\"deviceId\":\"";
  payload += escapeJson(balanceId);
  payload += "\",\"transport\":\"AtomS3 Wi-Fi\",\"ipAddress\":\"";
  payload += WiFi.softAPIP().toString();
  payload += "\",\"serial\":\"2400 7E1\"}";
  webSocket.broadcastTXT(payload);
  nextDeviceInfoBroadcastAt = millis() + DEVICE_INFO_BROADCAST_INTERVAL_MS;
}

void sendDeviceInfo(uint8_t clientId) {
  String payload = "{\"type\":\"device\",\"kind\":\"balance\",\"name\":\"A&D ";
  payload += escapeJson(balanceType == "-" ? "FZ-i" : balanceType);
  payload += "\",\"model\":\"";
  payload += escapeJson(balanceType);
  payload += "\",\"serialNumber\":\"";
  payload += escapeJson(balanceSerial);
  payload += "\",\"deviceId\":\"";
  payload += escapeJson(balanceId);
  payload += "\",\"transport\":\"AtomS3 Wi-Fi\",\"ipAddress\":\"";
  payload += WiFi.softAPIP().toString();
  payload += "\",\"serial\":\"2400 7E1\"}";
  webSocket.sendTXT(clientId, payload);
}

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
  handleCommand(message);
}

void handleCommand(const String& message) {
  if (message.indexOf("clear-tare") >= 0) {
    sendBalanceCommand("PT:0 g");
    return;
  }

  if (message.indexOf("tare") >= 0) {
    sendBalanceCommand("T");
    return;
  }

  if (message.indexOf("zero") >= 0) {
    sendBalanceCommand("RZ");
    return;
  }

  if (message.indexOf("request-weight") >= 0 || message.indexOf("weight") >= 0) {
    sendBalanceCommand("Q");
    return;
  }

  if (message.indexOf("print") >= 0) {
    sendBalanceCommand("P");
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

void sendBalanceCommand(const char* command) {
  Serial.print("BALANCE < ");
  Serial.println(command);
  BalanceSerial.print(command);
  BalanceSerial.print("\r\n");
}

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

  if (balanceType == "-") {
    sendIdentityQuery(QUERY_TYPE, "?TN");
    return;
  }

  if (balanceSerial == "-") {
    sendIdentityQuery(QUERY_SERIAL, "?SN");
    return;
  }

  if (balanceId == "-") {
    sendIdentityQuery(QUERY_ID, "?ID");
  }
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

bool isLikelyWeightLine(const String& line) {
  String value = line;
  value.trim();
  value.toUpperCase();
  return value.endsWith(" G") ||
         value.endsWith("G") ||
         value.startsWith("S ") ||
         value.startsWith("ST") ||
         value.startsWith("US") ||
         value.startsWith("SD") ||
         value.startsWith("QT");
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
  screen.drawString(compactLine(lastWeightLine, 12), 4, 25);

  screen.setTextSize(1);
  screen.setTextColor(TFT_CYAN, TFT_BLACK);
  screen.drawString("IP " + WiFi.softAPIP().toString(), 4, 52);
  screen.setTextColor(connectedClients > 0 ? TFT_GREEN : TFT_ORANGE, TFT_BLACK);
  screen.drawString("SUNMI " + String(connectedClients), 4, 64);

  screen.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  screen.drawString("TYPE " + balanceType, 4, 80);
  screen.drawString("SN   " + balanceSerial, 4, 92);
  screen.drawString("ID   " + balanceId, 4, 104);

  if (millis() - lastStreamAt < 1500) {
    screen.fillCircle(119, 119, 4, TFT_GREEN);
  } else {
    screen.drawCircle(119, 119, 4, TFT_DARKGREY);
  }

  screen.pushSprite(0, 0);
}
