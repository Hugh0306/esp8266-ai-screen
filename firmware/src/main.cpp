// ESP8266 WiFi clock: shows local time plus live Claude Code / Codex CLI
// working status and usage quota, polled from a small bridge service that
// runs on the developer's Mac (see ../bridge/bridge.py).
//
// Display: 240x240 SPI ST7789 (TFT_eSPI). Pin mapping is set via build_flags
// in platformio.ini - edit those if your wiring differs.

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <WiFiManager.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <AnimatedGIF.h>
#include <uzlib.h>
#include <time.h>

#include "config.h"
#include "admin_page.h"
#include "img/claude_sprite.h"
#include "img/codex_sprite.h"
#include "img/claude_logo.h"
#include "img/codex_logo.h"
#include "img/weather_icons.h"

TFT_eSPI tft = TFT_eSPI();
ESP8266WebServer webServer(80);

class BoundedStringStream : public Stream {
 public:
  BoundedStringStream(String &output, size_t limit, size_t reserveBytes)
      : output_(output), limit_(limit) {
    output_.reserve(min(limit_, reserveBytes) + 1);
  }

  size_t write(uint8_t value) override { return write(&value, 1); }

  size_t write(const uint8_t *buffer, size_t size) override {
    if (overflowed_ || size > limit_ - output_.length()) {
      overflowed_ = true;
      return 0;
    }
    if (!output_.concat(reinterpret_cast<const char *>(buffer), size)) {
      overflowed_ = true;
      return 0;
    }
    return size;
  }

  int available() override { return 0; }
  int availableForWrite() override {
    return overflowed_ || output_.length() >= limit_ ? 0 : (int)(limit_ - output_.length());
  }
  int read() override { return -1; }
  int peek() override { return -1; }
  bool outputCanTimeout() override { return false; }
  bool overflowed() const { return overflowed_; }

 private:
  String &output_;
  size_t limit_;
  bool overflowed_ = false;
};

// ISRG Root X1, trust anchor for the device-specific QWeather API host.
// Keeping the root in PROGMEM lets the device validate TLS without shipping a
// mutable certificate store in LittleFS.
const char QWEATHER_ROOT_CA[] PROGMEM = R"PEM(-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE-----)PEM";

// ---------- custom sprite storage (LittleFS) ----------
// Custom uploads replace the compiled-in default animation without needing a
// firmware rebuild. You POST a raw .gif straight to /sprite/claude or
// /sprite/codex (the device serves its own upload page at "/"); the ESP8266
// decodes and rescales the GIF *on-device* (AnimatedGIF, line-by-line so it
// never needs a full-canvas buffer) into the wire format below, which the
// display path then reads back frame-by-frame:
//   [1 byte frame count][frame0 bytes][frame1 bytes]...
// Each frame is exactly CLAUDE_SPRITE_W x H (or CODEX_SPRITE_W x H) RGB565
// pixels, byte order matching tools/convert_sprites.py's to_rgb565() so the
// compiled-in defaults and custom uploads share one draw path.
const char *CLAUDE_SPRITE_FILE = "/c.bin";
const char *CODEX_SPRITE_FILE = "/x.bin";
const char *CLAUDE_SPRITE_TMP_FILE = "/c.tmp";
const char *CODEX_SPRITE_TMP_FILE = "/x.tmp";
const char *CLAUDE_GIF_FILE = "/c.gif"; // raw upload, decoded then removed
const char *CODEX_GIF_FILE = "/x.gif";
const int MAX_CUSTOM_FRAMES = 8;
const size_t GIF_DECODER_MIN_FILE_BYTES = 1024;
const size_t MAX_GIF_UPLOAD_BYTES = 256 * 1024;
const int MAX_GIF_CANVAS_DIM = 480;
const size_t MAX_GIF_CANVAS_PIXELS = (size_t)MAX_GIF_CANVAS_DIM * MAX_GIF_CANVAS_DIM;
const size_t CLAUDE_FRAME_BYTES = (size_t)CLAUDE_SPRITE_W * CLAUDE_SPRITE_H * 2;
const size_t CODEX_FRAME_BYTES = (size_t)CODEX_SPRITE_W * CODEX_SPRITE_H * 2;

// We never hold a whole sprite frame in RAM. Decoding a GIF needs ~24KB of
// heap for AnimatedGIF's own buffers, which wouldn't fit alongside a static
// full-frame buffer (a 120x120 frame is ~28KB) on the ESP8266's ~80KB. So both
// the display path and the decoder work one screen-row at a time through these
// two small scratch rows (SCREEN_W is the widest we ever need).
uint16_t rowBuf[SCREEN_W];     // current row being drawn / decoded

bool claudeCustom = false;
int claudeCustomFrames = 0;
bool codexCustom = false;
int codexCustomFrames = 0;
uint32_t spriteRev = 0; // bumped on upload/reset so the Mac mirror re-fetches
bool fsAvailable = false;

const int SCREEN_CX = 120, SCREEN_CY = 120;
const int RING_MARGIN = 4;      // inset from screen edge
const int RING_THICKNESS = 10;  // ring bar thickness
const unsigned long ANIM_INTERVAL_MS = 120;  // sprite frame advance
const unsigned long FLASH_INTERVAL_MS = 400; // "urgent" flash speed
const unsigned long SWITCH_BOTH_MS = 2000;   // both apps working: alternate fast
const unsigned long SWITCH_IDLE_MS = 6000;   // neither working: alternate slow

enum ActiveApp { APP_CLAUDE, APP_CODEX };
ActiveApp currentApp = APP_CLAUDE;
unsigned long lastSwitchMs = 0;

// Display override, settable from the Mac app via POST /api/display:
// auto = follow working status, claude/codex = pin that app on screen,
// net/clock/stock = show telemetry pages instead of the pet.
enum DisplayMode { MODE_AUTO, MODE_CLAUDE, MODE_CODEX, MODE_NET, MODE_CLOCK, MODE_STOCK };
DisplayMode displayMode = MODE_STOCK;

DisplayMode lastEffectiveMode = MODE_AUTO;

// ---------- net speed mode state ----------
// Rendering is decoupled from the network: pollNet() fetches every 2s and
// only refills a queue of 250ms samples (the bridge samples at 4Hz and tags
// them with a running seq, so nothing is drawn twice or skipped). The sweep
// itself consumes exactly one queued sample every NET_DRAW_INTERVAL_MS, so
// the trace advances at a constant rate no matter how long HTTP takes.
const unsigned long NET_POLL_INTERVAL_MS = 2000; // queue refill cadence
const unsigned long NET_DRAW_INTERVAL_MS = 250;  // one chart step per bridge sample
const int NET_QUEUE = 32;
long netQRx[NET_QUEUE], netQTx[NET_QUEUE]; // ring buffer of pending samples
int netQHead = 0, netQCount = 0;
long netSeq = -1;                          // last bridge sample seq consumed into the queue
long netCurRx = 0, netCurTx = 0;           // smoothed readout for the header
int netCpuPct = -1, netMemPct = -1;        // Mac CPU/MEM row; -1 = bridge sends none (hidden)
String netLastCpuVal, netLastMemVal;       // change detection for the CPU/MEM values
bool netSysLabelsDrawn = false;
unsigned long lastNetPollMs = 0;
unsigned long lastNetDrawMs = 0;
bool netChromeDrawn = false;
bool netHeaderDirty = false;

// Chart layout (task-manager style scrolling area chart, newest at the right)
const int NET_CHART_X = 8, NET_CHART_Y = 60, NET_CHART_W = 224, NET_CHART_H = 128;
long netHistRx[NET_CHART_W], netHistTx[NET_CHART_W]; // one 250ms sample per column
long netScale = 10240;    // current "nice" full-scale value (whole chart shares it)
String netLastDl, netLastUl, netLastScaleText; // change detection for partial redraws

// ---------- weather clock mode state ----------
enum ClockTheme { CLOCK_CLASSIC, CLOCK_MINIMAL, CLOCK_DASHBOARD };
ClockTheme clockTheme = CLOCK_CLASSIC;
bool lunarEnabled = true;
bool nightEnabled = false;
int nightStartMin = NIGHT_START_DEFAULT_MIN;
int nightEndMin = NIGHT_END_DEFAULT_MIN;
int nightBrightness = NIGHT_BRIGHTNESS_DEFAULT;
bool screenScheduleEnabled = SCREEN_SCHEDULE_ENABLED_DEFAULT;
int screenOffStartMin = SCREEN_OFF_START_DEFAULT_MIN;
int screenOffEndMin = SCREEN_OFF_END_DEFAULT_MIN;
String ntpServer = NTP_PRIMARY_DEFAULT;
String legacyCaiyunToken;
String qweatherApiKey;
String qweatherApiHost;
int8_t qweatherMflnState = -1;
bool wifiWasConnected = false;
uint32_t minQWeatherTlsHeap = 0;
uint32_t minQWeatherTlsBlock = 0;
bool manualScreenOff = false;
bool nightActive = false;
bool scheduledScreenOff = false;
bool screenScheduleWakeOverride = false;
bool ntpStarted = false;

enum FeedSource { FEED_NONE, FEED_CACHE, FEED_BRIDGE, FEED_DIRECT };

const char *feedSourceName(FeedSource source) {
  if (source == FEED_CACHE) return "cache";
  if (source == FEED_BRIDGE) return "bridge";
  if (source == FEED_DIRECT) return "direct";
  return "none";
}

struct WeatherState {
  bool available = false;
  String location = WEATHER_LOCATION_NAME;
  String conditionZh;
  String conditionEn;
  String skycon;
  String airQualityZh;
  String provider = "none";
  String lunarZh;
  float temperatureC = NAN;
  float apparentC = NAN;
  float highC = NAN;
  float lowC = NAN;
  float windKmh = NAN;
  int humidityPct = -1;
  int weatherCode = -1;
  int aqi = -1;
  long updatedAt = 0;
  bool stale = true;
  int textRev = -1;
  FeedSource source = FEED_NONE;
};
WeatherState weather;
WeatherState directWeatherCandidate;
enum DirectWeatherStage { QWEATHER_NOW, QWEATHER_FORECAST, QWEATHER_AIR };
DirectWeatherStage directWeatherStage = QWEATHER_NOW;
unsigned long directWeatherCandidateStartedMs = 0;
const int WEATHER_TEXT_W = 232, WEATHER_TEXT_H = 24;
const int WEATHER_TEXT_X = 4, WEATHER_TEXT_Y = 210;
const size_t WEATHER_TEXT_BYTES = (size_t)WEATHER_TEXT_W * WEATHER_TEXT_H * 2;
const size_t WEATHER_TEXT_CACHE_HEADER_BYTES = sizeof(uint32_t) * 4;
const uint32_t WEATHER_TEXT_CACHE_MAGIC = 0x574C3031UL; // WL01
const char *WEATHER_TEXT_CACHE_FILE = "/weather-text.bin";
const char *WEATHER_TEXT_CACHE_TMP_FILE = "/weather-text.tmp";
const char *WEATHER_TEXT_CACHE_BAK_FILE = "/weather-text.bak";
bool clockChromeDrawn = false;
bool clockDirty = true;
int weatherTextDrawnRev = -2;
int weatherTextCachedRev = -2;
uint32_t weatherTextCachedDay = 0;
bool weatherTextBitmapDrawn = false;
bool weatherTextNeedsFetch = true;
bool weatherLastDrawnStale = true;
unsigned long lastWeatherTextAttemptMs = 0;
int lastClockSecond = -1;
unsigned long lastWeatherPollMs = 0;
unsigned long lastTimedDisplayCheckMs = 0;
unsigned long lastBridgeWeatherAttemptMs = 0;
unsigned long lastDirectWeatherAttemptMs = 0;
unsigned long directWeatherRetryMs = 0;
bool bridgeWeatherHealthy = false;
bool bridgeWeatherReportedStale = false;
bool weatherDirectFallbackActive = false;
unsigned long lastWeatherSuccessMs = 0;
// ---------- stock watchlist mode state ----------
// Bridge rows are pre-formatted. When the bridge is unavailable, the firmware
// refreshes the same rows from public HTTP feeds and keeps the last good values.
const unsigned long STOCK_POLL_INTERVAL_MS = 5000;
const int MAX_STOCKS = 4;
struct StockRow {
  String symbol, code, price, pct;
  int up = 0; // 1 rising (green) / -1 falling (red) / 0 flat
};
StockRow stocks[MAX_STOCKS];
int stockCount = 0;
String stockSymbols[MAX_STOCKS];
int stockSymbolCount = 0;
const char *DEFAULT_STOCK_SYMBOLS[MAX_STOCKS] = {"usQQQ", "BTCUSDT", "ETHUSDT", "ETHBTC"};
bool stockConfigSyncPending = false;
FeedSource stockSource = FEED_NONE;
long stockUpdatedAt = 0;
bool stockEverLoaded = false;
bool stockDirty = false;
bool stockChromeDrawn = false;
String stockLastCode[MAX_STOCKS]; // top line (code + CJK name strip)
String stockLastVal[MAX_STOCKS];  // value line (price + pct)
unsigned long lastStockPollMs = 0;
unsigned long lastBridgeStockAttemptMs = 0;
unsigned long lastDirectTencentAttemptMs = 0;
unsigned long lastDirectCryptoAttemptMs = 0;
unsigned long directTencentRetryMs = 0;
unsigned long directCryptoRetryMs = 0;
bool bridgeStockHealthy = false;
bool bridgeStockReportedStale = false;
bool stockDirectFallbackActive = false;
unsigned long lastStockSuccessMs = 0;
unsigned long lastDirectTencentSuccessMs = 0;
unsigned long lastDirectCryptoSuccessMs = 0;
long directTencentUpdatedAt = 0;
long directCryptoUpdatedAt = 0;
// CJK names come as Mac-rendered RGB565 strips (GET /stock/names.raw, one
// 156x16 strip per row) - names_rev says when to re-fetch. -1 = not drawn.
const int STOCK_NAME_W = 156, STOCK_NAME_H = 16;
int stockNamesRev = -1;
int stockNamesDrawnRev = -1;
String stockLastFooter;
bool stockLastDrawnStale = true;
unsigned long lastStockFreshnessCheckMs = 0;
unsigned long lastStockNamesAttemptMs = 0;
unsigned long lastStockConfigSyncAttemptMs = 0;
unsigned long lastSerialStockConfigMs = 0;

bool offlineStateDirty = false;
bool offlineStateExists = false;
bool lastOfflineStateSaveFailed = false;
unsigned long lastOfflineStateSaveMs = 0;

int claudeFrame = 0;
int codexFrame = 0;
unsigned long lastAnimMs = 0;

bool flashOn = true;
unsigned long lastFlashMs = 0;

// Bridge host is not asked for during first-time WiFi setup: the Mac/Windows
// bridge discovers the device and pairs automatically (or set via /api/bridge).
String bridgeHost;

struct ClaudeStatus {
  String status = "unknown";
  long tokensToday = 0;
  int sessionMin = 0;
  int sessionWindowMin = 300;
  float fiveHourPct = -1; // real OAuth quota from the bridge, -1 = unknown
  int fiveHourResetMin = -1; // minutes until the 5h window resets
  float sevenDayPct = -1;
  int sevenDayResetMin = -1; // minutes until the 7-day window resets
  bool needsInput = false; // waiting on a permission/approval prompt
};

struct CodexStatus {
  String status = "unknown";
  long tokensToday = 0;
  float primaryPct = -1;
  int primaryResetMin = -1;
  float weeklyPct = -1;
  int weeklyResetMin = -1;
  bool needsInput = false;
};

ClaudeStatus claudeStatus;
CodexStatus codexStatus;

unsigned long lastPollMs = 0;
unsigned long lastSuccessMs = 0;
bool everPolled = false;
unsigned long lastBridgeAnySuccessMs = 0;
bool bridgeEverReachable = false;
unsigned long lastNetworkJobMs = 0;
uint32_t minFreeHeap = UINT32_MAX;
bool mainUiShown = false;      // false while the config-portal screen is up
bool webServerStarted = false; // deferred: port 80 clashes with the portal
bool webBridgeRequestMade = false;

// ---------- backlight brightness ----------
// The panel backlight (TFT_BL, active LOW) is PWM-dimmable — the vendor's own
// firmware does the same. 0 = off, 100 = full. Persisted so it survives reboot.

int brightness = BRIGHTNESS_DEFAULT; // 0-100

const char *clockThemeName(ClockTheme theme) {
  if (theme == CLOCK_MINIMAL) return "minimal";
  if (theme == CLOCK_DASHBOARD) return "dashboard";
  return "classic";
}

bool setClockTheme(const String &name) {
  if (name == "classic") clockTheme = CLOCK_CLASSIC;
  else if (name == "minimal") clockTheme = CLOCK_MINIMAL;
  else if (name == "dashboard") clockTheme = CLOCK_DASHBOARD;
  else return false;
  clockChromeDrawn = false;
  clockDirty = true;
  return true;
}

const char *displayModeName(DisplayMode mode) {
  if (mode == MODE_CLAUDE) return "claude";
  if (mode == MODE_CODEX) return "codex";
  if (mode == MODE_NET) return "net";
  if (mode == MODE_CLOCK) return "weather";
  if (mode == MODE_STOCK) return "stock";
  return "auto";
}

bool setDisplayMode(const String &name) {
  if (name == "auto") displayMode = MODE_AUTO;
  else if (name == "claude") displayMode = MODE_CLAUDE;
  else if (name == "codex") displayMode = MODE_CODEX;
  else if (name == "net") displayMode = MODE_NET;
  else if (name == "weather" || name == "clock" || name == "music") displayMode = MODE_CLOCK;
  else if (name == "stock") displayMode = MODE_STOCK;
  else return false;
  return true;
}

constexpr bool minuteInWindow(int minute, int startMinute, int endMinute) {
  return startMinute == endMinute
             ? false
             : (startMinute < endMinute ? (minute >= startMinute && minute < endMinute)
                                        : (minute >= startMinute || minute < endMinute));
}

static_assert(minuteInWindow(0, 0, 420), "00:00 must be inside the default off window");
static_assert(!minuteInWindow(420, 0, 420), "07:00 must end the default off window");
static_assert(minuteInWindow(1380, 1320, 420), "cross-midnight windows must include late hours");
static_assert(minuteInWindow(60, 1320, 420), "cross-midnight windows must include early hours");
static_assert(!minuteInWindow(600, 600, 600), "equal endpoints are a zero-length window");

bool screenIsOn() {
  return !manualScreenOff && (!scheduledScreenOff || screenScheduleWakeOverride);
}

int effectiveBrightness() {
  if (!screenIsOn()) return 0;
  if (nightActive) return min(brightness, nightBrightness);
  return brightness;
}

void applyBrightness() {
  // analogWriteRange(100) is set in setup(), so the duty value is just the
  // inverted percentage (active LOW: 0 duty = always LOW = full on).
  analogWrite(TFT_BL, 100 - effectiveBrightness());
}

void loadBrightness() {
  if (!fsAvailable) return;
  if (!LittleFS.exists(BRIGHTNESS_FILE)) return;
  File f = LittleFS.open(BRIGHTNESS_FILE, "r");
  if (!f) return;
  int v = f.readStringUntil('\n').toInt();
  f.close();
  // Legacy firmware persisted level 0 as "off". Screen-off is temporary now,
  // so ignore that old value and always boot with a discoverable lit display.
  if (v >= 1 && v <= 100) brightness = v;
}

void saveBrightness() {
  if (!fsAvailable) return;
  File f = LittleFS.open(BRIGHTNESS_FILE, "w");
  if (!f) return;
  f.println(brightness);
  f.close();
}

bool timeIsValid() { return time(nullptr) > 1700000000; }

bool validWeatherCredential(const String &value, size_t maxLength) {
  if (value.length() < 8 || value.length() > maxLength) return false;
  for (size_t i = 0; i < value.length(); i++) {
    char ch = value[i];
    if (!isalnum(static_cast<unsigned char>(ch)) && ch != '-' && ch != '_') return false;
  }
  return true;
}

bool validQWeatherApiKey(const String &value) {
  return validWeatherCredential(value, QWEATHER_API_KEY_MAX_LENGTH);
}

bool validLegacyCaiyunToken(const String &value) {
  return validWeatherCredential(value, LEGACY_CAIYUN_TOKEN_MAX_LENGTH);
}

bool validQWeatherApiHost(const String &value) {
  if (value.length() == 0 || value.length() > QWEATHER_API_HOST_MAX_LENGTH ||
      !value.endsWith(".re.qweatherapi.com") || value.indexOf("..") >= 0) return false;
  int labelStart = 0;
  for (size_t i = 0; i <= value.length(); i++) {
    char ch = i < value.length() ? value[i] : '.';
    if (ch == '.') {
      int labelLength = (int)i - labelStart;
      if (labelLength < 1 || labelLength > 63 || value[labelStart] == '-' || value[i - 1] == '-') return false;
      labelStart = i + 1;
    } else if (!(ch >= 'a' && ch <= 'z') && !(ch >= '0' && ch <= '9') && ch != '-') {
      return false;
    }
  }
  return true;
}

bool qweatherConfigured() {
  return validQWeatherApiKey(qweatherApiKey) && validQWeatherApiHost(qweatherApiHost);
}

void wakeScreenTemporarily() {
  manualScreenOff = false;
  screenScheduleWakeOverride = scheduledScreenOff;
}

void updateTimedDisplayState(bool force = false) {
  bool nextNightActive = false;
  bool nextScheduledScreenOff = false;
  if (timeIsValid()) {
    time_t now = time(nullptr);
    struct tm local;
    localtime_r(&now, &local);
    int minute = local.tm_hour * 60 + local.tm_min;
    nextNightActive = nightEnabled && minuteInWindow(minute, nightStartMin, nightEndMin);
    nextScheduledScreenOff = screenScheduleEnabled &&
                             minuteInWindow(minute, screenOffStartMin, screenOffEndMin);
  }

  bool scheduleBoundary = nextScheduledScreenOff != scheduledScreenOff;
  if (scheduleBoundary) screenScheduleWakeOverride = false;
  bool changed = nextNightActive != nightActive || scheduleBoundary;
  nightActive = nextNightActive;
  scheduledScreenOff = nextScheduledScreenOff;
  if (force || changed) {
    applyBrightness();
  }
}

void startNtp() {
  configTime(CLOCK_TZ, ntpServer.c_str(), NTP_SECONDARY, NTP_FALLBACK);
  ntpStarted = true;
  Serial.printf("[ntp] sync requested via %s\n", ntpServer.c_str());
}

bool validPersistedJson(const char *path, bool requireSymbols) {
  if (!fsAvailable || !LittleFS.exists(path)) return false;
  File file = LittleFS.open(path, "r");
  if (!file || file.size() == 0 || file.size() > DIRECT_RESPONSE_MAX_BYTES) {
    if (file) file.close();
    return false;
  }
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  return !error && (doc["schema"] | 0) == 1 && (!requireSymbols || doc["symbols"].is<JsonArray>());
}

bool promoteVerifiedJson(const char *tmpPath, const char *currentPath, const char *backupPath,
                         bool requireSymbols) {
  if (!validPersistedJson(tmpPath, requireSymbols)) return false;

  bool currentExists = LittleFS.exists(currentPath);
  bool currentValid = currentExists && validPersistedJson(currentPath, requireSymbols);
  if (currentExists && !currentValid) {
    if (!LittleFS.remove(currentPath)) return false;
    currentExists = false;
  }

  if (!currentExists) {
    // A backup may be the only last-good copy. Keep it until the new current
    // has been promoted and parsed successfully.
    if (!LittleFS.rename(tmpPath, currentPath)) return false;
    if (!validPersistedJson(currentPath, requireSymbols)) {
      LittleFS.remove(currentPath);
      return false;
    }
    return true;
  }

  // The current file is valid, so it is safe to retire an older backup before
  // rotating current -> backup. A failed promotion always leaves one valid copy.
  if (LittleFS.exists(backupPath) && !LittleFS.remove(backupPath)) return false;
  if (!LittleFS.rename(currentPath, backupPath)) return false;
  if (!LittleFS.rename(tmpPath, currentPath)) {
    LittleFS.rename(backupPath, currentPath);
    return false;
  }
  if (!validPersistedJson(currentPath, requireSymbols)) {
    LittleFS.remove(currentPath);
    LittleFS.rename(backupPath, currentPath);
    return false;
  }
  return true;
}

bool saveDeviceSettings() {
  if (!fsAvailable) return false;
  JsonDocument doc;
  doc["schema"] = 1;
  doc["display_mode"] = displayModeName(displayMode);
  doc["brightness"] = brightness;
  doc["clock_theme"] = clockThemeName(clockTheme);
  doc["lunar_enabled"] = lunarEnabled;
  doc["night_enabled"] = nightEnabled;
  doc["night_start_min"] = nightStartMin;
  doc["night_end_min"] = nightEndMin;
  doc["night_brightness"] = nightBrightness;
  doc["screen_schedule_enabled"] = screenScheduleEnabled;
  doc["screen_off_start_min"] = screenOffStartMin;
  doc["screen_off_end_min"] = screenOffEndMin;
  doc["ntp_server"] = ntpServer;
  if (validLegacyCaiyunToken(legacyCaiyunToken)) doc["caiyun_token"] = legacyCaiyunToken;
  if (qweatherConfigured()) {
    doc["qweather_key"] = qweatherApiKey;
    doc["qweather_host"] = qweatherApiHost;
  }

  File f = LittleFS.open(DEVICE_SETTINGS_TMP_FILE, "w");
  if (!f) return false;
  size_t expected = measureJson(doc);
  size_t written = serializeJson(doc, f);
  f.flush();
  size_t fileSize = f.size();
  if (written != expected || fileSize != expected || fileSize > DIRECT_RESPONSE_MAX_BYTES) {
    f.close();
    LittleFS.remove(DEVICE_SETTINGS_TMP_FILE);
    return false;
  }
  f.close();
  return promoteVerifiedJson(DEVICE_SETTINGS_TMP_FILE, DEVICE_SETTINGS_FILE,
                             DEVICE_SETTINGS_BAK_FILE, false);
}

enum SettingsCredentialState {
  CREDENTIAL_FILE_MISSING,
  CREDENTIAL_FILE_MATCH,
  CREDENTIAL_FILE_DIFFERENT,
  CREDENTIAL_FILE_UNREADABLE
};

SettingsCredentialState settingsFileQWeatherState(const char *path, const String &expectedKey,
                                                   const String &expectedHost) {
  if (!LittleFS.exists(path)) return CREDENTIAL_FILE_MISSING;
  File file = LittleFS.open(path, "r");
  if (!file || file.size() == 0 || file.size() > DIRECT_RESPONSE_MAX_BYTES) {
    if (file) file.close();
    return CREDENTIAL_FILE_UNREADABLE;
  }
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  if (error || (doc["schema"] | 0) != 1) return CREDENTIAL_FILE_UNREADABLE;
  String persistedKey = doc["qweather_key"] | "";
  String persistedHost = doc["qweather_host"] | "";
  persistedKey.trim();
  persistedHost.trim();
  persistedHost.toLowerCase();
  return persistedKey == expectedKey && persistedHost == expectedHost
             ? CREDENTIAL_FILE_MATCH
             : CREDENTIAL_FILE_DIFFERENT;
}

bool synchronizeQWeatherSettingsCopies() {
  if (saveDeviceSettings()) return true;

  SettingsCredentialState current = settingsFileQWeatherState(
      DEVICE_SETTINGS_FILE, qweatherApiKey, qweatherApiHost);
  SettingsCredentialState backup = settingsFileQWeatherState(
      DEVICE_SETTINGS_BAK_FILE, qweatherApiKey, qweatherApiHost);
  if (current == CREDENTIAL_FILE_MATCH) {
    if (backup == CREDENTIAL_FILE_DIFFERENT && !LittleFS.remove(DEVICE_SETTINGS_BAK_FILE)) return false;
    return backup != CREDENTIAL_FILE_UNREADABLE;
  }
  if (backup == CREDENTIAL_FILE_MATCH) {
    if (current == CREDENTIAL_FILE_DIFFERENT && !LittleFS.remove(DEVICE_SETTINGS_FILE)) return false;
    return current != CREDENTIAL_FILE_UNREADABLE;
  }
  return false;
}

