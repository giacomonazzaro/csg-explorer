#pragma once
#include <string_view>
using namespace std;

#include "csg.h"
using namespace yocto;

// file wrapper with RIIA
struct file_wrapper {
  file_wrapper() {}
  ~file_wrapper() {
    if (fs) fclose(fs);
  }
  file_wrapper(const file_wrapper&) = delete;
  file_wrapper& operator=(const file_wrapper&) = delete;
  file_wrapper(file_wrapper&& other) {
    if (this == &other) return;
    std::swap(filename, other.filename);
    std::swap(filename, other.filename);
  }
  file_wrapper& operator=(file_wrapper&& other) {
    if (this == &other) return *this;
    std::swap(filename, other.filename);
    std::swap(filename, other.filename);
    return *this;
  }

  string filename = ""s;
  FILE*  fs       = nullptr;
};

file_wrapper open_file(const string& filename, const string& mode) {
  auto fs = file_wrapper{};
  fs.fs   = fopen(filename.c_str(), mode.c_str());
  if (!fs.fs) throw std::runtime_error{filename + ": file not found"};
  fs.filename = filename;
  return fs;
}

bool read_line(file_wrapper& fs, char* buffer, int size) {
  return (bool)fgets(buffer, size, fs.fs);
}

// utilities
static bool is_newline(char c) { return c == '\r' || c == '\n'; }
static bool is_space(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}
static void skip_whitespace(string_view& str) {
  while (!str.empty() && is_space(str.front())) str.remove_prefix(1);
}
// static void trim_whitespace(string_view& str) {
//  while (!str.empty() && is_space(str.front())) str.remove_prefix(1);
//  while (!str.empty() && is_space(str.back())) str.remove_suffix(1);
//}
// static bool is_digit(char c) { return c >= '0' && c <= '9'; }
// static bool is_alpha(char c) {
//  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
//}
//
// static bool is_whitespace(string_view str) {
//  while (!str.empty()) {
//    if (!is_space(str.front())) return false;
//    str.remove_prefix(1);
//  }
//  return true;
//}

inline void parse_value(string_view& str, string_view& value) {
  skip_whitespace(str);
  if (str.empty()) assert(0 && "string expected");
  if (str.front() != '"') {
    auto cpy = str;
    while (!cpy.empty() && !is_space(cpy.front())) cpy.remove_prefix(1);
    value = str;
    value.remove_suffix(cpy.size());
    str.remove_prefix(str.size() - cpy.size());
  } else {
    if (str.front() != '"') assert(0 && "string expected");
    str.remove_prefix(1);
    if (str.empty()) assert(0 && "string expected");
    auto cpy = str;
    while (!cpy.empty() && cpy.front() != '"') cpy.remove_prefix(1);
    if (cpy.empty()) assert(0 && "string expected");
    value = str;
    value.remove_suffix(cpy.size());
    str.remove_prefix(str.size() - cpy.size());
    str.remove_prefix(1);
  }
}
inline void parse_value(string_view& str, string& value) {
  auto valuev = string_view{};
  parse_value(str, valuev);
  value = string{valuev};
}
inline void parse_value(string_view& str, int8_t& value) {
  char* end = nullptr;
  value     = (int8_t)strtol(str.data(), &end, 10);
  if (str.data() == end) assert(0 && "int expected");
  str.remove_prefix(end - str.data());
}
inline void parse_value(string_view& str, int16_t& value) {
  char* end = nullptr;
  value     = (int16_t)strtol(str.data(), &end, 10);
  if (str.data() == end) assert(0 && "int expected");
  str.remove_prefix(end - str.data());
}
inline void parse_value(string_view& str, int32_t& value) {
  char* end = nullptr;
  value     = (int32_t)strtol(str.data(), &end, 10);
  if (str.data() == end) assert(0 && "int expected");
  str.remove_prefix(end - str.data());
}
inline void parse_value(string_view& str, int64_t& value) {
  char* end = nullptr;
  value     = (int64_t)strtoll(str.data(), &end, 10);
  if (str.data() == end) assert(0 && "int expected");
  str.remove_prefix(end - str.data());
}
inline void parse_value(string_view& str, uint8_t& value) {
  char* end = nullptr;
  value     = (uint8_t)strtoul(str.data(), &end, 10);
  if (str.data() == end) assert(0 && "uint expected");
  str.remove_prefix(end - str.data());
}
inline void parse_value(string_view& str, uint16_t& value) {
  char* end = nullptr;
  value     = (uint16_t)strtoul(str.data(), &end, 10);
  if (str.data() == end) assert(0 && "uint expected");
  str.remove_prefix(end - str.data());
}
inline void parse_value(string_view& str, uint32_t& value) {
  char* end = nullptr;
  value     = (uint32_t)strtoul(str.data(), &end, 10);
  if (str.data() == end) assert(0 && "uint expected");
  str.remove_prefix(end - str.data());
}
inline void parse_value(string_view& str, uint64_t& value) {
  char* end = nullptr;
  value     = (uint64_t)strtoull(str.data(), &end, 10);
  if (str.data() == end) assert(0 && "uint expected");
  str.remove_prefix(end - str.data());
}
inline void parse_value(string_view& str, bool& value) {
  auto valuei = 0;
  parse_value(str, valuei);
  value = (bool)valuei;
}
inline void parse_value(string_view& str, float& value) {
  char* end = nullptr;
  value     = strtof(str.data(), &end);
  if (str.data() == end) assert(0 && "float expected");
  str.remove_prefix(end - str.data());
}
inline void parse_value(string_view& str, double& value) {
  char* end = nullptr;
  value     = strtod(str.data(), &end);
  if (str.data() == end) assert(0 && "double expected");
  str.remove_prefix(end - str.data());
}
#ifdef __APPLE__
inline void parse_value(string_view& str, size_t& value) {
  char* end = nullptr;
  value     = (size_t)strtoull(str.data(), &end, 10);
  if (str.data() == end) assert(0 && "uint expected");
  str.remove_prefix(end - str.data());
}
#endif

