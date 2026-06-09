// Owns all TFT rendering and display backlight behavior.
// Keep screen-specific drawing here so future menu pages can be added without touching Spotify or input code.
#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>

#include "AppState.h"
#include "Config.h"

struct MenuItemView {
  String label;
};

class DisplayManager {
public:
  // Powers the display and allocates the sprite buffer while keeping backlight off until first draw.
  void begin();

  // Draws a simple boot/status screen before the full Spotify state is available.
  void showBootMessage(const String &message);

  // Draws a boot/status screen with the current battery and charge state in the header.
  void showBootMessage(const String &message, const BatteryState &battery);

  // Draws the Home Assistant pairing code screen with a large, readable code.
  void showPairingCode(const String &pairCode);

  // Draws the Home Assistant pairing code screen with battery and charge state in the header.
  void showPairingCode(const String &pairCode, const BatteryState &battery);

  // Renders the current playback screen; future menu screens can be added beside this method.
  void renderPlaybackScreen(
      const SpotifyState &playback,
      const BatteryState &battery,
      const StatusNotice &notice,
      int displayedVolume,
      bool homeAssistantConnected,
      PlaybackConnectionState playbackConnectionState);

  // Renders a selectable menu list with a clear title and highlighted cursor row.
  void renderMenuList(
      const String &title,
      const MenuItemView *items,
      size_t itemCount,
      size_t selectedIndex,
      const StatusNotice &notice);

  // Renders the static About screen with app name, drawn Spotify icon, and firmware version.
  void renderAboutScreen(const StatusNotice &notice, const AboutStatus &status, size_t selectedIndex);

  // Renders a blocking charge-required screen for low-battery protection mode.
  void renderLowBatteryScreen(const BatteryState &battery, const String &message);

  // Renders the newest firmware log lines from the in-memory Serial log buffer.
  void renderLogsScreen(const String *lines, size_t lineCount, const StatusNotice &notice);

  // Renders the tiny Pong mini game screen.
  void renderPongScreen(int paddleY, int ballX, int ballY, int score, bool missFlash, const StatusNotice &notice);

  // Renders the local Asteroids-style mini game.
  void renderAsteroidsScreen(int shipX, int shipY, int asteroidX, int asteroidY, int bulletY, bool bulletActive, int score, bool hitFlash, const StatusNotice &notice);

  // Renders the local side-scrolling plane mini game.
  void renderFlyerScreen(int planeY, int obstacleX, int obstacleY, int shotX, bool shotActive, int score, bool hitFlash, const StatusNotice &notice);

  // Renders the 160x160 current-song album art view.
  void renderAlbumArtScreen(
      const SpotifyState &playback,
      const StatusNotice &notice,
      const String &imagePath,
      const String &albumArtStatus);

  // Redraws only the moving Current Song title/artist text to avoid flickering static labels.
  void renderAlbumArtMarqueeText(const SpotifyState &playback, bool titleChanged, bool artistChanged);

  // Draws a temporary full-screen DJ response overlay above the current UI.
  void renderDjResponseOverlay(const String &text);

  // Forces the next DJ response overlay render to repaint its full-screen frame.
  void resetDjResponseOverlayCache();

  // Forces the next Current Song render to redraw the album-art pane.
  void resetAlbumArtRenderCache();

  // Advances the one-shot title marquee and returns true when the screen should redraw.
  bool advanceTitleScrollIfNeeded(const SpotifyState &playback, int maxWidth = 304, uint8_t font = 4);

  // Advances the one-shot artist/show marquee and returns true when the screen should redraw.
  bool advanceArtistScrollIfNeeded(const SpotifyState &playback, int maxWidth = 304, uint8_t font = 4);

  // Restarts title scrolling for state changes that keep the same title text, such as pause -> play.
  void restartTitleScroll();

  // Restarts artist scrolling together with title scrolling for deliberate playback transitions.
  void restartArtistScroll();

  // Restores the screen to the active brightness after any button or encoder interaction.
  void wakeForUserActivity();

  // Temporarily drives the backlight at a specific level for boot/recovery screens.
  void forceBacklightPercent(uint8_t percent);

  // Turns the backlight off after the selected idle timeout.
  void updateIdleBrightness();

  // Applies user-selected active brightness and off timeout.
  void configurePowerSaving(uint8_t activeBrightnessPercent, uint32_t offAfterMs);

  // Applies the display theme. Light mode uses the ST7789's alternate palette on this board.
  void setLightTheme(bool enabled);

  // Returns the actual backlight output percentage, used to keep the LED ring in visual sync.
  uint8_t backlightPercent() const;