void loadDeviceSettings() {
  const char *path = validPersistedJson(DEVICE_SETTINGS_FILE, false)
                         ? DEVICE_SETTINGS_FILE
                         : (validPersistedJson(DEVICE_SETTINGS_BAK_FILE, false)
                                ? DEVICE_SETTINGS_BAK_FILE
                                : nullptr);
  if (!path) return;
  File f = LittleFS.open(path, "r");
  if (!f) return;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return;

  setDisplayMode(String((const char *)(doc["display_mode"] | "stock")));
  brightness = constrain(doc["brightness"] | brightness, 1, 100);
  setClockTheme(String((const char *)(doc["clock_theme"] | "classic")));
  lunarEnabled = doc["lunar_enabled"] | true;
  nightEnabled = doc["night_enabled"] | false;
  nightStartMin = constrain(doc["night_start_min"] | NIGHT_START_DEFAULT_MIN, 0, 1439);
  nightEndMin = constrain(doc["night_end_min"] | NIGHT_END_DEFAULT_MIN, 0, 1439);
  nightBrightness = constrain(doc["night_brightness"] | NIGHT_BRIGHTNESS_DEFAULT, 1, 50);
  screenScheduleEnabled = doc["screen_schedule_enabled"] | SCREEN_SCHEDULE_ENABLED_DEFAULT;
  screenOffStartMin = constrain(doc["screen_off_start_min"] | SCREEN_OFF_START_DEFAULT_MIN, 0, 1439);
  screenOffEndMin = constrain(doc["screen_off_end_min"] | SCREEN_OFF_END_DEFAULT_MIN, 0, 1439);
  String configuredNtp = doc["ntp_server"] | NTP_PRIMARY_DEFAULT;
  configuredNtp.trim();
  if (configuredNtp.length() > 0 && configuredNtp.length() <= 63) ntpServer = configuredNtp;
  String configuredCaiyun = doc["caiyun_token"] | "";
  configuredCaiyun.trim();
  if (validLegacyCaiyunToken(configuredCaiyun)) legacyCaiyunToken = configuredCaiyun;
  String configuredQWeatherKey = doc["qweather_key"] | "";
  String configuredQWeatherHost = doc["qweather_host"] | "";
  configuredQWeatherKey.trim();
  configuredQWeatherHost.trim();
  configuredQWeatherHost.toLowerCase();
  if (validQWeatherApiKey(configuredQWeatherKey) && validQWeatherApiHost(configuredQWeatherHost)) {
    qweatherApiKey = configuredQWeatherKey;
    qweatherApiHost = configuredQWeatherHost;
  }
}

// ---------- persistence for the bridge host ----------

bool readBridgeHostFile(const char *path, String &value) {
  value = "";
  if (!fsAvailable || !LittleFS.exists(path)) return false;
  File file = LittleFS.open(path, "r");
  if (!file || file.size() == 0 || file.size() > 128) {
    if (file) file.close();
    return false;
  }
  value = file.readStringUntil('\n');
  file.close();
  value.trim();
  return value.length() > 0 && value.length() <= 127;
}

void loadBridgeHost() {
  if (!readBridgeHostFile(WIFI_CONFIG_FILE, bridgeHost)) {
    readBridgeHostFile(WIFI_CONFIG_BAK_FILE, bridgeHost);
  }
}

bool saveBridgeHost(const String &host) {
  if (!fsAvailable || host.length() == 0 || host.length() > 127 || host.indexOf('\n') >= 0 ||
      host.indexOf('\r') >= 0) return false;
  File file = LittleFS.open(WIFI_CONFIG_TMP_FILE, "w");
  if (!file) return false;
  size_t written = file.print(host);
  written += file.write('\n');
  file.flush();
  size_t fileSize = file.size();
  file.close();
  String verified;
  if (written != host.length() + 1 || fileSize != written ||
      !readBridgeHostFile(WIFI_CONFIG_TMP_FILE, verified) || verified != host) {
    LittleFS.remove(WIFI_CONFIG_TMP_FILE);
    return false;
  }

  bool currentExists = LittleFS.exists(WIFI_CONFIG_FILE);
  String currentValue;
  bool currentValid = currentExists && readBridgeHostFile(WIFI_CONFIG_FILE, currentValue);
  if (currentExists && !currentValid) {
    if (!LittleFS.remove(WIFI_CONFIG_FILE)) return false;
    currentExists = false;
  }
  if (!currentExists) {
    if (!LittleFS.rename(WIFI_CONFIG_TMP_FILE, WIFI_CONFIG_FILE)) return false;
    if (!readBridgeHostFile(WIFI_CONFIG_FILE, verified) || verified != host) {
      LittleFS.remove(WIFI_CONFIG_FILE);
      return false;
    }
    return true;
  }
  if (LittleFS.exists(WIFI_CONFIG_BAK_FILE) && !LittleFS.remove(WIFI_CONFIG_BAK_FILE)) return false;
  if (!LittleFS.rename(WIFI_CONFIG_FILE, WIFI_CONFIG_BAK_FILE)) return false;
  if (!LittleFS.rename(WIFI_CONFIG_TMP_FILE, WIFI_CONFIG_FILE)) {
    LittleFS.rename(WIFI_CONFIG_BAK_FILE, WIFI_CONFIG_FILE);
    return false;
  }
  if (!readBridgeHostFile(WIFI_CONFIG_FILE, verified) || verified != host) {
    LittleFS.remove(WIFI_CONFIG_FILE);
    LittleFS.rename(WIFI_CONFIG_BAK_FILE, WIFI_CONFIG_FILE);
    return false;
  }
  return true;
}

// ---------- offline stock/weather state ----------

bool isCryptoSymbol(const String &symbol) {
  return symbol == "BTCUSDT" || symbol == "ETHUSDT" || symbol == "ETHBTC";
}

String normalizeStockSymbol(String raw) {
  raw.trim();
  if (raw.length() == 0) return "";
  raw.toUpperCase();
  String compactCrypto = raw;
  compactCrypto.replace("-", "");
  if (isCryptoSymbol(compactCrypto)) return compactCrypto;
  if (isCryptoSymbol(raw)) return raw;

  String prefix;
  String code = raw;
  if (raw.length() > 2) {
    prefix = raw.substring(0, 2);
    prefix.toLowerCase();
    if (prefix == "sh" || prefix == "sz" || prefix == "bj" || prefix == "hk" || prefix == "us") {
      code = raw.substring(2);
    } else {
      prefix = "us";
    }
  } else {
    prefix = "us";
  }
  if (code.length() == 0 || code.length() > 16) return "";
  for (size_t i = 0; i < code.length(); i++) {
    char c = code.charAt(i);
    if (!isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '-') return "";
  }
  return prefix + code;
}

String stockDisplayCode(const String &symbol) {
  if (isCryptoSymbol(symbol)) return symbol;
  if (symbol.length() > 2) return symbol.substring(2);
  return symbol;
}

bool stockSymbolListsEqual(const String *a, int aCount, const String *b, int bCount) {
  if (aCount != bCount) return false;
  for (int i = 0; i < aCount; i++) {
    if (!a[i].equalsIgnoreCase(b[i])) return false;
  }
  return true;
}

int stockRowIndex(const String &symbol) {
  for (int i = 0; i < stockCount; i++) {
    if (stocks[i].symbol.equalsIgnoreCase(symbol)) return i;
  }
  return -1;
}

void applyStockSymbolList(const String *symbols, int count) {
  StockRow previous[MAX_STOCKS];
  int previousCount = stockCount;
  for (int i = 0; i < previousCount; i++) previous[i] = stocks[i];

  stockSymbolCount = constrain(count, 0, MAX_STOCKS);
  stockCount = stockSymbolCount;
  for (int i = 0; i < stockSymbolCount; i++) {
    stockSymbols[i] = symbols[i];
    int found = -1;
    for (int j = 0; j < previousCount; j++) {
      if (previous[j].symbol.equalsIgnoreCase(symbols[i])) {
        found = j;
        break;
      }
    }
    if (found >= 0) {
      stocks[i] = previous[found];
    } else {
      stocks[i].symbol = symbols[i];
      stocks[i].code = stockDisplayCode(symbols[i]);
      stocks[i].price = "--";
      stocks[i].pct = "--";
      stocks[i].up = 0;
    }
  }
  for (int i = stockSymbolCount; i < MAX_STOCKS; i++) {
    stockSymbols[i] = "";
    stocks[i] = StockRow();
  }
  stockEverLoaded = stockCount > 0;
  stockNamesRev = -1;
  stockNamesDrawnRev = -1;
  stockDirty = true;
}

void loadDefaultStockSymbols() {
  String defaults[MAX_STOCKS];
  for (int i = 0; i < MAX_STOCKS; i++) defaults[i] = DEFAULT_STOCK_SYMBOLS[i];
  applyStockSymbolList(defaults, MAX_STOCKS);
}

long feedAgeSeconds(long updatedAt) {
  if (updatedAt <= 0 || !timeIsValid()) return -1;
  long age = (long)time(nullptr) - updatedAt;
  return age < 0 ? 0 : age;
}

bool intervalElapsed(unsigned long nowMs, unsigned long lastMs, unsigned long intervalMs) {
  return lastMs == 0 || (unsigned long)(nowMs - lastMs) >= intervalMs;
}

void markBridgeReachable() {
  bridgeEverReachable = true;
  lastBridgeAnySuccessMs = millis();
}

bool bridgeRecentlyReachable(unsigned long nowMs) {
  return bridgeEverReachable && (unsigned long)(nowMs - lastBridgeAnySuccessMs) < BRIDGE_RETRY_INTERVAL_MS;
}

bool directFallbackAllowed(unsigned long nowMs) {
  if (bridgeRecentlyReachable(nowMs)) return false;
  if (bridgeEverReachable) return true;
  return nowMs >= BRIDGE_RETRY_INTERVAL_MS;
}

bool stockDataStale() {
  if (stockSource == FEED_NONE || stockSource == FEED_CACHE) return true;
  if (stockSource == FEED_DIRECT) {
    bool needsTencent = false, needsCrypto = false;
    for (int i = 0; i < stockSymbolCount; i++) {
      if (isCryptoSymbol(stockSymbols[i])) needsCrypto = true;
      else needsTencent = true;
    }
    if (needsTencent &&
        (lastDirectTencentSuccessMs == 0 ||
         (unsigned long)(millis() - lastDirectTencentSuccessMs) > STOCK_STALE_AFTER_S * 1000UL)) return true;
    if (needsCrypto &&
        (lastDirectCryptoSuccessMs == 0 ||
         (unsigned long)(millis() - lastDirectCryptoSuccessMs) > STOCK_STALE_AFTER_S * 1000UL)) return true;
  }
  long age = feedAgeSeconds(stockUpdatedAt);
  if (age >= 0) return age > STOCK_STALE_AFTER_S;
  return lastStockSuccessMs == 0 || (unsigned long)(millis() - lastStockSuccessMs) > STOCK_STALE_AFTER_S * 1000UL;
}

bool weatherDataStale() {
  if (!weather.available || weather.source == FEED_NONE || weather.source == FEED_CACHE) return true;
  if (weather.stale) return true;
  long age = feedAgeSeconds(weather.updatedAt);
  if (age >= 0) return age > WEATHER_STALE_AFTER_S;
  return lastWeatherSuccessMs == 0 ||
         (unsigned long)(millis() - lastWeatherSuccessMs) > WEATHER_STALE_AFTER_S * 1000UL;
}

bool readHttpTextBounded(HTTPClient &http, size_t maxBytes, String &payload) {
  payload = "";
  int contentLength = http.getSize();
  if (contentLength > (int)maxBytes) {
    Serial.printf("[http] reject oversized response %d\n", contentLength);
    return false;
  }
  size_t reserveBytes = contentLength > 0 ? (size_t)contentLength : min(maxBytes, (size_t)1024);
  BoundedStringStream sink(payload, maxBytes, reserveBytes);
  int written = http.writeToStream(&sink);
  if (written < 0 || sink.overflowed() || payload.length() == 0 || payload.length() > maxBytes) {
    payload = "";
    return false;
  }
  return true;
}

bool httpGetText(const String &url, unsigned long timeoutMs, size_t maxBytes, String &payload, int &statusCode) {
  payload = "";
  statusCode = 0;
  if (WiFi.status() != WL_CONNECTED) return false;
  WiFiClient client;
  HTTPClient http;
  http.setTimeout(timeoutMs);
  if (!http.begin(client, url)) return false;
  statusCode = http.GET();
  bool ok = statusCode == HTTP_CODE_OK && readHttpTextBounded(http, maxBytes, payload);
  http.end();
  return ok;
}

bool gunzipJson(const String &compressed, size_t maxBytes, String &payload) {
  payload = "";
  size_t compressedLength = compressed.length();
  if (compressedLength < 18 || compressedLength > QWEATHER_GZIP_MAX_BYTES) return false;
  const uint8_t *source = reinterpret_cast<const uint8_t *>(compressed.c_str());
  if (source[0] != 0x1f || source[1] != 0x8b || source[2] != 8) return false;
  uint32_t expectedLength = (uint32_t)source[compressedLength - 4] |
                            ((uint32_t)source[compressedLength - 3] << 8) |
                            ((uint32_t)source[compressedLength - 2] << 16) |
                            ((uint32_t)source[compressedLength - 1] << 24);
  if (expectedLength == 0 || expectedLength > maxBytes) return false;

  uint8_t *output = static_cast<uint8_t *>(malloc(expectedLength + 1));
  TINF_DATA *state = static_cast<TINF_DATA *>(malloc(sizeof(TINF_DATA)));
  if (!output || !state) {
    free(output);
    free(state);
    return false;
  }

  uzlib_init();
  uzlib_uncompress_init(state, nullptr, 0);
  state->source = source;
  state->source_limit = source + compressedLength;
  state->source_read_cb = nullptr;
  int result = uzlib_gzip_parse_header(state);
  if (result == TINF_OK) {
    state->dest_start = state->dest = output;
    // One sentinel byte lets uzlib consume the end marker when the stream
    // produces exactly ISIZE bytes, while still detecting overlong output.
    state->dest_limit = output + expectedLength + 1;
    do {
      result = uzlib_uncompress_chksum(state);
    } while (result == TINF_OK && state->dest < state->dest_limit);
  }
  size_t actualLength = result == TINF_DONE ? (size_t)(state->dest - output) : 0;
  bool valid = result == TINF_DONE && state->source == source + compressedLength &&
               actualLength == expectedLength && output[0] == '{' &&
               output[expectedLength - 1] == '}';
  free(state);
  bool ok = valid && payload.reserve(expectedLength + 1) &&
            payload.concat(reinterpret_cast<const char *>(output), expectedLength);
  free(output);
  if (!ok) payload = "";
  return ok;
}

bool qweatherHttpsGetText(const String &url, unsigned long timeoutMs, size_t maxBytes,
                          String &payload, int &statusCode) {
  payload = "";
  statusCode = 0;
  if (WiFi.status() != WL_CONNECTED || !timeIsValid() || !qweatherConfigured()) return false;
  String compressed;
  bool downloaded = false;
  {
    if (qweatherMflnState <= 0) {
      qweatherMflnState = BearSSL::WiFiClientSecure::probeMaxFragmentLength(
                              qweatherApiHost.c_str(), 443, 512)
                              ? 1
                              : 0;
      Serial.printf("[weather] QWeather TLS MFLN probe=%d\n", qweatherMflnState);
    }
    if (qweatherMflnState != 1) return false;

    BearSSL::X509List trustAnchor(QWEATHER_ROOT_CA);
    BearSSL::WiFiClientSecure client;
    client.setTrustAnchors(&trustAnchor);
    client.setBufferSizes(512, 512);
    HTTPClient http;
    http.setTimeout(timeoutMs);
    if (!http.begin(client, url)) return false;
    http.addHeader("X-QW-Api-Key", qweatherApiKey);
    statusCode = http.GET();
    if (statusCode < 0) qweatherMflnState = -1;
    uint32_t freeHeap = ESP.getFreeHeap();
    uint32_t freeBlock = ESP.getMaxFreeBlockSize();
    if (minQWeatherTlsHeap == 0 || freeHeap < minQWeatherTlsHeap) minQWeatherTlsHeap = freeHeap;
    if (minQWeatherTlsBlock == 0 || freeBlock < minQWeatherTlsBlock) minQWeatherTlsBlock = freeBlock;
    downloaded = statusCode == HTTP_CODE_OK &&
                 readHttpTextBounded(http, QWEATHER_GZIP_MAX_BYTES, compressed);
    freeHeap = ESP.getFreeHeap();
    freeBlock = ESP.getMaxFreeBlockSize();
    if (freeHeap < minQWeatherTlsHeap) minQWeatherTlsHeap = freeHeap;
    if (freeBlock < minQWeatherTlsBlock) minQWeatherTlsBlock = freeBlock;
    http.end();
  }
  return downloaded && gunzipJson(compressed, maxBytes, payload);
}

bool saveOfflineState(const String *symbolOverride = nullptr, int symbolOverrideCount = -1,
                      int pendingOverride = -1) {
  if (!fsAvailable) return false;
  lastOfflineStateSaveMs = millis();
  lastOfflineStateSaveFailed = true;
  JsonDocument doc;
  doc["schema"] = 1;
  const String *persistedSymbols = symbolOverride ? symbolOverride : stockSymbols;
  int persistedSymbolCount = symbolOverrideCount >= 0 ? symbolOverrideCount : stockSymbolCount;
  bool persistedPending = pendingOverride >= 0 ? pendingOverride != 0 : stockConfigSyncPending;
  doc["stock_sync_pending"] = persistedPending;
  doc["stock_updated_at"] = stockUpdatedAt;
  JsonArray symbols = doc["symbols"].to<JsonArray>();
  for (int i = 0; i < persistedSymbolCount; i++) symbols.add(persistedSymbols[i]);
  JsonArray rows = doc["stocks"].to<JsonArray>();
  for (int i = 0; i < persistedSymbolCount; i++) {
    int sourceIndex = stockRowIndex(persistedSymbols[i]);
    JsonObject row = rows.add<JsonObject>();
    row["symbol"] = persistedSymbols[i];
    row["code"] = sourceIndex >= 0 ? stocks[sourceIndex].code : stockDisplayCode(persistedSymbols[i]);
    row["price"] = sourceIndex >= 0 ? stocks[sourceIndex].price : "--";
    row["pct"] = sourceIndex >= 0 ? stocks[sourceIndex].pct : "--";
    row["up"] = sourceIndex >= 0 ? stocks[sourceIndex].up : 0;
  }
  JsonObject w = doc["weather"].to<JsonObject>();
  w["available"] = weather.available;
  if (weather.available) {
    w["temperature_c"] = weather.temperatureC;
    w["apparent_c"] = weather.apparentC;
    w["high_c"] = weather.highC;
    w["low_c"] = weather.lowC;
    w["wind_kmh"] = weather.windKmh;
    w["humidity_pct"] = weather.humidityPct;
    w["weather_code"] = weather.weatherCode;
    w["skycon"] = weather.skycon;
    w["aqi"] = weather.aqi;
    w["air_quality_zh"] = weather.airQualityZh;
    w["provider"] = weather.provider;
    w["condition_en"] = weather.conditionEn;
    w["condition_zh"] = weather.conditionZh;
    w["lunar_zh"] = weather.lunarZh;
    w["updated_at"] = weather.updatedAt;
  }

  File f = LittleFS.open(OFFLINE_STATE_TMP_FILE, "w");
  if (!f) return false;
  size_t written = serializeJson(doc, f);
  f.flush();
  size_t fileSize = f.size();
  if (written == 0 || fileSize != written || fileSize > DIRECT_RESPONSE_MAX_BYTES) {
    f.close();
    LittleFS.remove(OFFLINE_STATE_TMP_FILE);
    return false;
  }
  f.close();

  if (!promoteVerifiedJson(OFFLINE_STATE_TMP_FILE, OFFLINE_STATE_FILE,
                           OFFLINE_STATE_BAK_FILE, true)) return false;
  offlineStateDirty = false;
  offlineStateExists = true;
  lastOfflineStateSaveFailed = false;
  return true;
}

bool markOfflineStateDirty(bool saveNow = false) {
  offlineStateDirty = true;
  return !saveNow || saveOfflineState();
}

void maybeSaveOfflineState() {
  if (!fsAvailable || !offlineStateDirty) return;
  unsigned long interval = lastOfflineStateSaveFailed || !offlineStateExists
                               ? OFFLINE_CACHE_RETRY_MS
                               : OFFLINE_CACHE_SAVE_INTERVAL_MS;
  if (lastOfflineStateSaveMs == 0 || millis() - lastOfflineStateSaveMs >= interval) {
    saveOfflineState();
  }
}

void loadOfflineState() {
  loadDefaultStockSymbols();
  const char *path = validPersistedJson(OFFLINE_STATE_FILE, true)
                         ? OFFLINE_STATE_FILE
                         : (validPersistedJson(OFFLINE_STATE_BAK_FILE, true)
                                ? OFFLINE_STATE_BAK_FILE
                                : nullptr);
  if (!path) return;
  File f = LittleFS.open(path, "r");
  if (!f) return;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err || (doc["schema"] | 0) != 1) return;

  String loadedSymbols[MAX_STOCKS];
  int loadedCount = 0;
  for (JsonVariant value : doc["symbols"].as<JsonArray>()) {
    String symbol = normalizeStockSymbol(value.as<String>());
    if (symbol.length() == 0) continue;
    bool duplicate = false;
    for (int i = 0; i < loadedCount; i++) duplicate = duplicate || loadedSymbols[i].equalsIgnoreCase(symbol);
    if (!duplicate && loadedCount < MAX_STOCKS) loadedSymbols[loadedCount++] = symbol;
  }
  if (loadedCount > 0) applyStockSymbolList(loadedSymbols, loadedCount);
  stockConfigSyncPending = doc["stock_sync_pending"] | false;
  stockUpdatedAt = doc["stock_updated_at"] | 0L;

  bool hasCachedStock = false;
  for (JsonObject row : doc["stocks"].as<JsonArray>()) {
    String symbol = normalizeStockSymbol(row["symbol"].as<String>());
    int index = stockRowIndex(symbol);
    if (index < 0) continue;
    stocks[index].code = row["code"] | stockDisplayCode(symbol);
    stocks[index].price = row["price"] | "--";
    stocks[index].pct = row["pct"] | "--";
    stocks[index].up = row["up"] | 0;
    hasCachedStock = hasCachedStock || stocks[index].price != "--";
  }
  if (hasCachedStock) stockSource = FEED_CACHE;

  JsonObject w = doc["weather"];
  if (!w.isNull() && (w["available"] | false)) {
    weather.available = true;
    weather.temperatureC = w["temperature_c"] | NAN;
    weather.apparentC = w["apparent_c"] | NAN;
    weather.highC = w["high_c"] | NAN;
    weather.lowC = w["low_c"] | NAN;
    weather.windKmh = w["wind_kmh"] | NAN;
    weather.humidityPct = w["humidity_pct"] | -1;
    weather.weatherCode = w["weather_code"] | -1;
    weather.skycon = w["skycon"] | "";
    weather.aqi = w["aqi"] | -1;
    weather.airQualityZh = w["air_quality_zh"] | "";
    weather.provider = w["provider"] | "legacy";
    weather.conditionEn = w["condition_en"] | "";
    weather.conditionZh = w["condition_zh"] | "";
    weather.lunarZh = w["lunar_zh"] | "";
    weather.updatedAt = w["updated_at"] | 0L;
    weather.stale = true;
    weather.source = FEED_CACHE;
    weather.textRev = -1;
  }
  offlineStateExists = true;
  stockDirty = true;
  clockDirty = true;
}

// ---------- custom sprite loading ----------

// Checks LittleFS for a previously-uploaded custom sprite and validates its
// size before trusting it (frame count byte + exact expected byte length).
void loadCustomSpriteState() {
  claudeCustom = false;
  if (LittleFS.exists(CLAUDE_SPRITE_FILE)) {
    File f = LittleFS.open(CLAUDE_SPRITE_FILE, "r");
    if (f && f.size() >= 1) {
      uint8_t cnt = f.read();
      size_t expected = 1 + (size_t)cnt * CLAUDE_FRAME_BYTES;
      if (cnt > 0 && cnt <= MAX_CUSTOM_FRAMES && (size_t)f.size() == expected) {
        claudeCustom = true;
        claudeCustomFrames = cnt;
      }
    }
    if (f) f.close();
  }

  codexCustom = false;
  if (LittleFS.exists(CODEX_SPRITE_FILE)) {
    File f = LittleFS.open(CODEX_SPRITE_FILE, "r");
    if (f && f.size() >= 1) {
      uint8_t cnt = f.read();
      size_t expected = 1 + (size_t)cnt * CODEX_FRAME_BYTES;
      if (cnt > 0 && cnt <= MAX_CUSTOM_FRAMES && (size_t)f.size() == expected) {
        codexCustom = true;
        codexCustomFrames = cnt;
      }
    }
    if (f) f.close();
  }

  Serial.printf("[sprite] claude custom=%d frames=%d | codex custom=%d frames=%d\n", claudeCustom,
                claudeCustomFrames, codexCustom, codexCustomFrames);
}

void cleanupInterruptedSpriteUploads() {
  if (!fsAvailable) return;
  LittleFS.remove(CLAUDE_GIF_FILE);
  LittleFS.remove(CODEX_GIF_FILE);
  LittleFS.remove(CLAUDE_SPRITE_TMP_FILE);
  LittleFS.remove(CODEX_SPRITE_TMP_FILE);
}

int claudeFrameCount() { return claudeCustom ? claudeCustomFrames : CLAUDE_SPRITE_FRAMES; }
int codexFrameCount() { return codexCustom ? codexCustomFrames : CODEX_SPRITE_FRAMES; }

// Draws one sprite frame centered on screen, one row at a time so we never
// need a full-frame buffer: each row comes either from the custom LittleFS
// file (streamed) or the compiled-in PROGMEM default (copied row-by-row).
void drawSpriteFrame(bool custom, const char *file, const uint16_t *const *progmemFrames, int frameIdx, int w,
                     int h, size_t frameBytes) {
  int x0 = SCREEN_CX - w / 2, y0 = SCREEN_CY - h / 2;
  size_t rowBytes = (size_t)w * 2;
  if (custom) {
    File f = LittleFS.open(file, "r");
    if (!f) return;
    f.seek(1 + (size_t)frameIdx * frameBytes);
    for (int r = 0; r < h; r++) {
      f.read((uint8_t *)rowBuf, rowBytes);
      tft.pushImage(x0, y0 + r, w, 1, rowBuf);
    }
    f.close();
  } else {
    const uint16_t *frame = progmemFrames[frameIdx];
    for (int r = 0; r < h; r++) {
      memcpy_P(rowBuf, frame + (size_t)r * w, rowBytes);
      tft.pushImage(x0, y0 + r, w, 1, rowBuf);
    }
  }
}

// ---------- helpers ----------

String formatTokens(long tokens) {
  if (tokens >= 1000000) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1fM", tokens / 1000000.0);
    return String(buf);
  }
  if (tokens >= 1000) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1fk", tokens / 1000.0);
    return String(buf);
  }
  return String(tokens);
}

// ---------- drawing ----------

void drawStaticChrome() {
  tft.fillScreen(TFT_BLACK);
}

// Bridge unreachable / data stale -> flashing red overrides everything else,
// matches the "urgent, look now" state from the reference signal-light design.
bool bridgeStale() {
  if (!everPolled) return true;
  return (millis() - lastSuccessMs) >= 2UL * BRIDGE_POLL_INTERVAL_MS;
}

