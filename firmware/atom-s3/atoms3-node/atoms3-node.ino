#include <M5Unified.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Preferences.h>
#include <ArduinoJson.h>

// =====================================================
// PROTOCOLE
// =====================================================
#define PROTO_MAGIC   0xBD
#define PROTO_VERSION 0x01

#define MSG_DISCOVER        0x01
#define MSG_ANNOUNCE        0x02
#define MSG_COMMAND         0x03
#define MSG_RESPONSE        0x04
#define MSG_STATUS          0x05
#define MSG_PING            0x06
#define MSG_PONG            0x07
#define MSG_HEARTBEAT       0x08
#define MSG_NODE_INFO_REQ   0x09
#define MSG_NODE_INFO_RESP  0x0A
#define MSG_CONFIG_REQ      0x0B
#define MSG_CONFIG_RESP     0x0C
#define MSG_CONFIG_SET      0x0D
#define MSG_CONFIG_ACK      0x0E

// Marques balance
#define BRAND_AD        0
#define BRAND_METTLER   1
#define BRAND_SARTORIUS 2

typedef struct __attribute__((packed)) {
  uint8_t  magic;
  uint8_t  version;
  uint8_t  type;
  uint8_t  nodeId;
  uint16_t seq;
  uint8_t  reserved;
  uint8_t  crc;
  char     payload[200];
} msg_t;
static_assert(sizeof(msg_t) == 208, "msg_t must be 208 bytes");

// =====================================================
// Theme iOS (raccord avec le hub CoreS3)
// =====================================================
#define COLOR_BG          0xF7BE
#define COLOR_SURFACE     0xFFFF
#define COLOR_DIVIDER     0xDEFB
#define COLOR_PRIMARY     0x041F
#define COLOR_PRIMARY_BG  0xE71F
#define COLOR_SUCCESS     0x36E5
#define COLOR_WARNING     0xFCA0
#define COLOR_ERROR       0xF986
#define COLOR_TEXT        0x0000
#define COLOR_TEXT_DIM    0x8C92

#define SCREEN_W  128
#define SCREEN_H  128

// =====================================================
// Config par défaut (fallback si NVS vide)
// =====================================================
#define DEFAULT_NODE_ID      1
#define DEFAULT_NODE_NAME    "BAL-01"
#define DEFAULT_BRAND        BRAND_AD
#define DEFAULT_BAUD         2400
#define DEFAULT_PARITY       1        // Even
#define DEFAULT_DATABITS     7
#define DEFAULT_STOPBITS     1
#define DEFAULT_RX           5
#define DEFAULT_TX           6
#define DEFAULT_SWAP         false
#define DEFAULT_POLL_CMD     "Q"
#define DEFAULT_LINE_TIMEOUT 300
#define DEFAULT_ZERO_CMD     "Z"
#define DEFAULT_CAPACITY     5000.0f
#define DEFAULT_RESOLUTION   0.01f

#define HEARTBEAT_MS       2000
#define DISPLAY_REFRESH_MS 200
#define BAL_TIMEOUT_MS     2000
#define VALUE_TTL_MS       2000
#define IDENTITY_QUERY_DELAY_MS   700
#define IDENTITY_QUERY_TIMEOUT_MS 1800

// =====================================================
// Config runtime (chargée depuis NVS)
// =====================================================
static uint8_t  cfg_nodeId      = DEFAULT_NODE_ID;
static char     cfg_name[32]    = DEFAULT_NODE_NAME;
static uint8_t  cfg_brand       = DEFAULT_BRAND;
static uint32_t cfg_baud        = DEFAULT_BAUD;
static uint8_t  cfg_parity      = DEFAULT_PARITY;
static uint8_t  cfg_dataBits    = DEFAULT_DATABITS;
static uint8_t  cfg_stopBits    = DEFAULT_STOPBITS;
static uint8_t  cfg_rxPin       = DEFAULT_RX;
static uint8_t  cfg_txPin       = DEFAULT_TX;
static bool     cfg_swapRxTx    = DEFAULT_SWAP;
static char     cfg_label[32]   = "";
static char     cfg_pollCmd[16] = DEFAULT_POLL_CMD;
static uint16_t cfg_lineTimeout = DEFAULT_LINE_TIMEOUT;
static char     cfg_zeroCmd[16] = DEFAULT_ZERO_CMD;
static float    cfg_capacity    = DEFAULT_CAPACITY;
static float    cfg_resolution  = DEFAULT_RESOLUTION;

// =====================================================
// State
// =====================================================
static uint8_t  hubMac[6] = {0};
static bool     hubKnown = false;
static uint16_t txSeq = 0;

enum NodeState { N_IDLE, N_QUERYING };
static NodeState state = N_IDLE;
static unsigned long stateStart = 0;
static char  pendingCmd[16] = "";

static char  latestValue[64] = "";
static unsigned long latestValueTime = 0;
static char  lastCmdStatus[16] = "NONE";
static bool  balanceReady  = false;
static bool  autoQuery     = false;

