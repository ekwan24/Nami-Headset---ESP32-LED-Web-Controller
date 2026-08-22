#include <FastLED.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <math.h>
#include <string.h>

// ============================================================
// LED HARDWARE
// ============================================================
// GPIO18 (D10): free on this board. GPIO5 (used on the classic ESP32) is a
// JTAG pin on the C6 and can't be used for general GPIO.
#define LED_PIN      18
#define LED_TYPE     WS2812B
#define COLOR_ORDER  GRB

#define RINGA_LEDS   54
#define STRIP_LEDS   66
#define RINGB_LEDS   54
#define NUM_LEDS     (RINGA_LEDS + STRIP_LEDS + RINGB_LEDS)  // 174
#define RINGA_START  0
#define STRIP_START  RINGA_LEDS
#define RINGB_START  (RINGA_LEDS + STRIP_LEDS)

CRGB leds[NUM_LEDS];
CRGB ledsPrev[NUM_LEDS];  // scratch buffer: holds the outgoing pattern's live render during a crossfade

// ============================================================
// DEFAULT TUNING VALUES
// Brightness values are plain percentages (0-100). Speed ticks range
// -5 (slowest) to +5 (fastest), 0 = neutral.
// ============================================================

// Shared color for all 3 Quick Access effects (hex RGB)
#define QA_COLOR_R  0x00
#define QA_COLOR_G  0xA7
#define QA_COLOR_B  0x7D

// ---- Default 1: Solid ----
#define QA1_BRIGHTNESS_PCT       65   // overall brightness

// ---- Default 2: Breathe ----
#define QA2_MIN_BRIGHTNESS_PCT   15   // dimmest point of the breathing cycle
#define QA2_MAX_BRIGHTNESS_PCT   100   // brightest point of the breathing cycle
#define QA2_SPEED_TICK           -1   // breathing speed

// ---- Default 3: Waves ----
#define QA3_PEAK_BRIGHTNESS_PCT  100   // brightness at the head of each wave
#define QA3_BASE_BRIGHTNESS_PCT  12   // brightness of the background glow
#define QA3_SPEED_TICK            0   // wave travel speed
#define QA3_TRAIL_LENGTH         30   // how many LEDs long the fading trail is (1-30)

// ---- Default 3 (Waves) specific: Ring B rotation ----
// 1 = Ring B travels the opposite index direction from Ring A (mirrored
// mount). 0 = same direction.
#define RINGB_MIRROR_ROTATION 1

#define RINGB_PHASE_OFFSET  0

// ---- Default 4: Rainbow ----
#define QA4_BRIGHTNESS_PCT        100   // overall brightness (uniform, no peak/base split)
#define QA4_SPEED_TICK              2   // wheel/wave travel speed
#define QA4_RAINBOW_SPEED_TICK      2   // independent hue-cycling speed

// Hue offset (0-255) to align Ring B's rainbow wheel with Ring A's.
#define RAINBOW_RINGB_HUE_OFFSET  0

// ---- Pattern-switch crossfade ----
// Dissolve duration (ms) when switching between Solid/Breathe/Waves.
// Rainbow always switches instantly instead of blending.
#define TRANSITION_MS  400

#define PAT_SOLID   0
#define PAT_BREATHE 1
#define PAT_WAVES   2
#define PAT_RAINBOW 3

// ============================================================
// PER-SEGMENT SETTINGS
// ============================================================
struct SegSettings {
  uint8_t  pattern     = PAT_SOLID;
  uint8_t  r = QA_COLOR_R, g = QA_COLOR_G, b = QA_COLOR_B;   // default color
  uint8_t  brightness  = 153;   // 60% of 255 — peak/head
  uint8_t  baseBright  = 76;    // 30% of 255 — base brightness (independent baseline)
  uint8_t  minBright   = 64;    // 25% of 255
  uint8_t  maxBright   = 255;   // 100%
  uint8_t  speedThr    = 2;     // frame threshold (higher = slower)
  uint8_t  speedStep   = 1;     // pixels per step
  uint8_t  trail       = 6;
  uint8_t  hueThr      = 2;     // Rainbow only: frame threshold for hue cycling
  uint8_t  hueStep     = 1;     // Rainbow only: hue units per step
};

