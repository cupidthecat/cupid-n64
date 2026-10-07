#include "desktop/windows/video/paint_buffer.hpp"
#include "../support/test.hpp"

using namespace cupid::desktop;

int main() {
  const auto target = CreateCompatibleDC(nullptr);
  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = 80;
  info.bmiHeader.biHeight = -60;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  void *pixels = nullptr;
  const auto bitmap = CreateDIBSection(target, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
  test::equal(target != nullptr && bitmap != nullptr && pixels != nullptr, true);
  if (!target || !bitmap || !pixels)
    return 1;
  const auto original = SelectObject(target, bitmap);
  const auto initial = CreateSolidBrush(RGB(200, 30, 100));
  const auto blue = CreateSolidBrush(RGB(10, 20, 240));
  const auto green = CreateSolidBrush(RGB(20, 230, 40));
  const auto resources = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
  {
    PaintBuffer buffer;
    RECT bounds{0, 0, 80, 60};
    FillRect(target, &bounds, initial);
    auto drawing = buffer.begin(target, 80, 60);
    FillRect(drawing, &bounds, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    test::equal(GetPixel(target, 40, 30), RGB(200, 30, 100));
    RECT left{0, 0, 40, 60}, right{40, 0, 80, 60};
    FillRect(drawing, &left, blue);
    FillRect(drawing, &right, green);
    test::equal(GetPixel(target, 20, 30), RGB(200, 30, 100));
    test::equal(GetPixel(target, 60, 30), RGB(200, 30, 100));
    buffer.present(target);
    test::equal(GetPixel(target, 20, 30), RGB(10, 20, 240));
    test::equal(GetPixel(target, 60, 30), RGB(20, 230, 40));

    drawing = buffer.begin(target, 80, 60);
    test::equal(GetPixel(drawing, 20, 30), RGB(10, 20, 240));
    FillRect(drawing, &left, green);
    test::equal(GetPixel(target, 20, 30), RGB(10, 20, 240));
    buffer.present(target);
    test::equal(GetPixel(target, 20, 30), RGB(20, 230, 40));

    for (int pass = 0; pass < 100; ++pass) {
      const int width = pass % 2 ? 80 : 40, height = pass % 2 ? 60 : 30;
      bounds = {0, 0, width, height};
      drawing = buffer.begin(target, width, height);
      BITMAP shape{};
      GetObjectW(GetCurrentObject(drawing, OBJ_BITMAP), sizeof(shape), &shape);
      test::equal(shape.bmWidth, width);
      test::equal(shape.bmHeight, height);
      FillRect(drawing, &bounds, blue);
      buffer.present(target);
      test::equal(GetPixel(target, width - 1, height - 1), RGB(10, 20, 240));
    }
    bool rejected = false;
    try {
      buffer.begin(target, 0, 60);
    } catch (const std::runtime_error &) {
      rejected = true;
    }
    test::equal(rejected, true);
  }
  test::equal(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS), resources);
  SelectObject(target, original);
  DeleteObject(bitmap);
  DeleteObject(initial);
  DeleteObject(blue);
  DeleteObject(green);
  DeleteDC(target);
  std::cout << test::checks << " checks, " << test::failures << " failures\n";
  return test::failures ? 1 : 0;
}
