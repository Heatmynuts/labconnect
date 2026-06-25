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
#define MSG_PURGE_RESET     0x0F

// Marques balance
#define BRAND_AD        0
#define BRAND_METTLER   1
#define BRAND_SARTORIUS 2
#define BRAND_KERN      3
#define BRAND_OHAUS     4
#define BRAND_BIZERBA   5
#define BRAND_PRECIA    6
#define BRAND_PRECISA   7
#define BRAND_SHIMADZU  8
#define BRAND_RADWAG    9
#define BRAND_DINI      10
#define BRAND_UNKNOWN   255

#ifndef FW_VARIANT_CODE
#define FW_VARIANT_CODE "ATOM_AND"
#endif

#ifndef FW_LOCK_SERIAL_DEFAULTS
#define FW_LOCK_SERIAL_DEFAULTS 1
#endif

enum BalanceProtocol : uint8_t {
  PROTOCOL_UNKNOWN = 0,
  PROTOCOL_AD,
  PROTOCOL_SICS,
  PROTOCOL_SARTORIUS_SBI,
  PROTOCOL_KERN_KCP,
  PROTOCOL_OHAUS,
  PROTOCOL_SHIMADZU,
  PROTOCOL_RADWAG,
  PROTOCOL_DINI,
  PROTOCOL_PRECISA,
  PROTOCOL_BIZERBA,
  PROTOCOL_PRECIA_A_PLUS,
  PROTOCOL_GENERIC_ASCII
};

static const char* protocolName(BalanceProtocol protocol) {
  switch (protocol) {
    case PROTOCOL_AD: return "A&D";
    case PROTOCOL_SICS: return "SICS";
    case PROTOCOL_SARTORIUS_SBI: return "SBI";
    case PROTOCOL_KERN_KCP: return "KCP";
    case PROTOCOL_OHAUS: return "Ohaus";
    case PROTOCOL_SHIMADZU: return "Shimadzu";
    case PROTOCOL_RADWAG: return "Radwag";
    case PROTOCOL_DINI: return "Dini";
    case PROTOCOL_PRECISA: return "Precisa";
    case PROTOCOL_BIZERBA: return "Bizerba";
    case PROTOCOL_PRECIA_A_PLUS: return "Precia";
    case PROTOCOL_GENERIC_ASCII: return "ASCII";
    default: return "?";
  }
}

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
#define DEFAULT_NODE_NAME    "ATOM-AND"
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
#define DEFAULT_AUTODETECT   false

#define HEARTBEAT_MS       2000
#define DISPLAY_REFRESH_MS 200
#define BAL_TIMEOUT_MS     2000
#define VALUE_TTL_MS       2000
#define IDENTITY_QUERY_DELAY_MS   700
#define IDENTITY_QUERY_TIMEOUT_MS 1800
#define AUTODETECT_BOOT_DELAY_MS  1500
#define AUTODETECT_SETTLE_MS       90
#define AUTODETECT_RETRY_DELAY_MS  80
#define AUTODETECT_MAX_SCORE       100

// =====================================================
// Config runtime (chargée depuis NVS)
// =====================================================
static uint8_t  cfg_nodeId      = DEFAULT_NODE_ID;
static char     cfg_name[32]    = DEFAULT_NODE_NAME;
static char     cfg_typeName[32]= "";
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
static BalanceProtocol cfg_protocol = PROTOCOL_UNKNOWN;
static uint8_t  cfg_confidence = 0;
static bool     cfg_autodetect = DEFAULT_AUTODETECT;

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

enum IdentityQuery {
  ID_NONE, ID_TYPE, ID_SERIAL, ID_BALANCE_ID, ID_BALANCE_ID_ALT,
  ID_SICS_TYPE, ID_SICS_SERIAL, ID_RADWAG_TYPE, ID_RADWAG_SERIAL,
  ID_DINI_VERSION
};
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

enum LineEnding : uint8_t { END_CR, END_LF, END_CRLF };

static BalanceProtocol protocolForBrand(uint8_t brand) {
  switch (brand) {
    case BRAND_AD: return PROTOCOL_AD;
    case BRAND_METTLER: return PROTOCOL_SICS;
    case BRAND_SARTORIUS: return PROTOCOL_SARTORIUS_SBI;
    default: return PROTOCOL_UNKNOWN;
  }
}

struct ScanProfile {
  uint8_t brand;
  BalanceProtocol protocol;
  uint32_t baud;
  uint8_t parity;       // 0 none, 1 even, 2 odd
  uint8_t dataBits;
  uint8_t stopBits;
  const char* command;
  LineEnding ending;
  uint16_t timeoutMs;
  const char* label;
};