// ============================================================
// EFFECT DATA — one Preset struct = one Quick Access effect's
// settings for all 3 LED segments
// ============================================================
struct Preset {
  SegSettings seg[3];  // 0=RingA, 1=Strip, 2=RingB
};

// ============================================================
// GLOBAL STATE
// ============================================================
uint8_t runQA      = 0;    // 0-2 which quick access effect is running
bool    ledsOn     = true;

bool     transitioning    = false;   // true while crossfading between two Quick Access patterns
uint8_t  fromQA            = 0;      // pattern being faded OUT of (only meaningful while transitioning)
uint32_t transitionStartMs = 0;

// ============================================================
// HELPERS
// ============================================================
// Target ms per animation frame. loop() trims its trailing delay by however
// long rendering already took, so actual frame rate stays consistent.
#define FRAME_INTERVAL_MS 20

// percent (0-100) -> 0-255
uint8_t pctTo255(uint8_t pct) {
  return (uint8_t)((pct / 100.0f) * 255);
}

// apply a tick (-5..+5) to a threshold/step pair; higher tick = faster.
// Shared by Speed (speedThr/speedStep) and Rainbow Speed (hueThr/hueStep).
void applyTick(int tick, uint8_t &thr, uint8_t &step) {
  if (tick <= 0) { step = 1; thr = 2 + (-tick) * 2; }
  else           { thr = 1; step = tick; }
}
void applySpeedTick(int tick, SegSettings &sg) { applyTick(tick, sg.speedThr, sg.speedStep); }
void applyHueTick(int tick, SegSettings &sg)   { applyTick(tick, sg.hueThr, sg.hueStep); }

// Converts speedStep/speedThr into a continuous LEDs-per-second rate, so
// motion can be advanced smoothly every frame instead of hopping once every
// speedThr frames.
float ledsPerSecond(const SegSettings &sg) {
  return (sg.speedStep / (float)sg.speedThr) * (1000.0f / FRAME_INTERVAL_MS);
}

// ============================================================
// QUICK ACCESS (hardcoded signature effects)
// ============================================================
Preset makeQA(uint8_t effectIdx) {
  Preset p;
  for (int i = 0; i < 3; i++) {
    p.seg[i].r = QA_COLOR_R; p.seg[i].g = QA_COLOR_G; p.seg[i].b = QA_COLOR_B;

    switch (effectIdx) {

      case 0:  // Default 1 — Solid
        p.seg[i].pattern    = PAT_SOLID;
        p.seg[i].brightness = pctTo255(QA1_BRIGHTNESS_PCT);
        break;

      case 1:  // Default 2 — Breathe
        p.seg[i].pattern    = PAT_BREATHE;
        p.seg[i].minBright  = pctTo255(QA2_MIN_BRIGHTNESS_PCT);
        p.seg[i].maxBright  = pctTo255(QA2_MAX_BRIGHTNESS_PCT);
        applySpeedTick(QA2_SPEED_TICK, p.seg[i]);
        break;

      case 2:  // Default 3 — Waves
        p.seg[i].pattern     = PAT_WAVES;
        p.seg[i].brightness  = pctTo255(QA3_PEAK_BRIGHTNESS_PCT);
        p.seg[i].baseBright  = pctTo255(QA3_BASE_BRIGHTNESS_PCT);
        applySpeedTick(QA3_SPEED_TICK, p.seg[i]);
        p.seg[i].trail       = QA3_TRAIL_LENGTH;
        break;

      case 3:  // Default 4 — Rainbow
        p.seg[i].pattern     = PAT_RAINBOW;
        p.seg[i].brightness  = pctTo255(QA4_BRIGHTNESS_PCT);
        applySpeedTick(QA4_SPEED_TICK, p.seg[i]);
        applyHueTick(QA4_RAINBOW_SPEED_TICK, p.seg[i]);
        break;
    }
  }
  return p;
}

Preset QA[4];          // filled in setup() — mutated live by tuning commands
Preset QA_ORIGINAL[4]; // pristine copies of the firmware defaults, used by the RESET command
float  waveBrightScale = 1.0f; // 0.0-1.0, master brightness for Waves pattern only (scales base + trail proportionally)

