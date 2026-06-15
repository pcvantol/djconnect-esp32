// Rendering code for the 320x170 ST7789 screen.
// This file intentionally contains only presentation logic: it reads state snapshots and draws them.
#include "DisplayManager.h"

#include "AppLog.h"

#include <LittleFS.h>
#include <TJpg_Decoder.h>
#include <WiFi.h>
#include <esp_heap_caps.h>

#include "Config.h"
#include "I18n.h"
#include "LogicHelpers.h"
#include "TextHelpers.h"
#include "assets/DJConnectIcon160.h"

static constexpr uint16_t BrightYellow = 0xFFE0;
static constexpr uint16_t BrightPurple = 0xB81F;
static constexpr uint16_t GameBlue = 0x04FF;
static constexpr uint16_t GameLightBlue = 0x867F;
static constexpr uint16_t GamePink = 0xF81F;
static constexpr uint16_t GameBrown = 0xA2C6;
static constexpr uint16_t GameDarkBrown = 0x6203;
static constexpr uint16_t NeutralLightGrey = 0xC618;
static constexpr uint16_t StatusGreen = 0x1DCB;
static constexpr uint16_t VolumeOrange = 0xFD20;
static constexpr int AlbumTextX = 172;
static constexpr int AlbumTextWidth = 140;
static constexpr int AlbumTitleY = 34;
static constexpr int AlbumTitleHeight = 38;
static constexpr int AlbumArtistY = 82;
static constexpr int AlbumArtistHeight = 30;
static TFT_eSPI *JpegTarget = nullptr;
static int16_t JpegClipRight = 0;
static int16_t JpegClipBottom = 0;

template <typename Canvas>
static int canvasWidth(Canvas &canvas) {
  return canvas.width();
}

template <typename Canvas>
static int canvasHeight(Canvas &canvas) {
  return canvas.height();
}

template <typename Canvas>
static bool tallCanvas(Canvas &canvas) {
  return canvasHeight(canvas) >= 220;
}

template <typename Canvas>
static int footerY(Canvas &canvas) {
  return max(0, canvasHeight(canvas) - 19);
}

template <typename Canvas>
static int contentWidth(Canvas &canvas) {
  return max(0, canvasWidth(canvas) - 16);
}

static bool jpegOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap) {
  if (JpegTarget == nullptr) {
    return false;
  }
  if (y >= JpegTarget->height() || (JpegClipBottom > 0 && y >= JpegClipBottom)) {
    return false;
  }
  if (JpegClipRight > 0 && x >= JpegClipRight) {
    return true;
  }

  if (JpegClipRight > 0 && x + static_cast<int16_t>(w) > JpegClipRight) {
    w = JpegClipRight - x;
  }
  if (JpegClipBottom > 0 && y + static_cast<int16_t>(h) > JpegClipBottom) {
    h = JpegClipBottom - y;
  }
  if (w == 0 || h == 0) {
    return true;
  }

  JpegTarget->pushImage(x, y, w, h, bitmap);
  return true;
}

static uint8_t jpegScaleFor(uint16_t width, uint16_t height) {
  const uint16_t largest = max(width, height);
  if (largest >= 600) {
    return 4;
  }
  if (largest >= 300) {
    return 2;
  }
  return 1;
}

static String localizedStatusText(const String &text) {
  if (text == "Playback credentials unavailable") {
    return I18n::text("playback_credentials_unavailable");
  }
  if (text == "Spotify not connected" || text == "Playback not connected" || text == "Not connected") {
    return I18n::text("spotify_not_connected");
  }
  if (text == "Spotify connected" || text == "Playback connected" || text == "Spotify authorized") {
    return I18n::text("spotify_connected");
  }
  if (text == "WiFi disconnected") {
    return I18n::text("wifi_disconnected");
  }
  return text;
}

void DisplayManager::begin() {
  pinMode(Config::DisplayBacklightPin, OUTPUT);
  digitalWrite(Config::DisplayBacklightPin, LOW);

  // Power-enable is shared by board peripherals; assert it before display and WS2812 work.
  if constexpr (Config::HasBoardPowerEnable) {
    pinMode(Config::BoardPowerEnablePin, OUTPUT);
    digitalWrite(Config::BoardPowerEnablePin, HIGH);
  }

  if constexpr (Config::HasSdCardChipSelect) {
    pinMode(Config::SdCardChipSelectPin, OUTPUT);
    digitalWrite(Config::SdCardChipSelectPin, HIGH);
  }
  if constexpr (Config::HasLoraChipSelect) {
    pinMode(Config::LoraChipSelectPin, OUTPUT);
    digitalWrite(Config::LoraChipSelectPin, HIGH);
  }

  // PWM backlight allows 100%/10%/0% idle brightness instead of only on/off.
  ledcAttach(
      Config::DisplayBacklightPin,
      Config::DisplayBacklightPwmFrequency,
      Config::DisplayBacklightPwmResolution);
  setBacklightPercent(0);

  tft_.init();
  tft_.setRotation(Config::DisplayRotation);
  tft_.setTextDatum(TL_DATUM);
  tft_.fillScreen(TFT_BLACK);
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(jpegOutput);

  // A sprite buffer eliminates the old full-screen flicker from direct redraws.
  screen_.setColorDepth(8);
  screenBufferReady_ = screen_.createSprite(tft_.width(), tft_.height()) != nullptr;
  if (screenBufferReady_) {
    screen_.setTextDatum(TL_DATUM);
  }

  AppLog.print("Display buffer: ");
  AppLog.print(tft_.width());
  AppLog.print("x");
  AppLog.print(tft_.height());
  AppLog.print(" rotation=");
  AppLog.print(Config::DisplayRotation);
  AppLog.println(screenBufferReady_ ? " OK" : " FAILED, using direct draw");
}

void DisplayManager::showBootMessage(const String &message) {
  if (screenBufferReady_) {
    screen_.fillSprite(TFT_BLACK);
    renderBoot(screen_, message);
    screen_.pushSprite(0, 0);
    wakeForUserActivity();
    return;
  }

  tft_.fillScreen(TFT_BLACK);
  renderBoot(tft_, message);
  wakeForUserActivity();
}

void DisplayManager::showBootMessage(const String &message, const BatteryState &battery) {
  if (screenBufferReady_) {
    screen_.fillSprite(TFT_BLACK);
    renderBoot(screen_, message, &battery);
    screen_.pushSprite(0, 0);
    wakeForUserActivity();
    return;
  }

  tft_.fillScreen(TFT_BLACK);
  renderBoot(tft_, message, &battery);
  wakeForUserActivity();
}

void DisplayManager::showSplashScreen() {
  if (screenBufferReady_) {
    screen_.fillSprite(TFT_BLACK);
    renderBoot(screen_, "");
    screen_.pushSprite(0, 0);
    wakeForUserActivity();
    return;
  }

  tft_.fillScreen(TFT_BLACK);
  renderBoot(tft_, "");
  wakeForUserActivity();
}

void DisplayManager::showSplashScreen(const BatteryState &battery) {
  if (screenBufferReady_) {
    screen_.fillSprite(TFT_BLACK);
    renderBoot(screen_, "", &battery);
    screen_.pushSprite(0, 0);
    wakeForUserActivity();
    return;
  }

  tft_.fillScreen(TFT_BLACK);
  renderBoot(tft_, "", &battery);
  wakeForUserActivity();
}

void DisplayManager::showPairingCode(const String &pairCode) {
  if (screenBufferReady_) {
    screen_.fillSprite(TFT_BLACK);
    renderPairingCode(screen_, pairCode);
    screen_.pushSprite(0, 0);
    wakeForUserActivity();
    return;
  }

  tft_.fillScreen(TFT_BLACK);
  renderPairingCode(tft_, pairCode);
  wakeForUserActivity();
}

