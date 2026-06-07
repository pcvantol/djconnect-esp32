// Short I2S UI sounds for the on-board T-Embed-CC1101 speaker.
#pragma once

#include <Arduino.h>

class SoundManager {
public:
  using StreamActivityCallback = void (*)(void *context);

  // Initializes I2S output and starts the tiny audio worker task.
  void begin();

  // Sets the global level for generated on-board speaker cues.
  void setVolumePercent(uint8_t percent);

  // Friendly startup arpeggio played after hardware init.
  void playStartup();

  // Short pitch cue for encoder volume changes.
  void playVolumeTick(int direction);

  // Subtle left/right cue for menu cursor movement.
  void playMenuTick(int direction);

  // Very short cue for the middle encoder button press.
  void playButtonPress();

  // Gentle confirmation cue for completed UI actions.
  void playConfirm();

  // Subtle game cue when the Pong ball bounces off a wall.
  void playPongBounce();

  // Low game cue when the Pong ball is missed.
  void playPongMiss();

  // Single bleep when opening the menu.
  void playMenuOpen();

  // Single bleep when navigating back.
  void playBack();

  // Soft descending cue immediately before the device enters turn-off sleep.
  void playTurnOff();

  // Short bright cue immediately before a top-button soft reset.
  void playSoftReset();

  // Distinct cue when push-to-talk starts listening.
  void playPttStart();

  // Distinct cue when push-to-talk stops and begins processing.
  void playPttStop();

  // Lower warning beep used before destructive reset actions.
  void playHardReset();

  // Clear attention cue when the battery crosses into the warning range.
  void playBatteryWarning();

  // Gentle periodic setup-mode attention beep while the captive portal is open.
  void playSetupPrompt();

  // Bright confirmation cue when charging passes the nearly-full threshold.
  void playChargingComplete();

  // Three-stage OTA cues: start, throttled progress tick, and final result.
  void playOtaStart();
  void playOtaProgress();
  void playOtaComplete();
  void playOtaFailed();

  // Plays a raw WAV stream returned by Home Assistant voice processing.
  bool playWavStream(Stream &stream, int contentLength);

  // Streams an MP3 URL directly to the on-board speaker using a small decoder.
  bool playMp3Stream(const String &url);

  // Plays an already opened MP3 HTTP stream. Used after content-type/header sniffing.
  bool playMp3Stream(Stream &stream, const uint8_t *prefix, size_t prefixLength, int contentLength);

  // Optional callback invoked from blocking WAV/MP3 playback loops for visual activity frames.
  void setStreamActivityCallback(StreamActivityCallback callback, void *context);

private:
  enum class AudioState : uint8_t {
    Idle,
    Cue,
    StreamingWav,
    StreamingMp3,
  };

  enum class Event : uint8_t {
    Startup,
    VolumeUp,
    VolumeDown,
    MenuLeft,
    MenuRight,
    ButtonPress,
    Confirm,
    PongBounce,
    PongMiss,
    MenuOpen,
    Back,
    TurnOff,
    SoftReset,
    PttStart,
    PttStop,
    HardReset,
    BatteryWarning,
    SetupPrompt,
    ChargingComplete,
    OtaStart,
    OtaProgress,
    OtaComplete,
    OtaFailed,
  };

  static void soundTask(void *parameter);
  void runTask();
  void enqueue(Event event);
  bool installI2s();
  bool takeI2s(uint32_t timeoutMs);
  void releaseI2s();
  bool beginAudioState(AudioState state, uint32_t timeoutMs);
  void endAudioState();
  void playTone(uint16_t frequency, uint16_t durationMs, uint8_t amplitude = 18);
  void playSilence(uint16_t durationMs);
  void notifyStreamActivity();

  QueueHandle_t queue_ = nullptr;
  SemaphoreHandle_t i2sMutex_ = nullptr;
  bool ready_ = false;
  AudioState audioState_ = AudioState::Idle;
  uint8_t volumePercent_ = 100;
  uint32_t lastVolumeTickAt_ = 0;
  StreamActivityCallback streamActivityCallback_ = nullptr;
  void *streamActivityContext_ = nullptr;
};