// ============================================================
// RENDERING HELPERS
// ============================================================
// Scale a segment's color by brightness. nscale8_video (not nscale8) keeps
// a nonzero channel from rounding down to black at low brightness.
CRGB segColor(const SegSettings &sg, uint8_t bright255) {
  CRGB c = CRGB(sg.r, sg.g, sg.b);
  c.nscale8_video(bright255);
  return c;
}

// Solid: fill the segment range with solid color
void renderSolid(uint16_t start, uint16_t count, const SegSettings &sg, CRGB *buf) {
  fill_solid(buf + start, count, segColor(sg, sg.brightness));
}

// Breathe: brightness eases between minBright and maxBright. smoothstep()
// softens the sine curve's ends, and a gamma curve (BREATHE_GAMMA) corrects
// for perceived (roughly logarithmic) brightness so the ramp doesn't look
// front-loaded.
#define BREATHE_GAMMA 1.8f
void renderBreathe(uint16_t start, uint16_t count, const SegSettings &sg, float phase, CRGB *buf) {
  float t      = (sinf(phase) + 1.0f) * 0.5f;      // 0..1 raw sine
  float eased  = t * t * (3.0f - 2.0f * t);         // smoothstep
  float shaped = powf(eased, BREATHE_GAMMA);        // perceptual gamma correction
  uint8_t lo = sg.minBright;
  uint8_t hi = sg.maxBright;
  uint8_t br = lo + (uint8_t)(shaped * (hi - lo));
  fill_solid(buf + start, count, segColor(sg, br));
}

// Waves on strip: two fronts travel from each end toward the center with a
// continuous wrapping trail. `dt` drives a fractional head position for
// smooth (not per-LED-stepped) motion.
void renderWavesStrip(const SegSettings &sg, float dt, CRGB *buf) {
  static float headF = 0.0f;
  uint8_t baseBr = (uint8_t)(sg.baseBright * waveBrightScale);
  fill_solid(buf + STRIP_START, STRIP_LEDS, segColor(sg, baseBr));
  // Round up so an odd STRIP_LEDS still gets its center LED covered by one
  // of the two fronts (no effect on even counts).
  const uint8_t half = (STRIP_LEDS + 1) / 2;
  headF += ledsPerSecond(sg) * dt;
  headF = fmodf(headF, (float)half);
  if (headF < 0.0f) headF += half;
  uint8_t peakBr = (uint8_t)(sg.brightness * waveBrightScale);
  // Cap the trail to half the travel distance so it always fully fades out
  // before the head wraps back to the edge — otherwise the wrap shows a
  // visible chunk of still-lit trail disappearing.
  uint8_t effTrail = min(sg.trail, (uint8_t)(half / 2));
  if (effTrail < 1) effTrail = 1;
  int headIdx = (int)floorf(headF);
  // Start at -1 so the not-yet-reached neighbor pixel picks up a fractional
  // glow, turning the motion into a smooth glide instead of a per-LED jump.
  for (int w = -1; w <= (int)effTrail; w++) {
    int p = ((headIdx - w) % half + half) % half;
    float dist = fabsf(headF - (headIdx - w));   // continuous distance from head, in LEDs
    if (dist > effTrail) continue;
    uint8_t trailVal = (uint8_t)(peakBr - dist * (peakBr - baseBr) / effTrail);
    buf[STRIP_START + p]                    = segColor(sg, trailVal);
    buf[STRIP_START + (STRIP_LEDS - 1 - p)] = segColor(sg, trailVal);
  }
}

// Waves on ring: spinning dot with trail. `reverse` flips which side of the
// head the trail is drawn on (needed when Ring B runs the opposite index
// direction — see RINGB_MIRROR_ROTATION). `posF` is a continuous fractional
// position so motion glides smoothly instead of stepping per-LED.
void renderWavesRing(uint16_t start, uint8_t count, const SegSettings &sg, float posF, bool reverse, CRGB *buf) {
  uint8_t baseBr = (uint8_t)(sg.baseBright * waveBrightScale);
  fill_solid(buf + start, count, segColor(sg, baseBr));
  uint8_t peakBr = (uint8_t)(sg.brightness * waveBrightScale);
  int headIdx = (int)floorf(posF);
  int dirSign = reverse ? -1 : 1;
  for (int t = -1; t <= (int)sg.trail; t++) {
    // rawIdx must stay unwrapped for the distance calc — comparing a
    // wrapped index against continuous posF caused a once-per-revolution
    // blink right where the head crosses from the last LED back to the first.
    int rawIdx = headIdx - dirSign * t;
    int idx = ((rawIdx % count) + count) % count;
    float dist = fabsf(dirSign * (posF - rawIdx));   // distance from head, in LEDs
    if (dist > sg.trail) continue;
    uint8_t trailVal = (uint8_t)(peakBr - dist * (peakBr - baseBr) / sg.trail);
    buf[start + idx] = segColor(sg, trailVal);
  }
}