// True when the app currently on screen is waiting on a permission/approval
// prompt — drives the red "look now, act" border flash.
bool currentAppNeedsInput() {
  return currentApp == APP_CLAUDE ? claudeStatus.needsInput : codexStatus.needsInput;
}

// Working vs idle is now conveyed by the sprite animation itself (moving vs
// still), not by ring color. The ring just stays steady green, except
// bridge-stale which flashes red ("check it now") and overrides everything.
uint16_t currentStatusColor() {
  if (bridgeStale()) return flashOn ? TFT_RED : TFT_BLACK;
  return TFT_GREEN;
}

// The ring is skipped when nothing changed (see drawSquareRing) so the 5s
// poll doesn't visibly blank-and-repaint it. Anything that paints over the
// ring area must invalidate this cache.
float ringLastPct = -1000;
uint16_t ringLastColor = 1;

// Paints the full square border in one color (all four sides), used for the
// attention flash so the whole edge blinks, not just the filled quota arc.
void drawFullBorder(uint16_t color) {
  ringLastPct = -1000; // ring got painted over; next ring draw must repaint
  int x0 = RING_MARGIN, y0 = RING_MARGIN;
  int side = SCREEN_W - 2 * RING_MARGIN;
  tft.fillRect(x0, y0, side, RING_THICKNESS, color);                              // top
  tft.fillRect(x0, SCREEN_H - RING_MARGIN - RING_THICKNESS, side, RING_THICKNESS, color); // bottom
  tft.fillRect(x0, y0, RING_THICKNESS, side, color);                              // left
  tft.fillRect(SCREEN_W - RING_MARGIN - RING_THICKNESS, y0, RING_THICKNESS, side, color); // right
}

// Square progress ring hugging the screen edge. `pct` of the perimeter
// (clockwise from top-left) is drawn in `color`, the rest in dark grey.
void drawSquareRing(float pct, uint16_t color) {
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  if (pct == ringLastPct && color == ringLastColor) return; // nothing changed
  ringLastPct = pct;
  ringLastColor = color;

  int x0 = RING_MARGIN, y0 = RING_MARGIN;
  int x1 = SCREEN_W - RING_MARGIN, y1 = SCREEN_H - RING_MARGIN;
  int side = x1 - x0;
  float perimeter = side * 4.0;

  // Unfilled track is drawn black (not grey) so it blends into the background
  // and only the active quota portion is visible - still needs to be actively
  // repainted each time though, to erase a previously longer fill if the
  // percentage drops (e.g. a quota window reset).
  tft.fillRect(x0, y0, side, RING_THICKNESS, TFT_BLACK);                  // top
  tft.fillRect(x1 - RING_THICKNESS, y0, RING_THICKNESS, side, TFT_BLACK); // right
  tft.fillRect(x0, y1 - RING_THICKNESS, side, RING_THICKNESS, TFT_BLACK); // bottom
  tft.fillRect(x0, y0, RING_THICKNESS, side, TFT_BLACK);                  // left

  // filled portion, clockwise: top -> right -> bottom -> left
  float remaining = perimeter * (pct / 100.0);
  if (remaining <= 0) return;

  float seg = min(remaining, (float)side);
  tft.fillRect(x0, y0, (int)seg, RING_THICKNESS, color);
  remaining -= side;
  if (remaining <= 0) return;

  seg = min(remaining, (float)side);
  tft.fillRect(x1 - RING_THICKNESS, y0, RING_THICKNESS, (int)seg, color);
  remaining -= side;
  if (remaining <= 0) return;

  seg = min(remaining, (float)side);
  tft.fillRect(x1 - (int)seg, y1 - RING_THICKNESS, (int)seg, RING_THICKNESS, color);
  remaining -= side;
  if (remaining <= 0) return;

  seg = min(remaining, (float)side);
  tft.fillRect(x0, y1 - (int)seg, RING_THICKNESS, (int)seg, color);
}

void drawClaudeSprite(int frameIdx) {
  drawSpriteFrame(claudeCustom, CLAUDE_SPRITE_FILE, claude_sprite_frames, frameIdx, CLAUDE_SPRITE_W,
                  CLAUDE_SPRITE_H, CLAUDE_FRAME_BYTES);
}

void drawCodexSprite(int frameIdx) {
  drawSpriteFrame(codexCustom, CODEX_SPRITE_FILE, codex_sprite_frames, frameIdx, CODEX_SPRITE_W, CODEX_SPRITE_H,
                  CODEX_FRAME_BYTES);
}

String pctText(float pct) {
  return pct >= 0 ? String((int)pct) + "%" : "-";
}

// Quota readout below the sprite: two columns ("5h" / "Wk"), small grey label
// over a big font-4 percentage. Values repaint only when their text changes
// (force = after a full-screen clear), so the 5s poll never flashes them.
const int QUOTA_LABEL_Y = 183, QUOTA_VALUE_Y = 199;
const int QUOTA_COL1_X = 70, QUOTA_COL2_X = 170;
String lastQuota5h, lastQuotaWk;

// Faux-bold: the packed TFT_eSPI fonts have no bold face, so draw twice with
// a 1px x offset. Transparent draws - the caller must have cleared the region.
void drawBoldString(const String &s, int x, int y, int font, uint16_t color) {
  tft.setTextColor(color);
  tft.drawString(s, x, y, font);
  tft.drawString(s, x + 1, y, font);
}

void drawQuotaText(float hourPct, float weekPct, bool force) {
  // Codex may only have a weekly window now. Collapse the two-column layout
  // to one centered Wk value when the 5h value is absent.
  bool single = hourPct < 0 && weekPct >= 0;
  static int8_t lastSingle = -1;
  if ((int8_t)single != lastSingle) {
    lastSingle = (int8_t)single;
    force = true;
    tft.fillRect(0, QUOTA_LABEL_Y, 240, QUOTA_VALUE_Y + 26 - QUOTA_LABEL_Y, TFT_BLACK);
  }
  tft.setTextDatum(TC_DATUM);
  if (single) {
    if (force) drawBoldString("Wk", 120, QUOTA_LABEL_Y, 2, TFT_LIGHTGREY);
    String value = pctText(weekPct);
    if (force || value != lastQuotaWk) {
      lastQuota5h = "";
      lastQuotaWk = value;
      tft.fillRect(70, QUOTA_VALUE_Y, 100, 26, TFT_BLACK);
      drawBoldString(value, 120, QUOTA_VALUE_Y, 4, TFT_WHITE);
    }
    return;
  }
  if (force) {
    drawBoldString("5h", QUOTA_COL1_X, QUOTA_LABEL_Y, 2, TFT_LIGHTGREY);
    drawBoldString("Wk", QUOTA_COL2_X, QUOTA_LABEL_Y, 2, TFT_LIGHTGREY);
  }
  String v1 = pctText(hourPct), v2 = pctText(weekPct);
  if (force || v1 != lastQuota5h) {
    lastQuota5h = v1;
    tft.fillRect(QUOTA_COL1_X - 50, QUOTA_VALUE_Y, 100, 26, TFT_BLACK);
    drawBoldString(v1, QUOTA_COL1_X, QUOTA_VALUE_Y, 4, TFT_WHITE);
  }
  if (force || v2 != lastQuotaWk) {
    lastQuotaWk = v2;
    tft.fillRect(QUOTA_COL2_X - 50, QUOTA_VALUE_Y, 100, 26, TFT_BLACK);
    drawBoldString(v2, QUOTA_COL2_X, QUOTA_VALUE_Y, 4, TFT_WHITE);
  }
}

// ---------- quota-exhausted countdown ----------
// When the current app's 5h or weekly window is used up, the pet is replaced
// by a countdown to that window's reset (bridge sends minutes-until-reset).
// A spent weekly window blocks usage even after the 5h one resets, so the
// weekly countdown takes priority when both are exhausted.

enum CdType { CD_NONE, CD_5H, CD_WEEK };

float currentHourPct() {
  return currentApp == APP_CLAUDE ? claudeStatus.fiveHourPct : codexStatus.primaryPct;
}

int currentHourResetMin() {
  return currentApp == APP_CLAUDE ? claudeStatus.fiveHourResetMin : codexStatus.primaryResetMin;
}

float currentWeekPct() {
  return currentApp == APP_CLAUDE ? claudeStatus.sevenDayPct : codexStatus.weeklyPct;
}

int currentWeekResetMin() {
  return currentApp == APP_CLAUDE ? claudeStatus.sevenDayResetMin : codexStatus.weeklyResetMin;
}

CdType desiredCountdown() {
  if (currentWeekPct() >= 99.9f && currentWeekResetMin() >= 0) return CD_WEEK;
  if (currentHourPct() >= 99.9f && currentHourResetMin() >= 0) return CD_5H;
  return CD_NONE;
}

CdType showingCd = CD_NONE; // what's on screen now (vs desiredCountdown())
String lastCountdown;

// The bridge only reports whole minutes, so the seconds tick locally against
// a deadline anchored at millis(). Re-anchor only when the bridge disagrees
// by more than ~a minute (new window, big clock drift), otherwise a poll
// landing mid-minute would make the seconds jump around.
unsigned long cdDeadlineMs = 0; // 0 = not anchored
ActiveApp cdApp = APP_CLAUDE;   // which app/window the anchor belongs to
CdType cdAnchorType = CD_NONE;

void syncCountdownDeadline() {
  int m = showingCd == CD_WEEK ? currentWeekResetMin() : currentHourResetMin();
  if (m < 0) {
    cdDeadlineMs = 0;
    return;
  }
  long bridgeSec = (long)m * 60 + 30; // bridge floors to minutes: assume mid-minute
  long ourSec = (long)(cdDeadlineMs - millis()) / 1000;
  if (cdDeadlineMs == 0 || cdApp != currentApp || cdAnchorType != showingCd || ourSec < 0 ||
      labs(ourSec - bridgeSec) > 90) {
    cdDeadlineMs = millis() + (unsigned long)bridgeSec * 1000UL;
    cdApp = currentApp;
    cdAnchorType = showingCd;
  }
}

void drawCountdown(bool force) {
  long remain = cdDeadlineMs ? (long)(cdDeadlineMs - millis()) / 1000
                             : (long)(showingCd == CD_WEEK ? currentWeekResetMin() : currentHourResetMin()) * 60;
  if (remain < 0) remain = 0;
  char buf[16];
  long hours = remain / 3600;
  if (hours >= 100) // weekly can be up to 168h: h:mm:ss wouldn't fit the ring
    snprintf(buf, sizeof(buf), "%ld:%02ld", hours, (remain % 3600) / 60);
  else
    snprintf(buf, sizeof(buf), "%ld:%02ld:%02ld", hours, (remain % 3600) / 60, remain % 60);
  String t(buf);
  if (!force && t == lastCountdown) return;
  // in-place glyph overwrite can't erase a shrinking string (h:mm:ss width is
  // constant, but 100:00 -> 99:59:59 changes layout once) - clear on any
  // length change
  if (t.length() != lastCountdown.length()) force = true;
  lastCountdown = t;
  tft.setTextDatum(TC_DATUM);
  if (force) {
    tft.fillRect(SCREEN_CX - 99, 66, 198, 84, TFT_BLACK);
    drawBoldString(showingCd == CD_WEEK ? "Wk RESET IN" : "5h RESET IN", SCREEN_CX, 72, 2, TFT_LIGHTGREY);
  }
  // Background-color draw overwrites glyphs in place (no clear-then-draw
  // flash between seconds).
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.drawString(t, SCREEN_CX, 102, 6);
}

// App logo in the top-left corner (inside the quota ring) so a glance tells
// which app the screen is currently showing. Drawn row-by-row from PROGMEM
// through rowBuf, same as the sprite path.
const int LOGO_X = 14, LOGO_Y = 18;

void drawAppLogo() {
  const uint16_t *logo = (currentApp == APP_CLAUDE) ? claude_logo_0 : codex_logo_0;
  int w = (currentApp == APP_CLAUDE) ? CLAUDE_LOGO_W : CODEX_LOGO_W;
  int h = (currentApp == APP_CLAUDE) ? CLAUDE_LOGO_H : CODEX_LOGO_H;
  for (int r = 0; r < h; r++) {
    memcpy_P(rowBuf, logo + (size_t)r * w, (size_t)w * 2);
    tft.pushImage(LOGO_X, LOGO_Y + r, w, 1, rowBuf);
  }
}

// Claude's ring percentage: real 5h OAuth quota from the bridge when known,
// otherwise fall back to elapsed session time as a rough stand-in.
float claudeRingPct() {
  if (claudeStatus.fiveHourPct >= 0) return claudeStatus.fiveHourPct;
  return claudeStatus.sessionWindowMin > 0
             ? (100.0 * claudeStatus.sessionMin / claudeStatus.sessionWindowMin)
             : 0;
}

float codexRingPct() {
  if (codexStatus.primaryPct >= 0) return codexStatus.primaryPct;
  return max(codexStatus.weeklyPct, 0.0f);
}

// Redraws whichever app is currently active, full screen: quota ring +
// sprite (or the reset countdown while the 5h window is exhausted).
// Full clear + repaint - only for real transitions (app switch, mode return,
// sprite change); steady-state data updates go through refreshActiveApp().
void drawActiveApp() {
  tft.fillScreen(TFT_BLACK);
  ringLastPct = -1000; // screen was cleared: force the ring repaint
  showingCd = desiredCountdown();
  if (showingCd != CD_NONE) syncCountdownDeadline();
  else cdDeadlineMs = 0;
  if (currentApp == APP_CLAUDE) {
    drawSquareRing(claudeRingPct(), currentStatusColor());
    if (showingCd == CD_NONE) drawClaudeSprite(claudeFrame);
    drawQuotaText(claudeRingPct(), claudeStatus.sevenDayPct, true);
  } else {
    drawSquareRing(codexRingPct(), currentStatusColor());
    if (showingCd == CD_NONE) drawCodexSprite(codexFrame);
    drawQuotaText(codexStatus.primaryPct, codexStatus.weeklyPct, true);
  }
  if (showingCd != CD_NONE) drawCountdown(true);
  drawAppLogo();
}

// In-place refresh after a bridge poll: ring repaint + only the text that
// actually changed. No fillScreen, so the 5s poll doesn't blank the screen.
void refreshActiveApp() {
  if (desiredCountdown() != showingCd) { // pet <-> countdown (or 5h <-> weekly) swap
    drawActiveApp();
    return;
  }
  if (currentApp == APP_CLAUDE) {
    drawSquareRing(claudeRingPct(), currentStatusColor());
    drawQuotaText(claudeRingPct(), claudeStatus.sevenDayPct, false);
  } else {
    drawSquareRing(codexRingPct(), currentStatusColor());
    drawQuotaText(codexStatus.primaryPct, codexStatus.weeklyPct, false);
  }
  if (showingCd != CD_NONE) {
    syncCountdownDeadline();
    drawCountdown(false);
  }
}

// Redraws just the ring (cheap) - used for status color animation ticks
// between full redraws.
void redrawRingOnly() {
  if (currentApp == APP_CLAUDE) {
    drawSquareRing(claudeRingPct(), currentStatusColor());
  } else {
    drawSquareRing(codexRingPct(), currentStatusColor());
  }
}

// Who gets the screen:
//   - display mode pinned (Mac app) -> that app, always
//   - exactly one app working       -> that app, immediately
//   - both working                  -> alternate every SWITCH_BOTH_MS (2s)
//   - neither working               -> alternate slowly (SWITCH_IDLE_MS)
bool updateActiveApp() {
  ActiveApp desired = currentApp;

  if (displayMode == MODE_CLAUDE) {
    desired = APP_CLAUDE;
  } else if (displayMode == MODE_CODEX) {
    desired = APP_CODEX;
  } else if (claudeStatus.needsInput && !codexStatus.needsInput) {
    desired = APP_CLAUDE; // approval prompt wins the screen
  } else if (codexStatus.needsInput && !claudeStatus.needsInput) {
    desired = APP_CODEX;
  } else {
    bool claudeWorking = claudeStatus.status == "working";
    bool codexWorking = codexStatus.status == "working";
    if (claudeWorking && !codexWorking) {
      desired = APP_CLAUDE;
    } else if (codexWorking && !claudeWorking) {
      desired = APP_CODEX;
    } else {
      unsigned long interval = (claudeWorking && codexWorking) ? SWITCH_BOTH_MS : SWITCH_IDLE_MS;
      if (millis() - lastSwitchMs >= interval) {
        lastSwitchMs = millis();
        desired = (currentApp == APP_CLAUDE) ? APP_CODEX : APP_CLAUDE;
      }
    }
  }

  if (desired != currentApp) {
    currentApp = desired;
    lastSwitchMs = millis();
    return true;
  }
  return false;
}

// ---------- net speed screen ----------

String speedText(long bps) {
  char buf[16];
  if (bps >= 1000000) snprintf(buf, sizeof(buf), "%.1fM", bps / 1000000.0);
  else if (bps >= 1000) snprintf(buf, sizeof(buf), "%.0fK", bps / 1000.0);
  else snprintf(buf, sizeof(buf), "%ldB", bps);
  return String(buf);
}

// pushImage() colors must be pre-byte-swapped (this firmware never enables
// setSwapBytes; see the sprite pipeline). Natural RGB565 -> wire order:
inline uint16_t swap565(uint16_t c) { return (uint16_t)((c << 8) | (c >> 8)); }

void resetNetChart() {
  memset(netHistRx, 0, sizeof(netHistRx));
  memset(netHistTx, 0, sizeof(netHistTx));
  netScale = 10240;
  netLastDl = "";
  netLastUl = "";
  netLastScaleText = "";
  netLastCpuVal = "";
  netLastMemVal = "";
  netSysLabelsDrawn = false;
  netQHead = 0;
  netQCount = 0;
  netSeq = -1;
}

// Adaptive full scale: the window's peak always lands at ~87% of the chart
// height, so the undulation stays visible no matter the absolute speed.
// (The old 1/2/5 stepped scale could squash everything to under half height.)
long adaptiveNetScale(long maxV) {
  long s = maxV + maxV / 7; // ~1.15x headroom above the peak
  return s > 10240 ? s : 10240;
}

// Static chrome: labels that never change while in net mode.
void drawNetChrome() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(0x7BEF, TFT_BLACK);
  tft.drawString("DOWN", 14, 10, 1);
  tft.drawString("UP", 134, 10, 1);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("MAC NET  -  56s", SCREEN_CX, 226, 1); // below the CPU/MEM row
}

// Mac CPU / memory usage row between the chart and the footer: small grey
// labels at fixed positions, big font-4 values left-aligned at fixed x so a
// width change (5% -> 30%) never shifts the rest of the row around.
// Hidden only if an old bridge doesn't send the fields yet.
const int NET_SYS_Y = 192;                          // row top (26px tall, font 4)
const int NET_CPU_LABEL_X = 28, NET_CPU_VAL_X = 62; // value region 62..126 ("100%" = 63px)
const int NET_MEM_LABEL_X = 130, NET_MEM_VAL_X = 164;

void drawNetSysinfoIfChanged() {
  if (netCpuPct < 0) {
    if (netSysLabelsDrawn) { // bridge stopped sending: erase the whole row
      tft.fillRect(0, NET_SYS_Y, SCREEN_W, 26, TFT_BLACK);
      netSysLabelsDrawn = false;
      netLastCpuVal = "";
      netLastMemVal = "";
    }
    return;
  }
  tft.setTextDatum(TL_DATUM);
  if (!netSysLabelsDrawn) {
    netSysLabelsDrawn = true;
    tft.setTextColor(0x7BEF, TFT_BLACK);
    tft.drawString("CPU", NET_CPU_LABEL_X, NET_SYS_Y + 6, 2);
    tft.drawString("MEM", NET_MEM_LABEL_X, NET_SYS_Y + 6, 2);
  }
  String c = String(netCpuPct) + "%", m = String(netMemPct) + "%";
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  if (c != netLastCpuVal) {
    netLastCpuVal = c;
    tft.fillRect(NET_CPU_VAL_X, NET_SYS_Y, 64, 26, TFT_BLACK);
    tft.drawString(c, NET_CPU_VAL_X, NET_SYS_Y, 4);
  }
  if (m != netLastMemVal) {
    netLastMemVal = m;
    tft.fillRect(NET_MEM_VAL_X, NET_SYS_Y, 64, 26, TFT_BLACK);
    tft.drawString(m, NET_MEM_VAL_X, NET_SYS_Y, 4);
  }
}

// Header readouts (1s-averaged), each repainted only when its text changes.
void drawNetHeaderIfChanged() {
  String dl = speedText(netCurRx) + "/s";
  String ul = speedText(netCurTx) + "/s";
  tft.setTextDatum(TL_DATUM);
  if (dl != netLastDl) {
    netLastDl = dl;
    tft.fillRect(12, 20, 116, 28, TFT_BLACK);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString(dl, 12, 20, 4);
  }
  if (ul != netLastUl) {
    netLastUl = ul;
    tft.fillRect(132, 20, 108, 28, TFT_BLACK);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString(ul, 132, 20, 4);
  }
}

// Repaints the whole chart region from the sample ring, one row at a time
// through rowBuf (a single pushImage per row = no clear-then-draw flicker).
// Download is a dim-green filled area with a bright top edge; upload is a
// 2px yellow line on top; faint gridlines at 25/50/75%.
void drawNetChart() {
  static const uint16_t COL_GRID = swap565(0x2104);   // very dark grey
  static const uint16_t COL_FILL = swap565(0x02A0);   // dim green
  static const uint16_t COL_EDGE = swap565(TFT_GREEN);
  static const uint16_t COL_UL = swap565(TFT_YELLOW);
  static const uint16_t COL_BLACK = swap565(TFT_BLACK);

  long maxV = 0;
  for (int i = 0; i < NET_CHART_W; i++) {
    if (netHistRx[i] > maxV) maxV = netHistRx[i];
    if (netHistTx[i] > maxV) maxV = netHistTx[i];
  }
  netScale = adaptiveNetScale(maxV);

  // Per-column heights (3-tap smoothed), then per-column line "bands": each
  // band spans from the previous column's height to this one's, so steep
  // rises/falls render as connected vertical strokes instead of detached
  // stair-step dots — that's what makes the undulation read as a continuous
  // line, like the Mac mirror's stroked polyline.
  static uint8_t hRx[NET_CHART_W], hTx[NET_CHART_W];
  static uint8_t dlLo[NET_CHART_W], dlHi[NET_CHART_W]; // DL edge band, incl. 3px weight
  static uint8_t ulLo[NET_CHART_W], ulHi[NET_CHART_W]; // UL line band
  // The panel is physically tiny (2.7cm across), so the stroke must be much
  // thicker than the Mac mirror's to read at the same visual weight.
  const int LINE_T = 10; // stroke thickness in px
  for (int i = 0; i < NET_CHART_W; i++) {
    int lo = i > 0 ? i - 1 : 0, hi = i < NET_CHART_W - 1 ? i + 1 : NET_CHART_W - 1;
    long rx = (netHistRx[lo] + netHistRx[i] + netHistRx[hi]) / 3;
    long tx = (netHistTx[lo] + netHistTx[i] + netHistTx[hi]) / 3;
    int hr = (int)((float)rx / netScale * (NET_CHART_H - 2));
    int ht = (int)((float)tx / netScale * (NET_CHART_H - 2));
    hRx[i] = (uint8_t)constrain(hr, 0, NET_CHART_H - 1);
    hTx[i] = (uint8_t)constrain(ht, 0, NET_CHART_H - 1);
  }
  for (int i = 0; i < NET_CHART_W; i++) {
    int prevR = i > 0 ? hRx[i - 1] : hRx[0];
    int prevT = i > 0 ? hTx[i - 1] : hTx[0];
    dlHi[i] = (uint8_t)max((int)hRx[i], prevR);
    dlLo[i] = (uint8_t)max(0, min((int)hRx[i], prevR) - (LINE_T - 1));
    ulHi[i] = (uint8_t)max((int)hTx[i], prevT);
    ulLo[i] = (uint8_t)max(0, min((int)hTx[i], prevT) - (LINE_T - 1));
  }

  for (int row = 0; row < NET_CHART_H; row++) {
    int yFromBot = NET_CHART_H - 1 - row;
    bool gridRow = (row == NET_CHART_H / 4 || row == NET_CHART_H / 2 || row == 3 * NET_CHART_H / 4);
    for (int i = 0; i < NET_CHART_W; i++) {
      uint16_t c = gridRow ? COL_GRID : COL_BLACK;
      if (yFromBot <= dlHi[i] && yFromBot >= dlLo[i]) c = COL_EDGE;
      else if (yFromBot < dlLo[i]) c = COL_FILL;
      if (ulHi[i] > 0 && yFromBot <= ulHi[i] && yFromBot >= ulLo[i]) c = COL_UL;
      rowBuf[i] = c;
    }
    tft.pushImage(NET_CHART_X, NET_CHART_Y + row, NET_CHART_W, 1, rowBuf);
    if ((row & 31) == 31) yield();
  }

  // axis label (outside the chart, so it never gets repainted over)
  String scaleText = speedText(netScale);
  if (scaleText != netLastScaleText) {
    netLastScaleText = scaleText;
    tft.fillRect(120, 48, 112, 10, TFT_BLACK);
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(0x7BEF, TFT_BLACK);
    tft.drawString(scaleText, NET_CHART_X + NET_CHART_W, 48, 1);
    tft.setTextDatum(TL_DATUM);
  }
}

// Chart tick, every NET_DRAW_INTERVAL_MS: shift in queued sample(s), then
// one atomic repaint. If the queue backs up after a slow poll, it works off
// up to three samples per tick until it's back in step.
void netDrawTick() {
  if (!netChromeDrawn) {
    resetNetChart();
    drawNetChrome();
    netChromeDrawn = true;
    netHeaderDirty = true;
  }
  if (netHeaderDirty) {
    drawNetHeaderIfChanged();
    drawNetSysinfoIfChanged();
    netHeaderDirty = false;
  }
  if (netQCount == 0) return;
  int steps = min(netQCount, netQCount > 16 ? 3 : 1);
  while (steps-- > 0 && netQCount > 0) {
    memmove(netHistRx, netHistRx + 1, sizeof(long) * (NET_CHART_W - 1));
    memmove(netHistTx, netHistTx + 1, sizeof(long) * (NET_CHART_W - 1));
    netHistRx[NET_CHART_W - 1] = netQRx[netQHead];
    netHistTx[NET_CHART_W - 1] = netQTx[netQHead];
    netQHead = (netQHead + 1) % NET_QUEUE;
    netQCount--;
  }
  drawNetChart();
}

// Ingests one /net payload (from HTTP polling or a serial #NET frame) into
// the sample queue. The seq field tells us which samples we've already
// queued, so overlapping tails are fine.
bool handleNetPayload(const String &payload) {
  JsonDocument doc;
  if (deserializeJson(doc, payload)) return false;
  netCurRx = doc["rx_bps"] | 0L;
  netCurTx = doc["tx_bps"] | 0L;
  netCpuPct = doc["cpu_pct"] | -1;
  netMemPct = doc["mem_pct"] | -1;
  netHeaderDirty = true;
  long seq = doc["seq"] | -1L;
  JsonArray rx = doc["rx"], tx = doc["tx"];
  int n = min(rx.size(), tx.size());
  // how many of the tail samples are new to us
  int fresh = (netSeq < 0) ? min(n, 8) : (int)min((long)n, seq - netSeq);
  if (fresh < 0) fresh = 0;
  for (int i = n - fresh; i < n; i++) {
    if (netQCount >= NET_QUEUE) break; // queue full: drop the excess
    int tail = (netQHead + netQCount) % NET_QUEUE;
    netQRx[tail] = rx[i].as<long>();
    netQTx[tail] = tx[i].as<long>();
    netQCount++;
  }
  if (seq >= 0) netSeq = seq;
  return true;
}