  // Reports whether the TFT backlight is currently visible to the user.
  bool isOn() const;

  // Returns how long the UI has had no physical input.
  uint32_t idleMs() const;

private:
  // Tracks one horizontal one-shot text marquee.
  struct TextMarqueeState {
    String observedText;
    uint32_t changedAt = 0;
    uint32_t lastFrameAt = 0;
    int offsetPx = 0;
    bool finished = true;
  };

  template <typename Canvas>
  void renderBoot(Canvas &canvas, const String &message, const BatteryState *battery = nullptr);

  template <typename Canvas>
  void renderPairingCode(Canvas &canvas, const String &pairCode, const BatteryState *battery = nullptr);

  template <typename Canvas>
  void renderPlayback(
      Canvas &canvas,
      const SpotifyState &playback,
      const BatteryState &battery,
      const StatusNotice &notice,
      int displayedVolume,
      bool homeAssistantConnected,
      PlaybackConnectionState playbackConnectionState);

  template <typename Canvas>
  void renderMenu(
      Canvas &canvas,
      const String &title,
      const MenuItemView *items,
      size_t itemCount,
      size_t selectedIndex,
      const StatusNotice &notice);

  template <typename Canvas>
  void renderAbout(Canvas &canvas, const StatusNotice &notice, const AboutStatus &status, size_t selectedIndex);

  template <typename Canvas>
  void renderLogs(Canvas &canvas, const String *lines, size_t lineCount, const StatusNotice &notice);

  template <typename Canvas>
  void renderPong(Canvas &canvas, int paddleY, int ballX, int ballY, int score, bool missFlash, const StatusNotice &notice);
  template <typename Canvas>
  void renderAsteroids(Canvas &canvas, int shipX, int shipY, int asteroidX, int asteroidY, int bulletY, bool bulletActive, int score, bool hitFlash, const StatusNotice &notice);
  template <typename Canvas>
  void renderFlyer(Canvas &canvas, int planeY, int obstacleX, int obstacleY, int shotX, bool shotActive, int score, bool hitFlash, const StatusNotice &notice);

  template <typename Canvas>
  String clippedText(Canvas &canvas, String text, int maxWidth, uint8_t font);

  template <typename Canvas>
  void drawProgressBar(Canvas &canvas, int x, int y, int w, int h, int percent, uint16_t fillColor);

  template <typename Canvas>
  void drawBatteryIndicator(Canvas &canvas, const BatteryState &battery, int x, int y);

  template <typename Canvas>
  void drawWifiIndicator(Canvas &canvas, int x, int y);

  template <typename Canvas>
  void drawMenuTitle(Canvas &canvas, const String &title);

  template <typename Canvas>
  void drawMenuFooter(Canvas &canvas, const StatusNotice &notice);

  template <typename Canvas>
  void drawSpotifyLogo(Canvas &canvas, int x, int y);

  template <typename Canvas>
  void drawSpotifyDJIcon(Canvas &canvas, int x, int y, int size);

  template <typename Canvas>
  void drawMarqueeText(
      Canvas &canvas,
      TextMarqueeState &marquee,
      const String &text,
      int x,
      int y,
      int maxWidth,
      uint8_t font,
      int textHeight);

  void setBacklightPercent(uint8_t percent);
  void observeText(TextMarqueeState &marquee, const String &text);
  bool advanceMarqueeIfNeeded(TextMarqueeState &marquee, const String &text, uint8_t font, int maxWidth);
  void restartMarquee(TextMarqueeState &marquee);
  String titleText(const SpotifyState &playback) const;
  String artistText(const SpotifyState &playback) const;
  int estimatedProgressMs(const SpotifyState &playback) const;
  String playbackTimeText(const SpotifyState &playback) const;
  uint16_t batteryColor(const BatteryState &battery) const;

  TFT_eSPI tft_;
  TFT_eSprite screen_{&tft_};
  bool screenBufferReady_ = false;
  uint8_t backlightPercent_ = 100;
  uint8_t activeBrightnessPercent_ = 100;
  String lastAlbumArtPath_;
  String lastDjResponseOverlayText_;
  bool albumArtPaneDirty_ = true;
  uint32_t dimStartAfterMs_ = Config::DisplayDimStartAfterMs;
  uint32_t dimTargetAfterMs_ = Config::DisplayDimAfterMs;
  uint32_t offAfterMs_ = Config::DisplayOffAfterMs;
  uint32_t lastUserActivityAt_ = 0;
  TextMarqueeState titleMarquee_;
  TextMarqueeState artistMarquee_;
};
