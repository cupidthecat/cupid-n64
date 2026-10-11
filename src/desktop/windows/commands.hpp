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
  Screenshot,
  DisplayFit,
  DisplayInteger,
  DisplayStretch,
  AspectStandard,
  AspectPixels,
  ViewportCustom,
  FullscreenMonitorCurrent,
  ClearRecentRoms,
  StateSlotFirst = 100,
  RewindIntervalFirst = 200,
  RewindCapacityFirst = 210,
  RewindMemoryFirst = 220,
  ViewportPresetFirst = 300,
  FullscreenMonitorFirst = 400,
  RecentRomFirst = 500
};

} // namespace cupid::desktop
