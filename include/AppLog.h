// Serial-backed in-memory log buffer shared by the TFT logs screen and web dashboard.
#pragma once

#include <Arduino.h>
#include <Print.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class AppLogger : public Print {
public:
  static constexpr size_t MaxLines = 40;
  static constexpr size_t MaxLineLength = 144;

  // Clears the RAM buffer and starts teeing all writes to the existing Serial port.
  void begin();

  // Sets the minimum severity kept in Serial/web logs: debug, info, warning, or error.
  void setLevel(const String &level);
  String level() const;

  // Print-compatible sink used by the firmware instead of writing to Serial directly.
  size_t write(uint8_t value) override;
  size_t write(const uint8_t *buffer, size_t size) override;

  // Emits one complete log line while holding the logger lock.
  void line(const String &line);
  void line(const char *line);

  // Returns all buffered lines as newline-delimited text, oldest first.
  String text() const;

  // Copies a visible window from the newest log tail. scrollBack=0 means live/latest.
  size_t newestLines(String *target, size_t maxLines, size_t scrollBack = 0) const;

  // Returns the number of currently buffered lines, including the in-progress line.
  size_t availableLines() const;

private:
  void storeLineUnlocked(const char *line);
  void appendChar(char value);
  void appendCharUnlocked(char value);
  void commitCurrentLine();
  void commitCurrentLineUnlocked();
  void lock() const;
  void unlock() const;
  String linePrefix(const String &severity) const;
  String normalizedLine(const String &line) const;
  String severityForLine(const String &line) const;
  int severityRank(const String &severity) const;
  bool shouldLogSeverity(const String &severity) const;

  char lines_[MaxLines][MaxLineLength] = {};
  char currentLine_[MaxLineLength] = {};
  size_t currentLength_ = 0;
  size_t nextLine_ = 0;
  size_t lineCount_ = 0;
  bool ready_ = false;
  int minimumSeverityRank_ = 1;
  mutable SemaphoreHandle_t mutex_ = nullptr;
};

extern AppLogger AppLog;
