#include <FastLED.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Preferences.h>
#include <math.h>
#include <string.h>

// ============================================================
// LED HARDWARE
// ============================================================
// GPIO5 (the pin used on the classic ESP32 build) is a JTAG pin on the
// Seeed XIAO ESP32-C6 and unusable for general GPIO. D4-D10 carry default
// silkscreen labels for I2C/UART/SPI (D10/GPIO18 = MOSI), but this sketch
// never initializes those peripherals, so the pins are electrically free —
// same reasoning already applied to D3/GPIO21 (nominally SPI SS) before this
// pin was moved here. D10/GPIO18 isn't a JTAG or boot-strapping pin either.
#define LED_PIN      18
#define LED_TYPE     WS2812B
#define COLOR_ORDER  GRB

#define RINGA_LEDS   54
#define STRIP_LEDS   68
#define RINGB_LEDS   54
#define NUM_LEDS     (RINGA_LEDS + STRIP_LEDS + RINGB_LEDS)  // 176
#define RINGA_START  0
#define STRIP_START  RINGA_LEDS
#define RINGB_START  (RINGA_LEDS + STRIP_LEDS)

CRGB leds[NUM_LEDS];
CRGB ledsPrev[NUM_LEDS];  // scratch buffer: holds the outgoing pattern's live render during a crossfade

// ============================================================
// DEFAULT TUNING VALUES
// ------------------------------------------------------------
// Edit the numbers below to change how each Quick Access effect
// looks by default. Brightness values are plain percentages
// (0-100) — no need to do any 0-255 math, that happens
// automatically when the effects are built in makeQA().
//
// SPEED_TICK ranges from -5 (slowest) to +5 (fastest), 0 = neutral.
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
// Set to 1 if Ring B should travel the OPPOSITE index direction from Ring A
// (typical when B is physically mounted as a mirror image of A on the two
// sides of a headset — like two linked/meshed gears, which always turn
// opposite rotational directions but look synchronized where they meet).
// Set to 0 to make Ring B use the exact same raw position as Ring A.
#define RINGB_MIRROR_ROTATION 1

// Fine-tune WHERE Ring B's bright head sits relative to Ring A's at any
// given moment (rotation direction is already handled above — this is only
// about starting alignment). This exists because each ring's LED index 0
// may not be soldered/mounted at the same physical clock position, so the
// mirrored math alone can't guarantee the two heads line up visually.
// Range 0-53. Flash with 0 first, watch where the two heads sit relative to
// each other, then nudge this up (try steps of ~5-10) and reflash until the
// heads visually line up the way you want.
#define RINGB_PHASE_OFFSET  0

// ---- Default 4: Rainbow ----
#define QA4_BRIGHTNESS_PCT        100   // overall brightness (uniform, no peak/base split)
#define QA4_SPEED_TICK              2   // wheel/wave travel speed
#define QA4_RAINBOW_SPEED_TICK      2   // independent hue-cycling speed

// Fine-tune WHERE Ring B's rainbow wheel sits relative to Ring A's, same
// idea as RINGB_PHASE_OFFSET above but expressed as a hue offset (0-255)
// since Rainbow doesn't have discrete LED "positions" the way Waves does.
// Flash with 0 first, watch alignment, then nudge and reflash.
#define RAINBOW_RINGB_HUE_OFFSET  0

// ---- Pattern-switch crossfade ----
// How long a smooth dissolve takes when switching between Solid/Breathe/Waves
// via a Quick Access tap (milliseconds). Both the outgoing and incoming
// patterns render live and get pixel-blended for this long. Rainbow switches
// are always instant — its position+hue math isn't meant to be blended
// against, and dissolving into/out of a full-spectrum wheel didn't look good.
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

Preferences prefs;

// ============================================================
// HELPERS
// ============================================================
// Target ms per animation frame. Defined early (rather than down by loop())
// because renderWavesStrip() and ledsPerSecond() below need it to convert a
// speed tick into a continuous LEDs-per-second rate. The trailing sleep in
// loop() is trimmed by however long rendering + FastLED.show() already took
// this frame, instead of always sleeping a flat interval regardless of
// render cost — that kept the real frame period undefined, so animation
// speed would drift if LED count or effect cost ever changed.
#define FRAME_INTERVAL_MS 20