inline void parse_value(string_view& str, vec2f& value) {
  for (auto i = 0; i < 2; i++) parse_value(str, value[i]);
}
inline void parse_value(string_view& str, vec3f& value) {
  for (auto i = 0; i < 3; i++) parse_value(str, value[i]);
}
inline void parse_value(string_view& str, vec4f& value) {
  for (auto i = 0; i < 4; i++) parse_value(str, value[i]);
}
inline void parse_value(string_view& str, frame3f& value) {
  for (auto i = 0; i < 4; i++) parse_value(str, value[i]);
}
inline void parse_value(string_view& str, mat4f& value) {
  for (auto i = 0; i < 4; i++) parse_value(str, value[i]);
}

static void skip_comment(string_view& str, char comment_char = '#') {
  while (!str.empty() && is_newline(str.back())) str.remove_suffix(1);
  auto cpy = str;
  while (!cpy.empty() && cpy.front() != comment_char) cpy.remove_prefix(1);
  str.remove_suffix(cpy.size());
}

template <typename T>
void parse_value(file_wrapper& fs, string_view& str, T& value) {
  try {
    parse_value(str, value);
  } catch (std::exception& e) {
    throw std::runtime_error{fs.filename + ": parse error [" + e.what() + "]"};
  }
}

Csg parse_csg(const string& filename) {
  auto csg = CsgTree{};

  auto fs = open_file(filename, "rb");
  char buffer[4096];

  while (read_line(fs, buffer, sizeof(buffer))) {
    // str
    auto str = string_view{buffer};
    skip_comment(str);
    skip_whitespace(str);
    if (str.empty()) continue;

    auto primitive = CsgPrimitve{};
    auto operation = CsgOperation{};
    int  parent    = -1;

    parse_value(str, parent);
    if (parent == -1) parent = csg.root;

    parse_value(str, operation.blend);
    parse_value(str, operation.softness);

    string primitive_name = "";
    parse_value(str, primitive_name);
    if (primitive_name == "sphere")
      primitive.type = primitive_type::sphere;
    else if (primitive_name == "cube")
      primitive.type = primitive_type::box;
    else
      assert(0);

    int num_params;
    if (primitive.type == primitive_type::sphere) num_params = 4;
    if (primitive.type == primitive_type::box) num_params = 4;
    for (int i = 0; i < num_params; i++) {
      parse_value(str, primitive.params[i]);
    }

    add_edit(csg, parent, operation, primitive);
  }

  optimize_csg(csg);
  return csg;

}
