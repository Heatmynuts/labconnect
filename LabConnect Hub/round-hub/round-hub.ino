#include "lcd_bsp.h"
#include "cst816.h"
#include "lcd_bl_pwm_bsp.h"
#include "lcd_config.h"
#include "extra/libs/qrcode/qrcodegen.h"
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Preferences.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <driver/i2s_std.h>
#include <driver/gpio.h>
#include <math.h>
#include <stdarg.h>

#define ENC_A   8
#define ENC_B   7
#define ENC_SW  6

static volatile int32_t encRaw = 0;

// =====================================================
// CONFIG
// =====================================================
#define MAX_NODES             20
#define MAX_PROFILES          24
#define DISPLAY_W             360
#define DISPLAY_H             360
#define SCAN_DURATION_MS      3000
#define DISCOVER_INTERVAL_MS  300
#define DISPLAY_REFRESH_MS    50
#define HISTORY_SIZE          3
#define CMD_TIMEOUT_MS        4000
#define OFFLINE_TIMEOUT_MS    5000
#define USB_BOOT_MUTE_MS      5000

// WiFi AP
#define AP_SSID_PREFIX  "BDP-Hub-"
#define AP_PASS         "bdphub1234"
#define AP_CHANNEL      1

// Marques balance
#define BRAND_AD        0
#define BRAND_METTLER   1
#define BRAND_SARTORIUS 2
static const char* BRAND_NAMES[] = { "A&D", "Mettler", "Sartorius" };

#define PROTOCOL_UNKNOWN        0
#define PROTOCOL_AD             1
#define PROTOCOL_SICS           2
#define PROTOCOL_SARTORIUS_SBI  3

#define LINE_ENDING_CR          0
#define LINE_ENDING_LF          1
#define LINE_ENDING_CRLF        2

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
// THEME iOS
// =====================================================
#define COLOR_BG          0xF7BE
#define COLOR_SURFACE     0xFFFF
#define COLOR_SURFACE_HI  0xEF7D
#define COLOR_DIVIDER     0xDEFB
#define COLOR_PRIMARY     0x041F
#define COLOR_PRIMARY_HI  0x4D7F
#define COLOR_PRIMARY_BG  0xE71F
#define COLOR_SUCCESS     0x36E5
#define COLOR_WARNING     0xFCA0
#define COLOR_ERROR       0xF986
#define COLOR_TEXT        0x0000
#define COLOR_TEXT_SEC    0x4A49
#define COLOR_TEXT_DIM    0x8C92
#define COLOR_TEXT_TERT   0xBDD7

// =====================================================
// Types
// =====================================================
typedef struct {
  uint8_t  id;
  uint8_t  mac[6];
  char     nodeName[32];
  char     typeName[32];
  unsigned long lastSeen;
  bool     active;
  bool     persisted;
  // Config (peuplé via MSG_CONFIG_RESP ou NVS)
  bool     configKnown;
  bool     balReady;
  uint8_t  brand;
  uint32_t baud;
  uint8_t  parity;      // 0=N 1=E 2=O
  uint8_t  dataBits;
  uint8_t  stopBits;
  uint8_t  rxPin;
  uint8_t  txPin;
  bool     swapRxTx;
  char     pollCmd[16];
  uint16_t lineTimeout;
  char     zeroCmd[16];
  float    capacity;
  float    resolution;
  char     firmwareVariant[24];
  char     balanceId[21];
  char     displayLabel[32];
} node_entry_t;

typedef struct __attribute__((packed)) {
  uint8_t  id;
  uint8_t  mac[6];
  char     nodeName[32];
  char     typeName[32];
} stored_node_t;

typedef struct __attribute__((packed)) {
  char     balanceId[21];
  char     displayLabel[32];
} stored_node_meta_t;

typedef struct __attribute__((packed)) {
  uint8_t  brand;
  uint32_t baud;
  uint8_t  parity;
  uint8_t  dataBits;
  uint8_t  stopBits;
  uint8_t  rxPin;
  uint8_t  txPin;
  bool     swapRxTx;
  char     pollCmd[16];
  uint16_t lineTimeout;
  char     zeroCmd[16];
  float    capacity;
  float    resolution;
} stored_config_t;

typedef struct __attribute__((packed)) {
  char     profileName[32];
  char     label[32];
  char     typeName[32];
  uint8_t  brand;
  uint32_t baud;
  uint8_t  parity;
  uint8_t  dataBits;
  uint8_t  stopBits;
  uint8_t  rxPin;
  uint8_t  txPin;
  bool     swapRxTx;
  char     pollCmd[16];
  uint16_t lineTimeout;
  char     zeroCmd[16];
  float    capacity;
  float    resolution;
} stored_profile_t;

enum UIState { STATE_IDLE, STATE_SCANNING, STATE_LIST, STATE_CONNECTED, STATE_WIFI_QR };

// Pending command tracking
typedef enum { PEND_NONE, PEND_BALANCE, PEND_PING, PEND_INFO } pending_kind_t;

static uint8_t protocolForBrand(uint8_t brand);
static uint8_t lineEndingForBrand(uint8_t brand);
static void normalizeNodeIdentity(int idx);
void setNodeDisplayLabel(int idx);
static bool sendBalanceCmdAndWait(int idx, const char* cmd, char* out, size_t outSize, unsigned long timeoutMs);
static bool parseBalanceIdFromReply(uint8_t brand, const char* raw, char* out, size_t outSize);

// =====================================================
// Globals
// =====================================================
static node_entry_t nodes[MAX_NODES];
static int  nodeCount = 0;
static int  selectedNode = -1;
static int  listScroll = 0;
static UIState uiState = STATE_LIST;
static float tareOffset[MAX_NODES];
static float pendingTareAdjust[MAX_NODES];
static bool  pendingTare[MAX_NODES];
static int  listAnimFrom = -1;
static int  listAnimTo = -1;
static int  listAnimDir = 0;
static unsigned long listAnimStart = 0;

static unsigned long scanStart = 0;
static unsigned long lastDiscover = 0;

static struct {
  pending_kind_t kind;
  uint8_t  nodeId;
  uint8_t  mac[6];
  unsigned long sentAt;
} pending = { PEND_NONE, 0, {0}, 0 };
static bool pendingToPc = false;

static char lastValue[64] = "";
static char lastStatus[32] = "";
static unsigned long lastValueTime = 0;
#define VALUE_TTL_MS 2000
static char history[HISTORY_SIZE][64];
static unsigned long historyTime[HISTORY_SIZE];
static int  historyCount = 0;

static char serialLine[256];
static int  serialLen = 0;
static bool serialLastWasCR = false;
// USB serie reservee par defaut au logiciel client: pas de logs de boot parasites.
static bool serialHostMode = true;
static unsigned long usbBootMuteUntil = 0;

static unsigned long badPackets = 0;
static uint16_t txSeq = 0;
static bool displayDirty = true;
static unsigned long lastDisplayUpdate = 0;
static unsigned long lastNavAt = 0;

static char apSsid[32];
static char apIp[16];
static stored_profile_t profiles[MAX_PROFILES];
static int profileCount = 0;

static bool soundEnabled = true;
static bool serialReplyBeepEnabled = true;
static bool serialReplyHapticEnabled = true;
static int pendingConfigPushIdx = -1;
static float parseWeightValue(const char* raw);
static volatile bool pendingBeep = false;
static bool apiBalanceReplyPending = false;
static bool apiBalanceReplyReady = false;
static uint8_t apiBalanceReplyNodeId = 0;
static char apiBalanceReply[128] = "";
static int  flashBtnIdx = -1;
static unsigned long flashBtnAt = 0;
#define BTN_FLASH_MS 120
static bool hapticReady = false;

#define DRV2605_ADDR          0x5A
#define DRV2605_REG_MODE      0x01
#define DRV2605_REG_LIBRARY   0x03
#define DRV2605_REG_WAVESEQ1  0x04
#define DRV2605_REG_WAVESEQ2  0x05
#define DRV2605_REG_GO        0x0C

#define AUDIO_BCLK_PIN        GPIO_NUM_39
#define AUDIO_WS_PIN          GPIO_NUM_40
#define AUDIO_DOUT_PIN        GPIO_NUM_41
#define AUDIO_CTRL_PIN        GPIO_NUM_0
#define AUDIO_SAMPLE_RATE     44100
static i2s_chan_handle_t audioTxChan = NULL;
static bool audioReady = false;

Preferences prefs;
WebServer   webServer(80);

static void hubLogPrintf(const char* fmt, ...) {
  if (serialHostMode) return;
  va_list args;
  va_start(args, fmt);
  Serial.vprintf(fmt, args);
  va_end(args);
}

static void hubLogPrintln(const char* line) {
  if (serialHostMode) return;
  Serial.println(line);
}

static bool usbSerialMuted() {
  return millis() < usbBootMuteUntil;
}

static bool i2cWrite8(uint8_t addr, uint8_t reg, uint8_t val) {
  uint8_t buf[2] = { reg, val };
  return i2c_master_write_to_device(I2C_NUM_0, addr, buf, sizeof(buf), pdMS_TO_TICKS(20)) == ESP_OK;
}

static void hapticInit() {
  hapticReady = false;
  if (!i2cWrite8(DRV2605_ADDR, DRV2605_REG_MODE, 0x00)) return;
  i2cWrite8(DRV2605_ADDR, DRV2605_REG_LIBRARY, 0x01);
  i2cWrite8(DRV2605_ADDR, DRV2605_REG_WAVESEQ2, 0x00);
  hapticReady = true;
}

static void hapticPulse(uint8_t effect = 1) {
  if (!hapticReady) return;
  i2cWrite8(DRV2605_ADDR, DRV2605_REG_WAVESEQ1, effect);
  i2cWrite8(DRV2605_ADDR, DRV2605_REG_WAVESEQ2, 0x00);
  i2cWrite8(DRV2605_ADDR, DRV2605_REG_GO, 0x01);
}

static void audioInit() {
  audioReady = false;
  gpio_config_t gpioConf = {};
  gpioConf.pin_bit_mask = 1ULL << AUDIO_CTRL_PIN;
  gpioConf.mode = GPIO_MODE_OUTPUT;
  gpioConf.pull_up_en = GPIO_PULLUP_ENABLE;
  gpioConf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  gpioConf.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&gpioConf);
  gpio_set_level(AUDIO_CTRL_PIN, 1);

  i2s_chan_config_t txCfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
  if (i2s_new_channel(&txCfg, &audioTxChan, NULL) != ESP_OK) return;

  i2s_std_config_t stdCfg = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
    .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
    .gpio_cfg = {
      .mclk = I2S_GPIO_UNUSED,
      .bclk = AUDIO_BCLK_PIN,
      .ws = AUDIO_WS_PIN,
      .dout = AUDIO_DOUT_PIN,
      .din = I2S_GPIO_UNUSED,
      .invert_flags = {
        .mclk_inv = false,
        .bclk_inv = false,
        .ws_inv = false,
      },
    },
  };
  if (i2s_channel_init_std_mode(audioTxChan, &stdCfg) != ESP_OK) return;
  if (i2s_channel_enable(audioTxChan) != ESP_OK) return;
  audioReady = true;
}

static void beep(uint16_t freq = 1800, uint16_t durationMs = 55) {
  if (!audioReady || !audioTxChan) return;
  const int chunk = 128;
  int16_t samples[chunk];
  uint32_t total = (AUDIO_SAMPLE_RATE * durationMs) / 1000;
  uint32_t done = 0;
  while (done < total) {
    uint32_t n = min((uint32_t)chunk, total - done);
    for (uint32_t i = 0; i < n; i++) {
      float t = (float)(done + i) / (float)AUDIO_SAMPLE_RATE;
      float env = 1.0f;
      if (done + i < 180) env = (done + i) / 180.0f;
      if (total - (done + i) < 220) env = (total - (done + i)) / 220.0f;
      samples[i] = (int16_t)(sinf(2.0f * 3.14159265f * freq * t) * 12000.0f * env);
    }
    size_t written = 0;
    i2s_channel_write(audioTxChan, samples, n * sizeof(int16_t), &written, pdMS_TO_TICKS(20));
    done += n;
  }
}

static void playSerialReplyBeep() {
  if (!serialReplyBeepEnabled) return;
  beep(1560, 22);
  delay(8);
  beep(1960, 34);
}

static void playSerialReplyHaptic() {
  if (!serialReplyHapticEnabled) return;
  hapticPulse(17);
}

static void triggerButtonFeedback(int idx, uint8_t hapticEffect = 1) {
  flashBtnIdx = idx;
  flashBtnAt = millis();
  hapticPulse(hapticEffect);
}

void IRAM_ATTR encISR_A() {
  bool a = digitalRead(ENC_A);
  bool b = digitalRead(ENC_B);
  encRaw += (a == b) ? 1 : -1;
}