enum IdentityQuery { ID_NONE, ID_TYPE, ID_SERIAL, ID_BALANCE_ID, ID_BALANCE_ID_ALT };
static IdentityQuery pendingIdentity = ID_NONE;
static unsigned long nextIdentityQueryAt = 0;
static unsigned long pendingIdentityStartedAt = 0;
static char balanceType[20] = "?";
static char balanceSerial[20] = "?";
static char balanceId[20] = "?";
static bool identityTypeDone = false;
static bool identitySerialDone = false;
static bool identityPrimaryIdTried = false;
static bool identityAltIdTried = false;

static char  balLineBuf[128];
static int   balLineLen = 0;
static bool  balLastWasCR = false;

static unsigned long lastDisplayUpdate = 0;
static unsigned long lastHeartbeat = 0;
static bool dirty = true;
static unsigned long identifyUntil = 0;

HardwareSerial balSerial(1);
Preferences    cfgPrefs;
M5Canvas       sprite(&M5.Display);

// =====================================================
// Config NVS
// =====================================================
uint32_t getSerialConfig() {
  // Construit la config SERIAL_xYZ depuis parity + dataBits
  if (cfg_dataBits == 7) {
    if (cfg_parity == 1) return SERIAL_7E1;
    if (cfg_parity == 2) return SERIAL_7O1;
    return SERIAL_7N1;
  } else {
    if (cfg_parity == 1) return SERIAL_8E1;
    if (cfg_parity == 2) return SERIAL_8O1;
    return SERIAL_8N1;
  }
}

void loadConfig() {
  cfgPrefs.begin("nodecfg", true);
  cfg_nodeId   = cfgPrefs.getUChar("id",    DEFAULT_NODE_ID);
  cfg_brand    = cfgPrefs.getUChar("brand", DEFAULT_BRAND);
  cfg_baud     = cfgPrefs.getULong("baud",  DEFAULT_BAUD);
  cfg_parity   = cfgPrefs.getUChar("parity", DEFAULT_PARITY);
  cfg_dataBits = cfgPrefs.getUChar("dbits",  DEFAULT_DATABITS);
  cfg_stopBits = cfgPrefs.getUChar("sbits",  DEFAULT_STOPBITS);
  cfg_rxPin    = cfgPrefs.getUChar("rxpin",  DEFAULT_RX);
  cfg_txPin    = cfgPrefs.getUChar("txpin",  DEFAULT_TX);
  cfg_swapRxTx = cfgPrefs.getBool ("swap",   DEFAULT_SWAP);
  cfg_lineTimeout = cfgPrefs.getUShort("timeout", DEFAULT_LINE_TIMEOUT);
  cfg_capacity = cfgPrefs.getFloat("cap", DEFAULT_CAPACITY);
  cfg_resolution = cfgPrefs.getFloat("res", DEFAULT_RESOLUTION);

  // Strings
  char tmp[32];
  cfgPrefs.getString("name", tmp, sizeof(tmp));
  if (tmp[0]) strncpy(cfg_name, tmp, sizeof(cfg_name) - 1);

  char tmpl[32];
  cfgPrefs.getString("label", tmpl, sizeof(tmpl));
  if (tmpl[0]) strncpy(cfg_label, tmpl, sizeof(cfg_label) - 1);

  char tmpc[16];
  cfgPrefs.getString("pollcmd", tmpc, sizeof(tmpc));
  if (tmpc[0]) strncpy(cfg_pollCmd, tmpc, sizeof(cfg_pollCmd) - 1);

  char tmpz[16];
  cfgPrefs.getString("zerocmd", tmpz, sizeof(tmpz));
  if (tmpz[0]) strncpy(cfg_zeroCmd, tmpz, sizeof(cfg_zeroCmd) - 1);
  if (!cfg_zeroCmd[0]) strlcpy(cfg_zeroCmd, DEFAULT_ZERO_CMD, sizeof(cfg_zeroCmd));
  if (cfg_capacity <= 0) cfg_capacity = DEFAULT_CAPACITY;
  if (cfg_resolution <= 0) cfg_resolution = DEFAULT_RESOLUTION;

  // Hub MAC (pour reconnexion après reboot)
  uint8_t savedMac[6] = {0};
  if (cfgPrefs.getBytes("hubmac", savedMac, 6) == 6) {
    bool valid = false;
    for (int i = 0; i < 6; i++) if (savedMac[i]) { valid = true; break; }
    if (valid) {
      memcpy(hubMac, savedMac, 6);
      hubKnown = true;
    }
  }
  cfgPrefs.end();
}

void saveConfig() {
  cfgPrefs.begin("nodecfg", false);
  cfgPrefs.putUChar ("id",      cfg_nodeId);
  cfgPrefs.putString("name",    cfg_name);
  cfgPrefs.putUChar ("brand",   cfg_brand);
  cfgPrefs.putULong ("baud",    cfg_baud);
  cfgPrefs.putUChar ("parity",  cfg_parity);
  cfgPrefs.putUChar ("dbits",   cfg_dataBits);
  cfgPrefs.putUChar ("sbits",   cfg_stopBits);
  cfgPrefs.putUChar ("rxpin",   cfg_rxPin);
  cfgPrefs.putUChar ("txpin",   cfg_txPin);
  cfgPrefs.putBool  ("swap",    cfg_swapRxTx);
  cfgPrefs.putString("label",   cfg_label);
  cfgPrefs.putString("pollcmd", cfg_pollCmd);
  cfgPrefs.putUShort("timeout", cfg_lineTimeout);
  cfgPrefs.putString("zerocmd", cfg_zeroCmd);
  cfgPrefs.putFloat("cap", cfg_capacity);
  cfgPrefs.putFloat("res", cfg_resolution);
  cfgPrefs.end();
}