// Profils sûrs : uniquement lecture de poids. Bizerba et Precia Molen restent
// volontairement absents tant qu'une commande commune non destructive n'est
// pas confirmée pour les indicateurs ciblés.
static const ScanProfile SCAN_PROFILES[] = {
  {BRAND_UNKNOWN, PROTOCOL_SICS,          9600, 0, 8, 1, "SI",   END_CRLF, 500, "SICS"},
  {BRAND_UNKNOWN, PROTOCOL_SICS,          2400, 0, 8, 1, "SI",   END_CRLF, 550, "SICS 2400"},
  {BRAND_KERN,    PROTOCOL_KERN_KCP,      9600, 0, 8, 1, "W",    END_CRLF, 500, "Kern"},
  {BRAND_OHAUS,   PROTOCOL_OHAUS,         9600, 0, 8, 1, "IP",   END_CRLF, 500, "Ohaus"},
  {BRAND_DINI,    PROTOCOL_DINI,          9600, 0, 8, 1, "READ", END_CRLF, 500, "Dini"},
  {BRAND_AD,      PROTOCOL_AD,            2400, 1, 7, 1, "Q",    END_CRLF, 400, "A&D"},
  {BRAND_SARTORIUS, PROTOCOL_SARTORIUS_SBI, 9600, 2, 8, 1, "\x1bP", END_CRLF, 600, "Sartorius"},
  {BRAND_SHIMADZU, PROTOCOL_SHIMADZU,      300, 0, 8, 1, "D05",  END_CR,   650, "Shimadzu"},
  {BRAND_SHIMADZU, PROTOCOL_SHIMADZU,     1200, 0, 8, 1, "D05",  END_CR,   650, "Shimadzu"},
  {BRAND_PRECISA, PROTOCOL_PRECISA,       9600, 0, 8, 1, "PRT",  END_CRLF, 650, "Precisa"},
  {BRAND_PRECISA, PROTOCOL_PRECISA,       4800, 1, 7, 1, "PRT",  END_CRLF, 650, "Precisa"},
  // Variantes fréquentes, essayées seulement après tous les profils usine.
  {BRAND_UNKNOWN, PROTOCOL_SICS,          4800, 0, 8, 1, "SI",   END_CRLF, 550, "SICS alt"},
  {BRAND_UNKNOWN, PROTOCOL_SICS,         19200, 0, 8, 1, "SI",   END_CRLF, 550, "SICS alt"},
  {BRAND_UNKNOWN, PROTOCOL_SICS,         38400, 0, 8, 1, "SI",   END_CRLF, 550, "SICS alt"},
  {BRAND_AD,      PROTOCOL_AD,            4800, 1, 7, 1, "Q",    END_CRLF, 450, "A&D alt"},
  {BRAND_AD,      PROTOCOL_AD,            9600, 1, 7, 1, "Q",    END_CRLF, 450, "A&D alt"},
  {BRAND_AD,      PROTOCOL_AD,            2400, 1, 7, 2, "Q",    END_CRLF, 450, "A&D alt"},
  {BRAND_SARTORIUS, PROTOCOL_SARTORIUS_SBI, 1200, 2, 7, 2, "\x1bP", END_CRLF, 650, "Sartorius alt"},
  {BRAND_KERN,    PROTOCOL_KERN_KCP,      4800, 0, 8, 1, "W",    END_CRLF, 550, "Kern alt"},
  {BRAND_OHAUS,   PROTOCOL_OHAUS,         4800, 0, 8, 1, "IP",   END_CRLF, 550, "Ohaus alt"},
  {BRAND_DINI,    PROTOCOL_DINI,         19200, 0, 8, 1, "READ", END_CRLF, 550, "Dini alt"}
};
static const size_t SCAN_PROFILE_COUNT = sizeof(SCAN_PROFILES) / sizeof(SCAN_PROFILES[0]);

enum ScanState : uint8_t {
  SCAN_OFF, SCAN_BOOT_WAIT, SCAN_CONFIGURE, SCAN_SETTLE,
  SCAN_WAIT_RESPONSE, SCAN_RETRY_DELAY, SCAN_CONFIRM_WAIT,
  SCAN_DONE, SCAN_FAILED
};
static ScanState scanState = SCAN_OFF;
static size_t scanProfileIndex = 0;
static uint8_t scanAttempt = 0;
static unsigned long scanStateSince = 0;
static char scanFirstResponse[128] = "";
static uint8_t scanFirstScore = 0;
static BalanceProtocol scanDetectedProtocol = PROTOCOL_UNKNOWN;
static uint8_t scanDetectedBrand = BRAND_UNKNOWN;
static bool scanButtonLatched = false;
static bool scanSavedFirst = false;
static ScanProfile savedScanProfile = {};
static LineEnding cfg_lineEnding = END_CRLF;

static char  balLineBuf[128];
static int   balLineLen = 0;
static bool  balLastWasCR = false;
static unsigned long balLastByteAt = 0;

static unsigned long lastDisplayUpdate = 0;
static unsigned long lastHeartbeat = 0;
static bool dirty = true;
static unsigned long identifyUntil = 0;

HardwareSerial balSerial(1);
Preferences    cfgPrefs;
M5Canvas       sprite(&M5.Display);

// Prototypes explicites requis par le préprocesseur Arduino pour les fonctions
// dont la signature utilise les types déclarés dans ce fichier.
uint8_t computeMsgCrc(const msg_t* m);
bool validateMsg(const uint8_t* data, int len, msg_t& m);
const ScanProfile& activeScanProfile();
void sendBalanceCmdWithEnding(const char* cmd, LineEnding ending);
uint8_t classifyWeightResponse(const char* raw, const ScanProfile& expected,
                               BalanceProtocol& protocol, uint8_t& brand);
void finishAutoDetect(const ScanProfile& profile, const char* response,
                      BalanceProtocol protocol, uint8_t brand, uint8_t score);
void sendIdentityQuery(IdentityQuery query, const char* cmd);
bool saveConfigMigration();
void applyFirmwareDefaults();

static bool looksLikeLegacyBalanceTypeName(const char* text) {
  if (!text || !text[0]) return false;
  if (strcmp(text, DEFAULT_NODE_NAME) == 0) return false;
  if (strncmp(text, "Node ", 5) == 0 || strncmp(text, "NODE ", 5) == 0) return false;
  if (strstr(text, "A&D") || strstr(text, "Mettler") || strstr(text, "METTLER") ||
      strstr(text, "Sartorius") || strstr(text, "SARTORIUS")) return true;

  int letters = 0;
  int i = 0;
  while (text[i] >= 'A' && text[i] <= 'Z') { letters++; i++; }
  if (letters >= 2 && letters <= 4 && text[i] == '-') {
    i++;
    if (text[i] >= '0' && text[i] <= '9') return true;
  }
  return false;
}