// =====================================================
// Page HTML (PROGMEM)
// =====================================================
static const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="fr">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1">
<title>BDP Hub</title>
<style>
:root{--blue:#007AFF;--green:#34C759;--red:#FF3B30;--bg:#F2F2F7;--surface:#FFF;--border:rgba(60,60,67,.12);--text:#000;--text2:#3C3C43;--text3:rgba(60,60,67,.55);--text4:rgba(60,60,67,.25)}
*{box-sizing:border-box;margin:0;padding:0;-webkit-tap-highlight-color:transparent}
body{font-family:-apple-system,BlinkMacSystemFont,'SF Pro Text',sans-serif;background:var(--bg);color:var(--text);min-height:100vh;overflow-x:hidden}

/* Header */
.hdr{background:rgba(255,255,255,.82);backdrop-filter:blur(20px);-webkit-backdrop-filter:blur(20px);border-bottom:.5px solid var(--border);position:sticky;top:0;z-index:50;height:56px;display:flex;align-items:center;padding:0 20px;gap:12px}
.hdr-icon{width:32px;height:32px;background:var(--blue);border-radius:8px;display:flex;align-items:center;justify-content:center;font-size:17px;flex-shrink:0}
.hdr-text{flex:1}
.hdr-title{font-size:17px;font-weight:700;letter-spacing:-.3px}
.hdr-sub{font-size:12px;color:var(--text3);margin-top:1px}

/* Content */
.cnt{padding:18px 12px calc(24px + env(safe-area-inset-bottom));max-width:760px;margin:0 auto}

/* Section label */
.slbl{font-size:12px;font-weight:600;color:var(--text3);text-transform:uppercase;letter-spacing:.6px;margin:24px 0 8px 4px}
.slbl:first-child{margin-top:0}
.shead{display:flex;align-items:center;justify-content:space-between;gap:10px;margin:24px 0 8px 4px;flex-wrap:wrap}
.shead .slbl{margin:0}
.sactions{display:flex;align-items:center;gap:8px}
.sbtn{border:none;border-radius:11px;background:var(--surface);color:var(--blue);box-shadow:0 1px 0 var(--border);height:34px;padding:0 12px;font-size:13px;font-weight:600;font-family:inherit;display:inline-flex;align-items:center;gap:8px;cursor:pointer;transition:transform .1s,opacity .15s}
.sbtn:active{transform:scale(.97);opacity:.8}
.sbtn[disabled]{opacity:.55;cursor:default}
.sbtn svg{width:15px;height:15px;stroke:currentColor;stroke-width:2;fill:none}

/* Info bar */
.ibar{background:var(--surface);border-radius:14px;padding:14px 16px;display:flex;align-items:center;gap:14px;box-shadow:0 1px 0 var(--border)}
.ibar-icon{font-size:22px;flex-shrink:0}
.ibar-body{flex:1;min-width:0}
.ibar-name{font-size:14px;font-weight:600;color:var(--text2)}
.ibar-hint{font-size:12px;color:var(--text3);margin-top:1px}
.ibar-ip{font-size:14px;font-weight:700;color:var(--blue);flex-shrink:0}

/* Node list */
.ncard{background:var(--surface);border-radius:14px;overflow:hidden;box-shadow:0 1px 0 var(--border)}
.ngrid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:10px;background:transparent;box-shadow:none;border-radius:0;overflow:visible}
.node-card{aspect-ratio:1/.68;background:var(--surface);border-radius:14px;box-shadow:0 1px 0 var(--border);padding:10px;display:flex;flex-direction:column;justify-content:flex-start;gap:7px;cursor:pointer;transition:background .12s,transform .1s;min-width:0;position:relative}
.node-card:active{background:#F4F4F4;transform:scale(.98)}
.node-card.node-add{align-items:center;justify-content:center;gap:10px;background:#F7F9FD;border:1px dashed rgba(0,122,255,.28);box-shadow:none}
.node-card.node-add .plus{width:52px;height:52px;border-radius:18px;background:#E8F1FF;color:var(--blue);display:flex;align-items:center;justify-content:center;font-size:34px;font-weight:300;line-height:1}
.node-card.node-add .nname{font-size:16px}
.node-card.node-add .nbrand{max-width:150px}
.nrow{display:flex;align-items:center;padding:12px 16px;gap:13px;cursor:pointer;border-bottom:.5px solid var(--border);transition:background .12s}
.nrow:last-child{border-bottom:none}
.nrow:active{background:#F4F4F4}

/* Avatar */
.avt{width:44px;height:44px;border-radius:12px;background:#E8F0FE;display:flex;align-items:center;justify-content:center;font-size:14px;font-weight:800;color:var(--blue);flex-shrink:0;position:relative}
.avt-dot{position:absolute;bottom:-2px;right:-2px;width:13px;height:13px;border-radius:50%;border:2.5px solid var(--surface)}
.don{background:var(--green)}.doff{background:var(--text4)}

/* Node info */
.ninfo{flex:1;min-width:0}
.node-card .ninfo{flex:1;display:flex;flex-direction:column;justify-content:center;gap:2px}
.nname{font-size:15px;font-weight:600;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.node-card .nname{font-size:18px;text-align:center;white-space:normal;line-height:1.06}
.node-media{width:100%;height:58px;border-radius:9px;background:#F7F8FB;display:flex;align-items:center;justify-content:center;overflow:hidden;border:.5px solid var(--border)}
.node-img{width:100%;height:100%;object-fit:contain;background:#fff}
.node-mark{font-size:12px;font-weight:700;color:var(--text3);text-align:center;padding:0 8px}
.node-card.has-img .nname{font-size:16px}
.nmeta{font-size:12px;color:var(--text3);margin-top:3px;display:flex;align-items:center;gap:6px;flex-wrap:wrap}
.node-card .nmeta{justify-content:center;text-align:center;gap:5px}
.node-card .ntype{font-size:12px;font-weight:600;color:var(--text2);text-align:center;line-height:1.1}
.node-card .nbrand{font-size:11px;color:var(--text3);text-align:center;line-height:1.08}
.node-card .nstatus{margin-top:2px;display:flex;justify-content:center}
.nbadge{display:inline-flex;align-items:center;padding:2px 7px;border-radius:20px;font-size:10px;font-weight:600}
.bon{background:rgba(52,199,89,.14);color:#1A7A35}.boff{background:rgba(60,60,67,.1);color:var(--text3)}
.nacts{display:flex;align-items:center;gap:2px;flex-shrink:0}
.node-card .nacts{justify-content:center}
.node-card .avt{display:none}
.ibtn{width:30px;height:30px;border:none;border-radius:9px;background:transparent;color:var(--text3);display:flex;align-items:center;justify-content:center;cursor:pointer;transition:background .12s,color .12s,transform .1s}
.ibtn:active{background:rgba(60,60,67,.1);transform:scale(.94)}
.ibtn svg{width:16px;height:16px;stroke:currentColor}
.ibtn-del{color:var(--red)}

/* Empty / Loading */
.empty{padding:36px 20px;text-align:center;color:var(--text3)}
.empty-ico{font-size:36px;opacity:.4;margin-bottom:10px}
.empty-ttl{font-size:16px;font-weight:600;color:var(--text2);margin-bottom:5px}
.empty-sub{font-size:14px}
.loading{padding:32px;display:flex;justify-content:center}
.spin{width:24px;height:24px;border:2.5px solid var(--border);border-top-color:var(--blue);border-radius:50%;animation:rot .65s linear infinite}
@keyframes rot{to{transform:rotate(360deg)}}

/* Toast */
.toast{position:fixed;bottom:calc(24px + env(safe-area-inset-bottom));left:50%;transform:translateX(-50%) translateY(80px);opacity:0;background:rgba(28,28,30,.9);color:#fff;padding:11px 20px;border-radius:14px;font-size:14px;font-weight:500;white-space:normal;transition:transform .3s cubic-bezier(.34,1.56,.64,1),opacity .3s;z-index:200;pointer-events:none;max-width:calc(100vw - 40px);text-align:center}
.toast.show{transform:translateX(-50%) translateY(0);opacity:1}
.toast.ok:before{content:'✓  ';color:#4CD964}
.toast.err:before{content:'✕  ';color:#FF6B6B}

/* Sheet overlay */
.ov{display:none;position:fixed;inset:0;background:rgba(0,0,0,.38);z-index:100;align-items:center;justify-content:center;padding:16px}
.ov.open{display:flex}

/* Modal */
.sheet{background:var(--bg);border-radius:22px;width:min(720px,calc(100vw - 24px));max-height:min(90vh,960px);overflow-y:auto;overflow-x:hidden;box-shadow:0 28px 80px rgba(0,0,0,.22);animation:modalin .22s ease-out}
@keyframes modalin{from{transform:translateY(12px) scale(.98);opacity:.2}to{transform:translateY(0) scale(1);opacity:1}}
.sh-handle{display:none}
.sh-hdr{display:flex;align-items:center;justify-content:space-between;padding:14px 20px;position:sticky;top:0;background:rgba(242,242,247,.88);backdrop-filter:blur(12px);-webkit-backdrop-filter:blur(12px);border-bottom:.5px solid var(--border);z-index:1}
.sh-title{font-size:17px;font-weight:700}
.sh-close{color:var(--blue);font-size:15px;font-weight:600;cursor:pointer;padding:4px 0 4px 12px}
.sh-body{padding:16px}
.action-row{display:grid;grid-template-columns:1fr 1.45fr;gap:10px;margin:12px 0 4px}
.write-id-wrap{display:flex;gap:8px;align-items:stretch}
.write-id-input{min-width:0;flex:1;border:1px solid var(--border);border-radius:14px;background:var(--surface);color:var(--blue);font-family:inherit;font-size:14px;font-weight:600;padding:0 12px;outline:none}
.write-id-input:focus{border-color:rgba(0,122,255,.42);box-shadow:0 0 0 3px rgba(0,122,255,.12)}
.write-id-wrap .btn{flex:0 0 auto;width:auto;min-width:116px}
.btn-sm{padding:12px 14px;font-size:14px}
@media (max-width:640px){
  .ov{align-items:flex-end;padding:0}
  .sheet{width:100%;max-height:92vh;border-radius:20px 20px 0 0;animation:modalup .24s cubic-bezier(.32,1,.23,1)}
  @keyframes modalup{from{transform:translateY(100%)}to{transform:translateY(0)}}
  .sh-handle{display:block;width:36px;height:4px;background:var(--text4);border-radius:2px;margin:10px auto 0}
  .action-row{grid-template-columns:1fr}
  .write-id-wrap{display:grid;grid-template-columns:1fr auto}
}

/* Form */
.fg{background:var(--surface);border-radius:14px;overflow:hidden;margin-bottom:4px;box-shadow:0 1px 0 var(--border)}
.fr{display:flex;align-items:center;min-height:46px;padding:0 16px;border-bottom:.5px solid var(--border);gap:12px}
.fr:last-child{border-bottom:none}
.fl{font-size:15px;color:var(--text);flex:1;padding:10px 0;white-space:nowrap}
.fc{flex:0 0 auto;border:none;outline:none;background:transparent;font-size:15px;font-family:inherit;color:var(--blue);text-align:right;padding:10px 0;min-width:0;max-width:180px}
select.fc{-webkit-appearance:none;appearance:none;cursor:pointer}
input.fc[type=text]{max-width:130px}
input.fc[type=number]{max-width:76px}

@media (max-width:920px){
  .cnt{max-width:680px}
  .ngrid{grid-template-columns:repeat(2,minmax(0,1fr))}
}

/* Buttons */
.btns{display:flex;flex-direction:column;gap:10px;margin-top:20px}
.btn{width:100%;padding:15px;border:none;border-radius:14px;font-size:16px;font-weight:600;font-family:inherit;cursor:pointer;letter-spacing:-.1px;transition:opacity .15s,transform .1s}
.btn:active{opacity:.72;transform:scale(.98)}
.btn-p{background:var(--blue);color:#fff}
.btn-s{background:rgba(60,60,67,.1);color:var(--text)}
/* Toggle switch */
.sw{width:51px;height:31px;background:var(--text4);border-radius:16px;position:relative;transition:background .2s;flex-shrink:0;pointer-events:none}
.sw::after{content:'';position:absolute;width:27px;height:27px;border-radius:50%;background:#fff;top:2px;left:2px;transition:left .2s;box-shadow:0 2px 4px rgba(0,0,0,.2)}
.sw.on{background:var(--green)}.sw.on::after{left:22px}

@media (max-width:640px){
  .hdr{padding:0 14px}
  .cnt{padding:14px 10px calc(24px + env(safe-area-inset-bottom))}
  .ibar{display:grid;grid-template-columns:auto 1fr;align-items:center}
  .ibar-ip{grid-column:1/-1;padding-left:36px;font-size:13px}
  .ngrid{grid-template-columns:1fr;gap:12px}
  .node-card{aspect-ratio:auto;min-height:236px}
  .node-card .nname{font-size:17px}
  .node-card .ntype{font-size:13px}
  .node-card .nbrand{font-size:12px}
  .ov{align-items:flex-end;padding:0}
  .sheet{width:100%;max-height:92vh;border-radius:20px 20px 0 0;animation:modalup .24s cubic-bezier(.32,1,.23,1)}
  @keyframes modalup{from{transform:translateY(100%)}to{transform:translateY(0)}}
  .sh-handle{display:block;width:36px;height:4px;background:var(--text4);border-radius:2px;margin:10px auto 0}
  .sh-hdr{padding:14px 16px}
  .sh-body{padding:14px}
  .action-row{grid-template-columns:1fr}
  .write-id-wrap{display:grid;grid-template-columns:1fr auto}
  .fr{display:block;padding:10px 16px 12px}
  .fl{display:block;padding:0 0 6px;white-space:normal}
  .fc{display:block;width:100%;max-width:none;padding:6px 0 0;text-align:left}
  input.fc[type=text],input.fc[type=number]{max-width:none}
  .btns{padding-bottom:calc(8px + env(safe-area-inset-bottom))}
}
</style>
</head>
<body>

<div class="hdr">
  <div class="hdr-icon">&#9878;</div>
  <div class="hdr-text">
    <div class="hdr-title">BDP Hub</div>
    <div class="hdr-sub" id="hdr-sub">Chargement...</div>
  </div>
</div>

<div class="cnt">
  <div class="slbl">Reseau</div>
  <div class="ibar">
    <div class="ibar-icon">&#128246;</div>
    <div class="ibar-body">
      <div class="ibar-name" id="ssid-name">BDP-Hub</div>
      <div class="ibar-hint">Connectez-vous pour configurer</div>
    </div>
    <div class="ibar-ip">192.168.4.1</div>
  </div>

  <div class="shead">
    <div class="slbl">Nodes</div>
    <div class="sactions">
      <button class="sbtn" id="scan-btn" onclick="startNodeScan()" aria-label="Rechercher des nodes">
        <svg viewBox="0 0 24 24" aria-hidden="true">
          <path d="M21 12a9 9 0 0 0-15.36-6.36"></path>
          <path d="M3 4v5h5"></path>
          <path d="M3 12a9 9 0 0 0 15.36 6.36"></path>
          <path d="M21 20v-5h-5"></path>
        </svg>
        <span id="scan-btn-label">Rechercher</span>
      </button>
    </div>
  </div>
  <div class="ngrid" id="nlist">
    <div class="loading"><div class="spin"></div></div>
  </div>

  <div class="slbl">Options</div>
  <div class="ncard">
    <div class="nrow" onclick="toggleSound()">
      <div class="avt" style="background:#F0FFF4;color:#1A7A35;font-size:19px">&#9835;</div>
      <div class="ninfo">
        <div class="nname">Son a la reception</div>
        <div class="nmeta"><span id="snd-lbl">Active</span></div>
      </div>
      <div class="sw on" id="sw-snd"></div>
    </div>
    <div class="nrow">
      <div class="avt" style="background:#EEF4FF;color:#2258B8;font-size:19px">&#9834;</div>
      <div class="ninfo">
        <div class="nname">Retour reponse serie</div>
        <div class="nmeta"><span id="sr-lbl">Bip + vibration</span></div>
      </div>
      <select class="fc" id="c-sr-mode" onchange="setSerialReplyModeFromSelect()">
        <option value="silent">Silence</option>
        <option value="beep">Bip</option>
        <option value="haptic">Vibration</option>
        <option value="both">Bip + vibration</option>
      </select>
    </div>
    <div class="nrow" onclick="purgeNodes()">
      <div class="avt" style="background:#FFF3F2;color:#C62828;font-size:18px">&#10006;</div>
      <div class="ninfo">
        <div class="nname">Purge des nodes</div>
        <div class="nmeta">Efface la liste du knob et reinitialise les Atom en ligne</div>
      </div>
    </div>
  </div>
</div>

<div class="toast" id="toast"></div>

<div class="ov" id="ov">
  <div class="sheet">
    <div class="sh-handle"></div>
    <div class="sh-hdr">
      <span class="sh-title" id="sh-title">Configuration</span>
      <span class="sh-close" onclick="closeSheet()">Fermer</span>
    </div>
    <div class="sh-body">
      <input type="hidden" id="c-id">

      <div class="slbl">Node</div>
      <div class="fg">
        <div class="fr">
          <label class="fl">Node</label>
          <input class="fc" id="c-node-ref" type="text" readonly placeholder="Node 01">
        </div>
        <div class="fr">
          <label class="fl">Nom du node</label>
          <input class="fc" id="c-node-name" type="text" maxlength="31" placeholder="Node paillasse 1">
        </div>
        <div class="fr">
          <label class="fl">Adresse MAC</label>
          <input class="fc" id="c-mac" type="text" readonly placeholder="--:--:--:--:--:--">
        </div>
      </div>

      <div class="slbl">Identite</div>
      <div class="fg">
        <div class="fr">
          <label class="fl">Marque</label>
          <select class="fc" id="c-brand" onchange="onBrand()">
            <option value="0">A&amp;D</option>
            <option value="1">Mettler</option>
            <option value="2">Sartorius</option>
          </select>
        </div>
        <div class="fr">
          <label class="fl">Identifiant</label>
          <input class="fc" id="c-label" type="text" maxlength="31" placeholder="CDO 05">
        </div>
        <div class="fr">
          <label class="fl">ID balance</label>
          <input class="fc" id="c-balance-id" type="text" maxlength="20" placeholder="ID balance" oninput="syncLabelFromBalanceId()">
        </div>
        <div class="fr">
          <label class="fl">Type</label>
          <input class="fc" id="c-name" type="text" maxlength="31" placeholder="BA-225">
        </div>
      </div>

      <div class="action-row">
        <button class="btn btn-s btn-sm" onclick="readBalanceId()">Lire l'ID</button>
        <div class="write-id-wrap" id="mettler-write-wrap">
          <input class="write-id-input" id="c-balance-id-write" type="text" maxlength="20" placeholder="ID a ecrire" aria-label="ID balance a ecrire" oninput="syncWriteBalanceId()">
          <button class="btn btn-s btn-sm" onclick="programBalanceId()">Ecrire l'ID</button>
        </div>
      </div>

      <div class="slbl">RS232</div>
      <div id="transport-lock-note" class="nmeta" style="display:none;margin:0 4px 10px 4px">Parametres imposes par le firmware de l'AtomS3.</div>
      <div class="fg">
        <div class="fr">
          <label class="fl">Baud rate</label>
          <select class="fc" id="c-baud">
            <option>1200</option><option>2400</option><option>4800</option>
            <option>9600</option><option>19200</option>
          </select>
        </div>
        <div class="fr">
          <label class="fl">Parite</label>
          <select class="fc" id="c-par">
            <option value="0">None (N)</option>
            <option value="1">Even (E)</option>
            <option value="2">Odd (O)</option>
          </select>
        </div>
        <div class="fr">
          <label class="fl">Bits de donnees</label>
          <select class="fc" id="c-db">
            <option value="7">7</option><option value="8">8</option>
          </select>
        </div>
        <div class="fr">
          <label class="fl">Bits de stop</label>
          <select class="fc" id="c-sb">
            <option value="1">1</option><option value="2">2</option>
          </select>
        </div>
        <div class="fr">
          <label class="fl">GPIO RX</label>
          <input class="fc" id="c-rx" type="number" min="0" max="48">
        </div>
        <div class="fr">
          <label class="fl">GPIO TX</label>
          <input class="fc" id="c-tx" type="number" min="0" max="48">
        </div>
        <div class="fr">
          <label class="fl">Inverser RX / TX</label>
          <select class="fc" id="c-sw">
            <option value="0">Non</option><option value="1">Oui</option>
          </select>
        </div>
      </div>

      <div class="slbl">Metrologie</div>
      <div class="fg">
        <div class="fr">
          <label class="fl">Capacite (g)</label>
          <input class="fc" id="c-cap" type="number" min="0.001" step="0.001" placeholder="5000">
        </div>
        <div class="fr">
          <label class="fl">Resolution (g)</label>
          <input class="fc" id="c-res" type="number" min="0.0001" step="0.0001" placeholder="0.01">
        </div>
      </div>

      <div class="slbl">Protocole</div>
      <div class="fg">
        <div class="fr">
          <label class="fl">Commande de lecture</label>
          <input class="fc" id="c-cmd" type="text" maxlength="15" placeholder="Q">
        </div>
        <div class="fr">
          <label class="fl">Timeout reponse (ms)</label>
          <input class="fc" id="c-to" type="number" min="100" max="5000">
        </div>
        <div class="fr">
          <label class="fl">Commande zero</label>
          <input class="fc" id="c-zero" type="text" maxlength="15" placeholder="Z">
        </div>
      </div>

      <div class="slbl">Profils</div>
      <div class="fg">
        <div class="fr">
          <label class="fl">Nom du profil</label>
          <input class="fc" id="c-profile-name" type="text" maxlength="31" placeholder="Profil balance">
        </div>
        <div class="fr">
          <label class="fl">Profil</label>
          <select class="fc" id="c-profile" onchange="syncProfileNameFromSelect()"></select>
        </div>
      </div>

      <div class="btns">
        <button class="btn btn-p" onclick="saveSheet()">Sauvegarder</button>
        <button class="btn btn-s" onclick="closeSheet()">Annuler</button>
      </div>
    </div>
  </div>
</div>

<script>
const PR={
  0:{baud:2400,parity:1,dataBits:7,stopBits:1,pollCmd:'Q', lineTimeout:300,zeroCmd:'Z'},
  1:{baud:9600,parity:0,dataBits:8,stopBits:1,pollCmd:'SI',lineTimeout:500,zeroCmd:'Z'},
  2:{baud:9600,parity:2,dataBits:8,stopBits:1,pollCmd:'P', lineTimeout:500,zeroCmd:'Z'}
};
const MET={capacity:5000,resolution:0.01};
const IMG_CDO={
  '02':'data:image/jpeg;base64,/9j/4AAQSkZJRgABAQAAAQABAAD/2wBDABELDA8MChEPDg8TEhEUGSobGRcXGTMkJh4qPDU/Pjs1OjlDS2BRQ0daSDk6U3FUWmNma2xrQFB2fnRofWBpa2f/2wBDARITExkWGTEbGzFnRTpFZ2dnZ2dnZ2dnZ2dnZ2dnZ2dnZ2dnZ2dnZ2dnZ2dnZ2dnZ2dnZ2dnZ2dnZ2dnZ2dnZ2f/wgARCABqAJYDASIAAhEBAxEB/8QAGQABAAMBAQAAAAAAAAAAAAAAAAIDBAEF/8QAFgEBAQEAAAAAAAAAAAAAAAAAAAEC/9oADAMBAAIQAxAAAAH3gAAAAAAAAAAAACJJTIsRkAAAAAACIy1cLOViSItuw6zWAAAAB5+zCYewqud0vOtXfHPtiN8tQCgAAAcw7x5kPWgedHVSlOvN2vSmSgAAAAAAAOdAAAAAAAAAAAAAAH//xAAiEAACAgICAQUBAAAAAAAAAAABAgARAxITMAQUICEiYCP/2gAIAQEAAQUC/AE1N5uJY7yaha5cuXNjORomTbrJoFifcxmNdevM9nlAcPc2MDWZusxieO246XNKUuHx6nE0P1mF2YhzMRZoBs3W2A7f1SciGUjTh+OI0PqqjUdpVWjeMhnp3EPMkwOz5vyf/8QAFhEBAQEAAAAAAAAAAAAAAAAAAUBQ/9oACAEDAQE/Ac1af//EABYRAQEBAAAAAAAAAAAAAAAAABFAUP/aAAgBAgEBPwHNGn//xAAmEAABBAAEBQUAAAAAAAAAAAAAAQIRISIwMYEyYGFxsRMgQVHB/9oACAEBAAY/AuZcVe6tV0Eam+XAqYtjiavdINHbLJFeC2r5OJD1Hbdhyx85dLJqu59p0KlBZWkFWfws6JmSx0FpJiaU4qNiIk6kZ1pJUoYXlpIlVyp//8QAJxABAAICAAQGAgMAAAAAAAAAAQARITEwQVFxIGBhgZGhscHR4fD/2gAIAQEAAT8h8gALZ2w7EG5nHM2yxb4BRpgXrLaVpp4ZXMtXLemZdZ7Zl+BAAW1CZwusrrwyOUYQm1YgLC+8+SHN9oP3MIU9EXLTR7YfUp37k2BVlB5f2jI15HrwrBcESyVo18F9EodfIg5qfEy0FmCKdchgi/5z/W54bmaxHlHpPqS/oZ+guY5Sq+yYCQOuYaQMtEreM1n3E/DFj+7YgK6HpmWxhG2teVP/2gAMAwEAAgADAAAAEAAAAAAAAAAAAABACAAAAAAAEIMCAAAAACOxM+wAAAAMIM7YAAAAAAAAEAAAAAAAAAAAAAAP/8QAGREBAAIDAAAAAAAAAAAAAAAAAQARMEBQ/9oACAEDAQE/EOFcHDUAodn/xAAZEQEBAQADAAAAAAAAAAAAAAABEQAwQFD/2gAIAQIBAT8Q8AzpckxJwVwqTs//xAAmEAEAAgECBgIDAQEAAAAAAAABABEhMVFBYXGBkaEwsWDR8BBA/9oACAEBAAE/EPwDxsN4bvFi9b6iaA3f57CdDeNadOUZZeaGvSa4nVKlFlyFL+O0HuOYcgtCuCOiQQsV3dPUtLv/ACoCa22i7vIMvSBnjK6rivNfi0iRpNaS9I2PMtl0uWj+RtEHVA38Yy9ygVf7xJSsLcHk/SOkDskfDmMDjoannG6z4ItQtE1By+InFFW7SuUy1p8zHrK3IPclAp4yj0ljBrUVw7waS8SW3B4tqJ9ukFVlFpfZTMC61vPgO3xgCII6jBsFxteHlXCPh7zfrPqYLd0M+qYGrvJ6OY4kLtcrEdEA1fmKbW1zIJmUyu7xfmNrvRLFsdQ8Mzoptb6RwkOIPrmWSIAYKtOv7/4wDQ/Af//Z',
  '05':'data:image/jpeg;base64,/9j/4AAQSkZJRgABAQAAAQABAAD/2wBDABELDA8MChEPDg8TEhEUGSobGRcXGTMkJh4qPDU/Pjs1OjlDS2BRQ0daSDk6U3FUWmNma2xrQFB2fnRofWBpa2f/2wBDARITExkWGTEbGzFnRTpFZ2dnZ2dnZ2dnZ2dnZ2dnZ2dnZ2dnZ2dnZ2dnZ2dnZ2dnZ2dnZ2dnZ2dnZ2dnZ2dnZ2f/wgARCABqAJYDASIAAhEBAxEB/8QAGgABAAMBAQEAAAAAAAAAAAAAAAIDBAEFBv/EABYBAQEBAAAAAAAAAAAAAAAAAAABAv/aAAwDAQACEAMQAAAB94AAAAiS5SL1YsZdQAAAAABnlG8y8kIQl1Lr8+hQAAAAAKLqbTN2PUQnBbtGTUdAAAQoNSHCxn6V57iUxt4sna0lv8/0F6ABGWY8/wAz6Dlz89Z73Fy+li0xDlWgz2ykVcsuVaAACq0Zo6xjbOFVkhm0iRweiPM9MoJQAAAAAAAAAAAP/8QAKBAAAgEDAgUDBQAAAAAAAAAAAQIAAxESEDAEEyAhMRQyIjQEFQ/9oACAEBAAEFAvzCbTLS8ygcs24xOWGuQWNcyj7dz5tSv1tKPt3Pm6GlHxufMfF9WlNgu6ThVqMXnhSwgPY7DMFC1gzXl5k8+5ritsE0t3/Y8dBNpxKVK0PDVlg5iT1FVZwzO8WWlooYHVEx6i0xUzlyzzC5LjN25dIcShl+y+IYi49ZQGcuYtO8vEpgQgEenpaWErrVyorW5n9//8QAGBEAAgMAAAAAAAAAAAAAAAAAAAERMFD/2gAIAQMBAT8B12JRd//EABgRAAIDAAAAAAAAAAAAAAAAAAABETBQ/9oACAECAQE/AddDc3f/xAAqEAAAAwYEBQUAAAAAAAAAAAAAARECECAhMDEyQVGBAyJhcZESQEJQYP/aAAgBAQAGPwL6GVVIEzE620BGtq+3s9op1Z6C6FoFycVFTNAhJ3V9mfI+DrBEkM3KJR8rTPp7jB4F2mRjUHxG07hXzaWDrFN0jGIczLJ7AmC2C6C7rq/rQvAqTCGMBOsF4dgRt2/Af//EACcQAQACAgEDAwMFAAAAAAAAAAEAESExQRAwcSBRYUCBwWCR0fDx/9oACAEBAAE/Ie3f0IxV5qCXuXjQzBIbLy93GmZbc9Ki4Fq1Nj9s2+e6ln+uPQiChse81mzz3fz/AIjrrz07fPe9kobagjydOMETuhR9ybajmnMESgCELTi5cnxFbK4gUB6nYgcsSIj4M9K6wW+ZbxeX/Ev/AGZbcWykMVeNbBi5SaVllBrRCrTbMoItS9+kjbKe5wOUvMvuucz7BhrsfOYwAlVhlmtuYsiqxiaYPk65WjcoW+oCoJeHSahQBQ3vcoGg37monF3vtDj+BGF45Td084iXCy9NTDIqUMPBtlK31lLyRfAlXEx4ZZKiOK5uUIE9mZL6C2xDAdckYIht/QH/2gAMAwEAAgADAAAAEAAAAAHACAAAAAADNIyAAAAAABPcJAAABBCGfBSAABI/uo5ZAAAAMJEAwwAAAAAAAAAAAAAP/8QAGxEBAAIDAQEAAAAAAAAAAAAAAQARITAxUFH/2gAIAQMBAT8Q8nPyBc4ZgjzUAcgsqUO3u//EABsRAQACAwEBAAAAAAAAAAAAAAEAMREhMFBR/9oACAECAQE/EPJ19jLdREvkq3FhzMiu3//EACkQAQACAgAFBAIBBQAAAAAAAAEAESExQVFhcYEgMKGxEPDBQFCR0eH/2gAIAQEAAT8Q9ul1Zf8AQjZY4oe6ZQK87nUmWoESV2RXu2FW6o/e8FkXhcQtKPET1hqBtK3W4Ds3rgP9zDvf490LBdN+fw2sCZMOoPDPKau8V/v0e7wfpmMF2lQ7sNs8ZqT9bp7v8n2RUnRiayHNaimGvkw295tFg2W8GoNlmn3FDXQGVtKJlkupNDgsEO4tSr5oBDNOoglth3K/IYo685TovEzJdFX6gyFalBDQ0EFS5BdwSXV9oZp8AafM4Lvx9RboXT/hG63l3AYUlIm4pngpg/MFUABY4NbgCtCiXOI4vgcu0QwtXU4BwzXP0qUAC7WgjtOPDK4rzfqXxY3YU+ZXel2EpOGjD7XHXmkV+Wq8Qt3zqcibAl2ZEw1dQVr+CN+JU0MoQWtBCwCtvLoeq7YWiOSIqAKVGO0oDjYGeTbM5BYAQdCU6m5Qs1KY2KHgAiEgBVLe3+ZoBdD9plalkw7wlTBtaqPJeGHgVTAQdgtvLp63AUW8QOx3Kjzvsx4o9yLlrUqpZ+CNMhyjhb2FjAlYVvNtfjZDxBRUxAW75PiXFELouyqq79yi7ov+6f/Z'
};
let nodes=[];
let profiles=[];
let soundOn=true;
let serialReplyBeepOn=true;
let serialReplyHapticOn=true;
let scanBusy=false;
let nodesLoading=false;
let suppressNodeErrorsUntil=0;

async function apiFetch(url, opts={}, timeoutMs=3500){
  const ctrl=new AbortController();
  const timer=setTimeout(()=>ctrl.abort(),timeoutMs);
  try{
    return await fetch(url,{...opts,signal:ctrl.signal});
  }finally{
    clearTimeout(timer);
  }
}

function pauseNodeErrors(ms=5000){
  suppressNodeErrorsUntil=Date.now()+ms;
}

function scheduleLoadNodes(delay=0, force=false){
  setTimeout(()=>loadNodes(force),delay);
}

function currentSerialReplyMode(){
  if(serialReplyBeepOn && serialReplyHapticOn) return 'both';
  if(serialReplyBeepOn) return 'beep';
  if(serialReplyHapticOn) return 'haptic';
  return 'silent';
}

function serialReplyModeLabel(mode){
  if(mode==='beep') return 'Bip';
  if(mode==='haptic') return 'Vibration';
  if(mode==='both') return 'Bip + vibration';
  return 'Silence';
}

function inferCdoId(node){
  if(node.balanceId && IMG_CDO[node.balanceId]) return node.balanceId;
  const text=(node.label||node.name||'').toUpperCase();
  const m=text.match(/CDO\\s*(0[2-6])/);
  return m?m[1]:'';
}

function normalizedBalanceIdValue(raw){
  const text=(raw||'').toUpperCase().trim();
  const m=text.match(/(0[0-9])/);
  return m?m[1]:'';
}

function typedBalanceIdValue(raw){
  return (raw||'').replace(/\D/g,'').slice(0,2);
}

function typedMettlerWriteIdValue(raw){
  return (raw||'').toUpperCase().replace(/[^A-Z0-9 ]/g,'').slice(0,20);
}

function syncLabelFromBalanceId(){
  const value=typedMettlerWriteIdValue(document.getElementById('c-balance-id').value);
  document.getElementById('c-balance-id').value=value;
  const id=normalizedBalanceIdValue(value);
  if(id){
    document.getElementById('c-label').value='CDO '+id;
  }
}

function syncWriteBalanceId(){
  const id=typedMettlerWriteIdValue(document.getElementById('c-balance-id-write').value);
  document.getElementById('c-balance-id-write').value=id;
}

function setMettlerWriteVisibility(brand){
  const isMettler=parseInt(brand,10)===1;
  const wrap=document.getElementById('mettler-write-wrap');
  if(wrap) wrap.style.display=isMettler?'block':'none';
  if(!isMettler){
    document.getElementById('c-balance-id-write').value='';
  }else{
    syncWriteBalanceId();
  }
}

function setScanBusy(on){
  scanBusy=!!on;
  const btn=document.getElementById('scan-btn');
  const lbl=document.getElementById('scan-btn-label');
  if(!btn || !lbl) return;
  btn.disabled=scanBusy;
  lbl.textContent=scanBusy?'Recherche...':'Rechercher';
}

async function parseJsonResponse(r){
  const raw=await r.text();
  if(!raw) return {};
  try{
    return JSON.parse(raw);
  }catch(e){
    throw new Error((r.url||'api')+'_json_parse: '+raw.slice(0,240));
  }
}

async function startNodeScan(){
  if(scanBusy) return;
  setScanBusy(true);
  try{
    const r=await apiFetch('/api/scan',{method:'POST'});
    const d=await parseJsonResponse(r);
    toast(r.ok?(d.message||'Recherche lancee'):(d.error||'Erreur scan'),r.ok);
    if(r.ok){
      scheduleLoadNodes(0,true);
      scheduleLoadNodes(900,true);
      scheduleLoadNodes(2400,true);
      scheduleLoadNodes(4600,true);
    }
  }catch(e){
    toast(String(e&&e.message?e.message:'Erreur reseau'),false);
  }finally{
    setTimeout(()=>setScanBusy(false),1200);
  }
}

async function purgeNodes(){
  if(!confirm('Purger tous les nodes sauvegardes du knob et reinitialiser les Atom actuellement en ligne ?')) return;
  try{
    const r=await apiFetch('/api/purge',{method:'POST'},5000);
    const d=await parseJsonResponse(r);
    const msg=r.ok
      ?((d.message||'Purge terminee')+(typeof d.remoteResetCount==='number'?' ('+d.remoteResetCount+' Atom reinitialise'+(d.remoteResetCount>1?'s':'')+')':''))
      :(d.error||'Erreur purge');
    toast(msg,r.ok);
    if(r.ok){
      nodes=[];
      scheduleLoadNodes(0,true);
      scheduleLoadNodes(1400,true);
      scheduleLoadNodes(3800,true);
    }
  }catch(e){
    toast(String(e&&e.message?e.message:'Erreur reseau'),false);
  }
}

async function loadNodes(force=false){
  if(nodesLoading) return;
  if(!force && document.getElementById('ov').classList.contains('open')) return;
  nodesLoading=true;
  try{
    const r=await apiFetch('/api/nodes',{},3000);
    if(!r.ok) throw new Error('nodes_http_'+r.status);
    const raw=await r.text();
    try{
      nodes=JSON.parse(raw);
    }catch(parseErr){
      throw new Error('nodes_json_parse: '+String(parseErr&&parseErr.message?parseErr.message:parseErr)+' :: '+raw.slice(0,240));
    }
    const n=nodes.length;
    document.getElementById('hdr-sub').textContent=n+' node'+(n!==1?'s':'');
    document.getElementById('ssid-name').textContent='BDP-Hub';
  }catch(e){
    if(Date.now()<suppressNodeErrorsUntil) return;
    const msg=String(e&&e.message?e.message:e);
    document.getElementById('nlist').innerHTML='<div class="empty"><div class="empty-ico">&#9888;</div><div class="empty-ttl">Erreur de connexion</div><div class="empty-sub">'+esc(msg)+'</div></div>';
    return;
  }finally{
    nodesLoading=false;
  }
  try{
    render();
  }catch(e){
    document.getElementById('nlist').innerHTML='<div class="empty"><div class="empty-ico">&#9888;</div><div class="empty-ttl">Erreur affichage</div><div class="empty-sub">Rechargez la page</div></div>';
  }
}

async function loadProfiles(){
  try{
    const r=await apiFetch('/api/profiles');
    if(!r.ok) throw new Error('profiles_http_'+r.status);
    const raw=await r.text();
    profiles=JSON.parse(raw);
    refreshProfileSelect();
  }catch(e){
    console.warn('loadProfiles failed',e);
  }
}

async function loadSettings(){
  try{
    const r=await apiFetch('/api/settings');
    const d=await r.json();
    setSnd(d.sound!==false);
    setSerialReplyBeep(d.serialReplyBeep!==false);
    setSerialReplyHaptic(d.serialReplyHaptic!==false);
    const mode=currentSerialReplyMode();
    document.getElementById('c-sr-mode').value=mode;
    document.getElementById('sr-lbl').textContent=serialReplyModeLabel(mode);
  }catch(e){}
}

function setSnd(on){
  soundOn=on;
  document.getElementById('sw-snd').className='sw'+(on?' on':'');
  document.getElementById('snd-lbl').textContent=on?'Active':'Desactive';
}

async function toggleSound(){
  setSnd(!soundOn);
  await pushSettings();
}

function setSerialReplyBeep(on){
  serialReplyBeepOn=on;
}

function setSerialReplyHaptic(on){
  serialReplyHapticOn=on;
}

async function pushSettings(){
  try{await apiFetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({sound:soundOn,serialReplyBeep:serialReplyBeepOn,serialReplyHaptic:serialReplyHapticOn})});}catch(e){}
}

function applySerialReplyMode(mode){
  setSerialReplyBeep(mode==='beep' || mode==='both');
  setSerialReplyHaptic(mode==='haptic' || mode==='both');
  document.getElementById('c-sr-mode').value=mode;
  document.getElementById('sr-lbl').textContent=serialReplyModeLabel(mode);
}

async function setSerialReplyModeFromSelect(){
  applySerialReplyMode(document.getElementById('c-sr-mode').value);
  await pushSettings();
}

function render(){
  const el=document.getElementById('nlist');
  const addCard='<div class="node-card node-add" onclick="openNewProfileConf()"><div class="plus">+</div><div class="ninfo"><div class="nname">Nouveau profil</div><div class="nbrand">Ouvre les parametres pour creer un profil</div></div></div>';
  const profileCards=profiles.map((p,i)=>{
    const label=p&&p.label?p.label:'';
    const match=label.match(/(02|03|04|05|06)/);
    const cdoId=inferCdoId({balanceId:match?match[1]:'',label,name:(p&&p.type)?p.type:((p&&p.name)?p.name:'')});
    const img=IMG_CDO[cdoId]||'';
    const mediaHtml=img
      ?('<div class="node-media"><img class="node-img" src="'+img+'" alt="'+(p.label||p.name||'Profil')+'"></div>')
      :('<div class="node-media"><div class="node-mark">'+(p.brandName||'Profil')+'</div></div>');
    return ''
      + '<div class="node-card '+(img?'has-img':'')+'" onclick="openProfileConfByIndex('+i+')">'
      +   mediaHtml
      +   '<div class="ninfo">'
      +     '<div class="nname">'+(p.label||p.name||'Profil')+'</div>'
      +     '<div class="ntype">'+(p.type||'Type non renseigne')+'</div>'
      +     '<div class="nbrand">'+(p.brandName||'Marque non renseignee')+'</div>'
      +     '<div class="nstatus"><span class="nbadge boff">Profil</span></div>'
      +   '</div>'
      +   '<div class="nacts">'
      +     '<button class="ibtn" onclick="event.stopPropagation();openProfileConfByIndex('+i+')" aria-label="Modifier '+(p.name||'profil')+'">'
      +       '<svg viewBox="0 0 24 24" fill="none"><path d="M12 20h9"/><path d="M16.5 3.5a2.1 2.1 0 0 1 3 3L7 19l-4 1 1-4 12.5-12.5z"/></svg>'
      +     '</button>'
      +     '<button class="ibtn ibtn-del" onclick="event.stopPropagation();deleteProfileByIndex('+i+')" aria-label="Supprimer '+(p.name||'profil')+'">'
      +       '<svg viewBox="0 0 24 24" fill="none"><path d="M3 6h18"/><path d="M8 6V4h8v2"/><path d="M19 6l-1 14H6L5 6"/><path d="M10 11v5"/><path d="M14 11v5"/></svg>'
      +     '</button>'
      +   '</div>'
      + '</div>';
  }).join('');
  if(!nodes.length && !profiles.length){
    el.innerHTML=addCard;
    return;
  }
  el.innerHTML=addCard+profileCards+nodes.map(n=>{
    const c=n.configKnown?n.config:null;
    const cdoId=inferCdoId(n);
    const img=IMG_CDO[cdoId]||'';
    const identifier=n.label||((n.balanceId&&n.balanceId!=='?')?('CDO '+n.balanceId):'');
    const type=n.type||n.typeName||'';
    const mac=n.mac||'';
    const brand=c?c.brandName:'';
    const firmware=c&&c.firmware?c.firmware:'';
    const brandLine=firmware?(brand?brand+' • '+firmware:firmware):brand;
    const mediaHtml=img
      ?('<div class="node-media"><img class="node-img" src="'+img+'" alt="'+(n.label||n.type||n.typeName||n.name||'Balance')+'"></div>')
      :('<div class="node-media"><div class="node-mark">'+(brand||'Profil client')+'</div></div>');
    return ''
      + '<div class="node-card '+(img?'has-img':'')+'" onclick="openConf('+n.id+')">'
      +   '<div class="avt">'+String(n.id).padStart(2,'0')+'<div class="avt-dot '+(n.online?'don':'doff')+'"></div></div>'
      +   mediaHtml
      +   '<div class="ninfo">'
      +     '<div class="nname">'+(identifier||'CDO ?')+'</div>'
      +     '<div class="ntype">'+(type||'Type non renseigne')+'</div>'
      +     '<div class="nbrand">'+(brandLine||'Marque non renseignee')+'</div>'
      +     '<div class="nbrand">'+mac+'</div>'
      +     '<div class="nstatus"><span class="nbadge '+(n.online?'bon':'boff')+'">'+(n.online?'En ligne':'Hors ligne')+'</span></div>'
      +   '</div>'
      +   '<div class="nacts">'
      +     '<button class="ibtn" onclick="event.stopPropagation();openConf('+n.id+')" aria-label="Modifier '+((n.nodeName||n.name)||'node')+'">'
      +       '<svg viewBox="0 0 24 24" fill="none"><path d="M12 20h9"/><path d="M16.5 3.5a2.1 2.1 0 0 1 3 3L7 19l-4 1 1-4 12.5-12.5z"/></svg>'
      +     '</button>'
      +     '<button class="ibtn ibtn-del" onclick="event.stopPropagation();deleteNode('+n.id+')" aria-label="Supprimer '+((n.nodeName||n.name)||'node')+'">'
      +       '<svg viewBox="0 0 24 24" fill="none"><path d="M3 6h18"/><path d="M8 6V4h8v2"/><path d="M19 6l-1 14H6L5 6"/><path d="M10 11v5"/><path d="M14 11v5"/></svg>'
      +     '</button>'
      +   '</div>'
      + '</div>';
  }).join('');
}

function openConf(id){
  const n=nodes.find(x=>x.id===id);if(!n)return;
  const identifier=n.label||((n.balanceId&&n.balanceId!=='?')?`CDO ${n.balanceId}`:'CDO ?');
  document.getElementById('sh-title').textContent=identifier+' · parametres';
  document.getElementById('c-id').value=id;
  document.getElementById('c-node-ref').value='Node '+String(n.id).padStart(2,'0');
  document.getElementById('c-mac').value=n.mac||'';
  document.getElementById('c-profile-name').value='';
  const c=n.configKnown?n.config:PR[0];
  document.getElementById('c-label').value=identifier;
  document.getElementById('c-balance-id').value=normalizedBalanceIdValue(n.balanceId||identifier);
  document.getElementById('c-balance-id-write').value=normalizedBalanceIdValue(n.balanceId||identifier);
  syncWriteBalanceId();
  document.getElementById('c-name').value=n.type||n.typeName||'';
  document.getElementById('c-node-name').value=n.name||n.nodeName||'';
  sv('c-brand',n.configKnown?c.brand:0);
  sv('c-baud',c.baud);sv('c-par',c.parity);
  sv('c-db',c.dataBits);sv('c-sb',c.stopBits);
  document.getElementById('c-rx').value=c.rxPin??5;
  document.getElementById('c-tx').value=c.txPin??6;
  sv('c-sw',c.swapRxTx?1:0);
  document.getElementById('c-cmd').value=c.pollCmd;
  document.getElementById('c-to').value=c.lineTimeout;
  document.getElementById('c-zero').value=c.zeroCmd||'Z';
  document.getElementById('c-cap').value=c.capacity||MET.capacity;
  document.getElementById('c-res').value=c.resolution||MET.resolution;
  setTransportLock(!!(c&&c.firmware&&c.firmware!=='ATOM_GENERIC'), c&&c.firmware?c.firmware:'');
  refreshProfileSelect();
  document.getElementById('ov').classList.add('open');
  document.querySelector('.sheet').scrollTop=0;
}

function openNewProfileConf(){
  document.getElementById('sh-title').textContent='Nouveau profil · parametres';
  document.getElementById('c-id').value='';
  document.getElementById('c-node-ref').value='Profil';
  document.getElementById('c-node-name').value='';
  document.getElementById('c-mac').value='';
  document.getElementById('c-profile').value='';
  document.getElementById('c-profile-name').value='';
  document.getElementById('c-label').value='';
  document.getElementById('c-balance-id').value='';
  document.getElementById('c-balance-id-write').value='';
  syncWriteBalanceId();
  document.getElementById('c-name').value='';
  sv('c-brand',0);
  onBrand();
  document.getElementById('c-rx').value=5;
  document.getElementById('c-tx').value=6;
  sv('c-sw',0);
  document.getElementById('c-cap').value=MET.capacity;
  document.getElementById('c-res').value=MET.resolution;
  setTransportLock(false,'');
  refreshProfileSelect();
  document.getElementById('ov').classList.add('open');
  document.querySelector('.sheet').scrollTop=0;
}

function openProfileConfByIndex(index){
  const p=profiles[index];if(!p)return;
  document.getElementById('sh-title').textContent=(p.label||p.name||'Profil')+' · parametres';
  document.getElementById('c-id').value='';
  document.getElementById('c-node-ref').value='Profil';
  document.getElementById('c-node-name').value='';
  document.getElementById('c-mac').value='';
  refreshProfileSelect();
  document.getElementById('c-profile').value=p.name||'';
  document.getElementById('c-profile-name').value=p.name||'';
  document.getElementById('c-label').value=p.label||'';
  document.getElementById('c-balance-id').value=normalizedBalanceIdValue((p.balanceId||p.label||''));
  document.getElementById('c-balance-id-write').value=normalizedBalanceIdValue((p.balanceId||p.label||''));
  syncWriteBalanceId();
  document.getElementById('c-name').value=p.type||'';
  sv('c-brand',p.brand??0);
  sv('c-baud',p.baud);
  sv('c-par',p.parity);
  sv('c-db',p.dataBits);
  sv('c-sb',p.stopBits);
  document.getElementById('c-rx').value=p.rxPin??5;
  document.getElementById('c-tx').value=p.txPin??6;
  sv('c-sw',(p.swapRxTx?1:0));
  document.getElementById('c-cmd').value=p.pollCmd||'Q';
  document.getElementById('c-to').value=p.lineTimeout||300;
  document.getElementById('c-zero').value=p.zeroCmd||'Z';
  document.getElementById('c-cap').value=p.capacity||MET.capacity;
  document.getElementById('c-res').value=p.resolution||MET.resolution;
  setTransportLock(false,'');
  document.getElementById('ov').classList.add('open');
  document.querySelector('.sheet').scrollTop=0;
}

function refreshProfileSelect(){
  const sel=document.getElementById('c-profile');
  if(!sel) return;
  const current=sel.value;
  sel.innerHTML='<option value="">Choisir un profil</option>'+profiles.map(p=>`<option value="${p.name}">${p.name}</option>`).join('');
  if(current) sel.value=current;
}

function currentProfileNameInput(){
  return (document.getElementById('c-profile-name').value||'').trim();
}

function syncProfileNameFromSelect(){
  const selected=(document.getElementById('c-profile').value||'').trim();
  const id=parseInt(document.getElementById('c-id').value);
  if(selected && (!Number.isFinite(id) || id<=0)) document.getElementById('c-profile-name').value=selected;
}

function sv(id,v){const e=document.getElementById(id);if(e)e.value=String(v);}
function setTransportLock(locked, firmware){
  const ids=['c-brand','c-baud','c-par','c-db','c-sb','c-rx','c-tx','c-sw','c-cmd','c-to','c-zero'];
  ids.forEach(id=>{const el=document.getElementById(id); if(el) el.disabled=!!locked;});
  const note=document.getElementById('transport-lock-note');
  if(note){
    note.style.display=locked?'block':'none';
    note.textContent=locked?('Parametres imposes par le firmware '+firmware+'.'):'';
  }
}
function closeSheet(){document.getElementById('ov').classList.remove('open');}
function esc(s){return String(s||'').replace(/[&<>"]/g,m=>({ '&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;' }[m]));}

function onBrand(){
  const brand=parseInt(document.getElementById('c-brand').value);
  const p=PR[brand];if(!p)return;
  sv('c-baud',p.baud);sv('c-par',p.parity);sv('c-db',p.dataBits);sv('c-sb',p.stopBits);
  document.getElementById('c-cmd').value=p.pollCmd;
  document.getElementById('c-to').value=p.lineTimeout;
  document.getElementById('c-zero').value=p.zeroCmd||'Z';
  setMettlerWriteVisibility(brand);
}

async function readBalanceId(){
  const id=parseInt(document.getElementById('c-id').value);
  if(!Number.isFinite(id) || id<=0){
    toast('Choisissez un node',false);
    return;
  }
  try{
    pauseNodeErrors(5000);
    const r=await apiFetch('/api/balance-id/read',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({id,brand:parseInt(document.getElementById('c-brand').value)})},5000);
    const d=await r.json();
    if(!r.ok){
      toast(d.error||'Lecture impossible',false);
      return;
    }
    if(d.balanceId){
      const clientId=normalizedBalanceIdValue(d.balanceId);
      if(clientId){
        document.getElementById('c-balance-id').value=clientId;
        document.getElementById('c-label').value='CDO '+clientId;
      }
      document.getElementById('c-balance-id-write').value=typedMettlerWriteIdValue(d.balanceId);
    }
    toast(d.message||('ID lu: '+(d.balanceId||d.raw||'?')),true);
    await loadNodes(true);
  }catch(e){toast('Erreur reseau',false);}
}

async function saveConf(){
  const id=parseInt(document.getElementById('c-id').value);
  if(!Number.isFinite(id) || id<=0){
    toast('Choisissez un node ou renseignez un nom de profil',false);
    return;
  }
  const body={id,...currentConfigPayload()};
  closeSheet();
  try{
    pauseNodeErrors(6500);
    const r=await apiFetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)},5000);
    const d=await r.json();
    toast(r.ok?d.message:d.error,r.ok);
    if(r.ok)scheduleLoadNodes(4200,true);
  }catch(e){toast('Erreur reseau',false);}
}

async function saveSheet(){
  const id=parseInt(document.getElementById('c-id').value);
  if(Number.isFinite(id) && id>0){
    await saveConf();
    return;
  }

  const profileName=currentProfileNameInput()||(document.getElementById('c-profile').value||'').trim();
  if(!profileName){
    toast('Renseignez un nom de profil',false);
    return;
  }
  await saveProfile();
}

function currentConfigPayload(){
  const balanceId=normalizedBalanceIdValue(document.getElementById('c-balance-id').value);
  if(balanceId){
    document.getElementById('c-balance-id').value=balanceId;
    document.getElementById('c-label').value='CDO '+balanceId;
  }
  return {
    nodeName:document.getElementById('c-node-name').value.trim(),
    balanceId,
    label:document.getElementById('c-label').value.trim(),
    name:document.getElementById('c-name').value.trim(),
    brand:parseInt(document.getElementById('c-brand').value),
    baud:parseInt(document.getElementById('c-baud').value),
    parity:parseInt(document.getElementById('c-par').value),
    dataBits:parseInt(document.getElementById('c-db').value),
    stopBits:parseInt(document.getElementById('c-sb').value),
    rxPin:parseInt(document.getElementById('c-rx').value),
    txPin:parseInt(document.getElementById('c-tx').value),
    swapRxTx:document.getElementById('c-sw').value==='1',
    pollCmd:document.getElementById('c-cmd').value.trim(),
    lineTimeout:parseInt(document.getElementById('c-to').value),
    zeroCmd:document.getElementById('c-zero').value.trim()||'Z',
    capacity:parseFloat(document.getElementById('c-cap').value)||MET.capacity,
    resolution:parseFloat(document.getElementById('c-res').value)||MET.resolution
  };
}

async function programBalanceId(){
  const id=parseInt(document.getElementById('c-id').value);
  if(!Number.isFinite(id) || id<=0){
    toast('Choisissez un node Mettler',false);
    return;
  }
  const brand=parseInt(document.getElementById('c-brand').value);
  if(brand!==1){
    toast('Disponible uniquement pour Mettler',false);
    return;
  }
  const balanceId=typedMettlerWriteIdValue(document.getElementById('c-balance-id-write').value).trim();
  if(!balanceId){
    toast('Renseignez l ID a ecrire',false);
    return;
  }
  document.getElementById('c-balance-id-write').value=balanceId;
  try{
    pauseNodeErrors(5000);
    const r=await apiFetch('/api/balance-id',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({id,balanceId})},5000);
    const d=await r.json();
    toast(r.ok?(d.message||'ID programme'):(d.error||'Erreur'),r.ok);
    if(r.ok){
      const clientId=normalizedBalanceIdValue(balanceId);
      document.getElementById('c-balance-id').value=balanceId;
      if(clientId){
        document.getElementById('c-label').value='CDO '+clientId;
      }
      await loadNodes(true);
    }
  }catch(e){toast('Erreur reseau',false);}
}

async function saveProfile(){
  const currentProfile=(document.getElementById('c-profile').value||'').trim();
  const typedProfileName=currentProfileNameInput();
  const profileName=typedProfileName||currentProfile||(document.getElementById('c-label').value||'Profil').trim();
  if(!profileName) return;
  const body={profileName,...currentConfigPayload()};
  try{
    pauseNodeErrors(6500);
    const r=await apiFetch('/api/profiles',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)},5000);
    const d=await r.json();
    if(!r.ok){
      toast(d.error||'Erreur',false);
      return;
    }
    await loadProfiles();
    document.getElementById('c-profile').value=profileName;

    const id=parseInt(document.getElementById('c-id').value);
    if(!Number.isFinite(id) || id<=0){
      toast('Profil sauvegarde',true);
      closeSheet();
      return;
    }
    const ra=await apiFetch('/api/profiles/apply',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({id,profileName})},5000);
    const da=await ra.json();
    toast(ra.ok?'Profil sauvegarde et applique':(da.error||'Profil sauvegarde, application impossible'),ra.ok);
    if(ra.ok){
      closeSheet();
      scheduleLoadNodes(4200,true);
    }
  }catch(e){toast('Erreur reseau',false);}
}

async function duplicateProfile(){
  const currentProfile=(document.getElementById('c-profile').value||'').trim();
  const baseName=currentProfile||(document.getElementById('c-label').value||'Profil').trim();
  const typedProfileName=currentProfileNameInput();
  const profileName=typedProfileName||(baseName?`${baseName} copie`:'Nouveau profil');
  if(!profileName) return;
  document.getElementById('c-profile').value='';
  const body={profileName,...currentConfigPayload()};
  try{
    pauseNodeErrors(6500);
    const r=await apiFetch('/api/profiles',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)},5000);
    const d=await r.json();
    if(!r.ok){
      toast(d.error||'Erreur',false);
      return;
    }
    await loadProfiles();
    document.getElementById('c-profile').value=profileName;

    const id=parseInt(document.getElementById('c-id').value);
    if(!Number.isFinite(id) || id<=0){
      toast('Profil duplique',true);
      closeSheet();
      return;
    }
    const ra=await apiFetch('/api/profiles/apply',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({id,profileName})},5000);
    const da=await ra.json();
    toast(ra.ok?'Profil duplique et applique':(da.error||'Profil sauvegarde, application impossible'),ra.ok);
    if(ra.ok){
      closeSheet();
      scheduleLoadNodes(4200,true);
    }
  }catch(e){toast('Erreur reseau',false);}
}

async function applyProfile(){
  const profileName=document.getElementById('c-profile').value;
  const id=parseInt(document.getElementById('c-id').value);
  if(!profileName){toast('Choisissez un profil',false);return;}
  if(!Number.isFinite(id) || id<=0){toast('Choisissez un node pour appliquer ce profil',false);return;}
  try{
    pauseNodeErrors(6500);
    const r=await apiFetch('/api/profiles/apply',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({id,profileName})},5000);
    const d=await r.json();
    toast(r.ok?'Profil applique':(d.error||'Erreur'),r.ok);
    if(r.ok){
      closeSheet();
      scheduleLoadNodes(4200,true);
    }
  }catch(e){toast('Erreur reseau',false);}
}

async function deleteProfile(){
  const profileName=document.getElementById('c-profile').value;
  if(!profileName) return;
  if(!confirm('Supprimer le profil '+profileName+' ?')) return;
  try{
    const r=await apiFetch('/api/profiles?name='+encodeURIComponent(profileName),{method:'DELETE'});
    const d=await r.json();
    toast(r.ok?'Profil supprime':(d.error||'Erreur'),r.ok);
    if(r.ok){await loadProfiles();}
  }catch(e){toast('Erreur reseau',false);}
}

function deleteProfileByIndex(index){
  const p=profiles[index];if(!p)return;
  document.getElementById('c-profile').value=p.name||'';
  deleteProfile();
}

async function deleteNode(id){
  const n=nodes.find(x=>x.id===id);
  if(!n)return;
  const nodeTitle=n.label||n.type||n.name||('Node '+id);
  if(!confirm('Supprimer '+nodeTitle+' de la liste ?'))return;
  try{
    const r=await apiFetch('/api/nodes/delete?id='+encodeURIComponent(id),{method:'POST'});
    const d=await parseJsonResponse(r);
    toast(r.ok?(d.message||'Appareil supprime'):(d.error||'Erreur suppression'),r.ok);
    if(r.ok)loadNodes(true);
  }catch(e){toast(String(e&&e.message?e.message:'Erreur reseau'),false);}
}

let _tt;
function toast(msg,ok){
  const el=document.getElementById('toast');
  el.textContent=msg;el.className='toast '+(ok?'ok':'err');
  clearTimeout(_tt);
  requestAnimationFrame(()=>el.classList.add('show'));
  _tt=setTimeout(()=>el.classList.remove('show'),3500);
}

document.getElementById('ov').addEventListener('click',e=>{if(e.target===document.getElementById('ov'))closeSheet();});
loadNodes(true);
loadSettings();
loadProfiles();
setInterval(()=>loadNodes(false),4000);
</script>
</body>
</html>
)rawliteral";

// =====================================================
// CRC
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

bool validateMsg(const uint8_t* data, int len, msg_t& m) {
  if (len != sizeof(msg_t)) {
    hubLogPrintf("# bad_len=%d\n", len);
    return false;
  }
  memcpy(&m, data, sizeof(m));
  if (m.magic != PROTO_MAGIC) {
    hubLogPrintf("# bad_magic=0x%02X\n", m.magic);
    return false;
  }
  if (m.version != PROTO_VERSION) {
    hubLogPrintf("# bad_version=%u\n", m.version);
    return false;
  }
  uint8_t recv = m.crc;
  m.crc = 0;
  uint8_t calc = crc8((const uint8_t*)&m, sizeof(m));
  if (calc != recv) {
    hubLogPrintf("# bad_crc calc=%02X recv=%02X\n", calc, recv);
    return false;
  }
  m.payload[199] = 0;
  return true;
}

// =====================================================
// NVS
// =====================================================
void saveNodes() {
  prefs.begin("bdphub", false);
  prefs.putUInt("count", nodeCount);
  for (int i = 0; i < nodeCount; i++) {
    char key[8];
    snprintf(key, sizeof(key), "n%d", i);
    stored_node_t sn = {};
    sn.id = nodes[i].id;
    memcpy(sn.mac, nodes[i].mac, 6);
    strncpy(sn.nodeName, nodes[i].nodeName, 31);
    strncpy(sn.typeName, nodes[i].typeName, 31);
    prefs.putBytes(key, &sn, sizeof(sn));
    nodes[i].persisted = true;

    char mkey[8];
    snprintf(mkey, sizeof(mkey), "m%d", i);
    stored_node_meta_t sm = {};
    strncpy(sm.balanceId, nodes[i].balanceId, sizeof(sm.balanceId) - 1);
    strncpy(sm.displayLabel, nodes[i].displayLabel, sizeof(sm.displayLabel) - 1);
    prefs.putBytes(mkey, &sm, sizeof(sm));

    if (nodes[i].configKnown) {
      char ckey[8];
      snprintf(ckey, sizeof(ckey), "c%d", i);
      stored_config_t sc = {};
      sc.brand = nodes[i].brand;
      sc.baud  = nodes[i].baud;
      sc.parity    = nodes[i].parity;
      sc.dataBits  = nodes[i].dataBits;
      sc.stopBits  = nodes[i].stopBits;
      sc.rxPin     = nodes[i].rxPin;
      sc.txPin     = nodes[i].txPin;
      sc.swapRxTx  = nodes[i].swapRxTx;
      strncpy(sc.pollCmd, nodes[i].pollCmd, 15);
      sc.lineTimeout = nodes[i].lineTimeout;
      strncpy(sc.zeroCmd, nodes[i].zeroCmd, 15);
      sc.capacity = nodes[i].capacity;
      sc.resolution = nodes[i].resolution;
      prefs.putBytes(ckey, &sc, sizeof(sc));
    }
  }
  prefs.end();
}

void loadNodes() {
  prefs.begin("bdphub", true);
  int count = prefs.getUInt("count", 0);
  if (count > MAX_NODES) count = MAX_NODES;
  for (int i = 0; i < count; i++) {
    char key[8];
    snprintf(key, sizeof(key), "n%d", i);
    stored_node_t sn;
    if (prefs.getBytes(key, &sn, sizeof(sn)) == sizeof(sn)) {
      nodes[i].id = sn.id;
      memcpy(nodes[i].mac, sn.mac, 6);
      strncpy(nodes[i].nodeName, sn.nodeName, 31);
      nodes[i].nodeName[31] = 0;
      strncpy(nodes[i].typeName, sn.typeName, 31);
      nodes[i].typeName[31] = 0;
      nodes[i].lastSeen = 0;
      nodes[i].active = false;
      nodes[i].persisted = true;
      nodes[i].balanceId[0] = 0;
      nodes[i].displayLabel[0] = 0;

      char mkey[8];
      snprintf(mkey, sizeof(mkey), "m%d", i);
      stored_node_meta_t sm;
      if (prefs.getBytes(mkey, &sm, sizeof(sm)) == sizeof(sm)) {
        strncpy(nodes[i].balanceId, sm.balanceId, sizeof(nodes[i].balanceId) - 1);
        nodes[i].balanceId[sizeof(nodes[i].balanceId) - 1] = 0;
        strncpy(nodes[i].displayLabel, sm.displayLabel, sizeof(nodes[i].displayLabel) - 1);
        nodes[i].displayLabel[sizeof(nodes[i].displayLabel) - 1] = 0;
      }

      esp_now_peer_info_t peer = {};
      memcpy(peer.peer_addr, sn.mac, 6);
      peer.channel = 0;
      peer.encrypt = false;
      esp_now_add_peer(&peer);

      // Charge config si disponible
      char ckey[8];
      snprintf(ckey, sizeof(ckey), "c%d", i);
      stored_config_t sc;
      if (prefs.getBytes(ckey, &sc, sizeof(sc)) == sizeof(sc)) {
        nodes[i].brand      = sc.brand;
        nodes[i].baud       = sc.baud;
        nodes[i].parity     = sc.parity;
        nodes[i].dataBits   = sc.dataBits;
        nodes[i].stopBits   = sc.stopBits;
        nodes[i].rxPin      = sc.rxPin;
        nodes[i].txPin      = sc.txPin;
        nodes[i].swapRxTx   = sc.swapRxTx;
        strncpy(nodes[i].pollCmd, sc.pollCmd, 15);
        nodes[i].pollCmd[15] = 0;
        nodes[i].lineTimeout = sc.lineTimeout;
        strncpy(nodes[i].zeroCmd, sc.zeroCmd, 15);
        nodes[i].zeroCmd[15] = 0;
        if (!nodes[i].zeroCmd[0]) strlcpy(nodes[i].zeroCmd, "Z", sizeof(nodes[i].zeroCmd));
        nodes[i].capacity = sc.capacity > 0 ? sc.capacity : 5000.0f;
        nodes[i].resolution = sc.resolution > 0 ? sc.resolution : 0.01f;
        nodes[i].configKnown = true;
      }
    }
  }
  nodeCount = count;
  prefs.end();
}

void clearNodes() {
  prefs.begin("bdphub", false);
  prefs.clear();
  prefs.end();
  nodeCount = 0;
  selectedNode = -1;
  memset(tareOffset, 0, sizeof(tareOffset));
  memset(pendingTareAdjust, 0, sizeof(pendingTareAdjust));
  memset(pendingTare, 0, sizeof(pendingTare));
}

void saveProfiles() {
  prefs.begin("bdphub", false);
  prefs.putUInt("pcount", profileCount);
  for (int i = 0; i < profileCount; i++) {
    char key[8];
    snprintf(key, sizeof(key), "p%d", i);
    prefs.putBytes(key, &profiles[i], sizeof(profiles[i]));
  }
  for (int i = profileCount; i < MAX_PROFILES; i++) {
    char key[8];
    snprintf(key, sizeof(key), "p%d", i);
    prefs.remove(key);
  }
  prefs.end();
}

void loadProfiles() {
  prefs.begin("bdphub", true);
  int count = prefs.getUInt("pcount", 0);
  if (count > MAX_PROFILES) count = MAX_PROFILES;
  memset(profiles, 0, sizeof(profiles));
  for (int i = 0; i < count; i++) {
    char key[8];
    snprintf(key, sizeof(key), "p%d", i);
    if (prefs.getBytes(key, &profiles[i], sizeof(profiles[i])) != sizeof(profiles[i])) {
      memset(&profiles[i], 0, sizeof(profiles[i]));
    }
  }
  profileCount = count;
  prefs.end();
}

int findProfileIndexByName(const char* name) {
  if (!name || !*name) return -1;
  for (int i = 0; i < profileCount; i++) {
    if (strcmp(profiles[i].profileName, name) == 0) return i;
  }
  return -1;
}

// =====================================================
// Registre nodes
// =====================================================
int findNodeByMac(const uint8_t* mac) {
  for (int i = 0; i < nodeCount; i++) {
    if (memcmp(nodes[i].mac, mac, 6) == 0) return i;
  }
  return -1;
}

int findNodeBySelfId(uint8_t id) {
  for (int i = 0; i < nodeCount; i++) {
    if (nodes[i].id == id) return i;
  }
  return -1;
}

int findActiveNodeById(uint8_t id) {
  int idx = findNodeBySelfId(id);
  if (idx >= 0 && nodes[idx].active) return idx;
  return -1;
}

uint8_t nextFreeNodeId(uint8_t preferred) {
  if (preferred > 0 && findNodeBySelfId(preferred) < 0) return preferred;
  for (uint16_t id = 1; id < 255; id++) {
    if (findNodeBySelfId((uint8_t)id) < 0) return (uint8_t)id;
  }
  return 0;
}

int upsertNode(const uint8_t* mac, const char* name, uint8_t id) {
  int idx = findNodeByMac(mac);
  if (idx >= 0) {
    strncpy(nodes[idx].nodeName, name, 31);
    nodes[idx].nodeName[31] = 0;
    nodes[idx].lastSeen = millis();
    nodes[idx].active = true;
    return idx;
  }
  if (nodeCount >= MAX_NODES) return -1;
  uint8_t assignedId = nextFreeNodeId(id);
  if (assignedId == 0) return -1;
  idx = nodeCount++;
  nodes[idx].id = assignedId;
  memcpy(nodes[idx].mac, mac, 6);
  strncpy(nodes[idx].nodeName, name, 31);
  nodes[idx].nodeName[31] = 0;
  nodes[idx].lastSeen = millis();
  nodes[idx].active = true;
  nodes[idx].persisted   = false;
  nodes[idx].configKnown = false;
  nodes[idx].balReady    = false;
  nodes[idx].brand = BRAND_AD;
  nodes[idx].baud = 2400;
  nodes[idx].parity = 1;
  nodes[idx].dataBits = 7;
  nodes[idx].stopBits = 1;
  nodes[idx].rxPin = 5;
  nodes[idx].txPin = 6;
  nodes[idx].swapRxTx = false;
  strlcpy(nodes[idx].pollCmd, "Q", sizeof(nodes[idx].pollCmd));
  nodes[idx].lineTimeout = 300;
  nodes[idx].balanceId[0] = 0;
  nodes[idx].displayLabel[0] = 0;
  strlcpy(nodes[idx].zeroCmd, "Z", sizeof(nodes[idx].zeroCmd));
  nodes[idx].capacity = 5000.0f;
  nodes[idx].resolution = 0.01f;
  tareOffset[idx] = 0;
  pendingTareAdjust[idx] = 0;
  pendingTare[idx] = false;

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, mac, 6);
  peer.channel = 0;
  peer.encrypt = false;
  esp_now_add_peer(&peer);

  return idx;
}

void clearPending();
void startScan();

bool deleteNodeById(uint8_t id) {
  int idx = findNodeBySelfId(id);
  if (idx < 0) return false;

  esp_now_del_peer(nodes[idx].mac);
  if (selectedNode == idx) selectedNode = -1;
  else if (selectedNode > idx) selectedNode--;
  if (pending.kind != PEND_NONE && pending.nodeId == id) clearPending();

  for (int i = idx; i < nodeCount - 1; i++) {
    nodes[i] = nodes[i + 1];
    tareOffset[i] = tareOffset[i + 1];
    pendingTareAdjust[i] = pendingTareAdjust[i + 1];
    pendingTare[i] = pendingTare[i + 1];
  }

  if (nodeCount > 0) {
    nodeCount--;
    memset(&nodes[nodeCount], 0, sizeof(nodes[nodeCount]));
    tareOffset[nodeCount] = 0;
    pendingTareAdjust[nodeCount] = 0;
    pendingTare[nodeCount] = false;
  }

  listScroll = 0;
  displayDirty = true;
  saveNodes();
  return true;
}

// =====================================================
// Envoi
// =====================================================
void sendMsg(const uint8_t* mac, uint8_t type, const char* payload) {
  msg_t m = {};
  m.magic = PROTO_MAGIC;
  m.version = PROTO_VERSION;
  m.type = type;
  m.nodeId = 0;
  m.seq = ++txSeq;
  if (payload) {
    strncpy(m.payload, payload, 199);
    m.payload[199] = 0;
  }
  m.crc = 0;
  m.crc = computeMsgCrc(&m);
  esp_now_send(mac, (uint8_t*)&m, sizeof(m));
}

void clearPending() {
  pending.kind = PEND_NONE;
  pendingToPc = false;
}

void startPending(pending_kind_t kind, int idx, bool forwardToPc = true) {
  pending.kind = kind;
  pending.nodeId = nodes[idx].id;
  memcpy(pending.mac, nodes[idx].mac, 6);
  pending.sentAt = millis();
  pendingToPc = forwardToPc;
}

void sendBalanceCmdToNode(int idx, const char* cmd) {
  sendMsg(nodes[idx].mac, MSG_COMMAND, cmd);
  startPending(PEND_BALANCE, idx, true);
  hubLogPrintf("@%02u TX %s\n", nodes[idx].id, cmd);
}

void sendLocalBalanceCmdToNode(int idx, const char* cmd) {
  if (idx < 0 || idx >= nodeCount || !nodes[idx].active || !cmd || !*cmd) return;
  sendMsg(nodes[idx].mac, MSG_COMMAND, cmd);
}

static bool sendBalanceCmdAndWait(int idx, const char* cmd, char* out, size_t outSize, unsigned long timeoutMs) {
  if (out && outSize) out[0] = 0;
  if (idx < 0 || idx >= nodeCount || !nodes[idx].active || !cmd || !*cmd) return false;
  apiBalanceReplyPending = true;
  apiBalanceReplyReady = false;
  apiBalanceReplyNodeId = nodes[idx].id;
  apiBalanceReply[0] = 0;
  sendLocalBalanceCmdToNode(idx, cmd);
  unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    delay(10);
    if (apiBalanceReplyReady) {
      if (out && outSize) {
        strlcpy(out, apiBalanceReply, outSize);
      }
      apiBalanceReplyPending = false;
      apiBalanceReplyReady = false;
      apiBalanceReply[0] = 0;
      return true;
    }
  }
  apiBalanceReplyPending = false;
  apiBalanceReplyReady = false;
  apiBalanceReply[0] = 0;
  return false;
}