void saveHubMac() {
  cfgPrefs.begin("nodecfg", false);
  cfgPrefs.putBytes("hubmac", hubMac, 6);
  cfgPrefs.end();
}

// =====================================================
// Utils CRC
// =====================================================
uint8_t crc8(const uint8_t* data, size_t len) {
  uint8_t crc = 0;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) {
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
    }
  }
  return crc;
}

uint8_t computeMsgCrc(const msg_t* m) {
  msg_t copy = *m;
  copy.crc = 0;
  return crc8((const uint8_t*)&copy, sizeof(copy));
}

// Stabilité selon la marque
bool isValueStable(const char* value) {
  if (!value || strncmp(value, "ERROR", 5) == 0) return false;
  if (cfg_brand == BRAND_METTLER) {
    // Réponse Mettler MT-SICS : "S S  value unit" (stable) ou "S D  ..." (instable)
    return (strlen(value) > 2 && value[0] == 'S' && value[1] == ' ' && value[2] == 'S');
  }
  // A&D / Sartorius : préfixe "US," = instable
  return (strncmp(value, "US", 2) != 0);
}

// Nettoie le préfixe status (ST,/ US,/ S S / S D  ...) → valeur numérique + unité
const char* stripStatusPrefix(const char* raw) {
  static char buf[64];
  if (!raw || !*raw) return "";
  const char* p = raw;
  while (*p && *p != '+' && *p != '-' && *p != '.' &&
         !(*p >= '0' && *p <= '9')) p++;
  if (*p == '\0') {
    strncpy(buf, raw, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    return buf;
  }
  int o = 0;
  bool prevSpace = false;
  while (*p && o < (int)sizeof(buf) - 1) {
    if (*p == ' ') {
      if (!prevSpace && o > 0) buf[o++] = ' ';
      prevSpace = true;
    } else {
      buf[o++] = *p;
      prevSpace = false;
    }
    p++;
  }
  while (o > 0 && buf[o - 1] == ' ') o--;
  buf[o] = 0;
  return buf;
}

bool isUnknownIdentity(const char* value) {
  return !value || value[0] == 0 || strcmp(value, "?") == 0;
}

bool isLikelyWeightLine(const char* raw) {
  if (!raw || !*raw) return false;

  char value[64];
  strlcpy(value, raw, sizeof(value));
  for (int i = 0; value[i]; i++) {
    if (value[i] >= 'a' && value[i] <= 'z') value[i] = (char)(value[i] - 'a' + 'A');
  }

  char* start = value;
  while (*start == ' ') start++;
  size_t len = strlen(start);
  while (len > 0 && start[len - 1] == ' ') start[--len] = 0;

  return strstr(start, " G") != nullptr ||
         (len > 0 && start[len - 1] == 'G') ||
         strncmp(start, "S ", 2) == 0 ||
         strncmp(start, "ST", 2) == 0 ||
         strncmp(start, "US", 2) == 0 ||
         strncmp(start, "SD", 2) == 0 ||
         strncmp(start, "QT", 2) == 0;
}

void compactIdentityLine(const char* raw, char* out, size_t outSize) {
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
  if (out[0] == 0) strlcpy(out, "?", outSize);
}

bool isAllZeroIdentity(const char* value) {
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

bool isKnownClientBalanceId(const char* id) {
  return id &&
         (strcmp(id, "02") == 0 || strcmp(id, "03") == 0 ||
          strcmp(id, "04") == 0 || strcmp(id, "05") == 0 ||
          strcmp(id, "06") == 0);
}

bool extractClientBalanceId(const char* raw, char* out, size_t outSize) {
  if (!raw || !out || outSize < 3) return false;

  char compact[64];
  compactIdentityLine(raw, compact, sizeof(compact));
  for (size_t i = 0; compact[i]; i++) {
    if (compact[i] >= 'a' && compact[i] <= 'z') compact[i] = (char)(compact[i] - 'a' + 'A');
  }

  const char* search = strstr(compact, "CDO");
  if (!search) search = compact;

  for (size_t i = 0; search[i] && search[i + 1]; i++) {
    if (search[i] < '0' || search[i] > '9' || search[i + 1] < '0' || search[i + 1] > '9') continue;
    char candidate[3] = {search[i], search[i + 1], 0};
    if (isKnownClientBalanceId(candidate)) {
      strlcpy(out, candidate, outSize);
      return true;
    }
  }

  return false;
}

bool extractQuotedIdentityValue(const char* raw, char* out, size_t outSize) {
  if (!raw || !out || outSize == 0) return false;
  out[0] = 0;

  const char* start = strchr(raw, '"');
  if (!start) return false;
  start++;
  const char* end = strchr(start, '"');
  if (!end || end <= start) return false;

  size_t len = (size_t)(end - start);
  if (len >= outSize) len = outSize - 1;
  memcpy(out, start, len);
  out[len] = 0;
  return len > 0;
}

const char* cdoLabel() {
  static char label[28];
  char id[20];
  if (extractClientBalanceId(balanceId, id, sizeof(id))) {
    snprintf(label, sizeof(label), "CDO %s", id);
  } else if (cfg_label[0]) {
    strlcpy(label, cfg_label, sizeof(label));
  } else {
    strlcpy(label, "CDO ?", sizeof(label));
  }
  return label;
}

int clientBrandForBalanceId(const char* id) {
  if (!id) return -1;
  if (strcmp(id, "02") == 0 || strcmp(id, "03") == 0 || strcmp(id, "05") == 0) return BRAND_AD;
  if (strcmp(id, "04") == 0 || strcmp(id, "06") == 0) return BRAND_METTLER;
  return -1;
}

void applyClientBrandMapping() {
  int mappedBrand = clientBrandForBalanceId(balanceId);
  if (mappedBrand < 0 || cfg_brand == mappedBrand) return;
  cfg_brand = (uint8_t)mappedBrand;
  saveConfig();
}

bool isPollingCommand(const char* cmd) {
  if (!cmd || !*cmd) return false;
  return strcmp(cmd, cfg_pollCmd) == 0 || strcmp(cmd, "Q") == 0;
}

bool isIdentifyCommand(const char* cmd) {
  return cmd && strcmp(cmd, "IDENTIFY") == 0;
}

// =====================================================
// Envoi message
// =====================================================
void sendMsg(const uint8_t* mac, uint8_t type, const char* payload) {
  msg_t m = {};
  m.magic   = PROTO_MAGIC;
  m.version = PROTO_VERSION;
  m.type    = type;
  m.nodeId  = cfg_nodeId;
  m.seq     = ++txSeq;
  if (payload) {
    strncpy(m.payload, payload, 199);
    m.payload[199] = 0;
  }
  m.crc = 0;
  m.crc = computeMsgCrc(&m);
  esp_now_send(mac, (uint8_t*)&m, sizeof(m));
}

void sendToHub(uint8_t type, const char* payload) {
  if (!hubKnown) return;
  sendMsg(hubMac, type, payload);
}

// =====================================================
// Validation message reçu
// =====================================================
bool validateMsg(const uint8_t* data, int len, msg_t& m) {
  if (len != sizeof(msg_t)) return false;
  memcpy(&m, data, sizeof(m));
  if (m.magic   != PROTO_MAGIC)   return false;
  if (m.version != PROTO_VERSION) return false;
  uint8_t recv = m.crc;
  m.crc = 0;
  uint8_t calc = crc8((const uint8_t*)&m, sizeof(m));
  if (calc != recv) return false;
  m.payload[199] = 0;
  return true;
}

// =====================================================
// Balance RS232
// =====================================================
void sendBalanceCmd(const char* cmd) {
  balSerial.print(cmd);
  balSerial.print("\r\n");
}

void sendIdentityQuery(IdentityQuery query, const char* cmd) {
  pendingIdentity = query;
  pendingIdentityStartedAt = millis();
  Serial.printf("BALANCE ID < %s\n", cmd);
  sendBalanceCmd(cmd);
}

bool captureIdentityResponse(const char* line) {
  if (pendingIdentity == ID_NONE || !line || !*line) return false;
  if (isLikelyWeightLine(line)) return false;

  if (pendingIdentity == ID_TYPE) {
    compactIdentityLine(line, balanceType, sizeof(balanceType));
    identityTypeDone = true;
  } else if (pendingIdentity == ID_SERIAL) {
    compactIdentityLine(line, balanceSerial, sizeof(balanceSerial));
    identitySerialDone = true;
  } else if (pendingIdentity == ID_BALANCE_ID || pendingIdentity == ID_BALANCE_ID_ALT) {
    char identitySource[64];
    const char* rawIdentity = line;
    if (pendingIdentity == ID_BALANCE_ID_ALT) {
      if (strncmp(line, "ID", 2) != 0) return false;
      if (extractQuotedIdentityValue(line, identitySource, sizeof(identitySource))) {
        rawIdentity = identitySource;
      }
    }
    if (!extractClientBalanceId(rawIdentity, balanceId, sizeof(balanceId))) {
      compactIdentityLine(rawIdentity, balanceId, sizeof(balanceId));
    }
    char id[20];
    if (extractClientBalanceId(balanceId, id, sizeof(id))) strlcpy(balanceId, id, sizeof(balanceId));
    if (isAllZeroIdentity(balanceId)) strlcpy(balanceId, "?", sizeof(balanceId));
    identityPrimaryIdTried = true;
    identityAltIdTried = true;
    applyClientBrandMapping();
  }

  Serial.printf("BALANCE ID > %s\n", line);
  pendingIdentity = ID_NONE;
  nextIdentityQueryAt = millis() + IDENTITY_QUERY_DELAY_MS;
  balanceReady = true;
  dirty = true;
  sendToHub(MSG_STATUS, "IDENTITY_UPDATED");
  return true;
}

void runIdentityQuery() {
  if (pendingIdentity != ID_NONE) {
    if (millis() - pendingIdentityStartedAt > IDENTITY_QUERY_TIMEOUT_MS) {
      if (pendingIdentity == ID_TYPE) {
        identityTypeDone = true;
      } else if (pendingIdentity == ID_SERIAL) {
        identitySerialDone = true;
      } else if (pendingIdentity == ID_BALANCE_ID) {
        identityPrimaryIdTried = true;
      } else if (pendingIdentity == ID_BALANCE_ID_ALT) {
        identityAltIdTried = true;
      }
      pendingIdentity = ID_NONE;
      nextIdentityQueryAt = millis() + IDENTITY_QUERY_DELAY_MS;
      dirty = true;
    }
    return;
  }

  if (state != N_IDLE || millis() < nextIdentityQueryAt) return;

  if (!identityTypeDone) {
    sendIdentityQuery(ID_TYPE, "?TN");
    return;
  }

  if (!identitySerialDone) {
    sendIdentityQuery(ID_SERIAL, "?SN");
    return;
  }

  if (isUnknownIdentity(balanceId)) {
    if (!identityPrimaryIdTried) {
      sendIdentityQuery(ID_BALANCE_ID, "?ID");
      return;
    }
    if (!identityAltIdTried) {
      sendIdentityQuery(ID_BALANCE_ID_ALT, "I10");
    }
  }
}

void onBalanceLine(const char* line) {
  if (!line || !*line) return;
  if (captureIdentityResponse(line)) return;

  bool wasReady = balanceReady;
  bool wasAuto  = autoQuery;
  balanceReady  = true;
  autoQuery     = false;

  if (state == N_QUERYING) {
    strncpy(lastCmdStatus, "OK", sizeof(lastCmdStatus) - 1);
    state = N_IDLE;
  }

  if (!wasAuto) {
    // Query explicite (bouton ou commande hub) → valeur visible + envoi hub
    strncpy(latestValue, line, sizeof(latestValue) - 1);
    latestValue[sizeof(latestValue) - 1] = 0;
    latestValueTime = millis();
    sendToHub(MSG_RESPONSE, latestValue);
    dirty = true;
  }

  if (!wasReady) {
    // Passage offline→online : notifie le hub immédiatement
    sendToHub(MSG_HEARTBEAT, "bal:1");
    lastHeartbeat = millis();
    dirty = true;  // rafraîchit le dot de statut sur l'AtomS3
  }
}

void readBalanceNonBlocking() {
  while (balSerial.available()) {
    char c = balSerial.read();
    if (c == '\r') {
      if (balLineLen > 0) {
        balLineBuf[balLineLen] = 0;
        onBalanceLine(balLineBuf);
        balLineLen = 0;
      }
      balLastWasCR = true;
    } else if (c == '\n') {
      if (balLastWasCR) {
        balLastWasCR = false;
      } else if (balLineLen > 0) {
        balLineBuf[balLineLen] = 0;
        onBalanceLine(balLineBuf);
        balLineLen = 0;
      }
    } else {
      balLastWasCR = false;
      if (balLineLen < (int)sizeof(balLineBuf) - 1) {
        balLineBuf[balLineLen++] = c;
      } else {
        balLineLen = 0;
      }
    }
  }
}

// =====================================================
// Query (non bloquant)
// =====================================================
void startQuery(const char* cmd) {
  strncpy(pendingCmd, cmd, sizeof(pendingCmd) - 1);
  pendingCmd[sizeof(pendingCmd) - 1] = 0;
  state = N_QUERYING;
  stateStart = millis();
  char ev[32];
  snprintf(ev, sizeof(ev), "CMD:%s", cmd);
  sendToHub(MSG_STATUS, ev);
  sendBalanceCmd(cmd);
  dirty = true;
}

// =====================================================
// ESP-NOW callbacks
// =====================================================
void onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  msg_t m;
  if (!validateMsg(data, len, m)) return;

  switch (m.type) {
    case MSG_DISCOVER: {
      bool newHub = !hubKnown || memcmp(hubMac, info->src_addr, 6) != 0;
      memcpy(hubMac, info->src_addr, 6);
      if (!hubKnown || newHub) {
        hubKnown = true;
        esp_now_peer_info_t peer = {};
        memcpy(peer.peer_addr, hubMac, 6);
        peer.channel = 0;
        peer.encrypt = false;
        esp_now_del_peer(hubMac);
        esp_now_add_peer(&peer);
        saveHubMac();
        dirty = true;
      }
      sendMsg(info->src_addr, MSG_ANNOUNCE, cfg_name);
      break;
    }

    case MSG_PING:
      sendMsg(info->src_addr, MSG_PONG, cfg_name);
      break;

    case MSG_NODE_INFO_REQ: {
      char buf[200];
      snprintf(buf, sizeof(buf),
               "%s uptime=%lu last=\"%s\" lastcmd=%s brand=%d baud=%u type=\"%s\" sn=\"%s\" bid=\"%s\" label=\"%s\"",
               cfg_name, millis() / 1000UL,
               latestValue[0] ? latestValue : "",
               lastCmdStatus, cfg_brand, cfg_baud,
               balanceType, balanceSerial, balanceId, cdoLabel());
      sendMsg(info->src_addr, MSG_NODE_INFO_RESP, buf);
      break;
    }

    case MSG_COMMAND:
      if (isIdentifyCommand(m.payload)) {
        identifyUntil = millis() + 2500;
        dirty = true;
      } else if (m.payload[0] && !isPollingCommand(m.payload)) {
        startQuery(m.payload);
      }
      break;

    case MSG_CONFIG_REQ: {
      // Sérialise la config courante en JSON → MSG_CONFIG_RESP
      StaticJsonDocument<256> doc;
      doc["n"]   = cfg_name;
      doc["br"]  = cfg_brand;
      doc["bd"]  = cfg_baud;
      doc["pa"]  = cfg_parity;
      doc["db"]  = cfg_dataBits;
      doc["sb"]  = cfg_stopBits;
      doc["rx"]  = cfg_rxPin;
      doc["tx"]  = cfg_txPin;
      doc["sw"]  = cfg_swapRxTx ? 1 : 0;
      doc["lb"]  = cdoLabel();
      doc["cmd"] = cfg_pollCmd;
      doc["to"]  = cfg_lineTimeout;
      doc["zc"]  = cfg_zeroCmd;
      doc["cp"]  = cfg_capacity;
      doc["rs"]  = cfg_resolution;
      char buf[200];
      serializeJson(doc, buf, sizeof(buf));
      sendMsg(info->src_addr, MSG_CONFIG_RESP, buf);
      break;
    }

    case MSG_CONFIG_SET: {
      // Parse JSON, sauvegarde NVS, reboot
      StaticJsonDocument<256> doc;
      if (deserializeJson(doc, m.payload) == DeserializationError::Ok) {
        if (doc.containsKey("n"))   strlcpy(cfg_name,    doc["n"],   sizeof(cfg_name));
        if (doc.containsKey("br"))  cfg_brand       = doc["br"];
        if (doc.containsKey("bd"))  cfg_baud        = doc["bd"];
        if (doc.containsKey("pa"))  cfg_parity      = doc["pa"];
        if (doc.containsKey("db"))  cfg_dataBits    = doc["db"];
        if (doc.containsKey("sb"))  cfg_stopBits    = doc["sb"];
        if (doc.containsKey("rx"))  cfg_rxPin       = doc["rx"];
        if (doc.containsKey("tx"))  cfg_txPin       = doc["tx"];
        if (doc.containsKey("sw"))  cfg_swapRxTx    = (doc["sw"].as<int>() != 0);
        if (doc.containsKey("lb"))  strlcpy(cfg_label, doc["lb"], sizeof(cfg_label));
        if (doc.containsKey("cmd")) strlcpy(cfg_pollCmd, doc["cmd"], sizeof(cfg_pollCmd));
        if (doc.containsKey("to"))  cfg_lineTimeout = doc["to"];
        if (doc.containsKey("zc"))  strlcpy(cfg_zeroCmd, doc["zc"], sizeof(cfg_zeroCmd));
        if (doc.containsKey("cp"))  cfg_capacity = doc["cp"].as<float>();
        if (doc.containsKey("rs"))  cfg_resolution = doc["rs"].as<float>();
        if (!cfg_zeroCmd[0]) strlcpy(cfg_zeroCmd, DEFAULT_ZERO_CMD, sizeof(cfg_zeroCmd));
        if (cfg_capacity <= 0) cfg_capacity = DEFAULT_CAPACITY;
        if (cfg_resolution <= 0) cfg_resolution = DEFAULT_RESOLUTION;
        char parsedId[20];
        if (extractClientBalanceId(cfg_label, parsedId, sizeof(parsedId))) {
          strlcpy(balanceId, parsedId, sizeof(balanceId));
          applyClientBrandMapping();
        }
        saveConfig();
        sendMsg(info->src_addr, MSG_CONFIG_ACK, "ok");
        delay(150);   // laisse l'ACK partir avant le reboot
        ESP.restart();
      } else {
        sendMsg(info->src_addr, MSG_CONFIG_ACK, "err:json");
      }
      break;
    }

    default:
      break;
  }
}

// =====================================================
// UI helpers
// =====================================================
void drawScaleIconMini(int cx, int cy, uint16_t color) {
  sprite.fillRect(cx - 6, cy - 3, 12, 1, color);
  sprite.fillRect(cx, cy - 3, 1, 5, color);
  sprite.fillRect(cx - 3, cy + 2, 7, 1, color);
  sprite.drawLine(cx - 6, cy - 1, cx - 8, cy + 1, color);
  sprite.drawLine(cx - 6, cy - 1, cx - 4, cy + 1, color);
  sprite.drawLine(cx + 6, cy - 1, cx + 8, cy + 1, color);
  sprite.drawLine(cx + 6, cy - 1, cx + 4, cy + 1, color);
}

void drawBalIcon(int cx, int cy, bool ready) {
  uint16_t c = ready ? COLOR_SUCCESS : COLOR_TEXT_DIM;
  sprite.fillRect(cx - 14, cy - 8, 28, 2, c);
  sprite.fillRect(cx - 1,  cy - 8, 2,  14, c);
  sprite.fillRect(cx - 8,  cy + 6, 18, 2,  c);
  sprite.drawLine(cx - 14, cy - 4, cx - 18, cy + 4, c);
  sprite.drawLine(cx - 18, cy + 4, cx - 10, cy + 4, c);
  sprite.drawLine(cx + 14, cy - 4, cx + 18, cy + 4, c);
  sprite.drawLine(cx + 18, cy + 4, cx + 10, cy + 4, c);
  if (!ready) {
    sprite.drawLine(cx - 20, cy - 10, cx + 20, cy + 10, COLOR_ERROR);
    sprite.drawLine(cx - 20, cy + 10, cx + 20, cy - 10, COLOR_ERROR);
  }
}

void drawBadgeSmall(int cx, int cy, const char* text, uint16_t bg, uint16_t fg) {
  sprite.setFont(&fonts::FreeSansBold9pt7b);
  int tw = sprite.textWidth(text);
  int w = tw + 12;
  int h = 18;
  int x = cx - w / 2;
  int y = cy - h / 2;
  sprite.fillRoundRect(x, y, w, h, h / 2, bg);
  sprite.setTextColor(fg);
  sprite.setTextDatum(middle_center);
  sprite.drawString(text, cx, cy + 1);
  sprite.setTextDatum(top_left);
}

static bool splitTextForWidth(const char* text, int maxWidth, char* line1, size_t line1Size, char* line2, size_t line2Size) {
  if (!text || !*text) return false;
  line1[0] = 0;
  line2[0] = 0;

  size_t len = strlen(text);
  int bestSplit = -1;
  int bestScore = 1 << 30;
  for (size_t i = 1; i + 1 < len; i++) {
    if (text[i] != ' ') continue;
    char left[64];
    char right[64];
    strlcpy(left, text, min(sizeof(left), i + 1));
    while (left[0] && left[strlen(left) - 1] == ' ') left[strlen(left) - 1] = 0;
    const char* r = text + i + 1;
    while (*r == ' ') r++;
    strlcpy(right, r, sizeof(right));
    int lw = sprite.textWidth(left);
    int rw = sprite.textWidth(right);
    if (lw <= maxWidth && rw <= maxWidth) {
      int score = abs(lw - rw);
      if (score < bestScore) {
        bestScore = score;
        bestSplit = (int)i;
      }
    }
  }

  if (bestSplit >= 0) {
    strlcpy(line1, text, min(line1Size, (size_t)bestSplit + 1));
    while (line1[0] && line1[strlen(line1) - 1] == ' ') line1[strlen(line1) - 1] = 0;
    const char* r = text + bestSplit + 1;
    while (*r == ' ') r++;
    strlcpy(line2, r, line2Size);
    return true;
  }

  size_t split = 0;
  char probe[64] = "";
  while (split < len) {
    size_t next = split + 1;
    strlcpy(probe, text, min(sizeof(probe), next + 1));
    if (sprite.textWidth(probe) > maxWidth) break;
    split = next;
  }
  if (split == 0 || split >= len) return false;
  strlcpy(line1, text, min(line1Size, split + 1));
  strlcpy(line2, text + split, line2Size);
  while (line2[0] == ' ') memmove(line2, line2 + 1, strlen(line2));
  return sprite.textWidth(line2) <= maxWidth;
}

static void drawFittedCenteredValue(int cx, int cy, const char* text, uint16_t color) {
  if (!text || !*text) return;
  sprite.setTextColor(color);
  sprite.setTextDatum(middle_center);

  const int maxWidth = SCREEN_W - 14;

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

  char line1[64];
  char line2[64];
  if (splitTextForWidth(text, maxWidth, line1, sizeof(line1), line2, sizeof(line2))) {
    sprite.drawString(line1, cx, cy - 10);
    sprite.drawString(line2, cx, cy + 10);
    sprite.setTextDatum(top_left);
    return;
  }

  sprite.setFont(&fonts::FreeSans9pt7b);
  if (sprite.textWidth(text) <= maxWidth) {
    sprite.drawString(text, cx, cy);
    sprite.setTextDatum(top_left);
    return;
  }
  if (splitTextForWidth(text, maxWidth, line1, sizeof(line1), line2, sizeof(line2))) {
    sprite.drawString(line1, cx, cy - 10);
    sprite.drawString(line2, cx, cy + 10);
  } else {
    sprite.drawString(text, cx, cy);
  }
  sprite.setTextDatum(top_left);
}

// =====================================================
// Affichage (sprite, throttled)
// =====================================================
void drawScreen() {
  bool identifyBlink = identifyUntil > millis() && ((millis() / 180) % 2 == 0);
  sprite.fillSprite(identifyBlink ? COLOR_PRIMARY_BG : COLOR_BG);
  sprite.setTextDatum(top_left);

  // Identite principale
  sprite.setFont(&fonts::FreeSansBold12pt7b);
  sprite.setTextColor(COLOR_TEXT);
  sprite.setTextDatum(middle_center);
  sprite.drawString(cdoLabel(), SCREEN_W / 2, 16);
  sprite.setTextDatum(top_left);
  sprite.fillCircle(SCREEN_W - 6, 10, 4, hubKnown ? COLOR_SUCCESS : COLOR_WARNING);

  // Card valeur
  sprite.fillRoundRect(6, 32, SCREEN_W - 12, 64, 10, identifyBlink ? COLOR_SUCCESS : COLOR_SURFACE);

  if (state == N_QUERYING) {
    sprite.setFont(&fonts::FreeSansBold18pt7b);
    sprite.setTextColor(COLOR_PRIMARY);
    sprite.setTextDatum(middle_center);
    sprite.drawString(pendingCmd, SCREEN_W / 2, 63);
    sprite.setTextDatum(top_left);
  }
  else if (strncmp(latestValue, "ERROR", 5) == 0) {
    drawFittedCenteredValue(SCREEN_W / 2, 63, latestValue + 6, COLOR_ERROR);
  }
  else if (latestValue[0]) {
    drawFittedCenteredValue(SCREEN_W / 2, 63, latestValue, COLOR_TEXT);
  }
  else {
    if (!balanceReady) {
      sprite.setFont(&fonts::FreeSansBold12pt7b);
      sprite.setTextColor(COLOR_ERROR);
      sprite.setTextDatum(middle_center);
      sprite.drawString("Hors ligne", SCREEN_W / 2, 62);
      sprite.setFont(&fonts::FreeSans9pt7b);
      sprite.setTextColor(COLOR_TEXT_DIM);
      char snLine[32];
      snprintf(snLine, sizeof(snLine), "SN:%s", balanceSerial);
      sprite.drawString(snLine, SCREEN_W / 2, 84);
      sprite.setTextDatum(top_left);
    }
  }

  sprite.pushSprite(0, 0);
}

// =====================================================
// Setup / Loop
// =====================================================
void setup() {
  M5.begin();
  Serial.begin(115200);
  sprite.createSprite(SCREEN_W, SCREEN_H);

  loadConfig();   // charge NVS → cfg_* variables + hubMac si connu

  balSerial.begin(cfg_baud, getSerialConfig(), cfg_rxPin, cfg_txPin);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  // Canal 1 : identique au hub en mode AP
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESPNOW_INIT_FAIL");
    return;
  }
  esp_now_register_recv_cb(onRecv);

  // Peer broadcast pour recevoir les DISCOVER
  esp_now_peer_info_t bcPeer = {};
  memset(bcPeer.peer_addr, 0xFF, 6);
  bcPeer.channel = 0;
  bcPeer.encrypt = false;
  esp_now_add_peer(&bcPeer);

  // Si hub connu (NVS), l'ajouter comme peer et envoyer ANNOUNCE immédiatement
  if (hubKnown) {
    esp_now_peer_info_t hubPeer = {};
    memcpy(hubPeer.peer_addr, hubMac, 6);
    hubPeer.channel = 0;
    hubPeer.encrypt = false;
    esp_now_add_peer(&hubPeer);
    sendMsg(hubMac, MSG_ANNOUNCE, cfg_name);
  }

  Serial.printf("Node %s id=%d brand=%d baud=%u RX=%d TX=%d\n",
                cfg_name, cfg_nodeId, cfg_brand, cfg_baud, cfg_rxPin, cfg_txPin);
  drawScreen();
}

