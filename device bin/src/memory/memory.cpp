#include "memory.h"

#include <sys/uio.h>

int memory_utils::pid_ = -1;

void memory_utils::initialize(int pid) { pid_ = pid; }

bool memory_utils::read_memory(void *address, void *buffer, size_t size) {
  return process_vm(address, buffer, size);
}

bool memory_utils::process_vm(void *address, void *buffer, size_t size) {
  if (pid_ < 0 || !buffer || !size)
    return false;

  struct iovec local[1];
  struct iovec remote[1];

  local[0].iov_base = buffer;
  local[0].iov_len = size;

  remote[0].iov_base = address;
  remote[0].iov_len = size;

  ssize_t bytes = process_vm_readv(pid_, local, 1, remote, 1, 0);
  return bytes == static_cast<ssize_t>(size);
}
