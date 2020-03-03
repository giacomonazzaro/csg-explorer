#pragma once
#include "ext/yocto-gl/yocto/yocto_math.h"
using namespace yocto;

enum struct primitive_type { sphere, box, none };

struct CsgOperation {
  float blend;
  float softness;
};

struct CsgPrimitve {
  float          params[16];
  primitive_type type;
};

struct CsgNode {
  string name = "";
  vec2i children = {-1, -1};
  union {
    CsgOperation operation;
    CsgPrimitve  primitive;
  };
};

struct CsgTree {
  vector<CsgNode> nodes = {};
  int             root  = -1;
};

inline int add_primitive(CsgTree& csg, const CsgPrimitve& primitive) {
  auto node      = CsgNode();
  node.children  = {-1, -1};
  node.primitive = primitive;
  csg.nodes.push_back(node);
  return csg.nodes.size() - 1;
}

inline int add_operation(
    CsgTree& csg, const CsgOperation& operation, const vec2i& children) {
  auto node      = CsgNode();
  node.children  = children;
  node.operation = operation;
  csg.nodes.push_back(node);
  return csg.nodes.size() - 1;
}

using Csg = CsgTree;

inline float smin(float a, float b, float k) {
  if (k == 0) return yocto::min(a, b);
  float h = max(k - yocto::abs(a - b), 0.0) / k;
  return min(a, b) - h * h * k * (1.0 / 4.0);
};

inline float smax(float a, float b, float k) {
  if (k == 0) return yocto::max(a, b);
  float h = max(k - yocto::abs(a - b), 0.0) / k;
  return max(a, b) + h * h * k * (1.0 / 4.0);
};

inline float eval_primitive(
    const vec3f& position, const CsgPrimitve& primitive) {
  // Sphere
  if (primitive.type == primitive_type::sphere) {
    auto center = (const vec3f*)primitive.params;
    auto radius = primitive.params[3];
    return length(position - *center) - radius;
  }
  // Box
  if (primitive.type == primitive_type::box) {
    return 1;
  }
  assert(0);
  return 1;
}

inline float eval_operation(float f, float g, const CsgOperation& operation) {
  if (operation.blend >= 0) {
    // Union
    return lerp(f, smin(f, g, operation.softness), operation.blend);
  } else {
    // Subtracion
    return lerp(f, smax(f, -g, operation.softness), -operation.blend);
  }
}

inline float eval_csg_recursive(
    const CsgTree& csg, const vec3f& position, const CsgNode& node) {
  if (node.children == vec2i{-1, -1}) {
    return eval_primitive(position, node.primitive);
  } else {
    auto f = eval_csg_recursive(csg, position, csg.nodes[node.children.x]);
    auto g = eval_csg_recursive(csg, position, csg.nodes[node.children.y]);
    return eval_operation(f, g, node.operation);
  }
}

inline float eval_csg_recursive(const CsgTree& csg, const vec3f& position) {
  return eval_csg_recursive(csg, position, csg.nodes[csg.root]);
}

inline void optimize_csg_internal(
    const CsgTree& csg, int n, vector<CsgNode>& result, vector<int>& mapping) {
  auto& node = csg.nodes[n];
  auto  f    = CsgNode{};

  if (node.children == vec2i{-1, -1}) {
    f.children  = {-1, -1};
    f.primitive = node.primitive;
  } else {
    optimize_csg_internal(csg, node.children.x, result, mapping);
    optimize_csg_internal(csg, node.children.y, result, mapping);
    f.children  = {mapping[node.children.x], mapping[node.children.y]};
    f.operation = node.operation;
  }
  f.name = node.name;
  mapping[n] = result.size();
  result.push_back(f);
}

inline void optimize_csg(CsgTree& csg) {
  auto result  = CsgTree{};
  auto mapping = vector<int>(csg.nodes.size(), -1);
  optimize_csg_internal(csg, csg.root, result.nodes, mapping);
  result.root = result.nodes.size() - 1;
  std::swap(csg, result);
}
inline float eval_csg(vector<float>& values, const CsgTree& csg, const vec3f& position) {
  assert(csg.root == csg.nodes.size() - 1);
  assert(values.size() == csg.nodes.size());
  for (int i = 0; i < csg.nodes.size(); i++) {
    auto& inst = csg.nodes[i];
    if (inst.children == vec2i{-1, -1}) {
      values[i] = eval_primitive(position, inst.primitive);
    } else {
      auto f    = values[inst.children.x];
      auto g    = values[inst.children.y];
      values[i] = eval_operation(f, g, inst.operation);
    }
  }
  return values.back();
}

inline float eval_csg(const CsgTree& csg, const vec3f& position) {
  auto values = vector<float>(csg.nodes.size());
  return eval_csg(values, csg, position);
}

