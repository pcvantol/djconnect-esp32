// I2S speaker cues. Uses generated square waves to avoid storing audio assets.
#include "SoundManager.h"

#include <driver/i2s.h>

#include "AppLog.h"
#include "Config.h"

static constexpr i2s_port_t SpeakerI2sPort = I2S_NUM_0;
static constexpr uint32_t MinVolumeTickIntervalMs = 90;

void SoundManager::begin() {
  if (ready_) {
    return;
  }

  const i2s_config_t i2sConfig = {
      .mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX),
      .sample_rate = Config::SpeakerSampleRate,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 4,
      .dma_buf_len = 128,
      .use_apll = false,
      .tx_desc_auto_clear = true,
      .fixed_mclk = 0,
  };
  const i2s_pin_config_t pinConfig = {
      .mck_io_num = I2S_PIN_NO_CHANGE,
      .bck_io_num = Config::SpeakerBclkPin,
      .ws_io_num = Config::SpeakerLrclkPin,
      .data_out_num = Config::SpeakerDataPin,
      .data_in_num = I2S_PIN_NO_CHANGE,
  };

  if (i2s_driver_install(SpeakerI2sPort, &i2sConfig, 0, nullptr) != ESP_OK ||
      i2s_set_pin(SpeakerI2sPort, &pinConfig) != ESP_OK) {
    AppLog.println("Speaker I2S init failed");
    return;
  }
  i2s_zero_dma_buffer(SpeakerI2sPort);

  queue_ = xQueueCreate(6, sizeof(Event));
  if (queue_ == nullptr) {
    AppLog.println("Speaker queue failed");
    return;
  }

  ready_ = true;
  xTaskCreatePinnedToCore(soundTask, "sound", 3072, this, 1, nullptr, 0);
}

void SoundManager::playStartup() {
  enqueue(Event::Startup);
}

void SoundManager::playVolumeTick(int direction) {
  const uint32_t now = millis();
  if (now - lastVolumeTickAt_ < MinVolumeTickIntervalMs) {
    return;
  }
  lastVolumeTickAt_ = now;
  enqueue(direction >= 0 ? Event::VolumeUp : Event::VolumeDown);
}

void SoundManager::playHardReset() {
  enqueue(Event::HardReset);
}

void SoundManager::playBatteryWarning() {
  enqueue(Event::BatteryWarning);
}

void SoundManager::playSetupPrompt() {
  enqueue(Event::SetupPrompt);
}

void SoundManager::playChargingComplete() {
  enqueue(Event::ChargingComplete);
}

void SoundManager::soundTask(void *parameter) {
  static_cast<SoundManager *>(parameter)->runTask();
}

void SoundManager::runTask() {
  Event event;
  for (;;) {
    if (xQueueReceive(queue_, &event, portMAX_DELAY) != pdTRUE) {
      continue;
    }

    switch (event) {
      case Event::Startup:
        playTone(523, 70);
        playSilence(20);
        playTone(659, 70);
        playSilence(20);
        playTone(784, 110);
        break;
      case Event::VolumeUp:
        playTone(1047, 28, 14);
        break;
      case Event::VolumeDown:
        playTone(784, 28, 14);
        break;
      case Event::HardReset:
        // Deliberately unlike startup/soft reset: sharp descending alarm burst.
        playTone(988, 90, 24);
        playSilence(35);
        playTone(494, 120, 24);
        playSilence(35);
        playTone(247, 180, 24);
        break;
      case Event::BatteryWarning:
        playTone(880, 90, 20);
        playSilence(50);
        playTone(660, 160, 20);
        break;
      case Event::SetupPrompt:
        playTone(659, 70, 14);
        playSilence(30);
        playTone(880, 90, 14);
        break;
      case Event::ChargingComplete:
        playTone(784, 80, 16);
        playSilence(35);
        playTone(988, 80, 16);
        playSilence(35);
        playTone(1175, 140, 16);
        break;
    }
    playSilence(8);
  }
}

void SoundManager::enqueue(Event event) {
  if (!ready_ || queue_ == nullptr) {
    return;
  }
  xQueueSend(queue_, &event, 0);
}

void SoundManager::playTone(uint16_t frequency, uint16_t durationMs, uint8_t amplitude) {
  if (!ready_ || frequency == 0 || durationMs == 0) {
    return;
  }

  const size_t sampleCount = (Config::SpeakerSampleRate * durationMs) / 1000;
  const uint32_t halfPeriod = max<uint32_t>(1, Config::SpeakerSampleRate / (frequency * 2UL));
  const int16_t level = static_cast<int16_t>(amplitude) * 512;
  int16_t buffer[128];
  size_t generated = 0;

  while (generated < sampleCount) {
    const size_t count = min(sampleCount - generated, static_cast<size_t>(128));
    for (size_t index = 0; index < count; index++) {
      const bool high = ((generated + index) / halfPeriod) % 2 == 0;
      buffer[index] = high ? level : -level;
    }
    size_t written = 0;
    i2s_write(SpeakerI2sPort, buffer, count * sizeof(int16_t), &written, pdMS_TO_TICKS(50));
    generated += count;
  }
}

void SoundManager::playSilence(uint16_t durationMs) {
  if (!ready_ || durationMs == 0) {
    return;
  }
  const size_t sampleCount = (Config::SpeakerSampleRate * durationMs) / 1000;
  int16_t buffer[128] = {};
  size_t generated = 0;
  while (generated < sampleCount) {
    const size_t count = min(sampleCount - generated, static_cast<size_t>(128));
    size_t written = 0;
    i2s_write(SpeakerI2sPort, buffer, count * sizeof(int16_t), &written, pdMS_TO_TICKS(50));
    generated += count;
  }
}