// percent (0-100) -> 0-255
uint8_t pctTo255(uint8_t pct) {
  return (uint8_t)((pct / 100.0f) * 255);
}

// apply speed tick (-5..+5) to a segment
void applySpeedTick(int tick, SegSettings &sg) {
  if (tick <= 0) {
    sg.speedStep = 1;
    sg.speedThr  = 2 + (-tick) * 2;
  } else {
    sg.speedThr  = 1;
    sg.speedStep = tick;
  }
}

// apply rainbow hue-cycle tick (-5..+5) to a segment — same shape as
// applySpeedTick but drives the independent hueThr/hueStep pair used by
// the Rainbow pattern's "Rainbow Speed" control.
void applyHueTick(int tick, SegSettings &sg) {
  if (tick <= 0) {
    sg.hueStep = 1;
    sg.hueThr  = 2 + (-tick) * 2;
  } else {
    sg.hueThr  = 1;
    sg.hueStep = tick;
  }
}

// Convert a segment's speedStep/speedThr pair (the "every N frames, move M
// pixels" cadence used elsewhere) into a continuous LEDs-per-second rate, so
// Waves motion can be advanced smoothly every frame (accumulated * dt)
// instead of jumping a whole LED only once every speedThr frames — the same
// average speed, but rendered as motion instead of a stutter.
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

// return pointer to the currently running Quick Access effect
Preset* runningPreset() {
  return &QA[runQA];
}

// ============================================================
// RENDERING HELPERS
// ============================================================
// Scale a segment's color by a brightness level (0-255). Uses the
// video-safe scale (nscale8_video) rather than plain nscale8: plain scaling
// is a straight linear multiply that rounds any nonzero channel down to 0
// once brightness gets low enough, which crushes dim regions (Waves' base
// glow, Breathe's dark end, trail tails) to black and loses their hue
// entirely. The video variant guarantees a nonzero channel stays at least 1
// as long as both the channel and the scale are nonzero, so dim colors stay
// visibly tinted instead of clipping to grey/black.
CRGB segColor(const SegSettings &sg, uint8_t bright255) {
  CRGB c = CRGB(sg.r, sg.g, sg.b);
  c.nscale8_video(bright255);
  return c;
}

// Solid: fill the segment range with solid color
void renderSolid(uint16_t start, uint16_t count, const SegSettings &sg, CRGB *buf) {
  fill_solid(buf + start, count, segColor(sg, sg.brightness));
}

// Breathe: brightness eases between minBright and maxBright.
// A raw sine wave, mapped linearly onto LED brightness, actually looks
// "sharp" to the eye: human brightness perception is roughly logarithmic,
// so equal linear steps look bigger and more sudden at the low end than
// at the high end. Two things fix that:
//  1. smoothstep() softens the sine's already-eased endpoints even more,
//     removing any remaining sense of a "snap" at the top/bottom.
//  2. a gamma curve (>1) compresses the low end of the ramp so brightness
//     climbs slowly out of the dark and only accelerates near the top —
//     matching how the eye actually perceives the change.
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

