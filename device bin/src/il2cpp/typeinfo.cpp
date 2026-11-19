#include "typeinfo.h"

#include "memory/memory.h"
#include "offsets.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <unistd.h>
#include <vector>

namespace {
namespace kl = ofset::il2cpp_class;
namespace fi = ofset::il2cpp_field;
namespace mi = ofset::il2cpp_method;

constexpr uint64_t kModuleSpanMax = 0x30000000ull;

bool read_raw(uint64_t addr, void *buf, size_t size) {
  if (addr < 0x1000 || !buf || !size)
    return false;
  return memory_utils::read_memory(reinterpret_cast<void *>(addr), buf, size);
}

uint64_t rd64(uint64_t addr) {
  uint64_t v = 0;
  read_raw(addr, &v, sizeof(v));
  return v;
}

uint32_t rd32(uint64_t addr) {
  uint32_t v = 0;
  read_raw(addr, &v, sizeof(v));
  return v;
}

uint16_t rd16(uint64_t addr) {
  uint16_t v = 0;
  read_raw(addr, &v, sizeof(v));
  return v;
}

uint8_t rd8(uint64_t addr) {
  uint8_t v = 0;
  read_raw(addr, &v, sizeof(v));
  return v;
}

bool ptr_ok(uint64_t p) { return p >= 0x10000ull && (p >> 48) == 0; }

std::string read_cstr(uint64_t addr, size_t max_len = 160) {
  std::string out;
  if (!ptr_ok(addr))
    return out;

  char chunk[32];
  while (out.size() < max_len) {
    size_t want = std::min(sizeof(chunk), max_len - out.size());
    if (!read_raw(addr + out.size(), chunk, want))
      break;
    for (size_t i = 0; i < want; ++i) {
      if (chunk[i] == 0)
        return out;
      out.push_back(chunk[i]);
    }
  }
  return out;
}

bool ident_like(const std::string &s) {
  if (s.empty() || s.size() > 120)
    return false;
  for (unsigned char c : s) {
    if (!(std::isalnum(c) || c == '_' || c == '.' || c == '`' || c == '<' ||
          c == '>' || c == '$' || c == '-' || c == '+' || c == '/' ||
          c == ':'))
      return false;
  }
  return true;
}

std::string klass_name(uint64_t klass) {
  if (!ptr_ok(klass))
    return "<bad>";

  std::string ns = read_cstr(rd64(klass + kl::namespaze));
  std::string name = read_cstr(rd64(klass + kl::name));
  if (!ident_like(name) || (!ns.empty() && !ident_like(ns)))
    return "<bad>";
  return ns.empty() ? name : ns + "." + name;
}
std::string klass_name_raw(uint64_t klass) {
  if (!ptr_ok(klass))
    return "<bad>";
  std::string ns = read_cstr(rd64(klass + kl::namespaze));
  std::string name = read_cstr(rd64(klass + kl::name));
  if (name.empty() || name.size() > 120)
    return "<bad>";
  return ns.empty() ? name : ns + "." + name;
}

bool klass_is_array(uint64_t klass) {
  std::string n = klass_name_raw(klass);
  return n.size() >= 2 && n.compare(n.size() - 2, 2, "[]") == 0;
}

bool klass_is_byte_array(uint64_t klass) {
  std::string n = klass_name_raw(klass);
  return n == "Byte[]" || n == "System.Byte[]";
}

bool class_plausible(uint64_t klass) {
  if (!ptr_ok(klass) || (klass & 7) != 0)
    return false;

  std::string name = read_cstr(rd64(klass + kl::name));
  if (!ident_like(name))
    return false;

  std::string ns = read_cstr(rd64(klass + kl::namespaze));
  if (!ns.empty() && !ident_like(ns))
    return false;

  uint32_t inst = rd32(klass + kl::instance_size);
  uint16_t fc = rd16(klass + kl::field_count);
  return inst > 0 && inst <= 0x10000 && fc <= 0x200;
}

const char *type_enum_name(uint8_t t) {
  switch (t) {
  case 0x01: return "void";
  case 0x02: return "bool";
  case 0x03: return "char";
  case 0x04: return "sbyte";
  case 0x05: return "byte";
  case 0x06: return "short";
  case 0x07: return "ushort";
  case 0x08: return "int";
  case 0x09: return "uint";
  case 0x0A: return "long";
  case 0x0B: return "ulong";
  case 0x0C: return "float";
  case 0x0D: return "double";
  case 0x0E: return "string";
  case 0x0F: return "ptr";
  case 0x10: return "byref";
  case 0x11: return "valuetype";
  case 0x12: return "class";
  case 0x13: return "var";
  case 0x14: return "array";
  case 0x15: return "genericinst";
  case 0x16: return "typedbyref";
  case 0x18: return "intptr";
  case 0x19: return "uintptr";
  case 0x1B: return "fnptr";
  case 0x1C: return "object";
  case 0x1D: return "szarray";
  case 0x1E: return "mvar";
  default: return "?";
  }
}

const char *ind(int depth) {
  static const char *kInd[] = {"", "  ", "    ", "      ", "        "};
  return kInd[std::min(depth, 4)];
}

std::string module_rva(uint64_t addr, uint64_t base) {
  if (!base || addr < base || addr - base >= kModuleSpanMax)
    return "";
  char buf[32];
  snprintf(buf, sizeof(buf), " rva=0x%llX",
           static_cast<unsigned long long>(addr - base));
  return buf;
}

struct MapEntry {
  uint64_t start = 0;
  uint64_t end = 0;
  uint64_t offset = 0;
  char perms[8] = {0};
  std::string path;
};

std::vector<MapEntry> load_maps(int pid) {
  std::vector<MapEntry> out;

  char name[64];
  snprintf(name, sizeof(name), "/proc/%d/maps", pid);
  FILE *f = fopen(name, "r");
  if (!f)
    return out;

  char line[1024];
  while (fgets(line, sizeof(line), f)) {
    unsigned long long s = 0, e = 0, off = 0;
    char perms[8] = {0};
    int consumed = 0;
    if (sscanf(line, "%llx-%llx %7s %llx %*s %*s %n", &s, &e, perms, &off,
               &consumed) < 4)
      continue;

    MapEntry m;
    m.start = s;
    m.end = e;
    m.offset = off;
    memcpy(m.perms, perms, sizeof(perms) - 1);
    if (consumed > 0) {
      std::string rest(line + consumed);
      while (!rest.empty() && (rest.back() == '\n' || rest.back() == '\r' ||
                               rest.back() == ' '))
        rest.pop_back();
      m.path = rest;
    }
    if (m.end > m.start)
      out.push_back(std::move(m));
  }
  fclose(f);
  return out;
}

bool ends_with(const std::string &s, const char *suffix) {
  size_t n = strlen(suffix);
  return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

std::vector<uint64_t> find_pattern(const std::vector<MapEntry> &maps,
                                   const std::string &needle,
                                   size_t max_hits = 4) {
  std::vector<uint64_t> hits;
  if (needle.empty())
    return hits;

  const size_t chunk = 1 << 20;
  const size_t overlap = needle.size() - 1;
  std::vector<char> buf(chunk);
  const char first = needle[0];

  for (const auto &m : maps) {
    if (m.perms[0] != 'r')
      continue;
    if (!m.path.empty() && m.path[0] == '[' &&
        m.path.compare(0, 5, "[vvar") == 0)
      continue;

    uint64_t addr = m.start;
    while (addr < m.end) {
      size_t n = static_cast<size_t>(
          std::min<uint64_t>(chunk, m.end - addr));
      if (!read_raw(addr, buf.data(), n)) {
        addr += n;
        continue;
      }

      size_t i = 0;
      while (i + needle.size() <= n) {
        const char *p = static_cast<const char *>(
            memchr(buf.data() + i, first, n - i - needle.size() + 1));
        if (!p)
          break;
        size_t pos = static_cast<size_t>(p - buf.data());
        if (memcmp(p, needle.data(), needle.size()) == 0) {
          hits.push_back(addr + pos);
          if (hits.size() >= max_hits)
            return hits;
        }
        i = pos + 1;
      }

      if (n <= overlap)
        break;
      addr += n - overlap;
    }
  }
  return hits;
}

std::vector<uint64_t> find_qword_refs(const std::vector<MapEntry> &maps,
                                      uint64_t value) {
  std::vector<uint64_t> refs;
  const size_t chunk = 1 << 20;
  std::vector<char> buf(chunk);

  for (const auto &m : maps) {
    if (m.perms[0] != 'r' || m.perms[1] != 'w')
      continue;

    uint64_t addr = m.start;
    while (addr < m.end) {
      size_t n = static_cast<size_t>(
          std::min<uint64_t>(chunk, m.end - addr));
      if (!read_raw(addr, buf.data(), n)) {
        addr += n;
        continue;
      }
      for (size_t i = 0; i + 8 <= n; i += 8) {
        uint64_t q = 0;
        memcpy(&q, buf.data() + i, 8);
        if (q == value)
          refs.push_back(addr + i);
      }
      addr += n;
    }
  }
  return refs;
}

std::vector<uint64_t> find_module_refs(const std::vector<MapEntry> &maps,
                                       uint64_t value, size_t max_refs = 8) {
  std::vector<uint64_t> refs;
  const size_t chunk = 1 << 20;
  std::vector<char> buf(chunk);

  for (const auto &m : maps) {
    if (m.perms[0] != 'r' || m.path.find("libunity") == std::string::npos)
      continue;

    uint64_t addr = m.start;
    while (addr < m.end) {
      size_t n = static_cast<size_t>(
          std::min<uint64_t>(chunk, m.end - addr));
      if (!read_raw(addr, buf.data(), n)) {
        addr += n;
        continue;
      }
      for (size_t i = 0; i + 8 <= n; i += 8) {
        uint64_t q = 0;
        memcpy(&q, buf.data() + i, 8);
        if (q == value) {
          refs.push_back(addr + i);
          if (refs.size() >= max_refs)
            return refs;
        }
      }
      addr += n;
    }
  }
  return refs;
}

bool validate_class(uint64_t klass, const std::string &name,
                    const std::string &ns) {
  if (!class_plausible(klass))
    return false;

  if (read_cstr(rd64(klass + kl::name)) != name)
    return false;
  if (!ns.empty() && read_cstr(rd64(klass + kl::namespaze)) != ns)
    return false;
  return true;
}

uint64_t locate_class(const std::vector<MapEntry> &maps,
                      const std::string &name, const std::string &ns) {
  printf("  [scan] searching \"%s\" in readable memory...\n", name.c_str());

  std::string needle = name;
  needle.push_back('\0');
  std::vector<uint64_t> strings = find_pattern(maps, needle);
  if (strings.empty()) {
    printf("  [scan] name string not found\n");
    return 0;
  }
  printf("  [scan] name string: %zu hit(s), first @0x%llx\n", strings.size(),
         static_cast<unsigned long long>(strings[0]));

  for (uint64_t s : strings) {
    std::vector<uint64_t> refs = find_qword_refs(maps, s);
    printf("  [scan] %zu reference(s) to 0x%llx\n", refs.size(),
           static_cast<unsigned long long>(s));

    for (uint64_t ref : refs) {
      uint64_t klass = ref - kl::name;
      if (!validate_class(klass, name, ns))
        continue;
      printf("  [scan] class found @0x%llx\n",
             static_cast<unsigned long long>(klass));
      return klass;
    }
  }
  return 0;
}

struct ClassInfo {
  uint64_t addr = 0;
  uint64_t image = 0;
  uint64_t name = 0;
  uint64_t ns = 0;
  uint64_t element = 0;
  uint64_t parent = 0;
  uint64_t generic = 0;
  uint64_t metadata_handle = 0;
  uint64_t fields = 0;
  uint64_t methods = 0;
  uint64_t statics = 0;
  uint64_t interfaces = 0;
  uint32_t instance_size = 0;
  uint32_t flags = 0;
  uint16_t field_count = 0;
  uint16_t method_count = 0;
  uint16_t interface_count = 0;
};

bool load_class(uint64_t klass, ClassInfo &ci) {
  uint8_t head[kl::head_size];
  if (!read_raw(klass, head, sizeof(head)))
    return false;

  auto Q = [&](size_t o) {
    uint64_t v = 0;
    memcpy(&v, head + o, 8);
    return v;
  };
  auto D = [&](size_t o) {
    uint32_t v = 0;
    memcpy(&v, head + o, 4);
    return v;
  };
  auto W = [&](size_t o) {
    uint16_t v = 0;
    memcpy(&v, head + o, 2);
    return v;
  };

  ci.addr = klass;
  ci.image = Q(kl::image);
  ci.statics = Q(kl::static_fields);
  ci.field_count = W(kl::field_count);
  ci.instance_size = D(kl::instance_size);
  ci.interface_count = W(kl::interface_count);
  ci.methods = Q(kl::methods);
  ci.interfaces = Q(kl::implemented_interfaces);
  ci.fields = Q(kl::fields);
  ci.ns = Q(kl::namespaze);
  ci.parent = Q(kl::parent);
  ci.generic = Q(kl::generic_class);
  ci.metadata_handle = Q(kl::type_metadata_handle);
  ci.flags = D(kl::flags);
  ci.element = Q(kl::element_class);
  ci.method_count = W(kl::method_count);
  ci.name = Q(kl::name);

  if (ci.field_count > 0x1000)
    ci.field_count = 0;
  if (ci.method_count > 0x2000)
    ci.method_count = 0;
  return true;
}

struct FieldLayout {
  uint64_t fields_off = kl::fields;
  uint64_t name_off = fi::name;
  uint64_t token_off = fi::token;
  uint64_t offset_off = fi::offset;
  uint64_t stride = fi::stride;
  bool detected = false;
};

FieldLayout g_field_layout;
int g_field_layout_attempts = 0;

bool detect_field_layout(const ClassInfo &ci) {
  if (g_field_layout.detected)
    return true;
  if (!ptr_ok(ci.addr) || ci.field_count == 0)
    return false;
  if (g_field_layout_attempts >= 3)
    return false;
  ++g_field_layout_attempts;

  uint32_t n = ci.field_count;
  if (n > 8)
    n = 8;

  int best_score = 0;
  FieldLayout best;

  for (uint64_t fields_off = 0; fields_off < kl::head_size;
       fields_off += 8) {
    uint64_t fields = rd64(ci.addr + fields_off);
    if (!ptr_ok(fields))
      continue;

    for (uint64_t name_off : fi::name_candidates) {
      for (uint64_t offset_off : fi::offset_candidates) {
        for (uint64_t stride : fi::stride_candidates) {
          int score = 0;
          for (uint32_t i = 0; i < n; ++i) {
            uint64_t entry = fields + static_cast<uint64_t>(i) * stride;
            uint64_t name_ptr = rd64(entry + name_off);
            if (!ptr_ok(name_ptr))
              break;
            std::string nm = read_cstr(name_ptr);
            if (!ident_like(nm))
              break;
            int32_t off = static_cast<int32_t>(rd32(entry + offset_off));
            if (off < 0 || off > 0x100000)
              break;
            if (i == 0 && off != 0x00 && off != ofset::object::first_field)
              break;
            ++score;
          }
          if (score > best_score) {
            best_score = score;
            best.fields_off = fields_off;
            best.name_off = name_off;
            best.offset_off = offset_off;
            best.stride = stride;
            best.detected = true;
          }
        }
      }
    }
    if (best_score >= static_cast<int>(n))
      break;
  }

  if (best_score >= 2) {
    g_field_layout = best;
    return true;
  }
  return false;
}

struct FieldView {
  std::string name;
  uint64_t type_b = 0; // +0x18 (по твоему layout)
  uint64_t type_a = 0; // +0x10 (parent в каноне, но проверяем оба)
  int32_t offset = 0;
  uint32_t token = 0;
};

bool read_field(const ClassInfo &ci, uint32_t index, uint64_t stride,
                FieldView &out) {
  uint64_t use_stride = g_field_layout.detected ? g_field_layout.stride : stride;
  uint64_t name_off = g_field_layout.detected ? g_field_layout.name_off : fi::name;
  uint64_t offset_off =
      g_field_layout.detected ? g_field_layout.offset_off : fi::offset;
  uint64_t token_off =
      g_field_layout.detected ? g_field_layout.token_off : fi::token;

  if (use_stride > 0x40)
    return false;
  uint64_t base = ci.fields + static_cast<uint64_t>(index) * use_stride;
  uint8_t raw[0x40] = {};
  if (!read_raw(base, raw, use_stride))
    return false;

  uint64_t name_ptr = 0;
  memcpy(&name_ptr, raw + name_off, 8);
  memcpy(&out.type_a, raw + fi::parent, 8);
  memcpy(&out.type_b, raw + fi::type, 8);
  memcpy(&out.offset, raw + offset_off, 4);
  memcpy(&out.token, raw + token_off, 4);

  out.name = read_cstr(name_ptr);
  if (!ident_like(out.name))
    return false;
  return true;
}

uint64_t field_class_ptr(const ClassInfo &ci, const FieldView &f) {
  const uint64_t cands[2] = {f.type_b, f.type_a};
  for (uint64_t p : cands) {
    if (!ptr_ok(p) || p == ci.addr)
      continue;

    if (class_plausible(p))
      return p;

    uint8_t raw[0x20];
    if (!read_raw(p, raw, sizeof(raw)))
      continue;

    uint64_t best = 0;
    for (size_t off = 0; off + 8 <= sizeof(raw); off += 8) {
      uint64_t cand = 0;
      memcpy(&cand, raw + off, 8);
      if (cand == ci.addr || !class_plausible(cand))
        continue;
      if (!best || klass_name(best) == "System.Object")
        best = cand;
    }
    if (best)
      return best;
  }
  return 0;
}

std::string field_type_label(const ClassInfo &ci, const FieldView &f) {
  if (uint64_t k = field_class_ptr(ci, f))
    return klass_name(k);

  const uint64_t cands[2] = {f.type_b, f.type_a};
  for (uint64_t p : cands) {
    if (!ptr_ok(p) || p == ci.addr)
      continue;
    uint8_t te = rd8(p + ofset::il2cpp_type::type_enum);
    if (te != 0 && te <= 0x1E)
      return type_enum_name(te);
  }
  return "?";
}

uint64_t detect_field_stride(const ClassInfo &ci) {
  if (g_field_layout.detected)
    return g_field_layout.stride;
  if (ci.field_count < 2)
    return fi::stride;

  for (uint64_t stride : fi::stride_candidates) {
    uint8_t raw[0x40] = {};
    if (!read_raw(ci.fields + stride, raw, 0x40))
      continue;

    uint64_t name_ptr = 0, type_ptr = 0;
    memcpy(&name_ptr, raw + fi::name, 8);
    memcpy(&type_ptr, raw + fi::type, 8);

    if (ident_like(read_cstr(name_ptr)) && ptr_ok(type_ptr))
      return stride;
  }
  return fi::stride;
}

void print_fields(const ClassInfo &ci, uint64_t stride, int depth) {
  if (!ptr_ok(ci.fields)) {
    printf("%sfields: <null>\n", ind(depth));
    return;
  }

  uint32_t limit = ci.field_count;
  if (limit == 0 || limit > 128)
    limit = 64;

  printf("%sfields @0x%llx (field_count=%u, stride=0x%llX)\n", ind(depth),
         static_cast<unsigned long long>(ci.fields), ci.field_count,
         static_cast<unsigned long long>(stride));

  for (uint32_t i = 0; i < limit; ++i) {
    FieldView f;
    if (!read_field(ci, i, stride, f))
      break;
    std::string label = field_type_label(ci, f);
    printf("%s  [%02u] %-30s off=0x%08X  %s\n", ind(depth), i, f.name.c_str(),
           static_cast<uint32_t>(f.offset), label.c_str());
    if (label == "?") {
      printf("%s       raw +0x10=0x%llx +0x18=0x%llx | klass(+0x10)=%s "
             "klass(+0x18)=%s\n",
             ind(depth), static_cast<unsigned long long>(f.type_a),
             static_cast<unsigned long long>(f.type_b),
             klass_name(f.type_a).c_str(), klass_name(f.type_b).c_str());
    }
  }
}

void print_methods(const ClassInfo &ci, uint64_t base, int depth) {
  if (!ptr_ok(ci.methods)) {
    printf("%smethods: <null>\n", ind(depth));
    return;
  }

  uint32_t limit = ci.method_count;
  if (limit == 0 || limit > 512)
    limit = 64;

  printf("%smethods @0x%llx (method_count=%u)\n", ind(depth),
         static_cast<unsigned long long>(ci.methods), ci.method_count);

  for (uint32_t i = 0; i < limit; ++i) {
    uint64_t mi_ptr = rd64(ci.methods + static_cast<uint64_t>(i) * 8);
    if (!ptr_ok(mi_ptr))
      continue;

    uint8_t raw[mi::size];
    if (!read_raw(mi_ptr, raw, sizeof(raw)))
      continue;

    uint64_t name_ptr = 0, code = 0;
    uint32_t token = 0;
    memcpy(&name_ptr, raw + mi::name, 8);
    memcpy(&code, raw + mi::method_pointer, 8);
    memcpy(&token, raw + mi::token, 4);

    std::string name = read_cstr(name_ptr);
    if (!ident_like(name))
      name = "<unknown>";

    uint8_t params = raw[mi::params_count];

    printf("%s  [%02u] %-42s ptr=0x%llx%s tok=0x%08X params=%u\n", ind(depth),
           i, name.c_str(), static_cast<unsigned long long>(code),
           module_rva(code, base).c_str(), token, params);
  }
}

void print_object_head(uint64_t obj, uint64_t base, int depth) {
  if (!ptr_ok(obj))
    return;

  uint64_t obj_klass = rd64(obj);
  std::string kn = klass_name(obj_klass);
  printf("%s      object @0x%llx klass=%s\n", ind(depth),
         static_cast<unsigned long long>(obj), kn.c_str());

  if (kn == "System.String") {
    int32_t len = static_cast<int32_t>(rd32(obj + ofset::string_object::length));
    if (len > 0 && len < 200) {
      std::string s;
      for (int32_t i = 0; i < len; ++i) {
        uint16_t c =
            rd16(obj + ofset::string_object::chars +
                 static_cast<uint64_t>(i) * 2);
        s.push_back(c >= 0x20 && c < 0x7F ? static_cast<char>(c) : '.');
      }
      printf("%s        string[%d] \"%s\"\n", ind(depth), len, s.c_str());
    }
    return;
  }

  if (klass_is_byte_array(obj_klass)) {
    uint32_t len = rd32(obj + ofset::array_object::length);
    if (len > 0 && len <= 256) {
      std::string hex, ascii;
      char b[8];
      for (uint32_t i = 0; i < len && i < 48; ++i) {
        uint8_t v = rd8(obj + ofset::array_object::data + i);
        snprintf(b, sizeof(b), "%02X ", v);
        hex += b;
        ascii.push_back(v >= 0x20 && v < 0x7F ? static_cast<char>(v) : '.');
      }
      printf("%s        byte[%u] %s |%s|\n", ind(depth), len, hex.c_str(),
             ascii.c_str());
    }
    return;
  }

  bool is_delegate = kn.find("Func") != std::string::npos ||
                     kn.find("Action") != std::string::npos ||
                     kn.find("Delegate") != std::string::npos;

  if (kn.find("MessageParser") == std::string::npos && !is_delegate)
    return;

  uint64_t method_ptr = 0;
  uint64_t target = 0;

  if (!is_delegate) {
    uint64_t factory = rd64(obj + ofset::message_parser::factory);
    if (!ptr_ok(factory))
      return;

    printf("%s        +0x10 factory=0x%llx (%s)\n", ind(depth),
           static_cast<unsigned long long>(factory),
           klass_name(rd64(factory)).c_str());

    method_ptr = rd64(factory + ofset::delegate_object::method_ptr);
    target = rd64(factory + ofset::delegate_object::target);
  } else {
    method_ptr = rd64(obj + ofset::delegate_object::method_ptr);
    target = rd64(obj + ofset::delegate_object::target);
  }

  printf("%s          method_ptr=0x%llx%s target=0x%llx\n", ind(depth),
         static_cast<unsigned long long>(method_ptr),
         module_rva(method_ptr, base).c_str(),
         static_cast<unsigned long long>(target));
}

bool interesting_static(const std::string &name) {
  std::string lower;
  lower.reserve(name.size());
  for (char c : name)
    lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  return lower.find("parser") != std::string::npos ||
         lower.find("descriptor") != std::string::npos ||
         lower.find("instance") != std::string::npos;
}

void print_statics(const ClassInfo &ci, uint64_t stride, uint64_t base,
                   int depth) {
  if (!ptr_ok(ci.statics)) {
    printf("%sstatics @+0x40: <null>\n", ind(depth));
    return;
  }

  printf("%sstatics @0x%llx\n", ind(depth),
         static_cast<unsigned long long>(ci.statics));

  for (uint32_t off = 0; off < kl::static_fields_dump_size; off += 8)
    printf("%s  +0x%03X: 0x%016llx\n", ind(depth), off,
           static_cast<unsigned long long>(rd64(ci.statics + off)));

  uint32_t limit = ci.field_count;
  if (limit == 0 || limit > 128)
    limit = 64;

  for (uint32_t i = 0; i < limit; ++i) {
    FieldView f;
    if (!read_field(ci, i, stride, f))
      break;
    if (f.offset < 0 ||
        static_cast<uint32_t>(f.offset) + 8 > kl::static_fields_dump_size)
      continue;
    if (f.offset != 0 && !interesting_static(f.name))
      continue;

    uint64_t value = rd64(ci.statics + f.offset);
    if (!ptr_ok(value))
      continue;

    printf("%s  field %-24s type=%-26s @+0x%03X = 0x%llx\n", ind(depth),
           f.name.c_str(), field_type_label(ci, f).c_str(), f.offset,
           static_cast<unsigned long long>(value));
    print_object_head(value, base, depth);
  }
}

void print_class(const ClassInfo &ci_in, uint64_t base, int depth) {
  ClassInfo ci = ci_in;
  detect_field_layout(ci);
  if (g_field_layout.detected)
    ci.fields = rd64(ci.addr + g_field_layout.fields_off);
  uint64_t stride = detect_field_stride(ci);

  printf("%sclass @0x%llx\n", ind(depth),
         static_cast<unsigned long long>(ci.addr));
  printf("%s  name      : %s\n", ind(depth), read_cstr(ci.name).c_str());
  printf("%s  namespace : %s\n", ind(depth), read_cstr(ci.ns).c_str());

  if (ptr_ok(ci.image)) {
    std::string iname = read_cstr(rd64(ci.image));
    if (ident_like(iname))
      printf("%s  image     : %s\n", ind(depth), iname.c_str());
    else
      printf("%s  image     : 0x%llx (name layout differs)\n", ind(depth),
             static_cast<unsigned long long>(ci.image));
  }

  printf("%s  parent    : %s\n", ind(depth), klass_name(ci.parent).c_str());

  uint64_t declaring = rd64(ci.addr + kl::declaring_type);
  if (ptr_ok(declaring))
    printf("%s  declaring : %s\n", ind(depth), klass_name(declaring).c_str());
  if (ptr_ok(ci.element))
    printf("%s  element   : %s\n", ind(depth), klass_name(ci.element).c_str());
  if (ptr_ok(ci.generic))
    printf("%s  generic   : 0x%llx\n", ind(depth),
           static_cast<unsigned long long>(ci.generic));

  printf("%s  handle    : 0x%llx   flags=0x%08X\n", ind(depth),
         static_cast<unsigned long long>(ci.metadata_handle), ci.flags);
  printf("%s  instance  : 0x%X\n", ind(depth), ci.instance_size);
  printf("%s  fields    : 0x%llx (count=%u)\n", ind(depth),
         static_cast<unsigned long long>(ci.fields), ci.field_count);
  printf("%s  methods   : 0x%llx (count=%u)\n", ind(depth),
         static_cast<unsigned long long>(ci.methods), ci.method_count);

  print_fields(ci, stride, depth);

  if (depth == 0) {
    print_statics(ci, stride, base, depth);
    print_methods(ci, base, depth);
  }

  if (depth >= 2)
    return;

  uint32_t limit = ci.field_count;
  if (limit == 0 || limit > 128)
    limit = 64;

  for (uint32_t i = 0; i < limit; ++i) {
    FieldView f;
    if (!read_field(ci, i, stride, f))
      break;
    if (f.name == "_unknownFields" || f.name == "_parser")
      continue;

    uint64_t nested = field_class_ptr(ci, f);
    if (!ptr_ok(nested) || nested == ci.addr)
      continue;

    ClassInfo nested_ci;
    if (!load_class(nested, nested_ci))
      continue;

    printf("%s--- nested message for field '%s' ---\n", ind(depth),
           f.name.c_str());
    print_class(nested_ci, base, depth + 1);
  }
}

void print_string_obj(uint64_t s, int depth) {
  int32_t len = static_cast<int32_t>(rd32(s + ofset::string_object::length));
  if (len < 0 || len > 8192)
    return;

  std::string out;
  out.reserve(static_cast<size_t>(len));
  for (int32_t i = 0; i < len; ++i) {
    uint16_t c =
        rd16(s + ofset::string_object::chars + static_cast<uint64_t>(i) * 2);
    out.push_back(c >= 0x20 && c < 0x7F ? static_cast<char>(c) : '.');
  }

  if (out.size() <= 120) {
    printf("%s    string[%d] \"%s\"\n", ind(depth), len, out.c_str());
    return;
  }

  printf("%s    string[%d]:\n", ind(depth), len);
  for (size_t pos = 0; pos < out.size(); pos += 120) {
    printf("%s      \"%s\"\n", ind(depth),
           out.substr(pos, 120).c_str());
  }
}

void print_bytestring_obj(uint64_t bs, int depth) {
  uint64_t arr = rd64(bs + ofset::byte_string::data);
  int32_t index = static_cast<int32_t>(rd32(bs + ofset::byte_string::index));
  int32_t len = static_cast<int32_t>(rd32(bs + ofset::byte_string::length));

  if (len <= 0) {
    printf("%s    bytestring[0] (empty)\n", ind(depth));
    return;
  }
  if (!ptr_ok(arr) || index < 0 || len > 0x100000) {
    printf("%s    bytestring[%d] (unreadable)\n", ind(depth), len);
    return;
  }

  int32_t arr_len = static_cast<int32_t>(rd32(arr + ofset::array_object::length));
  if (arr_len < 0 || index + len > arr_len) {
    printf("%s    bytestring[%d] (bad array len=%d)\n", ind(depth), len,
           arr_len);
    return;
  }

  printf("%s    bytestring[%d] ", ind(depth), len);
  for (int32_t i = 0; i < len && i < 48; ++i)
    printf("%02X", rd8(arr + ofset::array_object::data + index + i));
  if (len > 48)
    printf("...");
  printf("\n");
}

void print_repeated_obj(uint64_t rf, int depth) {
  for (uint64_t off : ofset::repeated_field::array_candidates) {
    uint64_t arr = rd64(rf + off);
    if (!ptr_ok(arr))
      continue;

    if (!klass_is_array(rd64(arr)))
      continue;

    int32_t count =
        static_cast<int32_t>(rd32(rf + off + ofset::repeated_field::count_delta));
    int32_t arr_len =
        static_cast<int32_t>(rd32(arr + ofset::array_object::length));
    if (count < 0 || count > arr_len || count > 4096)
      continue;

    printf("%s    repeated[%d]:\n", ind(depth), count);
    for (int32_t i = 0; i < count && i < 64; ++i) {
      uint64_t v =
          rd64(arr + ofset::array_object::data + static_cast<uint64_t>(i) * 8);
      if (!ptr_ok(v)) {
        printf("%s      [%d] 0x%llx\n", ind(depth), i,
               static_cast<unsigned long long>(v));
        continue;
      }
      std::string vn = klass_name(rd64(v));
      printf("%s      [%d] %s\n", ind(depth), i, vn.c_str());
      if (vn == "System.String")
        print_string_obj(v, depth + 1);
    }
    return;
  }
}

void print_bytes_obj(uint64_t b, int depth) {
  uint32_t len = rd32(b + ofset::array_object::length);
  if (len == 0 || len > 0x100000)
    return;

  printf("%s    bytes[%u] ", ind(depth), len);
  for (uint32_t i = 0; i < len && i < 32; ++i)
    printf("%02X", rd8(b + ofset::array_object::data + i));
  if (len > 32)
    printf("...");
  printf("\n");
}

void dump_live_object(uint64_t obj, int depth) {
  if (!ptr_ok(obj))
    return;

  uint64_t k = rd64(obj);
  if (!class_plausible(k)) {
    printf("%s[0x%llx] not a managed object\n", ind(depth),
           static_cast<unsigned long long>(obj));
    return;
  }

  ClassInfo ci;
  if (!load_class(k, ci))
    return;

  std::string kn = klass_name(k);
  printf("%sobject %s @0x%llx (size=0x%X)\n", ind(depth), kn.c_str(),
         static_cast<unsigned long long>(obj), ci.instance_size);

  uint64_t stride = detect_field_stride(ci);
  uint32_t limit = ci.field_count;
  if (limit == 0 || limit > 64)
    limit = 64;

  for (uint32_t i = 0; i < limit; ++i) {
    FieldView f;
    if (!read_field(ci, i, stride, f))
      break;
    if (f.offset < static_cast<int32_t>(ofset::object::first_field))
      continue;
    if (f.name.find("codec") != std::string::npos)
      continue;
    if (static_cast<uint32_t>(f.offset) + 8 > ci.instance_size)
      continue;

    uint64_t val = rd64(obj + f.offset);
    printf("%s  +0x%02X %-24s = 0x%llx", ind(depth),
           static_cast<uint32_t>(f.offset), f.name.c_str(),
           static_cast<unsigned long long>(val));

    if (!ptr_ok(val)) {
      printf("\n");
      continue;
    }

    uint64_t vk = rd64(val);
    std::string vkn = klass_name(vk);
    printf("  %s\n", vkn.c_str());

    if (vkn == "System.String") {
      print_string_obj(val, depth + 1);
      continue;
    }
    if (klass_is_byte_array(vk)) {
      print_bytes_obj(val, depth + 1);
      continue;
    }
    if (vkn == "Google.Protobuf.ByteString") {
      print_bytestring_obj(val, depth + 1);
      continue;
    }
    if (vkn.find("RepeatedField") != std::string::npos) {
      print_repeated_obj(val, depth + 1);
      continue;
    }

    bool skip = vkn == "<bad>" || vkn.rfind("System.", 0) == 0 ||
                vkn.find("MapField") != std::string::npos ||
                vkn.find("Google.Protobuf.Reflection") != std::string::npos;
    if (depth < 2 && !skip)
      dump_live_object(val, depth + 1);
  }
}


uint64_t try_rva(uint64_t base, uint64_t rva, const std::string &name,
                 const std::string &ns) {
  const uint64_t slot = base + rva;
  uint64_t klass = rd64(slot);

  printf("[try] base=0x%llx + 0x%llX -> slot=0x%llx, value=0x%llx",
         static_cast<unsigned long long>(base),
         static_cast<unsigned long long>(rva),
         static_cast<unsigned long long>(slot),
         static_cast<unsigned long long>(klass));
  if (!ptr_ok(klass)) {
    printf(" (empty)\n");
    return 0;
  }

  if (validate_class(klass, name, ns)) {
    printf(" (OK: %s)\n", klass_name(klass).c_str());
    return klass;
  }

  printf(" (name=%s)\n", klass_name(klass).c_str());
  return 0;
}

std::string utf16_to_utf8(const std::vector<uint16_t> &w) {
  std::string out;
  out.reserve(w.size());
  for (size_t i = 0; i < w.size(); ++i) {
    uint32_t cp = w[i];
    if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < w.size()) {
      uint32_t lo = w[i + 1];
      if (lo >= 0xDC00 && lo <= 0xDFFF) {
        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
        ++i;
      }
    }
    if (cp < 0x80) {
      out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
      out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }
  return out;
}

std::string read_string_obj(uint64_t s) {
  if (!ptr_ok(s))
    return "";
  int32_t len = static_cast<int32_t>(rd32(s + ofset::string_object::length));
  if (len <= 0 || len > 0x100000)
    return "";

  std::vector<uint16_t> w(static_cast<size_t>(len));
  for (int32_t i = 0; i < len; ++i)
    w[static_cast<size_t>(i)] =
        rd16(s + ofset::string_object::chars + static_cast<uint64_t>(i) * 2);
  return utf16_to_utf8(w);
}

bool get_field_off(uint64_t obj, const char *fname, uint64_t *off) {
  if (!ptr_ok(obj))
    return false;
  uint64_t k = rd64(obj);
  if (!class_plausible(k))
    return false;

  ClassInfo ci;
  if (!load_class(k, ci))
    return false;

  detect_field_layout(ci);
  if (g_field_layout.detected)
    ci.fields = rd64(ci.addr + g_field_layout.fields_off);

  uint64_t stride = detect_field_stride(ci);
  uint32_t limit = ci.field_count;
  if (limit == 0 || limit > 128)
    limit = 64;

  for (uint32_t i = 0; i < limit; ++i) {
    FieldView f;
    if (!read_field(ci, i, stride, f))
      break;
    if (f.name == fname) {
      *off = f.offset;
      return true;
    }
  }
  return false;
}

std::string read_string_field(uint64_t obj, const char *fname) {
  uint64_t off = 0;
  if (!get_field_off(obj, fname, &off))
    return "";
  uint64_t v = rd64(obj + off);
  if (!ptr_ok(v))
    return "";
  if (klass_name(rd64(v)) != "System.String")
    return "";
  return read_string_obj(v);
}

std::string read_string_at(uint64_t obj, uint64_t off) {
  uint64_t v = rd64(obj + off);
  if (!ptr_ok(v))
    return "";
  if (klass_name(rd64(v)) != "System.String")
    return "";
  return read_string_obj(v);
}

bool read_bytestring(uint64_t bs, std::string *out) {
  out->clear();
  if (!ptr_ok(bs))
    return false;

  uint64_t arr = rd64(bs + ofset::byte_string::data);
  int32_t index = static_cast<int32_t>(rd32(bs + ofset::byte_string::index));
  int32_t len = static_cast<int32_t>(rd32(bs + ofset::byte_string::length));
  if (len <= 0)
    return true;
  if (!ptr_ok(arr) || index < 0 || len > 0x1000000)
    return false;

  int32_t arr_len = static_cast<int32_t>(rd32(arr + ofset::array_object::length));
  if (index + len > arr_len)
    return false;

  out->resize(static_cast<size_t>(len));
  for (int32_t i = 0; i < len; ++i)
    (*out)[static_cast<size_t>(i)] =
        static_cast<char>(rd8(arr + ofset::array_object::data + index + i));
  return true;
}

std::string base64_encode(const std::string &in) {
  static const char tbl[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(((in.size() + 2) / 3) * 4);

  size_t i = 0;
  while (i + 3 <= in.size()) {
    uint32_t v = (static_cast<unsigned char>(in[i]) << 16) |
                 (static_cast<unsigned char>(in[i + 1]) << 8) |
                 static_cast<unsigned char>(in[i + 2]);
    out.push_back(tbl[(v >> 18) & 63]);
    out.push_back(tbl[(v >> 12) & 63]);
    out.push_back(tbl[(v >> 6) & 63]);
    out.push_back(tbl[v & 63]);
    i += 3;
  }

  size_t rem = in.size() - i;
  if (rem == 1) {
    uint32_t v = static_cast<unsigned char>(in[i]) << 16;
    out.push_back(tbl[(v >> 18) & 63]);
    out.push_back(tbl[(v >> 12) & 63]);
    out += "==";
  } else if (rem == 2) {
    uint32_t v = (static_cast<unsigned char>(in[i]) << 16) |
                 (static_cast<unsigned char>(in[i + 1]) << 8);
    out.push_back(tbl[(v >> 18) & 63]);
    out.push_back(tbl[(v >> 12) & 63]);
    out.push_back(tbl[(v >> 6) & 63]);
    out.push_back('=');
  }
  return out;
}

std::string json_escape(const std::string &s) {
  std::string o;
  o.reserve(s.size() + 8);
  for (unsigned char c : s) {
    switch (c) {
    case '"':
      o += "\\\"";
      break;
    case '\\':
      o += "\\\\";
      break;
    case '\b':
      o += "\\b";
      break;
    case '\f':
      o += "\\f";
      break;
    case '\n':
      o += "\\n";
      break;
    case '\r':
      o += "\\r";
      break;
    case '\t':
      o += "\\t";
      break;
    default:
      if (c < 0x20) {
        char b[8];
        snprintf(b, sizeof(b), "\\u%04x", c);
        o += b;
      } else {
        o.push_back(static_cast<char>(c));
      }
    }
  }
  return o;
}

bool read_repeated_strings(uint64_t rf, std::vector<std::string> *out) {
  out->clear();
  if (!ptr_ok(rf))
    return false;

  for (uint64_t off : ofset::repeated_field::array_candidates) {
    uint64_t arr = rd64(rf + off);
    if (!ptr_ok(arr))
      continue;

    if (!klass_is_array(rd64(arr)))
      continue;

    int32_t count =
        static_cast<int32_t>(rd32(rf + off + ofset::repeated_field::count_delta));
    int32_t arr_len =
        static_cast<int32_t>(rd32(arr + ofset::array_object::length));
    if (count < 0 || count > arr_len || count > 4096)
      continue;

    for (int32_t i = 0; i < count; ++i) {
      uint64_t v =
          rd64(arr + ofset::array_object::data + static_cast<uint64_t>(i) * 8);
      if (ptr_ok(v) && klass_name(rd64(v)) == "System.String")
        out->push_back(read_string_obj(v));
      else
        out->push_back("");
    }
    return true;
  }
  return false;
}

bool read_map_string_string(
    uint64_t mf, std::vector<std::pair<std::string, std::string>> *out) {
  out->clear();
  if (!ptr_ok(mf))
    return false;

  uint64_t list = 0;
  for (uint64_t off : ofset::map_field::list_candidates) {
    uint64_t v = rd64(mf + off);
    if (!ptr_ok(v))
      continue;
    std::string n = klass_name(rd64(v));
    if (n.find("LinkedList`1") != std::string::npos) {
      list = v;
      break;
    }
  }
  if (!list)
    return false;

  uint64_t head = rd64(list + ofset::linked_list::head);
  int32_t count = static_cast<int32_t>(rd32(list + ofset::linked_list::count));
  if (!ptr_ok(head) || count < 0 || count > 100000)
    return false;

  uint64_t node = head;
  for (int32_t i = 0; i < count; ++i) {
    if (!ptr_ok(node))
      break;

    uint64_t k = rd64(node + ofset::linked_list::node_key);
    uint64_t v = rd64(node + ofset::linked_list::node_value);
    std::string ks, vs;
    if (ptr_ok(k) && klass_name(rd64(k)) == "System.String")
      ks = read_string_obj(k);
    if (ptr_ok(v) && klass_name(rd64(v)) == "System.String")
      vs = read_string_obj(v);
    if (!ks.empty())
      out->push_back({ks, vs});

    node = rd64(node + ofset::linked_list::node_next);
    if (node == head)
      break;
  }
  return true;
}

bool read_dictionary_string_string(
    uint64_t dict, std::vector<std::pair<std::string, std::string>> *out) {
  out->clear();
  if (!ptr_ok(dict))
    return false;

  uint64_t entries = rd64(dict + ofset::dictionary::entries);
  int32_t count =
      static_cast<int32_t>(rd32(dict + ofset::dictionary::count));
  if (!ptr_ok(entries) || count < 0 || count > 100000)
    return false;

  int32_t arr_len =
      static_cast<int32_t>(rd32(entries + ofset::array_object::length));
  if (count > arr_len)
    count = arr_len;

  for (uint32_t stride : ofset::dictionary::entry_strides) {
    std::vector<std::pair<std::string, std::string>> tmp;
    for (int32_t i = 0; i < count; ++i) {
      uint64_t e = entries + ofset::array_object::data +
                   static_cast<uint64_t>(i) * stride;
      int32_t hash =
          static_cast<int32_t>(rd32(e + ofset::dictionary::entry_hash));
      if (hash < 0)
        continue;
      uint64_t k = rd64(e + ofset::dictionary::entry_key);
      uint64_t v = rd64(e + ofset::dictionary::entry_value);
      std::string ks, vs;
      if (ptr_ok(k) && klass_name(rd64(k)) == "System.String")
        ks = read_string_obj(k);
      if (ptr_ok(v) && klass_name(rd64(v)) == "System.String")
        vs = read_string_obj(v);
      if (!ks.empty())
        tmp.push_back({ks, vs});
    }
    if (!tmp.empty()) {
      *out = std::move(tmp);
      return true;
    }
  }
  return false;
}

bool read_string_array(uint64_t arr, std::vector<std::string> *out) {
  out->clear();
  if (!ptr_ok(arr))
    return false;
  int32_t len = static_cast<int32_t>(rd32(arr + ofset::array_object::length));
  if (len < 0 || len > 100000)
    return false;
  for (int32_t i = 0; i < len; ++i) {
    uint64_t v =
        rd64(arr + ofset::array_object::data + static_cast<uint64_t>(i) * 8);
    if (ptr_ok(v) && klass_name(rd64(v)) == "System.String")
      out->push_back(read_string_obj(v));
    else
      out->push_back("");
  }
  return true;
}

bool read_byte_array(uint64_t arr, std::string *out) {
  out->clear();
  if (!ptr_ok(arr))
    return false;
  if (!klass_is_byte_array(rd64(arr)))
    return false;

  uint64_t len64 = 0;
  if (!read_raw(arr + ofset::array_object::length, &len64, sizeof(len64)))
    return false;
  if (len64 > 0x1000000ull)
    return false;
  if (len64 == 0)
    return true;

  size_t len = static_cast<size_t>(len64);
  out->resize(len);
  if (!read_raw(arr + ofset::array_object::data, out->data(), len)) {
    out->clear();
    return false;
  }
  return true;
}

uint64_t g_playerapi_ver_off = ofset::bolt_player_api::verification;

constexpr int kEnvWaitSeconds = 30;
constexpr int kIntegrityWaitSeconds = 60;

constexpr int kIntegrityGraceSeconds = 10;

uint64_t playerapi_instance(uint64_t api_klass) {
  if (!class_plausible(api_klass))
    return 0;
  uint64_t statics = rd64(api_klass + kl::static_fields);
  if (!ptr_ok(statics))
    return 0;
  uint64_t inst = rd64(statics + ofset::bolt_static_fields::instance);
  if (!ptr_ok(inst))
    return 0;
  if (klass_name(rd64(inst)) != ofset::class_name::bolt_player_api_full)
    return 0;
  return inst;
}

uint64_t playerapi_verification(uint64_t api_klass) {
  uint64_t inst = playerapi_instance(api_klass);
  if (!inst)
    return 0;

  uint64_t ver = rd64(inst + g_playerapi_ver_off);
  if (ptr_ok(ver) &&
      klass_name(rd64(ver)) == ofset::class_name::verification_full)
    return ver;

  uint64_t named = 0;
  if (get_field_off(inst, "_verification", &named) &&
      named >= ofset::object::first_field && named <= 0x200 &&
      named != g_playerapi_ver_off) {
    ver = rd64(inst + named);
    if (ptr_ok(ver) &&
        klass_name(rd64(ver)) == ofset::class_name::verification_full) {
      g_playerapi_ver_off = named;
      return ver;
    }
  }
  return 0;
}

uint64_t playerapi_device(uint64_t api_klass) {
  uint64_t inst = playerapi_instance(api_klass);
  if (!inst)
    return 0;

  uint64_t dev = rd64(inst + ofset::bolt_player_api::device_info);
  if (!ptr_ok(dev))
    return 0;

  std::string cn = klass_name(rd64(dev));
  if (cn == ofset::class_name::device_info_full ||
      cn == ofset::class_name::device_info_proto_full)
    return dev;
  return 0;
}

uint64_t playerapi_credential(uint64_t api_klass) {
  uint64_t inst = playerapi_instance(api_klass);
  if (!inst)
    return 0;
  return rd64(inst + ofset::bolt_player_api::credential);
}

bool playerapi_authenticated(uint64_t api_klass) {
  uint64_t inst = playerapi_instance(api_klass);
  if (!inst)
    return false;
  uint64_t state = rd64(inst + ofset::bolt_player_api::state);
  if (!ptr_ok(state))
    return false;
  if (klass_name(rd64(state)).find("PlayerApiState") == std::string::npos)
    return false;
  return rd8(state + ofset::player_api_state::authenticated) != 0;
}

bool verification_env_bytes(uint64_t ver, std::string *out) {
  out->clear();
  uint64_t env = rd64(ver + ofset::retained_verification::environment);
  if (!ptr_ok(env))
    return false;
  return read_byte_array(rd64(env + ofset::environment::byte_array), out);
}

void snapshot_set(std::vector<std::pair<std::string, std::string>> *snap,
                  const std::string &key, const std::string &value) {
  if (key.empty() || value.empty())
    return;
  for (auto &kv : *snap)
    if (kv.first == key) {
      kv.second = value;
      return;
    }
  snap->push_back({key, value});
}

bool verification_integrity_token(uint64_t ver, std::string *token) {
  token->clear();
  std::vector<std::pair<std::string, std::string>> snap;
  if (!read_dictionary_string_string(
          rd64(ver + ofset::retained_verification::app_snapshot), &snap))
    return false;
  for (const auto &kv : snap)
    if (kv.first == "android-playintegrity" && !kv.second.empty()) {
      *token = kv.second;
      return true;
    }
  return false;
}

bool appverification_integrity_token(uint64_t appv, std::string *token) {
  token->clear();
  std::vector<std::pair<std::string, std::string>> snap;
  if (!read_map_string_string(rd64(appv + ofset::app_verification::app_snapshot),
                              &snap))
    return false;
  for (const auto &kv : snap)
    if (kv.first == "android-playintegrity" && !kv.second.empty()) {
      *token = kv.second;
      return true;
    }
  return false;
}

bool appcheck_integrity_token(uint64_t tokens, std::string *token) {
  token->clear();
  std::vector<std::pair<std::string, std::string>> payload;
  if (!read_dictionary_string_string(
          rd64(tokens + ofset::app_check_tokens::payload), &payload))
    return false;
  for (const auto &kv : payload)
    if (kv.first == "android-playintegrity" && !kv.second.empty()) {
      *token = kv.second;
      return true;
    }
  return false;
}

bool merge_appverification_snapshot(
    uint64_t appv, std::vector<std::pair<std::string, std::string>> *snap,
    std::string *token) {
  std::vector<std::pair<std::string, std::string>> proto;
  if (!read_map_string_string(rd64(appv + ofset::app_verification::app_snapshot),
                              &proto))
    return false;
  bool found = false;
  for (const auto &kv : proto) {
    snapshot_set(snap, kv.first, kv.second);
    if (kv.first == "android-playintegrity" && !kv.second.empty()) {
      *token = kv.second;
      found = true;
    }
  }
  return found;
}

bool verification_ready(uint64_t ver, bool *has_integrity) {
  std::vector<std::pair<std::string, std::string>> snap;
  bool has_snap = read_dictionary_string_string(
      rd64(ver + ofset::retained_verification::app_snapshot), &snap);
  *has_integrity = false;
  for (const auto &kv : snap)
    if (kv.first == "android-playintegrity" && !kv.second.empty()) {
      *has_integrity = true;
      break;
    }
  if (*has_integrity)
    return true;

  std::string env;
  if (verification_env_bytes(ver, &env) && env.size() > 100)
    return true;
  if (has_snap && !snap.empty())
    return true;
  if (!read_string_at(ver, ofset::retained_verification::apk_hash).empty())
    return true;
  if (!read_string_at(ver, ofset::retained_verification::content_hash).empty())
    return true;
  return false;
}

uint64_t resolve_class_quiet(const std::vector<MapEntry> &maps,
                             uint64_t module_base, uint64_t rva,
                             const std::string &name, const std::string &ns) {
  std::vector<uint64_t> bases;
  for (const auto &m : maps) {
    if (m.offset == 0 && ends_with(m.path, "libunity.so") &&
        std::find(bases.begin(), bases.end(), m.start) == bases.end())
      bases.push_back(m.start);
  }

  for (uint64_t b : bases) {
    uint64_t k = rd64(b + rva);
    if (ptr_ok(k) && validate_class(k, name, ns))
      return k;
  }

  if (module_base &&
      std::find(bases.begin(), bases.end(), module_base) == bases.end()) {
    uint64_t k = rd64(module_base + rva);
    if (ptr_ok(k) && validate_class(k, name, ns))
      return k;
  }
  return 0;
}

std::vector<uint64_t> scan_heap_once(const std::vector<MapEntry> &maps,
                                     uint64_t klass, uint32_t inst_size,
                                     const std::string &name,
                                     size_t max_hits) {
  std::vector<uint64_t> hits;
  const size_t chunk = 1 << 20;
  std::vector<char> buf(chunk);

  auto plausible_obj = [](uint64_t v) {
    return ptr_ok(v) && class_plausible(rd64(v));
  };

  for (const auto &m : maps) {
    if (m.perms[0] != 'r' || m.perms[1] != 'w')
      continue;
    if (m.path.find("libunity") != std::string::npos)
      continue;

    uint64_t addr = m.start;
    while (addr < m.end && hits.size() < max_hits) {
      size_t n = static_cast<size_t>(
          std::min<uint64_t>(chunk, m.end - addr));
      if (!read_raw(addr, buf.data(), n)) {
        addr += n;
        continue;
      }

      for (size_t i = 0; i + 8 <= n && hits.size() < max_hits; i += 8) {
        uint64_t q = 0;
        memcpy(&q, buf.data() + i, 8);
        if (q != klass)
          continue;

        uint64_t obj = addr + i;
        if (obj + inst_size > m.end)
          continue;

        uint64_t monitor = rd64(obj + ofset::object::monitor);
        if (monitor != 0 && !ptr_ok(monitor))
          continue;

        if (name == ofset::class_name::facebook_auth_request) {
          uint64_t auth = rd64(obj + ofset::facebook_auth_request::auth_facebook);
          if (!ptr_ok(auth))
            continue;
          if (klass_name(rd64(auth)) != ofset::class_name::auth_facebook_full)
            continue;
        } else {
          uint64_t first = rd64(obj + ofset::object::first_field);
          if (first != 0 && !plausible_obj(first))
            continue;
          bool any = false;
          for (uint32_t off = ofset::object::first_field + 8;
               off + 8 <= inst_size; off += 8) {
            if (plausible_obj(rd64(obj + off))) {
              any = true;
              break;
            }
          }
          if (!any)
            continue;
        }

        hits.push_back(obj);
      }
      addr += n;
    }
    if (hits.size() >= max_hits)
      break;
  }
  return hits;
}

uint64_t probe_integrity(const std::vector<MapEntry> &maps, uint64_t ver,
                         uint64_t appcheck_klass, uint64_t appv_klass,
                         bool scan_appcheck, uint64_t *watch,
                         std::string *token) {
  if (ver && verification_integrity_token(ver, token))
    return ver;

  if (scan_appcheck && appcheck_klass) {
    uint32_t size = rd32(appcheck_klass + kl::instance_size);
    auto hits = scan_heap_once(maps, appcheck_klass, size,
                               ofset::class_name::app_check_tokens, 32);
    for (uint64_t h : hits)
      if (appcheck_integrity_token(h, token))
        return h;
  }

  if (appv_klass) {
    uint32_t size = rd32(appv_klass + kl::instance_size);
    auto hits = scan_heap_once(maps, appv_klass, size,
                               ofset::class_name::app_verification, 64);
    for (uint64_t h : hits) {
      if (appverification_integrity_token(h, token))
        return h;
      if (watch && !*watch) {
        uint64_t env = rd64(h + ofset::app_verification::environment);
        if (ptr_ok(env) &&
            ptr_ok(rd64(env + ofset::app_verification_environment::info)))
          *watch = h;
      }
    }
  }
  return 0;
}

} // namespace

void dump_typeinfo(int pid, uint64_t module_base, uint64_t rva,
                   const char *name_hint) {
  std::string expected = name_hint ? name_hint : "";
  std::string expected_ns;
  if (expected == ofset::class_name::facebook_auth_request)
    expected_ns = ofset::class_name::facebook_auth_request_namespace;

  std::vector<MapEntry> maps = load_maps(pid);

  std::vector<uint64_t> elf_bases;
  for (const auto &m : maps) {
    if (m.offset == 0 && ends_with(m.path, "libunity.so") &&
        std::find(elf_bases.begin(), elf_bases.end(), m.start) ==
            elf_bases.end())
      elf_bases.push_back(m.start);
  }

  printf("\n=== TypeInfo dump ===\n");
  printf("  pid          : %d\n", pid);
  printf("  module base  : 0x%llx (from process_manager)\n",
         static_cast<unsigned long long>(module_base));
  printf("  elf base(s)  :");
  if (elf_bases.empty())
    printf(" none");
  for (uint64_t b : elf_bases)
    printf(" 0x%llx", static_cast<unsigned long long>(b));
  printf("%s\n", elf_bases.empty() ? "" : " (mapping with offset 0)");
  printf("  rva          : 0x%llX\n", static_cast<unsigned long long>(rva));
  printf("  expected     : %s%s%s\n", expected_ns.empty() ? "" : expected_ns.c_str(),
         expected_ns.empty() ? "" : ".", expected.c_str());

  printf("  --- libunity mappings ---\n");
  int shown = 0;
  for (const auto &m : maps) {
    if (m.path.find("libunity") == std::string::npos)
      continue;
    printf("    0x%llx-0x%llx off=0x%llx %s\n",
           static_cast<unsigned long long>(m.start),
           static_cast<unsigned long long>(m.end),
           static_cast<unsigned long long>(m.offset), m.perms);
    if (++shown >= 12)
      break;
  }

  uint64_t klass = 0;
  uint64_t chosen_base = 0;

  for (uint64_t b : elf_bases) {
    uint64_t k = try_rva(b, rva, expected, expected_ns);
    if (k) {
      klass = k;
      chosen_base = b;
      break;
    }
  }
  if (!klass && module_base &&
      std::find(elf_bases.begin(), elf_bases.end(), module_base) ==
          elf_bases.end()) {
    uint64_t k = try_rva(module_base, rva, expected, expected_ns);
    if (k) {
      klass = k;
      chosen_base = module_base;
    }
  }

  if (!klass)
    klass = locate_class(maps, expected, expected_ns);

  if (!klass) {
    printf("  => class not resolved\n");
    printf("     если слот пустой — класс создаётся лениво: зайди в игру/\n");
    printf("     выполни логин и запусти дамп ещё раз\n");
    return;
  }

  ClassInfo ci;
  if (!load_class(klass, ci)) {
    printf("  => cannot read Il2CppClass at 0x%llx\n",
           static_cast<unsigned long long>(klass));
    return;
  }

  uint64_t display_base = chosen_base;
  if (!display_base) {
    for (uint32_t i = 0; i < ci.method_count && i < 64 && !display_base; ++i) {
      uint64_t mi = ptr_ok(ci.methods) ? rd64(ci.methods + i * 8) : 0;
      uint64_t code = ptr_ok(mi) ? rd64(mi + mi::method_pointer) : 0;
      for (uint64_t b : elf_bases) {
        if (code >= b && code - b < kModuleSpanMax) {
          display_base = b;
          break;
        }
      }
    }
    if (!display_base)
      display_base = elf_bases.empty() ? module_base : elf_bases.back();
  }
  printf("  used base    : 0x%llx\n",
         static_cast<unsigned long long>(display_base));

  std::vector<uint64_t> slots = find_module_refs(maps, klass);
  if (!slots.empty()) {
    printf("\n  possible TypeInfo slot(s) for this class:\n");
    for (uint64_t s : slots)
      printf("    0x%llx%s\n", static_cast<unsigned long long>(s),
             module_rva(s, display_base).c_str());
  }

  printf("\n  --- raw qwords of Il2CppClass (0x%llX) ---\n",
         static_cast<unsigned long long>(klass));
  for (size_t off = 0; off < kl::head_size; off += 0x20) {
    printf("  %03zX:", off);
    for (size_t i = 0; i < 0x20; i += 8)
      printf(" %016llx",
             static_cast<unsigned long long>(rd64(klass + off + i)));
    printf("\n");
  }

  printf("\n");
  print_class(ci, display_base, 0);
  printf("=== end of dump ===\n");
}

void scan_instances(int pid, uint64_t module_base, uint64_t rva,
                    const char *name_hint, bool watch) {
  std::string name = name_hint ? name_hint : "";
  std::string ns;
  if (name == ofset::class_name::facebook_auth_request)
    ns = ofset::class_name::facebook_auth_request_namespace;

  std::vector<MapEntry> maps = load_maps(pid);

  std::vector<uint64_t> bases;
  for (const auto &m : maps) {
    if (m.offset == 0 && ends_with(m.path, "libunity.so") &&
        std::find(bases.begin(), bases.end(), m.start) == bases.end())
      bases.push_back(m.start);
  }

  uint64_t klass = 0;
  for (uint64_t b : bases) {
    uint64_t k = try_rva(b, rva, name, ns);
    if (k) {
      klass = k;
      break;
    }
  }
  if (!klass && module_base &&
      std::find(bases.begin(), bases.end(), module_base) == bases.end())
    klass = try_rva(module_base, rva, name, ns);
  if (!klass)
    klass = locate_class(maps, name, ns);

  if (!klass) {
    printf("  => class not resolved\n");
    return;
  }

  uint32_t inst_size = rd32(klass + kl::instance_size);
  printf("\n=== live instances of %s (klass=0x%llx, size=0x%X) ===\n",
         klass_name(klass).c_str(), static_cast<unsigned long long>(klass),
         inst_size);
  if (watch)
    printf("  watch mode: ищу объект с заполненным authFacebook_ "
           "(Ctrl+C — стоп)\n");

  std::vector<uint64_t> hits;
  int iter = 0;
  for (;;) {
    hits = scan_heap_once(maps, klass, inst_size, name, 32);
    if (!hits.empty() || !watch)
      break;
    if ((iter++ % 10) == 0) {
      printf(".");
      fflush(stdout);
    }
    usleep(100000);
  }

  if (watch && !hits.empty())
    printf("\n");

  printf("found %zu live object(s)\n", hits.size());
  if (hits.empty()) {
    printf("  (нет живого FacebookAuthRequest с authFacebook_ — запусти\n");
    printf("   scan в момент нажатия логина через Facebook)\n");
    return;
  }

  for (size_t i = 0; i < hits.size() && i < 8; ++i) {
    printf("\n--- instance %zu @0x%llx ---\n", i,
           static_cast<unsigned long long>(hits[i]));
    dump_live_object(hits[i], 0);
  }
  printf("=== end ===\n");
}
struct TextBuilder {
  std::string text;

  void add(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(nullptr, 0, fmt, ap);
    va_end(ap);
    if (n > 0) {
      size_t old = text.size();
      text.resize(old + static_cast<size_t>(n) + 1);
      vsnprintf(&text[old], static_cast<size_t>(n) + 1, fmt, ap2);
      text.resize(old + static_cast<size_t>(n));
    }
    va_end(ap2);
  }
};

struct CaptureData {
  bool is_verification = true;
  uint64_t object = 0;
  uint64_t environment = 0;
  uint64_t info_array = 0;
  uint64_t info_length = 0;
  std::string info_klass;
  bool rooted = false;
  std::string apk_hash;
  std::string content_hash;
  std::string path;
  std::string token;
  std::vector<std::string> forbidden;
  std::vector<std::pair<std::string, std::string>> snapshot;
  int32_t env_version = 0;
  std::string env_bytes;
  uint64_t dev = 0;
  std::string credential;
  std::string game_id;
  std::string game_version;
  bool integrity = false;

  bool fb_present = false;
  std::string fb_game_id;
  std::string fb_game_version;
  std::string fb_locale;
  std::string fb_token;
  int32_t fb_platform = 0;
  int32_t fb_store = 0;
};

struct DeviceStrings {
  std::string device_id;
  std::string device_model;
  std::string ads_id;
  std::string vendor_id;
};

static void read_device_strings(uint64_t dev, DeviceStrings *out) {
  if (!ptr_ok(dev))
    return;

  std::string cn = klass_name(rd64(dev));
  if (cn == ofset::class_name::device_info_full) {
    out->device_id = read_string_at(dev, ofset::device_info::device_id);
    out->device_model = read_string_at(dev, ofset::device_info::device_model);
    out->ads_id = read_string_at(dev, ofset::device_info::ads_id);
    out->vendor_id = read_string_at(dev, ofset::device_info::vndr_id);
    return;
  }
  if (cn == ofset::class_name::device_info_proto_full) {
    out->device_id = read_string_at(dev, ofset::device_info_proto::device_id);
    out->device_model =
        read_string_at(dev, ofset::device_info_proto::device_model);
    out->ads_id = read_string_at(dev, ofset::device_info_proto::ads_id);
    out->vendor_id = read_string_at(dev, ofset::device_info_proto::vndr_id);
    return;
  }

  out->device_id = read_string_field(dev, "deviceId_");
  out->device_model = read_string_field(dev, "deviceModel_");
  out->ads_id = read_string_field(dev, "adsId_");
  out->vendor_id = read_string_field(dev, "vndrId_");
}

static void read_facebook(uint64_t authfb, uint64_t cred, CaptureData *d) {
  if (ptr_ok(cred) &&
      klass_name(rd64(cred)) == ofset::class_name::facebook_credential_full) {
    d->fb_present = true;
    d->fb_token = read_string_at(cred, ofset::facebook_credential::token);
  }

  if (!ptr_ok(authfb) ||
      klass_name(rd64(authfb)) != ofset::class_name::auth_facebook_full)
    return;

  d->fb_present = true;

  std::string gid = read_string_at(authfb, ofset::auth_facebook::game_id);
  std::string gver = read_string_at(authfb, ofset::auth_facebook::game_version);
  std::string tok = read_string_at(authfb, ofset::auth_facebook::token);
  std::string loc = read_string_at(authfb, ofset::auth_facebook::locale);
  if (!gid.empty())
    d->fb_game_id = gid;
  if (!gver.empty())
    d->fb_game_version = gver;
  if (!tok.empty())
    d->fb_token = tok;
  if (!loc.empty())
    d->fb_locale = loc;
  d->fb_platform =
      static_cast<int32_t>(rd32(authfb + ofset::auth_facebook::platform));
  d->fb_store = static_cast<int32_t>(rd32(authfb + ofset::auth_facebook::store));
}

static CaptureData collect_verification(uint64_t ver, uint64_t dev) {
  CaptureData d;
  d.is_verification = true;
  d.object = ver;
  d.dev = dev;

  d.rooted = rd8(ver + ofset::retained_verification::is_rooted) != 0;
  d.apk_hash = read_string_at(ver, ofset::retained_verification::apk_hash);
  d.content_hash =
      read_string_at(ver, ofset::retained_verification::content_hash);
  d.path = read_string_at(ver, ofset::retained_verification::path);

  read_string_array(rd64(ver + ofset::retained_verification::forbidden_apps),
                    &d.forbidden);
  read_dictionary_string_string(
      rd64(ver + ofset::retained_verification::app_snapshot), &d.snapshot);

  d.environment = rd64(ver + ofset::retained_verification::environment);
  if (ptr_ok(d.environment)) {
    d.env_version = static_cast<int32_t>(
        rd32(d.environment + ofset::environment::version));
    d.info_array = rd64(d.environment + ofset::environment::byte_array);
    if (ptr_ok(d.info_array)) {
      d.info_klass = klass_name_raw(rd64(d.info_array));
      uint64_t len = 0;
      if (read_raw(d.info_array + ofset::array_object::length, &len,
                   sizeof(len)))
        d.info_length = len;
    }
    read_byte_array(d.info_array, &d.env_bytes);
  }

  for (const auto &kv : d.snapshot)
    if (kv.first == "android-playintegrity" && !kv.second.empty())
      d.integrity = true;

  if (d.env_version != ofset::environment_version) {
    fprintf(stderr,
            "\njson: WARNING: environment version %d != expected %d "
            "(game build %u)\n",
            d.env_version, ofset::environment_version, ofset::build);
  }
  return d;
}

static CaptureData collect_appverification(uint64_t appv, uint64_t dev) {
  CaptureData d;
  d.is_verification = false;
  d.object = appv;
  d.dev = dev;

  d.rooted = rd8(appv + ofset::app_verification::is_rooted) != 0;
  d.apk_hash = read_string_at(appv, ofset::app_verification::apk_hash);
  d.content_hash = read_string_at(appv, ofset::app_verification::content_hash);
  d.path = read_string_at(appv, ofset::app_verification::path);
  d.token = read_string_at(appv, ofset::app_verification::token);

  read_repeated_strings(rd64(appv + ofset::app_verification::forbidden_apps),
                        &d.forbidden);
  read_map_string_string(rd64(appv + ofset::app_verification::app_snapshot),
                         &d.snapshot);

  d.environment = rd64(appv + ofset::app_verification::environment);
  if (ptr_ok(d.environment)) {
    d.env_version = static_cast<int32_t>(
        rd32(d.environment + ofset::app_verification_environment::version));
    uint64_t bs =
        rd64(d.environment + ofset::app_verification_environment::info);
    if (ptr_ok(bs)) {
      d.info_array = rd64(bs + ofset::byte_string::data);
      d.info_length =
          static_cast<uint32_t>(rd32(bs + ofset::byte_string::length));
      d.info_klass = klass_name_raw(rd64(d.info_array));
    }
    read_bytestring(bs, &d.env_bytes);
  }

  for (const auto &kv : d.snapshot)
    if (kv.first == "android-playintegrity" && !kv.second.empty())
      d.integrity = true;
  return d;
}

static std::string render_json(const CaptureData &d, bool full) {
  TextBuilder o;
  std::string env_b64 = base64_encode(d.env_bytes);

  o.add("{\n");
  o.add("  \"version\": %d,\n", d.env_version);
  o.add("  \"environment_b64\": \"%s\",\n", env_b64.c_str());
  o.add("  \"token\": \"%s\",\n", json_escape(d.token).c_str());
  o.add("  \"is_rooted\": %s,\n", d.rooted ? "true" : "false");
  o.add("  \"apk_hash\": \"%s\",\n", json_escape(d.apk_hash).c_str());
  o.add("  \"content_hash\": \"%s\",\n", json_escape(d.content_hash).c_str());
  o.add("  \"path\": \"%s\",\n", json_escape(d.path).c_str());

  o.add("  \"forbidden_apps\": [");
  for (size_t i = 0; i < d.forbidden.size(); ++i)
    o.add("%s\n    \"%s\"", i ? "," : "", json_escape(d.forbidden[i]).c_str());
  if (!d.forbidden.empty())
    o.add("\n  ");
  o.add("],\n");

  o.add("  \"app_snapshot\": {");
  for (size_t i = 0; i < d.snapshot.size(); ++i)
    o.add("%s\n    \"%s\": \"%s\"", i ? "," : "",
          json_escape(d.snapshot[i].first).c_str(),
          json_escape(d.snapshot[i].second).c_str());
  if (!d.snapshot.empty())
    o.add("\n  ");
  o.add("},\n");

  if (full) {
    if (d.fb_present) {
      o.add("  \"facebook\": {\n");
      o.add("    \"game_id\": \"%s\",\n",
            json_escape(d.fb_game_id).c_str());
      o.add("    \"game_version\": \"%s\",\n",
            json_escape(d.fb_game_version).c_str());
      o.add("    \"platform\": %d,\n", d.fb_platform);
      o.add("    \"locale\": \"%s\",\n", json_escape(d.fb_locale).c_str());
      o.add("    \"store\": %d,\n", d.fb_store);
      o.add("    \"token\": \"%s\"\n", json_escape(d.fb_token).c_str());
      o.add("  },\n");
    }

    DeviceStrings ds;
    read_device_strings(d.dev, &ds);
    o.add("  \"device\": {\n");
    o.add("    \"device_id\": \"%s\",\n", json_escape(ds.device_id).c_str());
    o.add("    \"device_model\": \"%s\",\n",
          json_escape(ds.device_model).c_str());
    o.add("    \"ads_id\": \"%s\",\n", json_escape(ds.ads_id).c_str());
    o.add("    \"vendor_id\": \"%s\"\n", json_escape(ds.vendor_id).c_str());
    o.add("  },\n");
  }

  o.add("  \"captured_at\": %lld\n", static_cast<long long>(time(nullptr)));
  o.add("}\n");
  return o.text;
}

static std::string render_raw(const CaptureData &d, bool full) {
  TextBuilder o;
  auto v = [](const std::string &s) { return s.empty() ? "(empty)" : s; };

  o.add("=== Standoff 2 %s build %u (environment %d) ===\n",
        ofset::game_version, ofset::build, ofset::environment_version);
  o.add("captured_at  : %lld\n", static_cast<long long>(time(nullptr)));
  o.add("source       : %s @0x%llx\n",
        d.is_verification ? "Verification" : "AppVerification",
        static_cast<unsigned long long>(d.object));
  if (!d.game_id.empty() || !d.game_version.empty())
    o.add("game         : %s / %s\n", v(d.game_id).c_str(),
          v(d.game_version).c_str());
  if (!d.credential.empty())
    o.add("credential   : %s\n", d.credential.c_str());
  o.add("integrity    : %s\n",
        d.integrity ? "yes (android-playintegrity)" : "no");

  o.add("\napk_hash     : %s\n", v(d.apk_hash).c_str());
  o.add("content_hash : %s\n", v(d.content_hash).c_str());
  o.add("path         : %s\n", v(d.path).c_str());
  o.add("is_rooted    : %s\n", d.rooted ? "true" : "false");
  o.add("token        : %s\n", v(d.token).c_str());
  o.add("environment  : @0x%llx version=%d info=0x%llx length=%llu klass=%s\n",
        static_cast<unsigned long long>(d.environment), d.env_version,
        static_cast<unsigned long long>(d.info_array),
        static_cast<unsigned long long>(d.info_length),
        d.info_klass.empty() ? "-" : d.info_klass.c_str());
  o.add("env_bytes    : %zu\n", d.env_bytes.size());

  o.add("\nforbidden_apps (%zu):\n", d.forbidden.size());
  for (const auto &s : d.forbidden)
    o.add("  %s\n", s.c_str());

  o.add("\napp_snapshot (%zu):\n", d.snapshot.size());
  for (const auto &kv : d.snapshot)
    o.add("  %s = %s\n", kv.first.c_str(), kv.second.c_str());

  if (full) {
    if (d.fb_present) {
      o.add("\nfacebook:\n");
      o.add("  game_id      = %s\n", v(d.fb_game_id).c_str());
      o.add("  game_version = %s\n", v(d.fb_game_version).c_str());
      o.add("  locale       = %s\n", v(d.fb_locale).c_str());
      o.add("  token        = %s\n", v(d.fb_token).c_str());
    }

    DeviceStrings ds;
    read_device_strings(d.dev, &ds);
    o.add("\ndevice       : @0x%llx\n",
          static_cast<unsigned long long>(d.dev));
    o.add("  device_id    = %s\n", v(ds.device_id).c_str());
    o.add("  device_model = %s\n", v(ds.device_model).c_str());
    o.add("  ads_id       = %s\n", v(ds.ads_id).c_str());
    o.add("  vendor_id    = %s\n", v(ds.vendor_id).c_str());
  }
  return o.text;
}

static void write_capture(const CaptureData &d, bool full,
                          const char *out_path) {
  std::string raw = render_raw(d, full);
  std::string json = render_json(d, full);

  fwrite(raw.data(), 1, raw.size(), stdout);
  fflush(stdout);

  if (!out_path || !*out_path)
    return;

  FILE *f = fopen(out_path, "wb");
  if (!f) {
    fprintf(stderr, "json: WARNING: cannot open %s for writing (errno=%d: %s)\n",
            out_path, errno, strerror(errno));
    return;
  }

  size_t raw_n = fwrite(raw.data(), 1, raw.size(), f);
  fputc('\n', f);
  size_t json_n = fwrite(json.data(), 1, json.size(), f);
  bool close_ok = fclose(f) == 0;
  if (!close_ok || raw_n != raw.size() || json_n != json.size()) {
    fprintf(stderr, "json: WARNING: short write to %s\n", out_path);
    return;
  }
  fprintf(stderr, "json: saved raw+json (%zu+%zu bytes) to %s\n", raw_n, json_n,
          out_path);
}

int json_capture(int pid, uint64_t module_base, uint64_t rva, bool full,
                 const char *out_path) {
  const std::string name = ofset::class_name::facebook_auth_request;
  const std::string ns = ofset::class_name::facebook_auth_request_namespace;

  std::vector<MapEntry> maps = load_maps(pid);
  uint64_t klass = resolve_class_quiet(maps, module_base, rva, name, ns);
  if (!klass) {
    fprintf(stderr, "json: class not resolved\n");
    return 1;
  }

  uint32_t inst_size = rd32(klass + kl::instance_size);
  fprintf(stderr, "json: waiting for live FacebookAuthRequest "
                  "(press FB login)...\n");

  std::vector<uint64_t> hits;
  uint64_t req = 0, auth = 0, appv = 0, dev = 0;
  time_t started = time(nullptr);
  int iter = 0;
  while (time(nullptr) - started < 180) {
    hits = scan_heap_once(maps, klass, inst_size, name, 1);
    if (!hits.empty()) {
      uint64_t r = hits[0];
      uint64_t a = rd64(r + ofset::facebook_auth_request::auth_facebook);
      uint64_t v = rd64(r + ofset::facebook_auth_request::app_verification);
      if (ptr_ok(a) &&
          klass_name(rd64(a)) == ofset::class_name::auth_facebook_full &&
          ptr_ok(v) &&
          klass_name(rd64(v)) == ofset::class_name::app_verification_full) {
        bool ready = false;
        uint64_t env = rd64(v + ofset::app_verification::environment);
        if (ptr_ok(env)) {
          std::string tmp;
          if (read_bytestring(
                  rd64(env + ofset::app_verification_environment::info),
                  &tmp) &&
              tmp.size() > 100)
            ready = true;
        }
        if (ready) {
          req = r;
          auth = a;
          appv = v;
          dev = rd64(r + ofset::facebook_auth_request::device_info);
          break;
        }
      }
    }
    if ((iter++ % 5) == 0) {
      fprintf(stderr, ".");
      fflush(stderr);
    }
    usleep(100000);
  }

  if (!req) {
    fprintf(stderr, "\njson: no live request with filled environment\n");
    return 1;
  }
  fprintf(stderr, "\njson: captured @0x%llx\n",
          static_cast<unsigned long long>(req));

  CaptureData d = collect_appverification(appv, dev);
  read_facebook(auth, 0, &d);
  if (d.fb_present && d.fb_platform == 0)
    d.fb_platform = 1;
  write_capture(d, full, out_path);
  return 0;
}

int json_capture_any(int pid, uint64_t module_base, bool full,
                     const char *out_path) {
  std::vector<MapEntry> maps = load_maps(pid);

  fprintf(stderr, "json any: target %s build %u (env %d), base=0x%llx\n",
          ofset::game_version, ofset::build, ofset::environment_version,
          static_cast<unsigned long long>(module_base));

  uint64_t api_klass = 0, ver_klass = 0, appv_klass = 0, appcheck_klass = 0;

  auto resolve_direct = [&]() {
    if (!api_klass)
      api_klass = resolve_class_quiet(
          maps, module_base, ofset::typeinfo::bolt_player_api,
          ofset::class_name::bolt_player_api,
          ofset::class_name::bolt_player_api_namespace);
    if (!ver_klass)
      ver_klass = resolve_class_quiet(
          maps, module_base, ofset::typeinfo::verification,
          ofset::class_name::verification,
          ofset::class_name::bolt_player_api_namespace);
    if (!appv_klass)
      appv_klass = resolve_class_quiet(
          maps, module_base, ofset::typeinfo::app_verification,
          ofset::class_name::app_verification,
          ofset::class_name::app_verification_namespace);
    if (!appcheck_klass)
      appcheck_klass = resolve_class_quiet(
          maps, module_base, ofset::typeinfo::app_check_tokens,
          ofset::class_name::app_check_tokens,
          ofset::class_name::bolt_player_api_namespace);
  };
  resolve_direct();

  time_t started = time(nullptr);
  while (!api_klass && !ver_klass && !appv_klass &&
         time(nullptr) - started < 5) {
    usleep(50000);
    resolve_direct();
  }

  if (!api_klass && !ver_klass && !appv_klass) {
    api_klass = locate_class(maps, ofset::class_name::bolt_player_api,
                             ofset::class_name::bolt_player_api_namespace);
    if (!api_klass)
      ver_klass = locate_class(maps, ofset::class_name::verification,
                               ofset::class_name::bolt_player_api_namespace);
    if (!api_klass && !ver_klass)
      appv_klass = locate_class(maps, ofset::class_name::app_verification,
                                ofset::class_name::app_verification_namespace);
  }

  if (!api_klass && !ver_klass && !appv_klass) {
    fprintf(stderr, "json any: BoltPlayerApi/Verification/AppVerification "
                    "not found\n");
    return 1;
  }

  fprintf(stderr,
          "json any: BoltPlayerApi=0x%llx Verification=0x%llx "
          "AppVerification=0x%llx AppCheckTokens=0x%llx\n",
          static_cast<unsigned long long>(api_klass),
          static_cast<unsigned long long>(ver_klass),
          static_cast<unsigned long long>(appv_klass),
          static_cast<unsigned long long>(appcheck_klass));

  bool scan_is_ver = ver_klass != 0;
  uint64_t scan_klass = ver_klass;
  const char *scan_name = "Verification";
  if (!scan_klass && appv_klass) {
    scan_klass = appv_klass;
    scan_name = "AppVerification";
    scan_is_ver = false;
  }

  fprintf(stderr, "json any: waiting for live %s...\n",
          api_klass ? "Verification (BoltPlayerApi)" : scan_name);

  uint64_t target = 0;
  bool target_is_ver = true;
  uint64_t fallback = 0;
  bool fallback_is_ver = true;
  time_t env_ready_at = 0;
  bool heap_started = false;
  bool saw_instance = false;
  bool saw_verification = false;
  int iter = 0;

  const bool want_integrity = full;
  bool integrity_wait = false;
  time_t integrity_deadline = 0;
  time_t auth_seen_at = 0;
  std::string integrity_token;
  uint64_t integrity_source = 0;
  time_t last_heap_scan = 0;
  time_t last_integrity_probe = 0;
  bool probe_appcheck = false;
  uint64_t watched_appv = 0;

  auto start_integrity_wait = [&]() {
    if (integrity_wait || !want_integrity)
      return;
    integrity_wait = true;
    time_t now = time(nullptr);
    integrity_deadline = now + kIntegrityWaitSeconds;
    if (api_klass && playerapi_authenticated(api_klass)) {
      integrity_deadline = now + kIntegrityGraceSeconds;
      fprintf(stderr,
              "\njson any: base data ready; already authenticated, waiting "
              "%ds for a cached android-playintegrity...\n",
              kIntegrityGraceSeconds);
    } else {
      fprintf(stderr,
              "\njson any: base data ready; waiting up to %ds for "
              "android-playintegrity (start the login now)...\n",
              kIntegrityWaitSeconds);
    }
  };

  while (time(nullptr) - started < 180) {
    resolve_direct();

    if (!scan_klass) {
      if (ver_klass) {
        scan_klass = ver_klass;
        scan_is_ver = true;
        scan_name = "Verification";
      } else if (appv_klass) {
        scan_klass = appv_klass;
        scan_is_ver = false;
        scan_name = "AppVerification";
      }
    }

    if (api_klass) {
      uint64_t inst = playerapi_instance(api_klass);
      if (inst && !saw_instance) {
        saw_instance = true;
        fprintf(stderr,
                "\njson any: BoltPlayerApi.instance @0x%llx game=%s ver=%s\n",
                static_cast<unsigned long long>(inst),
                read_string_at(inst, ofset::bolt_player_api::game_id).c_str(),
                read_string_at(inst, ofset::bolt_player_api::game_version)
                    .c_str());
      }
      uint64_t v = playerapi_verification(api_klass);
      if (v && !saw_verification) {
        saw_verification = true;
        fprintf(stderr, "\njson any: BoltPlayerApi._verification @0x%llx\n",
                static_cast<unsigned long long>(v));
      }
      if (v) {
        bool has_integrity = false;
        bool ready = verification_ready(v, &has_integrity);
        if (has_integrity) {
          target = v;
          target_is_ver = true;
          break;
        }
        if (ready) {
          if (v != fallback) {
            fallback = v;
            fallback_is_ver = true;
            env_ready_at = time(nullptr);
            std::string env;
            if (!verification_env_bytes(v, &env) || env.size() <= 100)
              fprintf(stderr,
                      "\njson any: waiting up to %ds for Environment.Info "
                      "(press login now)...\n",
                      kEnvWaitSeconds);
          }
          std::string env;
          bool env_ok =
              verification_env_bytes(v, &env) && env.size() > 100;
          int wait_secs = env_ok ? 0 : kEnvWaitSeconds;
          start_integrity_wait();
          if (env_ready_at &&
              time(nullptr) - env_ready_at >= wait_secs) {
            if (!integrity_wait) {
              target = fallback;
              target_is_ver = true;
              break;
            }
          }
        }
      }
    }

    if (!heap_started && (!api_klass || time(nullptr) - started >= 3))
      heap_started = true;

    if (heap_started && scan_klass && time(nullptr) != last_heap_scan) {
      last_heap_scan = time(nullptr);
      uint32_t inst_size = rd32(scan_klass + kl::instance_size);
      if (scan_is_ver) {
        auto hits = scan_heap_once(maps, scan_klass, inst_size,
                                   ofset::class_name::verification, 64);
        for (uint64_t h : hits) {
          bool has_integrity = false;
          if (!verification_ready(h, &has_integrity))
            continue;
          if (!fallback) {
            fallback = h;
            fallback_is_ver = true;
          }
          if (has_integrity) {
            target = h;
            target_is_ver = true;
            break;
          }
        }
        if (!target && fallback) {
          if (!env_ready_at)
            env_ready_at = time(nullptr);
          std::string env;
          bool env_ok =
              verification_env_bytes(fallback, &env) && env.size() > 100;
          int wait_secs = env_ok ? 0 : kEnvWaitSeconds;
          start_integrity_wait();
          if (time(nullptr) - env_ready_at >= wait_secs) {
            if (!integrity_wait) {
              target = fallback;
              target_is_ver = true;
            }
          }
        }
      } else {
        auto hits = scan_heap_once(maps, scan_klass, inst_size,
                                   ofset::class_name::app_verification, 64);
        uint64_t appv_fallback = 0;
        for (uint64_t h : hits) {
          uint64_t env = rd64(h + ofset::app_verification::environment);
          if (!ptr_ok(env))
            continue;
          std::string tmp;
          if (!read_bytestring(
                  rd64(env + ofset::app_verification_environment::info),
                  &tmp) ||
              tmp.size() <= 100)
            continue;
          if (!appv_fallback) {
            appv_fallback = h;
            fallback = h;
            fallback_is_ver = false;
          }
          std::vector<std::pair<std::string, std::string>> snap;
          read_map_string_string(rd64(h + ofset::app_verification::app_snapshot),
                                 &snap);
          bool ok = false;
          for (const auto &kv : snap)
            if (kv.first == "android-playintegrity" && !kv.second.empty())
              ok = true;
          if (ok) {
            target = h;
            target_is_ver = false;
            break;
          }
        }
        if (!target && appv_fallback) {
          if (!env_ready_at)
            env_ready_at = time(nullptr);
          start_integrity_wait();
          if (time(nullptr) - env_ready_at >= 3) {
            if (!integrity_wait) {
              target = appv_fallback;
              target_is_ver = false;
            }
          }
        }
      }
    }


    if (integrity_wait && !target) {
      time_t now = time(nullptr);

      if (integrity_token.empty() && watched_appv &&
          appverification_integrity_token(watched_appv, &integrity_token)) {
        integrity_source = watched_appv;
      }

      if (integrity_token.empty() && now != last_integrity_probe) {
        last_integrity_probe = now;
        probe_appcheck = !probe_appcheck;
        integrity_source =
            probe_integrity(maps, fallback_is_ver ? fallback : 0,
                            appcheck_klass, appv_klass, probe_appcheck,
                            &watched_appv, &integrity_token);
      }
      if (!integrity_token.empty()) {
        if (fallback) {
          target = fallback;
          target_is_ver = fallback_is_ver;
        } else if (integrity_source &&
                   klass_name(rd64(integrity_source)) ==
                       ofset::class_name::app_verification_full) {
          target = integrity_source;
          target_is_ver = false;
        }
        if (target)
          break;
      }

      if (api_klass && playerapi_authenticated(api_klass)) {
        if (!auth_seen_at) {
          auth_seen_at = now;
          if (integrity_deadline > now + kIntegrityGraceSeconds)
            integrity_deadline = now + kIntegrityGraceSeconds;
        }
      }
      if (now >= integrity_deadline) {
        target = fallback;
        target_is_ver = fallback_is_ver;
        break;
      }
    }

    if (target)
      break;
    if ((iter++ % 20) == 0) {
      fprintf(stderr, ".");
      fflush(stderr);
    }
    usleep(50000);
  }

  if (!target) {
    fprintf(stderr, "\njson any: no live %s with filled environment\n",
            scan_name);
    return 1;
  }

  fprintf(stderr, "\njson any: captured %s @0x%llx\n",
          target_is_ver ? "Verification" : "AppVerification",
          static_cast<unsigned long long>(target));

  uint64_t auth = 0, dev = 0, cred = 0;
  std::string cred_name;

  if (api_klass) {
    cred = playerapi_credential(api_klass);
    if (ptr_ok(cred))
      cred_name = klass_name(rd64(cred));
    if (!cred_name.empty())
      fprintf(stderr, "json any: credential=%s\n", cred_name.c_str());
  }

  if (full) {
    if (api_klass) {
      for (int i = 0; i < 20 && !dev; ++i) {
        dev = playerapi_device(api_klass);
        if (!dev)
          usleep(50000);
      }
      if (dev)
        fprintf(stderr, "json any: device_info @0x%llx (%s)\n",
                static_cast<unsigned long long>(dev),
                klass_name(rd64(dev)).c_str());
    }

    bool maybe_fb =
        cred_name.empty() || cred_name.find("Facebook") != std::string::npos;
    if (maybe_fb) {
      uint64_t fbklass = resolve_class_quiet(
          maps, module_base, ofset::typeinfo::facebook_auth_request,
          ofset::class_name::facebook_auth_request,
          ofset::class_name::facebook_auth_request_namespace);
      if (!fbklass)
        fbklass = locate_class(maps, ofset::class_name::facebook_auth_request,
                               ofset::class_name::facebook_auth_request_namespace);
      if (fbklass) {
        uint32_t fbsize = rd32(fbklass + kl::instance_size);
        auto fbhits = scan_heap_once(
            maps, fbklass, fbsize, ofset::class_name::facebook_auth_request, 32);
        uint64_t first_auth = 0, first_dev = 0;
        for (uint64_t r : fbhits) {
          uint64_t a = rd64(r + ofset::facebook_auth_request::auth_facebook);
          uint64_t v = rd64(r + ofset::facebook_auth_request::app_verification);
          if (!(ptr_ok(a) &&
                klass_name(rd64(a)) == ofset::class_name::auth_facebook_full &&
                ptr_ok(v) &&
                klass_name(rd64(v)) ==
                    ofset::class_name::app_verification_full))
            continue;
          if (!first_auth) {
            first_auth = a;
            first_dev = rd64(r + ofset::facebook_auth_request::device_info);
          }
          if (!read_string_at(a, ofset::auth_facebook::token).empty()) {
            auth = a;
            if (!dev)
              dev = rd64(r + ofset::facebook_auth_request::device_info);
            break;
          }
        }
        if (!auth && first_auth) {
          auth = first_auth;
          if (!dev)
            dev = first_dev;
        }
      }
    }
  }

  CaptureData d = target_is_ver ? collect_verification(target, dev)
                                : collect_appverification(target, dev);
  d.credential = cred_name;
  if (api_klass) {
    uint64_t api_inst = playerapi_instance(api_klass);
    if (api_inst) {
      d.game_id = read_string_at(api_inst, ofset::bolt_player_api::game_id);
      d.game_version =
          read_string_at(api_inst, ofset::bolt_player_api::game_version);
    }
  }

  read_facebook(auth, cred, &d);
  if (d.fb_present) {
    if (d.fb_game_id.empty())
      d.fb_game_id = d.game_id;
    if (d.fb_game_version.empty())
      d.fb_game_version = d.game_version;
    if (d.fb_platform == 0)
      d.fb_platform = 1; 
    fprintf(stderr,
            "json any: facebook authfb=0x%llx token=%zu bytes "
            "game=%s ver=%s locale=%s\n",
            static_cast<unsigned long long>(auth), d.fb_token.size(),
            d.fb_game_id.c_str(), d.fb_game_version.c_str(),
            d.fb_locale.c_str());
  }

  if (!integrity_token.empty()) {
    if (integrity_source && ptr_ok(integrity_source) &&
        klass_name(rd64(integrity_source)) ==
            ofset::class_name::app_verification_full)
      merge_appverification_snapshot(integrity_source, &d.snapshot,
                                     &integrity_token);
    snapshot_set(&d.snapshot, "android-playintegrity", integrity_token);
    d.integrity = true;
    fprintf(stderr,
            "json any: android-playintegrity @0x%llx (%zu bytes)\n",
            static_cast<unsigned long long>(integrity_source),
            integrity_token.size());
  }

  fprintf(stderr,
          "json any: env_version=%d env_bytes=%zu integrity=%s device=%s\n",
          d.env_version, d.env_bytes.size(), d.integrity ? "yes" : "no",
          ptr_ok(d.dev) ? "yes" : "no");
  if (d.env_bytes.empty())
    fprintf(stderr,
            "json any: WARNING: Environment.Info is empty "
            "(info=0x%llx length=%llu klass=%s) - captured without "
            "environment\n",
            static_cast<unsigned long long>(d.info_array),
            static_cast<unsigned long long>(d.info_length),
            d.info_klass.empty() ? "-" : d.info_klass.c_str());

  write_capture(d, full, out_path);
  return 0;
}
