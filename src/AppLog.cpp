// Serial-backed in-memory log buffer implementation.
#include "AppLog.h"

#include <esp_log.h>
#include <stdarg.h>
#include <stdio.h>

AppLogger AppLog;

namespace {
int appLogVprintf(const char *format, va_list args) {
  char buffer[192];
  const int written = vsnprintf(buffer, sizeof(buffer), format, args);
  if (written > 0) {
    AppLog.print(buffer);
  }
  return written;
}
}  // namespace

void AppLogger::begin() {
  currentLine_ = "";
  nextLine_ = 0;
  lineCount_ = 0;
  ready_ = true;
  esp_log_set_vprintf(appLogVprintf);
}

size_t AppLogger::write(uint8_t value) {
  Serial.write(value);
  appendChar(static_cast<char>(value));
  return 1;
}

size_t AppLogger::write(const uint8_t *buffer, size_t size) {
  Serial.write(buffer, size);
  for (size_t index = 0; index < size; index++) {
    appendChar(static_cast<char>(buffer[index]));
  }
  return size;
}

String AppLogger::text() const {
  String output;
  const size_t first = lineCount_ == MaxLines ? nextLine_ : 0;
  for (size_t index = 0; index < lineCount_; index++) {
    const size_t lineIndex = (first + index) % MaxLines;
    output += lines_[lineIndex];
    output += '\n';
  }
  if (!currentLine_.isEmpty()) {
    output += currentLine_;
  }
  return output;
}

size_t AppLogger::newestLines(String *target, size_t maxLines) const {
  if (target == nullptr || maxLines == 0) {
    return 0;
  }

  const size_t available = lineCount_ + (currentLine_.isEmpty() ? 0 : 1);
  const size_t count = min(maxLines, available);
  const size_t firstVisible = available - count;
  const size_t firstStored = lineCount_ == MaxLines ? nextLine_ : 0;

  for (size_t index = 0; index < count; index++) {
    const size_t sourceIndex = firstVisible + index;
    if (sourceIndex < lineCount_) {
      const size_t lineIndex = (firstStored + sourceIndex) % MaxLines;
      target[index] = lines_[lineIndex];
    } else {
      target[index] = currentLine_;
    }
  }
  return count;
}

void AppLogger::appendChar(char value) {
  if (!ready_) {
    return;
  }
  if (value == '\r') {
    return;
  }
  if (value == '\n') {
    commitCurrentLine();
    return;
  }

  currentLine_ += value;
  if (currentLine_.length() >= 120) {
    commitCurrentLine();
  }
}

void AppLogger::commitCurrentLine() {
  lines_[nextLine_] = currentLine_;
  currentLine_ = "";
  nextLine_ = (nextLine_ + 1) % MaxLines;
  if (lineCount_ < MaxLines) {
    lineCount_++;
  }
}
