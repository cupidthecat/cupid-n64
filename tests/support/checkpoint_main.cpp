#include <iostream>
#include <string_view>

int core_state_test_entry(int argc, char **argv);
int machine_replay_test_entry(int argc, char **argv);
int disk_state_test_entry();
int rsp_vector_test_entry();
int inspection_test_entry();

int main(int argc, char **argv) {
  if (argc < 2)
    return 2;
  const std::string_view name = argv[1];
  --argc;
  ++argv;
  if (name == "core_state_tests")
    return core_state_test_entry(argc, argv);
  if (name == "machine_replay_tests")
    return machine_replay_test_entry(argc, argv);
  if (name == "disk_state_tests")
    return disk_state_test_entry();
  if (name == "rsp_vector_execution_tests")
    return rsp_vector_test_entry();
  if (name == "inspection_tests")
    return inspection_test_entry();
  std::cerr << "Unknown checkpoint test: " << name << '\n';
  return 2;
}
