// Serial-backed in-memory log buffer implementation.
#include "AppLog.h"

#include <esp_log.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

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
  setenv("TZ", "CET-1CEST,M3.5.0/2,M10.5.0/3", 1);
  tzset();
  currentLine_[0] = '\0';
  currentLength_ = 0;
  memset(lines_, 0, sizeof(lines_));
  nextLine_ = 0;
  lineCount_ = 0;
  ready_ = true;
  esp_log_set_vprintf(appLogVprintf);
}

size_t AppLogger::write(uint8_t value) {
  appendChar(static_cast<char>(value));
  return 1;
}

size_t AppLogger::write(const uint8_t *buffer, size_t size) {
  for (size_t index = 0; index < size; index++) {
    appendChar(static_cast<char>(buffer[index]));
  }
  return size;
}

String AppLogger::text() const {
  String output;
  output.reserve((lineCount_ + (currentLength_ == 0 ? 0 : 1)) * 80);
  const size_t first = lineCount_ == MaxLines ? nextLine_ : 0;
  for (size_t index = 0; index < lineCount_; index++) {
    const size_t lineIndex = (first + index) % MaxLines;
    output += lines_[lineIndex];
    output += '\n';
  }
  if (currentLength_ > 0) {
    const String line = normalizedLine(String(currentLine_));
    output += linePrefix(severityForLine(line)) + line;
  }
  return output;
}

size_t AppLogger::newestLines(String *target, size_t maxLines) const {
  if (target == nullptr || maxLines == 0) {
    return 0;
  }

  const size_t available = lineCount_ + (currentLength_ == 0 ? 0 : 1);
  const size_t count = min(maxLines, available);
  const size_t firstVisible = available - count;
  const size_t firstStored = lineCount_ == MaxLines ? nextLine_ : 0;

  for (size_t index = 0; index < count; index++) {
    const size_t sourceIndex = firstVisible + index;
    if (sourceIndex < lineCount_) {
      const size_t lineIndex = (firstStored + sourceIndex) % MaxLines;
      target[index] = lines_[lineIndex];
    } else {
      const String line = normalizedLine(String(currentLine_));
      target[index] = linePrefix(severityForLine(line)) + line;
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

  if (currentLength_ + 1 >= MaxLineLength) {
    commitCurrentLine();
  }
  currentLine_[currentLength_++] = value;
  currentLine_[currentLength_] = '\0';
  if (currentLength_ >= 120) {
    commitCurrentLine();
  }
}

void AppLogger::commitCurrentLine() {
  if (currentLength_ == 0) {
    return;
  }
  const String normalized = normalizedLine(String(currentLine_));
  const String line = linePrefix(severityForLine(normalized)) + normalized;
  Serial.println(line);
  snprintf(lines_[nextLine_], MaxLineLength, "%s", line.c_str());
  currentLine_[0] = '\0';
  currentLength_ = 0;
  nextLine_ = (nextLine_ + 1) % MaxLines;
  if (lineCount_ < MaxLines) {
    lineCount_++;
  }
}

String AppLogger::linePrefix(const String &severity) const {
  time_t now = time(nullptr);
  struct tm local = {};
  char buffer[40] = {};
  if (now >= 1704067200 && localtime_r(&now, &local) != nullptr) {
    strftime(buffer, sizeof(buffer), "[%d-%m-%Y %H:%M:%S]", &local);
  } else {
    snprintf(buffer, sizeof(buffer), "[00-00-0000 00:00:00]");
  }
  return String(buffer) + " [" + severity + "] ";
}

String AppLogger::normalizedLine(const String &line) const {
  String value = line;
  value.trim();
  if (value.startsWith("[SpotifyDJ] ")) {
    value.remove(0, 12);
  } else if (value.startsWith("[SpotifyDJ]")) {
    value.remove(0, 11);
    value.trim();
  }
  if (value.startsWith("[inf]") || value.startsWith("[err]") ||
      value.startsWith("[wrn]") || value.startsWith("[dbg]")) {
    value.remove(0, 5);
    value.trim();
  }
  return value;
}

String AppLogger::severityForLine(const String &line) const {
  String value = line;
  value.trim();
  if (value.startsWith("[inf]") || value.startsWith("[err]") ||
      value.startsWith("[wrn]") || value.startsWith("[dbg]")) {
    return value.substring(1, 4);
  }

  String lower = value;
  lower.toLowerCase();

  if (lower.startsWith("e (") ||
      lower.indexOf(" failed") >= 0 ||
      lower.indexOf(" error") >= 0 ||
      lower.indexOf("invalid") >= 0 ||
      lower.indexOf("unauthorized") >= 0 ||
      lower.indexOf(" forbidden") >= 0 ||
      lower.indexOf("timeout") >= 0 ||
      lower.indexOf("mismatch") >= 0 ||
      lower.indexOf("aborted") >= 0 ||
      lower.indexOf("not found") >= 0 ||
      lower.indexOf("missing") >= 0 && lower.indexOf("source: missing") < 0) {
    return "err";
  }

  if (lower.startsWith("w (") ||
      lower.indexOf(" warning") >= 0 ||
      lower.indexOf(" skipped") >= 0 ||
      lower.indexOf(" busy") >= 0 ||
      lower.indexOf(" unavailable") >= 0 ||
      lower.indexOf(" too low") >= 0 ||
      lower.indexOf("retry") >= 0) {
    return "wrn";
  }

  if (lower.startsWith("network start:") ||
      lower.startsWith("network done:") ||
      lower.startsWith("spotify request:") ||
      lower.startsWith("spotify response:") ||
      lower.startsWith("battery:") ||
      lower.startsWith("responsiveness:") ||
      lower.startsWith("memory:") ||
      lower.startsWith("heap:")) {
    return "dbg";
  }

  return "inf";
}