// Refills the sample queue from the bridge's /net endpoint.
bool pollNet() {
  if (WiFi.status() != WL_CONNECTED || bridgeHost.length() == 0) return false;
  String url = "http://" + bridgeHost + "/net";
  String payload;
  int code = 0;
  bool ok = httpGetText(url, BRIDGE_HTTP_TIMEOUT_MS, DIRECT_RESPONSE_MAX_BYTES, payload, code) &&
            handleNetPayload(payload);
  if (ok) markBridgeReachable();
  return ok;
}

String clockFloat(float value, int decimals = 1) {
  if (isnan(value)) return "--";
  return String(value, decimals);
}

bool handleWeatherPayload(const String &payload) {
  bool hadStoredWeather = weather.source != FEED_NONE;
  JsonDocument doc;
  if (deserializeJson(doc, payload)) return false;
  String nextProvider = doc["provider"] | "";
  String nextSkycon = doc["skycon"] | "";
  if (!(doc["available"] | false) || doc["temperature_c"].isNull() || doc["apparent_c"].isNull() ||
      doc["high_c"].isNull() || doc["low_c"].isNull() || doc["wind_kmh"].isNull() ||
      doc["humidity_pct"].isNull() || doc["weather_code"].isNull() ||
      nextProvider != "qweather" || nextSkycon.length() == 0 || nextSkycon.length() > 31) {
    return false;
  }
  float nextTemperature = doc["temperature_c"].as<float>();
  float nextApparent = doc["apparent_c"].as<float>();
  float nextHigh = doc["high_c"].as<float>();
  float nextLow = doc["low_c"].as<float>();
  float nextWind = doc["wind_kmh"].as<float>();
  int nextHumidity = doc["humidity_pct"] | -1;
  int nextWeatherCode = doc["weather_code"] | -1;
  int nextAqi = doc["aqi"] | -1;
  int nextTextRev = doc["text_rev"] | -1;
  long nextUpdatedAt = doc["updated_at"] | 0L;
  if (!isfinite(nextTemperature) || !isfinite(nextApparent) || !isfinite(nextHigh) || !isfinite(nextLow) ||
      !isfinite(nextWind) || nextHumidity < 0 || nextHumidity > 100 || nextWeatherCode < 0 ||
      nextAqi < -1 || nextAqi > 500) {
    return false;
  }
  bool payloadStale = doc["stale"] | true;
  if (timeIsValid() && nextUpdatedAt > 0) {
    long age = (long)time(nullptr) - nextUpdatedAt;
    payloadStale = payloadStale || age > WEATHER_STALE_AFTER_S || age < -300;
  }
  if (payloadStale || nextUpdatedAt <= 0) {
    bridgeWeatherHealthy = false;
    bridgeWeatherReportedStale = true;
    markBridgeReachable();
    return false;
  }
  auto changedFloat = [](float before, float after) {
    if (isnan(before) && isnan(after)) return false;
    if (isnan(before) != isnan(after)) return true;
    return fabs(before - after) >= 0.05f;
  };
  bool changed = !weather.available || changedFloat(weather.temperatureC, nextTemperature) ||
                 changedFloat(weather.apparentC, nextApparent) || changedFloat(weather.highC, nextHigh) ||
                 changedFloat(weather.lowC, nextLow) || changedFloat(weather.windKmh, nextWind) ||
                 weather.humidityPct != nextHumidity || weather.weatherCode != nextWeatherCode ||
                 weather.aqi != nextAqi || weather.skycon != nextSkycon ||
                 weather.textRev != nextTextRev || weather.source != FEED_BRIDGE;
  weather.available = true;
  weather.location = doc["location"] | WEATHER_LOCATION_NAME;
  weather.conditionZh = doc["condition_zh"] | "";
  weather.conditionEn = doc["condition_en"] | "";
  weather.skycon = nextSkycon;
  weather.aqi = nextAqi;
  weather.airQualityZh = doc["air_quality_zh"] | "";
  weather.provider = nextProvider;
  weather.lunarZh = doc["lunar_zh"] | "";
  weather.temperatureC = nextTemperature;
  weather.apparentC = nextApparent;
  weather.highC = nextHigh;
  weather.lowC = nextLow;
  weather.windKmh = nextWind;
  weather.humidityPct = nextHumidity;
  weather.weatherCode = nextWeatherCode;
  weather.updatedAt = nextUpdatedAt;
  weather.stale = false;
  weather.textRev = nextTextRev;
  weather.source = FEED_BRIDGE;
  lastWeatherSuccessMs = millis();
  bridgeWeatherHealthy = true;
  bridgeWeatherReportedStale = false;
  weatherDirectFallbackActive = false;
  directWeatherCandidate = WeatherState();
  directWeatherStage = QWEATHER_NOW;
  directWeatherCandidateStartedMs = 0;
  weatherTextNeedsFetch = lunarEnabled;
  markBridgeReachable();
  markOfflineStateDirty(!hadStoredWeather);
  if (changed) clockDirty = true;
  return true;
}

bool pollBridgeWeather() {
  if (WiFi.status() != WL_CONNECTED || bridgeHost.length() == 0) return false;
  String url = "http://" + bridgeHost + "/weather";
  String payload;
  int code = 0;
  bool ok = httpGetText(url, BRIDGE_HTTP_TIMEOUT_MS, DIRECT_RESPONSE_MAX_BYTES, payload, code) &&
            handleWeatherPayload(payload);
  bridgeWeatherHealthy = ok;
  if (ok) markBridgeReachable();
  Serial.printf("[weather] bridge GET -> %d parsed=%d\n", code, ok);
  return ok;
}

bool validSkycon(const String &value) {
  if (value.length() == 0 || value.length() > 31) return false;
  for (size_t i = 0; i < value.length(); i++) {
    char ch = value[i];
    if (!(ch == '_' || (ch >= 'A' && ch <= 'Z'))) return false;
  }
  return true;
}

const char *weatherConditionEnglish(const String &skycon) {
  if (skycon == "CLEAR_DAY" || skycon == "CLEAR_NIGHT") return "Clear";
  if (skycon.startsWith("PARTLY_CLOUDY")) return "Partly cloudy";
  if (skycon == "CLOUDY") return "Cloudy";
  if (skycon.indexOf("RAIN") >= 0) return skycon == "STORM_RAIN" ? "Storm" : "Rain";
  if (skycon.indexOf("SNOW") >= 0) return "Snow";
  if (skycon == "FOG") return "Fog";
  if (skycon.indexOf("HAZE") >= 0) return "Haze";
  if (skycon == "DUST" || skycon == "SAND") return "Dust";
  if (skycon == "WIND") return "Windy";
  return "Weather";
}

const char *weatherConditionChinese(const String &skycon) {
  if (skycon == "CLEAR_DAY" || skycon == "CLEAR_NIGHT") return "晴";
  if (skycon.startsWith("PARTLY_CLOUDY")) return "多云";
  if (skycon == "CLOUDY") return "阴";
  if (skycon == "LIGHT_RAIN") return "小雨";
  if (skycon == "MODERATE_RAIN") return "中雨";
  if (skycon == "HEAVY_RAIN") return "大雨";
  if (skycon == "STORM_RAIN") return "暴雨";
  if (skycon.indexOf("SNOW") >= 0) return "雪";
  if (skycon == "FOG") return "雾";
  if (skycon.indexOf("HAZE") >= 0) return "霾";
  if (skycon == "DUST") return "浮尘";
  if (skycon == "SAND") return "沙尘";
  if (skycon == "WIND") return "大风";
  return "天气";
}

int weatherCodeForSkycon(const String &skycon) {
  if (skycon == "CLEAR_DAY" || skycon == "CLEAR_NIGHT") return 0;
  if (skycon.startsWith("PARTLY_CLOUDY")) return 2;
  if (skycon == "CLOUDY") return 3;
  if (skycon == "FOG") return 45;
  if (skycon.indexOf("HAZE") >= 0) return 48;
  if (skycon == "DUST" || skycon == "SAND") return 48;
  if (skycon == "LIGHT_RAIN") return 61;
  if (skycon == "MODERATE_RAIN") return 63;
  if (skycon == "HEAVY_RAIN") return 65;
  if (skycon == "STORM_RAIN") return 82;
  if (skycon == "LIGHT_SNOW") return 71;
  if (skycon == "MODERATE_SNOW") return 73;
  if (skycon == "HEAVY_SNOW") return 75;
  if (skycon == "STORM_SNOW") return 86;
  return 1;
}

bool qweatherFloat(JsonVariantConst value, float &parsed) {
  String raw = value.as<String>();
  if (raw.length() == 0 || raw.length() > 20) return false;
  char *end = nullptr;
  double number = strtod(raw.c_str(), &end);
  if (!end || *end != '\0' || !isfinite(number)) return false;
  parsed = (float)number;
  return true;
}

bool qweatherInt(JsonVariantConst value, int &parsed) {
  String raw = value.as<String>();
  if (raw.length() == 0 || raw.length() > 10) return false;
  char *end = nullptr;
  long number = strtol(raw.c_str(), &end, 10);
  if (!end || *end != '\0' || number < INT_MIN || number > INT_MAX) return false;
  parsed = (int)number;
  return true;
}

bool qweatherObservationEpoch(const String &value, long &epoch) {
  if (value.length() != 22 || value[4] != '-' || value[7] != '-' || value[10] != 'T' ||
      value[13] != ':' || !value.endsWith("+08:00")) return false;
  int year, month, day, hour, minute;
  if (sscanf(value.c_str(), "%4d-%2d-%2dT%2d:%2d+08:00", &year, &month, &day, &hour, &minute) != 5 ||
      year < 2024 || month < 1 || month > 12 || day < 1 || day > 31 ||
      hour < 0 || hour > 23 || minute < 0 || minute > 59) return false;
  struct tm local = {};
  local.tm_year = year - 1900;
  local.tm_mon = month - 1;
  local.tm_mday = day;
  local.tm_hour = hour;
  local.tm_min = minute;
  local.tm_isdst = -1;
  time_t parsed = mktime(&local);
  if (parsed <= 0) return false;
  epoch = (long)parsed;
  return true;
}

String qweatherSkycon(int icon) {
  if (icon == 100) return "CLEAR_DAY";
  if (icon == 150) return "CLEAR_NIGHT";
  if (icon >= 101 && icon <= 103) return "PARTLY_CLOUDY_DAY";
  if (icon >= 151 && icon <= 153) return "PARTLY_CLOUDY_NIGHT";
  if (icon == 104) return "CLOUDY";
  if (icon == 300 || icon == 305 || icon == 309 || icon == 314 || icon == 350 || icon == 399)
    return "LIGHT_RAIN";
  if (icon == 306 || icon == 313 || icon == 315) return "MODERATE_RAIN";
  if (icon == 301 || icon == 307 || icon == 308 || icon == 316 || icon == 317 || icon == 351)
    return "HEAVY_RAIN";
  if ((icon >= 302 && icon <= 304) || (icon >= 310 && icon <= 312) || icon == 318)
    return "STORM_RAIN";
  if (icon == 400 || (icon >= 404 && icon <= 408) || icon == 456 || icon == 457 || icon == 499)
    return "LIGHT_SNOW";
  if (icon == 401 || icon == 405 || icon == 409) return "MODERATE_SNOW";
  if (icon == 402 || icon == 403 || icon == 406 || icon == 407 || icon == 410) return "HEAVY_SNOW";
  if (icon == 500 || icon == 501 || icon == 509 || icon == 510 || icon == 514 || icon == 515) return "FOG";
  if (icon == 502 || (icon >= 511 && icon <= 513)) return "MODERATE_HAZE";
  if (icon == 503 || icon == 507 || icon == 508) return "SAND";
  if (icon == 504) return "DUST";
  return "WEATHER";
}

bool directWeatherCandidateFresh(unsigned long nowMs) {
  if (directWeatherStage == QWEATHER_NOW || directWeatherCandidateStartedMs == 0 ||
      intervalElapsed(nowMs, directWeatherCandidateStartedMs, WEATHER_STALE_AFTER_S * 1000UL)) return false;
  if (!timeIsValid() || directWeatherCandidate.updatedAt <= 0) return true;
  long age = (long)time(nullptr) - directWeatherCandidate.updatedAt;
  return age >= -300 && age <= WEATHER_STALE_AFTER_S;
}

void resetDirectWeatherCandidate() {
  directWeatherCandidate = WeatherState();
  directWeatherStage = QWEATHER_NOW;
  directWeatherCandidateStartedMs = 0;
}

bool handleDirectQWeatherNowPayload(const String &payload) {
  JsonDocument doc;
  if (deserializeJson(doc, payload)) return false;
  JsonObject current = doc["now"];
  if (String((const char *)(doc["code"] | "")) != "200" || current.isNull()) return false;
  float temperature, apparent, wind;
  int humidity, icon;
  String observationText = current["obsTime"] | "";
  String conditionZh = current["text"] | "";
  long observationEpoch = 0;
  if (!qweatherFloat(current["temp"], temperature) || !qweatherFloat(current["feelsLike"], apparent) ||
      !qweatherFloat(current["windSpeed"], wind) || !qweatherInt(current["humidity"], humidity) ||
      !qweatherInt(current["icon"], icon) || !qweatherObservationEpoch(observationText, observationEpoch) ||
      conditionZh.length() == 0 || conditionZh.length() > 24 || temperature < -80 || temperature > 60 ||
      apparent < -100 || apparent > 100 || wind < 0 || wind > 500 || humidity < 0 || humidity > 100) return false;
  if (timeIsValid()) {
    long age = (long)time(nullptr) - observationEpoch;
    if (age > WEATHER_STALE_AFTER_S || age < -300) return false;
  }

  String skycon = qweatherSkycon(icon);
  directWeatherCandidate = WeatherState();
  directWeatherCandidate.available = true;
  directWeatherCandidate.location = WEATHER_LOCATION_NAME;
  directWeatherCandidate.conditionEn = weatherConditionEnglish(skycon);
  directWeatherCandidate.conditionZh = conditionZh;
  directWeatherCandidate.skycon = skycon;
  directWeatherCandidate.provider = "qweather";
  directWeatherCandidate.temperatureC = temperature;
  directWeatherCandidate.apparentC = apparent;
  directWeatherCandidate.windKmh = wind;
  directWeatherCandidate.humidityPct = humidity;
  directWeatherCandidate.weatherCode = weatherCodeForSkycon(skycon);
  directWeatherCandidate.updatedAt = observationEpoch;
  directWeatherCandidate.stale = false;
  directWeatherCandidate.source = FEED_DIRECT;
  return true;
}

bool handleDirectQWeatherForecastPayload(const String &payload) {
  if (directWeatherStage != QWEATHER_FORECAST || !directWeatherCandidateFresh(millis())) return false;
  JsonDocument doc;
  if (deserializeJson(doc, payload)) return false;
  JsonArray daily = doc["daily"];
  if (String((const char *)(doc["code"] | "")) != "200" || daily.size() < 1) return false;
  String forecastDate = daily[0]["fxDate"] | "";
  if (timeIsValid()) {
    time_t now = time(nullptr);
    struct tm local;
    localtime_r(&now, &local);
    char today[11];
    strftime(today, sizeof(today), "%Y-%m-%d", &local);
    if (forecastDate != today) return false;
  }
  float high, low;
  if (!qweatherFloat(daily[0]["tempMax"], high) || !qweatherFloat(daily[0]["tempMin"], low) ||
      high < -80 || high > 60 || low < -80 || low > 60 || low > high) return false;
  directWeatherCandidate.highC = high;
  directWeatherCandidate.lowC = low;
  return true;
}

bool handleDirectQWeatherAirPayload(const String &payload) {
  if (directWeatherStage != QWEATHER_AIR || !directWeatherCandidateFresh(millis())) return false;
  JsonDocument doc;
  if (deserializeJson(doc, payload)) return false;
  JsonArray indexes = doc["indexes"];
  JsonObject selected;
  for (JsonObject index : indexes) {
    String code = index["code"] | "";
    if (code == "cn-mee") {
      selected = index;
      break;
    }
    if (selected.isNull() && code == "cn-mee-1h") selected = index;
  }
  int aqi;
  String category = selected["category"] | "";
  if (selected.isNull() || !qweatherInt(selected["aqi"], aqi) || aqi < 0 || aqi > 500 ||
      category.length() == 0 || category.length() > 18) return false;
  directWeatherCandidate.aqi = aqi;
  directWeatherCandidate.airQualityZh = category;

  bool hadStoredWeather = weather.source != FEED_NONE;
  String lastGoodLunar = weather.lunarZh;
  int lastGoodTextRev = weather.textRev;
  bool changed = !weather.available || weather.source != FEED_DIRECT ||
                 fabs(weather.temperatureC - directWeatherCandidate.temperatureC) >= 0.05f ||
                 fabs(weather.apparentC - directWeatherCandidate.apparentC) >= 0.05f ||
                 fabs(weather.windKmh - directWeatherCandidate.windKmh) >= 0.05f ||
                 isnan(weather.highC) || isnan(weather.lowC) ||
                 fabs(weather.highC - directWeatherCandidate.highC) >= 0.05f ||
                 fabs(weather.lowC - directWeatherCandidate.lowC) >= 0.05f ||
                 weather.humidityPct != directWeatherCandidate.humidityPct ||
                 weather.aqi != directWeatherCandidate.aqi || weather.skycon != directWeatherCandidate.skycon;
  weather = directWeatherCandidate;
  weather.lunarZh = lastGoodLunar;
  weather.textRev = lastGoodTextRev;
  lastWeatherSuccessMs = millis();
  weatherTextNeedsFetch = false;
  resetDirectWeatherCandidate();
  if (changed) clockDirty = true;
  markOfflineStateDirty(!hadStoredWeather);
  return true;
}

String qweatherUrl(const String &pathAndQuery) {
  String url = "https://";
  url += qweatherApiHost;
  url += pathAndQuery;
  return url;
}

bool pollDirectQWeatherNow() {
  if (!qweatherConfigured()) return false;
  String url = qweatherUrl("/v7/weather/now?location=" QWEATHER_LONGITUDE "%2C" QWEATHER_LATITUDE "&lang=zh&unit=m");
  String payload;
  int code = 0;
  bool ok = qweatherHttpsGetText(url, DIRECT_HTTP_TIMEOUT_MS, QWEATHER_JSON_MAX_BYTES, payload, code) &&
            handleDirectQWeatherNowPayload(payload);
  Serial.printf("[weather] QWeather now -> %d parsed=%d bytes=%u\n", code, ok,
                (unsigned)payload.length());
  if (ok) {
    directWeatherCandidateStartedMs = millis();
    directWeatherStage = QWEATHER_FORECAST;
  }
  return ok;
}

bool pollDirectQWeatherForecast() {
  if (!qweatherConfigured()) return false;
  String url = qweatherUrl("/v7/weather/3d?location=" QWEATHER_LONGITUDE "%2C" QWEATHER_LATITUDE "&lang=zh&unit=m");
  String payload;
  int code = 0;
  bool ok = qweatherHttpsGetText(url, DIRECT_HTTP_TIMEOUT_MS, QWEATHER_JSON_MAX_BYTES, payload, code) &&
            handleDirectQWeatherForecastPayload(payload);
  Serial.printf("[weather] QWeather 3d -> %d parsed=%d bytes=%u\n", code, ok,
                (unsigned)payload.length());
  if (ok) directWeatherStage = QWEATHER_AIR;
  return ok;
}

bool pollDirectQWeatherAir() {
  if (!qweatherConfigured()) return false;
  String url = qweatherUrl("/airquality/v1/current/" QWEATHER_LATITUDE "/" QWEATHER_LONGITUDE "?lang=zh");
  String payload;
  int code = 0;
  bool ok = qweatherHttpsGetText(url, DIRECT_HTTP_TIMEOUT_MS, QWEATHER_JSON_MAX_BYTES, payload, code) &&
            handleDirectQWeatherAirPayload(payload);
  Serial.printf("[weather] QWeather air -> %d parsed=%d bytes=%u\n", code, ok,
                (unsigned)payload.length());
  return ok;
}

uint32_t weatherTextDateKey() {
  if (!timeIsValid()) return 0;
  time_t now = time(nullptr);
  struct tm local;
  localtime_r(&now, &local);
  return (uint32_t)(local.tm_year + 1900) * 10000UL +
         (uint32_t)(local.tm_mon + 1) * 100UL + (uint32_t)local.tm_mday;
}

bool validWeatherTextCache(const char *path, int *revision = nullptr,
                           uint32_t *dateKey = nullptr) {
  if (!fsAvailable || !LittleFS.exists(path)) return false;
  File file = LittleFS.open(path, "r");
  if (!file || (size_t)file.size() != WEATHER_TEXT_CACHE_HEADER_BYTES + WEATHER_TEXT_BYTES) {
    if (file) file.close();
    return false;
  }
  uint32_t magic = 0;
  int32_t rev = -2;
  uint32_t day = 0;
  uint32_t expectedCrc = 0;
  bool valid = file.read((uint8_t *)&magic, sizeof(magic)) == sizeof(magic) &&
               file.read((uint8_t *)&rev, sizeof(rev)) == sizeof(rev) &&
               file.read((uint8_t *)&day, sizeof(day)) == sizeof(day) &&
               file.read((uint8_t *)&expectedCrc, sizeof(expectedCrc)) == sizeof(expectedCrc) &&
               magic == WEATHER_TEXT_CACHE_MAGIC && day >= 20200101UL && day <= 20991231UL;
  uint32_t crc = 0xffffffffUL;
  const size_t rowBytes = (size_t)WEATHER_TEXT_W * 2;
  for (int row = 0; valid && row < WEATHER_TEXT_H; row++) {
    valid = file.read((uint8_t *)rowBuf, rowBytes) == rowBytes;
    if (valid) crc = uzlib_crc32(rowBuf, rowBytes, crc);
  }
  valid = valid && ~crc == expectedCrc;
  uint32_t today = weatherTextDateKey();
  if (valid && today != 0 && day != today) valid = false;
  file.close();
  if (valid && revision) *revision = rev;
  if (valid && dateKey) *dateKey = day;
  return valid;
}

const char *lastGoodWeatherTextCache(int *revision = nullptr, uint32_t *dateKey = nullptr) {
  if (validWeatherTextCache(WEATHER_TEXT_CACHE_FILE, revision, dateKey))
    return WEATHER_TEXT_CACHE_FILE;
  if (validWeatherTextCache(WEATHER_TEXT_CACHE_BAK_FILE, revision, dateKey))
    return WEATHER_TEXT_CACHE_BAK_FILE;
  return nullptr;
}

void loadWeatherTextCacheState() {
  LittleFS.remove(WEATHER_TEXT_CACHE_TMP_FILE);
  int revision = -2;
  uint32_t dateKey = 0;
  if (lastGoodWeatherTextCache(&revision, &dateKey)) {
    weatherTextCachedRev = revision;
    weatherTextCachedDay = dateKey;
  }
}

bool drawWeatherTextCache() {
  int revision = -2;
  uint32_t dateKey = 0;
  const char *path = lastGoodWeatherTextCache(&revision, &dateKey);
  if (!path) {
    weatherTextCachedRev = -2;
    weatherTextCachedDay = 0;
    return false;
  }
  File file = LittleFS.open(path, "r");
  if (!file || !file.seek(WEATHER_TEXT_CACHE_HEADER_BYTES, SeekSet)) {
    if (file) file.close();
    weatherTextCachedRev = -2;
    weatherTextCachedDay = 0;
    return false;
  }
  const size_t rowBytes = (size_t)WEATHER_TEXT_W * 2;
  for (int row = 0; row < WEATHER_TEXT_H; row++) {
    if (file.read((uint8_t *)rowBuf, rowBytes) != rowBytes) {
      file.close();
      weatherTextCachedRev = -2;
      weatherTextCachedDay = 0;
      return false;
    }
    tft.pushImage(WEATHER_TEXT_X, WEATHER_TEXT_Y + row, WEATHER_TEXT_W, 1, rowBuf);
    yield();
  }
  file.close();
  weatherTextCachedRev = revision;
  weatherTextCachedDay = dateKey;
  return true;
}

bool promoteWeatherTextCache() {
  int revision = -2;
  if (!validWeatherTextCache(WEATHER_TEXT_CACHE_TMP_FILE, &revision)) {
    LittleFS.remove(WEATHER_TEXT_CACHE_TMP_FILE);
    return false;
  }
  bool currentValid = validWeatherTextCache(WEATHER_TEXT_CACHE_FILE);
  if (LittleFS.exists(WEATHER_TEXT_CACHE_FILE) && !currentValid &&
      !LittleFS.remove(WEATHER_TEXT_CACHE_FILE)) return false;
  if (currentValid) {
    if (LittleFS.exists(WEATHER_TEXT_CACHE_BAK_FILE) &&
        !LittleFS.remove(WEATHER_TEXT_CACHE_BAK_FILE)) return false;
    if (!LittleFS.rename(WEATHER_TEXT_CACHE_FILE, WEATHER_TEXT_CACHE_BAK_FILE)) return false;
  }
  if (!LittleFS.rename(WEATHER_TEXT_CACHE_TMP_FILE, WEATHER_TEXT_CACHE_FILE)) {
    if (currentValid) LittleFS.rename(WEATHER_TEXT_CACHE_BAK_FILE, WEATHER_TEXT_CACHE_FILE);
    return false;
  }
  if (!validWeatherTextCache(WEATHER_TEXT_CACHE_FILE, &revision)) {
    LittleFS.remove(WEATHER_TEXT_CACHE_FILE);
    if (currentValid) LittleFS.rename(WEATHER_TEXT_CACHE_BAK_FILE, WEATHER_TEXT_CACHE_FILE);
    return false;
  }
  weatherTextCachedRev = revision;
  validWeatherTextCache(WEATHER_TEXT_CACHE_FILE, nullptr, &weatherTextCachedDay);
  return true;
}

bool fetchWeatherTextFromBridge() {
  if (!lunarEnabled || WiFi.status() != WL_CONNECTED || bridgeHost.length() == 0) return false;
  WiFiClient client;
  HTTPClient http;
  String url = "http://" + bridgeHost + "/weather/text.raw";
  http.setTimeout(1500);
  if (!http.begin(client, url)) return false;
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    return false;
  }
  WiFiClient *stream = http.getStreamPtr();
  const size_t rowBytes = (size_t)WEATHER_TEXT_W * 2;
  File cache;
  bool cacheOk = false;
  uint32_t dateKey = weatherTextDateKey();
  uint32_t payloadCrc = 0xffffffffUL;
  if (fsAvailable && dateKey != 0) {
    LittleFS.remove(WEATHER_TEXT_CACHE_TMP_FILE);
    cache = LittleFS.open(WEATHER_TEXT_CACHE_TMP_FILE, "w");
    int32_t revision = weather.textRev;
    uint32_t crcPlaceholder = 0;
    cacheOk = cache && cache.write((const uint8_t *)&WEATHER_TEXT_CACHE_MAGIC,
                                   sizeof(WEATHER_TEXT_CACHE_MAGIC)) == sizeof(WEATHER_TEXT_CACHE_MAGIC) &&
              cache.write((const uint8_t *)&revision, sizeof(revision)) == sizeof(revision) &&
              cache.write((const uint8_t *)&dateKey, sizeof(dateKey)) == sizeof(dateKey) &&
              cache.write((const uint8_t *)&crcPlaceholder, sizeof(crcPlaceholder)) ==
                  sizeof(crcPlaceholder);
  }
  bool ok = true;
  for (int row = 0; row < WEATHER_TEXT_H; row++) {
    if (stream->readBytes((uint8_t *)rowBuf, rowBytes) != (int)rowBytes) {
      ok = false;
      break;
    }
    if (cacheOk) {
      payloadCrc = uzlib_crc32(rowBuf, rowBytes, payloadCrc);
      if (cache.write((const uint8_t *)rowBuf, rowBytes) != rowBytes) cacheOk = false;
    }
    tft.pushImage(WEATHER_TEXT_X, WEATHER_TEXT_Y + row, WEATHER_TEXT_W, 1, rowBuf);
    yield();
  }
  if (cache) {
    uint32_t finalCrc = ~payloadCrc;
    if (cacheOk && (!cache.seek(sizeof(uint32_t) * 3, SeekSet) ||
                    cache.write((const uint8_t *)&finalCrc, sizeof(finalCrc)) != sizeof(finalCrc))) {
      cacheOk = false;
    }
    cache.flush();
    cache.close();
  }
  http.end();
  if (ok && cacheOk) {
    if (!promoteWeatherTextCache()) Serial.println("[weather] lunar cache promotion failed");
  } else {
    LittleFS.remove(WEATHER_TEXT_CACHE_TMP_FILE);
  }
  return ok;
}