void sendPingToNode(int idx) {
  sendMsg(nodes[idx].mac, MSG_PING, "");
  startPending(PEND_PING, idx, true);
}

void sendInfoReqToNode(int idx) {
  sendMsg(nodes[idx].mac, MSG_NODE_INFO_REQ, "");
  startPending(PEND_INFO, idx, true);
}

void sendConfigReqToNode(int idx) {
  sendMsg(nodes[idx].mac, MSG_CONFIG_REQ, "");
}

void sendConfigSetToNode(int idx) {
  normalizeNodeIdentity(idx);
  char buf[256];
  StaticJsonDocument<384> doc;
  uint8_t b = nodes[idx].brand < 3 ? nodes[idx].brand : 0;
  doc["n"] = nodes[idx].nodeName;
  doc["tn"] = nodes[idx].typeName;
  doc["br"] = b;
  doc["bd"] = nodes[idx].baud;
  doc["pa"] = nodes[idx].parity;
  doc["db"] = nodes[idx].dataBits;
  doc["sb"] = nodes[idx].stopBits;
  doc["rx"] = nodes[idx].rxPin;
  doc["tx"] = nodes[idx].txPin;
  doc["sw"] = nodes[idx].swapRxTx ? 1 : 0;
  doc["lb"] = nodes[idx].displayLabel;
  doc["cmd"] = nodes[idx].pollCmd;
  doc["to"] = nodes[idx].lineTimeout;
  doc["zc"] = nodes[idx].zeroCmd[0] ? nodes[idx].zeroCmd : "Z";
  doc["cp"] = nodes[idx].capacity > 0 ? nodes[idx].capacity : 5000.0f;
  doc["rs"] = nodes[idx].resolution > 0 ? nodes[idx].resolution : 0.01f;
  doc["pr"] = protocolForBrand(b);
  doc["en"] = lineEndingForBrand(b);
  doc["cf"] = 100;
  doc["as"] = 0;
  serializeJson(doc, buf, sizeof(buf));
  sendMsg(nodes[idx].mac, MSG_CONFIG_SET, buf);
}

