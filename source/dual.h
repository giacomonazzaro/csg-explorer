struct Dvec3f {
  vec3f v;
  vec3f d;
};
struct F3x1 {
  float in;
  vec3f out;
};

operator+(const Dvec3f& a, const Dvec3f& b) { return {a.v + b.v, a.d + b.d}; }
operator*(const Dvec3f& a, const Dvec3f& b) {
  return {a.v * b.v, a.v * b.d + b.v * a.d};
}

F3x1 length(const Dvec3f& v) {
  auto d = length(v.v);
  returnurn{d, v.d * v.v / d};
}