bool drawWeatherTextFallback() {
  if (lunarEnabled && drawWeatherTextCache()) {
    weatherLastDrawnStale = weatherDataStale();
    return true;
  }
  tft.fillRect(0, WEATHER_TEXT_Y, SCREEN_W, WEATHER_TEXT_H, TFT_BLACK);
  bool stale = weatherDataStale();
  weatherLastDrawnStale = stale;
  return false;
}

void drawClockFooter(bool force) {
  bool stale = weatherDataStale();
  uint32_t today = weatherTextDateKey();
  bool cacheDateChanged = today != 0 && weatherTextCachedDay != 0 && weatherTextCachedDay != today;
  if (!force && !cacheDateChanged && weatherTextDrawnRev == weather.textRev &&
      weatherLastDrawnStale == stale) return;
  weatherTextBitmapDrawn = drawWeatherTextFallback();
  weatherTextDrawnRev = weather.textRev;
  weatherTextNeedsFetch = lunarEnabled && weather.source == FEED_BRIDGE &&
                          (weather.textRev != weatherTextCachedRev ||
                           (today != 0 && weatherTextCachedDay != today));
}

// Network work is deliberately outside drawClockScreen(). The loop gives all
// Bridge requests a one-request-per-tick budget, so an unreachable Bridge can
// never stack text, weather and status timeouts in one pass.
bool maybeFetchWeatherText() {
  if (!lunarEnabled || WiFi.status() != WL_CONNECTED || bridgeHost.length() == 0) return false;
  if (!weatherTextNeedsFetch && weatherTextBitmapDrawn &&
      weather.textRev == weatherTextCachedRev &&
      (weatherTextDateKey() == 0 || weatherTextCachedDay == weatherTextDateKey())) return false;
  if (!weatherTextNeedsFetch && millis() - lastWeatherTextAttemptMs < 60000UL) return false;
  lastWeatherTextAttemptMs = millis();
  weatherTextNeedsFetch = false;
  if (fetchWeatherTextFromBridge()) {
    weatherTextDrawnRev = weather.textRev;
    weatherTextBitmapDrawn = true;
    weatherLastDrawnStale = weatherDataStale();
  } else {
    weatherTextBitmapDrawn = drawWeatherTextFallback();
  }
  return true;
}

constexpr uint16_t clockRgb(uint8_t red, uint8_t green, uint8_t blue) {
  return (uint16_t(red & 0xF8) << 8) | (uint16_t(green & 0xFC) << 3) | uint16_t(blue >> 3);
}

const uint16_t HARBOR_BG = clockRgb(7, 19, 21);
const uint16_t HARBOR_TEXT = clockRgb(239, 244, 240);
const uint16_t HARBOR_MUTED = clockRgb(126, 151, 146);
const uint16_t HARBOR_TEAL = clockRgb(73, 216, 210);
const uint16_t CLOCK_CORAL = clockRgb(255, 107, 87);
const uint16_t CLOCK_AMBER = clockRgb(244, 200, 90);
const uint16_t DIAL_BG = clockRgb(16, 17, 16);
const uint16_t DIAL_TEXT = clockRgb(238, 231, 214);
const uint16_t GRID_BG = clockRgb(231, 236, 232);
const uint16_t GRID_TEXT = clockRgb(16, 32, 28);
const uint16_t GRID_MUTED = clockRgb(97, 115, 109);
const uint16_t GRID_GREEN = clockRgb(8, 126, 104);
const uint16_t GRID_CORAL = clockRgb(215, 91, 63);

struct ClockUnitPoint { int8_t x, y; };
const ClockUnitPoint CLOCK_UNIT[60] PROGMEM = {
    {0,-100},{10,-99},{21,-98},{31,-95},{41,-91},{50,-87},{59,-81},{67,-74},{74,-67},{81,-59},
    {87,-50},{91,-41},{95,-31},{98,-21},{99,-10},{100,0},{99,10},{98,21},{95,31},{91,41},
    {87,50},{81,59},{74,67},{67,74},{59,81},{50,87},{41,91},{31,95},{21,98},{10,99},
    {0,100},{-10,99},{-21,98},{-31,95},{-41,91},{-50,87},{-59,81},{-67,74},{-74,67},{-81,59},
    {-87,50},{-91,41},{-95,31},{-98,21},{-99,10},{-100,0},{-99,-10},{-98,-21},{-95,-31},{-91,-41},
    {-87,-50},{-81,-59},{-74,-67},{-67,-74},{-59,-81},{-50,-87},{-41,-91},{-31,-95},{-21,-98},{-10,-99}
};

uint16_t clockBackgroundColor() {
  if (clockTheme == CLOCK_MINIMAL) return DIAL_BG;
  if (clockTheme == CLOCK_DASHBOARD) return GRID_BG;
  return HARBOR_BG;
}

uint16_t clockForegroundColor() {
  if (clockTheme == CLOCK_MINIMAL) return DIAL_TEXT;
  if (clockTheme == CLOCK_DASHBOARD) return GRID_TEXT;
  return HARBOR_TEXT;
}

void clockPoint(int index, int length, int centerX, int centerY, int &x, int &y) {
  index = (index % 60 + 60) % 60;
  int unitX = (int8_t)pgm_read_byte(&CLOCK_UNIT[index].x);
  int unitY = (int8_t)pgm_read_byte(&CLOCK_UNIT[index].y);
  x = centerX + unitX * length / 100;
  y = centerY + unitY * length / 100;
}

void drawClockHand(int index, int length, int width, uint16_t color) {
  const int centerX = 120, centerY = 85;
  int x, y;
  clockPoint(index, length, centerX, centerY, x, y);
  tft.drawLine(centerX, centerY, x, y, color);
  for (int offset = 1; offset <= width / 2; offset++) {
    tft.drawLine(centerX + offset, centerY, x + offset, y, color);
    tft.drawLine(centerX - offset, centerY, x - offset, y, color);
    tft.drawLine(centerX, centerY + offset, x, y + offset, color);
    tft.drawLine(centerX, centerY - offset, x, y - offset, color);
  }
}

void drawDialTicks() {
  for (int index = 0; index < 60; index += 5) {
    int x1, y1, x2, y2;
    int inner = index % 15 == 0 ? 51 : 55;
    clockPoint(index, inner, 120, 85, x1, y1);
    clockPoint(index, 62, 120, 85, x2, y2);
    tft.drawLine(x1, y1, x2, y2, index % 15 == 0 ? HARBOR_TEAL : HARBOR_MUTED);
  }
}

String clockMetric(float value) { return clockFloat(value, 0); }
String humidityMetric() { return weather.humidityPct >= 0 ? String(weather.humidityPct) + "%" : "--"; }
String aqiMetric() { return weather.aqi >= 0 ? String(weather.aqi) : "--"; }
String rangeMetric() { return clockMetric(weather.highC) + "/" + clockMetric(weather.lowC); }

void drawMetricColumn(int centerX, int labelY, int valueY, const char *label, const String &value,
                      uint16_t labelColor, uint16_t valueColor, uint16_t background) {
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(labelColor, background);
  tft.drawString(label, centerX, labelY, 2);
  tft.setTextColor(valueColor, background);
  int valueFont = value.length() > 5 ? 2 : 4;
  tft.drawString(value, centerX, valueY + (valueFont == 2 ? 4 : 0), valueFont);
}

void drawWeatherStaleDot(uint16_t background, int x, int y) {
  tft.fillCircle(x, y, 3, weatherDataStale() ? TFT_ORANGE : background);
}

int weatherIconIndex() {
  if (weather.skycon == "CLEAR_DAY") return WEATHER_ICON_CLEAR_DAY;
  if (weather.skycon == "CLEAR_NIGHT") return WEATHER_ICON_CLEAR_NIGHT;
  if (weather.skycon == "PARTLY_CLOUDY_DAY") return WEATHER_ICON_PARTLY_DAY;
  if (weather.skycon == "PARTLY_CLOUDY_NIGHT") return WEATHER_ICON_PARTLY_NIGHT;
  if (weather.skycon == "CLOUDY") return WEATHER_ICON_CLOUDY;
  if (weather.skycon == "LIGHT_RAIN") return WEATHER_ICON_LIGHT_RAIN;
  if (weather.skycon == "MODERATE_RAIN") return WEATHER_ICON_MODERATE_RAIN;
  if (weather.skycon == "HEAVY_RAIN") return WEATHER_ICON_HEAVY_RAIN;
  if (weather.skycon == "STORM_RAIN") return WEATHER_ICON_STORM_RAIN;
  if (weather.skycon == "LIGHT_SNOW") return WEATHER_ICON_LIGHT_SNOW;
  if (weather.skycon == "MODERATE_SNOW") return WEATHER_ICON_MODERATE_SNOW;
  if (weather.skycon == "HEAVY_SNOW" || weather.skycon == "STORM_SNOW")
    return WEATHER_ICON_HEAVY_SNOW;
  if (weather.skycon == "FOG") return WEATHER_ICON_FOG;
  if (weather.skycon.indexOf("HAZE") >= 0) return WEATHER_ICON_HAZE;
  if (weather.skycon == "DUST" || weather.skycon == "SAND") return WEATHER_ICON_DUST;
  if (weather.skycon == "WIND") return WEATHER_ICON_WIND;
  return WEATHER_ICON_UNKNOWN;
}

void drawWeatherIcon(int x, int y, uint16_t color) {
  const uint8_t *bitmap = WEATHER_ICON_BITMAPS + weatherIconIndex() * WEATHER_ICON_BYTES;
  tft.drawBitmap(x, y, bitmap, WEATHER_ICON_W, WEATHER_ICON_H, color);
}

void drawDigitalSeconds(const struct tm &local, int clearX, int clearY, int rightX, int textY,
                        uint16_t color, uint16_t background) {
  char seconds[3];
  snprintf(seconds, sizeof(seconds), "%02d", local.tm_sec);
  tft.fillRect(clearX, clearY, rightX - clearX + 4, 38, background);
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(color, background);
  tft.setTextSize(2);
  tft.drawString(seconds, rightX, textY, 2);
  tft.setTextSize(1);
}

void drawLargeTemperature(int x, int y, int font, uint16_t color, uint16_t background) {
  String value = clockMetric(weather.temperatureC);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(color, background);
  int width = tft.drawString(value, x, y, font);
  int unitX = x + width + 3;
  tft.drawCircle(unitX + 3, y + 8, 3, color);
  tft.drawString("C", unitX + 9, y + 11, 4);
}

void drawClassicClock(const struct tm &local, bool force, bool secondChanged) {
  static int lastMinute = -1;
  static int lastDay = -1;
  if (force || local.tm_mday != lastDay) {
    lastDay = local.tm_mday;
    tft.fillRect(0, 0, SCREEN_W, 31, HARBOR_BG);
    char date[6], weekday[4];
    strftime(date, sizeof(date), "%m/%d", &local);
    strftime(weekday, sizeof(weekday), "%a", &local);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(HARBOR_TEAL, HARBOR_BG);
    tft.drawString(WEATHER_LOCATION_LABEL, 8, 8, 2);
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(HARBOR_MUTED, HARBOR_BG);
    tft.drawString(weekday, 160, 8, 2);
    tft.setTextColor(HARBOR_TEXT, HARBOR_BG);
    tft.drawString(date, 232, 4, 4);
  }
  if (force || local.tm_min != lastMinute) {
    lastMinute = local.tm_min;
    tft.fillRect(0, 31, 190, 59, HARBOR_BG);
    char timeText[6];
    snprintf(timeText, sizeof(timeText), "%02d:%02d", local.tm_hour, local.tm_min);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(HARBOR_TEXT, HARBOR_BG);
    tft.drawString(timeText, 8, 32, 7);
  }
  if (secondChanged) {
    drawDigitalSeconds(local, 174, 37, 224, 39, CLOCK_CORAL, HARBOR_BG);
    tft.fillRect(8, 86, 224, 4, clockRgb(24, 48, 47));
    tft.fillRect(8, 86, (local.tm_sec + 1) * 224 / 60, 4, HARBOR_TEAL);
  }
  if (force) {
    tft.fillRect(0, 94, SCREEN_W, 112, HARBOR_BG);
    drawLargeTemperature(8, 101, 7, CLOCK_AMBER, HARBOR_BG);
    drawWeatherIcon(178, 104, HARBOR_TEAL);
    drawMetricColumn(42, 164, 180, "H/L", rangeMetric(), HARBOR_MUTED, HARBOR_TEXT, HARBOR_BG);
    drawMetricColumn(120, 164, 180, "HUM", humidityMetric(), HARBOR_MUTED, HARBOR_TEXT, HARBOR_BG);
    drawMetricColumn(198, 164, 180, "AQI", aqiMetric(), HARBOR_MUTED, HARBOR_TEXT, HARBOR_BG);
    drawWeatherStaleDot(HARBOR_BG, 106, 8);
  }
}

void drawMinimalClock(const struct tm &local, bool force, bool secondChanged) {
  static int lastMinute = -1;
  static int lastSecond = -1;
  static int lastDay = -1;
  bool dayChanged = force || local.tm_mday != lastDay;
  bool minuteChanged = force || dayChanged || local.tm_min != lastMinute;
  if (force) {
    tft.fillRect(0, 0, SCREEN_W, 206, DIAL_BG);
  }
  if (dayChanged) lastDay = local.tm_mday;
  if (minuteChanged) {
    tft.fillRect(0, 0, 60, 26, DIAL_BG);
    tft.fillRect(132, 0, 30, 26, DIAL_BG);
    tft.fillRect(166, 0, 74, 31, DIAL_BG);
    lastMinute = local.tm_min;
    tft.fillRect(56, 21, 128, 128, DIAL_BG);
    tft.drawCircle(120, 85, 63, clockRgb(63, 72, 68));
    drawDialTicks();
  } else if (secondChanged && lastSecond >= 0) {
    int x, y, tailX, tailY;
    clockPoint(lastSecond, 54, 120, 85, x, y);
    clockPoint(lastSecond + 30, 10, 120, 85, tailX, tailY);
    tft.drawLine(tailX, tailY, x, y, DIAL_BG);
    drawDialTicks();
  }
  if (minuteChanged || secondChanged) {
    int hourIndex = (local.tm_hour % 12) * 5 + local.tm_min / 12;
    drawClockHand(hourIndex, 32, 5, DIAL_TEXT);
    drawClockHand(local.tm_min, 46, 3, HARBOR_TEAL);
    int secondX, secondY, tailX, tailY;
    clockPoint(local.tm_sec, 54, 120, 85, secondX, secondY);
    clockPoint(local.tm_sec + 30, 10, 120, 85, tailX, tailY);
    tft.drawLine(tailX, tailY, secondX, secondY, CLOCK_CORAL);
    tft.fillCircle(120, 85, 4, DIAL_TEXT);
    tft.fillCircle(120, 85, 2, CLOCK_CORAL);
    lastSecond = local.tm_sec;
  }
  if (minuteChanged) {
    char date[6], weekday[4];
    strftime(date, sizeof(date), "%m/%d", &local);
    strftime(weekday, sizeof(weekday), "%a", &local);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(HARBOR_TEAL, DIAL_BG);
    tft.drawString(WEATHER_LOCATION_LABEL, 8, 8, 2);
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(HARBOR_MUTED, DIAL_BG);
    tft.drawString(weekday, 160, 8, 2);
    tft.setTextColor(DIAL_TEXT, DIAL_BG);
    tft.drawString(date, 232, 4, 4);
  }
  if (force) {
    tft.fillRect(0, 151, SCREEN_W, 55, DIAL_BG);
    drawLargeTemperature(8, 153, 6, CLOCK_AMBER, DIAL_BG);
    drawWeatherIcon(178, 154, HARBOR_TEAL);
    drawWeatherStaleDot(DIAL_BG, 106, 8);
  }
}

void drawDashboardClock(const struct tm &local, bool force, bool secondChanged) {
  static int lastMinute = -1;
  static int lastDay = -1;
  if (force || local.tm_min != lastMinute) {
    lastMinute = local.tm_min;
    tft.fillRect(0, 0, 192, 60, GRID_BG);
    char timeText[6];
    snprintf(timeText, sizeof(timeText), "%02d:%02d", local.tm_hour, local.tm_min);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(GRID_TEXT, GRID_BG);
    tft.drawString(timeText, 8, 7, 6);
  }
  if (secondChanged) {
    drawDigitalSeconds(local, 174, 14, 224, 17, GRID_CORAL, GRID_BG);
  }
  if (force || local.tm_mday != lastDay) {
    lastDay = local.tm_mday;
    tft.fillRect(0, 60, 110, 27, GRID_BG);
    tft.fillRect(116, 60, 46, 27, GRID_BG);
    tft.fillRect(166, 58, 74, 33, GRID_BG);
    char date[6], weekday[4];
    strftime(date, sizeof(date), "%m/%d", &local);
    strftime(weekday, sizeof(weekday), "%a", &local);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(GRID_GREEN, GRID_BG);
    tft.drawString(WEATHER_LOCATION_LABEL, 8, 67, 2);
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(GRID_MUTED, GRID_BG);
    tft.drawString(weekday, 160, 67, 2);
    tft.setTextColor(GRID_TEXT, GRID_BG);
    tft.drawString(date, 232, 62, 4);
    tft.drawFastHLine(8, 90, 224, clockRgb(178, 190, 183));
  }
  if (force) {
    tft.fillRect(0, 91, SCREEN_W, 115, GRID_BG);
    drawLargeTemperature(8, 96, 7, GRID_GREEN, GRID_BG);
    drawWeatherIcon(178, 101, GRID_GREEN);
    tft.drawFastVLine(80, 159, 43, clockRgb(190, 200, 194));
    tft.drawFastVLine(160, 159, 43, clockRgb(190, 200, 194));
    drawMetricColumn(40, 160, 177, "H/L", rangeMetric(), GRID_MUTED, GRID_TEXT, GRID_BG);
    drawMetricColumn(120, 160, 177, "HUM", humidityMetric(), GRID_MUTED, GRID_TEXT, GRID_BG);
    drawMetricColumn(200, 160, 177, "AQI", aqiMetric(), GRID_MUTED, GRID_TEXT, GRID_BG);
    drawWeatherStaleDot(GRID_BG, 232, 8);
  }
}

void drawClockScreen(bool force = false) {
  if (!clockChromeDrawn) {
    tft.fillScreen(clockBackgroundColor());
    tft.fillRect(0, 206, SCREEN_W, 34, TFT_BLACK);
    clockChromeDrawn = true;
    weatherTextDrawnRev = -2;
    weatherTextBitmapDrawn = false;
    weatherTextNeedsFetch = true;
    force = true;
  }
  if (!timeIsValid()) {
    uint16_t background = clockBackgroundColor();
    tft.fillRect(0, 20, SCREEN_W, 180, background);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(clockForegroundColor(), background);
    tft.drawString("SYNCING TIME", SCREEN_CX, 100, 4);
    drawClockFooter(true);
    return;
  }
  time_t now = time(nullptr);
  struct tm local;
  localtime_r(&now, &local);
  if (weatherDataStale() != weatherLastDrawnStale) clockDirty = true;
  force = force || clockDirty;
  bool secondChanged = force || local.tm_sec != lastClockSecond;
  if (clockTheme == CLOCK_MINIMAL) drawMinimalClock(local, force, secondChanged);
  else if (clockTheme == CLOCK_DASHBOARD) drawDashboardClock(local, force, secondChanged);
  else drawClassicClock(local, force, secondChanged);
  drawClockFooter(force);
  clockDirty = false;
  lastClockSecond = local.tm_sec;
}

// ---------- stock watchlist screen ----------

bool handleStockPayload(const String &payload) {
  bool hadStoredStock = stockSource != FEED_NONE;
  JsonDocument doc;
  if (deserializeJson(doc, payload)) return false;
  JsonArray arr = doc["stocks"];
  if (arr.isNull() || arr.size() == 0) return false;
  long payloadUpdatedAt = doc["updated_at"] | 0L;
  bool payloadStale = doc["stale"] | true;
  if (timeIsValid() && payloadUpdatedAt > 0) {
    long age = (long)time(nullptr) - payloadUpdatedAt;
    payloadStale = payloadStale || age > STOCK_STALE_AFTER_S || age < -300;
  }
  if (payloadStale || payloadUpdatedAt <= 0) {
    bridgeStockHealthy = false;
    bridgeStockReportedStale = true;
    markBridgeReachable();
    return false;
  }

  String bridgeSymbols[MAX_STOCKS];
  int bridgeSymbolCount = 0;
  JsonArray symbols = doc["symbols"];
  if (symbols.isNull() || symbols.size() == 0 || symbols.size() > MAX_STOCKS) return false;
  for (JsonVariant value : symbols) {
    String symbol = normalizeStockSymbol(value.as<String>());
    if (symbol.length() == 0) return false;
    bool duplicate = false;
    for (int i = 0; i < bridgeSymbolCount; i++) duplicate = duplicate || bridgeSymbols[i].equalsIgnoreCase(symbol);
    if (duplicate) return false;
    bridgeSymbols[bridgeSymbolCount++] = symbol;
  }
  StockRow parsed[MAX_STOCKS];
  int parsedCount = 0;
  for (JsonObject s : arr) {
    if (parsedCount >= MAX_STOCKS) break;
    String symbol = normalizeStockSymbol(s["symbol"] | "");
    String price = s["price"] | "";
    String pct = s["pct"] | "";
    if (symbol.length() == 0 || price.length() == 0 || price.length() > 18 || pct.length() == 0 || pct.length() > 16) {
      continue;
    }
    parsed[parsedCount].symbol = symbol;
    parsed[parsedCount].code = s["code"] | stockDisplayCode(symbol);
    parsed[parsedCount].price = price;
    parsed[parsedCount].pct = pct;
    parsed[parsedCount].up = constrain(s["up"] | 0, -1, 1);
    parsedCount++;
  }
  if (parsedCount == 0) return false;

  for (int i = 0; i < parsedCount; i++) {
    for (int j = i + 1; j < parsedCount; j++) {
      if (parsed[i].symbol.equalsIgnoreCase(parsed[j].symbol)) return false;
    }
  }

  String *requiredSymbols = stockSymbols;
  int requiredCount = stockSymbolCount;
  if (!stockConfigSyncPending && bridgeSymbolCount > 0) {
    requiredSymbols = bridgeSymbols;
    requiredCount = bridgeSymbolCount;
  }
  if (parsedCount != requiredCount) return false;
  for (int i = 0; i < requiredCount; i++) {
    bool found = false;
    for (int j = 0; j < parsedCount; j++) found = found || parsed[j].symbol.equalsIgnoreCase(requiredSymbols[i]);
    if (!found) return false;
  }

  bool configChanged = false;
  if (bridgeSymbolCount > 0) {
    bool same = stockSymbolListsEqual(stockSymbols, stockSymbolCount, bridgeSymbols, bridgeSymbolCount);
    if (stockConfigSyncPending && same) {
      stockConfigSyncPending = false;
      configChanged = true;
    } else if (!stockConfigSyncPending && !same) {
      applyStockSymbolList(bridgeSymbols, bridgeSymbolCount);
      configChanged = true;
    }
  }

  int applied = 0;
  for (int i = 0; i < parsedCount; i++) {
    int index = stockRowIndex(parsed[i].symbol);
    if (index < 0) continue;
    stocks[index] = parsed[i];
    applied++;
  }
  if (applied != stockCount) return false;
  stockNamesRev = doc["names_rev"] | -1;
  stockEverLoaded = true;
  stockSource = FEED_BRIDGE;
  stockUpdatedAt = payloadUpdatedAt;
  lastStockSuccessMs = millis();
  stockDirty = true;
  bridgeStockHealthy = true;
  bridgeStockReportedStale = false;
  stockDirectFallbackActive = false;
  markBridgeReachable();
  markOfflineStateDirty(configChanged || !hadStoredStock);
  return true;
}

bool parseFiniteNumber(String text, double &value) {
  text.trim();
  if (text.length() == 0 || text.length() > 24) return false;
  char *end = nullptr;
  value = strtod(text.c_str(), &end);
  return end != text.c_str() && *end == '\0' && isfinite(value);
}

String formatMarketPrice(double price) {
  if (price >= 10000.0) return String(price, 0);
  if (price >= 1000.0) return String(price, 1);
  if (price >= 1.0) return String(price, 2);
  if (price >= 0.01) return String(price, 5);
  return String(price, 8);
}

String formatMarketPercent(double pct) {
  return String(pct >= 0.0 ? "+" : "") + String(pct, 2) + "%";
}

bool delimitedField(const String &text, int wanted, String &field) {
  int start = 0;
  int index = 0;
  while (start <= (int)text.length()) {
    int end = text.indexOf('~', start);
    if (end < 0) end = text.length();
    if (index == wanted) {
      field = text.substring(start, end);
      return true;
    }
    if (end >= (int)text.length()) break;
    start = end + 1;
    index++;
  }
  return false;
}

bool applyDirectStockRows(const StockRow *rows, int count) {
  bool hadStoredStock = stockSource != FEED_NONE;
  int applied = 0;
  for (int i = 0; i < count; i++) {
    int index = stockRowIndex(rows[i].symbol);
    if (index < 0) continue;
    stocks[index] = rows[i];
    applied++;
  }
  if (applied == 0) return false;
  stockSource = FEED_DIRECT;
  lastStockSuccessMs = millis();
  stockEverLoaded = true;
  stockNamesDrawnRev = -1;
  stockDirty = true;
  markOfflineStateDirty(!hadStoredStock);
  return true;
}

void updateDirectStockTimestamp() {
  bool needsTencent = false, needsCrypto = false;
  for (int i = 0; i < stockSymbolCount; i++) {
    if (isCryptoSymbol(stockSymbols[i])) needsCrypto = true;
    else needsTencent = true;
  }
  if ((needsTencent && directTencentUpdatedAt <= 0) || (needsCrypto && directCryptoUpdatedAt <= 0)) {
    stockUpdatedAt = 0;
    return;
  }
  if (needsTencent && needsCrypto) stockUpdatedAt = min(directTencentUpdatedAt, directCryptoUpdatedAt);
  else stockUpdatedAt = needsTencent ? directTencentUpdatedAt : directCryptoUpdatedAt;
}

void markDirectProviderSuccess(bool crypto) {
  unsigned long nowMs = millis();
  long epoch = timeIsValid() ? (long)time(nullptr) : 0L;
  if (crypto) {
    lastDirectCryptoSuccessMs = nowMs;
    directCryptoUpdatedAt = epoch;
  } else {
    lastDirectTencentSuccessMs = nowMs;
    directTencentUpdatedAt = epoch;
  }
  updateDirectStockTimestamp();
}

