// Short I2S UI sounds for the on-board T-Embed-CC1101 speaker.
#pragma once

#include <Arduino.h>

class SoundManager {
public:
  // Initializes I2S output and starts the tiny audio worker task.
  void begin();

  // Friendly startup arpeggio played after hardware init.
  void playStartup();

  // Short pitch cue for encoder volume changes.
  void playVolumeTick(int direction);

  // Lower warning beep used before destructive reset actions.
  void playHardReset();

  // Clear attention cue when the battery crosses into the warning range.
  void playBatteryWarning();

  // Gentle periodic setup-mode attention beep while the captive portal is open.
  void playSetupPrompt();

  // Bright confirmation cue when charging passes the nearly-full threshold.
  void playChargingComplete();

private:
  enum class Event : uint8_t {
    Startup,
    VolumeUp,
    VolumeDown,
    HardReset,
    BatteryWarning,
    SetupPrompt,
    ChargingComplete,
  };

  static void soundTask(void *parameter);
  void runTask();
  void enqueue(Event event);
  void playTone(uint16_t frequency, uint16_t durationMs, uint8_t amplitude = 18);
  void playSilence(uint16_t durationMs);

  QueueHandle_t queue_ = nullptr;
  bool ready_ = false;
  uint32_t lastVolumeTickAt_ = 0;
};
