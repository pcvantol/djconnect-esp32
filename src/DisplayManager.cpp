// Rendering code for the 320x170 ST7789 screen.
// This file intentionally contains only presentation logic: it reads state snapshots and draws them.
#include "DisplayManager.h"

#include "AppLog.h"

#include <LittleFS.h>
#include <TJpg_Decoder.h>
#include <WiFi.h>

#include "Config.h"
#include "I18n.h"
#include "LogicHelpers.h"
#include "TextHelpers.h"
#include "assets/DJConnectIcon160.h"

static constexpr uint16_t BrightYellow = 0xFFE0;
static constexpr uint16_t BrightPurple = 0xB81F;
static constexpr uint16_t NeutralLightGrey = 0xC618;
static constexpr uint16_t SpotifyGreen = 0x1DCB;
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
  pinMode(Config::BoardPowerEnablePin, OUTPUT);
  digitalWrite(Config::BoardPowerEnablePin, HIGH);

  pinMode(Config::SdCardChipSelectPin, OUTPUT);
  digitalWrite(Config::SdCardChipSelectPin, HIGH);
  pinMode(Config::LoraChipSelectPin, OUTPUT);
  digitalWrite(Config::LoraChipSelectPin, HIGH);

  // PWM backlight allows 100%/10%/0% idle brightness instead of only on/off.
  ledcSetup(
      Config::DisplayBacklightPwmChannel,
      Config::DisplayBacklightPwmFrequency,
      Config::DisplayBacklightPwmResolution);
  ledcAttachPin(Config::DisplayBacklightPin, Config::DisplayBacklightPwmChannel);
  setBacklightPercent(0);

  tft_.init();
  tft_.setRotation(3);
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
    screen_.drawString(message, 14, 146, 2);
    screen_.pushSprite(0, 0);
    wakeForUserActivity();
    return;
  }

  tft_.fillScreen(TFT_BLACK);
  renderBoot(tft_, "Battery " + String(battery.percent) + "%", &battery);
  tft_.setTextColor(TFT_RED, TFT_BLACK);
  tft_.drawString(message, 14, 146, 2);
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

void DisplayManager::renderPongScreen(int paddleY, int ballX, int ballY, int score, bool missFlash, const StatusNotice &notice) {
  if (screenBufferReady_) {
    screen_.fillSprite(TFT_BLACK);
    renderPong(screen_, paddleY, ballX, ballY, score, missFlash, notice);
    screen_.pushSprite(0, 0);
    return;
  }
  tft_.fillScreen(TFT_BLACK);
  renderPong(tft_, paddleY, ballX, ballY, score, missFlash, notice);
}

void DisplayManager::renderAsteroidsScreen(int shipX, int shipY, int asteroidX, int asteroidY, int bulletY, bool bulletActive, int score, bool hitFlash, const StatusNotice &notice) {
  if (screenBufferReady_) {
    screen_.fillSprite(TFT_BLACK);
    renderAsteroids(screen_, shipX, shipY, asteroidX, asteroidY, bulletY, bulletActive, score, hitFlash, notice);
    screen_.pushSprite(0, 0);
    return;
  }
  tft_.fillScreen(TFT_BLACK);
  renderAsteroids(tft_, shipX, shipY, asteroidX, asteroidY, bulletY, bulletActive, score, hitFlash, notice);
}

void DisplayManager::renderFlyerScreen(int planeY, int obstacleX, int obstacleY, int shotX, bool shotActive, int score, bool hitFlash, const StatusNotice &notice) {
  if (screenBufferReady_) {
    screen_.fillSprite(TFT_BLACK);
    renderFlyer(screen_, planeY, obstacleX, obstacleY, shotX, shotActive, score, hitFlash, notice);
    screen_.pushSprite(0, 0);
    return;
  }
  tft_.fillScreen(TFT_BLACK);
  renderFlyer(tft_, planeY, obstacleX, obstacleY, shotX, shotActive, score, hitFlash, notice);
}