bool parseTencentStocks(const String &payload, StockRow *rows, int &count) {
  count = 0;
  int lineStart = 0;
  while (lineStart < (int)payload.length() && count < MAX_STOCKS) {
    int lineEnd = payload.indexOf('\n', lineStart);
    if (lineEnd < 0) lineEnd = payload.length();
    String line = payload.substring(lineStart, lineEnd);
    line.trim();
    int eq = line.indexOf('=');
    if (line.startsWith("v_") && eq > 2) {
      String symbol = normalizeStockSymbol(line.substring(2, eq));
      int localIndex = stockRowIndex(symbol);
      int firstQuote = line.indexOf('"', eq);
      int lastQuote = line.lastIndexOf('"');
      if (localIndex >= 0 && firstQuote >= 0 && lastQuote > firstQuote) {
        String fields = line.substring(firstQuote + 1, lastQuote);
        String priceText, changeText, pctText;
        double price = 0, change = 0, pct = 0;
        if (delimitedField(fields, 3, priceText) && delimitedField(fields, 31, changeText) &&
            delimitedField(fields, 32, pctText) && parseFiniteNumber(priceText, price) &&
            parseFiniteNumber(changeText, change) && parseFiniteNumber(pctText, pct) && price > 0.0 &&
            fabs(pct) < 10000.0) {
          bool duplicate = false;
          for (int i = 0; i < count; i++) duplicate = duplicate || rows[i].symbol.equalsIgnoreCase(stockSymbols[localIndex]);
          if (duplicate) {
            lineStart = lineEnd + 1;
            continue;
          }
          rows[count].symbol = stockSymbols[localIndex];
          rows[count].code = stockDisplayCode(stockSymbols[localIndex]);
          rows[count].price = formatMarketPrice(price);
          rows[count].pct = formatMarketPercent(pct);
          rows[count].up = change > 0.0 ? 1 : (change < 0.0 ? -1 : 0);
          count++;
        }
      }
    }
    lineStart = lineEnd + 1;
  }
  return count > 0;
}

bool pollDirectTencentStocks() {
  String query;
  int requested = 0;
  for (int i = 0; i < stockSymbolCount; i++) {
    if (isCryptoSymbol(stockSymbols[i])) continue;
    if (query.length()) query += ',';
    query += stockSymbols[i];
    requested++;
  }
  if (query.length() == 0) return false;
  String payload;
  int code = 0;
  bool fetched = httpGetText("http://qt.gtimg.cn/q=" + query, DIRECT_HTTP_TIMEOUT_MS,
                             DIRECT_RESPONSE_MAX_BYTES, payload, code);
  StockRow rows[MAX_STOCKS];
  int count = 0;
  bool parsed = fetched && parseTencentStocks(payload, rows, count);
  bool applied = parsed && applyDirectStockRows(rows, count);
  bool complete = applied && count == requested;
  if (complete) markDirectProviderSuccess(false);
  else if (applied) updateDirectStockTimestamp();
  Serial.printf("[stock] Tencent GET -> %d complete=%d rows=%d/%d\n", code, complete, count, requested);
  return complete;
}

bool parseBinanceStocks(const String &payload, StockRow *rows, int &count) {
  count = 0;
  JsonDocument doc;
  if (deserializeJson(doc, payload)) return false;
  JsonArray tickers = doc.as<JsonArray>();
  if (tickers.isNull()) return false;
  for (JsonObject ticker : tickers) {
    if (count >= MAX_STOCKS) break;
    String symbol = normalizeStockSymbol(ticker["symbol"] | "");
    int localIndex = stockRowIndex(symbol);
    if (localIndex < 0 || !isCryptoSymbol(symbol)) continue;
    double price = 0, open = 0;
    if (!parseFiniteNumber(ticker["lastPrice"] | "", price) ||
        !parseFiniteNumber(ticker["openPrice"] | "", open) || price <= 0.0 || open <= 0.0) {
      continue;
    }
    double change = price - open;
    double pct = change / open * 100.0;
    if (!isfinite(pct) || fabs(pct) >= 10000.0) continue;
    bool duplicate = false;
    for (int i = 0; i < count; i++) duplicate = duplicate || rows[i].symbol.equalsIgnoreCase(stockSymbols[localIndex]);
    if (duplicate) continue;
    rows[count].symbol = stockSymbols[localIndex];
    rows[count].code = stockDisplayCode(stockSymbols[localIndex]);
    rows[count].price = formatMarketPrice(price);
    rows[count].pct = formatMarketPercent(pct);
    rows[count].up = change > 0.0 ? 1 : (change < 0.0 ? -1 : 0);
    count++;
  }
  return count > 0;
}

bool pollDirectBinanceStocks() {
  String encoded = "%5B";
  int requested = 0;
  for (int i = 0; i < stockSymbolCount; i++) {
    if (!isCryptoSymbol(stockSymbols[i])) continue;
    if (requested++) encoded += "%2C";
    encoded += "%22" + stockSymbols[i] + "%22";
  }
  encoded += "%5D";
  if (requested == 0) return false;
  String url = "http://data-api.binance.vision/api/v3/ticker/24hr?symbols=" + encoded + "&type=MINI";
  String payload;
  int code = 0;
  bool fetched = httpGetText(url, DIRECT_HTTP_TIMEOUT_MS, DIRECT_RESPONSE_MAX_BYTES, payload, code);
  StockRow rows[MAX_STOCKS];
  int count = 0;
  bool parsed = fetched && parseBinanceStocks(payload, rows, count);
  bool applied = parsed && applyDirectStockRows(rows, count);
  bool complete = applied && count == requested;
  if (complete) markDirectProviderSuccess(true);
  else if (applied) updateDirectStockTimestamp();
  Serial.printf("[stock] Binance GET -> %d complete=%d rows=%d/%d\n", code, complete, count, requested);
  return complete;
}

// Streams the Mac-rendered name strips and blits one per row (top line,
// right of the ASCII code). Wired-only mode has no HTTP: codes still show.
bool drawStockNames() {
  if (WiFi.status() != WL_CONNECTED || bridgeHost.length() == 0) return false;
  WiFiClient client;
  HTTPClient http;
  String url = "http://" + bridgeHost + "/stock/names.raw";
  http.setTimeout(BRIDGE_HTTP_TIMEOUT_MS);
  if (!http.begin(client, url)) return false;
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    return false;
  }
  WiFiClient *stream = http.getStreamPtr();
  uint8_t cnt = 0;
  if (stream->readBytes(&cnt, 1) != 1) {
    http.end();
    return false;
  }
  if (cnt > MAX_STOCKS) {
    http.end();
    return false;
  }
  const size_t rowBytes = (size_t)STOCK_NAME_W * 2;
  bool ok = true;
  for (int i = 0; i < cnt && ok; i++) {
    int y0 = 10 + i * 54;
    for (int r = 0; r < STOCK_NAME_H; r++) {
      if (stream->readBytes((uint8_t *)rowBuf, rowBytes) != (int)rowBytes) {
        ok = false;
        break;
      }
      if (i < stockCount) tft.pushImage(70, y0 + r, STOCK_NAME_W, 1, rowBuf);
      yield();
    }
  }
  http.end();
  return ok;
}

bool pollBridgeStock() {
  if (WiFi.status() != WL_CONNECTED || bridgeHost.length() == 0) return false;
  String url = "http://" + bridgeHost + "/stock";
  String payload;
  int code = 0;
  bool ok = httpGetText(url, BRIDGE_HTTP_TIMEOUT_MS, DIRECT_RESPONSE_MAX_BYTES, payload, code) &&
            handleStockPayload(payload);
  bridgeStockHealthy = ok;
  Serial.printf("[stock] bridge GET -> %d parsed=%d\n", code, ok);
  return ok;
}

// 54px per row: small grey code on top, big font-4 price (white) on the left
// and change% on the right - green rising / red falling.
// Rows repaint only when their text changes, same trick as everywhere else.
void drawStockScreen() {
  if (!stockChromeDrawn) {
    tft.fillScreen(TFT_BLACK);
    stockChromeDrawn = true;
    for (int i = 0; i < MAX_STOCKS; i++) {
      stockLastCode[i] = "\x01"; // force repaint
      stockLastVal[i] = "\x01";
    }
    stockNamesDrawnRev = -1;
    stockLastFooter = "";
  }
  stockDirty = false;

  if (stockCount == 0) {
    if (stockLastCode[0] != "") {
      for (int i = 0; i < MAX_STOCKS; i++) {
        stockLastCode[i] = "";
        stockLastVal[i] = "";
      }
      tft.fillRect(0, 0, SCREEN_W, 226, TFT_BLACK);
      tft.setTextDatum(TC_DATUM);
      tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
      tft.drawString(stockEverLoaded ? "No stocks configured" : "Waiting for bridge...", SCREEN_CX, 100, 2);
      if (stockEverLoaded) tft.drawString("Mac menu: Set watchlist", SCREEN_CX, 124, 2);
    }
    return;
  }

  TFT_eSprite valueSprite(&tft);
  bool valueSpriteAttempted = false;
  bool valueSpriteReady = false;

  for (int i = 0; i < MAX_STOCKS; i++) {
    int y0 = 10 + i * 54;
    bool has = i < stockCount;
    // top line (code + name strip) and value line refresh independently, so
    // a price tick never wipes the name bitmap
    String codeKey = has ? stocks[i].code : "";
    if (codeKey != stockLastCode[i]) {
      stockLastCode[i] = codeKey;
      tft.fillRect(0, y0, SCREEN_W, 17, TFT_BLACK);
      stockNamesDrawnRev = -1; // strip area wiped: re-fetch names
      if (has) {
        tft.setTextDatum(TL_DATUM);
        tft.setTextColor(0x7BEF, TFT_BLACK);
        tft.drawString(stocks[i].code, 14, y0, 2);
      }
    }
    String valKey = has ? stocks[i].price + "|" + stocks[i].pct + "|" + String(stocks[i].up) : "";
    if (valKey != stockLastVal[i]) {
      stockLastVal[i] = valKey;
      if (!valueSpriteAttempted) {
        valueSpriteAttempted = true;
        valueSprite.setColorDepth(8);
        valueSpriteReady = valueSprite.createSprite(SCREEN_W, 36) != nullptr;
      }
      if (valueSpriteReady) {
        valueSprite.fillSprite(TFT_BLACK);
        if (has) {
          valueSprite.setTextDatum(TL_DATUM);
          valueSprite.setTextColor(TFT_WHITE, TFT_BLACK);
          valueSprite.drawString(stocks[i].price, 14, 0, 4);
          uint16_t pc = stocks[i].up > 0 ? TFT_GREEN : (stocks[i].up < 0 ? TFT_RED : TFT_LIGHTGREY);
          valueSprite.setTextDatum(TR_DATUM);
          valueSprite.setTextColor(pc, TFT_BLACK);
          valueSprite.drawString(stocks[i].pct, 226, 0, 4);
        }
        valueSprite.pushSprite(0, y0 + 18);
      } else {
        // Low-memory fallback keeps the screen usable if the temporary 8-bit
        // sprite cannot be allocated.
        tft.fillRect(0, y0 + 18, SCREEN_W, 36, TFT_BLACK);
        if (has) {
          tft.setTextDatum(TL_DATUM);
          tft.setTextColor(TFT_WHITE, TFT_BLACK);
          tft.drawString(stocks[i].price, 14, y0 + 18, 4);
          uint16_t pc = stocks[i].up > 0 ? TFT_GREEN : (stocks[i].up < 0 ? TFT_RED : TFT_LIGHTGREY);
          tft.setTextDatum(TR_DATUM);
          tft.setTextColor(pc, TFT_BLACK);
          tft.drawString(stocks[i].pct, 226, y0 + 18, 4);
        }
      }
    }
  }

  if (valueSpriteReady) valueSprite.deleteSprite();

  String footer;
  if (stockSource == FEED_BRIDGE) footer = "BRIDGE";
  else if (stockSource == FEED_DIRECT) footer = "DIRECT";
  else if (stockSource == FEED_CACHE) footer = "CACHE";
  else footer = "WAITING";
  if (stockConfigSyncPending) footer += "  SYNC";
  bool stale = stockDataStale();
  if (stale) footer += "  STALE";
  if (footer != stockLastFooter) {
    stockLastFooter = footer;
    tft.fillRect(0, 226, SCREEN_W, 14, TFT_BLACK);
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(stale ? TFT_ORANGE : 0x7BEF, TFT_BLACK);
    tft.drawString(footer, SCREEN_CX, 228, 1);
  }
  stockLastDrawnStale = stale;
}

// ---------- WiFi / bridge polling ----------

WiFiManager wifiManager; // global: the config portal now runs non-blocking in loop()

void configModeCallback(WiFiManager *wm) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("WiFi setup needed", 8, 32, 2);
  tft.drawString("Connect phone to AP:", 8, 62, 2);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString(WIFI_PORTAL_AP_NAME, 8, 87, 2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("then open 192.168.4.1", 8, 117, 2);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("Or: plug into the computer", 8, 155, 2);
  tft.drawString("via USB - no WiFi needed", 8, 178, 2);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString("Firmware v" FW_VERSION, 8, 215, 2);
}

// Non-blocking: with saved credentials this still waits ~10s for the join,
// but a missing/failed WiFi no longer traps boot in the portal - the portal
// keeps running from loop() while the USB serial link can take over the
// screen (wired mode for APs with client isolation).
void setupWiFi() {
  wifiManager.setAPCallback(configModeCallback);
  wifiManager.setConfigPortalBlocking(false);

  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Connecting WiFi...", 8, 100, 2);

  Serial.println("[wifi] starting WiFiManager autoConnect (non-blocking portal)...");
  bool ok = wifiManager.autoConnect(WIFI_PORTAL_AP_NAME);
  Serial.printf("[wifi] autoConnect result=%d ssid=%s ip=%s\n", ok, WiFi.SSID().c_str(),
                WiFi.localIP().toString().c_str());
  Serial.printf("[wifi] bridge host = '%s'\n", bridgeHost.c_str());
}

bool parseStatusJson(const String &payload) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) return false;

  JsonObject c = doc["claude"];
  if (!c.isNull()) {
    claudeStatus.status = c["status"] | "unknown";
    claudeStatus.tokensToday = c["tokens_today"] | 0;
    claudeStatus.sessionMin = c["session_min"] | 0;
    claudeStatus.sessionWindowMin = c["session_window_min"] | 300;
    claudeStatus.fiveHourPct = c["five_hour_pct"] | -1.0;
    claudeStatus.fiveHourResetMin = c["five_hour_reset_min"] | -1;
    claudeStatus.sevenDayPct = c["seven_day_pct"] | -1.0;
    claudeStatus.sevenDayResetMin = c["seven_day_reset_min"] | -1;
    claudeStatus.needsInput = c["needs_input"] | false;
  }

  JsonObject x = doc["codex"];
  if (!x.isNull()) {
    codexStatus.status = x["status"] | "unknown";
    codexStatus.tokensToday = x["tokens_today"] | 0;
    codexStatus.primaryPct = x["primary_pct"] | -1.0;
    codexStatus.primaryResetMin = x["primary_reset_min"] | -1;
    codexStatus.weeklyPct = x["weekly_pct"] | -1.0;
    codexStatus.weeklyResetMin = x["weekly_reset_min"] | -1;
    codexStatus.needsInput = x["needs_input"] | false;
  }
  return true;
}

// AUTO remains the Claude/Codex activity view. Weather clock is an explicit
// page so music playback can no longer pull the screen away from the user.
DisplayMode effectiveMode() { return displayMode; }

bool pollBridge() {
  if (WiFi.status() != WL_CONNECTED || bridgeHost.length() == 0) {
    Serial.printf("[bridge] skip poll: wifi=%d host='%s'\n", WiFi.status() == WL_CONNECTED, bridgeHost.c_str());
    return false;
  }

  String url = "http://" + bridgeHost + BRIDGE_DEFAULT_PATH;
  String payload;
  int code = 0;
  bool fetched = httpGetText(url, BRIDGE_HTTP_TIMEOUT_MS, DIRECT_RESPONSE_MAX_BYTES, payload, code);
  Serial.printf("[bridge] GET %s -> %d\n", url.c_str(), code);
  bool ok = false;
  if (fetched) {
    if (parseStatusJson(payload)) {
      ok = true;
      lastSuccessMs = millis();
      everPolled = true;
      markBridgeReachable();
      Serial.printf("[bridge] claude=%s tok=%ld | codex=%s tok=%ld primary=%.0f%%\n",
                    claudeStatus.status.c_str(), claudeStatus.tokensToday,
                    codexStatus.status.c_str(), codexStatus.tokensToday, codexStatus.primaryPct);
    } else {
      Serial.println("[bridge] JSON parse failed");
    }
  } else {
    claudeStatus.status = "offline";
    codexStatus.status = "offline";
  }
  DisplayMode eff = effectiveMode();
  if (eff != MODE_NET && eff != MODE_CLOCK && eff != MODE_STOCK) {
    // Only a real app switch clears the screen; a plain data refresh paints
    // in place so the poll doesn't flash the whole display.
    if (updateActiveApp()) drawActiveApp();
    else refreshActiveApp();
  }
  return ok;
}

// ---------- wired (USB serial) bridge link ----------
// Fallback for WiFi networks with client isolation (device can't reach the
// bridge over LAN) - or for skipping WiFi setup entirely: when the clock is
// plugged into the computer over USB, the bridge pushes the same /status and
// /net payloads down the CH340 serial line as newline-terminated frames:
//   bridge -> device:  #HELLO  #STATUS {json}  #NET {json}  #STOCK {json}
//                      #WEATHER {json}  #CMD {json}
//   device -> bridge:  #DEVICE {"name":"aiclock","fw":"x.y.z"}
// Everything else the device prints (logs) is ignored by the bridge.
unsigned long lastSerialFrameMs = 0;
bool wiredEverLinked = false;
char serialLine[1600]; // biggest frame is #STATUS at ~600 bytes
size_t serialLineLen = 0;

bool wiredActive() { return wiredEverLinked && (millis() - lastSerialFrameMs) < 15000UL; }

void markWiredFrame() {
  lastSerialFrameMs = millis();
  wiredEverLinked = true;
}

// First data over either transport replaces the boot/portal screen.
void showMainUiIfNeeded() {
  if (mainUiShown) return;
  mainUiShown = true;
  drawStaticChrome();
  updateActiveApp();
  drawActiveApp();
}

void handleSerialFrame(char *line) {
  if (!strncmp(line, "#HELLO", 6)) {
    markWiredFrame();
    Serial.printf("#DEVICE {\"name\":\"aiclock\",\"fw\":\"%s\"}\n", FW_VERSION);
    return;
  }
  if (!strncmp(line, "#STATUS ", 8)) {
    if (parseStatusJson(String(line + 8))) {
      markWiredFrame();
      lastSuccessMs = millis();
      everPolled = true;
      markBridgeReachable();
      showMainUiIfNeeded();
      DisplayMode eff = effectiveMode();
      if (eff != MODE_NET && eff != MODE_CLOCK && eff != MODE_STOCK) {
        if (updateActiveApp()) drawActiveApp();
        else refreshActiveApp();
      }
    }
    return;
  }
  if (!strncmp(line, "#NET ", 5)) {
    if (handleNetPayload(String(line + 5))) {
      markWiredFrame();
      markBridgeReachable();
    }
    return;
  }
  if (!strncmp(line, "#STOCK ", 7)) {
    if (handleStockPayload(String(line + 7))) markWiredFrame();
    else bridgeStockHealthy = false;
    return;
  }
  if (!strncmp(line, "#WEATHER ", 9)) {
    if (handleWeatherPayload(String(line + 9))) markWiredFrame();
    else bridgeWeatherHealthy = false;
    return;
  }
  if (!strncmp(line, "#CMD ", 5)) {
    JsonDocument doc;
    if (deserializeJson(doc, line + 5)) return;
    markWiredFrame();
    int previousBrightness = brightness;
    bool previousScreenOff = manualScreenOff;
    bool previousScheduleWakeOverride = screenScheduleWakeOverride;
    DisplayMode previousMode = displayMode;
    bool brightnessCommand = false;
    bool persistSettings = false;
    int requestedBrightness = 0;
    if (doc["brightness"].is<int>()) {
      requestedBrightness = constrain(doc["brightness"].as<int>(), 0, 100);
      brightnessCommand = true;
      if (requestedBrightness == 0) {
        manualScreenOff = true;
        screenScheduleWakeOverride = false;
      } else {
        brightness = requestedBrightness;
        wakeScreenTemporarily();
        persistSettings = true;
      }
    }
    const char *mode = doc["display"] | (const char *)nullptr;
    if (mode) {
      String m(mode);
      if (setDisplayMode(m)) persistSettings = true;
      else Serial.printf("[serial] ignored invalid display mode '%s'\n", mode);
      // The effectiveMode transition handler in loop() repaints the chrome.
    }
    if (persistSettings && !saveDeviceSettings()) {
      brightness = previousBrightness;
      manualScreenOff = previousScreenOff;
      screenScheduleWakeOverride = previousScheduleWakeOverride;
      displayMode = previousMode;
      Serial.println("[serial] command rejected: settings were not persisted");
      return;
    }
    if (brightnessCommand) {
      applyBrightness();
      if (requestedBrightness > 0) saveBrightness();
    }
    return;
  }
}

// Drains the UART, splitting on newlines; frames start with '#', everything
// else (line noise, echoes) is dropped.
void pumpSerial() {
  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch == '\n' || ch == '\r') {
      if (serialLineLen > 0 && serialLine[0] == '#') {
        serialLine[serialLineLen] = 0;
        handleSerialFrame(serialLine);
      }
      serialLineLen = 0;
    } else if (serialLineLen < sizeof(serialLine) - 1) {
      serialLine[serialLineLen++] = ch;
    } else {
      serialLineLen = 0; // oversized line: drop it
    }
  }
}

// ---------- web admin ----------

void handleRoot() { webServer.send_P(200, "text/html; charset=utf-8", ADMIN_PAGE); }

bool requireWritableFilesystem() {
  if (fsAvailable) return true;
  webServer.send(503, "text/plain", "filesystem unavailable; data preserved");
  return false;
}

void handleSave() {
  if (!requireWritableFilesystem()) return;
  String newHost = webServer.arg("bridge");
  newHost.trim();
  if (!saveBridgeHost(newHost)) {
    webServer.send(507, "text/plain", "bridge host was not persisted");
    return;
  }
  bridgeHost = newHost;
  Serial.printf("[web] bridge host updated to '%s'\n", bridgeHost.c_str());
  webServer.sendHeader("Location", "/");
  webServer.send(303);
}

// ---------- JSON API for the Mac app and device console ----------

long retrySeconds(unsigned long lastMs, unsigned long intervalMs) {
  if (lastMs == 0) return 0;
  unsigned long elapsed = millis() - lastMs;
  if (elapsed >= intervalMs) return 0;
  return (long)((intervalMs - elapsed + 999UL) / 1000UL);
}

long stockDirectRetrySeconds() {
  long next = LONG_MAX;
  for (int i = 0; i < stockSymbolCount; i++) {
    long value = isCryptoSymbol(stockSymbols[i])
                     ? retrySeconds(lastDirectCryptoAttemptMs, directCryptoRetryMs)
                     : retrySeconds(lastDirectTencentAttemptMs, directTencentRetryMs);
    next = min(next, value);
  }
  return next == LONG_MAX ? -1 : next;
}

void appendStockState(JsonObject target) {
  target["source"] = feedSourceName(stockSource);
  target["updated_at"] = stockUpdatedAt;
  target["age_s"] = feedAgeSeconds(stockUpdatedAt);
  target["stale"] = stockDataStale();
  target["sync_pending"] = stockConfigSyncPending;
  target["bridge_healthy"] = bridgeStockHealthy;
  target["direct_retry_s"] = stockDirectRetrySeconds();
}

void handleApiInfo() {
  JsonDocument doc;
  doc["ip"] = WiFi.localIP().toString();
  doc["device_id"] = WiFi.macAddress();
  doc["ssid"] = WiFi.SSID();
  doc["bridge"] = bridgeHost;
  doc["mode"] = displayModeName(displayMode);           // configured mode
  doc["clock_theme"] = clockThemeName(clockTheme);
  doc["effective"] = displayModeName(effectiveMode());   // what's on screen now
  doc["showing"] = (currentApp == APP_CLAUDE) ? "claude" : "codex";
  doc["last_update_s"] = everPolled ? (long)((millis() - lastSuccessMs) / 1000) : -1;
  doc["sprite_rev"] = spriteRev;
  doc["brightness"] = brightness;
  doc["effective_brightness"] = effectiveBrightness();
  doc["screen_on"] = screenIsOn();
  doc["manual_screen_off"] = manualScreenOff;
  doc["screen_schedule_enabled"] = screenScheduleEnabled;
  doc["screen_schedule_active"] = scheduledScreenOff;
  doc["screen_schedule_wake_override"] = screenScheduleWakeOverride;
  doc["screen_off_start_min"] = screenOffStartMin;
  doc["screen_off_end_min"] = screenOffEndMin;
  doc["qweather_configured"] = qweatherConfigured();
  doc["qweather_tls_mfln"] = qweatherMflnState;
  doc["qweather_tls_min_heap"] = minQWeatherTlsHeap;
  doc["qweather_tls_min_block"] = minQWeatherTlsBlock;
  doc["night_active"] = nightActive;
  doc["time_valid"] = timeIsValid();
  doc["epoch"] = timeIsValid() ? (long)time(nullptr) : 0;
  doc["free_heap"] = ESP.getFreeHeap();
  doc["min_free_heap"] = minFreeHeap == UINT32_MAX ? ESP.getFreeHeap() : minFreeHeap;
  doc["max_free_block"] = ESP.getMaxFreeBlockSize();
  doc["uptime_s"] = millis() / 1000UL;
  doc["reset_reason"] = ESP.getResetReason();
  doc["wired"] = wiredActive(); // true = data currently arrives over USB serial
  doc["bridge_recent"] = wiredActive() || bridgeRecentlyReachable(millis());
  doc["fw"] = FW_VERSION;
  doc["fs_mounted"] = fsAvailable;
  FSInfo fsInfo;
  if (fsAvailable && LittleFS.info(fsInfo)) {
    doc["fs_used"] = fsInfo.usedBytes;
    doc["fs_total"] = fsInfo.totalBytes;
  }
  JsonObject w = doc["weather"].to<JsonObject>();
  w["available"] = weather.available;
  w["location"] = weather.location;
  if (weather.available) {
    w["temperature_c"] = weather.temperatureC;
    w["apparent_c"] = weather.apparentC;
    w["humidity_pct"] = weather.humidityPct;
    w["weather_code"] = weather.weatherCode;
    w["skycon"] = weather.skycon;
    w["aqi"] = weather.aqi;
    w["air_quality_zh"] = weather.airQualityZh;
    w["provider"] = weather.provider;
    w["condition_zh"] = weather.conditionZh;
    w["condition_en"] = weather.conditionEn;
    w["wind_kmh"] = weather.windKmh;
    w["high_c"] = weather.highC;
    w["low_c"] = weather.lowC;
  }
  w["lunar_zh"] = weather.lunarZh;
  w["updated_at"] = weather.updatedAt;
  w["age_s"] = feedAgeSeconds(weather.updatedAt);
  w["stale"] = weatherDataStale();
  w["source"] = feedSourceName(weather.source);
  w["bridge_healthy"] = bridgeWeatherHealthy;
  w["direct_retry_s"] = retrySeconds(lastDirectWeatherAttemptMs, directWeatherRetryMs);
  JsonObject stock = doc["stock"].to<JsonObject>();
  appendStockState(stock);
  JsonObject c = doc["claude"].to<JsonObject>();
  c["status"] = claudeStatus.status;
  c["custom_sprite"] = claudeCustom;
  c["w"] = CLAUDE_SPRITE_W;
  c["h"] = CLAUDE_SPRITE_H;
  JsonObject x = doc["codex"].to<JsonObject>();
  x["status"] = codexStatus.status;
  x["custom_sprite"] = codexCustom;
  x["w"] = CODEX_SPRITE_W;
  x["h"] = CODEX_SPRITE_H;
  String out;
  serializeJson(doc, out);
  webServer.send(200, "application/json", out);
}

