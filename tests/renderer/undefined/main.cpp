#include "../../support/test.hpp"
#include <stdexcept>
#include <string_view>

namespace test {
void gpu_subpass_attachment_tests();
void gpu_primitive_lod_tests();
} // namespace test

int main(int argc, char **argv) {
  if (argc != 2)
    return 1;
  try {
    if (std::string_view(argv[1]) == "--subpasses")
      test::gpu_subpass_attachment_tests();
    else if (std::string_view(argv[1]) == "--lod")
      test::gpu_primitive_lod_tests();
    else
      return 1;
  } catch (const std::runtime_error &error) {
    std::cout << error.what() << '\n';
    return 77;
  }
  std::cout << test::checks << " checks, " << test::failures << " failures\n";
  return test::failures ? 1 : 0;
}