static bool migrateLegacyNodeIdentity() {
  bool changed = false;
  if (!cfg_name[0]) {
    snprintf(cfg_name, sizeof(cfg_name), "Node %02u", cfg_nodeId);
    changed = true;
  }
  if (!cfg_typeName[0] && looksLikeLegacyBalanceTypeName(cfg_name)) {
    strlcpy(cfg_typeName, cfg_name, sizeof(cfg_typeName));
    snprintf(cfg_name, sizeof(cfg_name), "Node %02u", cfg_nodeId);
    changed = true;
  }
  return changed;
}

void applyFirmwareDefaults() {
  cfg_brand       = DEFAULT_BRAND;
  cfg_baud        = DEFAULT_BAUD;
  cfg_parity      = DEFAULT_PARITY;
  cfg_dataBits    = DEFAULT_DATABITS;
  cfg_stopBits    = DEFAULT_STOPBITS;
  cfg_rxPin       = DEFAULT_RX;
  cfg_txPin       = DEFAULT_TX;
  cfg_swapRxTx    = DEFAULT_SWAP;
  strlcpy(cfg_pollCmd, DEFAULT_POLL_CMD, sizeof(cfg_pollCmd));
  cfg_lineTimeout = DEFAULT_LINE_TIMEOUT;
  strlcpy(cfg_zeroCmd, DEFAULT_ZERO_CMD, sizeof(cfg_zeroCmd));
  cfg_capacity    = DEFAULT_CAPACITY;
  cfg_resolution  = DEFAULT_RESOLUTION;
  cfg_protocol    = protocolForBrand(DEFAULT_BRAND);
  cfg_confidence  = 100;
  cfg_autodetect  = false;
  cfg_lineEnding  = END_CRLF;
  cfg_typeName[0] = 0;
  cfg_label[0]    = 0;
}

// =====================================================
// Config NVS
// =====================================================
uint32_t getSerialConfig() {
  const bool twoStops = cfg_stopBits == 2;
  if (cfg_dataBits == 7) {
    if (cfg_parity == 1) return twoStops ? SERIAL_7E2 : SERIAL_7E1;
    if (cfg_parity == 2) return twoStops ? SERIAL_7O2 : SERIAL_7O1;
    return twoStops ? SERIAL_7N2 : SERIAL_7N1;
  } else {
    if (cfg_parity == 1) return twoStops ? SERIAL_8E2 : SERIAL_8E1;
    if (cfg_parity == 2) return twoStops ? SERIAL_8O2 : SERIAL_8O1;
    return twoStops ? SERIAL_8N2 : SERIAL_8N1;
  }
}

void loadConfig() {
  applyFirmwareDefaults();
  cfgPrefs.begin("nodecfg", true);
  cfg_nodeId   = cfgPrefs.getUChar("id",    DEFAULT_NODE_ID);

  char savedFwVariant[24] = "";
  cfgPrefs.getString("fwv", savedFwVariant, sizeof(savedFwVariant));
  const bool firmwareVariantMatches = savedFwVariant[0] && strcmp(savedFwVariant, FW_VARIANT_CODE) == 0;

  if (firmwareVariantMatches && !FW_LOCK_SERIAL_DEFAULTS) {
    cfg_brand    = cfgPrefs.getUChar("brand", DEFAULT_BRAND);
    cfg_baud     = cfgPrefs.getULong("baud",  DEFAULT_BAUD);
    cfg_parity   = cfgPrefs.getUChar("parity", DEFAULT_PARITY);
    cfg_dataBits = cfgPrefs.getUChar("dbits",  DEFAULT_DATABITS);
    cfg_stopBits = cfgPrefs.getUChar("sbits",  DEFAULT_STOPBITS);
    cfg_rxPin    = cfgPrefs.getUChar("rxpin",  DEFAULT_RX);
    cfg_txPin    = cfgPrefs.getUChar("txpin",  DEFAULT_TX);
    cfg_swapRxTx = cfgPrefs.getBool ("swap",   DEFAULT_SWAP);
    cfg_lineTimeout = cfgPrefs.getUShort("timeout", DEFAULT_LINE_TIMEOUT);
    cfg_protocol = (BalanceProtocol)cfgPrefs.getUChar("proto", protocolForBrand(DEFAULT_BRAND));
    cfg_confidence = cfgPrefs.getUChar("conf", 100);
    cfg_autodetect = cfgPrefs.getBool("autoscan", false);
    cfg_lineEnding = (LineEnding)cfgPrefs.getUChar("ending", END_CRLF);
  }
  if (firmwareVariantMatches) {
    cfg_capacity = cfgPrefs.getFloat("cap", DEFAULT_CAPACITY);
    cfg_resolution = cfgPrefs.getFloat("res", DEFAULT_RESOLUTION);
  }

  // Strings
  char tmp[32];
  cfgPrefs.getString("name", tmp, sizeof(tmp));
  if (tmp[0]) strncpy(cfg_name, tmp, sizeof(cfg_name) - 1);

  if (firmwareVariantMatches) {
    char tmpt[32];
    cfgPrefs.getString("type", tmpt, sizeof(tmpt));
    if (tmpt[0]) strncpy(cfg_typeName, tmpt, sizeof(cfg_typeName) - 1);

    char tmpl[32];
    cfgPrefs.getString("label", tmpl, sizeof(tmpl));
    if (tmpl[0]) strncpy(cfg_label, tmpl, sizeof(cfg_label) - 1);

    char tmpc[16];
    cfgPrefs.getString("pollcmd", tmpc, sizeof(tmpc));
    if (tmpc[0]) strncpy(cfg_pollCmd, tmpc, sizeof(cfg_pollCmd) - 1);

    char tmpz[16];
    cfgPrefs.getString("zerocmd", tmpz, sizeof(tmpz));
    if (tmpz[0]) strncpy(cfg_zeroCmd, tmpz, sizeof(cfg_zeroCmd) - 1);
  }
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

  if (migrateLegacyNodeIdentity()) {
    saveConfig();
  }
}