void loop() {
  M5.update();

  // RS232 balance non-bloquante
  readBalanceNonBlocking();
  runIdentityQuery();

  // Timeout balance
  if (state == N_QUERYING && millis() - stateStart > BAL_TIMEOUT_MS) {
    bool wasAuto  = autoQuery;
    bool wasReady = balanceReady;
    autoQuery     = false;
    balanceReady  = false;
    state         = N_IDLE;
    if (wasReady) {
      sendToHub(MSG_HEARTBEAT, "bal:0");  // notification immédiate hors ligne
      lastHeartbeat = millis();
    }
    if (!wasAuto) {
      strncpy(lastCmdStatus, "TIMEOUT", sizeof(lastCmdStatus) - 1);
      strncpy(latestValue, "ERROR balance_timeout", sizeof(latestValue) - 1);
      latestValue[sizeof(latestValue) - 1] = 0;
      latestValueTime = millis();
      sendToHub(MSG_RESPONSE, latestValue);
      dirty = true;
    } else {
      if (wasReady) dirty = true;  // rafraîchit si l'état a changé
    }
  }

  // Détection changement état hub
  static bool wasKnown = false;
  if (hubKnown != wasKnown) {
    wasKnown = hubKnown;
    dirty = true;
    Serial.printf("Hub %s\n", hubKnown ? "connected" : "lost");
  }

  // TTL valeur : efface après VALUE_TTL_MS
  if (latestValue[0] && millis() - latestValueTime >= VALUE_TTL_MS) {
    latestValue[0] = 0;
    dirty = true;
  }

  // Heartbeat (inclut état balance)
  if (hubKnown && millis() - lastHeartbeat > HEARTBEAT_MS) {
    lastHeartbeat = millis();
    sendToHub(MSG_HEARTBEAT, balanceReady ? "bal:1" : "bal:0");
  }

  if (identifyUntil > millis()) {
    dirty = true;
  }

  // Affichage throttled
  if (dirty && millis() - lastDisplayUpdate > DISPLAY_REFRESH_MS) {
    lastDisplayUpdate = millis();
    dirty = false;
    drawScreen();
  }
}