void DisplayManager::showPairingCode(const String &pairCode, const BatteryState &battery) {
  if (screenBufferReady_) {
    screen_.fillSprite(TFT_BLACK);
    renderPairingCode(screen_, pairCode, &battery);
    screen_.pushSprite(0, 0);
    wakeForUserActivity();
    return;
  }

  tft_.fillScreen(TFT_BLACK);
  renderPairingCode(tft_, pairCode, &battery);
  wakeForUserActivity();
}

void DisplayManager::renderPlaybackScreen(
    const SpotifyState &playback,
    const BatteryState &battery,
    const StatusNotice &notice,
    int displayedVolume,
    bool homeAssistantConnected,
    PlaybackConnectionState playbackConnectionState) {
  // Observing text here lets redraws notice metadata changes without app-level display bookkeeping.
  observeText(titleMarquee_, titleText(playback));
  observeText(artistMarquee_, artistText(playback));

  if (screenBufferReady_) {
    screen_.fillSprite(TFT_BLACK);
    renderPlayback(screen_, playback, battery, notice, displayedVolume, homeAssistantConnected, playbackConnectionState);
    screen_.pushSprite(0, 0);
    return;
  }

  tft_.fillScreen(TFT_BLACK);
  renderPlayback(tft_, playback, battery, notice, displayedVolume, homeAssistantConnected, playbackConnectionState);
}

void DisplayManager::renderMenuList(
    const String &title,
    const MenuItemView *items,
    size_t itemCount,
    size_t selectedIndex,
    const StatusNotice &notice) {
  if (screenBufferReady_) {
    screen_.fillSprite(TFT_BLACK);
    renderMenu(screen_, title, items, itemCount, selectedIndex, notice);
    screen_.pushSprite(0, 0);
    return;
  }

  tft_.fillScreen(TFT_BLACK);
  renderMenu(tft_, title, items, itemCount, selectedIndex, notice);
}

void DisplayManager::renderAboutScreen(const StatusNotice &notice, const AboutStatus &status, size_t selectedIndex) {
  if (screenBufferReady_) {
    screen_.fillSprite(TFT_BLACK);
    renderAbout(screen_, notice, status, selectedIndex);
    screen_.pushSprite(0, 0);
    return;
  }

  tft_.fillScreen(TFT_BLACK);
  renderAbout(tft_, notice, status, selectedIndex);
}

void DisplayManager::renderLowBatteryScreen(const BatteryState &battery, const String &message) {
  if (screenBufferReady_) {
    screen_.fillSprite(TFT_BLACK);
    renderBoot(screen_, "Battery " + String(battery.percent) + "%", &battery);
    screen_.setTextColor(TFT_RED, TFT_BLACK);
    screen_.drawString(message, 14, footerY(screen_) - 5, 2);
    screen_.pushSprite(0, 0);
    wakeForUserActivity();
    return;
  }

  tft_.fillScreen(TFT_BLACK);
  renderBoot(tft_, "Battery " + String(battery.percent) + "%", &battery);
  tft_.setTextColor(TFT_RED, TFT_BLACK);
  tft_.drawString(message, 14, footerY(tft_) - 5, 2);
  wakeForUserActivity();
}

void DisplayManager::renderLogsScreen(const String *lines, size_t lineCount, const StatusNotice &notice) {
  if (screenBufferReady_) {
    screen_.fillSprite(TFT_BLACK);
    renderLogs(screen_, lines, lineCount, notice);
    screen_.pushSprite(0, 0);
    return;
  }

  tft_.fillScreen(TFT_BLACK);
  renderLogs(tft_, lines, lineCount, notice);
}

void DisplayManager::renderPongScreen(int paddleY, int ballX, int ballY, int score, int highScore, bool missFlash, const StatusNotice &notice) {
  if (screenBufferReady_) {
    screen_.fillSprite(TFT_BLACK);
    renderPong(screen_, paddleY, ballX, ballY, score, highScore, missFlash, notice);
    screen_.pushSprite(0, 0);
    return;
  }
  tft_.fillScreen(TFT_BLACK);
  renderPong(tft_, paddleY, ballX, ballY, score, highScore, missFlash, notice);
}

void DisplayManager::renderAsteroidsScreen(int shipX, int shipY, int asteroidX, int asteroidY, int bulletY, bool bulletActive, int score, int highScore, bool hitFlash, const StatusNotice &notice) {
  if (screenBufferReady_) {
    screen_.fillSprite(TFT_BLACK);
    renderAsteroids(screen_, shipX, shipY, asteroidX, asteroidY, bulletY, bulletActive, score, highScore, hitFlash, notice);
    screen_.pushSprite(0, 0);
    return;
  }
  tft_.fillScreen(TFT_BLACK);
  renderAsteroids(tft_, shipX, shipY, asteroidX, asteroidY, bulletY, bulletActive, score, highScore, hitFlash, notice);
}

void DisplayManager::renderFlyerScreen(int planeY, int obstacleX, int obstacleY, int shotX, bool shotActive, int score, int highScore, bool hitFlash, const StatusNotice &notice) {
  if (screenBufferReady_) {
    screen_.fillSprite(TFT_BLACK);
    renderFlyer(screen_, planeY, obstacleX, obstacleY, shotX, shotActive, score, highScore, hitFlash, notice);
    screen_.pushSprite(0, 0);
    return;
  }
  tft_.fillScreen(TFT_BLACK);
  renderFlyer(tft_, planeY, obstacleX, obstacleY, shotX, shotActive, score, highScore, hitFlash, notice);
}

void DisplayManager::renderMazeChaseScreen(int playerX, int playerLane, int ghostX, int ghostLane, int pelletX, int pelletLane, int score, int highScore, bool ghostVulnerable, bool hitFlash, const StatusNotice &notice) {
  if (screenBufferReady_) {
    screen_.fillSprite(TFT_BLACK);
    renderMazeChase(screen_, playerX, playerLane, ghostX, ghostLane, pelletX, pelletLane, score, highScore, ghostVulnerable, hitFlash, notice);
    screen_.pushSprite(0, 0);
    return;
  }
  tft_.fillScreen(TFT_BLACK);
  renderMazeChase(tft_, playerX, playerLane, ghostX, ghostLane, pelletX, pelletLane, score, highScore, ghostVulnerable, hitFlash, notice);
}

