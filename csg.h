#pragma once
#include "yocto/yocto_math.h"
using namespace yocto;

enum struct primitive_type { sphere, box, none };

struct CsgOperation {
  bool           add       = true;
  float          softness  = 0;
  primitive_type primitive = primitive_type::none;
  vector<float>  params    = {};
};

struct CsgNode {
  int          parent   = -1;
  vec2i        children = {-1, -1};
  CsgOperation operation;
};

struct CsgTree {
  vector<CsgNode> nodes = {};
  int             root  = -1;
};

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

inline float eval_primitive(const vec3f& position, primitive_type primitive,
    const vector<float>& params) {
  if (primitive == primitive_type::sphere) {
    auto center = vec3f{params[0], params[1], params[2]};
    auto radius = params[3];
    return length(position - center) - radius;
  }
  if (primitive == primitive_type::box) {
    return 1;
  }
  assert(0);
  return 1;
}

inline float eval_csg(
    const CsgTree& csg, const vec3f& position, const CsgNode& node) {
  auto& op = node.operation;
  if (node.children == vec2i{-1, -1}) {
    return eval_primitive(position, op.primitive, op.params);
  } else {
    auto f = eval_csg(csg, position, csg.nodes[node.children.x]);
    auto g = eval_csg(csg, position, csg.nodes[node.children.y]);
    if (op.add)
      return smin(f, g, op.softness);
    else
      return smax(f, -g, op.softness);
  }
}

inline int add_operation(CsgTree& csg, int parent, const CsgOperation& op) {
  int index = csg.nodes.size();

  if (csg.nodes.empty()) {
    auto& n     = csg.nodes.emplace_back();
    n.operation = op;
    csg.root    = 0;
    return index;
  }

  if (parent == csg.root) {
    csg.nodes.push_back({});
    csg.nodes.push_back({});
    auto& root = csg.nodes[index];
    auto& n    = csg.nodes[index + 1];

    csg.root                 = index;
    csg.nodes[parent].parent = csg.root;

    root.children           = {parent, index + 1};
    root.operation.softness = op.softness;
    root.operation.add      = op.add;

    n.parent              = csg.root;
    n.operation.primitive = op.primitive;
    n.operation.params    = op.params;
    return index + 1;
  }

  assert(csg.nodes[parent].children.x == -1);
  assert(csg.nodes[parent].children.y == -1);

  // old
  auto& a               = csg.nodes.emplace_back();
  a.parent              = parent;
  a.operation.primitive = csg.nodes[parent].operation.primitive;
  a.operation.params    = csg.nodes[parent].operation.params;

  // new
  auto& b               = csg.nodes.emplace_back();
  b.parent              = parent;
  b.operation.primitive = op.primitive;
  b.operation.params    = op.params;

  csg.nodes[parent].children           = {index, index + 1};
  csg.nodes[parent].operation.softness = op.softness;
  csg.nodes[parent].operation.add      = op.add;
  return index + 1;
}