void DisplayManager::renderAlbumArtScreen(
    const SpotifyState &playback,
    const StatusNotice &notice,
    const String &imagePath,
    const String &albumArtStatus) {
  (void)albumArtStatus;
  observeText(titleMarquee_, titleText(playback));
  observeText(artistMarquee_, artistText(playback));

  tft_.setTextDatum(TL_DATUM);

  if (albumArtPaneDirty_ || imagePath != lastAlbumArtPath_) {
    lastAlbumArtPath_ = imagePath;
    albumArtPaneDirty_ = false;
    tft_.fillRect(0, 0, 168, 170, TFT_BLACK);
    tft_.drawRect(4, 4, 160, 160, TFT_DARKGREY);
    if (!imagePath.isEmpty() && LittleFS.exists(imagePath)) {
      uint16_t jpegWidth = 0;
      uint16_t jpegHeight = 0;
      TJpgDec.getFsJpgSize(&jpegWidth, &jpegHeight, imagePath, LittleFS);
      const uint8_t scale = jpegScaleFor(jpegWidth, jpegHeight);
      const int drawWidth = jpegWidth / scale;
      const int drawHeight = jpegHeight / scale;
      const int drawX = 5 + max(0, (158 - drawWidth) / 2);
      const int drawY = 5 + max(0, (158 - drawHeight) / 2);

      JpegTarget = &tft_;
      JpegClipRight = 164;
      JpegClipBottom = 164;
      TJpgDec.setJpgScale(scale);
      TJpgDec.drawFsJpg(drawX, drawY, imagePath, LittleFS);
      JpegTarget = nullptr;
      JpegClipRight = 0;
      JpegClipBottom = 0;
    } else {
      tft_.setTextColor(TFT_DARKGREY, TFT_BLACK);
      tft_.drawString(I18n::text("album_art_no_art"), 52, 76, 2);
    }
  }

  tft_.fillRect(168, 0, 152, 170, TFT_BLACK);
  tft_.setTextColor(BrightYellow, TFT_BLACK);
  tft_.drawString(I18n::text("current_song"), AlbumTextX, 8, 2);

  tft_.setTextColor(TFT_WHITE, TFT_BLACK);
  drawMarqueeText(tft_, titleMarquee_, titleText(playback), AlbumTextX, AlbumTitleY, AlbumTextWidth, 4, AlbumTitleHeight);

  tft_.setTextColor(NeutralLightGrey, TFT_BLACK);
  drawMarqueeText(tft_, artistMarquee_, artistText(playback), AlbumTextX, AlbumArtistY, AlbumTextWidth, 4, AlbumArtistHeight);

  if (notice.isVisible()) {
    tft_.setTextColor(BrightPurple, TFT_BLACK);
    tft_.drawString(clippedText(tft_, notice.message, AlbumTextWidth, 2), AlbumTextX, 140, 2);
  }

  tft_.setTextColor(TFT_WHITE, TFT_BLACK);
  tft_.drawString(I18n::text("back_top_button"), AlbumTextX, 154, 1);
}

void DisplayManager::renderAlbumArtMarqueeText(const SpotifyState &playback, bool titleChanged, bool artistChanged) {
  tft_.setTextDatum(TL_DATUM);
  if (titleChanged) {
    tft_.fillRect(168, AlbumTitleY, 152, AlbumTitleHeight + 4, TFT_BLACK);
    tft_.setTextColor(TFT_WHITE, TFT_BLACK);
    drawMarqueeText(tft_, titleMarquee_, titleText(playback), AlbumTextX, AlbumTitleY, AlbumTextWidth, 4, AlbumTitleHeight);
  }
  if (artistChanged) {
    tft_.fillRect(168, AlbumArtistY, 152, AlbumArtistHeight + 4, TFT_BLACK);
    tft_.setTextColor(NeutralLightGrey, TFT_BLACK);
    drawMarqueeText(tft_, artistMarquee_, artistText(playback), AlbumTextX, AlbumArtistY, AlbumTextWidth, 4, AlbumArtistHeight);
  }
}

