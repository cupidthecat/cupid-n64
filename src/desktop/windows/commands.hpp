#pragma once

namespace cupid::desktop {

enum Command : unsigned {
  Open = 1,
  Firmware,
  Exit,
  Pause,
  Reset,
  Fullscreen,
  Mute,
  Controls,
  FrameAdvance,
  SaveState,
  LoadState,
  UndoStateSave,
  UndoStateLoad,
  FastForward,
  HoldFastForward,
  ReleaseFastForward,
  BeginRewind,
  EndRewind,
  RewindEnabled,
  RewindMute,
  StateSlotFirst = 100,
  RewindIntervalFirst = 200,
  RewindCapacityFirst = 210,
  RewindMemoryFirst = 220
};

} // namespace cupid::desktop
