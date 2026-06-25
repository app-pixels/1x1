/*
 * app_18_mult_v2.cpp — MultiplicationTable v2 (refactored from 18_MultiplicationTable_v2.ino)
 *
 * Controls:
 *   PWR  short press — cycle cursor through tables 1-10
 *   BOOT short press — toggle table / answer depending on overlay state
 */

#include "app_18_mult_v2.h"
#include "app_common.h"
#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include <Preferences.h>
#include "canvas/Arduino_Canvas.h"
#include "pin_config.h"
#include "HWCDC.h"
#include <Adafruit_XCA9554.h>

extern USBCDC USBSerial;
extern Arduino_Canvas *g_canvas;

#define BOOT_BTN       0
#define PWR_POLL_MS    50
#define OVERLAY_MS     2000

// ── Display ───────────────────────────────────────────────────────────────────
static Arduino_Canvas *canvas = nullptr;

// ── GPIO expander ─────────────────────────────────────────────────────────────
static Adafruit_XCA9554 expander;

// ── NVS ───────────────────────────────────────────────────────────────────────
static Preferences prefs;

// ── Colour palette ────────────────────────────────────────────────────────────
static const uint16_t PALETTE[8] = {
  0xF800, 0xFD20, 0xFFE0, 0x07E0,
  0x07FF, 0x801F, 0xF81F, 0xFB56,
};

// ── Main text layout ──────────────────────────────────────────────────────────
#define TEXT_SX     9
#define TEXT_SY     12
#define TEXT_MARGIN 2
#define CHAR_W      (6 * TEXT_SX + TEXT_MARGIN)
#define CHAR_H      (8 * TEXT_SY)
#define DOT_R       8
#define DOT_CY_OFF  (CHAR_H * 4 / 10)
#define DOT_SLOT    (CHAR_W + 14)
#define QUESTION_CY  (LCD_HEIGHT * 5 / 16)
#define ANSWER_CY    (LCD_HEIGHT * 11 / 16)

// ── Selector strip ────────────────────────────────────────────────────────────
#define SEL_TEXTSIZE  2
#define SEL_CHARW     (6 * SEL_TEXTSIZE)
#define SEL_SLOT      (LCD_WIDTH / 10)
#define SEL_AREA_Y    (LCD_HEIGHT - 40)
#define SEL_NUM_Y     (LCD_HEIGHT - 36)
#define SEL_UL_Y      (LCD_HEIGHT - 18)

// ── UI colours ────────────────────────────────────────────────────────────────
#define COL_SEL_ACTIVE  0x4A49
#define COL_SEL_IDLE    0x18C3
#define COL_CURSOR_UL   0x738E

// ── Application state ─────────────────────────────────────────────────────────
enum A18State { A18_QUESTION, A18_ANSWER, A18_CELEBRATION };
static A18State appState         = A18_QUESTION;
static int      qa, qb;
static bool     tableSelected[10];
static uint8_t  cursorPos        = 0;
static int      answeredCount    = 0;
#define CELEBRATE_EVERY 10

static bool     overlayVisible   = false;
static uint32_t overlayTimer     = 0;
static bool     bootWasDown      = false;
static uint32_t lastPwrCheck     = 0;

// ── NVS helpers ───────────────────────────────────────────────────────────────
static void saveSelection() {
  char key[3] = {'t', (char)('0' + cursorPos), '\0'};
  prefs.putBool(key, tableSelected[cursorPos]);
}

static void loadSelection() {
  for (int i = 0; i < 10; i++) {
    char key[3] = {'t', (char)('0' + i), '\0'};
    tableSelected[i] = prefs.getBool(key, true);
  }
}

// ── Helpers ───────────────────────────────────────────────────────────────────
static uint16_t questionColor() { return PALETTE[(qa * qb) % 8]; }

static int countSelected() {
  int n = 0;
  for (int i = 0; i < 10; i++) if (tableSelected[i]) n++;
  return n;
}