void DisplayManager::renderDjResponseOverlay(const String &text) {
  if (text == lastDjResponseOverlayText_) {
    return;
  }
  lastDjResponseOverlayText_ = text;
  tft_.fillScreen(TFT_BLACK);
  tft_.setTextDatum(TL_DATUM);

  const int micX = 28;
  const int micY = 20;
  tft_.drawRoundRect(micX + 14, micY, 34, 54, 16, SpotifyGreen);
  tft_.drawRoundRect(micX + 18, micY + 5, 26, 44, 13, SpotifyGreen);
  tft_.drawLine(micX + 31, micY + 54, micX + 31, micY + 76, SpotifyGreen);
  tft_.drawLine(micX + 18, micY + 76, micX + 44, micY + 76, SpotifyGreen);
  tft_.drawArc(micX + 31, micY + 42, 34, 30, 25, 155, SpotifyGreen, TFT_BLACK);

  tft_.setTextColor(SpotifyGreen, TFT_BLACK);
  tft_.drawString("DJ", 92, 22, 4);

  String remaining = text;
  remaining.trim();
  const int x = 18;
  int y = 86;
  const int maxWidth = 286;
  const int lineHeight = 30;
  const uint8_t font = remaining.length() > 58 ? 2 : 4;
  const int actualLineHeight = font == 4 ? lineHeight : 22;

  while (!remaining.isEmpty() && y < 166) {
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
    tft_.drawString(line, x, y, font);
    y += actualLineHeight;
  }
}

void DisplayManager::resetDjResponseOverlayCache() {
  lastDjResponseOverlayText_ = "";
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
  ledcWrite(Config::DisplayBacklightPwmChannel, duty);

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
  return TFT_GREEN;
}

template <typename Canvas>
void DisplayManager::renderBoot(Canvas &canvas, const String &message, const BatteryState *battery) {
  canvas.setTextDatum(TL_DATUM);
  if (battery != nullptr) {
    drawBatteryIndicator(canvas, *battery, 228, 5);
  }
  drawDJConnectIcon(canvas, 14, 12, 78);

  canvas.setTextColor(BrightPurple, TFT_BLACK);
  canvas.drawString("DJConnect", 98, 34, 4);

  canvas.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  canvas.drawString(Config::AppVersion, 102, 70, 2);

  canvas.setTextColor(TFT_WHITE, TFT_BLACK);
  String remaining = message;
  const bool multiLine = remaining.indexOf('\n') >= 0;
  const int startY = multiLine ? 104 : 124;
  for (int lineIndex = 0; lineIndex < 3 && remaining.length() > 0; ++lineIndex) {
    const int breakAt = remaining.indexOf('\n');
    const String line = breakAt >= 0 ? remaining.substring(0, breakAt) : remaining;
    canvas.setTextColor(lineIndex == 0 ? TFT_WHITE : TFT_LIGHTGREY, TFT_BLACK);
    canvas.drawString(clippedText(canvas, line, 292, 2), 14, startY + (lineIndex * 20), 2);
    if (breakAt < 0) {
      break;
    }
    remaining = remaining.substring(breakAt + 1);
  }
}