// =====================================================
// Helpers valeur
// =====================================================
bool isValueStable(const char* value, uint8_t brand) {
  if (!value || strncmp(value, "ERROR", 5) == 0) return false;
  if (brand == BRAND_METTLER) {
    // "S S  ..." = stable, "S D  ..." = instable
    return (strlen(value) > 2 && value[0] == 'S' && value[1] == ' ' && value[2] == 'S');
  }
  return (strncmp(value, "US", 2) != 0);
}

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

void pushHistory(const char* value) {
  for (int i = HISTORY_SIZE - 1; i > 0; i--) {
    strncpy(history[i], history[i - 1], 63);
    history[i][63] = 0;
    historyTime[i] = historyTime[i - 1];
  }
  strncpy(history[0], value, 63);
  history[0][63] = 0;
  historyTime[0] = millis();
  if (historyCount < HISTORY_SIZE) historyCount++;
}

bool extractQuotedField(const char* payload, const char* key, char* out, size_t outSize) {
  if (!payload || !key || !out || outSize == 0) return false;
  out[0] = 0;
  char needle[20];
  snprintf(needle, sizeof(needle), "%s=\"", key);
  const char* p = strstr(payload, needle);
  if (!p) return false;
  p += strlen(needle);
  size_t o = 0;
  while (*p && *p != '"' && o < outSize - 1) out[o++] = *p++;
  out[o] = 0;
  return o > 0;
}