static void newQuestion() {
  int active[10], n = 0;
  for (int i = 0; i < 10; i++)
    if (tableSelected[i]) active[n++] = i + 1;
  qa = (n > 0) ? active[random(0, n)] : random(1, 11);
  qb = random(1, 11);
}

// ── Draw: selector strip ──────────────────────────────────────────────────────
static void drawTableSelector() {
  canvas->setTextSize(SEL_TEXTSIZE);
  for (int i = 0; i < 10; i++) {
    int cx = i * SEL_SLOT + SEL_SLOT / 2;
    char buf[4];
    sprintf(buf, "%d", i + 1);
    int16_t tw = strlen(buf) * SEL_CHARW;
    canvas->setTextColor(tableSelected[i] ? COL_SEL_ACTIVE : COL_SEL_IDLE);
    canvas->setCursor(cx - tw / 2, SEL_NUM_Y);
    canvas->print(buf);
    if (i == cursorPos) {
      canvas->drawFastHLine(cx - 8, SEL_UL_Y, 16, COL_CURSOR_UL);
    }
  }
}

// ── Overlay helpers ───────────────────────────────────────────────────────────
static void showOverlay() {
  overlayVisible = true;
  overlayTimer   = millis();
}

static void hideOverlay() {
  overlayVisible = false;
  canvas->fillRect(0, SEL_AREA_Y, LCD_WIDTH, LCD_HEIGHT - SEL_AREA_Y, 0x0000);
  draw_battery_g(canvas, LCD_WIDTH, LCD_HEIGHT);
  draw_watermark_g(canvas, LCD_WIDTH, LCD_HEIGHT);
  canvas->flush();
}

static void refreshSelector() {
  canvas->fillRect(0, SEL_AREA_Y, LCD_WIDTH, LCD_HEIGHT - SEL_AREA_Y, 0x0000);
  drawTableSelector();
  canvas->flush();
}

// ── Draw: question ────────────────────────────────────────────────────────────
static void drawQuestion() {
  canvas->fillScreen(0x0000);

  char left[4], right[4];
  sprintf(left,  "%d", qa);
  sprintf(right, "%d", qb);
  int ll = strlen(left), rl = strlen(right);

  int16_t totalW = ll * CHAR_W + DOT_SLOT + rl * CHAR_W;
  int16_t startX = (LCD_WIDTH - totalW) / 2;
  int16_t y      = QUESTION_CY - CHAR_H / 2;

  uint16_t col = questionColor();
  canvas->setTextSize(TEXT_SX, TEXT_SY, TEXT_MARGIN);
  canvas->setTextColor(col);
  canvas->setCursor(startX, y);
  canvas->print(left);
  canvas->fillCircle(startX + ll * CHAR_W + DOT_SLOT / 2, y + DOT_CY_OFF, DOT_R, col);
  canvas->setCursor(startX + ll * CHAR_W + DOT_SLOT, y);
  canvas->print(right);

  draw_battery_g(canvas, LCD_WIDTH, LCD_HEIGHT);
  draw_watermark_g(canvas, LCD_WIDTH, LCD_HEIGHT);
  if (overlayVisible) {
    drawTableSelector();
  }
  canvas->flush();
}

// ── Draw: answer ──────────────────────────────────────────────────────────────
static void drawAnswer() {
  char buf[8];
  sprintf(buf, "= %d", qa * qb);
  int16_t x = (LCD_WIDTH - (int16_t)strlen(buf) * CHAR_W) / 2;
  int16_t y = ANSWER_CY - CHAR_H / 2;
  canvas->setTextSize(TEXT_SX, TEXT_SY, TEXT_MARGIN);
  canvas->setTextColor(0xFFFF);
  canvas->setCursor(x, y);
  canvas->print(buf);
  draw_battery_g(canvas, LCD_WIDTH, LCD_HEIGHT);
  draw_watermark_g(canvas, LCD_WIDTH, LCD_HEIGHT);
  canvas->flush();
}

