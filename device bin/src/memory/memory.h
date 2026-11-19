#pragma once

#include <cstddef>

class memory_utils {
public:
  static void initialize(int pid);
  static bool read_memory(void *address, void *buffer, size_t size);

private:
  static int pid_;
  static bool process_vm(void *address, void *buffer, size_t size);
};
