#pragma once

#include <windows.h>

#include <mmsystem.h>

namespace cupid::desktop {

class TimerResolution {
public:
  TimerResolution() : active_(timeBeginPeriod(1) == TIMERR_NOERROR) {}
  ~TimerResolution() {
    if (active_)
      timeEndPeriod(1);
  }
  TimerResolution(const TimerResolution &) = delete;
  TimerResolution &operator=(const TimerResolution &) = delete;

private:
  bool active_;
};

} // namespace cupid::desktop