// Rainbow ring: a full-spectrum color wheel around the ring, uniform
// brightness. `startHue` is the rotating phase (Speed + Rainbow Speed).
void renderRainbowRing(uint16_t start, uint8_t count, const SegSettings &sg, uint8_t startHue, CRGB *buf) {
  // i*256/count computed fresh per LED (not one truncated delta reused) so
  // rounding error can't accumulate into a seam where the ring wraps.
  for (uint8_t i = 0; i < count; i++) {
    uint8_t hue = startHue + (uint8_t)((i * 256) / count);
    CHSV hsv(hue, 255, sg.brightness);
    CRGB c;
    hsv2rgb_rainbow(hsv, c);
    buf[start + i] = c;
  }
}

// Rainbow strip: a gradient flows in from both ends toward the center and
// loops continuously. `phase` shifts the mapping over time.
void renderRainbowStrip(uint16_t start, uint16_t count, const SegSettings &sg, uint8_t phase, CRGB *buf) {
  // Round up so an odd count still covers its center LED (no effect on even).
  uint16_t half = (count + 1) / 2;
  uint8_t  deltaHue = 255 / half;
  for (uint16_t p = 0; p < half; p++) {
    CHSV hsv((uint8_t)(phase - p * deltaHue), 255, sg.brightness);
    CRGB c;
    hsv2rgb_rainbow(hsv, c);
    buf[start + p]                = c;
    buf[start + (count - 1 - p)]  = c;
  }
}

// ============================================================
// BLE
// ============================================================
#define SERVICE_UUID        "12345678-1234-5678-1234-56789abcdef0"
#define CHARACTERISTIC_UUID "12345678-1234-5678-1234-56789abcdef1"
#define RSSI_CHAR_UUID      "12345678-1234-5678-1234-56789abcdef2"
#define STATE_CHAR_UUID     "12345678-1234-5678-1234-56789abcdef3"

BLECharacteristic *rssiChar   = nullptr;
BLECharacteristic *stateChar  = nullptr;
bool               clientConn = false;
uint16_t           connHandle = 0;
int8_t             lastRssi   = 0;

// This board's BLE stack is NimBLE (not Bluedroid) — RSSI reads are a
// direct synchronous call (ble_gap_conn_rssi), no async GAP callback needed.
class ServerCallback : public BLEServerCallbacks {
  void onConnect(BLEServer* srv, ble_gap_conn_desc *desc) {
    Serial.println("Client connected");
    clientConn = true;
    connHandle = desc->conn_handle;
  }
  void onDisconnect(BLEServer* srv) {
    Serial.println("Client disconnected, re-advertising");
    clientConn = false;
    BLEDevice::startAdvertising();
  }
};

// ── STATE SYNC ────────────────────────────────────────────────
// Convert 0-255 brightness back to 0-100 percent
uint8_t to_pct(uint8_t val) {
  return (uint8_t)(val / 255.0f * 100 + 0.5f);
}

// Reverse-engineer a tick (-5..+5) from its threshold/step pair (inverse of
// applyTick above). thr==1 marks a positive tick — checking step>1 instead
// used to miss tick==+1 (step==1 there too), collapsing it to 0.
int8_t decodeTick(uint8_t thr, uint8_t step) {
  if (thr == 1) return step;
  return -((int)(thr - 2) / 2);
}
int8_t getSpeedTick(const SegSettings &sg) { return decodeTick(sg.speedThr, sg.speedStep); }
int8_t getHueTick(const SegSettings &sg)   { return decodeTick(sg.hueThr, sg.hueStep); }

