#pragma once

#include <vector>
#include <windows.h>

namespace cupid::desktop {

class ScrollForm {
public:
  static constexpr unsigned content_id = 80;
  void initialize(HWND dialog, int footer_y);
  HWND content() const {
    return content_;
  }
  bool message(UINT event, WPARAM wparam);

private:
  struct Control {
    HWND window;
    RECT bounds;
  };
  void fit(const RECT &work_area);
  void position();
  HWND dialog_ = nullptr, content_ = nullptr;
  int width_ = 0, height_ = 0, footer_ = 0, footer_height_ = 0;
  int x_ = 0, y_ = 0, line_ = 24;
  std::vector<Control> fields_, buttons_;
};

} // namespace cupid::desktop