bool isKnownBalanceId(const char* id) {
  return id && id[0] && strcmp(id, "?") != 0;
}

int clientBrandForBalanceId(const char* id) {
  if (!id) return -1;
  if (strcmp(id, "02") == 0 || strcmp(id, "03") == 0 || strcmp(id, "05") == 0) return BRAND_AD;
  if (strcmp(id, "04") == 0 || strcmp(id, "06") == 0) return BRAND_METTLER;
  return -1;
}

bool extractClientBalanceId(const char* raw, char* out, size_t outSize) {
  if (!raw || !out || outSize < 3) return false;
  const char* search = strstr(raw, "CDO");
  if (!search) search = raw;
  for (size_t i = 0; search[i] && search[i + 1]; i++) {
    if (search[i] < '0' || search[i] > '9' || search[i + 1] < '0' || search[i + 1] > '9') continue;
    char candidate[3] = {search[i], search[i + 1], 0};
    if (clientBrandForBalanceId(candidate) >= 0) {
      strlcpy(out, candidate, outSize);
      return true;
    }
  }
  return false;
}

bool sanitizeMettlerWriteId(const char* raw, char* out, size_t outSize) {
  if (!raw || !out || outSize < 2) return false;
  size_t n = 0;
  for (size_t i = 0; raw[i] && n < 20 && n + 1 < outSize; i++) {
    char c = raw[i];
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    bool ok = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ';
    if (!ok) continue;
    out[n++] = c;
  }
  while (n > 0 && out[n - 1] == ' ') n--;
  out[n] = 0;
  return n > 0;
}

static bool extractQuotedValue(const char* raw, char* out, size_t outSize) {
  if (!raw || !out || outSize < 2) return false;
  const char* first = strchr(raw, '"');
  if (!first) return false;
  first++;
  const char* second = strchr(first, '"');
  if (!second || second <= first) return false;
  size_t len = (size_t)(second - first);
  if (len >= outSize) len = outSize - 1;
  memcpy(out, first, len);
  out[len] = 0;
  return len > 0;
}

static bool parseBalanceIdFromReply(uint8_t brand, const char* raw, char* out, size_t outSize) {
  if (!raw || !out || outSize < 3) return false;
  out[0] = 0;
  if (brand == BRAND_METTLER) {
    char quoted[32];
    if (extractQuotedValue(raw, quoted, sizeof(quoted)) &&
        sanitizeMettlerWriteId(quoted, out, outSize)) return true;
  }
  return extractClientBalanceId(raw, out, outSize);
}

static uint8_t protocolForBrand(uint8_t brand) {
  switch (brand) {
    case BRAND_AD: return PROTOCOL_AD;
    case BRAND_METTLER: return PROTOCOL_SICS;
    case BRAND_SARTORIUS: return PROTOCOL_SARTORIUS_SBI;
    default: return PROTOCOL_UNKNOWN;
  }
}

static uint8_t lineEndingForBrand(uint8_t) {
  return LINE_ENDING_CRLF;
}

static void normalizeNodeIdentity(int idx) {
  if (idx < 0 || idx >= nodeCount) return;
  char parsedId[20];
  if (extractClientBalanceId(nodes[idx].displayLabel, parsedId, sizeof(parsedId)) ||
      extractClientBalanceId(nodes[idx].balanceId, parsedId, sizeof(parsedId))) {
    strlcpy(nodes[idx].balanceId, parsedId, sizeof(nodes[idx].balanceId));
    setNodeDisplayLabel(idx);
  }
}

void setNodeDisplayLabel(int idx) {
  if (idx < 0 || idx >= nodeCount) return;
  if (isKnownBalanceId(nodes[idx].balanceId)) {
    snprintf(nodes[idx].displayLabel, sizeof(nodes[idx].displayLabel), "CDO %s", nodes[idx].balanceId);
  } else {
    nodes[idx].displayLabel[0] = 0;
  }
}

int findProfileIndexForBalanceId(const char* balanceId) {
  if (!isKnownBalanceId(balanceId)) return -1;
  for (int i = 0; i < profileCount; i++) {
    char profileId[20];
    if (extractClientBalanceId(profiles[i].label, profileId, sizeof(profileId)) &&
        strcmp(profileId, balanceId) == 0) {
      return i;
    }
  }
  return -1;
}

bool nodeMatchesProfile(int idx, const stored_profile_t& p) {
  if (idx < 0 || idx >= nodeCount) return false;
  const node_entry_t& n = nodes[idx];
  return strcmp(n.displayLabel, p.label) == 0 &&
         strcmp(n.typeName, p.typeName) == 0 &&
         n.brand == p.brand &&
         n.baud == p.baud &&
         n.parity == p.parity &&
         n.dataBits == p.dataBits &&
         n.stopBits == p.stopBits &&
         n.rxPin == p.rxPin &&
         n.txPin == p.txPin &&
         n.swapRxTx == p.swapRxTx &&
         strcmp(n.pollCmd, p.pollCmd) == 0 &&
         n.lineTimeout == p.lineTimeout &&
         strcmp(n.zeroCmd, p.zeroCmd) == 0 &&
         fabsf(n.capacity - p.capacity) < 0.0001f &&
         fabsf(n.resolution - p.resolution) < 0.0001f;
}

bool applyProfileToNode(int nodeIdx, int profileIdx) {
  if (nodeIdx < 0 || nodeIdx >= nodeCount || profileIdx < 0 || profileIdx >= profileCount) return false;
  stored_profile_t& p = profiles[profileIdx];
  strlcpy(nodes[nodeIdx].displayLabel, p.label, sizeof(nodes[nodeIdx].displayLabel));
  if (extractClientBalanceId(p.label, nodes[nodeIdx].balanceId, sizeof(nodes[nodeIdx].balanceId))) {
    setNodeDisplayLabel(nodeIdx);
  }
  strlcpy(nodes[nodeIdx].typeName, p.typeName, sizeof(nodes[nodeIdx].typeName));
  nodes[nodeIdx].brand = p.brand;
  nodes[nodeIdx].baud = p.baud;
  nodes[nodeIdx].parity = p.parity;
  nodes[nodeIdx].dataBits = p.dataBits;
  nodes[nodeIdx].stopBits = p.stopBits;
  nodes[nodeIdx].rxPin = p.rxPin;
  nodes[nodeIdx].txPin = p.txPin;
  nodes[nodeIdx].swapRxTx = p.swapRxTx;
  strlcpy(nodes[nodeIdx].pollCmd, p.pollCmd, sizeof(nodes[nodeIdx].pollCmd));
  nodes[nodeIdx].lineTimeout = p.lineTimeout;
  strlcpy(nodes[nodeIdx].zeroCmd, p.zeroCmd, sizeof(nodes[nodeIdx].zeroCmd));
  nodes[nodeIdx].capacity = p.capacity;
  nodes[nodeIdx].resolution = p.resolution;
  nodes[nodeIdx].configKnown = true;
  sendConfigSetToNode(nodeIdx);
  saveNodes();
  return true;
}

bool autoApplyProfileForNode(int idx) {
  if (idx < 0 || idx >= nodeCount || !isKnownBalanceId(nodes[idx].balanceId)) return false;
  int profileIdx = findProfileIndexForBalanceId(nodes[idx].balanceId);
  if (profileIdx < 0) return false;
  if (nodeMatchesProfile(idx, profiles[profileIdx])) return false;
  hubLogPrintf("@%02u AUTO_PROFILE %s\n", nodes[idx].id, profiles[profileIdx].profileName);
  return applyProfileToNode(idx, profileIdx);
}

void applyClientBrandMapping(int idx) {
  if (idx < 0 || idx >= nodeCount) return;
  int mappedBrand = clientBrandForBalanceId(nodes[idx].balanceId);
  if (mappedBrand < 0 || nodes[idx].brand == mappedBrand) return;
  nodes[idx].brand = (uint8_t)mappedBrand;
  nodes[idx].configKnown = true;
}

const char* nodeDisplayName(int idx) {
  if (idx >= 0 && idx < nodeCount && nodes[idx].displayLabel[0]) return nodes[idx].displayLabel;
  if (idx >= 0 && idx < nodeCount) return nodes[idx].nodeName;
  return "";
}

const char* nodeBalanceType(int idx) {
  if (idx >= 0 && idx < nodeCount && nodes[idx].typeName[0]) return nodes[idx].typeName;
  return "";
}

// =====================================================
// Paramètres hub (NVS)
// =====================================================
void loadSettings() {
  prefs.begin("bdphub", true);
  soundEnabled = prefs.getBool("snd_en", true);
  serialReplyBeepEnabled = prefs.getBool("sr_beep", true);
  serialReplyHapticEnabled = prefs.getBool("sr_haptic", true);
  prefs.end();
}

void saveSettings() {
  prefs.begin("bdphub", false);
  prefs.putBool("snd_en", soundEnabled);
  prefs.putBool("sr_beep", serialReplyBeepEnabled);
  prefs.putBool("sr_haptic", serialReplyHapticEnabled);
  prefs.end();
}

static void appendJsonEscaped(String& out, const char* value) {
  out += '"';
  if (value) {
    for (size_t i = 0; value[i]; i++) {
      uint8_t c = (uint8_t)value[i];
      if (c == '"' || c == '\\') {
        out += '\\';
        out += (char)c;
      } else if (c == '\n') {
        out += "\\n";
      } else if (c == '\r') {
        out += "\\r";
      } else if (c == '\t') {
        out += "\\t";
      } else if (c < 0x20) {
        char hex[7];
        snprintf(hex, sizeof(hex), "\\u%04X", c);
        out += hex;
      } else {
        out += (char)c;
      }
    }
  }
  out += '"';
}

// =====================================================
// Handlers web
// =====================================================
void handleRoot() {
  webServer.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  webServer.sendHeader("Pragma", "no-cache");
  webServer.sendHeader("Expires", "0");
  webServer.send_P(200, "text/html", HTML_PAGE);
}

void handleFavicon() {
  webServer.send(204);
}

void handleApiNodes() {
  webServer.sendHeader("Access-Control-Allow-Origin", "*");
  String body;
  body.reserve((nodeCount * 768) + 16);
  body += '[';
  for (int i = 0; i < nodeCount; i++) {
    if (i > 0) body += ',';
    char mac[18];
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
      nodes[i].mac[0], nodes[i].mac[1], nodes[i].mac[2],
      nodes[i].mac[3], nodes[i].mac[4], nodes[i].mac[5]);
    unsigned long ago = nodes[i].lastSeen ? (millis() - nodes[i].lastSeen) / 1000UL : 0;
    body += "{\"id\":";
    body += String(nodes[i].id);
    body += ",\"mac\":";
    appendJsonEscaped(body, mac);
    body += ",\"name\":";
    appendJsonEscaped(body, nodes[i].nodeName);
    body += ",\"type\":";
    appendJsonEscaped(body, nodes[i].typeName);
    body += ",\"label\":";
    appendJsonEscaped(body, nodes[i].displayLabel);
    body += ",\"balanceId\":";
    appendJsonEscaped(body, nodes[i].balanceId);
    body += ",\"online\":";
    body += nodes[i].active ? "true" : "false";
    body += ",\"ago\":";
    body += String(ago);
    body += ",\"configKnown\":";
    body += nodes[i].configKnown ? "true" : "false";
    if (nodes[i].configKnown) {
      uint8_t b = nodes[i].brand < 3 ? nodes[i].brand : 0;
    body += ",\"config\":{\"brand\":";
    body += String(nodes[i].brand);
    body += ",\"brandName\":";
    appendJsonEscaped(body, BRAND_NAMES[b]);
    body += ",\"firmware\":";
    appendJsonEscaped(body, nodes[i].firmwareVariant);
    body += ",\"baud\":";
    body += String(nodes[i].baud);
      body += ",\"parity\":";
      body += String(nodes[i].parity);
      body += ",\"dataBits\":";
      body += String(nodes[i].dataBits);
      body += ",\"stopBits\":";
      body += String(nodes[i].stopBits);
      body += ",\"rxPin\":";
      body += String(nodes[i].rxPin);
      body += ",\"txPin\":";
      body += String(nodes[i].txPin);
      body += ",\"swapRxTx\":";
      body += nodes[i].swapRxTx ? "true" : "false";
      body += ",\"pollCmd\":";
      appendJsonEscaped(body, nodes[i].pollCmd);
      body += ",\"lineTimeout\":";
      body += String(nodes[i].lineTimeout);
      body += ",\"zeroCmd\":";
      appendJsonEscaped(body, nodes[i].zeroCmd[0] ? nodes[i].zeroCmd : "Z");
      body += ",\"capacity\":";
      body += String(nodes[i].capacity > 0 ? nodes[i].capacity : 5000.0f, 4);
      body += ",\"resolution\":";
      body += String(nodes[i].resolution > 0 ? nodes[i].resolution : 0.01f, 4);
      body += ",\"protocol\":";
      body += String(protocolForBrand(nodes[i].brand));
      body += '}';
    }
    body += '}';
  }
  body += ']';
  webServer.send(200, "application/json", body);
}

void handleApiConfigSet() {
  webServer.sendHeader("Access-Control-Allow-Origin", "*");
  if (!webServer.hasArg("plain")) {
    webServer.send(400, "application/json", "{\"error\":\"no body\"}");
    return;
  }
  StaticJsonDocument<768> doc;
  auto err = deserializeJson(doc, webServer.arg("plain"));
  if (err) {
    webServer.send(400, "application/json", "{\"error\":\"json_parse\"}");
    return;
  }
  int id = doc["id"] | -1;
  if (id < 1 || id > 254) {
    webServer.send(400, "application/json", "{\"error\":\"invalid_id\"}");
    return;
  }
  int idx = findNodeBySelfId((uint8_t)id);
  if (idx < 0) {
    webServer.send(404, "application/json", "{\"error\":\"node_not_found\"}");
    return;
  }
  if (doc.containsKey("label")) {
    char rawLabel[32];
    strlcpy(rawLabel, doc["label"] | "", sizeof(rawLabel));
    if (extractClientBalanceId(rawLabel, nodes[idx].balanceId, sizeof(nodes[idx].balanceId))) {
      setNodeDisplayLabel(idx);
      applyClientBrandMapping(idx);
    } else if (rawLabel[0]) {
      strlcpy(nodes[idx].displayLabel, rawLabel, sizeof(nodes[idx].displayLabel));
    } else {
      nodes[idx].displayLabel[0] = 0;
      nodes[idx].balanceId[0] = 0;
    }
  }
  if (doc.containsKey("balanceId")) {
    char rawBalanceId[20];
    strlcpy(rawBalanceId, doc["balanceId"] | "", sizeof(rawBalanceId));
    if (extractClientBalanceId(rawBalanceId, nodes[idx].balanceId, sizeof(nodes[idx].balanceId))) {
      setNodeDisplayLabel(idx);
    } else if (rawBalanceId[0]) {
      strlcpy(nodes[idx].balanceId, rawBalanceId, sizeof(nodes[idx].balanceId));
      setNodeDisplayLabel(idx);
    }
  }
  if (doc.containsKey("nodeName"))    strlcpy(nodes[idx].nodeName, doc["nodeName"], sizeof(nodes[idx].nodeName));
  if (doc.containsKey("name"))        strlcpy(nodes[idx].typeName, doc["name"], sizeof(nodes[idx].typeName));
  if (doc.containsKey("brand"))       nodes[idx].brand     = doc["brand"];
  if (doc.containsKey("baud"))        nodes[idx].baud      = doc["baud"];
  if (doc.containsKey("parity"))      nodes[idx].parity    = doc["parity"];
  if (doc.containsKey("dataBits"))    nodes[idx].dataBits  = doc["dataBits"];
  if (doc.containsKey("stopBits"))    nodes[idx].stopBits  = doc["stopBits"];
  if (doc.containsKey("rxPin"))       nodes[idx].rxPin     = doc["rxPin"];
  if (doc.containsKey("txPin"))       nodes[idx].txPin     = doc["txPin"];
  if (doc.containsKey("swapRxTx"))    nodes[idx].swapRxTx  = (bool)doc["swapRxTx"];
  if (doc.containsKey("pollCmd"))     strlcpy(nodes[idx].pollCmd, doc["pollCmd"], sizeof(nodes[idx].pollCmd));
  if (doc.containsKey("lineTimeout")) nodes[idx].lineTimeout = doc["lineTimeout"];
  if (doc.containsKey("zeroCmd"))     strlcpy(nodes[idx].zeroCmd, doc["zeroCmd"], sizeof(nodes[idx].zeroCmd));
  if (doc.containsKey("capacity"))    nodes[idx].capacity = doc["capacity"].as<float>();
  if (doc.containsKey("resolution"))  nodes[idx].resolution = doc["resolution"].as<float>();
  if (!nodes[idx].zeroCmd[0])         strlcpy(nodes[idx].zeroCmd, "Z", sizeof(nodes[idx].zeroCmd));
  if (nodes[idx].capacity <= 0)       nodes[idx].capacity = 5000.0f;
  if (nodes[idx].resolution <= 0)     nodes[idx].resolution = 0.01f;
  normalizeNodeIdentity(idx);
  nodes[idx].configKnown = true;

  saveNodes();
  pendingConfigPushIdx = idx;

  hubLogPrintf("@%02u CONFIG_SET brand=%d baud=%u\n",
    nodes[idx].id, nodes[idx].brand, nodes[idx].baud);
  webServer.send(200, "application/json",
    "{\"status\":\"ok\",\"message\":\"Config envoyee, node redemarrage...\"}");
}

void handleApiBalanceIdSet() {
  webServer.sendHeader("Access-Control-Allow-Origin", "*");
  if (!webServer.hasArg("plain")) {
    webServer.send(400, "application/json", "{\"error\":\"no body\"}");
    return;
  }
  StaticJsonDocument<192> doc;
  if (deserializeJson(doc, webServer.arg("plain")) != DeserializationError::Ok) {
    webServer.send(400, "application/json", "{\"error\":\"json_parse\"}");
    return;
  }
  int id = doc["id"] | -1;
  const char* requested = doc["balanceId"] | "";
  if (id < 1 || id > 254) {
    webServer.send(400, "application/json", "{\"error\":\"invalid_id\"}");
    return;
  }
  int idx = findNodeBySelfId((uint8_t)id);
  if (idx < 0) {
    webServer.send(404, "application/json", "{\"error\":\"node_not_found\"}");
    return;
  }
  if (!nodes[idx].active) {
    webServer.send(400, "application/json", "{\"error\":\"node_offline\"}");
    return;
  }
  char balanceId[21] = "";
  if (!sanitizeMettlerWriteId(requested, balanceId, sizeof(balanceId))) {
    webServer.send(400, "application/json", "{\"error\":\"invalid_balance_id\"}");
    return;
  }
  if (nodes[idx].brand != BRAND_METTLER) {
    webServer.send(400, "application/json", "{\"error\":\"mettler_only\"}");
    return;
  }

  char cmd[64];
  snprintf(cmd, sizeof(cmd), "I10 \"%s\"", balanceId);
  sendLocalBalanceCmdToNode(idx, cmd);
  char clientId[21] = "";
  if (extractClientBalanceId(balanceId, clientId, sizeof(clientId))) {
    strlcpy(nodes[idx].balanceId, clientId, sizeof(nodes[idx].balanceId));
    setNodeDisplayLabel(idx);
    saveNodes();
  }
  hubLogPrintf("@%02u BALANCE_ID_SET %s cmd=%s\n", nodes[idx].id, balanceId, cmd);
  webServer.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"ID balance programme\"}");
}

void handleApiBalanceIdRead() {
  webServer.sendHeader("Access-Control-Allow-Origin", "*");
  if (!webServer.hasArg("plain")) {
    webServer.send(400, "application/json", "{\"error\":\"no body\"}");
    return;
  }
  StaticJsonDocument<192> doc;
  if (deserializeJson(doc, webServer.arg("plain")) != DeserializationError::Ok) {
    webServer.send(400, "application/json", "{\"error\":\"json_parse\"}");
    return;
  }
  int id = doc["id"] | -1;
  if (id < 1 || id > 254) {
    webServer.send(400, "application/json", "{\"error\":\"invalid_id\"}");
    return;
  }
  int idx = findNodeBySelfId((uint8_t)id);
  if (idx < 0) {
    webServer.send(404, "application/json", "{\"error\":\"node_not_found\"}");
    return;
  }
  if (!nodes[idx].active) {
    webServer.send(400, "application/json", "{\"error\":\"node_offline\"}");
    return;
  }
  uint8_t brand = doc["brand"] | nodes[idx].brand;
  const char* cmd = "";
  if (brand == BRAND_AD) cmd = "?ID";
  else if (brand == BRAND_METTLER) cmd = "I10";
  else {
    webServer.send(400, "application/json", "{\"error\":\"brand_not_supported\"}");
    return;
  }

  char raw[128];
  if (!sendBalanceCmdAndWait(idx, cmd, raw, sizeof(raw), 1800)) {
    webServer.send(504, "application/json", "{\"error\":\"read_timeout\"}");
    return;
  }
  char balanceId[21] = "";
  bool ok = parseBalanceIdFromReply(brand, raw, balanceId, sizeof(balanceId));
  if (ok) {
    char clientId[21] = "";
    if (extractClientBalanceId(balanceId, clientId, sizeof(clientId))) {
      strlcpy(nodes[idx].balanceId, clientId, sizeof(nodes[idx].balanceId));
      setNodeDisplayLabel(idx);
      saveNodes();
    }
  }
  String body = "{\"status\":\"ok\",\"message\":\"ID lu\",\"raw\":";
  appendJsonEscaped(body, raw);
  body += ",\"balanceId\":";
  appendJsonEscaped(body, ok ? balanceId : "");
  body += "}";
  webServer.send(200, "application/json", body);
}

