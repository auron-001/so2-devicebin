#include "il2cpp/typeinfo.h"
#include "memory/memory.h"
#include "offsets.h"
#include "process/process_manager.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
constexpr const char *kDefaultCapturePath = "/data/local/tmp/so2_capture.json";
} // namespace

int main(int argc, char **argv) {
  process_manager proc("com.axlebolt.standoff2");
  if (!proc.initialize())
    return -1;

  memory_utils::initialize(proc.get_pid());

  if (argc > 1 && strcmp(argv[1], "json") == 0) {
    bool any = false;
    bool full = false;
    const char *out_path = kDefaultCapturePath;

    for (int i = 2; i < argc; ++i) {
      if (strcmp(argv[i], "any") == 0 || strcmp(argv[i], "auto") == 0) {
        any = true;
      } else if (strcmp(argv[i], "full") == 0) {
        full = true;
      } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
        out_path = argv[++i];
      } else {
        out_path = argv[i];
      }
    }

    if (any)
      return json_capture_any(proc.get_pid(), proc.get_libunity_base(), full,
                              out_path);
    return json_capture(proc.get_pid(), proc.get_libunity_base(),
                        ofset::typeinfo::facebook_auth_request, full, out_path);
  }

  if (argc > 1 && strcmp(argv[1], "scan") == 0) {
    uint64_t rva = ofset::typeinfo::facebook_auth_request;
    const char *hint = ofset::class_name::facebook_auth_request;
    bool watch = false;
    int argi = 2;
    if (argc > argi && strcmp(argv[argi], "watch") == 0) {
      watch = true;
      argi++;
    }
    if (argc > argi)
      rva = strtoull(argv[argi], nullptr, 0);
    if (argc > argi + 1)
      hint = argv[argi + 1];

    scan_instances(proc.get_pid(), proc.get_libunity_base(), rva, hint, watch);
    return 0;
  }

  if (argc > 1 && strcmp(argv[1], "typeinfo") == 0) {
    uint64_t rva = ofset::typeinfo::facebook_auth_request;
    const char *hint = ofset::class_name::facebook_auth_request;
    if (argc > 2)
      rva = strtoull(argv[2], nullptr, 0);
    if (argc > 3)
      hint = argv[3];

    dump_typeinfo(proc.get_pid(), proc.get_libunity_base(), rva, hint);
    return 0;
  }

  printf("Usage: %s json [any|auto] [full] [out.json]\n", argv[0]);
  printf("       %s scan [watch] [rva] [name]\n", argv[0]);
  printf("       %s typeinfo [rva] [name]\n", argv[0]);
  printf("JSON is printed to stdout and saved to %s by default.\n",
         kDefaultCapturePath);
  return -1;
}
