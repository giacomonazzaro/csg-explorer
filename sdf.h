struct SphereSdf {
  float distance = 0;
  vec3f center   = {0, 0, 0};
  float radius   = 0;
};

SphereSdf sphere(const vec3f& p, const vec3f& center, float radius) {
  auto diff    = center - p;
  auto len     = length(diff);
  sdf.distance = len - radius;
  sdf.gradient = diff / len;
  sdf.radius   = -1;
}

template <typename T>
float square(const T& x) {
  return x * x;
}

template <typename F, typename G>
SphereSdf loss(F f, G g) {
  SphereSdf result = {};
  for (int i = 0; i < N; i++) {
    for (int j = 0; j < N; j++) {
      for (int k = 0; k < N; k++) {
        auto  p    = vec3f(i, j, k) / N;
        auto  fp   = f(p);
        auto  gp   = g(p);
        float loss = square(gp.distance - fp.distance);
        result.distance += loss;
        result.center += 2 * loss * fp.center;
        result.radius += 2 * loss * fp.radius;
      }
    }
  }
  result.distance /= N * N * N;
  result.center /= N * N * N;
  result.radius /= N * N * N;
  return result;
}

void gradient_descent(vec3f& center, float& radius) {
  auto  g   = [&](const vec3f& p) { return sphere(p, center, radius); };
  float eps = 0.01;
  for (int i = 0; i < N; i++) {
    auto gradient = loss(, g);
    center -= eps * gradient.center;
    radius -= eps * gradient.radius;
  }
}

// BoxSdf box(const vec3f& p, const vec3f& center, const vec3f size) {
//   auto q = abs(p) - size;
//   auto dq = vec3f(1);
//   for (int i = 0; i < 3; i++) {
//       if(q[]
//   }
//   q = length(max(q, 0.0))
//   return  + min(max(q.x, max(q.y, q.z)), 0.0);
// }

// float smin(float a, float b, float k) {
//   float h = max(k - abs(a - b), 0.0) / k;
//   return min(a, b) - h * h * k * (1.0 / 4.0);
// }