void handleApiDisplay() {
  if (!requireWritableFilesystem()) return;
  String mode = webServer.arg("mode");
  DisplayMode previousMode = displayMode;
  if (!setDisplayMode(mode)) {
    webServer.send(400, "text/plain", "mode must be auto|claude|codex|net|weather|stock");
    return;
  }
  if (!saveDeviceSettings()) {
    displayMode = previousMode;
    webServer.send(507, "text/plain", "settings were not persisted");
    return;
  }
  Serial.printf("[api] display mode = %s\n", mode.c_str());
  if (displayMode == MODE_NET) {
    netChromeDrawn = false;
    lastNetPollMs = 0; // poll + draw on the next loop tick
  } else if (displayMode == MODE_CLOCK) {
    clockChromeDrawn = false;
    lastWeatherPollMs = 0;
  } else if (displayMode == MODE_STOCK) {
    stockChromeDrawn = false;
    lastStockPollMs = 0; // poll + draw on the next loop tick
  } else {
    updateActiveApp();
    drawActiveApp(); // unconditional: also repaints over a previous net chart
  }
  webServer.send(200, "text/plain", "ok");
}

void handleApiBrightness() {
  String levelArg = webServer.arg("level");
  if (levelArg.length() == 0) {
    webServer.send(400, "text/plain", "missing level (0-100)");
    return;
  }
  int level = levelArg.toInt();
  if (level < 0) level = 0;
  if (level > 100) level = 100;
  if (level > 0 && !requireWritableFilesystem()) return;
  int previousBrightness = brightness;
  bool previousScreenOff = manualScreenOff;
  bool previousScheduleWakeOverride = screenScheduleWakeOverride;
  if (level == 0) {
    manualScreenOff = true;
    screenScheduleWakeOverride = false;
  } else {
    brightness = level;
    wakeScreenTemporarily();
  }
  applyBrightness();
  if (level > 0) {
    if (!saveDeviceSettings()) {
      brightness = previousBrightness;
      manualScreenOff = previousScreenOff;
      screenScheduleWakeOverride = previousScheduleWakeOverride;
      applyBrightness();
      webServer.send(507, "text/plain", "brightness was not persisted");
      return;
    }
    saveBrightness();
  }
  Serial.printf("[api] brightness = %d screen=%d\n", brightness, screenIsOn());
  webServer.send(200, "text/plain", "ok");
}

void handleApiScreen() {
  String state = webServer.arg("state");
  if (state == "off") {
    manualScreenOff = true;
    screenScheduleWakeOverride = false;
  } else if (state == "on") {
    wakeScreenTemporarily();
  } else if (state == "auto") {
    manualScreenOff = false;
    screenScheduleWakeOverride = false;
  } else {
    webServer.send(400, "text/plain", "state must be on|off|auto");
    return;
  }
  applyBrightness();
  webServer.send(200, "application/json", "{\"ok\":true}");
}

void sendSettingsJson() {
  JsonDocument doc;
  doc["clock_theme"] = clockThemeName(clockTheme);
  doc["lunar_enabled"] = lunarEnabled;
  doc["night_enabled"] = nightEnabled;
  doc["night_start_min"] = nightStartMin;
  doc["night_end_min"] = nightEndMin;
  doc["night_brightness"] = nightBrightness;
  doc["screen_schedule_enabled"] = screenScheduleEnabled;
  doc["screen_off_start_min"] = screenOffStartMin;
  doc["screen_off_end_min"] = screenOffEndMin;
  doc["qweather_configured"] = qweatherConfigured();
  doc["qweather_host"] = qweatherApiHost;
  doc["ntp_server"] = ntpServer;
  String out;
  serializeJson(doc, out);
  webServer.send(200, "application/json", out);
}

bool apiBool(const String &value) { return value == "1" || value == "true" || value == "on"; }

bool parseBoolSetting(const String &value, bool &result) {
  if (value == "1" || value == "true" || value == "on") {
    result = true;
    return true;
  }
  if (value == "0" || value == "false" || value == "off") {
    result = false;
    return true;
  }
  return false;
}

bool parseMinuteSetting(const String &value, int &minute) {
  if (value.length() == 0 || value.length() > 4) return false;
  for (size_t i = 0; i < value.length(); i++) {
    if (!isdigit(static_cast<unsigned char>(value[i]))) return false;
  }
  long parsed = value.toInt();
  if (parsed < 0 || parsed > 1439) return false;
  minute = (int)parsed;
  return true;
}

bool validNtpHost(const String &host) {
  if (host.length() == 0 || host.length() > 63) return false;
  for (size_t i = 0; i < host.length(); i++) {
    char c = host[i];
    if (!isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '-') return false;
  }
  return true;
}

void handleApiSettingsPost() {
  if (!requireWritableFilesystem()) return;
  ClockTheme previousTheme = clockTheme;
  bool previousLunarEnabled = lunarEnabled;
  bool previousNightEnabled = nightEnabled;
  int previousNightStartMin = nightStartMin;
  int previousNightEndMin = nightEndMin;
  int previousNightBrightness = nightBrightness;
  bool previousScreenScheduleEnabled = screenScheduleEnabled;
  int previousScreenOffStartMin = screenOffStartMin;
  int previousScreenOffEndMin = screenOffEndMin;
  String previousQWeatherKey = qweatherApiKey;
  String previousQWeatherHost = qweatherApiHost;
  String previousNtpServer = ntpServer;
  ClockTheme nextTheme = clockTheme;
  bool nextLunarEnabled = lunarEnabled;
  bool nextNightEnabled = nightEnabled;
  int nextNightStartMin = nightStartMin;
  int nextNightEndMin = nightEndMin;
  int nextNightBrightness = nightBrightness;
  bool nextScreenScheduleEnabled = screenScheduleEnabled;
  int nextScreenOffStartMin = screenOffStartMin;
  int nextScreenOffEndMin = screenOffEndMin;
  String nextQWeatherKey = qweatherApiKey;
  String nextQWeatherHost = qweatherApiHost;
  String nextNtpServer = ntpServer;

  String theme = webServer.arg("clock_theme");
  if (theme.length()) {
    if (theme == "classic") nextTheme = CLOCK_CLASSIC;
    else if (theme == "minimal") nextTheme = CLOCK_MINIMAL;
    else if (theme == "dashboard") nextTheme = CLOCK_DASHBOARD;
    else {
      webServer.send(400, "text/plain", "clock_theme must be classic|minimal|dashboard");
      return;
    }
  }
  if (webServer.hasArg("lunar_enabled")) nextLunarEnabled = apiBool(webServer.arg("lunar_enabled"));
  if (webServer.hasArg("night_enabled")) nextNightEnabled = apiBool(webServer.arg("night_enabled"));
  if (webServer.hasArg("night_start_min")) nextNightStartMin = constrain(webServer.arg("night_start_min").toInt(), 0, 1439);
  if (webServer.hasArg("night_end_min")) nextNightEndMin = constrain(webServer.arg("night_end_min").toInt(), 0, 1439);
  if (webServer.hasArg("night_brightness")) nextNightBrightness = constrain(webServer.arg("night_brightness").toInt(), 1, 50);
  if (webServer.hasArg("screen_schedule_enabled")) {
    if (!parseBoolSetting(webServer.arg("screen_schedule_enabled"), nextScreenScheduleEnabled)) {
      webServer.send(400, "text/plain", "screen_schedule_enabled must be 0 or 1");
      return;
    }
  }
  if (webServer.hasArg("screen_off_start_min") &&
      !parseMinuteSetting(webServer.arg("screen_off_start_min"), nextScreenOffStartMin)) {
    webServer.send(400, "text/plain", "screen_off_start_min must be an integer from 0 to 1439");
    return;
  }
  if (webServer.hasArg("screen_off_end_min") &&
      !parseMinuteSetting(webServer.arg("screen_off_end_min"), nextScreenOffEndMin)) {
    webServer.send(400, "text/plain", "screen_off_end_min must be an integer from 0 to 1439");
    return;
  }
  if (webServer.hasArg("qweather_key")) {
    String key = webServer.arg("qweather_key");
    key.trim();
    if (!validQWeatherApiKey(key)) {
      webServer.send(400, "text/plain", "qweather_key format is invalid");
      return;
    }
    nextQWeatherKey = key;
  }
  if (webServer.hasArg("qweather_host")) {
    String host = webServer.arg("qweather_host");
    host.trim();
    host.toLowerCase();
    if (!validQWeatherApiHost(host)) {
      webServer.send(400, "text/plain", "qweather_host format is invalid");
      return;
    }
    nextQWeatherHost = host;
  }
  if (webServer.hasArg("clear_qweather_credentials")) {
    bool clearCredentials = false;
    if (!parseBoolSetting(webServer.arg("clear_qweather_credentials"), clearCredentials)) {
      webServer.send(400, "text/plain", "clear_qweather_credentials must be 0 or 1");
      return;
    }
    if (clearCredentials) {
      if (webServer.hasArg("qweather_key") || webServer.hasArg("qweather_host")) {
        webServer.send(400, "text/plain", "cannot set and clear qweather credentials together");
        return;
      }
      nextQWeatherKey = "";
      nextQWeatherHost = "";
    }
  }
  bool nextQWeatherEmpty = nextQWeatherKey.length() == 0 && nextQWeatherHost.length() == 0;
  if (!nextQWeatherEmpty &&
      (!validQWeatherApiKey(nextQWeatherKey) || !validQWeatherApiHost(nextQWeatherHost))) {
    webServer.send(400, "text/plain", "qweather_key and qweather_host must both be configured");
    return;
  }
  if (webServer.hasArg("ntp_server")) {
    nextNtpServer = webServer.arg("ntp_server");
    nextNtpServer.trim();
    if (!validNtpHost(nextNtpServer)) {
      webServer.send(400, "text/plain", "invalid ntp_server");
      return;
    }
  }
  bool ntpChanged = nextNtpServer != ntpServer;
  bool screenScheduleChanged = nextScreenScheduleEnabled != screenScheduleEnabled ||
                               nextScreenOffStartMin != screenOffStartMin ||
                               nextScreenOffEndMin != screenOffEndMin;
  bool qweatherHostChanged = nextQWeatherHost != qweatherApiHost;
  bool qweatherCredentialsChanged = nextQWeatherKey != qweatherApiKey || qweatherHostChanged;
  clockTheme = nextTheme;
  lunarEnabled = nextLunarEnabled;
  nightEnabled = nextNightEnabled;
  nightStartMin = nextNightStartMin;
  nightEndMin = nextNightEndMin;
  nightBrightness = nextNightBrightness;
  screenScheduleEnabled = nextScreenScheduleEnabled;
  screenOffStartMin = nextScreenOffStartMin;
  screenOffEndMin = nextScreenOffEndMin;
  qweatherApiKey = nextQWeatherKey;
  qweatherApiHost = nextQWeatherHost;
  ntpServer = nextNtpServer;
  if (!saveDeviceSettings()) {
    clockTheme = previousTheme;
    lunarEnabled = previousLunarEnabled;
    nightEnabled = previousNightEnabled;
    nightStartMin = previousNightStartMin;
    nightEndMin = previousNightEndMin;
    nightBrightness = previousNightBrightness;
    screenScheduleEnabled = previousScreenScheduleEnabled;
    screenOffStartMin = previousScreenOffStartMin;
    screenOffEndMin = previousScreenOffEndMin;
    qweatherApiKey = previousQWeatherKey;
    qweatherApiHost = previousQWeatherHost;
    ntpServer = previousNtpServer;
    webServer.send(507, "text/plain", "settings were not persisted");
    return;
  }
  if (qweatherCredentialsChanged) {
    lastDirectWeatherAttemptMs = 0;
    directWeatherRetryMs = 0;
    resetDirectWeatherCandidate();
  }
  if (qweatherHostChanged) {
    qweatherMflnState = -1;
    minQWeatherTlsHeap = 0;
    minQWeatherTlsBlock = 0;
  }
  if (qweatherCredentialsChanged && !synchronizeQWeatherSettingsCopies()) {
    webServer.send(507, "text/plain", "qweather credential backup was not synchronized");
    return;
  }
  clockChromeDrawn = false;
  weatherTextDrawnRev = -2;
  weatherTextBitmapDrawn = false;
  weatherTextNeedsFetch = true;
  clockDirty = true;
  if (screenScheduleChanged) screenScheduleWakeOverride = false;
  updateTimedDisplayState(true);
  if (ntpChanged && WiFi.status() == WL_CONNECTED) startNtp();
  sendSettingsJson();
}

void handleApiTimeSync() {
  if (WiFi.status() != WL_CONNECTED) {
    webServer.send(503, "text/plain", "wifi not connected");
    return;
  }
  startNtp();
  webServer.send(200, "application/json", "{\"ok\":true}");
}

void handleApiStocks(bool post) {
  if (post) {
    if (!requireWritableFilesystem()) return;
    String payload = webServer.arg("plain");
    if (payload.length() == 0 || payload.length() > 512) {
      webServer.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid body\"}");
      return;
    }
    JsonDocument request;
    if (deserializeJson(request, payload) || !request["symbols"].is<JsonArray>()) {
      webServer.send(400, "application/json", "{\"ok\":false,\"error\":\"symbols must be an array\"}");
      return;
    }
    String next[MAX_STOCKS];
    int nextCount = 0;
    for (JsonVariant value : request["symbols"].as<JsonArray>()) {
      String raw = value.as<String>();
      String symbol = normalizeStockSymbol(raw);
      if (symbol.length() == 0) {
        webServer.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid stock symbol\"}");
        return;
      }
      bool duplicate = false;
      for (int i = 0; i < nextCount; i++) duplicate = duplicate || next[i].equalsIgnoreCase(symbol);
      if (duplicate) continue;
      if (nextCount >= MAX_STOCKS) {
        webServer.send(400, "application/json", "{\"ok\":false,\"error\":\"at most 4 symbols\"}");
        return;
      }
      next[nextCount++] = symbol;
    }
    if (nextCount == 0) {
      webServer.send(400, "application/json", "{\"ok\":false,\"error\":\"at least 1 symbol\"}");
      return;
    }
    if (!stockSymbolListsEqual(stockSymbols, stockSymbolCount, next, nextCount)) {
      if (!saveOfflineState(next, nextCount, 1)) {
        webServer.send(507, "application/json", "{\"ok\":false,\"error\":\"stock list was not persisted\"}");
        return;
      }
      applyStockSymbolList(next, nextCount);
      stockConfigSyncPending = true;
      lastBridgeStockAttemptMs = 0;
      lastDirectTencentAttemptMs = 0;
      lastDirectCryptoAttemptMs = 0;
      lastDirectTencentSuccessMs = 0;
      lastDirectCryptoSuccessMs = 0;
      directTencentUpdatedAt = 0;
      directCryptoUpdatedAt = 0;
    }
  }

  JsonDocument response;
  response["ok"] = true;
  JsonArray symbols = response["symbols"].to<JsonArray>();
  for (int i = 0; i < stockSymbolCount; i++) symbols.add(stockSymbols[i]);
  appendStockState(response.as<JsonObject>());
  String out;
  serializeJson(response, out);
  webServer.send(200, "application/json", out);
}

bool syncStockConfigToBridge() {
  if (!stockConfigSyncPending || WiFi.status() != WL_CONNECTED || bridgeHost.length() == 0) return false;
  JsonDocument request;
  JsonArray symbols = request["symbols"].to<JsonArray>();
  for (int i = 0; i < stockSymbolCount; i++) symbols.add(stockSymbols[i]);
  String body;
  serializeJson(request, body);

  WiFiClient client;
  HTTPClient http;
  String url = "http://" + bridgeHost + "/stock/config";
  http.setTimeout(BRIDGE_HTTP_TIMEOUT_MS);
  if (!http.begin(client, url)) return false;
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(body);
  String response;
  bool responseOk = code == HTTP_CODE_OK && readHttpTextBounded(http, DIRECT_RESPONSE_MAX_BYTES, response);
  http.end();
  if (!responseOk) return false;

  JsonDocument doc;
  if (deserializeJson(doc, response) || !(doc["ok"] | false)) return false;
  String confirmed[MAX_STOCKS];
  int confirmedCount = 0;
  for (JsonVariant value : doc["symbols"].as<JsonArray>()) {
    String symbol = normalizeStockSymbol(value.as<String>());
    if (symbol.length() && confirmedCount < MAX_STOCKS) confirmed[confirmedCount++] = symbol;
  }
  markBridgeReachable();
  if (!stockSymbolListsEqual(stockSymbols, stockSymbolCount, confirmed, confirmedCount)) return false;
  stockConfigSyncPending = false;
  markOfflineStateDirty(true);
  Serial.println("[stock] local watchlist confirmed by Bridge");
  return true;
}

void maybeSendSerialStockConfig() {
  if (!stockConfigSyncPending || !wiredActive() || !intervalElapsed(millis(), lastSerialStockConfigMs, 5000UL)) return;
  lastSerialStockConfigMs = millis();
  JsonDocument doc;
  JsonArray symbols = doc["symbols"].to<JsonArray>();
  for (int i = 0; i < stockSymbolCount; i++) symbols.add(stockSymbols[i]);
  Serial.print("#STOCK_CONFIG ");
  serializeJson(doc, Serial);
  Serial.println();
}

void handleApiBridge() {
  if (!requireWritableFilesystem()) return;
  String newHost = webServer.arg("host");
  newHost.trim();
  if (newHost.length() == 0) {
    webServer.send(400, "text/plain", "missing host");
    return;
  }
  if (!saveBridgeHost(newHost)) {
    webServer.send(507, "text/plain", "bridge host was not persisted");
    return;
  }
  bridgeHost = newHost;
  Serial.printf("[api] bridge host = '%s'\n", bridgeHost.c_str());
  webServer.send(200, "text/plain", "ok");
  lastPollMs = 0; // poll the new bridge on the next loop tick
  lastWeatherPollMs = 0;
}

// Streams the animation currently in use for a slot, in the same wire format
// as the custom .bin: [1 byte frame count][RGB565 frames...]. Lets the Mac
// app mirror exactly what the device is showing (custom upload or built-in).
void handleSpriteRaw(ActiveApp slot) {
  bool custom = (slot == APP_CLAUDE) ? claudeCustom : codexCustom;
  const char *binPath = (slot == APP_CLAUDE) ? CLAUDE_SPRITE_FILE : CODEX_SPRITE_FILE;
  if (custom) {
    File f = LittleFS.open(binPath, "r");
    if (f) {
      webServer.streamFile(f, "application/octet-stream");
      f.close();
      return;
    }
  }
  int frames = (slot == APP_CLAUDE) ? CLAUDE_SPRITE_FRAMES : CODEX_SPRITE_FRAMES;
  int w = (slot == APP_CLAUDE) ? CLAUDE_SPRITE_W : CODEX_SPRITE_W;
  int h = (slot == APP_CLAUDE) ? CLAUDE_SPRITE_H : CODEX_SPRITE_H;
  const uint16_t *const *arr = (slot == APP_CLAUDE) ? claude_sprite_frames : codex_sprite_frames;
  size_t frameBytes = (size_t)w * h * 2;
  webServer.setContentLength(1 + (size_t)frames * frameBytes);
  webServer.send(200, "application/octet-stream", "");
  uint8_t cnt = (uint8_t)frames;
  webServer.sendContent((const char *)&cnt, 1);
  for (int i = 0; i < frames; i++) {
    webServer.sendContent_P((PGM_P)arr[i], frameBytes);
    yield();
  }
}

bool parseUint32Decimal(const String &input, uint32_t &value) {
  if (input.length() == 0) return false;
  uint32_t parsed = 0;
  for (size_t i = 0; i < input.length(); i++) {
    char c = input.charAt(i);
    if (c < '0' || c > '9') return false;
    uint8_t digit = (uint8_t)(c - '0');
    if (parsed > (0xFFFFFFFFUL - digit) / 10UL) return false;
    parsed = parsed * 10UL + digit;
  }
  value = parsed;
  return true;
}

// Removes a custom sprite so the compiled-in default animation comes back.
void handleSpriteReset(ActiveApp slot) {
  if (!fsAvailable) {
    webServer.send(503, "text/plain", "filesystem unavailable; animation preserved");
    return;
  }
  if (!webServer.hasArg("expected_rev")) {
    webServer.send(428, "text/plain", "expected_rev is required");
    return;
  }
  uint32_t expectedRev = 0;
  if (!parseUint32Decimal(webServer.arg("expected_rev"), expectedRev)) {
    webServer.send(400, "text/plain", "invalid expected_rev");
    return;
  }
  if (expectedRev != spriteRev) {
    webServer.send(409, "text/plain", "sprite revision changed");
    return;
  }

  const char *binPath = (slot == APP_CLAUDE) ? CLAUDE_SPRITE_FILE : CODEX_SPRITE_FILE;
  if (LittleFS.exists(binPath) && !LittleFS.remove(binPath)) {
    webServer.send(500, "text/plain", "unable to remove custom animation");
    return;
  }
  spriteRev++;
  loadCustomSpriteState();
  if (slot == APP_CLAUDE) claudeFrame = 0;
  else codexFrame = 0;
  if (currentApp == slot) drawActiveApp();
  webServer.send(200, "text/plain", "ok");
}

void handleResetWifi() {
  if (webServer.arg("confirm") != "RESET") {
    webServer.send(428, "text/plain", "confirm=RESET is required");
    return;
  }
  webServer.send(200, "text/html", "<html><body>Resetting WiFi, device will restart...</body></html>");
  delay(200);
  WiFiManager wm;
  wm.resetSettings();
  ESP.restart();
}

// ---------- on-device GIF decode (AnimatedGIF) ----------
// AnimatedGIF hands us the image one horizontal line at a time (via the draw
// callback) at the GIF's native resolution, so we never need a full-canvas
// buffer. We nearest-neighbour rescale into the target slot size and stream the
// result straight to the .bin one target row at a time. Because the .bin can't
// hold a whole frame in RAM to composite against, GIFs that only re-encode a
// changed sub-rectangle (the common optimizer output, disposal method 1) are
// composited by reading the *previous frame's* rows back out of the .bin we're
// writing. (Disposal method 2 "restore to background" isn't distinguished -
// uncovered pixels keep the previous frame instead of clearing; fine for the
// looping character animations this is for.)

struct GifDecodeCtx {
  int canvasW, canvasH; // GIF native size
  int targetW, targetH; // slot size we're rescaling down to
  size_t rowBytes;      // targetW * 2
  File out;             // output .bin, written sequentially
  File prevFile;        // previous frame in the .bin, read sequentially for compositing
  bool hasPrev;         // false for frame 0 (nothing to composite over -> black)
  uint16_t *prevRow;    // allocated only after AnimatedGIF's large block succeeds
  int producedRow;      // next target row still owed for the current frame
  bool writeOk;
};

static File gifReadFile; // one decode runs at a time, so a single handle is fine

void *gifOpenCB(const char *fname, int32_t *pSize) {
  gifReadFile = LittleFS.open(fname, "r");
  if (!gifReadFile) return nullptr;
  *pSize = (int32_t)gifReadFile.size();
  return (void *)&gifReadFile;
}

void gifCloseCB(void *) {
  if (gifReadFile) gifReadFile.close();
}

int32_t gifReadCB(GIFFILE *pFile, uint8_t *pBuf, int32_t iLen) {
  File *f = (File *)pFile->fHandle;
  // AnimatedGIF's own SD example keeps this one-byte-short guard near EOF.
  if ((pFile->iSize - pFile->iPos) < iLen) iLen = pFile->iSize - pFile->iPos - 1;
  if (iLen <= 0) return 0;
  int32_t n = (int32_t)f->read(pBuf, iLen);
  pFile->iPos = (int32_t)f->position();
  return n;
}

int32_t gifSeekCB(GIFFILE *pFile, int32_t iPosition) {
  File *f = (File *)pFile->fHandle;
  f->seek(iPosition);
  pFile->iPos = iPosition;
  return iPosition;
}

// Loads the next previous-frame row into prevRowBuf (black if there's no
// previous frame). Reads are sequential and stay aligned with producedRow.
static void readPrevRow(GifDecodeCtx *ctx) {
  if (ctx->hasPrev)
    ctx->prevFile.read((uint8_t *)ctx->prevRow, ctx->rowBytes);
  else
    memset(ctx->prevRow, 0, ctx->rowBytes);
}

// Appends the current rowBuf as the next output row.
static void emitRow(GifDecodeCtx *ctx) {
  if (ctx->writeOk && ctx->out.write((const uint8_t *)rowBuf, ctx->rowBytes) != ctx->rowBytes)
    ctx->writeOk = false;
  ctx->producedRow++;
}

// AnimatedGIF 2.2.3 redundantly reads a full palette after its initial
// 255-byte probe. Valid compact GIFs can therefore fail open with GIF_BAD_FILE
// when the file ends before that second read. The upload is temporary, so pad
// it past the decoder's largest probe without changing the decoded image.
bool padCompactGifForDecoder(const char *gifPath) {
  File file = LittleFS.open(gifPath, "a");
  if (!file) {
    Serial.println("[gif] unable to reopen upload for decoder padding");
    return false;
  }
  size_t originalSize = file.size();
  if (originalSize == 0) {
    file.close();
    Serial.println("[gif] uploaded file is empty");
    return false;
  }
  if (originalSize >= GIF_DECODER_MIN_FILE_BYTES) {
    file.close();
    return true;
  }

  const uint8_t zeros[32] = {0};
  size_t remaining = GIF_DECODER_MIN_FILE_BYTES - originalSize;
  while (remaining > 0) {
    size_t chunk = min(remaining, sizeof(zeros));
    if (file.write(zeros, chunk) != chunk) {
      file.close();
      Serial.println("[gif] decoder padding write failed");
      return false;
    }
    remaining -= chunk;
    yield();
  }
  file.close();
  Serial.printf("[gif] padded compact upload %u -> %u bytes\n",
                (unsigned)originalSize, (unsigned)GIF_DECODER_MIN_FILE_BYTES);
  return true;
}

// Emits a row that this frame doesn't touch: a straight copy of the previous
// frame (top/bottom gaps of a partial frame).
static void emitPrevRow(GifDecodeCtx *ctx) {
  readPrevRow(ctx);
  memcpy(rowBuf, ctx->prevRow, ctx->rowBytes);
  emitRow(ctx);
}

// Rescales one decoded native line into target rows, compositing over the
// previous frame, and streams every target row it can now finalize.
void gifDrawCB(GIFDRAW *pDraw) {
  GifDecodeCtx *ctx = (GifDecodeCtx *)pDraw->pUser;
  int sy = pDraw->iY + pDraw->y; // absolute source line on the GIF canvas
  if ((sy & 15) == 0) yield();
  if (sy < 0 || sy >= ctx->canvasH) return;

  const uint8_t *pal = pDraw->pPalette24; // RGB888, 256 entries
  const uint8_t *src = pDraw->pPixels;    // palette indices, one per pixel of this line
  bool hasTrans = pDraw->ucHasTransparency;
  uint8_t transIdx = pDraw->ucTransparent;

  // Emit every target row whose nearest source line is <= sy and isn't done yet.
  while (ctx->producedRow < ctx->targetH) {
    int ty = ctx->producedRow;
    int srcRow = (int)((long)ty * ctx->canvasH / ctx->targetH);
    if (srcRow > sy) break;                       // needs a later source line
    if (srcRow < sy) { emitPrevRow(ctx); continue; } // source line was skipped -> previous frame

    // srcRow == sy: composite this source line over the previous frame's row.
    readPrevRow(ctx);
    memcpy(rowBuf, ctx->prevRow, ctx->rowBytes);
    for (int tx = 0; tx < ctx->targetW; tx++) {
      int sx = (int)((long)tx * ctx->canvasW / ctx->targetW);
      int rel = sx - pDraw->iX;
      if (rel < 0 || rel >= pDraw->iWidth) continue; // outside this frame's rect: keep previous pixel
      uint8_t idx = src[rel];
      if (hasTrans && idx == transIdx) continue;     // transparent: keep previous pixel
      uint8_t r = pal[idx * 3 + 0], g = pal[idx * 3 + 1], b = pal[idx * 3 + 2];
      uint16_t val = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
      rowBuf[tx] = (uint16_t)(((val & 0xFF) << 8) | (val >> 8)); // byte-swap to match convert_sprites.py
    }
    emitRow(ctx);
  }
}