// Builds the state string the webpage parses on connect/resync.
// snprintf into a fixed buffer, not String concatenation, to avoid heap
// fragmentation from repeated allocations on every state read.
void buildStateString(char *out, size_t outSize) {
  const SegSettings &sg = QA[runQA].seg[0];  // all 3 segs are identical, read seg 0
  snprintf(out, outSize,
    "QA:%u,ON:%d,COLOR:%02X%02X%02X,BRIGHT:%u,BASE:%u,MINBR:%u,MAXBR:%u,SPD:%d,TRAIL:%u,WBRIGHT:%d,RSPD:%d",
    runQA, ledsOn ? 1 : 0, sg.r, sg.g, sg.b,
    to_pct(sg.brightness), to_pct(sg.baseBright), to_pct(sg.minBright), to_pct(sg.maxBright),
    getSpeedTick(sg), sg.trail, (int)(waveBrightScale * 100 + 0.5f), getHueTick(sg));
}

class StateCallback : public BLECharacteristicCallbacks {
  void onRead(BLECharacteristic *c) {
    char buf[128];
    buildStateString(buf, sizeof(buf));
    c->setValue((uint8_t*)buf, strlen(buf));
  }
};

// Comfortably above the longest real command ("WAVEBRIGHT:100"); anything
// longer is dropped before parsing.
#define MAX_CMD_LEN 32

class CmdCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) {
    // getValue() must return a String (BLE library API); copy into a fixed
    // stack buffer immediately rather than chaining more String allocations.
    String raw = c->getValue();
    if (raw.length() < 1 || raw.length() >= MAX_CMD_LEN) return;

    char buf[MAX_CMD_LEN];
    memcpy(buf, raw.c_str(), raw.length());
    buf[raw.length()] = '\0';

    char *start = buf;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') start++;
    char *end = start + strlen(start);
    while (end > start && (end[-1]==' '||end[-1]=='\t'||end[-1]=='\r'||end[-1]=='\n')) *--end = '\0';
    if (*start == '\0') return;

    char *colon = strchr(start, ':');
    const char *cmd = start;
    const char *arg = "";
    if (colon) { *colon = '\0'; arg = colon + 1; }

    // Tuning commands apply to the RUNNING Quick Access effect's 3 segments.
    // Helper: apply a lambda to all 3 segments of the running QA slot.
    #define FOR_QA_SEGS(BODY) for (int _i=0;_i<3;_i++){ SegSettings &sg = QA[runQA].seg[_i]; BODY }

    if (strcmp(cmd, "COLOR") == 0 || strcmp(cmd, "QACOLOR") == 0) {
      long rgb = strtol(arg, NULL, 16);
      uint8_t nr=(rgb>>16)&0xFF, ng=(rgb>>8)&0xFF, nb=rgb&0xFF;
      // color is universal — apply to all 4 QA slots, all segments
      for (int qi=0; qi<4; qi++)
        for (int si=0; si<3; si++) {
          QA[qi].seg[si].r=nr; QA[qi].seg[si].g=ng; QA[qi].seg[si].b=nb;
        }

    } else if (strcmp(cmd, "BRIGHT") == 0) {
      // peak/head brightness (Solid + Waves) — if base is now above peak, pull base down to match
      uint8_t val = pctTo255(constrain((int)atol(arg), 0, 100));
      FOR_QA_SEGS( sg.brightness = val; if (sg.baseBright > val) sg.baseBright = val; )

    } else if (strcmp(cmd, "BASE") == 0) {
      // base brightness (Waves) — capped at current peak brightness, never brighter than peak
      uint8_t val = pctTo255(constrain((int)atol(arg), 0, 100));
      FOR_QA_SEGS( sg.baseBright = min(val, sg.brightness); )

    } else if (strcmp(cmd, "MINBR") == 0) {
      // capped at current max — min must never exceed max (renderBreathe subtracts them)
      uint8_t val = pctTo255(constrain((int)atol(arg), 0, 100));
      FOR_QA_SEGS( sg.minBright = min(val, sg.maxBright); )

    } else if (strcmp(cmd, "MAXBR") == 0) {
      // if min is now above the new max, pull min down to match
      uint8_t val = pctTo255(constrain((int)atol(arg), 0, 100));
      FOR_QA_SEGS( sg.maxBright = val; if (sg.minBright > val) sg.minBright = val; )

    } else if (strcmp(cmd, "SPD") == 0) {
      int tick = constrain((int)atol(arg), -5, 5);
      FOR_QA_SEGS( applySpeedTick(tick, sg); )

    } else if (strcmp(cmd, "TRAIL") == 0) {
      uint8_t val = constrain((int)atol(arg), 1, 30);
      FOR_QA_SEGS( sg.trail = val; )

    } else if (strcmp(cmd, "HUESPD") == 0) {
      // Rainbow only: independent hue-cycling speed
      int tick = constrain((int)atol(arg), -5, 5);
      FOR_QA_SEGS( applyHueTick(tick, sg); )

    } else if (strcmp(cmd, "QA") == 0) {
      // switch pattern; crossfade unless either side is Rainbow (instant then)
      uint8_t newQA = constrain((int)atol(arg), 0, 3);
      if (newQA != runQA) {
        if (runQA != PAT_RAINBOW && newQA != PAT_RAINBOW) {
          fromQA = runQA;
          transitionStartMs = millis();
          transitioning = true;
        } else {
          transitioning = false;
        }
        runQA = newQA;
      }

    } else if (strcmp(cmd, "ON") == 0) {
      ledsOn = true;

    } else if (strcmp(cmd, "OFF") == 0) {
      ledsOn = false;

    } else if (strcmp(cmd, "WAVEBRIGHT") == 0) {
      // master brightness for Waves pattern only — scales base + trail/peak proportionally
      waveBrightScale = constrain((int)atol(arg), 0, 100) / 100.0f;

    } else if (strcmp(cmd, "RESET") == 0) {
      // restore the running Quick Access effect to its firmware defaults
      QA[runQA] = QA_ORIGINAL[runQA];
      waveBrightScale = 1.0f;
    }

