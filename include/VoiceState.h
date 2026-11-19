// Push-to-talk state for the Home Assistant Assist pipeline flow.
#pragma once

enum class VoiceState {
  Idle,
  Connecting,
  Authenticating,
  StartingPipeline,
  Listening,
  WaitingForResult,
  SendingCommand,
  Done,
  Error,
};