template <typename Canvas>
void DisplayManager::renderPairingCode(Canvas &canvas, const String &pairCode, const BatteryState *battery) {
  canvas.setTextDatum(TL_DATUM);
  if (battery != nullptr) {
    drawBatteryIndicator(canvas, *battery, 228, 5);
  }

  drawDJConnectIcon(canvas, 14, 10, 38);
  canvas.setTextColor(BrightPurple, TFT_BLACK);
  canvas.drawString("DJConnect", 60, 18, 4);

  canvas.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  canvas.drawString(I18n::text("pairing_code"), 14, 62, 2);
  canvas.drawString("Home Assistant", 14, 82, 2);

  canvas.drawFastHLine(14, 106, 292, TFT_DARKGREY);

  canvas.setTextDatum(MC_DATUM);
  canvas.setTextColor(BrightPurple, TFT_BLACK);
  canvas.drawString(pairCode, 160, 132, 4);
  canvas.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  canvas.drawString(I18n::text("setup_turn_off_hint"), 160, 160, 1);
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
  canvas.setTextDatum(TL_DATUM);

  canvas.setTextColor(BrightYellow, TFT_BLACK);
  canvas.drawString(I18n::text("now_playing"), 8, 5, 2);

  auto drawStatusBadge = [&](int x, const char *label, uint16_t color) {
    canvas.setTextColor(color, TFT_BLACK);
    canvas.drawString(label, x, 5, 2);
  };
  const uint16_t playbackStatusColor = playbackConnectionState == PlaybackConnectionState::Ok
                                           ? SpotifyGreen
                                           : playbackConnectionState == PlaybackConnectionState::Idle
                                                 ? NeutralLightGrey
                                                 : TFT_RED;
  drawStatusBadge(156, "H", homeAssistantConnected ? SpotifyGreen : TFT_RED);
  drawStatusBadge(176, "S", playbackStatusColor);
  drawWifiIndicator(canvas, 214, 1);
  drawBatteryIndicator(canvas, battery, 250, 5);
  canvas.drawFastHLine(8, 25, 304, TFT_DARKGREY);

  // Body: only show output and volume controls when Spotify reports an active playback context.
  if (playback.hasPlayback) {
    const String device = playback.deviceName.isEmpty() ? I18n::text("no_active_device") : playback.deviceName;
    canvas.setTextColor(TFT_CYAN, TFT_BLACK);
    canvas.drawString(clippedText(canvas, device, 304, 2), 8, 32, 2);
  }

  const String title = titleText(playback);
  canvas.setTextColor(TFT_WHITE, TFT_BLACK);
  drawMarqueeText(canvas, titleMarquee_, title, 8, playback.hasPlayback ? 56 : 62, 304, 4, 34);

  const String artist = artistText(playback);
  if (!artist.isEmpty()) {
    canvas.setTextColor(NeutralLightGrey, TFT_BLACK);
    drawMarqueeText(canvas, artistMarquee_, artist, 8, 91, 304, 4, 30);
  }

  if (playback.hasPlayback && playback.durationMs > 0) {
    const int percent = (estimatedProgressMs(playback) * 100) / playback.durationMs;
    drawProgressBar(canvas, 8, 124, 304, 8, percent, SpotifyGreen);
  }

  if (playback.hasPlayback && playback.supportsVolume && displayedVolume >= 0) {
    const int volumeFillPercent = (constrain(displayedVolume, 0, Config::MaxSpotifyVolumePercent) * 100) /
                                  Config::MaxSpotifyVolumePercent;
    canvas.setTextColor(VolumeOrange, TFT_BLACK);
    canvas.drawString(String(I18n::text("volume")) + " " + String(displayedVolume) + "%", 8, 137, 2);
    drawProgressBar(canvas, 70, 142, 242, 5, volumeFillPercent, VolumeOrange);
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
    canvas.setTextColor(SpotifyGreen, TFT_BLACK);
  }
  canvas.drawString(clippedText(canvas, footer, 304, 2), 8, 151, 2);
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
  const int rowHeight = 27;
  const int maxLabelWidth = itemCount > 4 ? 276 : 288;
  const size_t visibleCount = min(itemCount, static_cast<size_t>(4));
  size_t firstVisible = 0;
  if (itemCount > visibleCount && selectedIndex >= visibleCount) {
    firstVisible = selectedIndex - visibleCount + 1;
  }

  for (size_t visibleIndex = 0; visibleIndex < visibleCount; visibleIndex++) {
    const size_t index = firstVisible + visibleIndex;
    const int y = rowTop + (visibleIndex * rowHeight);
    const bool selected = index == selectedIndex;

    if (selected) {
      canvas.fillRoundRect(8, y - 2, 296, rowHeight - 2, 4, TFT_DARKGREEN);
      canvas.drawRoundRect(8, y - 2, 296, rowHeight - 2, 4, TFT_GREEN);
      canvas.setTextColor(TFT_WHITE, TFT_DARKGREEN);
    } else {
      canvas.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    }

    canvas.drawString(clippedText(canvas, items[index].label, maxLabelWidth, 2), 16, y + 3, 2);
  }

  if (itemCount > visibleCount) {
    const int trackX = 309;
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
  drawDJConnectIcon(canvas, 14, 34, 44);

  canvas.setTextColor(TFT_WHITE, TFT_BLACK);
  canvas.drawString("DJConnect", 66, 38, 4);

  canvas.setTextColor(NeutralLightGrey, TFT_BLACK);
  canvas.drawString(Config::AppVersion, 70, 70, 2);

  struct Row {
    const char *label;
    String value;
    uint16_t color;
  };
  Row rows[] = {
      {I18n::text("web"), status.webAddress.isEmpty() ? "-" : status.webAddress, TFT_WHITE},
      {I18n::text("wifi"), I18n::connected(status.wifiConnected), static_cast<uint16_t>(status.wifiConnected ? SpotifyGreen : TFT_RED)},
      {"Spotify", I18n::connected(status.spotifyConnected), static_cast<uint16_t>(status.spotifyConnected ? SpotifyGreen : TFT_RED)},
      {"Home Assistant", status.haPaired ? I18n::text("connected") : I18n::text("not_paired"), static_cast<uint16_t>(status.haPaired ? SpotifyGreen : TFT_RED)},
      {"Copyright", "2026 Peter van Tol", NeutralLightGrey},
      {"Firmware", "Proprietary", NeutralLightGrey},
      {"Spotify", "Trademark Spotify AB", NeutralLightGrey},
      {"Notice", "Not affiliated", NeutralLightGrey},
      {"OSS", "See notices", NeutralLightGrey},
  };

  const size_t itemCount = sizeof(rows) / sizeof(rows[0]);
  const size_t visibleCount = 4;
  selectedIndex = min(selectedIndex, itemCount - 1);
  size_t firstVisible = 0;
  if (selectedIndex >= visibleCount) {
    firstVisible = selectedIndex - visibleCount + 1;
  }

  const int rowTop = 92;
  const int rowHeight = 17;
  for (size_t visibleIndex = 0; visibleIndex < visibleCount; visibleIndex++) {
    const size_t index = firstVisible + visibleIndex;
    const int y = rowTop + (visibleIndex * rowHeight);
    const bool selected = index == selectedIndex;
    if (selected) {
      canvas.fillRoundRect(8, y - 1, 292, rowHeight - 1, 3, TFT_DARKGREEN);
    }
    canvas.setTextColor(TFT_WHITE, selected ? TFT_DARKGREEN : TFT_BLACK);
    canvas.drawString(clippedText(canvas, rows[index].label, 112, 2), 14, y, 2);
    canvas.setTextColor(rows[index].color, selected ? TFT_DARKGREEN : TFT_BLACK);
    canvas.drawString(clippedText(canvas, rows[index].value, 170, 2), 136, y, 2);
  }

  const int trackX = 309;
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
  const size_t visibleLines = min(lineCount, static_cast<size_t>(9));
  for (size_t index = 0; index < visibleLines; index++) {
    const int y = rowTop + static_cast<int>(index * rowHeight);
    canvas.drawString(clippedText(canvas, lines[index], 306, 1), 8, y, 1);
  }

  if (visibleLines == 0) {
    canvas.drawString("No logs yet", 8, 58, 2);
  }

  drawMenuFooter(canvas, notice);
}

template <typename Canvas>
void DisplayManager::renderPong(Canvas &canvas, int paddleY, int ballX, int ballY, int score, bool missFlash, const StatusNotice &notice) {
  canvas.setTextDatum(TL_DATUM);
  canvas.fillScreen(TFT_BLACK);
  canvas.setTextColor(TFT_GREEN, TFT_BLACK);
  canvas.drawString("Pong", 8, 5, 2);
  const String scoreText = String("Score ") + String(score);
  canvas.setTextColor(TFT_WHITE, TFT_BLACK);
  canvas.drawString(scoreText, 312 - canvas.textWidth(scoreText, 2), 8, 2);
  canvas.drawFastHLine(8, 36, 304, TFT_DARKGREY);
  canvas.drawFastVLine(160, 42, 118, TFT_DARKGREY);
  canvas.fillRoundRect(18, paddleY, 8, 34, 3, TFT_GREEN);
  canvas.fillCircle(ballX, ballY, 4, TFT_ORANGE);
  if (missFlash) {
    canvas.drawRect(0, 0, 320, 170, TFT_RED);
    canvas.drawRect(1, 1, 318, 168, TFT_RED);
  }
  drawMenuFooter(canvas, notice);
}

template <typename Canvas>
void DisplayManager::renderAsteroids(Canvas &canvas, int shipX, int shipY, int asteroidX, int asteroidY, int bulletY, bool bulletActive, int score, bool hitFlash, const StatusNotice &notice) {
  canvas.setTextDatum(TL_DATUM);
  canvas.fillScreen(TFT_BLACK);
  canvas.setTextColor(TFT_GREEN, TFT_BLACK);
  canvas.drawString(I18n::text("asteroids"), 8, 5, 2);
  const String scoreText = String("Score ") + String(score);
  canvas.setTextColor(TFT_WHITE, TFT_BLACK);
  canvas.drawString(scoreText, 312 - canvas.textWidth(scoreText, 2), 8, 2);
  canvas.drawFastHLine(8, 36, 304, TFT_DARKGREY);
  canvas.drawLine(shipX, shipY - 12, shipX - 10, shipY + 10, TFT_GREEN);
  canvas.drawLine(shipX, shipY - 12, shipX + 10, shipY + 10, TFT_GREEN);
  canvas.drawLine(shipX - 10, shipY + 10, shipX + 10, shipY + 10, TFT_GREEN);
  canvas.drawCircle(asteroidX, asteroidY, 12, TFT_ORANGE);
  canvas.drawLine(asteroidX - 8, asteroidY - 4, asteroidX - 2, asteroidY - 12, TFT_ORANGE);
  canvas.drawLine(asteroidX + 3, asteroidY + 11, asteroidX + 10, asteroidY + 3, TFT_ORANGE);
  if (bulletActive) {
    canvas.fillRoundRect(shipX - 2, bulletY, 4, 10, 2, TFT_YELLOW);
  }
  if (hitFlash) {
    canvas.drawRect(0, 0, 320, 170, TFT_RED);
    canvas.drawRect(1, 1, 318, 168, TFT_RED);
  }
  drawMenuFooter(canvas, notice);
}

template <typename Canvas>
void DisplayManager::renderFlyer(Canvas &canvas, int planeY, int obstacleX, int obstacleY, int shotX, bool shotActive, int score, bool hitFlash, const StatusNotice &notice) {
  canvas.setTextDatum(TL_DATUM);
  canvas.fillScreen(TFT_BLACK);
  canvas.setTextColor(TFT_GREEN, TFT_BLACK);
  canvas.drawString(I18n::text("flyer"), 8, 5, 2);
  const String scoreText = String("Score ") + String(score);
  canvas.setTextColor(TFT_WHITE, TFT_BLACK);
  canvas.drawString(scoreText, 312 - canvas.textWidth(scoreText, 2), 8, 2);
  canvas.drawFastHLine(8, 36, 304, TFT_DARKGREY);
  const int planeX = 42;
  canvas.fillTriangle(planeX + 18, planeY, planeX - 12, planeY - 10, planeX - 12, planeY + 10, TFT_GREEN);
  canvas.drawLine(planeX - 4, planeY, planeX - 22, planeY - 18, TFT_GREEN);
  canvas.drawLine(planeX - 4, planeY, planeX - 22, planeY + 18, TFT_GREEN);
  canvas.fillRect(obstacleX - 8, obstacleY - 16, 16, 32, TFT_ORANGE);
  canvas.drawCircle(obstacleX, obstacleY - 22, 5, TFT_DARKGREY);
  canvas.drawCircle(obstacleX, obstacleY + 22, 5, TFT_DARKGREY);
  if (shotActive) {
    canvas.fillRoundRect(shotX, planeY - 2, 14, 4, 2, TFT_YELLOW);
  }
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

  const uint16_t activeColor = level >= 4 ? SpotifyGreen : level >= 3 ? TFT_YELLOW : TFT_RED;
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
  canvas.setTextColor(BrightPurple, TFT_BLACK);
  canvas.drawString(clippedText(canvas, title, 260, 2), 8, 5, 2);
  canvas.setTextColor(TFT_WHITE, TFT_BLACK);
  canvas.drawString(I18n::text("menu"), 270, 5, 2);
  canvas.drawFastHLine(8, 25, 304, TFT_DARKGREY);
}

template <typename Canvas>
void DisplayManager::drawMenuFooter(Canvas &canvas, const StatusNotice &notice) {
  if (notice.isVisible()) {
    canvas.setTextColor(BrightPurple, TFT_BLACK);
    canvas.drawString(clippedText(canvas, notice.message, 304, 2), 8, 151, 2);
    return;
  }

  canvas.setTextColor(TFT_WHITE, TFT_BLACK);
  const String backHint = I18n::text("back_top_button");
  canvas.drawString(backHint, 312 - canvas.textWidth(backHint, 2), 151, 2);
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