    #undef FOR_QA_SEGS
  }
};

// ============================================================
// SETUP
// ============================================================
void setup() {
  // Reduced from the 160MHz default to cut active-mode current (LED
  // rendering + low-traffic BLE, no WiFi). RMT and millis()/delay() timing
  // are unaffected, but per-frame render cost is higher in wall-clock terms.
  setCpuFrequencyMhz(80);

  Serial.begin(115200);

  // FastLED must drive WS2812 output via RMT, not a bit-banged path (which
  // would disable interrupts long enough to stall BLE). Print the version
  // at boot to confirm rather than assume.
  Serial.printf("FastLED version: %d\n", FASTLED_VERSION);

  // LED init
  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(255);   // per-pixel brightness now handled per segment
  FastLED.setCorrection(TypicalLEDStrip);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, 3000);
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();

  // Every power cycle boots into Waves at firmware defaults — nothing
  // persists across power loss (unplug/replug is the reset gesture).
  runQA  = PAT_WAVES;
  ledsOn = true;

  // Build Quick Access effects (hardcoded signature looks)
  QA[0] = makeQA(0);    // Default 1: Solid
  QA[1] = makeQA(1);    // Default 2: Breathe
  QA[2] = makeQA(2);    // Default 3: Waves
  QA[3] = makeQA(3);    // Default 4: Rainbow

  // Pristine copies for the RESET command — tuning commands only mutate QA[].
  QA_ORIGINAL[0] = QA[0];
  QA_ORIGINAL[1] = QA[1];
  QA_ORIGINAL[2] = QA[2];
  QA_ORIGINAL[3] = QA[3];

  // BLE
  BLEDevice::init("ESP32-LED");
  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallback());
  BLEService *service = server->createService(BLEUUID(SERVICE_UUID), 20);

  BLECharacteristic *ch = service->createCharacteristic(
    CHARACTERISTIC_UUID, BLECharacteristic::PROPERTY_WRITE);
  ch->setCallbacks(new CmdCallback());

  rssiChar = service->createCharacteristic(
    RSSI_CHAR_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  rssiChar->addDescriptor(new BLE2902());

  stateChar = service->createCharacteristic(
    STATE_CHAR_UUID, BLECharacteristic::PROPERTY_READ);
  stateChar->setCallbacks(new StateCallback());

  service->start();

  // Advertising: name in main packet, UUID in scan response (avoids 31-byte overflow)
  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->setScanResponse(true);
  BLEAdvertisementData advData;
  advData.setName("ESP32-LED");
  adv->setAdvertisementData(advData);
  BLEAdvertisementData scanData;
  scanData.setCompleteServices(BLEUUID(SERVICE_UUID));
  adv->setScanResponseData(scanData);
  BLEDevice::startAdvertising();

  Serial.println("BLE advertising as ESP32-LED");
}

