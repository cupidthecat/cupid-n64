#include <iostream>
#include <string_view>

int renderer_settings_test_entry();
int renderer_undefined_test_entry(int argc, char **argv);
int renderer_state_test_entry();
int renderer_test_entry();
int renderer_reset_test_entry(int argc, char **argv);

int main(int argc, char **argv) {
  if (argc < 2)
    return 2;
  const std::string_view name = argv[1];
  --argc;
  ++argv;
  if (name == "renderer_settings_tests")
    return renderer_settings_test_entry();
  if (name == "renderer_undefined_tests")
    return renderer_undefined_test_entry(argc, argv);
  if (name == "renderer_state_tests")
    return renderer_state_test_entry();
  if (name == "renderer_tests")
    return renderer_test_entry();
  if (name == "renderer_reset_tests")
    return renderer_reset_test_entry(argc, argv);
  std::cerr << "Unknown renderer test: " << name << '\n';
  return 2;
}
