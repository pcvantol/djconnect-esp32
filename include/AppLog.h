// Serial-backed in-memory log buffer shared by the TFT logs screen and web dashboard.
#pragma once

#include <Arduino.h>
#include <Print.h>

class AppLogger : public Print {
public:
  static constexpr size_t MaxLines = 40;

  // Clears the RAM buffer and starts teeing all writes to the existing Serial port.
  void begin();

  // Print-compatible sink used by the firmware instead of writing to Serial directly.
  size_t write(uint8_t value) override;
  size_t write(const uint8_t *buffer, size_t size) override;

  // Returns all buffered lines as newline-delimited text, oldest first.
  String text() const;

  // Copies the newest lines into caller storage and returns the number copied.
  size_t newestLines(String *target, size_t maxLines) const;

private:
  void appendChar(char value);
  void commitCurrentLine();
  String linePrefix() const;
  String normalizedLine(const String &line) const;

  String lines_[MaxLines];
  String currentLine_;
  size_t nextLine_ = 0;
  size_t lineCount_ = 0;
  bool ready_ = false;
};

extern AppLogger AppLog;