// ============================================================
// ANIMATION LOOP
// ============================================================
// Continuous (fractional) position state, advanced each frame by dt.
static float    ringPosF = 0.0f;                       // Waves ring position (Ring A drives, B follows)
static float    breathePhase[3] = {0.0f, 0.0f, 0.0f};   // one per segment
static uint32_t lastFrameMs = 0;

// Rainbow phase state: position (driven by Speed) and hue (driven by
// Rainbow Speed) accumulate independently and combine at render time.
// Rings A/B share one wheel position; strip has its own.
static float   rbRingPosF = 0.0f;
static uint8_t rbRingHue = 0, rbRingHueTick = 0;
static float   rbStripPosF = 0.0f;
static uint8_t rbStripHue = 0, rbStripHueTick = 0;

// Renders Solid or Breathe for one segment. `phase` is that segment's
// persistent breathe-cycle state.
void renderSegment(uint16_t start, uint16_t count, const SegSettings &sg, float &phase, float dt, CRGB *buf) {
  if (sg.pattern == PAT_SOLID) {
    renderSolid(start, count, sg, buf);
  } else if (sg.pattern == PAT_BREATHE) {
    float rate = sg.speedStep / (float)sg.speedThr;   // cycles/sec (0.5 at neutral speed)
    phase = fmodf(phase + 2.0f * M_PI * rate * dt, 2.0f * M_PI);
    renderBreathe(start, count, sg, phase, buf);
  }
}

// Renders one full preset (Ring A + Strip + Ring B), advancing whatever
// animation state it needs. Called twice during a crossfade (outgoing +
// incoming); safe because every QA slot has a different pattern type, so
// they never share a static animation variable in the same frame.
void renderPreset(const Preset &p, float dt, CRGB *buf) {
  const SegSettings &sgA = p.seg[0];   // Ring A
  const SegSettings &sgS = p.seg[1];   // Strip
  const SegSettings &sgB = p.seg[2];   // Ring B

  // ---- RING A ----
  if (sgA.pattern == PAT_WAVES) {
    // advance shared ring position (Ring A drives it, Ring B follows the same position, in sync)
    ringPosF += ledsPerSecond(sgA) * dt;
    ringPosF = fmodf(ringPosF, (float)RINGA_LEDS);
    if (ringPosF < 0.0f) ringPosF += RINGA_LEDS;
    renderWavesRing(RINGA_START, RINGA_LEDS, sgA, ringPosF, false, buf);
  } else if (sgA.pattern == PAT_RAINBOW) {
    // wheel position advances continuously by dt, same technique as Waves' ringPosF
    rbRingPosF = fmodf(rbRingPosF + ledsPerSecond(sgA) * dt, 256.0f);
    if (rbRingPosF < 0.0f) rbRingPosF += 256.0f;
    if (++rbRingHueTick >= sgA.hueThr)   { rbRingHueTick = 0; rbRingHue += sgA.hueStep; }
    // Negate the combined sum (not just position) to match Waves' rotation direction —
    // renderRainbowRing's hue formula runs the opposite sense to Waves' index, and negating
    // only one term let position and hue-cycling cancel out when their ticks matched.
    renderRainbowRing(RINGA_START, RINGA_LEDS, sgA, (uint8_t)(-((int)rbRingHue + (int)rbRingPosF)), buf);
  } else {
    renderSegment(RINGA_START, RINGA_LEDS, sgA, breathePhase[0], dt, buf);
  }

  // ---- STRIP ----
  if (sgS.pattern == PAT_WAVES) {
    renderWavesStrip(sgS, dt, buf);
  } else if (sgS.pattern == PAT_RAINBOW) {
    // same continuous-position technique as Ring A's rainbow above
    rbStripPosF = fmodf(rbStripPosF + ledsPerSecond(sgS) * dt, 256.0f);
    if (rbStripPosF < 0.0f) rbStripPosF += 256.0f;
    if (++rbStripHueTick >= sgS.hueThr)   { rbStripHueTick = 0; rbStripHue += sgS.hueStep; }
    renderRainbowStrip(STRIP_START, STRIP_LEDS, sgS, (uint8_t)((int)rbStripPosF + rbStripHue), buf);
  } else {
    renderSegment(STRIP_START, STRIP_LEDS, sgS, breathePhase[1], dt, buf);
  }

  // ---- RING B (moves in sync with Ring A) ----
  if (sgB.pattern == PAT_WAVES) {
#if RINGB_MIRROR_ROTATION
    float ringPosBF = fmodf((float)RINGB_LEDS - ringPosF + RINGB_PHASE_OFFSET, (float)RINGB_LEDS);
    if (ringPosBF < 0.0f) ringPosBF += RINGB_LEDS;
    renderWavesRing(RINGB_START, RINGB_LEDS, sgB, ringPosBF, true, buf);   // reversed: head is moving the opposite index direction, so trail must too
#else
    float ringPosBF = fmodf(ringPosF + RINGB_PHASE_OFFSET, (float)RINGB_LEDS);
    if (ringPosBF < 0.0f) ringPosBF += RINGB_LEDS;
    renderWavesRing(RINGB_START, RINGB_LEDS, sgB, ringPosBF, false, buf);
#endif
  } else if (sgB.pattern == PAT_RAINBOW) {
    uint8_t hueA = (uint8_t)(-((int)rbRingHue + (int)rbRingPosF));   // same expression as Ring A's render call above
#if RINGB_MIRROR_ROTATION
    // mirrored mount: wheel runs the opposite hue direction, offset for alignment
    uint8_t hueB = (uint8_t)(255 - hueA + RAINBOW_RINGB_HUE_OFFSET);
    renderRainbowRing(RINGB_START, RINGB_LEDS, sgB, hueB, buf);
#else
    uint8_t hueB = (uint8_t)(hueA + RAINBOW_RINGB_HUE_OFFSET);
    renderRainbowRing(RINGB_START, RINGB_LEDS, sgB, hueB, buf);
#endif
  } else {
    renderSegment(RINGB_START, RINGB_LEDS, sgB, breathePhase[2], dt, buf);
  }
}