void handleApiNodeDelete() {
  webServer.sendHeader("Access-Control-Allow-Origin", "*");
  int id = -1;
  if (webServer.hasArg("id")) {
    id = webServer.arg("id").toInt();
  } else if (webServer.hasArg("plain")) {
    StaticJsonDocument<64> doc;
    if (deserializeJson(doc, webServer.arg("plain")) != DeserializationError::Ok) {
      webServer.send(400, "application/json", "{\"error\":\"json_parse\"}");
      return;
    }
    id = doc["id"] | -1;
  } else {
    webServer.send(400, "application/json", "{\"error\":\"missing_id\"}");
    return;
  }
  if (id < 1 || id > 254) {
    webServer.send(400, "application/json", "{\"error\":\"invalid_id\"}");
    return;
  }
  if (!deleteNodeById((uint8_t)id)) {
    webServer.send(404, "application/json", "{\"error\":\"node_not_found\"}");
    return;
  }
  hubLogPrintf("@%02u NODE_DELETED\n", id);
  webServer.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Appareil supprime\"}");
}

void handleApiScanStart() {
  webServer.sendHeader("Access-Control-Allow-Origin", "*");
  startScan();
  webServer.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Recherche des nodes lancee\"}");
}

void handleApiPurge() {
  webServer.sendHeader("Access-Control-Allow-Origin", "*");
  int remoteResetCount = 0;
  for (int i = 0; i < nodeCount; i++) {
    if (!nodes[i].active) continue;
    sendMsg(nodes[i].mac, MSG_PURGE_RESET, "purge");
    remoteResetCount++;
    delay(25);
  }
  clearNodes();
  String body = "{\"status\":\"ok\",\"message\":\"Purge terminee\",\"remoteResetCount\":";
  body += String(remoteResetCount);
  body += "}";
  webServer.send(200, "application/json", body);
}

void handleApiSettings() {
  webServer.sendHeader("Access-Control-Allow-Origin", "*");
  char buf[120];
  snprintf(buf, sizeof(buf), "{\"sound\":%s,\"serialReplyBeep\":%s,\"serialReplyHaptic\":%s}",
    soundEnabled ? "true" : "false",
    serialReplyBeepEnabled ? "true" : "false",
    serialReplyHapticEnabled ? "true" : "false");
  webServer.send(200, "application/json", buf);
}

void handleApiSettingsSet() {
  webServer.sendHeader("Access-Control-Allow-Origin", "*");
  if (!webServer.hasArg("plain")) {
    webServer.send(400, "application/json", "{\"error\":\"no body\"}");
    return;
  }
  StaticJsonDocument<64> doc;
  if (deserializeJson(doc, webServer.arg("plain")) == DeserializationError::Ok) {
    if (doc.containsKey("sound")) soundEnabled = (bool)doc["sound"];
    if (doc.containsKey("serialReplyBeep")) serialReplyBeepEnabled = (bool)doc["serialReplyBeep"];
    if (doc.containsKey("serialReplyHaptic")) serialReplyHapticEnabled = (bool)doc["serialReplyHaptic"];
    saveSettings();
    webServer.send(200, "application/json", "{\"status\":\"ok\"}");
  } else {
    webServer.send(400, "application/json", "{\"error\":\"json_parse\"}");
  }
}

void handleApiProfiles() {
  webServer.sendHeader("Access-Control-Allow-Origin", "*");
  String body;
  body.reserve((profileCount * 512) + 16);
  body += '[';
  for (int i = 0; i < profileCount; i++) {
    if (i > 0) body += ',';
    char profileBalanceId[20] = "";
    extractClientBalanceId(profiles[i].label, profileBalanceId, sizeof(profileBalanceId));
    body += "{\"name\":";
    appendJsonEscaped(body, profiles[i].profileName);
    body += ",\"label\":";
    appendJsonEscaped(body, profiles[i].label);
    body += ",\"balanceId\":";
    appendJsonEscaped(body, profileBalanceId);
    body += ",\"type\":";
    appendJsonEscaped(body, profiles[i].typeName);
    body += ",\"brand\":";
    body += String(profiles[i].brand);
    body += ",\"brandName\":";
    appendJsonEscaped(body, BRAND_NAMES[profiles[i].brand < 3 ? profiles[i].brand : 0]);
    body += ",\"baud\":";
    body += String(profiles[i].baud);
    body += ",\"parity\":";
    body += String(profiles[i].parity);
    body += ",\"dataBits\":";
    body += String(profiles[i].dataBits);
    body += ",\"stopBits\":";
    body += String(profiles[i].stopBits);
    body += ",\"rxPin\":";
    body += String(profiles[i].rxPin);
    body += ",\"txPin\":";
    body += String(profiles[i].txPin);
    body += ",\"swapRxTx\":";
    body += profiles[i].swapRxTx ? "true" : "false";
    body += ",\"pollCmd\":";
    appendJsonEscaped(body, profiles[i].pollCmd);
    body += ",\"lineTimeout\":";
    body += String(profiles[i].lineTimeout);
    body += ",\"zeroCmd\":";
    appendJsonEscaped(body, profiles[i].zeroCmd);
    body += ",\"capacity\":";
    body += String(profiles[i].capacity, 4);
    body += ",\"resolution\":";
    body += String(profiles[i].resolution, 4);
    body += ",\"protocol\":";
    body += String(protocolForBrand(profiles[i].brand));
    body += '}';
  }
  body += ']';
  webServer.send(200, "application/json", body);
}