void DisplayManager::renderAlbumArtScreen(
    const SpotifyState &playback,
    const StatusNotice &notice,
    const String &imagePath,
    const String &albumArtStatus) {
  observeText(titleMarquee_, titleText(playback));
  observeText(artistMarquee_, artistText(playback));

  tft_.setTextDatum(TL_DATUM);
  const int w = tft_.width();
  const int h = tft_.height();
  const bool tall = h >= 220;
  const int artPaneW = min(168, max(120, w / 2 + 8));
  const int artSize = min(160, min(artPaneW - 8, h - 10));
  const int artX = 4;
  const int artY = 4;
  const int textX = artPaneW + 4;
  const int textW = max(80, w - textX - 8);
  const int titleY = tall ? 44 : AlbumTitleY;
  const int artistY = tall ? 104 : AlbumArtistY;

  if (albumArtPaneDirty_ || imagePath != lastAlbumArtPath_) {
    lastAlbumArtPath_ = imagePath;
    albumArtPaneDirty_ = false;
    tft_.fillRect(0, 0, artPaneW, h, TFT_BLACK);
    tft_.drawRect(artX, artY, artSize, artSize, TFT_DARKGREY);
    if (!imagePath.isEmpty() && LittleFS.exists(imagePath)) {
      uint16_t jpegWidth = 0;
      uint16_t jpegHeight = 0;
      TJpgDec.getFsJpgSize(&jpegWidth, &jpegHeight, imagePath, LittleFS);
      if (jpegWidth == 0 || jpegHeight == 0) {
        AppLog.print("Album art: invalid JPEG ");
        AppLog.println(imagePath);
        tft_.setTextColor(TFT_DARKGREY, TFT_BLACK);
        tft_.drawString(clippedText(tft_, albumArtStatus, artSize - 28, 2),
                        artX + 14,
                        artY + (artSize / 2) - 8,
                        2);
      } else {
        const uint8_t scale = jpegScaleFor(jpegWidth, jpegHeight);
        const int drawWidth = jpegWidth / scale;
        const int drawHeight = jpegHeight / scale;

        AppLog.print("Album art: render ");
        AppLog.print(jpegWidth);
        AppLog.print("x");
        AppLog.print(jpegHeight);
        AppLog.print(" scale=");
        AppLog.print(scale);
        AppLog.print(" free=");
        AppLog.print(heap_caps_get_free_size(MALLOC_CAP_8BIT));
        AppLog.print(" largest=");
        AppLog.println(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        JpegTarget = &tft_;
        JpegClipRight = artX + artSize;
        JpegClipBottom = artY + artSize;
        TJpgDec.setJpgScale(scale);
        const uint8_t result = TJpgDec.drawFsJpg(artX + 1 + max(0, (artSize - 2 - drawWidth) / 2),
                                                 artY + 1 + max(0, (artSize - 2 - drawHeight) / 2),
                                                 imagePath,
                                                 LittleFS);
        JpegTarget = nullptr;
        JpegClipRight = 0;
        JpegClipBottom = 0;
        if (result != 0) {
          AppLog.print("Album art: JPEG render failed code=");
          AppLog.println(result);
        }
      }
    } else {
      tft_.setTextColor(TFT_DARKGREY, TFT_BLACK);
      const String fallback = albumArtStatus.isEmpty() ? I18n::text("album_art_no_art") : albumArtStatus;
      tft_.drawString(clippedText(tft_, fallback, artSize - 28, 2), artX + 14, artY + (artSize / 2) - 8, 2);
    }
  }

  tft_.fillRect(artPaneW, 0, w - artPaneW, h, TFT_BLACK);
  tft_.setTextColor(BrightYellow, TFT_BLACK);
  tft_.drawString(I18n::text("current_song"), textX, 8, 2);

  tft_.setTextColor(TFT_WHITE, TFT_BLACK);
  drawMarqueeText(tft_, titleMarquee_, titleText(playback), textX, titleY, textW, 4, AlbumTitleHeight);

  tft_.setTextColor(BrightPurple, TFT_BLACK);
  drawMarqueeText(tft_, artistMarquee_, artistText(playback), textX, artistY, textW, 4, AlbumArtistHeight);

  if (notice.isVisible()) {
    tft_.setTextColor(BrightPurple, TFT_BLACK);
    tft_.drawString(clippedText(tft_, notice.message, textW, 2), textX, h - 30, 2);
  }

  tft_.setTextColor(TFT_WHITE, TFT_BLACK);
  tft_.drawString(I18n::text("back_top_button"), textX, h - 16, 1);
}

void DisplayManager::renderAlbumArtMarqueeText(const SpotifyState &playback, bool titleChanged, bool artistChanged) {
  tft_.setTextDatum(TL_DATUM);
  const int w = tft_.width();
  const int h = tft_.height();
  const bool tall = h >= 220;
  const int artPaneW = min(168, max(120, w / 2 + 8));
  const int textX = artPaneW + 4;
  const int textW = max(80, w - textX - 8);
  const int titleY = tall ? 44 : AlbumTitleY;
  const int artistY = tall ? 104 : AlbumArtistY;
  if (titleChanged) {
    tft_.fillRect(artPaneW, titleY, w - artPaneW, AlbumTitleHeight + 4, TFT_BLACK);
    tft_.setTextColor(TFT_WHITE, TFT_BLACK);
    drawMarqueeText(tft_, titleMarquee_, titleText(playback), textX, titleY, textW, 4, AlbumTitleHeight);
  }
  if (artistChanged) {
    tft_.fillRect(artPaneW, artistY, w - artPaneW, AlbumArtistHeight + 4, TFT_BLACK);
    tft_.setTextColor(BrightPurple, TFT_BLACK);
    drawMarqueeText(tft_, artistMarquee_, artistText(playback), textX, artistY, textW, 4, AlbumArtistHeight);
  }
}

void DisplayManager::renderDjResponseOverlay(const String &title, const String &text) {
  if (title == lastDjResponseOverlayTitle_ && text == lastDjResponseOverlayText_) {
    return;
  }
  lastDjResponseOverlayTitle_ = title;
  lastDjResponseOverlayText_ = text;
  tft_.fillScreen(TFT_BLACK);
  tft_.setTextDatum(TL_DATUM);
  const int w = tft_.width();
  const int h = tft_.height();
  const bool tall = h >= 220;

  drawDJConnectIcon(tft_, 18, tall ? 18 : 14, tall ? 62 : 54);
  tft_.setTextColor(BrightPurple, TFT_BLACK);
  tft_.drawString(clippedText(tft_, title.isEmpty() ? String("DJConnect") : title, w - 102, 4),
                  tall ? 94 : 84,
                  tall ? 34 : 26,
                  4);

  String remaining = text;
  remaining.trim();
  const int x = 18;
  int y = tall ? 106 : 86;
  const int maxWidth = w - 36;
  const int lineHeight = 30;
  const uint8_t font = remaining.length() > 58 ? 2 : 4;
  const int actualLineHeight = font == 4 ? lineHeight : 22;

  while (!remaining.isEmpty() && y < h - 8) {
    String line;
    while (!remaining.isEmpty()) {
      int space = remaining.indexOf(' ');
      String word = space < 0 ? remaining : remaining.substring(0, space);
      String candidate = line.isEmpty() ? word : line + " " + word;
      if (!line.isEmpty() && tft_.textWidth(candidate, font) > maxWidth) {
        break;
      }
      line = candidate;
      remaining = space < 0 ? "" : remaining.substring(space + 1);
      remaining.trim();
    }
    if (line.isEmpty()) {
      line = remaining;
      remaining = "";
    }
    tft_.setTextColor(BrightPurple, TFT_BLACK);
    tft_.drawString(line, x, y, font);
    y += actualLineHeight;
  }
}

void DisplayManager::resetDjResponseOverlayCache() {
  lastDjResponseOverlayText_ = "";
  lastDjResponseOverlayTitle_ = "";
}

void DisplayManager::resetAlbumArtRenderCache() {
  albumArtPaneDirty_ = true;
}

bool DisplayManager::advanceTitleScrollIfNeeded(const SpotifyState &playback, int maxWidth, uint8_t font) {
  return advanceMarqueeIfNeeded(titleMarquee_, titleText(playback), font, maxWidth);
}

bool DisplayManager::advanceArtistScrollIfNeeded(const SpotifyState &playback, int maxWidth, uint8_t font) {
  return advanceMarqueeIfNeeded(artistMarquee_, artistText(playback), font, maxWidth);
}

void DisplayManager::restartTitleScroll() {
  restartMarquee(titleMarquee_);
}

void DisplayManager::restartArtistScroll() {
  restartMarquee(artistMarquee_);
}

void DisplayManager::wakeForUserActivity() {
  lastUserActivityAt_ = millis();
  setBacklightPercent(activeBrightnessPercent_);
}

void DisplayManager::forceBacklightPercent(uint8_t percent) {
  lastUserActivityAt_ = millis();
  setBacklightPercent(percent);
}

void DisplayManager::updateIdleBrightness() {
  // Incoming input calls wakeForUserActivity(); this method only applies the idle policy.
  const uint32_t idleMs = millis() - lastUserActivityAt_;
  setBacklightPercent(offAfterMs_ > 0 && idleMs >= offAfterMs_ ? 0 : activeBrightnessPercent_);
}

void DisplayManager::configurePowerSaving(uint8_t activeBrightnessPercent, uint32_t offAfterMs) {
  activeBrightnessPercent_ = constrain(activeBrightnessPercent, 10, 100);
  offAfterMs_ = offAfterMs;
  setBacklightPercent(backlightPercent_ == 0 ? 0 : activeBrightnessPercent_);
}

void DisplayManager::setLightTheme(bool enabled) {
  // On this ST7789 build the controller inversion flag is opposite to the
  // user-facing theme name: inversion on gives the normal dark palette.
  tft_.invertDisplay(!enabled);
}

void DisplayManager::setBacklightPercent(uint8_t percent) {
  percent = constrain(percent, 0, 100);
  if (percent == backlightPercent_) {
    return;
  }

  backlightPercent_ = percent;
  const uint8_t duty = Logic::backlightDutyForPercent(percent, 255);
  ledcWrite(Config::DisplayBacklightPin, duty);

  AppLog.print("Backlight: ");
  AppLog.print(percent);
  AppLog.print("% duty=");
  AppLog.println(duty);
}

uint8_t DisplayManager::backlightPercent() const {
  return backlightPercent_;
}

bool DisplayManager::isOn() const {
  return backlightPercent_ > 0;
}

uint32_t DisplayManager::idleMs() const {
  return millis() - lastUserActivityAt_;
}

int DisplayManager::screenshotWidth() {
  return screenBufferReady_ ? screen_.width() : tft_.width();
}

int DisplayManager::screenshotHeight() {
  return screenBufferReady_ ? screen_.height() : tft_.height();
}

bool DisplayManager::hasScreenshotBuffer() const {
  return screenBufferReady_;
}

uint16_t DisplayManager::screenshotPixel565(int x, int y) {
  if (screenBufferReady_) {
    return screen_.readPixel(x, y);
  }
  return tft_.readPixel(x, y);
}

void DisplayManager::observeText(TextMarqueeState &marquee, const String &text) {
  if (text == marquee.observedText) {
    return;
  }

  marquee.observedText = text;
  restartMarquee(marquee);
}

bool DisplayManager::advanceMarqueeIfNeeded(
    TextMarqueeState &marquee,
    const String &text,
    uint8_t font,
    int maxWidth) {
  observeText(marquee, text);

  if (marquee.finished) {
    return false;
  }

  // Short text never animates; long text scrolls once then rests.
  const int textWidth = tft_.textWidth(text, font);
  if (textWidth <= maxWidth) {
    marquee.finished = true;
    return false;
  }

  const uint32_t now = millis();
  if (now < marquee.changedAt + Config::TitleScrollStartDelayMs) {
    return false;
  }
  if (now - marquee.lastFrameAt < Config::TitleScrollFrameMs) {
    return false;
  }

  marquee.lastFrameAt = now;
  marquee.offsetPx += 2;

  if (marquee.offsetPx > textWidth + Config::TitleScrollGapPx) {
    marquee.offsetPx = 0;
    marquee.finished = true;
  }

  return true;
}

void DisplayManager::restartMarquee(TextMarqueeState &marquee) {
  marquee.changedAt = millis();
  marquee.offsetPx = 0;
  marquee.lastFrameAt = 0;
  marquee.finished = false;
}

String DisplayManager::titleText(const SpotifyState &playback) const {
  if (!playback.hasPlayback) {
    return I18n::text("no_playback");
  }
  return playback.trackName.isEmpty() ? I18n::text("nothing_playing") : playback.trackName;
}

String DisplayManager::artistText(const SpotifyState &playback) const {
  if (!playback.hasPlayback) {
    return "";
  }
  return playback.artistName.isEmpty() ? playback.currentType : playback.artistName;
}

int DisplayManager::estimatedProgressMs(const SpotifyState &playback) const {
  if (playback.durationMs <= 0) {
    return 0;
  }

  // Spotify is polled every few seconds; local estimation keeps the progress footer/bar moving.
  return Logic::estimatedProgressMs(
      playback.progressMs,
      playback.durationMs,
      playback.isPlaying,
      playback.progressSyncedAt,
      millis());
}

String DisplayManager::playbackTimeText(const SpotifyState &playback) const {
  if (playback.durationMs <= 0) {
    return "";
  }
  return formatTrackTime(estimatedProgressMs(playback)) + "/" + formatTrackTime(playback.durationMs);
}

uint16_t DisplayManager::batteryColor(const BatteryState &battery) const {
  if (!battery.available || battery.percent < 0) {
    return TFT_DARKGREY;
  }
  if (battery.percent <= 15) {
    return TFT_RED;
  }
  if (battery.percent <= 35) {
    return TFT_ORANGE;
  }
  return StatusGreen;
}

template <typename Canvas>
void DisplayManager::renderBoot(Canvas &canvas, const String &message, const BatteryState *battery) {
  const int w = canvasWidth(canvas);
  const bool tall = tallCanvas(canvas);
  canvas.setTextDatum(TL_DATUM);
  if constexpr (Config::HasBq27220BatteryGauge) {
    if (battery != nullptr) {
      drawBatteryIndicator(canvas, *battery, w - 92, 5);
    }
  }
  const int iconSize = tall ? 88 : 78;
  drawDJConnectIcon(canvas, 14, tall ? 18 : 12, iconSize);

  canvas.setTextColor(BrightPurple, TFT_BLACK);
  canvas.drawString("DJConnect", 98, tall ? 46 : 34, 4);

  canvas.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  canvas.drawString(clippedText(canvas, Config::AppTagline, w - 112, 2), 102, tall ? 78 : 58, 2);

  canvas.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  canvas.drawString(Config::AppVersion, 102, tall ? 102 : 78, 2);

  canvas.setTextColor(NeutralLightGrey, TFT_BLACK);
  canvas.drawString(clippedText(canvas, Config::WebsiteUrl, w - 112, 1), 102, tall ? 126 : 98, 1);

  canvas.setTextColor(TFT_WHITE, TFT_BLACK);
  String remaining = message;
  const bool multiLine = remaining.indexOf('\n') >= 0;
  const int startY = tall ? (multiLine ? 146 : 166) : (multiLine ? 114 : 128);
  for (int lineIndex = 0; lineIndex < 3 && remaining.length() > 0; ++lineIndex) {
    const int breakAt = remaining.indexOf('\n');
    const String line = breakAt >= 0 ? remaining.substring(0, breakAt) : remaining;
    canvas.setTextColor(lineIndex == 0 ? TFT_WHITE : TFT_LIGHTGREY, TFT_BLACK);
    canvas.drawString(clippedText(canvas, line, contentWidth(canvas), 2), 14, startY + (lineIndex * 20), 2);
    if (breakAt < 0) {
      break;
    }
    remaining = remaining.substring(breakAt + 1);
  }
}

template <typename Canvas>
void DisplayManager::renderPairingCode(Canvas &canvas, const String &pairCode, const BatteryState *battery) {
  const int w = canvasWidth(canvas);
  const int h = canvasHeight(canvas);
  const bool tall = tallCanvas(canvas);
  canvas.setTextDatum(TL_DATUM);
  drawWifiIndicator(canvas, Config::HasBq27220BatteryGauge ? w - 122 : w - 34, 3);
  if constexpr (Config::HasBq27220BatteryGauge) {
    if (battery != nullptr) {
      drawBatteryIndicator(canvas, *battery, w - 92, 5);
    }
  }

  drawDJConnectIcon(canvas, 14, tall ? 18 : 10, tall ? 48 : 38);
  canvas.setTextColor(BrightPurple, TFT_BLACK);
  canvas.drawString("DJConnect", tall ? 72 : 60, tall ? 28 : 18, 4);

  canvas.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  canvas.drawString(I18n::text("pairing_code"), 14, tall ? 86 : 62, 2);
  canvas.drawString("Home Assistant", 14, tall ? 108 : 82, 2);
  const String haUrl = String(I18n::text("pairing_ha_url")) + ": " + Config::DefaultHomeAssistantUrl;
  canvas.setTextColor(NeutralLightGrey, TFT_BLACK);
  canvas.drawString(clippedText(canvas, haUrl, w - 28, 1), 14, tall ? 128 : 100, 1);

  canvas.drawFastHLine(14, tall ? 148 : 112, w - 28, TFT_DARKGREY);

  canvas.setTextDatum(MC_DATUM);
  canvas.setTextColor(BrightPurple, TFT_BLACK);
  canvas.drawString(pairCode, w / 2, tall ? 184 : 136, 4);
  canvas.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  canvas.drawString(I18n::text("setup_turn_off_hint"), w / 2, h - 10, 1);
  canvas.setTextDatum(TL_DATUM);
}

template <typename Canvas>
void DisplayManager::renderPlayback(
    Canvas &canvas,
    const SpotifyState &playback,
    const BatteryState &battery,
    const StatusNotice &notice,
    int displayedVolume,
    bool homeAssistantConnected,
    PlaybackConnectionState playbackConnectionState) {
  const int w = canvasWidth(canvas);
  const bool tall = tallCanvas(canvas);
  const int bodyWidth = contentWidth(canvas);
  const int titleY = playback.hasPlayback ? (tall ? 70 : 56) : (tall ? 84 : 62);
  const int artistY = tall ? 116 : 91;
  const int progressY = tall ? 160 : 124;
  const int volumeLabelY = tall ? 181 : 137;
  const int volumeBarY = tall ? 186 : 142;
  canvas.setTextDatum(TL_DATUM);

  canvas.setTextColor(BrightYellow, TFT_BLACK);
  canvas.drawString(I18n::text("now_playing"), 8, 5, 2);

  auto drawStatusBadge = [&](int x, const char *label, uint16_t color) {
    canvas.setTextColor(color, TFT_BLACK);
    canvas.drawString(label, x, 5, 2);
  };
  auto drawPlaybackBadge = [&](int x, uint16_t color) {
    canvas.fillCircle(x + 4, 18, 2, color);
    canvas.drawFastVLine(x + 6, 6, 12, color);
    canvas.drawFastHLine(x + 6, 6, 7, color);
    canvas.drawFastVLine(x + 13, 6, 5, color);
  };
  const uint16_t playbackStatusColor = playbackConnectionState == PlaybackConnectionState::Ok
                                           ? StatusGreen
                                           : playbackConnectionState == PlaybackConnectionState::Idle
                                                 ? NeutralLightGrey
                                                 : TFT_RED;
  const int right = w - 8;
  const int batteryX = right - 62;
  const int wifiX = Config::HasBq27220BatteryGauge ? batteryX - 36 : right - 22;
  const int playbackX = wifiX - 36;
  const int haX = playbackX - 22;
  drawStatusBadge(haX, "H", homeAssistantConnected ? StatusGreen : TFT_RED);
  drawPlaybackBadge(playbackX, playbackStatusColor);
  drawWifiIndicator(canvas, wifiX, 1);
  if constexpr (Config::HasBq27220BatteryGauge) {
    drawBatteryIndicator(canvas, battery, batteryX, 5);
  }
  canvas.drawFastHLine(8, 25, bodyWidth, TFT_DARKGREY);

  // Body: only show output and volume controls when Spotify reports an active playback context.
  if (playback.hasPlayback) {
    const String device = playback.deviceName.isEmpty() ? I18n::text("no_active_device") : playback.deviceName;
    canvas.setTextColor(TFT_CYAN, TFT_BLACK);
    canvas.drawString(clippedText(canvas, device, bodyWidth, 2), 8, tall ? 38 : 32, 2);
  }

  const String title = titleText(playback);
  canvas.setTextColor(TFT_WHITE, TFT_BLACK);
  drawMarqueeText(canvas, titleMarquee_, title, 8, titleY, bodyWidth, 4, 34);

  const String artist = artistText(playback);
  if (!artist.isEmpty()) {
    canvas.setTextColor(BrightPurple, TFT_BLACK);
    drawMarqueeText(canvas, artistMarquee_, artist, 8, artistY, bodyWidth, 4, 30);
  }

  if (playback.hasPlayback && playback.durationMs > 0) {
    const int percent = (estimatedProgressMs(playback) * 100) / playback.durationMs;
    drawProgressBar(canvas, 8, progressY, bodyWidth, 8, percent, StatusGreen);
  }

  if (playback.hasPlayback && playback.supportsVolume && displayedVolume >= 0) {
    const int volumeFillPercent = (constrain(displayedVolume, 0, Config::MaxSpotifyVolumePercent) * 100) /
                                  Config::MaxSpotifyVolumePercent;
    canvas.setTextColor(VolumeOrange, TFT_BLACK);
    canvas.drawString(String(I18n::text("volume")) + " " + String(displayedVolume) + "%", 8, volumeLabelY, 2);
    drawProgressBar(canvas, 70, volumeBarY, max(20, bodyWidth - 62), 5, volumeFillPercent, VolumeOrange);
  }

  // Footer prefers actionable errors/notices, otherwise it shows play state and track time.
  String footer;
  if (!playback.error.isEmpty()) {
    footer = localizedStatusText(playback.error);
    canvas.setTextColor(TFT_RED, TFT_BLACK);
  } else if (notice.isVisible()) {
    footer = localizedStatusText(notice.message);
    canvas.setTextColor(BrightPurple, TFT_BLACK);
  } else if (!playback.hasPlayback) {
    footer = I18n::text("center_liked_proxy");
    canvas.setTextColor(VolumeOrange, TFT_BLACK);
  } else {
    footer = playback.isPlaying ? I18n::text("playing") : I18n::text("paused");
    const String timeText = playbackTimeText(playback);
    if (!timeText.isEmpty()) {
      footer += " ";
      footer += timeText;
    }
    canvas.setTextColor(StatusGreen, TFT_BLACK);
  }
  canvas.drawString(clippedText(canvas, footer, bodyWidth, 2), 8, footerY(canvas), 2);
}

template <typename Canvas>
void DisplayManager::renderMenu(
    Canvas &canvas,
    const String &title,
    const MenuItemView *items,
    size_t itemCount,
    size_t selectedIndex,
    const StatusNotice &notice) {
  canvas.setTextDatum(TL_DATUM);
  drawMenuTitle(canvas, title);

  const int rowTop = 34;
  const int rowHeight = tallCanvas(canvas) ? 31 : 27;
  const int availableHeight = max(0, footerY(canvas) - rowTop - 7);
  const size_t maxVisible = max(1, availableHeight / rowHeight);
  const int maxLabelWidth = itemCount > maxVisible ? canvasWidth(canvas) - 44 : canvasWidth(canvas) - 32;
  const size_t visibleCount = min(itemCount, static_cast<size_t>(maxVisible));
  size_t firstVisible = 0;
  if (itemCount > visibleCount && selectedIndex >= visibleCount) {
    firstVisible = selectedIndex - visibleCount + 1;
  }

  for (size_t visibleIndex = 0; visibleIndex < visibleCount; visibleIndex++) {
    const size_t index = firstVisible + visibleIndex;
    const int y = rowTop + (visibleIndex * rowHeight);
    const bool selected = index == selectedIndex;

    if (selected) {
      canvas.fillRoundRect(8, y - 2, canvasWidth(canvas) - 24, rowHeight - 2, 4, TFT_DARKGREEN);
      canvas.drawRoundRect(8, y - 2, canvasWidth(canvas) - 24, rowHeight - 2, 4, TFT_GREEN);
      canvas.setTextColor(TFT_WHITE, TFT_DARKGREEN);
    } else {
      canvas.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    }

    canvas.drawString(clippedText(canvas, items[index].label, maxLabelWidth, 2), 16, y + 3, 2);
  }

  if (itemCount > visibleCount) {
    const int trackX = canvasWidth(canvas) - 11;
    const int trackTop = rowTop;
    const int trackHeight = static_cast<int>(visibleCount * rowHeight) - 4;
    const int thumbHeight = max(18, (trackHeight * static_cast<int>(visibleCount)) / static_cast<int>(itemCount));
    const int maxFirst = static_cast<int>(itemCount - visibleCount);
    const int thumbTravel = max(1, trackHeight - thumbHeight);
    const int thumbY = trackTop + (maxFirst > 0 ? (thumbTravel * static_cast<int>(firstVisible)) / maxFirst : 0);
    canvas.drawRoundRect(trackX, trackTop, 4, trackHeight, 2, TFT_DARKGREY);
    canvas.fillRoundRect(trackX, thumbY, 4, thumbHeight, 2, TFT_LIGHTGREY);
  }

  drawMenuFooter(canvas, notice);
}

template <typename Canvas>
void DisplayManager::renderAbout(Canvas &canvas, const StatusNotice &notice, const AboutStatus &status, size_t selectedIndex) {
  canvas.setTextDatum(TL_DATUM);
  drawMenuTitle(canvas, I18n::text("about"));
  const bool tall = tallCanvas(canvas);
  drawDJConnectIcon(canvas, 14, tall ? 38 : 34, tall ? 52 : 44);

  canvas.setTextColor(TFT_WHITE, TFT_BLACK);
  canvas.drawString("DJConnect", tall ? 76 : 66, tall ? 44 : 38, 4);

  canvas.setTextColor(NeutralLightGrey, TFT_BLACK);
  canvas.drawString(Config::AppVersion, tall ? 80 : 70, tall ? 78 : 70, 2);

  struct Row {
    const char *label;
    String value;
    uint16_t color;
    uint8_t valueFont;
  };
  Row rows[] = {
      {I18n::text("web"), status.webAddress.isEmpty() ? "-" : status.webAddress, TFT_WHITE, 2},
      {I18n::text("wifi"), I18n::connected(status.wifiConnected), static_cast<uint16_t>(status.wifiConnected ? StatusGreen : TFT_RED), 2},
      {I18n::text("music"), I18n::connected(status.spotifyConnected), static_cast<uint16_t>(status.spotifyConnected ? StatusGreen : TFT_RED), 2},
      {"Home Assistant", status.haPaired ? I18n::text("connected") : I18n::text("not_paired"), static_cast<uint16_t>(status.haPaired ? StatusGreen : TFT_RED), 2},
      {"Website", Config::WebsiteUrl, NeutralLightGrey, 1},
      {"Copyright", "2026 Peter van Tol", NeutralLightGrey, 2},
      {"Spotify", "Trademark Spotify AB", NeutralLightGrey, 2},
      {"Notice", "Not affiliated", NeutralLightGrey, 2},
      {"OSS", "See notices", NeutralLightGrey, 2},
  };

  const size_t itemCount = sizeof(rows) / sizeof(rows[0]);
  const int rowTop = tall ? 106 : 92;
  const int rowHeight = tall ? 19 : 17;
  const int availableHeight = max(0, footerY(canvas) - rowTop - 5);
  const size_t visibleCount = min(itemCount, static_cast<size_t>(max(1, availableHeight / rowHeight)));
  selectedIndex = min(selectedIndex, itemCount - 1);
  size_t firstVisible = 0;
  if (selectedIndex >= visibleCount) {
    firstVisible = selectedIndex - visibleCount + 1;
  }

  for (size_t visibleIndex = 0; visibleIndex < visibleCount; visibleIndex++) {
    const size_t index = firstVisible + visibleIndex;
    const int y = rowTop + (visibleIndex * rowHeight);
    const bool selected = index == selectedIndex;
    if (selected) {
      canvas.fillRoundRect(8, y - 1, canvasWidth(canvas) - 28, rowHeight - 1, 3, TFT_DARKGREEN);
    }
    canvas.setTextColor(TFT_WHITE, selected ? TFT_DARKGREEN : TFT_BLACK);
    canvas.drawString(clippedText(canvas, rows[index].label, 112, 2), 14, y, 2);
    canvas.setTextColor(rows[index].color, selected ? TFT_DARKGREEN : TFT_BLACK);
    const int valueY = y + (rows[index].valueFont == 1 ? 4 : 0);
    canvas.drawString(clippedText(canvas, rows[index].value, canvasWidth(canvas) - 150, rows[index].valueFont), 136, valueY, rows[index].valueFont);
  }

  const int trackX = canvasWidth(canvas) - 11;
  const int trackTop = rowTop;
  const int trackHeight = static_cast<int>(visibleCount * rowHeight) - 2;
  const int thumbHeight = max(18, (trackHeight * static_cast<int>(visibleCount)) / static_cast<int>(itemCount));
  const int maxFirst = static_cast<int>(itemCount - visibleCount);
  const int thumbTravel = max(1, trackHeight - thumbHeight);
  const int thumbY = trackTop + (maxFirst > 0 ? (thumbTravel * static_cast<int>(firstVisible)) / maxFirst : 0);
  canvas.drawRoundRect(trackX, trackTop, 4, trackHeight, 2, TFT_DARKGREY);
  canvas.fillRoundRect(trackX, thumbY, 4, thumbHeight, 2, TFT_LIGHTGREY);

  drawMenuFooter(canvas, notice);
}

template <typename Canvas>
void DisplayManager::renderLogs(Canvas &canvas, const String *lines, size_t lineCount, const StatusNotice &notice) {
  canvas.setTextDatum(TL_DATUM);
  drawMenuTitle(canvas, I18n::text("logs"));

  canvas.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  const int rowTop = 34;
  const int rowHeight = 12;
  const int availableHeight = max(0, footerY(canvas) - rowTop - 5);
  const size_t visibleLines = min(lineCount, static_cast<size_t>(max(1, availableHeight / rowHeight)));
  for (size_t index = 0; index < visibleLines; index++) {
    const int y = rowTop + static_cast<int>(index * rowHeight);
    canvas.drawString(clippedText(canvas, lines[index], contentWidth(canvas) + 2, 1), 8, y, 1);
  }

  if (visibleLines == 0) {
    canvas.drawString("No logs yet", 8, 58, 2);
  }

  drawMenuFooter(canvas, notice);
}

template <typename Canvas>
void DisplayManager::renderPong(Canvas &canvas, int paddleY, int ballX, int ballY, int score, int highScore, bool missFlash, const StatusNotice &notice) {
  canvas.setTextDatum(TL_DATUM);
  canvas.fillScreen(TFT_BLACK);
  canvas.setTextColor(TFT_ORANGE, TFT_BLACK);
  canvas.drawString(I18n::text("pong"), 8, 5, 2);
  const String scoreText = String("Score ") + String(score) + "  High " + String(highScore);
  canvas.setTextColor(TFT_WHITE, TFT_BLACK);
  canvas.drawString(scoreText, 312 - canvas.textWidth(scoreText, 2), 8, 2);
  canvas.drawFastHLine(8, 36, 304, TFT_DARKGREY);
  canvas.drawFastVLine(160, 42, 118, TFT_DARKGREY);
  canvas.fillRoundRect(18, paddleY, 8, 34, 3, TFT_ORANGE);
  canvas.fillCircle(ballX, ballY, 4, TFT_GREEN);
  if (missFlash) {
    canvas.drawRect(0, 0, 320, 170, TFT_RED);
    canvas.drawRect(1, 1, 318, 168, TFT_RED);
  }
  drawMenuFooter(canvas, notice);
}

template <typename Canvas>
void DisplayManager::renderAsteroids(Canvas &canvas, int shipX, int shipY, int asteroidX, int asteroidY, int bulletY, bool bulletActive, int score, int highScore, bool hitFlash, const StatusNotice &notice) {
  canvas.setTextDatum(TL_DATUM);
  canvas.fillScreen(TFT_BLACK);
  canvas.setTextColor(BrightPurple, TFT_BLACK);
  canvas.drawString(I18n::text("asteroids"), 8, 5, 2);
  const String scoreText = String("Score ") + String(score) + "  High " + String(highScore);
  canvas.setTextColor(TFT_WHITE, TFT_BLACK);
  canvas.drawString(scoreText, 312 - canvas.textWidth(scoreText, 2), 8, 2);
  canvas.drawFastHLine(8, 36, 304, TFT_DARKGREY);
  canvas.drawLine(shipX, shipY - 10, shipX - 8, shipY + 8, BrightPurple);
  canvas.drawLine(shipX, shipY - 10, shipX + 8, shipY + 8, BrightPurple);
  canvas.drawLine(shipX - 8, shipY + 8, shipX + 8, shipY + 8, BrightPurple);
  canvas.drawCircle(asteroidX, asteroidY, 9, GamePink);
  canvas.drawLine(asteroidX - 6, asteroidY - 3, asteroidX - 2, asteroidY - 9, GamePink);
  canvas.drawLine(asteroidX + 2, asteroidY + 8, asteroidX + 7, asteroidY + 2, GamePink);
  if (bulletActive) {
    canvas.fillRoundRect(shipX - 2, bulletY, 4, 10, 2, GameLightBlue);
  }
  if (hitFlash) {
    canvas.drawRect(0, 0, 320, 170, TFT_RED);
    canvas.drawRect(1, 1, 318, 168, TFT_RED);
  }
  drawMenuFooter(canvas, notice);
}

template <typename Canvas>
void DisplayManager::renderFlyer(Canvas &canvas, int planeY, int obstacleX, int obstacleY, int shotX, bool shotActive, int score, int highScore, bool hitFlash, const StatusNotice &notice) {
  canvas.setTextDatum(TL_DATUM);
  canvas.fillScreen(TFT_BLACK);
  canvas.setTextColor(GameLightBlue, TFT_BLACK);
  canvas.drawString(I18n::text("flyer"), 8, 5, 2);
  const String scoreText = String("Score ") + String(score) + "  High " + String(highScore);
  canvas.setTextColor(TFT_WHITE, TFT_BLACK);
  canvas.drawString(scoreText, 312 - canvas.textWidth(scoreText, 2), 8, 2);
  canvas.drawFastHLine(8, 36, 304, TFT_DARKGREY);
  const int planeX = 42;
  canvas.fillTriangle(planeX + 18, planeY, planeX - 12, planeY - 10, planeX - 12, planeY + 10, GameLightBlue);
  canvas.drawLine(planeX - 4, planeY, planeX - 22, planeY - 18, GameLightBlue);
  canvas.drawLine(planeX - 4, planeY, planeX - 22, planeY + 18, GameLightBlue);
  canvas.fillRect(obstacleX - 8, obstacleY - 16, 16, 32, GameBrown);
  canvas.drawCircle(obstacleX, obstacleY - 22, 5, GameDarkBrown);
  canvas.drawCircle(obstacleX, obstacleY + 22, 5, GameDarkBrown);
  if (shotActive) {
    canvas.fillRoundRect(shotX, planeY - 2, 14, 4, 2, GameLightBlue);
  }
  if (hitFlash) {
    canvas.drawRect(0, 0, 320, 170, TFT_RED);
    canvas.drawRect(1, 1, 318, 168, TFT_RED);
  }
  drawMenuFooter(canvas, notice);
}

template <typename Canvas>
void DisplayManager::renderMazeChase(Canvas &canvas, int playerX, int playerLane, int ghostX, int ghostLane, int pelletX, int pelletLane, int score, int highScore, bool ghostVulnerable, bool hitFlash, const StatusNotice &notice) {
  canvas.setTextDatum(TL_DATUM);
  canvas.fillScreen(TFT_BLACK);
  canvas.setTextColor(TFT_YELLOW, TFT_BLACK);
  canvas.drawString(I18n::text("maze_chase"), 8, 5, 2);
  const String scoreText = String("Score ") + String(score) + "  High " + String(highScore);
  canvas.setTextColor(TFT_WHITE, TFT_BLACK);
  canvas.drawString(scoreText, 312 - canvas.textWidth(scoreText, 2), 8, 2);
  canvas.drawFastHLine(8, 36, 304, TFT_DARKGREY);
  const int lanes[3] = {58, 96, 134};
  for (int lane = 0; lane < 3; lane++) {
    canvas.drawFastHLine(18, lanes[lane], 284, GameBlue);
    for (int x = 42; x <= 278; x += 34) {
      canvas.drawPixel(x, lanes[lane], TFT_DARKGREY);
    }
  }
  canvas.fillCircle(pelletX, lanes[constrain(pelletLane, 0, 2)], 4, TFT_WHITE);
  const int y = lanes[constrain(playerLane, 0, 2)];
  canvas.fillCircle(playerX, y, 10, TFT_YELLOW);
  canvas.fillTriangle(playerX + 4, y, playerX + 13, y - 7, playerX + 13, y + 7, TFT_BLACK);
  const int gy = lanes[constrain(ghostLane, 0, 2)];
  const uint16_t ghostColor = ghostVulnerable ? (((millis() / 180) % 2) == 0 ? GameBlue : TFT_WHITE) : BrightPurple;
  canvas.fillRoundRect(ghostX - 10, gy - 10, 20, 20, 6, ghostColor);
  canvas.fillCircle(ghostX - 4, gy - 3, 2, TFT_WHITE);
  canvas.fillCircle(ghostX + 4, gy - 3, 2, TFT_WHITE);
  if (hitFlash) {
    canvas.drawRect(0, 0, 320, 170, TFT_RED);
    canvas.drawRect(1, 1, 318, 168, TFT_RED);
  }
  drawMenuFooter(canvas, notice);
}

template <typename Canvas>
String DisplayManager::clippedText(Canvas &canvas, String text, int maxWidth, uint8_t font) {
  text.trim();
  if (text.isEmpty()) {
    return "-";
  }
  if (canvas.textWidth(text, font) <= maxWidth) {
    return text;
  }

  // Clip non-marquee fields so they never collide with adjacent UI.
  const String ellipsis = "...";
  while (text.length() > 1 && canvas.textWidth(text + ellipsis, font) > maxWidth) {
    text.remove(text.length() - 1);
  }
  return text + ellipsis;
}

template <typename Canvas>
void DisplayManager::drawProgressBar(
    Canvas &canvas,
    int x,
    int y,
    int w,
    int h,
    int percent,
    uint16_t fillColor) {
  percent = constrain(percent, 0, 100);
  canvas.drawRoundRect(x, y, w, h, h / 2, TFT_DARKGREY);
  canvas.fillRoundRect(x + 1, y + 1, w - 2, h - 2, (h - 2) / 2, TFT_BLACK);
  const int fillW = ((w - 2) * percent) / 100;
  if (fillW > 0) {
    canvas.fillRoundRect(x + 1, y + 1, fillW, h - 2, (h - 2) / 2, fillColor);
  }
}

template <typename Canvas>
void DisplayManager::drawBatteryIndicator(Canvas &canvas, const BatteryState &battery, int x, int y) {
  const uint16_t color = batteryColor(battery);
  canvas.drawRect(x, y + 3, 19, 10, color);
  canvas.fillRect(x + 19, y + 6, 2, 4, color);

  if (battery.available && battery.percent >= 0) {
    const int fillW = map(constrain(battery.percent, 0, 100), 0, 100, 0, 15);
    if (fillW > 0) {
      canvas.fillRect(x + 2, y + 5, fillW, 6, color);
    }
    if (battery.charging) {
      // Yellow bolt overlays the battery icon when the gauge reports charge mode.
      canvas.drawLine(x + 10, y + 2, x + 6, y + 8, TFT_YELLOW);
      canvas.drawLine(x + 6, y + 8, x + 11, y + 8, TFT_YELLOW);
      canvas.drawLine(x + 11, y + 8, x + 8, y + 15, TFT_YELLOW);
    }
    canvas.setTextColor(color, TFT_BLACK);
    const String label = String(battery.percent) + "%";
    canvas.drawString(label, x + 26, y, 2);
  } else {
    canvas.setTextColor(TFT_DARKGREY, TFT_BLACK);
    canvas.drawString("--%", x + 26, y, 2);
  }
}

template <typename Canvas>
void DisplayManager::drawWifiIndicator(Canvas &canvas, int x, int y) {
  const bool connected = WiFi.status() == WL_CONNECTED;
  const int rssi = connected ? WiFi.RSSI() : 0;
  int level = 0;
  if (connected && rssi != 0) {
    if (rssi > -55) {
      level = 4;
    } else if (rssi > -67) {
      level = 3;
    } else if (rssi > -75) {
      level = 2;
    } else {
      level = 1;
    }
  }

  const uint16_t activeColor = level >= 3 ? StatusGreen : level >= 2 ? TFT_YELLOW : TFT_RED;
  static constexpr int heights[] = {5, 8, 11, 14};
  for (int index = 0; index < 4; index++) {
    const int barX = x + index * 5;
    const int barH = heights[index];
    const int barY = y + 16 - barH;
    if (index < level) {
      canvas.fillRoundRect(barX, barY, 3, barH, 1, activeColor);
    } else {
      canvas.drawRoundRect(barX, barY, 3, barH, 1, TFT_DARKGREY);
    }
  }

  if (!connected) {
    canvas.drawLine(x - 1, y + 2, x + 20, y + 17, TFT_RED);
  }
}

template <typename Canvas>
void DisplayManager::drawMenuTitle(Canvas &canvas, const String &title) {
  const int w = canvasWidth(canvas);
  canvas.setTextColor(BrightPurple, TFT_BLACK);
  canvas.drawString(clippedText(canvas, title, w - 68, 2), 8, 5, 2);
  canvas.setTextColor(TFT_WHITE, TFT_BLACK);
  const String menuText = I18n::text("menu");
  canvas.drawString(menuText, w - 8 - canvas.textWidth(menuText, 2), 5, 2);
  canvas.drawFastHLine(8, 25, contentWidth(canvas), TFT_DARKGREY);
}

template <typename Canvas>
void DisplayManager::drawMenuFooter(Canvas &canvas, const StatusNotice &notice) {
  const int y = footerY(canvas);
  const int w = canvasWidth(canvas);
  if (notice.isVisible()) {
    canvas.setTextColor(BrightPurple, TFT_BLACK);
    canvas.drawString(clippedText(canvas, notice.message, contentWidth(canvas), 2), 8, y, 2);
    return;
  }

  canvas.setTextColor(TFT_WHITE, TFT_BLACK);
  const String backHint = I18n::text("back_top_button");
  canvas.drawString(backHint, w - 8 - canvas.textWidth(backHint, 2), y, 2);
}

template <typename Canvas>
void DisplayManager::drawSpotifyLogo(Canvas &canvas, int x, int y) {
  canvas.fillCircle(x + 26, y + 26, 25, TFT_GREEN);

  auto drawStripe = [&](float ax, float ay, float bx, float by, float cx, float cy, float width) {
    canvas.drawWideLine(x + ax, y + ay, x + bx, y + by, width, TFT_BLACK, TFT_GREEN);
    canvas.drawWideLine(x + bx, y + by, x + cx, y + cy, width, TFT_BLACK, TFT_GREEN);
  };

  drawStripe(12.0f, 20.0f, 27.0f, 17.0f, 42.0f, 22.0f, 4.5f);
  drawStripe(14.0f, 29.0f, 27.0f, 27.0f, 39.0f, 31.0f, 3.8f);
  drawStripe(17.0f, 37.0f, 26.0f, 36.0f, 35.0f, 39.0f, 3.2f);
}

template <typename Canvas>
void DisplayManager::drawDJConnectIcon(Canvas &canvas, int x, int y, int size) {
  const int sourceSize = DJCONNECT_ICON_160_WIDTH;
  for (int dy = 0; dy < size; dy++) {
    const int sy = (dy * sourceSize) / size;
    for (int dx = 0; dx < size; dx++) {
      const int sx = (dx * sourceSize) / size;
      const uint32_t byteIndex = (static_cast<uint32_t>(sy) * sourceSize + sx) * 2;
      const uint16_t color = (static_cast<uint16_t>(pgm_read_byte(&DJCONNECT_ICON_160_RGB565[byteIndex])) << 8) |
                             pgm_read_byte(&DJCONNECT_ICON_160_RGB565[byteIndex + 1]);
      if (color != TFT_BLACK) {
        canvas.drawPixel(x + dx, y + dy, color);
      }
    }
  }
}

template <typename Canvas>
void DisplayManager::drawMarqueeText(
    Canvas &canvas,
    TextMarqueeState &marquee,
    const String &text,
    int x,
    int y,
    int maxWidth,
    uint8_t font,
    int textHeight) {
  const int textWidth = canvas.textWidth(text, font);
  if (textWidth <= maxWidth) {
    canvas.drawString(text, x, y, font);
    return;
  }

  // Viewport clipping confines the scrolling text to its row without redrawing neighboring UI.
  canvas.setViewport(x, y, maxWidth, textHeight, false);
  canvas.fillRect(x, y, maxWidth, textHeight, TFT_BLACK);

  int offset = 0;
  const uint32_t now = millis();
  if (now > marquee.changedAt + Config::TitleScrollStartDelayMs) {
    offset = marquee.offsetPx;
  }

  canvas.drawString(text, x - offset, y, font);
  canvas.drawString(text, x - offset + textWidth + Config::TitleScrollGapPx, y, font);
  canvas.resetViewport();
}
