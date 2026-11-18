// Arduino entrypoints stay intentionally tiny; all real behavior lives in DJConnectApp.
#include "DJConnectApp.h"

DJConnectApp app;

void setup() {
  app.begin();
}

void loop() {
  app.loop();
}
