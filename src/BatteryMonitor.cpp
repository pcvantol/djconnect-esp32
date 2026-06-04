// BQ27220 fuel gauge integration.
#include "BatteryMonitor.h"

#include "AppLog.h"

#include "Config.h"
#include "LogicHelpers.h"

void BatteryMonitor::begin() {
  Wire.begin(Config::I2cSdaPin, Config::I2cSclPin);
  Wire.setClock(400000);
  Wire.setTimeOut(100);
  refresh();
}

bool BatteryMonitor::refresh() {
  uint16_t soc = 0;
  if (!readWord(Config::Bq27220StateOfChargeCommand, soc)) {
    markUnavailable();
    return false;
  }

  uint16_t voltage = 0;
  const bool hasVoltage = readWord(Config::Bq27220VoltageCommand, voltage);

  uint16_t status = 0;
  const bool hasStatus = readWord(Config::Bq27220BatteryStatusCommand, status);

  uint16_t currentRaw = 0;
  const bool hasCurrent = readWord(Config::Bq27220CurrentCommand, currentRaw);

  const Logic::Bq27220Reading reading = Logic::interpretBq27220(
      soc,
      hasVoltage,
      voltage,
      hasStatus,
      status,
      hasCurrent,
      currentRaw,
      Config::BatteryChargeCurrentThresholdMa);
  state_.available = reading.available;
  state_.charging = reading.charging;
  state_.discharging = reading.discharging;
  state_.full = reading.full;
  state_.percentEstimated = reading.percentEstimated;
  state_.percent = reading.percent;
  state_.gaugePercent = reading.gaugePercent;
  state_.voltageMv = reading.voltageMv;
  state_.currentMa = reading.currentMa;

  return true;
}

bool BatteryMonitor::readWord(uint8_t command, uint16_t &value) {
  // Standard commands are addressed by writing the command byte, then reading two bytes back.
  Wire.beginTransmission(Config::Bq27220I2cAddress);
  Wire.write(command);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  if (Wire.requestFrom(Config::Bq27220I2cAddress, static_cast<uint8_t>(2)) != 2) {
    return false;
  }

  const uint8_t low = Wire.read();
  const uint8_t high = Wire.read();
  value = static_cast<uint16_t>(low | (high << 8));
  return true;
}

void BatteryMonitor::markUnavailable() {
  state_.available = false;
  state_.charging = false;
  state_.discharging = false;
  state_.full = false;
  state_.percentEstimated = false;
  state_.percent = -1;
  state_.gaugePercent = -1;
  state_.voltageMv = 0;
  state_.currentMa = 0;
}