void loop() {
  uint32_t frameStartMs = millis();

  // frame delta time, drives smooth animation regardless of loop timing jitter
  float dt = (lastFrameMs == 0) ? 0.0f : (frameStartMs - lastFrameMs) / 1000.0f;
  lastFrameMs = frameStartMs;

  // RSSI ~1/sec
  static uint32_t lastRssiMs = 0;
  if (clientConn && frameStartMs - lastRssiMs > 1000) {
    lastRssiMs = frameStartMs;
    ble_gap_conn_rssi(connHandle, &lastRssi);
    if (rssiChar && lastRssi != 0) {
      char buf[8];
      snprintf(buf, sizeof(buf), "%d", lastRssi);
      rssiChar->setValue((uint8_t*)buf, strlen(buf));
      rssiChar->notify();
    }
  }

  // Push the all-black frame once when LEDs turn off, not every 20ms forever
  // — WS2812 holds its last latched value on its own.
  static bool offFrameSent = false;
  if (!ledsOn) {
    if (!offFrameSent) {
      fill_solid(leds, NUM_LEDS, CRGB::Black);
      FastLED.show();
      offFrameSent = true;
    }
    delay(100);
    return;
  }
  offFrameSent = false;

  Preset *p = &QA[runQA];

  if (transitioning) {
    uint32_t transElapsedMs = frameStartMs - transitionStartMs;
    if (transElapsedMs >= TRANSITION_MS) {
      transitioning = false;
      renderPreset(*p, dt, leds);
    } else {
      // both patterns render live into separate buffers, then get pixel-blended
      renderPreset(QA[fromQA], dt, ledsPrev);   // outgoing
      renderPreset(*p, dt, leds);               // incoming
      uint8_t amt = (uint8_t)((transElapsedMs * 255UL) / TRANSITION_MS);   // 0=outgoing, 255=incoming
      for (int i = 0; i < NUM_LEDS; i++) {
        leds[i] = blend(ledsPrev[i], leds[i], amt);
      }
    }
  } else {
    renderPreset(*p, dt, leds);
  }

  FastLED.show();

  uint32_t elapsedMs = millis() - frameStartMs;
  if (elapsedMs < FRAME_INTERVAL_MS) delay(FRAME_INTERVAL_MS - elapsedMs);
}
