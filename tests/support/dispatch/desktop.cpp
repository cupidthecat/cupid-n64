#include <iostream>
#include <string_view>

int video_output_test_entry(int argc, char **argv);
#ifdef CUPID_SESSION_TESTS
int session_video_test_entry(int argc, char **argv);
int session_state_test_entry(int argc, char **argv);
int session_launch_test_entry();
#endif

int main(int argc, char **argv) {
  if (argc < 2)
    return 2;
  const std::string_view name = argv[1];
  --argc;
  ++argv;
  if (name == "video_output_tests")
    return video_output_test_entry(argc, argv);
#ifdef CUPID_SESSION_TESTS
  if (name == "session_video_tests")
    return session_video_test_entry(argc, argv);
  if (name == "session_state_tests")
    return session_state_test_entry(argc, argv);
  if (name == "session_launch_tests")
    return session_launch_test_entry();
#endif
  std::cerr << "Unknown desktop test: " << name << '\n';
  return 2;
}