void saveConfig() {
  cfgPrefs.begin("nodecfg", false);
  cfgPrefs.putString("fwv",     FW_VARIANT_CODE);
  cfgPrefs.putUChar ("id",      cfg_nodeId);
  cfgPrefs.putString("name",    cfg_name);
  cfgPrefs.putString("type",    cfg_typeName);
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
  cfgPrefs.putUChar ("proto", (uint8_t)cfg_protocol);
  cfgPrefs.putUChar ("conf", cfg_confidence);
  cfgPrefs.putBool  ("autoscan", cfg_autodetect);
  cfgPrefs.putUChar ("ending", (uint8_t)cfg_lineEnding);
  cfgPrefs.end();
}

void saveHubMac() {
  cfgPrefs.begin("nodecfg", false);
  cfgPrefs.putBytes("hubmac", hubMac, 6);
  cfgPrefs.end();
}

void purgeStoredNodeConfig() {
  cfgPrefs.begin("nodecfg", false);
  cfgPrefs.clear();
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
  if (cfg_protocol == PROTOCOL_SICS) {
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

bool extractSicsIdentityValue(const char* raw, char* out, size_t outSize) {
  if (!raw || !out || outSize == 0) return false;
  char compact[96];
  compactIdentityLine(raw, compact, sizeof(compact));
  for (size_t i = 0; compact[i]; i++) {
    if (compact[i] >= 'a' && compact[i] <= 'z') compact[i] = (char)(compact[i] - 'a' + 'A');
  }
  if (strncmp(compact, "I10_A,", 6) != 0) return false;
  return extractQuotedIdentityValue(compact, out, outSize);
}

bool isIdentityErrorLine(const char* raw) {
  if (!raw || !*raw) return true;
  char compact[32];
  compactIdentityLine(raw, compact, sizeof(compact));
  for (size_t i = 0; compact[i]; i++) {
    if (compact[i] >= 'a' && compact[i] <= 'z') compact[i] = (char)(compact[i] - 'a' + 'A');
  }
  return strcmp(compact, "ES") == 0 ||
         strcmp(compact, "ET") == 0 ||
         strcmp(compact, "EL") == 0;
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
const ScanProfile& activeScanProfile() {
  if (scanSavedFirst && scanProfileIndex == 0) return savedScanProfile;
  size_t index = scanProfileIndex - (scanSavedFirst ? 1 : 0);
  return SCAN_PROFILES[index];
}

size_t activeScanProfileCount() {
  return SCAN_PROFILE_COUNT + (scanSavedFirst ? 1 : 0);
}

void clearBalanceInput() {
  while (balSerial.available()) balSerial.read();
  balLineLen = 0;
  balLastWasCR = false;
  balLastByteAt = 0;
}

void configureBalanceSerial(uint32_t baud, uint8_t parity, uint8_t dataBits,
                            uint8_t stopBits) {
  cfg_baud = baud;
  cfg_parity = parity;
  cfg_dataBits = dataBits;
  cfg_stopBits = stopBits;
  int rx = cfg_swapRxTx ? cfg_txPin : cfg_rxPin;
  int tx = cfg_swapRxTx ? cfg_rxPin : cfg_txPin;
  balSerial.end();
  delay(2);
  balSerial.begin(cfg_baud, getSerialConfig(), rx, tx);
  clearBalanceInput();
}

void sendBalanceCmdWithEnding(const char* cmd, LineEnding ending) {
  if (!cmd) return;
  balSerial.write((const uint8_t*)cmd, strlen(cmd));
  if (ending == END_CR || ending == END_CRLF) balSerial.write('\r');
  if (ending == END_LF || ending == END_CRLF) balSerial.write('\n');
  balSerial.flush();
}

void sendBalanceCmd(const char* cmd) {
  sendBalanceCmdWithEnding(cmd, cfg_lineEnding);
}

bool containsWeightNumber(const char* raw) {
  if (!raw) return false;
  bool digit = false;
  bool unit = false;
  char upper[128];
  strlcpy(upper, raw, sizeof(upper));
  for (size_t i = 0; upper[i]; i++) {
    if (upper[i] >= 'a' && upper[i] <= 'z') upper[i] -= 32;
    if (upper[i] >= '0' && upper[i] <= '9') digit = true;
  }
  const char* units[] = {" G", ",G", "KG", " MG", "LB", "OZ", " N", " T"};
  for (const char* candidate : units) {
    if (strstr(upper, candidate)) { unit = true; break; }
  }
  return digit && unit;
}

uint8_t classifyWeightResponse(const char* raw, const ScanProfile& expected,
                               BalanceProtocol& protocol, uint8_t& brand) {
  protocol = PROTOCOL_UNKNOWN;
  brand = BRAND_UNKNOWN;
  if (!raw || !*raw) return 0;

  char text[128];
  compactIdentityLine(raw, text, sizeof(text));
  for (size_t i = 0; text[i]; i++) {
    if (text[i] >= 'a' && text[i] <= 'z') text[i] -= 32;
  }
  if (strstr(text, "ERR") || strstr(text, "ERROR") || strcmp(text, "OK") == 0) return 0;
  if (!containsWeightNumber(text)) return 0;

  if (expected.protocol == PROTOCOL_DINI && strchr(text, ',') &&
      (strstr(text, "ST,") || strstr(text, "US,") || strstr(text, "OL,"))) {
    protocol = PROTOCOL_DINI;
    brand = BRAND_DINI;
    return 94;
  }
  if ((strncmp(text, "ST,", 3) == 0 || strncmp(text, "US,", 3) == 0 ||
       strncmp(text, "OL,", 3) == 0) &&
      (strstr(text, ",GS,") || strstr(text, ",NT,"))) {
    protocol = PROTOCOL_AD;
    brand = BRAND_AD;
    return 96;
  }
  if (strncmp(text, "SI ", 3) == 0 || strncmp(text, "SI_", 3) == 0 ||
      strncmp(text, "SUI ", 4) == 0) {
    protocol = PROTOCOL_RADWAG;
    brand = BRAND_RADWAG;
    return 95;
  }
  if (strncmp(text, "S ", 2) == 0 && strlen(text) > 4) {
    protocol = PROTOCOL_SICS;
    brand = BRAND_UNKNOWN;
    return 90;
  }
  if (strncmp(text, "W ", 2) == 0 || strncmp(text, "W_", 2) == 0) {
    protocol = PROTOCOL_KERN_KCP;
    brand = BRAND_KERN;
    return 92;
  }
  // Les protocoles à trame de poids fixe n'ont pas toujours de signature de
  // marque. Ils ne sont acceptés qu'en réponse à leur commande dédiée.
  protocol = expected.protocol;
  brand = expected.brand;
  switch (expected.protocol) {
    case PROTOCOL_SARTORIUS_SBI:
    case PROTOCOL_OHAUS:
    case PROTOCOL_SHIMADZU:
    case PROTOCOL_DINI:
    case PROTOCOL_PRECISA:
      return 78;
    default:
      protocol = PROTOCOL_GENERIC_ASCII;
      brand = BRAND_UNKNOWN;
      return 62;
  }
}

void resetIdentityState() {
  pendingIdentity = ID_NONE;
  strlcpy(balanceType, "?", sizeof(balanceType));
  strlcpy(balanceSerial, "?", sizeof(balanceSerial));
  identityTypeDone = false;
  identitySerialDone = false;
  identityPrimaryIdTried = false;
  identityAltIdTried = false;
  nextIdentityQueryAt = millis() + IDENTITY_QUERY_DELAY_MS;
}

void finishAutoDetect(const ScanProfile& profile, const char* response,
                      BalanceProtocol protocol, uint8_t brand, uint8_t score) {
  cfg_protocol = protocol;
  cfg_brand = brand;
  cfg_confidence = score > AUTODETECT_MAX_SCORE ? AUTODETECT_MAX_SCORE : score;
  cfg_lineEnding = profile.ending;
  cfg_lineTimeout = profile.timeoutMs;
  strlcpy(cfg_pollCmd, profile.command, sizeof(cfg_pollCmd));
  strlcpy(latestValue, response, sizeof(latestValue));
  latestValueTime = millis();
  balanceReady = true;
  scanState = SCAN_DONE;
  state = N_IDLE;
  resetIdentityState();
  saveConfig();
  char status[96];
  snprintf(status, sizeof(status), "DETECTED proto=%s brand=%u baud=%lu conf=%u",
           protocolName(cfg_protocol), cfg_brand, (unsigned long)cfg_baud, cfg_confidence);
  Serial.println(status);
  sendToHub(MSG_STATUS, status);
  sendToHub(MSG_RESPONSE, latestValue);
  dirty = true;
}

void advanceScanProfile() {
  scanProfileIndex++;
  scanAttempt = 0;
  scanFirstResponse[0] = 0;
  if (scanProfileIndex >= activeScanProfileCount()) {
    scanState = SCAN_FAILED;
    scanStateSince = millis();
    loadConfig();
    configureBalanceSerial(cfg_baud, cfg_parity, cfg_dataBits, cfg_stopBits);
    Serial.println("AUTODETECT failed; manual/saved configuration restored");
    sendToHub(MSG_STATUS, "AUTODETECT_FAILED");
    dirty = true;
  } else {
    scanState = SCAN_CONFIGURE;
    scanStateSince = millis();
  }
}

void startAutoDetect(bool forceFullScan = false) {
  if (!cfg_autodetect && !forceFullScan) return;
  state = N_IDLE;
  autoQuery = false;
  pendingIdentity = ID_NONE;
  balanceReady = false;
  latestValue[0] = 0;
  scanSavedFirst = !forceFullScan && cfg_confidence >= 60 && cfg_pollCmd[0];
  if (scanSavedFirst) {
    savedScanProfile = {cfg_brand, cfg_protocol, cfg_baud, cfg_parity,
                        cfg_dataBits, cfg_stopBits, cfg_pollCmd,
                        cfg_lineEnding, cfg_lineTimeout, "Saved"};
  }
  scanProfileIndex = 0;
  scanAttempt = 0;
  scanFirstResponse[0] = 0;
  scanState = SCAN_BOOT_WAIT;
  scanStateSince = millis();
  Serial.println("AUTODETECT started");
  sendToHub(MSG_STATUS, "AUTODETECT_STARTED");
  dirty = true;
}

bool handleScanLine(const char* line) {
  if (scanState != SCAN_WAIT_RESPONSE && scanState != SCAN_CONFIRM_WAIT) return false;
  const ScanProfile& profile = activeScanProfile();

  if (scanState == SCAN_CONFIRM_WAIT && scanDetectedProtocol == PROTOCOL_SICS) {
    char ident[64];
    if (extractSicsIdentityValue(line, ident, sizeof(ident))) {
      char cdoId[20];
      if (extractClientBalanceId(ident, cdoId, sizeof(cdoId))) {
        strlcpy(balanceId, cdoId, sizeof(balanceId));
      } else {
        strlcpy(balanceId, ident, sizeof(balanceId));
      }
      applyClientBrandMapping();
      finishAutoDetect(profile, scanFirstResponse, scanDetectedProtocol, BRAND_METTLER, 100);
      return true;
    }
    if (isIdentityErrorLine(line)) {
      Serial.printf("SCAN ! SICS I10 error data=\"%s\" -> next\n", line);
      advanceScanProfile();
      return true;
    }
    Serial.printf("SCAN ! SICS I10 non-mettler data=\"%s\" -> next\n", line);
    advanceScanProfile();
    return true;
  }

  BalanceProtocol protocol;
  uint8_t brand;
  uint8_t score = classifyWeightResponse(line, profile, protocol, brand);
  Serial.printf("SCAN > score=%u proto=%s data=\"%s\"\n", score, protocolName(protocol), line);
  if (score < 60) return true;

  const bool profileMatchesDetected =
    (profile.protocol == PROTOCOL_UNKNOWN || profile.protocol == protocol);
  if (!profileMatchesDetected) {
    Serial.printf("SCAN ! incompatible profile=%s detected=%s -> next\n",
                  protocolName(profile.protocol), protocolName(protocol));
    advanceScanProfile();
    return true;
  }

  if (scanState == SCAN_WAIT_RESPONSE) {
    strlcpy(scanFirstResponse, line, sizeof(scanFirstResponse));
    scanFirstScore = score;
    scanDetectedProtocol = protocol;
    scanDetectedBrand = brand;
    clearBalanceInput();
    if (protocol == PROTOCOL_SICS) sendBalanceCmdWithEnding("I10", profile.ending);
    else sendBalanceCmdWithEnding(profile.command, profile.ending);
    scanState = SCAN_CONFIRM_WAIT;
    scanStateSince = millis();
  } else {
    if (protocol == scanDetectedProtocol ||
        (protocol == PROTOCOL_GENERIC_ASCII && scanDetectedProtocol == profile.protocol)) {
      uint8_t bestScore = scanFirstScore > score ? scanFirstScore : score;
      uint8_t finalScore = bestScore >= 96 ? 100 : (uint8_t)(bestScore + 4);
      finishAutoDetect(profile, line, scanDetectedProtocol, scanDetectedBrand, finalScore);
    }
  }
  return true;
}

void runAutoDetect() {
  if (scanState == SCAN_OFF || scanState == SCAN_DONE || scanState == SCAN_FAILED) return;
  unsigned long now = millis();

  if (scanState == SCAN_BOOT_WAIT) {
    if (now - scanStateSince >= AUTODETECT_BOOT_DELAY_MS) {
      scanState = SCAN_CONFIGURE;
      scanStateSince = now;
    }
    return;
  }

  const ScanProfile& profile = activeScanProfile();
  if (scanState == SCAN_CONFIGURE) {
    configureBalanceSerial(profile.baud, profile.parity, profile.dataBits, profile.stopBits);
    cfg_lineTimeout = profile.timeoutMs > 200 ? 200 : profile.timeoutMs;
    Serial.printf("SCAN %u/%u %s %lu/%u%c%u cmd=",
                  (unsigned)(scanProfileIndex + 1), (unsigned)activeScanProfileCount(),
                  profile.label, (unsigned long)profile.baud, profile.dataBits,
                  profile.parity == 1 ? 'E' : profile.parity == 2 ? 'O' : 'N',
                  profile.stopBits);
    for (const char* p = profile.command; *p; p++) Serial.printf("%02X ", (uint8_t)*p);
    Serial.println();
    scanState = SCAN_SETTLE;
    scanStateSince = now;
    dirty = true;
    return;
  }

  if (scanState == SCAN_SETTLE && now - scanStateSince >= AUTODETECT_SETTLE_MS) {
    sendBalanceCmdWithEnding(profile.command, profile.ending);
    scanState = SCAN_WAIT_RESPONSE;
    scanStateSince = now;
    return;
  }

  if ((scanState == SCAN_WAIT_RESPONSE || scanState == SCAN_CONFIRM_WAIT) &&
      now - scanStateSince > profile.timeoutMs) {
    if (scanState == SCAN_WAIT_RESPONSE && scanAttempt == 0) {
      scanAttempt = 1;
      scanState = SCAN_RETRY_DELAY;
      scanStateSince = now;
    } else {
      advanceScanProfile();
    }
    return;
  }

  if (scanState == SCAN_RETRY_DELAY && now - scanStateSince >= AUTODETECT_RETRY_DELAY_MS) {
    clearBalanceInput();
    sendBalanceCmdWithEnding(profile.command, profile.ending);
    scanState = SCAN_WAIT_RESPONSE;
    scanStateSince = now;
  }
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
  if (isIdentityErrorLine(line)) {
    if (pendingIdentity == ID_TYPE || pendingIdentity == ID_SICS_TYPE ||
        pendingIdentity == ID_RADWAG_TYPE || pendingIdentity == ID_DINI_VERSION) {
      identityTypeDone = true;
    } else if (pendingIdentity == ID_SERIAL || pendingIdentity == ID_SICS_SERIAL ||
               pendingIdentity == ID_RADWAG_SERIAL) {
      identitySerialDone = true;
    } else if (pendingIdentity == ID_BALANCE_ID) {
      identityPrimaryIdTried = true;
    } else if (pendingIdentity == ID_BALANCE_ID_ALT) {
      identityAltIdTried = true;
    }
    Serial.printf("BALANCE ID > %s\n", line);
    pendingIdentity = ID_NONE;
    nextIdentityQueryAt = millis() + IDENTITY_QUERY_DELAY_MS;
    dirty = true;
    return true;
  }

  if (pendingIdentity == ID_TYPE || pendingIdentity == ID_SICS_TYPE ||
      pendingIdentity == ID_RADWAG_TYPE || pendingIdentity == ID_DINI_VERSION) {
    compactIdentityLine(line, balanceType, sizeof(balanceType));
    identityTypeDone = true;
    char upper[64];
    strlcpy(upper, line, sizeof(upper));
    for (size_t i = 0; upper[i]; i++) if (upper[i] >= 'a' && upper[i] <= 'z') upper[i] -= 32;
    if (strstr(upper, "METTLER") || strstr(upper, "TOLEDO")) cfg_brand = BRAND_METTLER;
    else if (strstr(upper, "OHAUS")) cfg_brand = BRAND_OHAUS;
    else if (strstr(upper, "SARTORIUS")) cfg_brand = BRAND_SARTORIUS;
    else if (pendingIdentity == ID_RADWAG_TYPE || strstr(upper, "RADWAG")) cfg_brand = BRAND_RADWAG;
    else if (pendingIdentity == ID_DINI_VERSION) cfg_brand = BRAND_DINI;
  } else if (pendingIdentity == ID_SERIAL || pendingIdentity == ID_SICS_SERIAL ||
             pendingIdentity == ID_RADWAG_SERIAL) {
    compactIdentityLine(line, balanceSerial, sizeof(balanceSerial));
    identitySerialDone = true;
  } else if (pendingIdentity == ID_BALANCE_ID || pendingIdentity == ID_BALANCE_ID_ALT) {
    char identitySource[64];
    const char* rawIdentity = line;
    if (pendingIdentity == ID_BALANCE_ID_ALT) {
      if (extractQuotedIdentityValue(line, identitySource, sizeof(identitySource))) {
        rawIdentity = identitySource;
      } else {
        return false;
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
  saveConfig();
  dirty = true;
  sendToHub(MSG_STATUS, "IDENTITY_UPDATED");
  return true;
}

void runIdentityQuery() {
  if (pendingIdentity != ID_NONE) {
    if (millis() - pendingIdentityStartedAt > IDENTITY_QUERY_TIMEOUT_MS) {
      if (pendingIdentity == ID_TYPE || pendingIdentity == ID_SICS_TYPE ||
          pendingIdentity == ID_RADWAG_TYPE || pendingIdentity == ID_DINI_VERSION) {
        identityTypeDone = true;
      } else if (pendingIdentity == ID_SERIAL || pendingIdentity == ID_SICS_SERIAL ||
                 pendingIdentity == ID_RADWAG_SERIAL) {
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

  if (state != N_IDLE || millis() < nextIdentityQueryAt ||
      (scanState != SCAN_DONE && scanState != SCAN_OFF && scanState != SCAN_FAILED)) return;

  if (cfg_protocol == PROTOCOL_SICS) {
    if (isUnknownIdentity(balanceId) && !identityAltIdTried) {
      sendIdentityQuery(ID_BALANCE_ID_ALT, "I10");
    }
    return;
  }

  if (cfg_protocol == PROTOCOL_RADWAG) {
    if (!identityTypeDone) { sendIdentityQuery(ID_RADWAG_TYPE, "BN"); return; }
    if (!identitySerialDone) { sendIdentityQuery(ID_RADWAG_SERIAL, "NB"); return; }
    return;
  }

  if (cfg_protocol == PROTOCOL_DINI) {
    if (!identityTypeDone) sendIdentityQuery(ID_DINI_VERSION, "VER");
    return;
  }

  if (cfg_protocol != PROTOCOL_AD) return;

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
  Serial.printf("BALANCE > %s\n", line);
  if (handleScanLine(line)) return;
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
    balLastByteAt = millis();
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

  // Certaines balances terminent par timeout ou utilisent une longueur fixe.
  if (balLineLen > 0 && balLastByteAt && millis() - balLastByteAt > cfg_lineTimeout) {
    balLineBuf[balLineLen] = 0;
    onBalanceLine(balLineBuf);
    balLineLen = 0;
    balLastWasCR = false;
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
  Serial.printf("HUB CMD < %s\n", cmd);
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
               "%.20s uptime=%lu last=\"%.40s\" lastcmd=%.10s brand=%d proto=%d conf=%u scan=%d baud=%lu fw=\"%.20s\" type=\"%.18s\" sn=\"%.18s\" bid=\"%.12s\" label=\"%.20s\"",
               cfg_name, millis() / 1000UL,
               latestValue[0] ? latestValue : "",
               lastCmdStatus, cfg_brand, (int)cfg_protocol, cfg_confidence,
               (int)scanState, (unsigned long)cfg_baud, FW_VARIANT_CODE,
               balanceType, balanceSerial, balanceId, cdoLabel());
      sendMsg(info->src_addr, MSG_NODE_INFO_RESP, buf);
      break;
    }

    case MSG_COMMAND:
      if (isIdentifyCommand(m.payload)) {
        identifyUntil = millis() + 2500;
        dirty = true;
      } else if (m.payload[0]) {
        startQuery(m.payload);
      }
      break;

    case MSG_CONFIG_REQ: {
      // Sérialise la config courante en JSON → MSG_CONFIG_RESP
      StaticJsonDocument<256> doc;
      doc["n"]   = cfg_name;
      doc["tn"]  = cfg_typeName;
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
      doc["pr"]  = (uint8_t)cfg_protocol;
      doc["cf"]  = cfg_confidence;
      doc["as"]  = cfg_autodetect ? 1 : 0;
      doc["en"]  = (uint8_t)cfg_lineEnding;
      doc["fv"]  = FW_VARIANT_CODE;
      char buf[200];
      serializeJson(doc, buf, sizeof(buf));
      sendMsg(info->src_addr, MSG_CONFIG_RESP, buf);
      break;
    }

    case MSG_CONFIG_SET: {
      // Parse JSON, sauvegarde NVS, reboot
      StaticJsonDocument<256> doc;
      if (deserializeJson(doc, m.payload) == DeserializationError::Ok) {
        if (doc.containsKey("n"))   strlcpy(cfg_name, doc["n"], sizeof(cfg_name));
        if (doc.containsKey("tn"))  strlcpy(cfg_typeName, doc["tn"], sizeof(cfg_typeName));
        if (doc.containsKey("lb"))  strlcpy(cfg_label, doc["lb"], sizeof(cfg_label));
        if (doc.containsKey("cp"))  cfg_capacity = doc["cp"].as<float>();
        if (doc.containsKey("rs"))  cfg_resolution = doc["rs"].as<float>();
#if !FW_LOCK_SERIAL_DEFAULTS
        if (doc.containsKey("br"))  cfg_brand       = doc["br"];
        if (doc.containsKey("bd"))  cfg_baud        = doc["bd"];
        if (doc.containsKey("pa"))  cfg_parity      = doc["pa"];
        if (doc.containsKey("db"))  cfg_dataBits    = doc["db"];
        if (doc.containsKey("sb"))  cfg_stopBits    = doc["sb"];
        if (doc.containsKey("rx"))  cfg_rxPin       = doc["rx"];
        if (doc.containsKey("tx"))  cfg_txPin       = doc["tx"];
        if (doc.containsKey("sw"))  cfg_swapRxTx    = (doc["sw"].as<int>() != 0);
        if (doc.containsKey("cmd")) strlcpy(cfg_pollCmd, doc["cmd"], sizeof(cfg_pollCmd));
        if (doc.containsKey("to"))  cfg_lineTimeout = doc["to"];
        if (doc.containsKey("zc"))  strlcpy(cfg_zeroCmd, doc["zc"], sizeof(cfg_zeroCmd));
        if (doc.containsKey("pr"))  cfg_protocol = (BalanceProtocol)doc["pr"].as<uint8_t>();
        if (doc.containsKey("cf"))  cfg_confidence = doc["cf"];
        if (doc.containsKey("as"))  cfg_autodetect = doc["as"].as<int>() != 0;
        if (doc.containsKey("en"))  cfg_lineEnding = (LineEnding)doc["en"].as<uint8_t>();
        if (!doc.containsKey("pr")) cfg_protocol = protocolForBrand(cfg_brand);
        if (!doc.containsKey("en")) cfg_lineEnding = END_CRLF;
#endif
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

    case MSG_PURGE_RESET: {
      sendMsg(info->src_addr, MSG_CONFIG_ACK, "purged");
      delay(120);
      purgeStoredNodeConfig();
      delay(80);
      ESP.restart();
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

  // Card valeur / progression du scan
  sprite.fillRoundRect(6, 32, SCREEN_W - 12, 64, 10, identifyBlink ? COLOR_SUCCESS : COLOR_SURFACE);

  bool scanning = scanState == SCAN_BOOT_WAIT || scanState == SCAN_CONFIGURE ||
                  scanState == SCAN_SETTLE || scanState == SCAN_WAIT_RESPONSE ||
                  scanState == SCAN_RETRY_DELAY || scanState == SCAN_CONFIRM_WAIT;
  if (scanning) {
    sprite.setTextDatum(middle_center);
    sprite.setFont(&fonts::FreeSansBold9pt7b);
    sprite.setTextColor(COLOR_PRIMARY);
    sprite.drawString("Detection RS232", SCREEN_W / 2, 49);
    if (scanState != SCAN_BOOT_WAIT && scanProfileIndex < activeScanProfileCount()) {
      const ScanProfile& profile = activeScanProfile();
      char scanLine[40];
      snprintf(scanLine, sizeof(scanLine), "%s %lu/%u%c%u", profile.label,
               (unsigned long)profile.baud, profile.dataBits,
               profile.parity == 1 ? 'E' : profile.parity == 2 ? 'O' : 'N',
               profile.stopBits);
      sprite.setFont(&fonts::FreeSans9pt7b);
      sprite.setTextColor(COLOR_TEXT);
      sprite.drawString(scanLine, SCREEN_W / 2, 70);
      int total = max(1, (int)activeScanProfileCount());
      int width = (int)((SCREEN_W - 28) * (scanProfileIndex + 1) / total);
      sprite.fillRoundRect(14, 86, SCREEN_W - 28, 4, 2, COLOR_DIVIDER);
      sprite.fillRoundRect(14, 86, width, 4, 2, COLOR_PRIMARY);
    }
    sprite.setTextDatum(top_left);
  }
  else if (state == N_QUERYING) {
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

  configureBalanceSerial(cfg_baud, cfg_parity, cfg_dataBits, cfg_stopBits);

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

  Serial.printf("Node %s id=%d brand=%d baud=%lu RX=%d TX=%d\n",
                cfg_name, cfg_nodeId, cfg_brand, (unsigned long)cfg_baud, cfg_rxPin, cfg_txPin);
  drawScreen();
}

void loop() {
  M5.update();

  // RS232 balance non-bloquante
  readBalanceNonBlocking();
  scanButtonLatched = false;

  // Timeout balance
  if (state == N_QUERYING && scanState != SCAN_BOOT_WAIT &&
      scanState != SCAN_CONFIGURE && scanState != SCAN_SETTLE &&
      scanState != SCAN_WAIT_RESPONSE && scanState != SCAN_RETRY_DELAY &&
      scanState != SCAN_CONFIRM_WAIT && millis() - stateStart > BAL_TIMEOUT_MS) {
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
