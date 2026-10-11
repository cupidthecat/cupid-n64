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
  StateSlotFirst = 100
};

} // namespace cupid::desktop
