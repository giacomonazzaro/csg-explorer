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
static bool is_digit(char c) { return c >= '0' && c <= '9'; }
static bool is_number(char c) {
  return (c >= '0' && c <= '9') || c == '+' || c == '-';
}

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

void parse_primitive(
    string_view& str, CsgPrimitve& primitive, const string& name) {
  if (name == "sphere") {
    primitive.type = primitive_type::sphere;
  } else if (name == "cube") {
    primitive.type = primitive_type::box;
  } else {
    assert(0);
  }
  int num_params;
  if (primitive.type == primitive_type::sphere) num_params = 4;
  if (primitive.type == primitive_type::box) num_params = 4;
  for (int i = 0; i < num_params; i++) {
    parse_value(str, primitive.params[i]);
  }
}

Csg parse_csg(const string& filename) {
  auto csg = CsgTree{};

  auto                       fs = open_file(filename, "rb");
  char                       buffer[4096];
  unordered_map<string, int> names;

  while (read_line(fs, buffer, sizeof(buffer))) {
    // str
    auto str = string_view{buffer};
    skip_comment(str);
    skip_whitespace(str);
    if (str.empty()) continue;

    auto   primitive = CsgPrimitve{};
    auto   operation = CsgOperation{};
    string lhs;
    parse_value(str, lhs);
    assert(lhs != "sphere");
    assert(lhs != "cube");

    // operation
    skip_whitespace(str);
    operation.blend = +1;
    bool assignment = (str[0] == '=');
    bool add        = (str[0] == '+' && str[1] == '=');
    bool sub        = (str[0] == '-' && str[1] == '=');
    if (!assignment && !add && !sub) {
      //      printf("%s\n", str.c_str());
      assert(0 && "Not a valid operator.");
      // @Check =, += or -=
    }
    int parent = -1;

    if (assignment) {
      str.remove_prefix(1);
      names[lhs] = csg.nodes.size();
    } else {
      str.remove_prefix(2);
      parent = names.at(lhs);
    }
    skip_whitespace(str);

    if (is_number(str[0])) {
      parse_value(str, operation.blend);
      skip_whitespace(str);

      operation.softness = 0;
      if (is_number(str[0])) {
        parse_value(str, operation.softness);
        // @Check in [0, 1]
        skip_whitespace(str);
      }
    }
    if (sub) operation.blend = -operation.blend;

    string rhs;
    parse_value(str, rhs);
    int  child = -1;
    auto it    = names.find(rhs);

    // rhs is a name or primitve
    if (it != names.end()) {
      child = it->second;
    } else {
      parse_primitive(str, primitive, rhs);
    }

    if (assignment) {
      if (csg.nodes.empty()) csg.root = 0;
      names[lhs] = csg.nodes.size();
      add_primitive(csg, primitive);
    } else {
      assert(parent != -1);
      auto backup                  = csg.nodes[parent];
      csg.nodes[parent].operation  = operation;
      csg.nodes[parent].children.x = csg.nodes.size();
      csg.nodes.push_back(backup);

      if (child != -1) {
        csg.nodes[parent].children.y = child;
      } else {
        csg.nodes[parent].children.y = csg.nodes.size();
        add_primitive(csg, primitive);
      }
    }
  }

  optimize_csg(csg);
  return csg;
}