void handleApiProfilesSave() {
  webServer.sendHeader("Access-Control-Allow-Origin", "*");
  if (!webServer.hasArg("plain")) {
    webServer.send(400, "application/json", "{\"error\":\"no body\"}");
    return;
  }
  StaticJsonDocument<768> doc;
  if (deserializeJson(doc, webServer.arg("plain")) != DeserializationError::Ok) {
    webServer.send(400, "application/json", "{\"error\":\"json_parse\"}");
    return;
  }
  const char* profileName = doc["profileName"] | "";
  if (!profileName[0]) {
    webServer.send(400, "application/json", "{\"error\":\"missing_profile_name\"}");
    return;
  }
  int idx = findProfileIndexByName(profileName);
  if (idx < 0) {
    if (profileCount >= MAX_PROFILES) {
      webServer.send(400, "application/json", "{\"error\":\"profile_limit\"}");
      return;
    }
    idx = profileCount++;
    memset(&profiles[idx], 0, sizeof(profiles[idx]));
  }
  strlcpy(profiles[idx].profileName, profileName, sizeof(profiles[idx].profileName));
  char rawLabel[32];
  strlcpy(rawLabel, doc["label"] | "", sizeof(rawLabel));
  char rawBalanceId[20];
  strlcpy(rawBalanceId, doc["balanceId"] | "", sizeof(rawBalanceId));
  char parsedBalanceId[20] = "";
  if (extractClientBalanceId(rawBalanceId, parsedBalanceId, sizeof(parsedBalanceId)) || extractClientBalanceId(rawLabel, parsedBalanceId, sizeof(parsedBalanceId))) {
    snprintf(profiles[idx].label, sizeof(profiles[idx].label), "CDO %s", parsedBalanceId);
  } else {
    strlcpy(profiles[idx].label, rawLabel, sizeof(profiles[idx].label));
  }
  strlcpy(profiles[idx].typeName, doc["name"] | "", sizeof(profiles[idx].typeName));
  profiles[idx].brand = doc["brand"] | 0;
  profiles[idx].baud = doc["baud"] | 2400;
  profiles[idx].parity = doc["parity"] | 1;
  profiles[idx].dataBits = doc["dataBits"] | 7;
  profiles[idx].stopBits = doc["stopBits"] | 1;
  profiles[idx].rxPin = doc["rxPin"] | 5;
  profiles[idx].txPin = doc["txPin"] | 6;
  profiles[idx].swapRxTx = doc["swapRxTx"] | false;
  strlcpy(profiles[idx].pollCmd, doc["pollCmd"] | "Q", sizeof(profiles[idx].pollCmd));
  profiles[idx].lineTimeout = doc["lineTimeout"] | 300;
  strlcpy(profiles[idx].zeroCmd, doc["zeroCmd"] | "Z", sizeof(profiles[idx].zeroCmd));
  profiles[idx].capacity = doc["capacity"] | 5000.0f;
  profiles[idx].resolution = doc["resolution"] | 0.01f;
  saveProfiles();
  webServer.send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleApiProfilesDelete() {
  webServer.sendHeader("Access-Control-Allow-Origin", "*");
  String name = webServer.arg("name");
  int idx = findProfileIndexByName(name.c_str());
  if (idx < 0) {
    webServer.send(404, "application/json", "{\"error\":\"profile_not_found\"}");
    return;
  }
  for (int i = idx; i < profileCount - 1; i++) profiles[i] = profiles[i + 1];
  if (profileCount > 0) profileCount--;
  memset(&profiles[profileCount], 0, sizeof(profiles[profileCount]));
  saveProfiles();
  webServer.send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleApiProfilesApply() {
  webServer.sendHeader("Access-Control-Allow-Origin", "*");
  if (!webServer.hasArg("plain")) {
    webServer.send(400, "application/json", "{\"error\":\"no body\"}");
    return;
  }
  StaticJsonDocument<192> doc;
  if (deserializeJson(doc, webServer.arg("plain")) != DeserializationError::Ok) {
    webServer.send(400, "application/json", "{\"error\":\"json_parse\"}");
    return;
  }
  int id = doc["id"] | -1;
  const char* profileName = doc["profileName"] | "";
  int nodeIdx = findNodeBySelfId((uint8_t)id);
  int profileIdx = findProfileIndexByName(profileName);
  if (nodeIdx < 0) {
    webServer.send(404, "application/json", "{\"error\":\"node_not_found\"}");
    return;
  }
  if (profileIdx < 0) {
    webServer.send(404, "application/json", "{\"error\":\"profile_not_found\"}");
    return;
  }
  applyProfileToNode(nodeIdx, profileIdx);
  webServer.send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleApiOptions() {
  webServer.sendHeader("Access-Control-Allow-Origin", "*");
  webServer.sendHeader("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
  webServer.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  webServer.send(204);
}

void handleNotFound() {
  webServer.send(404, "text/plain", "Not found");
}

// =====================================================
// Scan
// =====================================================
void sendDiscoverBroadcast() {
  uint8_t bc[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  sendMsg(bc, MSG_DISCOVER, "");
}

void startScan() {
  for (int i = 0; i < nodeCount; i++) nodes[i].active = false;
  selectedNode = -1;
  listScroll = 0;
  uiState = STATE_SCANNING;
  scanStart = millis();
  lastDiscover = 0;
  displayDirty = true;
  hubLogPrintln("@00 SCAN_START");
}

// =====================================================
// ESP-NOW callbacks
// =====================================================
void onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  msg_t m;
  if (!validateMsg(data, len, m)) {
    badPackets++;
    return;
  }

  switch (m.type) {
    case MSG_ANNOUNCE: {
      int idx = upsertNode(info->src_addr, m.payload, m.nodeId);
      if (idx >= 0) {
        hubLogPrintf("@00 FOUND id=%u name=%s\n", m.nodeId, nodes[idx].nodeName);
        displayDirty = true;
        sendConfigReqToNode(idx);  // demande config à chaque announce
        sendInfoReqToNode(idx);
      }
      break;
    }

    case MSG_RESPONSE: {
      int idx = findNodeByMac(info->src_addr);
      if (idx < 0) break;
      nodes[idx].lastSeen = millis();
      nodes[idx].active = true;
      bool isError = (strncmp(m.payload, "ERROR", 5) == 0);
      bool matchesPending = (pending.kind == PEND_BALANCE && memcmp(pending.mac, info->src_addr, 6) == 0);
      if (apiBalanceReplyPending && nodes[idx].id == apiBalanceReplyNodeId) {
        strlcpy(apiBalanceReply, m.payload, sizeof(apiBalanceReply));
        apiBalanceReplyReady = true;
      }
      if (matchesPending && pendingToPc) {
        if (isError) Serial.printf("@%02u %s\n", nodes[idx].id, m.payload);
        else Serial.printf("@%02u RX %s\n", nodes[idx].id, m.payload);
      }
      if (!isError && !nodes[idx].balReady) {
        nodes[idx].balReady = true;
        displayDirty = true;
      }
      if (idx == selectedNode && !isError) {
        if (pendingTare[idx]) {
          tareOffset[idx] += pendingTareAdjust[idx];
          pendingTareAdjust[idx] = 0;
          pendingTare[idx] = false;
        }
        if (strncmp(lastValue, m.payload, 63) != 0) pushHistory(m.payload);
        strncpy(lastValue, m.payload, 63);
        lastValue[63] = 0;
        lastValueTime = millis();
        displayDirty = true;
        if (soundEnabled) pendingBeep = true;
      }
      if (matchesPending) {
        clearPending();
      }
      break;
    }

    case MSG_STATUS: {
      int idx = findNodeByMac(info->src_addr);
      if (idx < 0) break;
      nodes[idx].lastSeen = millis();
      nodes[idx].active = true;
      hubLogPrintf("@%02u EVENT %s\n", nodes[idx].id, m.payload);
      if (strcmp(m.payload, "IDENTITY_UPDATED") == 0) {
        sendInfoReqToNode(idx);
      }
      if (idx == selectedNode) {
        strncpy(lastStatus, m.payload, 31);
        lastStatus[31] = 0;
        displayDirty = true;
      }
      break;
    }

    case MSG_PONG: {
      int idx = findNodeByMac(info->src_addr);
      if (idx < 0) break;
      nodes[idx].lastSeen = millis();
      nodes[idx].active = true;
      hubLogPrintf("@%02u PONG %s\n", nodes[idx].id, m.payload);
      if (pending.kind == PEND_PING && memcmp(pending.mac, info->src_addr, 6) == 0) {
        clearPending();
      }
      break;
    }

    case MSG_NODE_INFO_RESP: {
      int idx = findNodeByMac(info->src_addr);
      if (idx < 0) break;
      nodes[idx].lastSeen = millis();
      nodes[idx].active = true;
      char tmp[32];
      bool identityChanged = false;
      if (extractQuotedField(m.payload, "bid", tmp, sizeof(tmp))) {
        if (!extractClientBalanceId(tmp, nodes[idx].balanceId, sizeof(nodes[idx].balanceId))) {
          strlcpy(nodes[idx].balanceId, tmp, sizeof(nodes[idx].balanceId));
        }
        setNodeDisplayLabel(idx);
        applyClientBrandMapping(idx);
        identityChanged = true;
      }
      if (extractQuotedField(m.payload, "label", tmp, sizeof(tmp))) {
        char parsedId[20];
        bool parsedLabelId = extractClientBalanceId(tmp, parsedId, sizeof(parsedId));
        if (parsedLabelId) {
          strlcpy(nodes[idx].balanceId, parsedId, sizeof(nodes[idx].balanceId));
          setNodeDisplayLabel(idx);
          applyClientBrandMapping(idx);
          identityChanged = true;
        }
        if (!parsedLabelId && strcmp(tmp, "CDO ?") != 0) {
          strlcpy(nodes[idx].displayLabel, tmp, sizeof(nodes[idx].displayLabel));
          identityChanged = true;
        }
      }
      if (extractQuotedField(m.payload, "fw", tmp, sizeof(tmp))) {
        strlcpy(nodes[idx].firmwareVariant, tmp, sizeof(nodes[idx].firmwareVariant));
      }
      if (identityChanged) saveNodes();
      if (autoApplyProfileForNode(idx)) {
        displayDirty = true;
        break;
      }
      displayDirty = true;
      hubLogPrintf("@%02u STATUS %s\n", nodes[idx].id, m.payload);
      if (pending.kind == PEND_INFO && memcmp(pending.mac, info->src_addr, 6) == 0) {
        clearPending();
      }
      break;
    }

    case MSG_HEARTBEAT: {
      int idx = findNodeByMac(info->src_addr);
      if (idx >= 0) {
        nodes[idx].lastSeen = millis();
        if (!nodes[idx].active) {
          nodes[idx].active = true;
          displayDirty = true;
          hubLogPrintf("@%02u BACK_ONLINE\n", nodes[idx].id);
          sendConfigReqToNode(idx);
        }
        // Parsing état balance : payload "bal:1" ou "bal:0"
        if (strncmp(m.payload, "bal:", 4) == 0) {
          bool bal = (m.payload[4] == '1');
          if (nodes[idx].balReady != bal) {
            nodes[idx].balReady = bal;
            displayDirty = true;
            hubLogPrintf("@%02u BAL_%s\n", nodes[idx].id, bal ? "READY" : "OFFLINE");
          }
        }
      }
      break;
    }

    case MSG_CONFIG_RESP: {
      int idx = findNodeByMac(info->src_addr);
      if (idx < 0) break;
      StaticJsonDocument<256> doc;
      if (deserializeJson(doc, m.payload) == DeserializationError::Ok) {
        strlcpy(nodes[idx].nodeName, doc["n"] | nodes[idx].nodeName, sizeof(nodes[idx].nodeName));
        strlcpy(nodes[idx].typeName, doc["tn"] | nodes[idx].typeName, sizeof(nodes[idx].typeName));
        nodes[idx].brand       = doc["br"] | 0;
        nodes[idx].baud        = doc["bd"] | 2400;
        nodes[idx].parity      = doc["pa"] | 1;
        nodes[idx].dataBits    = doc["db"] | 7;
        nodes[idx].stopBits    = doc["sb"] | 1;
        nodes[idx].rxPin       = doc["rx"] | 5;
        nodes[idx].txPin       = doc["tx"] | 6;
        nodes[idx].swapRxTx    = (doc["sw"] | 0) != 0;
        strlcpy(nodes[idx].pollCmd, doc["cmd"] | "Q", sizeof(nodes[idx].pollCmd));
        nodes[idx].lineTimeout = doc["to"] | 300;
        strlcpy(nodes[idx].zeroCmd, doc["zc"] | doc["zero"] | "Z", sizeof(nodes[idx].zeroCmd));
        nodes[idx].capacity    = doc["cp"] | doc["cap"] | 5000.0f;
        nodes[idx].resolution  = doc["rs"] | doc["res"] | 0.01f;
        strlcpy(nodes[idx].firmwareVariant, doc["fv"] | "", sizeof(nodes[idx].firmwareVariant));
        if (nodes[idx].capacity <= 0) nodes[idx].capacity = 5000.0f;
        if (nodes[idx].resolution <= 0) nodes[idx].resolution = 0.01f;
        nodes[idx].configKnown = true;
        if (!nodes[idx].zeroCmd[0]) strlcpy(nodes[idx].zeroCmd, "Z", sizeof(nodes[idx].zeroCmd));
        saveNodes();
        if (autoApplyProfileForNode(idx)) {
          displayDirty = true;
          break;
        }
        hubLogPrintf("@%02u CONFIG_RECV brand=%d baud=%u\n",
          nodes[idx].id, nodes[idx].brand, nodes[idx].baud);
      }
      break;
    }

    case MSG_CONFIG_ACK: {
      int idx = findNodeByMac(info->src_addr);
      if (idx >= 0) {
        hubLogPrintf("@%02u CONFIG_ACK %s\n", nodes[idx].id, m.payload);
      }
      break;
    }

    default:
      break;
  }
}

void onSent(const wifi_tx_info_t* info, esp_now_send_status_t status) {
  (void)info;
  (void)status;
}

// =====================================================
// Distinction commandes système vs balance
// =====================================================
bool isSystemCommand(const char* cmd) {
  return (strcmp(cmd, "PING") == 0) || (strcmp(cmd, "STATUS") == 0);
}

static void writeCurrentWeightToHostSerial() {
  if (usbSerialMuted()) return;
  serialHostMode = true;
  if (selectedNode < 0 || selectedNode >= nodeCount || !lastValue[0] || strncmp(lastValue, "ERROR", 5) == 0) {
    Serial.print("?\r\n");
    playSerialReplyBeep();
    playSerialReplyHaptic();
    return;
  }

  Serial.print(lastValue);
  Serial.print("\r\n");
  playSerialReplyBeep();
  playSerialReplyHaptic();
}

// =====================================================
// Parseur série PC
// =====================================================
void processSerialCommand(char* cmd) {
  if (usbSerialMuted()) return;
  while (*cmd == ' ' || *cmd == '\t') cmd++;
  if (*cmd == 0) return;
  int l = strlen(cmd);
  while (l > 0 && (cmd[l - 1] == ' ' || cmd[l - 1] == '\t')) cmd[--l] = 0;
  if (l == 0) return;

  if (strcmp(cmd, "SI") == 0) {
    writeCurrentWeightToHostSerial();
    return;
  }

  if (cmd[0] != '@' || l < 3) {
    Serial.println("@00 ERROR invalid_command");
    return;
  }

  int id = (int)strtol(cmd + 1, NULL, 16);
  const char* rest = cmd + 3;
  while (*rest == ' ') rest++;

  if (id == 0x00) {
    if (strcmp(rest, "SCAN") == 0) {
      startScan();
    } else if (strcmp(rest, "LIST") == 0) {
      Serial.printf("@00 LIST count=%d\n", nodeCount);
      for (int i = 0; i < nodeCount; i++) {
        unsigned long ago = nodes[i].lastSeen ? (millis() - nodes[i].lastSeen) / 1000UL : 0;
        Serial.printf("@%02u node=%s status=%s age=%lus\n",
          nodes[i].id, nodes[i].nodeName,
          nodes[i].active ? "online" : "offline", ago);
      }
    } else if (strcmp(rest, "CLEAR") == 0) {
      clearNodes();
      uiState = STATE_IDLE;
      displayDirty = true;
      Serial.println("@00 NVS_CLEARED");
    } else if (strcmp(rest, "STATS") == 0) {
      Serial.printf("@00 STATS nodes=%d bad=%lu uptime=%lus ap=%s\n",
        nodeCount, badPackets, millis() / 1000UL, apSsid);
    } else {
      Serial.println("@00 ERROR invalid_command");
    }
    return;
  }

  if (id == 0xFF) {
    int sent = 0;
    for (int i = 0; i < nodeCount; i++) {
      if (!nodes[i].active) continue;
      if (strcmp(rest, "PING") == 0) sendMsg(nodes[i].mac, MSG_PING, "");
      else if (strcmp(rest, "STATUS") == 0) sendMsg(nodes[i].mac, MSG_NODE_INFO_REQ, "");
      else sendMsg(nodes[i].mac, MSG_COMMAND, rest);
      sent++;
    }
    Serial.printf("@FF BROADCAST %s to=%d\n", rest, sent);
    return;
  }

  int idx = findActiveNodeById((uint8_t)id);
  if (idx < 0) {
    Serial.printf("@%02X ERROR node_not_found\n", id);
    return;
  }

  if (strcmp(rest, "PING") == 0) sendPingToNode(idx);
  else if (strcmp(rest, "STATUS") == 0) sendInfoReqToNode(idx);
  else if (*rest == 0) Serial.printf("@%02X ERROR empty_command\n", id);
  else sendBalanceCmdToNode(idx, rest);
}

void processSerialByte(char c) {
  if (usbSerialMuted()) return;
  if (serialLen == 0 && c == ' ') {
    writeCurrentWeightToHostSerial();
    serialLastWasCR = false;
    return;
  }

  if (c == '\r') {
    if (serialLen > 0) {
      serialLine[serialLen] = 0;
      processSerialCommand(serialLine);
      serialLen = 0;
    }
    serialLastWasCR = true;
  } else if (c == '\n') {
    if (serialLastWasCR) {
      serialLastWasCR = false;
    } else if (serialLen > 0) {
      serialLine[serialLen] = 0;
      processSerialCommand(serialLine);
      serialLen = 0;
    }
  } else {
    serialLastWasCR = false;
    if (serialLen < (int)sizeof(serialLine) - 1) {
      serialLine[serialLen++] = c;
    } else {
      serialLen = 0;
    }
  }
}


// =====================================================
// UI Waveshare LVGL 360x360 - round display safe layout
// =====================================================
#define UI_W 360
#define UI_H 360
#define UI_CX 180
#define UI_CY 180
#define UI_R 180

static bool lastTouchPressed = false;
static unsigned long lastTouchAt = 0;

static lv_color_t uiColor(uint32_t hex) { return lv_color_hex(hex); }
static const lv_color_t C_DARK = lv_color_hex(0x020712);
static const lv_color_t C_PANEL = lv_color_hex(0x101B29);
static const lv_color_t C_PANEL_2 = lv_color_hex(0x101B29);
static const lv_color_t C_BLUE = lv_color_hex(0x147CFF);
static const lv_color_t C_CYAN = lv_color_hex(0x22D9FF);
static const lv_color_t C_GREEN = lv_color_hex(0x69F044);
static const lv_color_t C_TEXT = lv_color_hex(0xF4F7FB);
static const lv_color_t C_MUTED = lv_color_hex(0x93AED2);

static lv_obj_t* addText(lv_obj_t* parent, const char* text, int x, int y, const lv_font_t* font, lv_color_t color) {
  lv_obj_t* label = lv_label_create(parent);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, color, 0);
  lv_obj_set_style_text_opa(label, LV_OPA_COVER, 0);
  lv_obj_set_pos(label, x, y);
  return label;
}

static lv_obj_t* addCenteredText(lv_obj_t* parent, const char* text, int cx, int y, const lv_font_t* font, lv_color_t color) {
  lv_obj_t* label = addText(parent, text, 0, y, font, color);
  lv_obj_update_layout(label);
  lv_obj_set_x(label, cx - lv_obj_get_width(label) / 2);
  return label;
}

static uint8_t lerp8(uint8_t a, uint8_t b, uint8_t t) {
  return a + (((int)b - (int)a) * t) / 255;
}

static lv_color_t mixHex(uint32_t a, uint32_t b, uint8_t t) {
  uint8_t ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
  uint8_t br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
  return lv_color_make(lerp8(ar, br, t), lerp8(ag, bg, t), lerp8(ab, bb, t));
}

static void addBackdropTexture(lv_obj_t* parent) {
  for (int y = 0; y < UI_H; y += 3) {
    uint8_t t = (uint8_t)((y * 255) / (UI_H - 1));
    lv_color_t c = t < 140
      ? mixHex(0x020712, 0x082447, (uint8_t)((t * 255) / 140))
      : mixHex(0x082447, 0x02050C, (uint8_t)(((t - 140) * 255) / 115));
    lv_obj_t* strip = lv_obj_create(parent);
    lv_obj_remove_style_all(strip);
    lv_obj_set_size(strip, UI_W, 3);
    lv_obj_set_pos(strip, 0, y);
    lv_obj_set_style_bg_opa(strip, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(strip, c, 0);
  }

  lv_obj_t* wash = lv_obj_create(parent);
  lv_obj_remove_style_all(wash);
  lv_obj_set_size(wash, 360, 150);
  lv_obj_set_pos(wash, 0, 188);
  lv_obj_set_style_bg_opa(wash, LV_OPA_20, 0);
  lv_obj_set_style_bg_color(wash, lv_color_hex(0x0B3158), 0);

  lv_obj_t* vignetteTop = lv_obj_create(parent);
  lv_obj_remove_style_all(vignetteTop);
  lv_obj_set_size(vignetteTop, 360, 54);
  lv_obj_set_pos(vignetteTop, 0, 0);
  lv_obj_set_style_bg_opa(vignetteTop, LV_OPA_40, 0);
  lv_obj_set_style_bg_color(vignetteTop, lv_color_hex(0x01030A), 0);

  lv_obj_t* vignetteBottom = lv_obj_create(parent);
  lv_obj_remove_style_all(vignetteBottom);
  lv_obj_set_size(vignetteBottom, 360, 64);
  lv_obj_set_pos(vignetteBottom, 0, 296);
  lv_obj_set_style_bg_opa(vignetteBottom, LV_OPA_50, 0);
  lv_obj_set_style_bg_color(vignetteBottom, lv_color_hex(0x01030A), 0);

  static const uint16_t pts[][2] = {
    {28,70},{54,214},{76,114},{96,302},{118,38},{142,254},{166,92},{204,310},
    {226,54},{248,232},{276,122},{302,276},{324,86},{336,198},{40,148},{312,152},
    {22,242},{88,184},{132,320},{188,42},{216,174},{266,296},{318,52},{342,248}
  };
  for (uint8_t i = 0; i < sizeof(pts) / sizeof(pts[0]); i++) {
    lv_obj_t* dot = lv_obj_create(parent);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 1, 1);
    lv_obj_set_pos(dot, pts[i][0], pts[i][1]);
    lv_obj_set_style_bg_opa(dot, LV_OPA_30, 0);
    lv_obj_set_style_bg_color(dot, (i & 1) ? lv_color_hex(0x163251) : lv_color_hex(0x0A1D33), 0);
  }
}

static lv_obj_t* addRoundPanel(lv_obj_t* parent, int x, int y, int w, int h) {
  lv_obj_t* card = lv_obj_create(parent);
  lv_obj_remove_style_all(card);
  lv_obj_set_pos(card, x, y);
  lv_obj_set_size(card, w, h);
  lv_obj_set_style_radius(card, 14, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(card, C_PANEL, 0);
  lv_obj_set_style_bg_grad_color(card, C_PANEL_2, 0);
  lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_NONE, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(card, lv_color_hex(0x1B2B42), 0);
  return card;
}

static void selectNodeIndex(int idx) {
  if (idx < 0 || idx >= nodeCount) return;
  listAnimFrom = -1;
  listAnimTo = -1;
  listAnimDir = 0;
  selectedNode = idx;
  lastValue[0] = 0;
  lastStatus[0] = 0;
  historyCount = 0;
  uiState = STATE_CONNECTED;
  displayDirty = true;
  if (!nodes[idx].persisted) saveNodes();
  hubLogPrintf("@%02u SELECTED %s\n", nodes[idx].id, nodes[idx].nodeName);
}

static void uiEvtStartScan(lv_event_t*) {
  startScan();
}

static void uiEvtOpenWifiQr(lv_event_t*) {
  if (millis() - lastNavAt < 350) return;
  lastNavAt = millis();
  uiState = STATE_WIFI_QR;
  displayDirty = true;
}

static void uiEvtSelectNode(lv_event_t* e) {
  int idx = (int)(intptr_t)lv_event_get_user_data(e);
  selectNodeIndex(idx);
}

static void uiEvtIdentifyNode(lv_event_t* e) {
  int idx = (int)(intptr_t)lv_event_get_user_data(e);
  if (idx < 0 || idx >= nodeCount || !nodes[idx].active) return;
  sendLocalBalanceCmdToNode(idx, "IDENTIFY");
  triggerButtonFeedback(3, 2);
}

static void uiEvtBack(lv_event_t*) {
  if (millis() - lastNavAt < 350) return;
  lastNavAt = millis();
  listAnimFrom = -1;
  listAnimTo = -1;
  listAnimDir = 0;
  selectedNode = -1;
  lastValue[0] = 0;
  lastStatus[0] = 0;
  uiState = STATE_LIST;
  displayDirty = true;
}

static void uiEvtHome(lv_event_t*) {
  if (millis() - lastNavAt < 350) return;
  lastNavAt = millis();
  listAnimFrom = -1;
  listAnimTo = -1;
  listAnimDir = 0;
  selectedNode = -1;
  lastValue[0] = 0;
  lastStatus[0] = 0;
  uiState = STATE_LIST;
  displayDirty = true;
}

static void uiEvtCloseWifiQr(lv_event_t*) {
  if (millis() - lastNavAt < 350) return;
  lastNavAt = millis();
  uiState = STATE_LIST;
  displayDirty = true;
}

static void uiEvtZero(lv_event_t*) {
  if (selectedNode < 0 || selectedNode >= nodeCount) return;
  beep();
  if (lastValue[0] && strncmp(lastValue, "ERROR", 5) != 0) {
    pendingTareAdjust[selectedNode] = parseWeightValue(lastValue);
    pendingTare[selectedNode] = true;
  }
  const char* zero = (nodes[selectedNode].configKnown && nodes[selectedNode].zeroCmd[0]) ? nodes[selectedNode].zeroCmd : "Z";
  sendLocalBalanceCmdToNode(selectedNode, zero);
}

static void uiEvtButtonPressed(lv_event_t* e) {
  uintptr_t packed = (uintptr_t)lv_event_get_user_data(e);
  uint8_t idx = packed & 0xFF;
  uint8_t effect = (packed >> 8) & 0xFF;
  triggerButtonFeedback(idx, effect ? effect : 1);
}

static lv_obj_t* addPillButton(lv_obj_t* parent, int x, int y, int w, int h,
                               const char* text, lv_event_cb_t cb = NULL,
                               void* userData = NULL, bool active = false,
                               uint8_t feedbackIdx = 255, uint8_t hapticEffect = 1,
                               lv_event_code_t actionEvent = LV_EVENT_CLICKED) {
  lv_obj_t* btn = lv_obj_create(parent);
  lv_obj_remove_style_all(btn);
  lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_pos(btn, x, y + (active ? 2 : 0));
  lv_obj_set_size(btn, w, h - (active ? 2 : 0));
  lv_obj_set_style_radius(btn, h / 2, 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(btn, active ? lv_color_hex(0x1E83FF) : lv_color_hex(0x0A3472), 0);
  lv_obj_set_style_bg_grad_color(btn, active ? lv_color_hex(0x4EA1FF) : lv_color_hex(0x176CDE), 0);
  lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_NONE, 0);
  lv_obj_set_style_border_width(btn, 1, 0);
  lv_obj_set_style_border_color(btn, active ? C_TEXT : lv_color_hex(0x1E83FF), 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0x2E9BFF), LV_STATE_PRESSED);
  lv_obj_set_style_bg_grad_color(btn, lv_color_hex(0x74BBFF), LV_STATE_PRESSED);
  lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_NONE, LV_STATE_PRESSED);
  lv_obj_set_style_border_color(btn, C_TEXT, LV_STATE_PRESSED);
  lv_obj_set_style_translate_y(btn, 2, LV_STATE_PRESSED);
  lv_obj_set_style_shadow_width(btn, 14, LV_STATE_PRESSED);
  lv_obj_set_style_shadow_opa(btn, LV_OPA_50, LV_STATE_PRESSED);
  lv_obj_set_style_shadow_color(btn, lv_color_hex(0x1E83FF), LV_STATE_PRESSED);
  lv_obj_t* label = lv_label_create(btn);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_color(label, C_TEXT, 0);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
  lv_obj_center(label);
  if (feedbackIdx != 255) {
    uintptr_t packed = (uintptr_t)feedbackIdx | ((uintptr_t)hapticEffect << 8);
    lv_obj_add_event_cb(btn, uiEvtButtonPressed, LV_EVENT_PRESSED, (void*)packed);
  }
  if (cb) lv_obj_add_event_cb(btn, cb, actionEvent, userData);
  return btn;
}

static lv_obj_t* addIconButton(lv_obj_t* parent, int x, int y, int size,
                               const char* icon, lv_event_cb_t cb = NULL,
                               void* userData = NULL, bool active = false,
                               uint8_t feedbackIdx = 255, uint8_t hapticEffect = 1,
                               lv_event_code_t actionEvent = LV_EVENT_CLICKED) {
  lv_obj_t* btn = lv_obj_create(parent);
  lv_obj_remove_style_all(btn);
  lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_pos(btn, x, y + (active ? 2 : 0));
  lv_obj_set_size(btn, size, size - (active ? 2 : 0));
  lv_obj_set_style_radius(btn, size / 2, 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(btn, active ? lv_color_hex(0x155AB5) : lv_color_hex(0x0D223C), 0);
  lv_obj_set_style_border_width(btn, 1, 0);
  lv_obj_set_style_border_color(btn, active ? lv_color_hex(0x89C4FF) : lv_color_hex(0x22508A), 0);
  lv_obj_set_style_shadow_width(btn, 12, 0);
  lv_obj_set_style_shadow_opa(btn, LV_OPA_30, 0);
  lv_obj_set_style_shadow_color(btn, lv_color_hex(0x07101C), 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0x173B63), LV_STATE_PRESSED);
  lv_obj_set_style_border_color(btn, lv_color_hex(0x5FAEFF), LV_STATE_PRESSED);
  lv_obj_set_style_translate_y(btn, 1, LV_STATE_PRESSED);

  lv_obj_t* label = lv_label_create(btn);
  lv_label_set_text(label, icon);
  lv_obj_set_style_text_color(label, C_TEXT, 0);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
  lv_obj_center(label);

  if (feedbackIdx != 255) {
    uintptr_t packed = (uintptr_t)feedbackIdx | ((uintptr_t)hapticEffect << 8);
    lv_obj_add_event_cb(btn, uiEvtButtonPressed, LV_EVENT_PRESSED, (void*)packed);
  }
  if (cb) lv_obj_add_event_cb(btn, cb, actionEvent, userData);
  return btn;
}

static void addStatusPill(lv_obj_t* parent, const char* text, int cx, int y, lv_color_t color) {
  lv_obj_t* pill = lv_obj_create(parent);
  lv_obj_remove_style_all(pill);
  lv_obj_set_size(pill, 116, 30);
  lv_obj_set_pos(pill, cx - 58, y);
  lv_obj_set_style_radius(pill, 15, 0);
  lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(pill, lv_color_hex(0x183A2D), 0);
  lv_obj_t* dot = lv_obj_create(pill);
  lv_obj_remove_style_all(dot);
  lv_obj_set_size(dot, 9, 9);
  lv_obj_set_style_radius(dot, 5, 0);
  lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(dot, color, 0);
  lv_obj_set_pos(dot, 18, 10);
  addText(pill, text, 34, 6, &lv_font_montserrat_14, C_TEXT);
}

static void addGaugeArc(lv_obj_t* parent, int pct, lv_color_t color) {
  if (pct < 0) pct = 0;
  if (pct > 1000) pct = 1000;
  lv_obj_t* arc = lv_arc_create(parent);
  lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
  lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_pos(arc, 7, 7);
  lv_obj_set_size(arc, 346, 346);
  lv_arc_set_range(arc, 0, 1000);
  lv_arc_set_value(arc, pct);
  lv_arc_set_bg_angles(arc, 154, 220);
  lv_obj_set_style_arc_width(arc, 4, LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc, 7, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arc, lv_color_hex(0x08223F), LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);
}

static float parseWeightValue(const char* raw) {
  if (!raw) return 0.0f;
  const char* p = raw;
  while (*p && *p != '+' && *p != '-' && *p != '.' && *p != ',' && (*p < '0' || *p > '9')) p++;
  static char num[24];
  size_t o = 0;
  if ((*p == '+' || *p == '-') && o < sizeof(num) - 1) num[o++] = *p++;
  while (((*p >= '0' && *p <= '9') || *p == '.' || *p == ',') && o < sizeof(num) - 1) {
    num[o++] = (*p == ',') ? '.' : *p;
    p++;
  }
  num[o] = 0;
  return o ? atof(num) : 0.0f;
}

static const char* formatWeightValue(const char* raw) {
  static char buf[24];
  if (!raw) {
    strlcpy(buf, "---", sizeof(buf));
    return buf;
  }

  const char* p = raw;
  while (*p && *p != '+' && *p != '-' && *p != '.' && *p != ',' && (*p < '0' || *p > '9')) p++;
  if (!*p) {
    strlcpy(buf, "---", sizeof(buf));
    return buf;
  }

  size_t out = 0;
  if ((*p == '+' || *p == '-') && out < sizeof(buf) - 1) buf[out++] = *p++;

  bool sawDigit = false;
  while (((*p >= '0' && *p <= '9') || *p == '.' || *p == ',') && out < sizeof(buf) - 1) {
    if (*p >= '0' && *p <= '9') sawDigit = true;
    buf[out++] = (*p == '.' || *p == ',') ? ',' : *p;
    p++;
  }
  buf[out] = 0;

  if (!sawDigit) {
    strlcpy(buf, "---", sizeof(buf));
  }
  return buf;
}

static int gaugePermille(const char* raw, int nodeIdx) {
  if (!raw || !*raw || strncmp(raw, "ERROR", 5) == 0) return 0;
  float capacity = 5000.0f;
  if (nodeIdx >= 0 && nodeIdx < nodeCount && nodes[nodeIdx].capacity > 0) capacity = nodes[nodeIdx].capacity;
  float value = parseWeightValue(raw);
  if (nodeIdx >= 0 && nodeIdx < MAX_NODES) value += tareOffset[nodeIdx];
  value = fabsf(value);
  int pct = (int)roundf((value * 1000.0f) / capacity);
  if (pct < 0) pct = 0;
  if (pct > 1000) pct = 1000;
  return pct;
}

static lv_color_t gaugeColorForPermille(int pct) {
  if (pct < 0) pct = 0;
  if (pct > 1000) pct = 1000;
  uint8_t r, g, b;
  if (pct <= 500) {
    float t = pct / 500.0f;
    r = (uint8_t)(0x69 + (0xFF - 0x69) * t);
    g = (uint8_t)(0xF0 + (0xD6 - 0xF0) * t);
    b = (uint8_t)(0x44 + (0x2F - 0x44) * t);
  } else {
    float t = (pct - 500) / 500.0f;
    r = 0xFF;
    g = (uint8_t)(0xD6 + (0x3B - 0xD6) * t);
    b = (uint8_t)(0x2F + (0x30 - 0x2F) * t);
  }
  return lv_color_make(r, g, b);
}

static int activeNodeCount() {
  int active = 0;
  for (int i = 0; i < nodeCount; i++) if (nodes[i].active) active++;
  return active;
}

static int listFirstVisible() {
  const int visibleRows = 4;
  if (nodeCount <= visibleRows) return 0;
  int first = (listScroll / visibleRows) * visibleRows;
  if (first > nodeCount - visibleRows) first = max(0, nodeCount - visibleRows);
  return first;
}

static void startListAnimation(int fromIdx, int toIdx, int dir) {
  if (fromIdx < 0 || toIdx < 0 || fromIdx == toIdx) {
    listAnimFrom = -1;
    listAnimTo = -1;
    listAnimDir = 0;
    return;
  }
  listAnimFrom = fromIdx;
  listAnimTo = toIdx;
  listAnimDir = dir >= 0 ? 1 : -1;
  listAnimStart = millis();
  displayDirty = true;
}

static float listAnimProgress() {
  if (listAnimFrom < 0 || listAnimTo < 0) return 1.0f;
  const unsigned long duration = 180;
  unsigned long elapsed = millis() - listAnimStart;
  if (elapsed >= duration) return 1.0f;
  float t = (float)elapsed / (float)duration;
  return 1.0f - (1.0f - t) * (1.0f - t);
}

static const char* currentDeviceName() {
  if (selectedNode >= 0 && selectedNode < nodeCount) return nodeDisplayName(selectedNode);
  for (int i = 0; i < nodeCount; i++) if (nodes[i].active) return nodeDisplayName(i);
  return "BDP Hub";
}

static const char* nodeConfiguredName(int idx) {
  if (idx >= 0 && idx < nodeCount && nodes[idx].typeName[0]) return nodes[idx].typeName;
  return "";
}

static void buildWifiQrPayload(char* out, size_t outSize) {
  if (!out || outSize == 0) return;
  snprintf(out, outSize, "WIFI:T:WPA;S:%s;P:%s;;", apSsid, AP_PASS);
}

static bool addWifiQrCode(lv_obj_t* parent, const char* payload, int x, int y, int targetSize) {
  static uint8_t qrData[qrcodegen_BUFFER_LEN_FOR_VERSION(8)];
  static uint8_t tempData[qrcodegen_BUFFER_LEN_FOR_VERSION(8)];
  static lv_color_t qrCanvasBuf[176 * 176];
  if (!qrcodegen_encodeText(payload, tempData, qrData, qrcodegen_Ecc_MEDIUM, 1, 8, qrcodegen_Mask_AUTO, true)) {
    return false;
  }

  const int qrSize = qrcodegen_getSize(qrData);
  const int quietZone = 2;
  const int modulePx = max(3, targetSize / (qrSize + quietZone * 2));
  const int fullSize = (qrSize + quietZone * 2) * modulePx;

  if (fullSize > 176) return false;

  int parentW = lv_obj_get_width(parent);
  int parentH = lv_obj_get_height(parent);
  int posX = x;
  int posY = y;
  if (parentW > fullSize) posX = (parentW - fullSize) / 2;
  if (parentH > fullSize) posY = (parentH - fullSize) / 2;

  lv_obj_t* frame = lv_obj_create(parent);
  lv_obj_remove_style_all(frame);
  lv_obj_set_size(frame, fullSize, fullSize);
  lv_obj_set_pos(frame, posX, posY);
  lv_obj_set_style_radius(frame, 0, 0);
  lv_obj_set_style_bg_opa(frame, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(frame, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_width(frame, 0, 0);
  lv_obj_clear_flag(frame, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_t* canvas = lv_canvas_create(frame);
  lv_canvas_set_buffer(canvas, qrCanvasBuf, fullSize, fullSize, LV_IMG_CF_TRUE_COLOR);
  lv_obj_set_pos(canvas, 0, 0);
  lv_obj_clear_flag(canvas, LV_OBJ_FLAG_CLICKABLE);
  lv_canvas_fill_bg(canvas, lv_color_hex(0xFFFFFF), LV_OPA_COVER);

  for (int yy = 0; yy < qrSize; ++yy) {
    for (int xx = 0; xx < qrSize; ++xx) {
      lv_color_t color = qrcodegen_getModule(qrData, xx, yy) ? lv_color_hex(0x101820) : lv_color_hex(0xFFFFFF);
      const int startX = (xx + quietZone) * modulePx;
      const int startY = (yy + quietZone) * modulePx;
      for (int py = 0; py < modulePx; ++py) {
        for (int px = 0; px < modulePx; ++px) {
          lv_canvas_set_px_color(canvas, startX + px, startY + py, color);
        }
      }
    }
  }

  return true;
}

static void addHeaderBackButton(lv_obj_t* scr, lv_event_cb_t cb) {
  addIconButton(scr, 58, 50, 44, LV_SYMBOL_LEFT, cb, NULL, false, 1, 1, LV_EVENT_CLICKED);
}

static void addHeaderActionButton(lv_obj_t* scr, const char* icon, lv_event_cb_t cb, bool active = false, uint8_t feedbackIdx = 255, uint8_t hapticEffect = 1) {
  addIconButton(scr, 258, 50, 44, icon, cb, NULL, active, feedbackIdx, hapticEffect, LV_EVENT_CLICKED);
}

static void renderWeightFace(lv_obj_t* scr, bool compactCards) {
  const char* name = currentDeviceName();
  addCenteredText(scr, name, UI_CX, 28, &lv_font_montserrat_20, C_TEXT);

  bool hasValue = lastValue[0] && strncmp(lastValue, "ERROR", 5) != 0;
  uint8_t brand = selectedNode >= 0 && selectedNode < nodeCount && nodes[selectedNode].configKnown ? nodes[selectedNode].brand : BRAND_AD;
  bool stable = hasValue && isValueStable(lastValue, brand);

  int gauge = gaugePermille(lastValue, selectedNode);
  addGaugeArc(scr, gauge, hasValue ? gaugeColorForPermille(gauge) : C_BLUE);

  const char* value = hasValue ? formatWeightValue(lastValue) : "---";
  lv_obj_t* mainVal = addCenteredText(scr, value, UI_CX - 16, 124, &lv_font_montserrat_40, C_TEXT);
  lv_obj_set_style_text_letter_space(mainVal, 0, 0);
  lv_obj_t* mainValShadow = addCenteredText(scr, value, UI_CX - 14, 124, &lv_font_montserrat_40, C_TEXT);
  lv_obj_set_style_text_letter_space(mainValShadow, 0, 0);
  lv_obj_set_style_text_opa(mainValShadow, LV_OPA_30, 0);
  if (hasValue) {
    lv_obj_t* unit = addText(scr, "g", 284, 145, &lv_font_montserrat_20, C_TEXT);
    lv_obj_update_layout(mainVal);
    lv_obj_set_x(unit, min(304, lv_obj_get_x(mainVal) + lv_obj_get_width(mainVal) + 7));
  }

  if (compactCards) {
    lv_obj_t* card = addRoundPanel(scr, 40, 80, 280, 104);
    addText(card, "Valeur actuelle", 18, 14, &lv_font_montserrat_14, C_MUTED);
    lv_obj_t* cv = addCenteredText(card, value, 140, 34, &lv_font_montserrat_40, stable ? C_GREEN : C_TEXT);
    lv_obj_set_style_text_letter_space(cv, 1, 0);
    lv_obj_t* cvShadow = addCenteredText(card, value, 142, 34, &lv_font_montserrat_40, stable ? C_GREEN : C_TEXT);
    lv_obj_set_style_text_letter_space(cvShadow, 1, 0);
    if (hasValue) {
      lv_obj_t* cu = addText(card, "g", 244, 56, &lv_font_montserrat_20, stable ? C_GREEN : C_TEXT);
      lv_obj_update_layout(cv);
      lv_obj_set_x(cu, min(252, lv_obj_get_x(cv) + lv_obj_get_width(cv) + 8));
    }
    if (historyCount > 0) {
      lv_obj_t* hist = addRoundPanel(scr, 40, 194, 280, 64);
      addText(hist, "Historique", 18, 8, &lv_font_montserrat_14, C_MUTED);
      addText(hist, "0s", 38, 34, &lv_font_montserrat_14, C_TEXT);
      addText(hist, "15s", 128, 34, &lv_font_montserrat_14, C_TEXT);
    }
  } else {
    if (selectedNode >= 0 && selectedNode < nodeCount && nodes[selectedNode].capacity > 0) {
      char cap[32];
      snprintf(cap, sizeof(cap), "%d%% / %.0f g", gauge / 10, nodes[selectedNode].capacity);
      addCenteredText(scr, cap, UI_CX, 226, &lv_font_montserrat_14, C_MUTED);
    }
  }

  addHeaderBackButton(scr, uiEvtBack);
  addPillButton(scr, 128, 286, 104, 42, "0", uiEvtZero, NULL, flashBtnIdx == 0, 0, 1, LV_EVENT_PRESSED);
}

static void uiRenderIdle(lv_obj_t* scr) {
  addGaugeArc(scr, 0, C_BLUE);
  addCenteredText(scr, "BDP Hub", UI_CX, 42, &lv_font_montserrat_20, C_TEXT);
  addCenteredText(scr, nodeCount ? currentDeviceName() : "Aucun appareil", UI_CX, 120, &lv_font_montserrat_24, C_TEXT);
  addCenteredText(scr, apSsid, UI_CX, 158, &lv_font_montserrat_14, C_MUTED);
  addCenteredText(scr, apIp, UI_CX, 180, &lv_font_montserrat_20, C_BLUE);
  addPillButton(scr, 60, 278, 240, 44, "Rechercher", uiEvtStartScan, NULL, flashBtnIdx == 2, 2, 1);
  addHeaderActionButton(scr, "QR", uiEvtOpenWifiQr, flashBtnIdx == 3, 3, 1);
}

static void uiRenderWifiQr(lv_obj_t* scr) {
  addHeaderBackButton(scr, uiEvtCloseWifiQr);

  lv_obj_t* card = lv_obj_create(scr);
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, 188, 188);
  lv_obj_set_pos(card, 86, 96);
  lv_obj_set_style_radius(card, 18, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_width(card, 0, 0);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_CLICKABLE);

  char qrPayload[96];
  buildWifiQrPayload(qrPayload, sizeof(qrPayload));
  if (!addWifiQrCode(card, qrPayload, 6, 6, 176)) {
    addCenteredText(card, "QR indisponible", 94, 76, &lv_font_montserrat_20, lv_color_hex(0x07101C));
  }

  addCenteredText(scr, apIp, UI_CX, 310, &lv_font_montserrat_14, C_MUTED);
}

static void uiRenderScanning(lv_obj_t* scr) {
  addCenteredText(scr, "Recherche...", UI_CX, 218, &lv_font_montserrat_20, C_TEXT);
  lv_obj_t* spinner = lv_arc_create(scr);
  lv_obj_set_size(spinner, 96, 96);
  lv_obj_set_pos(spinner, 132, 92);
  lv_obj_remove_style(spinner, NULL, LV_PART_KNOB);
  lv_obj_clear_flag(spinner, LV_OBJ_FLAG_CLICKABLE);
  lv_arc_set_bg_angles(spinner, 0, 360);
  lv_arc_set_angles(spinner, 0, 76);
  lv_arc_set_rotation(spinner, (millis() / 4) % 360);
  lv_obj_set_style_arc_width(spinner, 7, LV_PART_MAIN);
  lv_obj_set_style_arc_width(spinner, 7, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(spinner, lv_color_hex(0x0B223B), LV_PART_MAIN);
  lv_obj_set_style_arc_color(spinner, C_CYAN, LV_PART_INDICATOR);

  char foundStr[48];
  int found = activeNodeCount();
  snprintf(foundStr, sizeof(foundStr), "%d appareil%s detecte%s", found, found > 1 ? "s" : "", found > 1 ? "s" : "");
  addCenteredText(scr, foundStr, UI_CX, 248, &lv_font_montserrat_14, C_MUTED);

  unsigned long elapsed = millis() - scanStart;
  lv_obj_t* bar = lv_bar_create(scr);
  lv_obj_set_pos(bar, 62, 286);
  lv_obj_set_size(bar, 236, 8);
  lv_bar_set_range(bar, 0, SCAN_DURATION_MS);
  lv_bar_set_value(bar, min((int)elapsed, SCAN_DURATION_MS), LV_ANIM_OFF);
  lv_obj_set_style_radius(bar, 4, LV_PART_MAIN);
  lv_obj_set_style_radius(bar, 4, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(bar, lv_color_hex(0x102032), LV_PART_MAIN);
  lv_obj_set_style_bg_color(bar, C_BLUE, LV_PART_INDICATOR);
  addCenteredText(scr, "Recherche en cours", UI_CX, 306, &lv_font_montserrat_14, C_MUTED);
}

static void uiRenderList(lv_obj_t* scr) {
  addIconButton(scr, 58, 50, 44, "QR", uiEvtOpenWifiQr, NULL, flashBtnIdx == 3, 3, 1, LV_EVENT_CLICKED);
  addCenteredText(scr, "Appareils", UI_CX, 46, &lv_font_montserrat_24, C_TEXT);
  char sub[32];
  snprintf(sub, sizeof(sub), "%d/%d en ligne", activeNodeCount(), nodeCount);
  addCenteredText(scr, sub, UI_CX, 78, &lv_font_montserrat_14, C_MUTED);

  if (nodeCount == 0) {
    lv_obj_t* card = addRoundPanel(scr, 42, 124, 276, 82);
    addCenteredText(card, "Aucun appareil", 138, 20, &lv_font_montserrat_20, C_TEXT);
    addCenteredText(card, "Relance la recherche", 138, 50, &lv_font_montserrat_14, C_MUTED);
  } else {
    if (listScroll < 0) listScroll = 0;
    if (listScroll >= nodeCount) listScroll = nodeCount - 1;
    const int cardW = 216;
    const int cardH = 184;
    const int baseX = (DISPLAY_W - cardW) / 2;
    const int baseY = 112;
    const int slide = 220;
    float anim = listAnimProgress();

    auto drawNodeCard = [&](int idx, int dx, lv_opa_t opa) {
      if (idx < 0 || idx >= nodeCount) return;
      lv_obj_t* card = addRoundPanel(scr, baseX + dx, baseY, cardW, cardH);
      lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_add_event_cb(card, uiEvtSelectNode, LV_EVENT_CLICKED, (void*)(intptr_t)idx);
      lv_obj_add_event_cb(card, uiEvtIdentifyNode, LV_EVENT_LONG_PRESSED, (void*)(intptr_t)idx);
      lv_obj_set_style_radius(card, 22, 0);
      lv_obj_set_style_bg_color(card, lv_color_hex(0x123A68), 0);
      lv_obj_set_style_border_color(card, C_BLUE, 0);
      lv_obj_set_style_bg_opa(card, opa, 0);
      lv_obj_set_style_border_opa(card, opa, 0);

      lv_obj_t* dot = lv_obj_create(card);
      lv_obj_remove_style_all(dot);
      lv_obj_set_size(dot, 12, 12);
      lv_obj_set_style_radius(dot, 6, 0);
      lv_obj_set_style_bg_opa(dot, opa, 0);
      lv_obj_set_style_bg_color(dot, nodes[idx].active ? C_GREEN : C_MUTED, 0);
      lv_obj_set_pos(dot, cardW - 24, 14);

      lv_obj_t* identifier = addCenteredText(card, nodeDisplayName(idx), cardW / 2, 30, &lv_font_montserrat_24, C_TEXT);
      lv_obj_set_width(identifier, cardW - 24);
      lv_obj_set_style_text_opa(identifier, opa, 0);
      lv_label_set_long_mode(identifier, LV_LABEL_LONG_DOT);
      lv_obj_set_height(identifier, 54);

      if (nodeConfiguredName(idx)[0] && strcmp(nodeConfiguredName(idx), nodeDisplayName(idx)) != 0) {
        lv_obj_t* type = addCenteredText(card, nodeConfiguredName(idx), cardW / 2, 88, &lv_font_montserrat_20, C_TEXT);
        lv_obj_set_width(type, cardW - 24);
        lv_obj_set_style_text_opa(type, opa, 0);
        lv_label_set_long_mode(type, LV_LABEL_LONG_DOT);
      }

      if (nodes[idx].configKnown) {
        uint8_t b = nodes[idx].brand < 3 ? nodes[idx].brand : 0;
        lv_obj_t* brand = addCenteredText(card, BRAND_NAMES[b], cardW / 2, 128, &lv_font_montserrat_14, C_MUTED);
        lv_obj_set_style_text_opa(brand, opa, 0);
      }

    };

    if (listAnimFrom >= 0 && listAnimTo >= 0 && anim < 1.0f) {
      int fromDx = (int)(-listAnimDir * slide * anim);
      int toDx = (int)(listAnimDir * slide * (1.0f - anim));
      drawNodeCard(listAnimFrom, fromDx, (lv_opa_t)(LV_OPA_COVER * (1.0f - anim)));
      drawNodeCard(listAnimTo, toDx, (lv_opa_t)(LV_OPA_30 + (LV_OPA_COVER - LV_OPA_30) * anim));
      displayDirty = true;
    } else {
      listAnimFrom = -1;
      listAnimTo = -1;
      listAnimDir = 0;
      drawNodeCard(listScroll, 0, LV_OPA_COVER);
    }
  }
  addHeaderActionButton(scr, LV_SYMBOL_REFRESH, uiEvtStartScan, flashBtnIdx == 2, 2, 1);
}

static void uiRenderConnected(lv_obj_t* scr) {
  if (selectedNode < 0 || selectedNode >= nodeCount) {
    renderWeightFace(scr, false);
    return;
  }
  renderWeightFace(scr, false);
}

void renderDisplay() {
  if (!lcd_lvgl_lock(50)) return;
  lv_obj_t* scr = lv_scr_act();
  lv_obj_clean(scr);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(scr, C_DARK, 0);
  lv_obj_set_style_bg_grad_color(scr, C_DARK, 0);
  lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_NONE, 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  addBackdropTexture(scr);
  switch (uiState) {
    case STATE_IDLE:      uiRenderIdle(scr);      break;
    case STATE_SCANNING:  uiRenderScanning(scr);  break;
    case STATE_LIST:      uiRenderList(scr);      break;
    case STATE_CONNECTED: uiRenderConnected(scr); break;
    case STATE_WIFI_QR:   uiRenderWifiQr(scr);    break;
  }
  lcd_lvgl_unlock();
}

// =====================================================
// Touch Waveshare
// =====================================================
void handleTouch() {
  // Touch is handled by LVGL through the CST816 input driver in lcd_bsp.c.
}

void handleEncoder() {
  static int32_t lastStep = 0;
  static bool lastSw = true;
  static unsigned long lastBtnAt = 0;

  int32_t step = encRaw / 2;
  if (step != lastStep) {
    int dir = step > lastStep ? 1 : -1;
    lastStep = step;

    if (uiState == STATE_LIST && nodeCount > 0) {
      int prev = listScroll;
      listScroll += dir;
      if (listScroll < 0) listScroll = 0;
      if (listScroll >= nodeCount) listScroll = nodeCount - 1;
      if (listScroll != prev) startListAnimation(prev, listScroll, dir);
      displayDirty = true;
    }
  }

  bool sw = digitalRead(ENC_SW);
  if (!sw && lastSw && millis() - lastBtnAt > 220) {
    lastBtnAt = millis();
    if (uiState == STATE_IDLE) {
      uiState = STATE_LIST;
      displayDirty = true;
    } else if (uiState == STATE_LIST && nodeCount > 0) {
      selectNodeIndex(listScroll);
    } else if (uiState == STATE_CONNECTED) {
      selectedNode = -1;
      lastValue[0] = 0;
      lastStatus[0] = 0;
      uiState = STATE_LIST;
      displayDirty = true;
    } else if (uiState == STATE_LIST) {
      startScan();
    } else if (uiState == STATE_WIFI_QR) {
      uiState = STATE_LIST;
      displayDirty = true;
    }
  }
  lastSw = sw;
}

// =====================================================
// Setup / Loop
// =====================================================
void setup() {
  Serial.begin(115200);
  usbBootMuteUntil = millis() + USB_BOOT_MUTE_MS;
  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);
  pinMode(ENC_SW, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ENC_A), encISR_A, CHANGE);

  Touch_Init();
  hapticInit();
  audioInit();
  lcd_lvgl_Init();
  lcd_bl_pwm_bsp_init(LCD_PWM_MODE_255);

  uint32_t chip = (uint32_t)(ESP.getEfuseMac() >> 32);
  snprintf(apSsid, sizeof(apSsid), "%s%04X", AP_SSID_PREFIX, chip & 0xFFFF);
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.setAutoReconnect(false);
  WiFi.softAP(apSsid, AP_PASS, AP_CHANNEL);
  snprintf(apIp, sizeof(apIp), "%s", WiFi.softAPIP().toString().c_str());

  if (esp_now_init() != ESP_OK) {
    hubLogPrintln("@00 ESPNOW_INIT_FAIL");
    return;
  }

  esp_now_peer_info_t peer = {};
  memset(peer.peer_addr, 0xFF, 6);
  peer.channel = 0;
  peer.encrypt = false;
  esp_now_add_peer(&peer);

  esp_now_register_recv_cb(onRecv);
  esp_now_register_send_cb(onSent);

  webServer.on("/", HTTP_GET, handleRoot);
  webServer.on("/favicon.ico", HTTP_GET, handleFavicon);
  webServer.on("/api/nodes",    HTTP_GET,     handleApiNodes);
  webServer.on("/api/nodes",    HTTP_DELETE,  handleApiNodeDelete);
  webServer.on("/api/nodes",    HTTP_OPTIONS, handleApiOptions);
  webServer.on("/api/nodes/delete", HTTP_POST, handleApiNodeDelete);
  webServer.on("/api/nodes/delete", HTTP_OPTIONS, handleApiOptions);
  webServer.on("/api/scan", HTTP_POST, handleApiScanStart);
  webServer.on("/api/scan", HTTP_OPTIONS, handleApiOptions);
  webServer.on("/api/purge", HTTP_POST, handleApiPurge);
  webServer.on("/api/purge", HTTP_OPTIONS, handleApiOptions);
  webServer.on("/api/config",   HTTP_POST,    handleApiConfigSet);
  webServer.on("/api/config",   HTTP_OPTIONS, handleApiOptions);
  webServer.on("/api/balance-id", HTTP_POST, handleApiBalanceIdSet);
  webServer.on("/api/balance-id", HTTP_OPTIONS, handleApiOptions);
  webServer.on("/api/balance-id/read", HTTP_POST, handleApiBalanceIdRead);
  webServer.on("/api/balance-id/read", HTTP_OPTIONS, handleApiOptions);
  webServer.on("/api/profiles", HTTP_GET,     handleApiProfiles);
  webServer.on("/api/profiles", HTTP_POST,    handleApiProfilesSave);
  webServer.on("/api/profiles", HTTP_DELETE,  handleApiProfilesDelete);
  webServer.on("/api/profiles", HTTP_OPTIONS, handleApiOptions);
  webServer.on("/api/profiles/apply", HTTP_POST, handleApiProfilesApply);
  webServer.on("/api/profiles/apply", HTTP_OPTIONS, handleApiOptions);
  webServer.on("/api/settings", HTTP_GET,     handleApiSettings);
  webServer.on("/api/settings", HTTP_POST,    handleApiSettingsSet);
  webServer.on("/api/settings", HTTP_OPTIONS, handleApiOptions);
  webServer.onNotFound(handleNotFound);
  webServer.begin();

  loadSettings();
  loadNodes();
  loadProfiles();

  hubLogPrintf("@00 HUB_READY ap=%s ip=%s\n", apSsid, apIp);
  if (nodeCount > 0) {
    hubLogPrintf("@00 LOADED nodes=%d\n", nodeCount);
    startScan();
  }
  displayDirty = true;
}

void loop() {
  handleTouch();
  handleEncoder();
  webServer.handleClient();

  if (pendingConfigPushIdx >= 0 && pendingConfigPushIdx < nodeCount) {
    int idx = pendingConfigPushIdx;
    pendingConfigPushIdx = -1;
    sendConfigSetToNode(idx);
  } else if (pendingConfigPushIdx >= nodeCount) {
    pendingConfigPushIdx = -1;
  }

  if (flashBtnIdx >= 0 && millis() - flashBtnAt >= BTN_FLASH_MS) {
    flashBtnIdx = -1;
    displayDirty = true;
  }

  if (pendingBeep) {
    pendingBeep = false;
  }

  if (uiState == STATE_SCANNING) {
    if (millis() - lastDiscover >= DISCOVER_INTERVAL_MS) {
      lastDiscover = millis();
      sendDiscoverBroadcast();
    }
    if (millis() - scanStart >= SCAN_DURATION_MS) {
      int found = activeNodeCount();
      hubLogPrintf("@00 SCAN_END count=%d\n", found);
      saveNodes();
      uiState = STATE_LIST;
      displayDirty = true;
    } else {
      displayDirty = true;
    }
  }

  if (pending.kind != PEND_NONE && millis() - pending.sentAt > CMD_TIMEOUT_MS) {
    if (pendingToPc) Serial.printf("@%02u ERROR timeout\n", pending.nodeId);
    if (selectedNode >= 0 && nodes[selectedNode].id == pending.nodeId) {
      strncpy(lastValue, "ERROR timeout", sizeof(lastValue) - 1);
      lastValue[sizeof(lastValue) - 1] = 0;
      displayDirty = true;
    }
    clearPending();
  }

  static unsigned long lastActiveCheck = 0;
  if (millis() - lastActiveCheck > 5000) {
    lastActiveCheck = millis();
    for (int i = 0; i < nodeCount; i++) {
      if (nodes[i].active && nodes[i].lastSeen && millis() - nodes[i].lastSeen > OFFLINE_TIMEOUT_MS) {
        nodes[i].active = false;
        displayDirty = true;
      }
    }
  }

  if (uiState == STATE_CONNECTED) {
    if (lastValue[0] && millis() - lastValueTime >= VALUE_TTL_MS) {
      lastValue[0] = 0;
      displayDirty = true;
    }
    static unsigned long lastTimeRefresh = 0;
    if (historyCount > 0 && millis() - lastTimeRefresh > 1000) {
      lastTimeRefresh = millis();
      displayDirty = true;
    }
  }

  if (usbSerialMuted()) {
    while (Serial.available()) Serial.read();
  } else {
    while (Serial.available()) {
      processSerialByte((char)Serial.read());
    }
  }

  if (displayDirty && millis() - lastDisplayUpdate > DISPLAY_REFRESH_MS) {
    lastDisplayUpdate = millis();
    displayDirty = false;
    renderDisplay();
  }

  delay(1);
}
