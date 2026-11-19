#pragma once

#include <cstdint>

#include "offsets.h"

void dump_typeinfo(int pid, uint64_t module_base, uint64_t rva,
                   const char *name_hint);

void scan_instances(int pid, uint64_t module_base, uint64_t rva,
                    const char *name_hint, bool watch);

int json_capture(int pid, uint64_t module_base, uint64_t rva, bool full,
                 const char *out_path);

int json_capture_any(int pid, uint64_t module_base, bool full,
                     const char *out_path);