// Decodes gifPath into binPath in the [count][frames...] wire format the
// display path reads. Returns false on open/decode failure.
bool decodeGifToBin(const char *gifPath, const char *binPath, int targetW, int targetH) {
  // AnimatedGIF's internal state (~24KB of LZW/line/palette buffers) is big, so
  // allocate it on the heap only for the duration of a decode rather than
  // paying for it in .bss for the whole uptime.
  AnimatedGIF *gif = new AnimatedGIF();
  if (!gif) return false;
  uint16_t *previousRow = new uint16_t[targetW];
  if (!previousRow) {
    delete gif;
    return false;
  }
  gif->begin(GIF_PALETTE_RGB888);
  if (!gif->open(gifPath, gifOpenCB, gifCloseCB, gifReadCB, gifSeekCB, gifDrawCB)) {
    Serial.printf("[gif] open failed err=%d\n", gif->getLastError());
    gif->close();
    delete gif;
    delete[] previousRow;
    return false;
  }

  GifDecodeCtx ctx;
  ctx.canvasW = gif->getCanvasWidth();
  ctx.canvasH = gif->getCanvasHeight();
  if (ctx.canvasW <= 0 || ctx.canvasH <= 0 || ctx.canvasW > MAX_GIF_CANVAS_DIM ||
      ctx.canvasH > MAX_GIF_CANVAS_DIM ||
      (size_t)ctx.canvasW * (size_t)ctx.canvasH > MAX_GIF_CANVAS_PIXELS) {
    Serial.printf("[gif] canvas rejected %dx%d\n", ctx.canvasW, ctx.canvasH);
    gif->close();
    delete gif;
    delete[] previousRow;
    return false;
  }
  ctx.targetW = targetW;
  ctx.targetH = targetH;
  ctx.rowBytes = (size_t)targetW * 2;
  ctx.prevRow = previousRow;
  ctx.hasPrev = false;
  ctx.writeOk = true;
  size_t frameBytes = (size_t)targetW * targetH * 2;

  ctx.out = LittleFS.open(binPath, "w");
  if (!ctx.out) {
    gif->close();
    delete gif;
    delete[] previousRow;
    return false;
  }
  if (ctx.out.write((uint8_t)0) != 1) {
    gif->close();
    delete gif;
    delete[] previousRow;
    ctx.out.close();
    LittleFS.remove(binPath);
    Serial.println("[gif] output header write failed");
    return false;
  }

  uint8_t count = 0;
  int delayMs = 0, more = 1;
  bool decodeFailed = false;
  while (count < MAX_CUSTOM_FRAMES) {
    ctx.producedRow = 0;
    ctx.hasPrev = false;
    if (count > 0) {
      ctx.out.flush(); // make the just-written previous frame visible to the read handle
      ctx.prevFile = LittleFS.open(binPath, "r");
      ctx.hasPrev = (bool)ctx.prevFile;
      if (!ctx.hasPrev) {
        Serial.printf("[gif] previous frame open failed at frame=%u\n", count);
        decodeFailed = true;
        break;
      }
      ctx.prevFile.seek(1 + (size_t)(count - 1) * frameBytes);
    }

    more = gif->playFrame(false, &delayMs, &ctx);
    int error = gif->getLastError();
    int decodedRows = ctx.producedRow;
    if (more < 0) {
      Serial.printf("[gif] frame=%u decode failed err=%d rows=%d\n", count, error, decodedRows);
      decodeFailed = true;
    } else if (decodedRows == 0) {
      // Trailer/padding reached without a frame. AnimatedGIF returns 0 here;
      // do not turn that empty tail into a duplicate animation frame.
      if (more > 0 || (error != GIF_SUCCESS && error != GIF_EMPTY_FRAME)) {
        Serial.printf("[gif] frame=%u empty decode err=%d more=%d\n", count, error, more);
        decodeFailed = true;
      }
    } else {
      while (ctx.producedRow < ctx.targetH) emitPrevRow(&ctx);
      if (!ctx.writeOk) {
        Serial.printf("[gif] output write failed at frame=%u\n", count);
        decodeFailed = true;
      } else {
        count++;
      }
    }
    if (ctx.prevFile) ctx.prevFile.close();
    if (decodeFailed || decodedRows == 0 || more <= 0) break;
    yield();              // feed the WDT between frames
  }
  gif->close();
  delete gif;
  delete[] previousRow;
  ctx.out.close();

  if (decodeFailed || count == 0) {
    LittleFS.remove(binPath);
    return false;
  }
  File patch = LittleFS.open(binPath, "r+");
  if (!patch || !patch.seek(0) || patch.write(count) != 1) {
    if (patch) patch.close();
    LittleFS.remove(binPath);
    Serial.println("[gif] frame count patch failed");
    return false;
  }
  patch.close();
  Serial.printf("[gif] decoded %d frame(s) %dx%d -> %dx%d\n", count, ctx.canvasW, ctx.canvasH, targetW, targetH);
  return true;
}

// ---------- sprite upload (raw .gif -> on-device decode) ----------
// ESP8266WebServer fully buffers a plain POST body into a heap String before
// the handler runs, which a whole GIF would blow RAM on - so we take the
// upload over its streaming multipart/HTTPUpload path, writing the raw .gif to
// LittleFS in small chunks, then decode it on the done callback.
File uploadFile;
size_t uploadBytes = 0;
bool uploadFailed = false;
bool uploadTooLarge = false;

void handleSpriteUploadChunk(const char *gifPath) {
  HTTPUpload &upload = webServer.upload();
  if (upload.status == UPLOAD_FILE_START) {
    uploadBytes = 0;
    uploadFailed = !fsAvailable;
    uploadTooLarge = false;
    if (!fsAvailable) return;
    LittleFS.remove(gifPath);
    uploadFile = LittleFS.open(gifPath, "w");
    if (!uploadFile) uploadFailed = true;
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFailed || uploadTooLarge) return;
    if (upload.currentSize > MAX_GIF_UPLOAD_BYTES - uploadBytes) {
      uploadTooLarge = true;
      if (uploadFile) uploadFile.close();
      LittleFS.remove(gifPath);
      return;
    }
    if (!uploadFile || uploadFile.write(upload.buf, upload.currentSize) != upload.currentSize) {
      uploadFailed = true;
      if (uploadFile) uploadFile.close();
      LittleFS.remove(gifPath);
      return;
    }
    uploadBytes += upload.currentSize;
    yield();
  } else if (upload.status == UPLOAD_FILE_END || upload.status == UPLOAD_FILE_ABORTED) {
    if (uploadFile) uploadFile.close();
    if (upload.status == UPLOAD_FILE_ABORTED) {
      uploadFailed = true;
      LittleFS.remove(gifPath);
    }
  }
}

void handleSpriteUploadDone(ActiveApp slot) {
  const char *gifPath = (slot == APP_CLAUDE) ? CLAUDE_GIF_FILE : CODEX_GIF_FILE;
  const char *binPath = (slot == APP_CLAUDE) ? CLAUDE_SPRITE_FILE : CODEX_SPRITE_FILE;
  const char *tmpPath = (slot == APP_CLAUDE) ? CLAUDE_SPRITE_TMP_FILE : CODEX_SPRITE_TMP_FILE;
  int tw = (slot == APP_CLAUDE) ? CLAUDE_SPRITE_W : CODEX_SPRITE_W;
  int th = (slot == APP_CLAUDE) ? CLAUDE_SPRITE_H : CODEX_SPRITE_H;

  if (!fsAvailable) {
    webServer.send(503, "text/plain", "filesystem unavailable; animation preserved");
    return;
  }
  if (uploadTooLarge || uploadFailed || !LittleFS.exists(gifPath)) {
    LittleFS.remove(gifPath);
    webServer.send(uploadTooLarge ? 413 : 500, "text/plain",
                   uploadTooLarge ? "gif upload exceeds 256KB" : "gif upload failed");
    return;
  }

  LittleFS.remove(tmpPath);
  bool ok = padCompactGifForDecoder(gifPath) && decodeGifToBin(gifPath, tmpPath, tw, th);
  LittleFS.remove(gifPath); // temp raw gif no longer needed once decoded
  if (ok && !LittleFS.rename(tmpPath, binPath)) {
    LittleFS.remove(tmpPath);
    Serial.println("[sprite] unable to activate decoded animation");
    ok = false;
  }

  if (ok) {
    spriteRev++;
    loadCustomSpriteState();
    if (slot == APP_CLAUDE) claudeFrame = 0;
    else codexFrame = 0;
    if (currentApp == slot) drawActiveApp();
    webServer.send(200, "text/plain", "ok");
    Serial.println("[sprite] gif decoded & applied");
  } else {
    webServer.send(500, "text/plain", "gif decode failed (too large or unsupported?)");
    Serial.println("[sprite] gif decode FAILED");
  }
}

void handleGifMemoryDiagnostic() {
  if (webServer.arg("confirm") != "CHECK") {
    webServer.send(428, "application/json", "{\"ok\":false,\"error\":\"confirm=CHECK is required\"}");
    return;
  }
  if (!fsAvailable) {
    webServer.send(503, "application/json", "{\"ok\":false,\"error\":\"filesystem unavailable\"}");
    return;
  }
  uint32_t before = ESP.getMaxFreeBlockSize();
  AnimatedGIF *probeGif = new AnimatedGIF();
  uint16_t *probeRow = probeGif ? new uint16_t[CODEX_SPRITE_W] : nullptr;
  File probeRaw, probeOutput, probePrevious;
  if (probeGif && probeRow && codexCustom) {
    probeRaw = LittleFS.open(CODEX_SPRITE_FILE, "r");
    probeOutput = LittleFS.open(CODEX_SPRITE_FILE, "r");
    probePrevious = LittleFS.open(CODEX_SPRITE_FILE, "r");
  }
  bool filesReady = !codexCustom || (probeRaw && probeOutput && probePrevious);
  bool ok = probeGif && probeRow && filesReady;
  if (probeRaw) probeRaw.close();
  if (probeOutput) probeOutput.close();
  if (probePrevious) probePrevious.close();
  delete[] probeRow;
  delete probeGif;
  JsonDocument doc;
  doc["ok"] = ok;
  doc["decoder_bytes"] = sizeof(AnimatedGIF);
  doc["max_block_before"] = before;
  doc["max_block_after"] = ESP.getMaxFreeBlockSize();
  String response;
  serializeJson(doc, response);
  webServer.send(ok ? 200 : 503, "application/json", response);
}

void setupWebServer() {
  webServer.on("/", HTTP_GET, handleRoot);
  webServer.on("/save", HTTP_POST, handleSave);
  webServer.on("/reset-wifi", HTTP_POST, handleResetWifi);
  webServer.on("/api/info", HTTP_GET, handleApiInfo);
  webServer.on("/api/display", HTTP_POST, handleApiDisplay);
  webServer.on("/api/bridge", HTTP_POST, handleApiBridge);
  webServer.on("/api/brightness", HTTP_POST, handleApiBrightness);
  webServer.on("/api/screen", HTTP_POST, handleApiScreen);
  webServer.on("/api/settings", HTTP_GET, sendSettingsJson);
  webServer.on("/api/settings", HTTP_POST, handleApiSettingsPost);
  webServer.on("/api/time/sync", HTTP_POST, handleApiTimeSync);
  webServer.on("/api/diagnostics/gif-memory", HTTP_POST, handleGifMemoryDiagnostic);
  webServer.on("/api/stocks", HTTP_GET, []() { handleApiStocks(false); });
  webServer.on("/api/stocks", HTTP_POST, []() { handleApiStocks(true); });
  webServer.on("/sprite/claude/reset", HTTP_POST, []() { handleSpriteReset(APP_CLAUDE); });
  webServer.on("/sprite/codex/reset", HTTP_POST, []() { handleSpriteReset(APP_CODEX); });
  webServer.on("/sprite/claude/raw", HTTP_GET, []() { handleSpriteRaw(APP_CLAUDE); });
  webServer.on("/sprite/codex/raw", HTTP_GET, []() { handleSpriteRaw(APP_CODEX); });
  webServer.on(
      "/sprite/claude", HTTP_POST, []() { handleSpriteUploadDone(APP_CLAUDE); },
      []() { handleSpriteUploadChunk(CLAUDE_GIF_FILE); });
  webServer.on(
      "/sprite/codex", HTTP_POST, []() { handleSpriteUploadDone(APP_CODEX); },
      []() { handleSpriteUploadChunk(CODEX_GIF_FILE); });
  webServer.onNotFound([]() {
    if (webServer.method() == HTTP_GET) {
      webServer.sendHeader("Location", "/");
      webServer.send(302, "text/plain", "");
    } else {
      webServer.send(404, "application/json", "{\"ok\":false,\"error\":\"not found\"}");
    }
  });
  webServer.begin();
  Serial.printf("[web] admin server listening on http://%s/\n", WiFi.localIP().toString().c_str());
}

bool hasTencentStocks() {
  for (int i = 0; i < stockSymbolCount; i++) {
    if (!isCryptoSymbol(stockSymbols[i])) return true;
  }
  return false;
}

bool hasCryptoStocks() {
  for (int i = 0; i < stockSymbolCount; i++) {
    if (isCryptoSymbol(stockSymbols[i])) return true;
  }
  return false;
}

unsigned long failedRetry(unsigned long current, unsigned long successInterval, unsigned long minimum,
                          unsigned long maximum) {
  if (current == 0 || current == successInterval || current < minimum) return minimum;
  if (current >= maximum / 2) return maximum;
  return min(current * 2UL, maximum);
}

void beginNetworkJob() { lastNetworkJobMs = millis(); }

void activateStockDirectFallback() {
  if (stockDirectFallbackActive) return;
  stockDirectFallbackActive = true;
  bridgeStockHealthy = false;
  lastDirectTencentAttemptMs = 0;
  lastDirectCryptoAttemptMs = 0;
  directTencentRetryMs = 0;
  directCryptoRetryMs = 0;
  lastDirectTencentSuccessMs = 0;
  lastDirectCryptoSuccessMs = 0;
  directTencentUpdatedAt = 0;
  directCryptoUpdatedAt = 0;
  stockDirty = true;
  Serial.println("[stock] entering direct fallback");
}

void activateWeatherDirectFallback() {
  if (weatherDirectFallbackActive) return;
  weatherDirectFallbackActive = true;
  bridgeWeatherHealthy = false;
  lastDirectWeatherAttemptMs = 0;
  directWeatherRetryMs = 0;
  resetDirectWeatherCandidate();
  Serial.println("[weather] entering direct fallback");
}

bool runOneNetworkJob(unsigned long nowMs, DisplayMode eff) {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (lastNetworkJobMs != 0 && (unsigned long)(nowMs - lastNetworkJobMs) < 100UL) return false;

  bool wired = wiredActive();
  bool bridgeRecent = bridgeRecentlyReachable(nowMs);
  bool useDirect = directFallbackAllowed(nowMs);
  if (wired) {
    if (bridgeStockReportedStale || stockDataStale()) {
      bridgeStockHealthy = false;
      activateStockDirectFallback();
    }
    if (bridgeWeatherReportedStale || weatherDataStale()) {
      bridgeWeatherHealthy = false;
      activateWeatherDirectFallback();
    }
  }
  if (useDirect) {
    activateStockDirectFallback();
    activateWeatherDirectFallback();
  } else {
    if (!bridgeStockHealthy && lastBridgeStockAttemptMs != 0 &&
        intervalElapsed(nowMs, lastBridgeStockAttemptMs, BRIDGE_RETRY_INTERVAL_MS)) {
      activateStockDirectFallback();
    }
    if (!bridgeWeatherHealthy && lastBridgeWeatherAttemptMs != 0 &&
        intervalElapsed(nowMs, lastBridgeWeatherAttemptMs, BRIDGE_RETRY_INTERVAL_MS)) {
      activateWeatherDirectFallback();
    }
  }

  if (!wired && stockConfigSyncPending && bridgeRecent &&
      intervalElapsed(nowMs, lastStockConfigSyncAttemptMs, BRIDGE_RETRY_INTERVAL_MS)) {
    lastStockConfigSyncAttemptMs = nowMs;
    beginNetworkJob();
    syncStockConfigToBridge();
    return true;
  }

  if (!stockDirectFallbackActive) {
    unsigned long interval = bridgeStockHealthy ? STOCK_POLL_INTERVAL_MS : BRIDGE_RETRY_INTERVAL_MS;
    if (!wired && intervalElapsed(nowMs, lastBridgeStockAttemptMs, interval)) {
      lastBridgeStockAttemptMs = nowMs;
      lastStockPollMs = nowMs;
      beginNetworkJob();
      pollBridgeStock();
      return true;
    }
  } else {
    if (!wired && bridgeRecent && intervalElapsed(nowMs, lastBridgeStockAttemptMs, BRIDGE_RETRY_INTERVAL_MS)) {
      lastBridgeStockAttemptMs = nowMs;
      lastStockPollMs = nowMs;
      beginNetworkJob();
      pollBridgeStock();
      return true;
    }
    if (hasTencentStocks() && intervalElapsed(nowMs, lastDirectTencentAttemptMs, directTencentRetryMs)) {
      lastDirectTencentAttemptMs = nowMs;
      beginNetworkJob();
      if (pollDirectTencentStocks()) directTencentRetryMs = DIRECT_STOCK_INTERVAL_MS;
      else directTencentRetryMs = failedRetry(directTencentRetryMs, DIRECT_STOCK_INTERVAL_MS,
                                              DIRECT_STOCK_RETRY_MIN_MS, DIRECT_STOCK_RETRY_MAX_MS);
      return true;
    }
    if (hasCryptoStocks() && intervalElapsed(nowMs, lastDirectCryptoAttemptMs, directCryptoRetryMs)) {
      lastDirectCryptoAttemptMs = nowMs;
      beginNetworkJob();
      if (pollDirectBinanceStocks()) directCryptoRetryMs = DIRECT_STOCK_INTERVAL_MS;
      else directCryptoRetryMs = failedRetry(directCryptoRetryMs, DIRECT_STOCK_INTERVAL_MS,
                                             DIRECT_STOCK_RETRY_MIN_MS, DIRECT_STOCK_RETRY_MAX_MS);
      return true;
    }
  }

  if (!weatherDirectFallbackActive) {
    unsigned long interval = bridgeWeatherHealthy ? WEATHER_POLL_INTERVAL_MS : BRIDGE_RETRY_INTERVAL_MS;
    if (!wired && intervalElapsed(nowMs, lastBridgeWeatherAttemptMs, interval)) {
      lastBridgeWeatherAttemptMs = nowMs;
      lastWeatherPollMs = nowMs;
      beginNetworkJob();
      pollBridgeWeather();
      return true;
    }
  } else {
    if (!wired && bridgeRecent && intervalElapsed(nowMs, lastBridgeWeatherAttemptMs, BRIDGE_RETRY_INTERVAL_MS)) {
      lastBridgeWeatherAttemptMs = nowMs;
      lastWeatherPollMs = nowMs;
      beginNetworkJob();
      pollBridgeWeather();
      return true;
    }
    if (directWeatherStage != QWEATHER_NOW && !directWeatherCandidateFresh(nowMs)) {
      resetDirectWeatherCandidate();
      lastDirectWeatherAttemptMs = 0;
      directWeatherRetryMs = 0;
    }
    if (intervalElapsed(nowMs, lastDirectWeatherAttemptMs, directWeatherRetryMs)) {
      lastDirectWeatherAttemptMs = nowMs;
      beginNetworkJob();
      DirectWeatherStage attemptedStage = directWeatherStage;
      bool ok = attemptedStage == QWEATHER_NOW
                    ? pollDirectQWeatherNow()
                    : (attemptedStage == QWEATHER_FORECAST ? pollDirectQWeatherForecast()
                                                            : pollDirectQWeatherAir());
      if (ok) {
        directWeatherRetryMs = attemptedStage == QWEATHER_AIR
                                   ? DIRECT_WEATHER_INTERVAL_MS
                                   : QWEATHER_REQUEST_GAP_MS;
      } else {
        unsigned long successInterval = attemptedStage == QWEATHER_NOW
                                            ? DIRECT_WEATHER_INTERVAL_MS
                                            : QWEATHER_REQUEST_GAP_MS;
        directWeatherRetryMs = failedRetry(directWeatherRetryMs, successInterval,
                                           DIRECT_WEATHER_RETRY_MIN_MS,
                                           DIRECT_WEATHER_RETRY_MAX_MS);
      }
      return true;
    }
  }

  unsigned long statusInterval = bridgeRecent ? BRIDGE_POLL_INTERVAL_MS : BRIDGE_RETRY_INTERVAL_MS;
  if (!wired && intervalElapsed(nowMs, lastPollMs, statusInterval)) {
    lastPollMs = nowMs;
    beginNetworkJob();
    pollBridge();
    return true;
  }

  if (!wired && eff == MODE_NET && bridgeRecent && intervalElapsed(nowMs, lastNetPollMs, NET_POLL_INTERVAL_MS)) {
    lastNetPollMs = nowMs;
    beginNetworkJob();
    pollNet();
    return true;
  }

  if (!wired && eff == MODE_STOCK && bridgeRecent && stockSource == FEED_BRIDGE && stockNamesRev >= 0 &&
      stockNamesDrawnRev != stockNamesRev &&
      intervalElapsed(nowMs, lastStockNamesAttemptMs, BRIDGE_RETRY_INTERVAL_MS)) {
    lastStockNamesAttemptMs = nowMs;
    beginNetworkJob();
    if (drawStockNames()) stockNamesDrawnRev = stockNamesRev;
    return true;
  }

  if (eff == MODE_CLOCK && bridgeRecent && weather.source == FEED_BRIDGE && maybeFetchWeatherText()) {
    beginNetworkJob();
    return true;
  }
  return false;
}

// ---------- Arduino entry points ----------

void setup() {
  Serial.setRxBufferSize(2048); // a serial #STATUS frame (~600B) must survive a slow draw
  Serial.begin(115200);
  // Never auto-format on a mount error: LittleFS contains the user's custom
  // animations and settings. Recovery/formatting must be an explicit action.
  LittleFSConfig fsConfig(false);
  fsAvailable = LittleFS.setConfig(fsConfig) && LittleFS.begin();
  if (fsAvailable) {
    loadBridgeHost();
    loadBrightness();
    loadDeviceSettings();
    loadOfflineState();
    loadWeatherTextCacheState();
    cleanupInterruptedSpriteUploads();
    loadCustomSpriteState();
  } else {
    loadDefaultStockSymbols();
    Serial.println("[fs] LittleFS mount failed; auto-format disabled, data preserved");
  }

  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  analogWriteFreq(BRIGHTNESS_PWM_FREQ);
  analogWriteRange(100); // duty maps 1:1 to a 0-100 percentage
  applyBrightness();

  if (!fsAvailable) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("FS MOUNT ERROR", SCREEN_CX, 98, 2);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.drawString("DATA PRESERVED", SCREEN_CX, 122, 2);
    delay(2500);
  }

  setupWiFi();

  if (WiFi.status() == WL_CONNECTED) {
    startNtp();
    setupWebServer();
    webServerStarted = true;

    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("WiFi connected", 8, 70, 2);
    tft.drawString("Admin page:", 8, 100, 2);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString("http://" + WiFi.localIP().toString(), 8, 125, 2);
    delay(3000);

    showMainUiIfNeeded();
    lastPollMs = 0;
    lastWeatherPollMs = 0;
  }
  // else: the config-portal screen stays up; either the user configures WiFi
  // (handled in loop) or serial #STATUS frames arrive and take the screen over
}

void loop() {
  wifiManager.process(); // keeps the config portal alive until WiFi is set up
  pumpSerial();          // wired (USB) bridge frames

  bool wifiConnected = WiFi.status() == WL_CONNECTED;
  if (!wifiConnected && wifiWasConnected) qweatherMflnState = -1;
  wifiWasConnected = wifiConnected;

  if (!webServerStarted && wifiConnected) {
    // WiFi came up after boot (portal or slow AP); the portal has released
    // port 80 by now, so the admin server can bind it
    setupWebServer();
    webServerStarted = true;
    startNtp();
    showMainUiIfNeeded();
    lastPollMs = 0;
    lastWeatherPollMs = 0;
  }
  webBridgeRequestMade = false;
  if (webServerStarted) webServer.handleClient();
  unsigned long nowMs = millis();
  minFreeHeap = min(minFreeHeap, ESP.getFreeHeap());
  maybeSaveOfflineState();
  maybeSendSerialStockConfig();
  if (nowMs - lastTimedDisplayCheckMs >= 1000UL) {
    lastTimedDisplayCheckMs = nowMs;
    updateTimedDisplayState();
  }
  if (!mainUiShown) return; // config-portal screen is up, nothing to animate

  // On a mode transition, reset the incoming mode's chrome so it repaints
  // cleanly, and repaint the pet immediately when returning to it.
  DisplayMode eff = effectiveMode();
  if (eff != lastEffectiveMode) {
    lastEffectiveMode = eff;
    if (eff == MODE_NET) {
      netChromeDrawn = false;
      lastNetPollMs = 0;
    } else if (eff == MODE_CLOCK) {
      clockChromeDrawn = false;
      lastWeatherPollMs = 0;
      drawClockScreen(true);
    } else if (eff == MODE_STOCK) {
      stockChromeDrawn = false;
      lastStockPollMs = 0;
    } else {
      updateActiveApp();
      drawActiveApp();
    }
  }

  if (eff == MODE_NET) {
    // net-speed mode: rendering (constant-rate sweep) is independent of the
    // bridge polls that refill its sample queue
    if (nowMs - lastNetDrawMs >= NET_DRAW_INTERVAL_MS) {
      lastNetDrawMs = nowMs;
      netDrawTick();
    }
  } else if (eff == MODE_CLOCK) {
    static unsigned long lastClockDrawMs = 0;
    if (nowMs - lastClockDrawMs >= 1000UL) {
      lastClockDrawMs = nowMs;
      drawClockScreen();
    }
  } else if (eff == MODE_STOCK) {
    if (intervalElapsed(nowMs, lastStockFreshnessCheckMs, 1000UL)) {
      lastStockFreshnessCheckMs = nowMs;
      if (stockDataStale() != stockLastDrawnStale) stockDirty = true;
    }
    if (!stockChromeDrawn || stockDirty) drawStockScreen();
  } else {
    // sprite walk-cycle animation (only advances while that app is showing)
    if (nowMs - lastAnimMs >= ANIM_INTERVAL_MS) {
      lastAnimMs = nowMs;
      bool claudeWorking = claudeStatus.status == "working";
      bool codexWorking = codexStatus.status == "working";
      if (showingCd != CD_NONE) {
        // countdown owns the center area: no sprite frames over it
      } else if (currentApp == APP_CLAUDE && claudeWorking) {
        claudeFrame = (claudeFrame + 1) % claudeFrameCount();
        drawClaudeSprite(claudeFrame);
      } else if (currentApp == APP_CODEX && codexWorking) {
        codexFrame = (codexFrame + 1) % codexFrameCount();
        drawCodexSprite(codexFrame);
      }
    }

    // countdown seconds tick locally between bridge polls
    static unsigned long lastCdTickMs = 0;
    if (showingCd != CD_NONE && nowMs - lastCdTickMs >= 1000) {
      lastCdTickMs = nowMs;
      drawCountdown(false);
    }

    // "urgent" flash toggle (independent, faster cadence)
    if (nowMs - lastFlashMs >= FLASH_INTERVAL_MS) {
      lastFlashMs = nowMs;
      flashOn = !flashOn;
      if (bridgeStale()) {
        redrawRingOnly();
      } else if (currentAppNeedsInput()) {
        // approval needed: blink the whole border red, restore the quota ring
        // on the off-phase so it doesn't erase the normal chrome permanently
        if (flashOn) drawFullBorder(TFT_RED);
        else redrawRingOnly();
      }
    }

    // alternate which app is shown when neither/both are uniquely working
    if (updateActiveApp()) {
      drawActiveApp();
    }
  }

  if (!webBridgeRequestMade) runOneNetworkJob(nowMs, eff);
}