// ── Celebration ───────────────────────────────────────────────────────────────
static void drawCelebration() {
  overlayVisible = false;
  canvas->fillScreen(0x0000);
  int cx = LCD_WIDTH  / 2;
  int cy = LCD_HEIGHT / 2;
  uint16_t col = PALETTE[random(0, 8)];

  const int R = 130, r = 52;
  int px[10], py[10];
  for (int i = 0; i < 5; i++) {
    float a1 = -M_PI / 2.0f + i * 2.0f * M_PI / 5.0f;
    float a2 = a1 + M_PI / 5.0f;
    px[2*i]   = cx + (int)(R * cosf(a1));
    py[2*i]   = cy + (int)(R * sinf(a1));
    px[2*i+1] = cx + (int)(r * cosf(a2));
    py[2*i+1] = cy + (int)(r * sinf(a2));
  }
  for (int i = 0; i < 10; i++) {
    int j = (i + 1) % 10;
    canvas->fillTriangle(cx, cy, px[i], py[i], px[j], py[j], col);
  }
  draw_battery_g(canvas, LCD_WIDTH, LCD_HEIGHT);
  draw_watermark_g(canvas, LCD_WIDTH, LCD_HEIGHT);
  canvas->flush();
}

// ── Button handlers ───────────────────────────────────────────────────────────
static void handleBootPress() {
  if (overlayVisible) {
    tableSelected[cursorPos] = !tableSelected[cursorPos];
    if (countSelected() == 0) tableSelected[cursorPos] = true;
    saveSelection();
    showOverlay();
    refreshSelector();
  } else if (appState == A18_CELEBRATION) {
    newQuestion();
    appState = A18_QUESTION;
    drawQuestion();
  } else {
    if (appState == A18_QUESTION) {
      appState = A18_ANSWER;
      drawAnswer();
    } else {
      answeredCount++;
      newQuestion();
      if (answeredCount % CELEBRATE_EVERY == 0) {
        appState = A18_CELEBRATION;
        drawCelebration();
      } else {
        appState = A18_QUESTION;
        drawQuestion();
      }
    }
  }
}

static void handlePwrShortPress() {
  cursorPos = (cursorPos + 1) % 10;
  showOverlay();
  refreshSelector();
}

// ── App entry points ──────────────────────────────────────────────────────────
void app18_setup(Arduino_OLED *passed_gfx) {
  (void)passed_gfx;
  canvas = g_canvas;
  pinMode(BOOT_BTN, INPUT_PULLUP);
  randomSeed(esp_random());

  prefs.begin("mult-table", false);
  loadSelection();

  // Wire already initialised by launcher
  if (!expander.begin(0x20)) USBSerial.println("XCA9554 init failed");
  expander.pinMode(5, INPUT);
  expander.pinMode(4, INPUT);
  expander.pinMode(1, OUTPUT);
  expander.pinMode(2, OUTPUT);
  expander.digitalWrite(1, LOW);
  expander.digitalWrite(2, LOW);
  delay(20);
  expander.digitalWrite(1, HIGH);
  expander.digitalWrite(2, HIGH);

  // PMU already initialised and configured by launcher; battery detection
  // enabled via common_init().  Nothing to do here.

  // Display already initialised by launcher
  appState      = A18_QUESTION;
  overlayVisible = false;
  bootWasDown   = false;
  lastPwrCheck  = 0;
  answeredCount = 0;
  cursorPos     = 0;

  newQuestion();
  drawQuestion();
}

void app18_loop() {
  common_tick();
  uint32_t now = millis();

  bool bootDown = (digitalRead(BOOT_BTN) == LOW);
  if (bootDown && !bootWasDown) {
    common_activity();
    handleBootPress();
  }
  bootWasDown = bootDown;

  if (common_consume_pwr_short()) {
    common_activity();
    handlePwrShortPress();
  }

  if (overlayVisible && (millis() - overlayTimer >= OVERLAY_MS)) {
    hideOverlay();
  }

  delay(10);
}
