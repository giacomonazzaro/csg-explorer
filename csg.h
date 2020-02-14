#pragma once
#include "yocto/yocto_math.h"
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

struct CsgXXX {
  vec2i children;
  union {
    CsgOperation operation;
    CsgPrimitve  primitive;
  };
};

struct CsgNode {
  int   parent   = -1;
  vec2i children = {-1, -1};

  //  union {
  CsgOperation operation;
  CsgPrimitve  primitive;
  //  };

  CsgNode() {}
};

struct CsgTree {
  vector<CsgNode> nodes = {};
  int             root  = -1;
};

#define BAKE 0

#if BAKE
using Csg = vector<CsgXXX>;
#else
using Csg = CsgTree;
#endif

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
    const vec3f& position, primitive_type primitive, const float* params) {
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

inline float eval_operation(float f, float g, float blend, float softness) {
  if (blend >= 0) {
    return lerp(f, smin(f, g, softness), blend);
  } else {
    return lerp(f, smax(f, -g, softness), -blend);
  }
}

inline float eval_csg(
    const CsgTree& csg, const vec3f& position, const CsgNode& node) {
  if (node.children == vec2i{-1, -1}) {
    auto [params, type] = node.primitive;
    return eval_primitive(position, type, params);
  } else {
    auto f = eval_csg(csg, position, csg.nodes[node.children.x]);
    auto g = eval_csg(csg, position, csg.nodes[node.children.y]);
    auto [blend, softness] = node.operation;
    return eval_operation(f, g, blend, softness);
  }
}

inline float eval_csg(const CsgTree& csg, const vec3f& position) {
  return eval_csg(csg, position, csg.nodes[csg.root]);
}

inline void bake_eval_csg(
    const CsgTree& csg, int n, vector<CsgXXX>& result, vector<int>& mapping) {
  auto& node = csg.nodes[n];
  auto  f    = CsgXXX{};

  if (node.children == vec2i{-1, -1}) {
    auto& primitive = node.primitive;
    for (int i = 0; i < 16; i++) f.primitive.params[i] = primitive.params[i];
    f.primitive.type = primitive.type;
    f.children       = {-1, -1};
    mapping[n]       = result.size();
    result.push_back(f);
  } else {
    bake_eval_csg(csg, node.children.x, result, mapping);
    bake_eval_csg(csg, node.children.y, result, mapping);
    assert(mapping[node.children.x] != -1);
    assert(mapping[node.children.y] != -1);
    f.children           = {mapping[node.children.x], mapping[node.children.y]};
    f.operation.blend    = node.operation.blend;
    f.operation.softness = node.operation.softness;
    mapping[n]           = result.size();
    result.push_back(f);
  }
}

inline vector<CsgXXX> bake_eval_csg(const CsgTree& csg) {
  auto result  = vector<CsgXXX>();
  auto mapping = vector<int>(csg.nodes.size(), -1);
  bake_eval_csg(csg, csg.root, result, mapping);
  return result;
}

inline float eval_csg(const vector<CsgXXX>& csg, const vec3f& position) {
  auto values = vector<float>(csg.size());
  for (int i = 0; i < csg.size(); i++) {
    auto& inst = csg[i];
    if (inst.children == vec2i{-1, -1}) {
      values[i] = eval_primitive(
          position, inst.primitive.type, inst.primitive.params);
    } else {
      auto f    = values[inst.children.x];
      auto g    = values[inst.children.y];
      values[i] = eval_operation(
          f, g, inst.operation.blend, inst.operation.softness);
    }
  }
  return values.back();
}

inline int add_edit(
    CsgTree& csg, int parent, const CsgOperation& op, const CsgPrimitve& prim) {
  int index = csg.nodes.size();

  if (csg.nodes.empty()) {
    auto& n     = csg.nodes.emplace_back();
    n.primitive = prim;
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

    root.children  = {parent, index + 1};
    root.operation = op;

    n.parent    = csg.root;
    n.primitive = prim;

    return index + 1;
  }

  assert(csg.nodes[parent].children.x == -1);
  assert(csg.nodes[parent].children.y == -1);

  // old
  auto& a     = csg.nodes.emplace_back();
  a.parent    = parent;
  a.operation = csg.nodes[parent].operation;

  // new
  auto& b     = csg.nodes.emplace_back();
  b.parent    = parent;
  b.primitive = prim;

  csg.nodes[parent].children  = {index, index + 1};
  csg.nodes[parent].operation = op;
  return index + 1;
}

inline int add_sphere(CsgTree& csg, int parent, float softness,
    const vec3f& center, float radius) {
  return add_edit(csg, parent, {true, softness},
      {{center.x, center.y, center.z, radius}, primitive_type::sphere});
}

inline int subtract_sphere(CsgTree& csg, int parent, float softness,
    const vec3f& center, float radius) {
  return add_edit(csg, parent, {false, softness},
      {{center.x, center.y, center.z, radius}, primitive_type::sphere});
}

// typedef vector<CsgXXX> Csg;
