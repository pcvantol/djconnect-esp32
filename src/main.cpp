// Arduino entrypoints stay intentionally tiny; all real behavior lives in SpotifyDJApp.
#include "SpotifyDJApp.h"

SpotifyDJApp app;

void setup() {
  app.begin();
}

void loop() {
  app.loop();
}