// Waves on strip: two fronts from ends, continuous wrapping trail.
// `dt` (seconds since last frame) drives a continuously-accumulated
// fractional head position instead of hopping a whole LED every speedThr
// frames — see the comment on renderWavesRing below for why.
void renderWavesStrip(const SegSettings &sg, float dt, CRGB *buf) {
  static float headF = 0.0f;
  uint8_t baseBr = (uint8_t)(sg.baseBright * waveBrightScale);
  fill_solid(buf + STRIP_START, STRIP_LEDS, segColor(sg, baseBr));
  // Round up, not down: with an odd STRIP_LEDS, flooring this would leave
  // the exact center LED uncovered by either front (e.g. 69 LEDs: floor(69/2)
  // = 34 means the fronts only ever reach indices 33 and 35, skipping 34).
  // Rounding up instead makes both fronts converge onto that shared center
  // pixel right at the peak, before the wrap. No effect on even LED counts.
  const uint8_t half = (STRIP_LEDS + 1) / 2;
  headF += ledsPerSecond(sg) * dt;
  headF = fmodf(headF, (float)half);
  if (headF < 0.0f) headF += half;
  uint8_t peakBr = (uint8_t)(sg.brightness * waveBrightScale);
  // Cap the trail at half the travel distance to the middle, regardless of
  // the configured trail length. Each front only travels `half` LEDs before
  // snapping back to the edge (see the wrap above) — if the trail were
  // allowed to span that whole distance, it wouldn't finish fading to
  // baseBr before the snap, so the reset would look like a visible chunk of
  // still-lit trail disappearing instead of a clean Snake-style wrap.
  // Capping it to half of that distance guarantees a fully-faded (i.e.
  // invisible) gap before every reset.
  uint8_t effTrail = min(sg.trail, (uint8_t)(half / 2));
  if (effTrail < 1) effTrail = 1;
  int headIdx = (int)floorf(headF);
  // w runs one step past the head (-1) so the not-yet-reached neighbor pixel
  // gets a fractional glow too, instead of staying dark until the head hops
  // onto it — that one extra sample is what turns the motion from a visible
  // per-LED jump into a smooth glide.
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
// head the trail is drawn on — needed when the head's position itself is
// being driven in the opposite index direction (see RINGB_MIRROR_ROTATION),
// otherwise the trail ends up on the wrong side of the head.
//
// `posF` is a continuous (fractional) position rather than a whole LED
// index. Previously the head only ever sat on an integer pixel and hopped
// to the next one once every speedThr frames, which reads as a visible
// stutter, especially at slow speeds. Instead, each candidate pixel's
// brightness is now computed from its exact distance to the continuous head
// position — as posF drifts between two integer pixels, brightness eases
// from one to the other frame by frame, so the same average speed now looks
// like a smooth glide instead of a jump.
void renderWavesRing(uint16_t start, uint8_t count, const SegSettings &sg, float posF, bool reverse, CRGB *buf) {
  uint8_t baseBr = (uint8_t)(sg.baseBright * waveBrightScale);
  fill_solid(buf + start, count, segColor(sg, baseBr));
  uint8_t peakBr = (uint8_t)(sg.brightness * waveBrightScale);
  int headIdx = (int)floorf(posF);
  int dirSign = reverse ? -1 : 1;
  // t runs one step before the head (-1) for the same reason as the strip
  // version above: it lets the not-yet-reached neighbor pixel pick up a
  // fractional glow instead of staying dark until the head lands on it.
  for (int t = -1; t <= (int)sg.trail; t++) {
    // rawIdx is intentionally left unwrapped for the distance calc below —
    // wrapping it into 0..count-1 first (as `idx` does, for array access)
    // and then comparing against posF broke once per revolution: right as
    // the head crosses from the last LED back to the first, the wrapped
    // index and the still-continuous posF would differ by a full trip
    // around the ring, so dist spiked to ~count and the trail dropped to
    // base brightness for a frame — visible as a per-cycle blink.
    int rawIdx = headIdx - dirSign * t;
    int idx = ((rawIdx % count) + count) % count;
    float dist = fabsf(dirSign * (posF - rawIdx));   // continuous distance from head, in LEDs
    if (dist > sg.trail) continue;
    uint8_t trailVal = (uint8_t)(peakBr - dist * (peakBr - baseBr) / sg.trail);
    buf[start + idx] = segColor(sg, trailVal);
  }
}

// Rainbow ring: a smooth full-spectrum color wheel blended around the
// entire ring, all pixels at the same brightness. `startHue` is the
// rotating phase (Speed + Rainbow Speed combined upstream). `reverseDir`
// mirrors the wheel for Ring B, same idea as renderWavesRing's `reverse`.
void renderRainbowRing(uint16_t start, uint8_t count, const SegSettings &sg, uint8_t startHue, CRGB *buf) {
  uint8_t deltaHue = 255 / count;
  for (uint8_t i = 0; i < count; i++) {
    CHSV hsv(startHue + i * deltaHue, 255, sg.brightness);
    CRGB c;
    hsv2rgb_rainbow(hsv, c);
    buf[start + i] = c;
  }
}

// Rainbow strip: a rainbow gradient that appears to flow in from both ends
// toward the center and loop continuously. Hue at each half-strip pixel is
// a function of its distance from the nearest end, mirrored across the
// midpoint; `phase` (Speed + Rainbow Speed combined upstream) shifts that
// mapping over time so the gradient band travels end -> center -> loops.
// Uniform brightness throughout, no fading trail/base.
void renderRainbowStrip(uint16_t start, uint16_t count, const SegSettings &sg, uint8_t phase, CRGB *buf) {
  // Round up, not down — same reasoning as renderWavesStrip: with an odd
  // count, flooring leaves the exact center LED uncovered by either half
  // (e.g. 69 LEDs: floor(69/2) = 34 skips index 34 entirely). No effect on
  // even counts.
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
// FLASH STORAGE
// ============================================================
void saveRunningState() {
  prefs.putUChar("runQA", runQA);
}

void loadRunningState() {
  runQA = prefs.getUChar("runQA", 0);
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

// This board's BLE stack is NimBLE, not Bluedroid (the classic ESP32 build's
// stack) — the C6 Arduino core doesn't ship esp_gap_ble_api.h at all, only
// the NimBLE host headers. NimBLE's RSSI read (ble_gap_conn_rssi) is a
// direct synchronous call keyed off the connection handle, so there's no
// async GAP callback to register here like the Bluedroid version needed.
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

// Reverse-engineer speed tick (-5..+5) from speedThr/speedStep
int8_t getSpeedTick(const SegSettings &sg) {
  if (sg.speedStep > 1) return sg.speedStep;          // positive tick
  return -((int)(sg.speedThr - 2) / 2);               // zero or negative tick
}

// Reverse-engineer rainbow hue-cycle tick (-5..+5) from hueThr/hueStep
int8_t getHueTick(const SegSettings &sg) {
  if (sg.hueStep > 1) return sg.hueStep;
  return -((int)(sg.hueThr - 2) / 2);
}

// Build a state string the webpage can parse on connect.
// Format: "QA:N,ON:0/1,COLOR:RRGGBB,BRIGHT:pct,BASE:pct,MINBR:pct,MAXBR:pct,SPD:tick,TRAIL:val,WBRIGHT:pct"
// Written into a caller-supplied fixed buffer via snprintf rather than
// built with Arduino String concatenation — this runs on every state read
// (e.g. every reconnect), and repeated String += allocates/frees heap each
// time, which can fragment memory on a device that stays powered for weeks.
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

// Longest real command is "WAVEBRIGHT:100" (14 chars); this caps well above
// that so any legitimate write fits, while anything wildly longer — a stray
// reconnect storm, another BLE client scribbling garbage — gets dropped
// before it's even parsed instead of costing a String allocation.
#define MAX_CMD_LEN 32

class CmdCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) {
    // getValue() itself hands back one Arduino String (unavoidable — that's
    // the BLE library's own API); everything downstream copies straight
    // into a fixed stack buffer instead of chaining more String allocations
    // (trim/substring), which is what actually added up on every write.
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

    if (strcmp(cmd, "PAT") == 0) {
      FOR_QA_SEGS( sg.pattern = constrain((int)atol(arg), 0, 3); )

    } else if (strcmp(cmd, "COLOR") == 0 || strcmp(cmd, "QACOLOR") == 0) {
      long rgb = strtol(arg, NULL, 16);
      uint8_t nr=(rgb>>16)&0xFF, ng=(rgb>>8)&0xFF, nb=rgb&0xFF;
      // color is universal — apply to ALL 4 QA slots, all segments
      // (Rainbow ignores r/g/b at render time, but keeping it in sync
      // avoids a stale color reappearing if Rainbow's pattern is ever
      // switched back to something color-driven)
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
      uint8_t val = pctTo255(constrain((int)atol(arg), 0, 100));
      FOR_QA_SEGS( sg.minBright = val; )

    } else if (strcmp(cmd, "MAXBR") == 0) {
      uint8_t val = pctTo255(constrain((int)atol(arg), 0, 100));
      FOR_QA_SEGS( sg.maxBright = val; )

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
      // run a quick access effect. If neither the outgoing nor incoming
      // pattern is Rainbow, crossfade smoothly between them (see loop());
      // Rainbow always switches instantly (see TRANSITION_MS above).
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
        saveRunningState();
      }

    } else if (strcmp(cmd, "ON") == 0) {
      ledsOn = true;

    } else if (strcmp(cmd, "OFF") == 0) {
      ledsOn = false;

    } else if (strcmp(cmd, "WAVEBRIGHT") == 0) {
      // master brightness for Waves pattern only — scales base + trail/peak proportionally
      waveBrightScale = constrain((int)atol(arg), 0, 100) / 100.0f;

    } else if (strcmp(cmd, "RESET") == 0) {
      // restore the currently running Quick Access effect (color + all its
      // sliders) back to its firmware-defined defaults. Other QA slots are
      // untouched, matching how tuning commands only ever affect runQA.
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
  // Drop from the default 160MHz (C6's max, vs 240MHz on the classic ESP32)
  // down to 80MHz — confirmed a valid CPU frequency step for this board
  // (Arduino board menu lists 160/80/40/20/10). Same reasoning as the
  // original ESP32 build: reduces active-mode CPU current for this workload
  // (LED rendering + a low-traffic BLE peripheral, no WiFi). RMT (LED
  // timing) and the hardware timer behind millis()/delay() (frame pacing)
  // both run off clocks independent of this, so they shouldn't be affected —
  // but per-frame render cost (Rainbow's hsv2rgb_rainbow especially) takes
  // noticeably longer in wall-clock terms at this speed, so watch actual
  // frame timing on a real run before trusting this stays inside the
  // FRAME_INTERVAL_MS budget.
  setCpuFrequencyMhz(80);

  Serial.begin(115200);

  // On ESP32, FastLED's WS2812 output should go through the RMT peripheral,
  // not a bit-banged path that disables interrupts for the ~7ms it takes to
  // shift out 228 LEDs — that would be long enough to stall the BLE stack
  // mid-connection-event and cause intermittent disconnects. RMT has been
  // FastLED's ESP32 default since 3.3.x, but print the version at boot so
  // that's a known fact for this build, not an assumption.
  Serial.printf("FastLED version: %d\n", FASTLED_VERSION);

  // LED init
  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(255);   // per-pixel brightness now handled per segment
  FastLED.setCorrection(TypicalLEDStrip);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, 3000);
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();

  // Flash
  prefs.begin("ledapp", false);

  // Restore last running state. On/off intentionally does NOT persist —
  // every power-up starts with the LEDs on, regardless of how they were
  // left before the last power loss.
  loadRunningState();
  ledsOn = true;

  // Build Quick Access effects (hardcoded signature looks)
  QA[0] = makeQA(0);    // Default 1: Solid
  QA[1] = makeQA(1);    // Default 2: Breathe
  QA[2] = makeQA(2);    // Default 3: Waves
  QA[3] = makeQA(3);    // Default 4: Rainbow

  // Keep pristine copies for the RESET command — tuning commands (BRIGHT,
  // BASE, MINBR, MAXBR, SPD, TRAIL, HUESPD, COLOR...) only ever mutate QA[], never this.
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
// per-segment animation state
// ringPosF is a continuous (fractional) LED position rather than a whole
// index — see renderWavesRing's comment for why. stripHead's equivalent
// (headF) now lives inside renderWavesStrip as a static local since nothing
// else needs to read it.
static float    ringPosF = 0.0f;
static float    breathePhase[3] = {0.0f, 0.0f, 0.0f};  // one per segment (RingA, Strip, RingB)
static uint32_t lastFrameMs = 0;

// Rainbow pattern phase state — rings (A & B share one wheel position/hue,
// same idea as ringPosF above) and strip each get their own position phase
// (driven by Speed) and hue phase (driven by Rainbow Speed); the two are
// combined additively at render time. Position now accumulates continuously
// via dt (rbRingPosF/rbStripPosF), the same technique as Waves' ringPosF, so
// rotation speed is smooth and frame-rate independent instead of hopping a
// whole hue step only once every speedThr frames. Hue-cycling (Rainbow
// Speed) stays on the old discrete tick cadence — it's a color-cycle-in-place
// control, not physical rotation, so there's no Waves equivalent to match it to.
static float   rbRingPosF = 0.0f;
static uint8_t rbRingHue = 0, rbRingHueTick = 0;
static float   rbStripPosF = 0.0f;
static uint8_t rbStripHue = 0, rbStripHueTick = 0;

// Render one segment's Solid or Breathe pattern (Waves is handled separately
// per-segment-type in loop(), since ring waves and strip waves use different
// rendering logic). `phase` is that segment's persistent breathe-cycle state.
void renderSegment(uint16_t start, uint16_t count, const SegSettings &sg, float &phase, float dt, CRGB *buf) {
  if (sg.pattern == PAT_SOLID) {
    renderSolid(start, count, sg, buf);
  } else if (sg.pattern == PAT_BREATHE) {
    float rate = sg.speedStep / (float)sg.speedThr;   // cycles/sec (0.5 at neutral speed)
    phase = fmodf(phase + 2.0f * M_PI * rate * dt, 2.0f * M_PI);
    renderBreathe(start, count, sg, phase, buf);
  }
}

// Render one full preset (Ring A + Strip + Ring B) into the given buffer,
// advancing whichever pattern-specific animation state it needs along the
// way. Called once per frame normally; called twice during a crossfade (the
// outgoing preset into ledsPrev, the incoming one into leds — see loop()),
// which is safe because the outgoing and incoming presets are always
// different pattern types (each QA slot has a fixed, unique pattern), so
// they never both touch the same static animation-state variable in the
// same frame.
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
    // advance shared wheel position (Ring A drives it, Ring B follows, mirrored) continuously
    // by dt, same as ringPosF for Waves — smooth motion instead of a per-tick hop. 256 stands
    // in for "LEDs" in ledsPerSecond()'s math since the wheel position is a hue unit (0-255),
    // not an LED index.
    rbRingPosF = fmodf(rbRingPosF + ledsPerSecond(sgA) * dt, 256.0f);
    if (rbRingPosF < 0.0f) rbRingPosF += 256.0f;
    if (++rbRingHueTick >= sgA.hueThr)   { rbRingHueTick = 0; rbRingHue += sgA.hueStep; }
    // Negated, not added: renderRainbowRing bakes hue as startHue + i*deltaHue, so a rising
    // startHue actually shifts the visible band toward decreasing index — the opposite sense
    // from Waves, where a rising ringPosF moves the head toward increasing index. Negating the
    // combined position+hue sum (rather than just position) flips that to match Waves' absolute
    // direction while still letting position and hue-cycling add constructively — negating only
    // one term made them cancel to a constant whenever Speed and Rainbow Speed shared the same
    // tick (e.g. both default to 2), which is what froze the rings entirely.
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

  // frame delta time, used to advance breathe phase smoothly regardless of
  // BLE/RSSI jitter in loop timing
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

  // Once LEDs are off, push the all-black frame exactly once instead of
  // every 20ms forever — WS2812 holds its last latched value on its own,
  // so re-sending unchanged black data bought nothing but kept the CPU
  // fully active the whole time the headset sits powered-on-but-dark.
  // Idling longer here doesn't hurt responsiveness: BLE writes (including
  // the ON command that clears this) run in the BLE stack's own task,
  // independent of loop()'s cadence.
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

  Preset *p = runningPreset();

  if (transitioning) {
    uint32_t transElapsedMs = frameStartMs - transitionStartMs;
    if (transElapsedMs >= TRANSITION_MS) {
      transitioning = false;
      renderPreset(*p, dt, leds);
    } else {
      // Both patterns render live (with their own animation state advancing)
      // every frame of the transition, into separate buffers, then get
      // pixel-blended — a true crossfade rather than a fade-through-black.
      renderPreset(QA[fromQA], dt, ledsPrev);   // outgoing pattern
      renderPreset(*p, dt, leds);               // incoming pattern
      uint8_t amt = (uint8_t)((transElapsedMs * 255UL) / TRANSITION_MS);   // 0=fully outgoing, 255=fully incoming
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
