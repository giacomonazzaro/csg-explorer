//
// Implementation for Yocto/GL Input and Output functions.
//

//
// LICENSE:
//
// Copyright (c) 2016 -- 2019 Fabio Pellacini
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//

//
// TODO: update transforms -> should be compute transforms?
// TODO: update tesselation -> should be compute tesselation
// TODO: move out animation utilities
//

#include "yocto_sceneio.h"

#include <atomic>
#include <cassert>
#include <cctype>
#include <climits>
#include <cstdlib>
#include <deque>
#include <future>
#include <memory>

#include "yocto_image.h"
#include "yocto_modelio.h"
#include "yocto_shape.h"

// -----------------------------------------------------------------------------
// IMPLEMENTATION OF PATH HELPERS
// -----------------------------------------------------------------------------
namespace yocto {

// Utility to normalize a path
static inline string normalize_path(const string& filename_) {
  auto filename = filename_;
  for (auto& c : filename)

    if (c == '\\') c = '/';
  if (filename.size() > 1 && filename[0] == '/' && filename[1] == '/') {
    throw std::invalid_argument("absolute paths are not supported");
    return filename_;
  }
  if (filename.size() > 3 && filename[1] == ':' && filename[2] == '/' &&
      filename[3] == '/') {
    throw std::invalid_argument("absolute paths are not supported");
    return filename_;
  }
  auto pos = (size_t)0;
  while ((pos = filename.find("//")) != filename.npos)
    filename = filename.substr(0, pos) + filename.substr(pos + 1);
  return filename;
}

// Get directory name (including '/').
static inline string get_dirname(const string& filename_) {
  auto filename = normalize_path(filename_);
  auto pos      = filename.rfind('/');
  if (pos == string::npos) return "";
  return filename.substr(0, pos + 1);
}

// Get extension (not including '.').
static inline string get_extension(const string& filename_) {
  auto filename = normalize_path(filename_);
  auto pos      = filename.rfind('.');
  if (pos == string::npos) return "";
  return filename.substr(pos);
}

// Get filename without directory.
static inline string get_filename(const string& filename_) {
  auto filename = normalize_path(filename_);
  auto pos      = filename.rfind('/');
  if (pos == string::npos) return filename;
  return filename.substr(pos + 1);
}

// Get extension.
static inline string get_noextension(const string& filename_) {
  auto filename = normalize_path(filename_);
  auto pos      = filename.rfind('.');
  if (pos == string::npos) return filename;
  return filename.substr(0, pos);
}

// Get filename without directory and extension.
static inline string get_basename(const string& filename) {
  return get_noextension(get_filename(filename));
}

// Replaces extensions
static inline string replace_extension(
    const string& filename, const string& ext) {
  return get_noextension(filename) + ext;
}

}  // namespace yocto

// -----------------------------------------------------------------------------
// IMPLEMENTATION OF CONCURRENCY UTILITIES
// -----------------------------------------------------------------------------
namespace yocto {

// Simple parallel for used since our target platforms do not yet support
// parallel algorithms. `Func` takes the integer index.
template <typename Func>
inline void parallel_for(int begin, int end, Func&& func) {
  auto             futures  = vector<std::future<void>>{};
  auto             nthreads = std::thread::hardware_concurrency();
  std::atomic<int> next_idx(begin);
  for (auto thread_id = 0; thread_id < nthreads; thread_id++) {
    futures.emplace_back(
        std::async(std::launch::async, [&func, &next_idx, end]() {
          while (true) {
            auto idx = next_idx.fetch_add(1);
            if (idx >= end) break;
            func(idx);
          }
        }));
  }
  for (auto& f : futures) f.get();
}

// Simple parallel for used since our target platforms do not yet support
// parallel algorithms. `Func` takes a reference to a `T`.
template <typename T, typename Func>
inline void parallel_foreach(vector<T>& values, Func&& func) {
  parallel_for(
      0, (int)values.size(), [&func, &values](int idx) { func(values[idx]); });
}
template <typename T, typename Func>
inline void parallel_foreach(const vector<T>& values, Func&& func) {
  parallel_for(
      0, (int)values.size(), [&func, &values](int idx) { func(values[idx]); });
}

}  // namespace yocto

// -----------------------------------------------------------------------------
// IMPLEMENTATION OF ANIMATION UTILITIES
// -----------------------------------------------------------------------------
namespace yocto {

// Find the first keyframe value that is greater than the argument.
inline int keyframe_index(const vector<float>& times, const float& time) {
  for (auto i = 0; i < times.size(); i++)
    if (times[i] > time) return i;
  return (int)times.size();
}

// Evaluates a keyframed value using step interpolation.
template <typename T>
inline T keyframe_step(
    const vector<float>& times, const vector<T>& vals, float time) {
  if (time <= times.front()) return vals.front();
  if (time >= times.back()) return vals.back();
  time     = clamp(time, times.front(), times.back() - 0.001f);
  auto idx = keyframe_index(times, time);
  return vals.at(idx - 1);
}

// Evaluates a keyframed value using linear interpolation.
template <typename T>
inline vec4f keyframe_slerp(
    const vector<float>& times, const vector<vec4f>& vals, float time) {
  if (time <= times.front()) return vals.front();
  if (time >= times.back()) return vals.back();
  time     = clamp(time, times.front(), times.back() - 0.001f);
  auto idx = keyframe_index(times, time);
  auto t   = (time - times.at(idx - 1)) / (times.at(idx) - times.at(idx - 1));
  return slerp(vals.at(idx - 1), vals.at(idx), t);
}

// Evaluates a keyframed value using linear interpolation.
template <typename T>
inline T keyframe_linear(
    const vector<float>& times, const vector<T>& vals, float time) {
  if (time <= times.front()) return vals.front();
  if (time >= times.back()) return vals.back();
  time     = clamp(time, times.front(), times.back() - 0.001f);
  auto idx = keyframe_index(times, time);
  auto t   = (time - times.at(idx - 1)) / (times.at(idx) - times.at(idx - 1));
  return vals.at(idx - 1) * (1 - t) + vals.at(idx) * t;
}

// Evaluates a keyframed value using Bezier interpolation.
template <typename T>
inline T keyframe_bezier(
    const vector<float>& times, const vector<T>& vals, float time) {
  if (time <= times.front()) return vals.front();
  if (time >= times.back()) return vals.back();
  time     = clamp(time, times.front(), times.back() - 0.001f);
  auto idx = keyframe_index(times, time);
  auto t   = (time - times.at(idx - 1)) / (times.at(idx) - times.at(idx - 1));
  return interpolate_bezier(
      vals.at(idx - 3), vals.at(idx - 2), vals.at(idx - 1), vals.at(idx), t);
}

}  // namespace yocto

// -----------------------------------------------------------------------------
// SCENE STATS AND VALIDATION
// -----------------------------------------------------------------------------
namespace yocto {

vector<string> scene_stats(const sceneio_model& scene, bool verbose) {
  auto accumulate = [](const auto& values, const auto& func) -> size_t {
    auto sum = (size_t)0;
    for (auto& value : values) sum += func(value);
    return sum;
  };
  auto format = [](auto num) {
    auto str = std::to_string(num);
    while (str.size() < 13) str = " " + str;
    return str;
  };
  auto format3 = [](auto num) {
    auto str = std::to_string(num.x) + " " + std::to_string(num.y) + " " +
               std::to_string(num.z);
    while (str.size() < 13) str = " " + str;
    return str;
  };

  auto bbox = compute_bounds(scene);

  auto stats = vector<string>{};
  stats.push_back("cameras:      " + format(scene.cameras.size()));
  stats.push_back("shapes:       " + format(scene.shapes.size()));
  stats.push_back("subdivs:      " + format(scene.subdivs.size()));
  stats.push_back("instances:    " + format(scene.instances.size()));
  stats.push_back("environments: " + format(scene.environments.size()));
  stats.push_back("textures:     " + format(scene.textures.size()));
  stats.push_back("materials:    " + format(scene.materials.size()));
  stats.push_back("nodes:        " + format(scene.nodes.size()));
  stats.push_back("animations:   " + format(scene.animations.size()));
  stats.push_back(
      "points:       " + format(accumulate(scene.shapes,
                             [](auto& shape) { return shape.points.size(); })));
  stats.push_back(
      "lines:        " + format(accumulate(scene.shapes,
                             [](auto& shape) { return shape.lines.size(); })));
  stats.push_back("triangles:    " +
                  format(accumulate(scene.shapes,
                      [](auto& shape) { return shape.triangles.size(); })));
  stats.push_back(
      "quads:        " + format(accumulate(scene.shapes,
                             [](auto& shape) { return shape.quads.size(); })));
  stats.push_back(
      "spoints:      " + format(accumulate(scene.subdivs,
                             [](auto& shape) { return shape.points.size(); })));
  stats.push_back(
      "slines:       " + format(accumulate(scene.subdivs,
                             [](auto& shape) { return shape.lines.size(); })));
  stats.push_back("striangles:   " +
                  format(accumulate(scene.subdivs,
                      [](auto& shape) { return shape.triangles.size(); })));
  stats.push_back(
      "squads:       " + format(accumulate(scene.subdivs,
                             [](auto& shape) { return shape.quads.size(); })));
  stats.push_back("sfvquads:     " +
                  format(accumulate(scene.subdivs,
                      [](auto& shape) { return shape.quadspos.size(); })));
  stats.push_back(
      "texels4b:     " + format(accumulate(scene.textures, [](auto& texture) {
        return (size_t)texture.ldr.size().x * (size_t)texture.ldr.size().x;
      })));
  stats.push_back(
      "texels4f:     " + format(accumulate(scene.textures, [](auto& texture) {
        return (size_t)texture.hdr.size().x * (size_t)texture.hdr.size().y;
      })));
  stats.push_back("center:       " + format3(center(bbox)));
  stats.push_back("size:         " + format3(size(bbox)));

  return stats;
}

// Checks for validity of the scene.
vector<string> scene_validation(const sceneio_model& scene, bool notextures) {
  auto errs        = vector<string>();
  auto check_names = [&errs](const auto& vals, const string& base) {
    auto used = unordered_map<string, int>();
    used.reserve(vals.size());
    for (auto& value : vals) used[value.name] += 1;
    for (auto& [name, used] : used) {
      if (name == "") {
        errs.push_back("empty " + base + " name");
      } else if (used > 1) {
        errs.push_back("duplicated " + base + " name " + name);
      }
    }
  };
  auto check_empty_textures = [&errs](const vector<sceneio_texture>& vals) {
    for (auto& value : vals) {
      if (value.hdr.empty() && value.ldr.empty()) {
        errs.push_back("empty texture " + value.name);
      }
    }
  };

  check_names(scene.cameras, "camera");
  check_names(scene.shapes, "shape");
  check_names(scene.textures, "texture");
  check_names(scene.materials, "material");
  check_names(scene.instances, "instance");
  check_names(scene.environments, "environment");
  check_names(scene.nodes, "node");
  check_names(scene.animations, "animation");
  if (!notextures) check_empty_textures(scene.textures);

  return errs;
}

}  // namespace yocto

// -----------------------------------------------------------------------------
// SCENE UTILITIES
// -----------------------------------------------------------------------------
namespace yocto {

// Updates the scene and scene's instances bounding boxes
bbox3f compute_bounds(const sceneio_model& scene) {
  auto shape_bbox = vector<bbox3f>{};
  for (auto& shape : scene.shapes) {
    auto& sbvh = shape_bbox.emplace_back();
    sbvh       = invalidb3f;
    for (auto p : shape.positions) sbvh = merge(sbvh, p);
  }
  auto bbox = invalidb3f;
  for (auto& instance : scene.instances) {
    bbox = merge(
        bbox, transform_bbox(instance.frame, shape_bbox[instance.shape]));
  }
  return bbox;
}

// Add missing cameras.
void add_cameras(sceneio_model& scene) {
  if (!scene.cameras.empty()) return;
  auto& camera = scene.cameras.emplace_back();
  camera.name  = "default";
  // TODO: error in camera.lens and camera.film
  camera.orthographic = false;
  camera.film         = 0.036;
  camera.aperture     = 0;
  camera.lens         = 0.050;
  auto bbox           = compute_bounds(scene);
  auto center         = (bbox.max + bbox.min) / 2;
  auto bbox_radius    = length(bbox.max - bbox.min) / 2;
  auto camera_dir     = camera.frame.o - center;
  if (camera_dir == zero3f) camera_dir = {0, 0, 1};
  auto camera_dist = bbox_radius / camera.film;
  auto from        = camera_dir * (camera_dist * 1) + center;
  auto to          = center;
  auto up          = vec3f{0, 1, 0};
  camera.frame     = lookat_frame(from, to, up);
  camera.focus     = length(from - to);
}

// Add missing materials.
void add_materials(sceneio_model& scene) {
  auto material_id = -1;
  for (auto& instance : scene.instances) {
    if (instance.material >= 0) continue;
    if (material_id < 0) {
      auto material    = sceneio_material{};
      material.name    = "default";
      material.diffuse = {0.2f, 0.2f, 0.2f};
      scene.materials.push_back(material);
      material_id = (int)scene.materials.size() - 1;
    }
    instance.material = material_id;
  }
}

// Add missing radius.
void add_radius(sceneio_model& scene, float radius = 0.001f) {
  for (auto& shape : scene.shapes) {
    if (shape.points.empty() && shape.lines.empty()) continue;
    if (!shape.radius.empty()) continue;
    shape.radius.assign(shape.positions.size(), radius);
  }
}

// Add a sky environment
void add_sky(sceneio_model& scene, float sun_angle) {
  auto texture     = sceneio_texture{};
  texture.name     = "sky";
  texture.filename = "textures/sky.hdr";
  texture.hdr      = make_sunsky({1024, 512}, sun_angle);
  scene.textures.push_back(texture);
  auto environment         = sceneio_environment{};
  environment.name         = "sky";
  environment.emission     = {1, 1, 1};
  environment.emission_tex = (int)scene.textures.size() - 1;
  scene.environments.push_back(environment);
}

// Reduce memory usage
void trim_memory(sceneio_model& scene) {
  for (auto& shape : scene.shapes) {
    shape.points.shrink_to_fit();
    shape.lines.shrink_to_fit();
    shape.triangles.shrink_to_fit();
    shape.quads.shrink_to_fit();
    shape.positions.shrink_to_fit();
    shape.normals.shrink_to_fit();
    shape.texcoords.shrink_to_fit();
    shape.colors.shrink_to_fit();
    shape.radius.shrink_to_fit();
    shape.tangents.shrink_to_fit();
  }
  for (auto& subdiv : scene.subdivs) {
    subdiv.points.shrink_to_fit();
    subdiv.lines.shrink_to_fit();
    subdiv.triangles.shrink_to_fit();
    subdiv.quads.shrink_to_fit();
    subdiv.quadspos.shrink_to_fit();
    subdiv.quadsnorm.shrink_to_fit();
    subdiv.quadstexcoord.shrink_to_fit();
    subdiv.positions.shrink_to_fit();
    subdiv.normals.shrink_to_fit();
    subdiv.texcoords.shrink_to_fit();
    subdiv.colors.shrink_to_fit();
    subdiv.radius.shrink_to_fit();
    subdiv.tangents.shrink_to_fit();
  }
  for (auto& texture : scene.textures) {
    texture.ldr.shrink_to_fit();
    texture.hdr.shrink_to_fit();
  }
  scene.cameras.shrink_to_fit();
  scene.shapes.shrink_to_fit();
  scene.instances.shrink_to_fit();
  scene.materials.shrink_to_fit();
  scene.textures.shrink_to_fit();
  scene.environments.shrink_to_fit();
  scene.nodes.shrink_to_fit();
  scene.animations.shrink_to_fit();
}

// Apply subdivision and displacement rules.
sceneio_subdiv subdivide_subdiv(const sceneio_subdiv& shape) {
  using std::ignore;
  if (!shape.subdivisions) return shape;
  auto tesselated         = shape;
  tesselated.subdivisions = 0;
  if (!shape.points.empty()) {
    throw std::runtime_error("point subdivision not supported");
  } else if (!shape.lines.empty()) {
    tie(ignore, tesselated.normals) = subdivide_lines(
        tesselated.lines, tesselated.normals, shape.subdivisions);
    tie(ignore, tesselated.texcoords) = subdivide_lines(
        tesselated.lines, tesselated.texcoords, shape.subdivisions);
    tie(ignore, tesselated.colors) = subdivide_lines(
        tesselated.lines, tesselated.colors, shape.subdivisions);
    tie(ignore, tesselated.radius) = subdivide_lines(
        tesselated.lines, tesselated.radius, shape.subdivisions);
    tie(tesselated.lines, tesselated.positions) = subdivide_lines(
        tesselated.lines, tesselated.positions, shape.subdivisions);
    if (shape.smooth)
      tesselated.normals = compute_tangents(
          tesselated.lines, tesselated.positions);
  } else if (!shape.triangles.empty()) {
    tie(ignore, tesselated.normals) = subdivide_triangles(
        tesselated.triangles, tesselated.normals, shape.subdivisions);
    tie(ignore, tesselated.texcoords) = subdivide_triangles(
        tesselated.triangles, tesselated.texcoords, shape.subdivisions);
    tie(ignore, tesselated.colors) = subdivide_triangles(
        tesselated.triangles, tesselated.colors, shape.subdivisions);
    tie(ignore, tesselated.radius) = subdivide_triangles(
        tesselated.triangles, tesselated.radius, shape.subdivisions);
    tie(tesselated.triangles, tesselated.positions) = subdivide_triangles(
        tesselated.triangles, tesselated.positions, shape.subdivisions);
    if (shape.smooth)
      tesselated.normals = compute_normals(
          tesselated.triangles, tesselated.positions);
  } else if (!shape.quads.empty() && !shape.catmullclark) {
    tie(ignore, tesselated.normals) = subdivide_quads(
        tesselated.quads, tesselated.normals, shape.subdivisions);
    tie(ignore, tesselated.texcoords) = subdivide_quads(
        tesselated.quads, tesselated.texcoords, shape.subdivisions);
    tie(ignore, tesselated.colors) = subdivide_quads(
        tesselated.quads, tesselated.colors, shape.subdivisions);
    tie(ignore, tesselated.radius) = subdivide_quads(
        tesselated.quads, tesselated.radius, shape.subdivisions);
    tie(tesselated.quads, tesselated.positions) = subdivide_quads(
        tesselated.quads, tesselated.positions, shape.subdivisions);
    if (tesselated.smooth)
      tesselated.normals = compute_normals(
          tesselated.quads, tesselated.positions);
  } else if (!shape.quads.empty() && shape.catmullclark) {
    tie(ignore, tesselated.normals) = subdivide_catmullclark(
        tesselated.quads, tesselated.normals, shape.subdivisions);
    tie(ignore, tesselated.texcoords) = subdivide_catmullclark(
        tesselated.quads, tesselated.texcoords, shape.subdivisions);
    tie(ignore, tesselated.colors) = subdivide_catmullclark(
        tesselated.quads, tesselated.colors, shape.subdivisions);
    tie(ignore, tesselated.radius) = subdivide_catmullclark(
        tesselated.quads, tesselated.radius, shape.subdivisions);
    tie(tesselated.quads, tesselated.positions) = subdivide_catmullclark(
        tesselated.quads, tesselated.positions, shape.subdivisions);
    if (tesselated.smooth)
      tesselated.normals = compute_normals(
          tesselated.quads, tesselated.positions);
  } else if (!shape.quadspos.empty() && !shape.catmullclark) {
    std::tie(tesselated.quadsnorm, tesselated.normals) = subdivide_quads(
        tesselated.quadsnorm, tesselated.normals, shape.subdivisions);
    std::tie(tesselated.quadstexcoord, tesselated.texcoords) = subdivide_quads(
        tesselated.quadstexcoord, tesselated.texcoords, shape.subdivisions);
    std::tie(tesselated.quadspos, tesselated.positions) = subdivide_quads(
        tesselated.quadspos, tesselated.positions, shape.subdivisions);
    if (tesselated.smooth) {
      tesselated.normals = compute_normals(
          tesselated.quadspos, tesselated.positions);
      tesselated.quadsnorm = tesselated.quadspos;
    }
  } else if (!shape.quadspos.empty() && shape.catmullclark) {
    std::tie(tesselated.quadstexcoord, tesselated.texcoords) =
        subdivide_catmullclark(tesselated.quadstexcoord, tesselated.texcoords,
            shape.subdivisions, true);
    std::tie(tesselated.quadsnorm, tesselated.normals) = subdivide_catmullclark(
        tesselated.quadsnorm, tesselated.normals, shape.subdivisions, true);
    std::tie(tesselated.quadspos, tesselated.positions) =
        subdivide_catmullclark(
            tesselated.quadspos, tesselated.positions, shape.subdivisions);
    if (shape.smooth) {
      tesselated.normals = compute_normals(
          tesselated.quadspos, tesselated.positions);
      tesselated.quadsnorm = tesselated.quadspos;
    } else {
      tesselated.normals   = {};
      tesselated.quadsnorm = {};
    }
  } else {
    throw std::runtime_error("empty shape");
  }
  return tesselated;
}
// Apply displacement to a shape
sceneio_subdiv displace_subdiv(
    const sceneio_model& scene, const sceneio_subdiv& subdiv) {
  // Evaluate a texture
  auto eval_texture = [](const sceneio_texture& texture,
                          const vec2f&          texcoord) -> vec4f {
    if (!texture.hdr.empty()) {
      return eval_image(texture.hdr, texcoord, false, false);
    } else if (!texture.ldr.empty()) {
      return eval_image(texture.ldr, texcoord, true, false, false);
    } else {
      return {1, 1, 1, 1};
    }
  };

  if (!subdiv.displacement || subdiv.displacement_tex < 0) return subdiv;
  auto& displacement = scene.textures[subdiv.displacement_tex];
  if (subdiv.texcoords.empty()) {
    throw std::runtime_error("missing texture coordinates");
    return {};
  }

  auto displaced             = subdiv;
  displaced.displacement     = 0;
  displaced.displacement_tex = -1;

  // simple case
  if (!subdiv.triangles.empty()) {
    auto normals = subdiv.normals;
    if (normals.empty())
      normals = compute_normals(subdiv.triangles, subdiv.positions);
    for (auto vid = 0; vid < subdiv.positions.size(); vid++) {
      auto disp = mean(xyz(eval_texture(displacement, subdiv.texcoords[vid])));
      if (!is_hdr_filename(displacement.filename)) disp -= 0.5f;
      displaced.positions[vid] += normals[vid] * subdiv.displacement * disp;
    }
    if (subdiv.smooth || !subdiv.normals.empty()) {
      displaced.normals = compute_normals(
          displaced.triangles, displaced.positions);
    }
  } else if (!subdiv.quads.empty()) {
    auto normals = subdiv.normals;
    if (normals.empty())
      normals = compute_normals(subdiv.triangles, subdiv.positions);
    for (auto vid = 0; vid < subdiv.positions.size(); vid++) {
      auto disp = mean(xyz(eval_texture(displacement, subdiv.texcoords[vid])));
      if (!is_hdr_filename(displacement.filename)) disp -= 0.5f;
      displaced.positions[vid] += normals[vid] * subdiv.displacement * disp;
    }
    if (subdiv.smooth || !subdiv.normals.empty()) {
      displaced.normals = compute_normals(displaced.quads, displaced.positions);
    }
  } else if (!subdiv.quadspos.empty()) {
    // facevarying case
    auto offset = vector<float>(subdiv.positions.size(), 0);
    auto count  = vector<int>(subdiv.positions.size(), 0);
    for (auto fid = 0; fid < subdiv.quadspos.size(); fid++) {
      auto qpos = subdiv.quadspos[fid];
      auto qtxt = subdiv.quadstexcoord[fid];
      for (auto i = 0; i < 4; i++) {
        auto disp = mean(
            xyz(eval_texture(displacement, subdiv.texcoords[qtxt[i]])));
        if (!is_hdr_filename(displacement.filename)) disp -= 0.5f;
        offset[qpos[i]] += subdiv.displacement * disp;
        count[qpos[i]] += 1;
      }
    }
    auto normals = compute_normals(subdiv.quadspos, subdiv.positions);
    for (auto vid = 0; vid < subdiv.positions.size(); vid++) {
      displaced.positions[vid] += normals[vid] * offset[vid] / count[vid];
    }
    if (subdiv.smooth || !subdiv.normals.empty()) {
      displaced.quadsnorm = subdiv.quadspos;
      displaced.normals   = compute_normals(
          displaced.quadspos, displaced.positions);
    }
  }
  return displaced;
}

void tesselate_subdiv(
    sceneio_model& scene, const sceneio_subdiv& subdiv, bool no_quads) {
  auto tesselated = subdiv;
  if (tesselated.subdivisions) tesselated = subdivide_subdiv(tesselated);
  if (tesselated.displacement) tesselated = displace_subdiv(scene, tesselated);
  if (!subdiv.quadspos.empty()) {
    std::tie(tesselated.quads, tesselated.positions, tesselated.normals,
        tesselated.texcoords) = split_facevarying(tesselated.quadspos,
        tesselated.quadsnorm, tesselated.quadstexcoord, tesselated.positions,
        tesselated.normals, tesselated.texcoords);
  }
  if (!tesselated.quads.empty() && no_quads) {
    tesselated.triangles = quads_to_triangles(tesselated.quads);
    tesselated.quads     = {};
  }
  auto& shape     = scene.shapes[tesselated.shape];
  shape.points    = tesselated.points;
  shape.lines     = tesselated.lines;
  shape.triangles = tesselated.triangles;
  shape.quads     = tesselated.quads;
  shape.positions = tesselated.positions;
  shape.normals   = tesselated.normals;
  shape.texcoords = tesselated.texcoords;
  shape.colors    = tesselated.colors;
  shape.radius    = tesselated.radius;
}

// Update animation transforms
void update_transforms(sceneio_model& scene, sceneio_animation& animation,
    float time, const string& anim_group) {
  if (anim_group != "" && anim_group != animation.group) return;

  if (!animation.translations.empty()) {
    auto value = vec3f{0, 0, 0};
    switch (animation.interpolation) {
      case sceneio_animation::interpolation_type::step:
        value = keyframe_step(animation.times, animation.translations, time);
        break;
      case sceneio_animation::interpolation_type::linear:
        value = keyframe_linear(animation.times, animation.translations, time);
        break;
      case sceneio_animation::interpolation_type::bezier:
        value = keyframe_bezier(animation.times, animation.translations, time);
        break;
      default: throw std::runtime_error("should not have been here");
    }
    for (auto target : animation.targets)
      scene.nodes[target].translation = value;
  }
  if (!animation.rotations.empty()) {
    auto value = vec4f{0, 0, 0, 1};
    switch (animation.interpolation) {
      case sceneio_animation::interpolation_type::step:
        value = keyframe_step(animation.times, animation.rotations, time);
        break;
      case sceneio_animation::interpolation_type::linear:
        value = keyframe_linear(animation.times, animation.rotations, time);
        break;
      case sceneio_animation::interpolation_type::bezier:
        value = keyframe_bezier(animation.times, animation.rotations, time);
        break;
    }
    for (auto target : animation.targets) scene.nodes[target].rotation = value;
  }
  if (!animation.scales.empty()) {
    auto value = vec3f{1, 1, 1};
    switch (animation.interpolation) {
      case sceneio_animation::interpolation_type::step:
        value = keyframe_step(animation.times, animation.scales, time);
        break;
      case sceneio_animation::interpolation_type::linear:
        value = keyframe_linear(animation.times, animation.scales, time);
        break;
      case sceneio_animation::interpolation_type::bezier:
        value = keyframe_bezier(animation.times, animation.scales, time);
        break;
    }
    for (auto target : animation.targets) scene.nodes[target].scale = value;
  }
}

// Update node transforms
void update_transforms(sceneio_model& scene, sceneio_node& node,
    const frame3f& parent = identity3x4f) {
  auto frame = parent * node.local * translation_frame(node.translation) *
               rotation_frame(node.rotation) * scaling_frame(node.scale);
  if (node.instance >= 0) scene.instances[node.instance].frame = frame;
  if (node.camera >= 0) scene.cameras[node.camera].frame = frame;
  if (node.environment >= 0) scene.environments[node.environment].frame = frame;
  for (auto child : node.children)
    update_transforms(scene, scene.nodes[child], frame);
}

// Update node transforms
void update_transforms(
    sceneio_model& scene, float time, const string& anim_group) {
  for (auto& agr : scene.animations)
    update_transforms(scene, agr, time, anim_group);
  for (auto& node : scene.nodes) node.children.clear();
  for (auto node_id = 0; node_id < scene.nodes.size(); node_id++) {
    auto& node = scene.nodes[node_id];
    if (node.parent >= 0) scene.nodes[node.parent].children.push_back(node_id);
  }
  for (auto& node : scene.nodes)
    if (node.parent < 0) update_transforms(scene, node);
}

}  // namespace yocto

// -----------------------------------------------------------------------------
// GENERIC SCENE LOADING
// -----------------------------------------------------------------------------
namespace yocto {

// Helpers for throwing
static void throw_format_error(const string& filename) {
  throw std::runtime_error{filename + ": unknown format"};
}
static void throw_dependent_error(const string& filename, const string& err) {
  throw std::runtime_error{filename + ": error in resource (" + err + ")"};
}
static void throw_emptyshape_error(const string& filename, const string& name) {
  throw std::runtime_error{filename + ": empty shape " + name};
}
static void throw_missing_reference_error(
    const string& filename, const string& type, const string& name) {
  throw std::runtime_error{filename + ": missing " + type + " " + name};
}

// Load/save a scene in the builtin YAML format.
static void load_yaml_scene(
    const string& filename, sceneio_model& scene, bool noparallel);
static void save_yaml_scene(
    const string& filename, const sceneio_model& scene, bool noparallel);

// Load/save a scene from/to OBJ.
static void load_obj_scene(
    const string& filename, sceneio_model& scene, bool noparallel);
static void save_obj_scene(const string& filename, const sceneio_model& scene,
    bool instances, bool noparallel);

// Load/save a scene from/to PLY. Loads/saves only one mesh with no other data.
static void load_ply_scene(
    const string& filename, sceneio_model& scene, bool noparallel);
static void save_ply_scene(
    const string& filename, const sceneio_model& scene, bool noparallel);

// Load/save a scene from/to glTF.
static void load_gltf_scene(
    const string& filename, sceneio_model& scene, bool noparallel);

// Load/save a scene from/to pbrt. This is not robust at all and only
// works on scene that have been previously adapted since the two renderers
// are too different to match.
static void load_pbrt_scene(
    const string& filename, sceneio_model& scene, bool noparallel);
static void save_pbrt_scene(
    const string& filename, const sceneio_model& scene, bool noparallel);

// Load a scene
void load_scene(const string& filename, sceneio_model& scene, bool noparallel) {
  auto ext = get_extension(filename);
  if (ext == ".yaml" || ext == ".YAML") {
    return load_yaml_scene(filename, scene, noparallel);
  } else if (ext == ".obj" || ext == ".OBJ") {
    return load_obj_scene(filename, scene, noparallel);
  } else if (ext == ".gltf" || ext == ".GLTF") {
    return load_gltf_scene(filename, scene, noparallel);
  } else if (ext == ".pbrt" || ext == ".PBRT") {
    return load_pbrt_scene(filename, scene, noparallel);
  } else if (ext == ".ply" || ext == ".PLY") {
    return load_ply_scene(filename, scene, noparallel);
  } else {
    scene = {};
    throw_format_error(filename);
  }
}

// Save a scene
void save_scene(
    const string& filename, const sceneio_model& scene, bool noparallel) {
  auto ext = get_extension(filename);
  if (ext == ".yaml" || ext == ".YAML") {
    return save_yaml_scene(filename, scene, noparallel);
  } else if (ext == ".obj" || ext == ".OBJ") {
    return save_obj_scene(filename, scene, false, noparallel);
  } else if (ext == ".pbrt" || ext == ".PBRT") {
    return save_pbrt_scene(filename, scene, noparallel);
  } else if (ext == ".ply" || ext == ".PLY") {
    return save_ply_scene(filename, scene, noparallel);
  } else {
    throw_format_error(filename);
  }
}

void load_texture(const string& filename, sceneio_texture& texture) {
  try {
    if (is_hdr_filename(texture.filename)) {
      load_image(get_dirname(filename) + texture.filename, texture.hdr);
    } else {
      load_imageb(get_dirname(filename) + texture.filename, texture.ldr);
    }
  } catch (std::exception& e) {
    throw_dependent_error(filename, e.what());
  }
}

void save_texture(const string& filename, const sceneio_texture& texture) {
  try {
    if (!texture.hdr.empty()) {
      save_image(get_dirname(filename) + texture.filename, texture.hdr);
    } else {
      save_imageb(get_dirname(filename) + texture.filename, texture.ldr);
    }
  } catch (std::exception& e) {
    throw_dependent_error(filename, e.what());
  }
}

void load_textures(
    const string& filename, sceneio_model& scene, bool noparallel) {
  // load images
  if (noparallel) {
    for (auto& texture : scene.textures) {
      if (!texture.hdr.empty() || !texture.ldr.empty()) continue;
      load_texture(filename, texture);
    }
  } else {
    parallel_foreach(scene.textures, [filename](sceneio_texture& texture) {
      if (!texture.hdr.empty() || !texture.ldr.empty()) return;
      load_texture(filename, texture);
    });
  }
}

// helper to save textures
void save_textures(
    const string& filename, const sceneio_model& scene, bool noparallel) {
  // save images
  if (noparallel) {
    for (auto& texture : scene.textures) {
      save_texture(filename, texture);
    }
  } else {
    parallel_foreach(
        scene.textures, [filename](const sceneio_texture& texture) {
          save_texture(filename, texture);
        });
  }
}

void load_shape(const string& filename, sceneio_shape& shape) {
  try {
    load_shape(get_dirname(filename) + shape.filename, shape.points,
        shape.lines, shape.triangles, shape.quads, shape.positions,
        shape.normals, shape.texcoords, shape.colors, shape.radius);
  } catch (std::exception& e) {
    throw_dependent_error(filename, e.what());
  }
}

void save_shape(const string& filename, const sceneio_shape& shape) {
  try {
    save_shape(get_dirname(filename) + shape.filename, shape.points,
        shape.lines, shape.triangles, shape.quads, shape.positions,
        shape.normals, shape.texcoords, shape.colors, shape.radius);
  } catch (std::exception& e) {
    throw_dependent_error(filename, e.what());
  }
}

// Load json meshes
void load_shapes(
    const string& filename, sceneio_model& scene, bool noparallel) {
  // load shapes
  if (noparallel) {
    for (auto& shape : scene.shapes) {
      if (!shape.positions.empty()) continue;
      load_shape(filename, shape);
    }
  } else {
    parallel_foreach(scene.shapes, [filename](sceneio_shape& shape) {
      if (!shape.positions.empty()) return;
      load_shape(filename, shape);
    });
  }
}

// Save json meshes
void save_shapes(
    const string& filename, const sceneio_model& scene, bool noparallel) {
  // save shapes
  if (noparallel) {
    for (auto& shape : scene.shapes) {
      save_shape(filename, shape);
    }
  } else {
    parallel_foreach(scene.shapes, [&filename](const sceneio_shape& shape) {
      save_shape(filename, shape);
    });
  }
}

void load_subdiv(const string& filename, sceneio_subdiv& subdiv) {
  try {
    if (!subdiv.facevarying) {
      load_shape(get_dirname(filename) + subdiv.filename, subdiv.points,
          subdiv.lines, subdiv.triangles, subdiv.quads, subdiv.positions,
          subdiv.normals, subdiv.texcoords, subdiv.colors, subdiv.radius);
    } else {
      load_fvshape(get_dirname(filename) + subdiv.filename, subdiv.quadspos,
          subdiv.quadsnorm, subdiv.quadstexcoord, subdiv.positions,
          subdiv.normals, subdiv.texcoords);
    }
  } catch (std::exception& e) {
    throw_dependent_error(filename, e.what());
  }
}

void save_subdiv(const string& filename, const sceneio_subdiv& subdiv) {
  try {
    if (subdiv.quadspos.empty()) {
      save_shape(get_dirname(filename) + subdiv.filename, subdiv.points,
          subdiv.lines, subdiv.triangles, subdiv.quads, subdiv.positions,
          subdiv.normals, subdiv.texcoords, subdiv.colors, subdiv.radius);
    } else {
      save_fvshape(get_dirname(filename) + subdiv.filename, subdiv.quadspos,
          subdiv.quadsnorm, subdiv.quadstexcoord, subdiv.positions,
          subdiv.normals, subdiv.texcoords);
    }
  } catch (std::exception& e) {
    throw_dependent_error(filename, e.what());
  }
}

// Load json meshes
void load_subdivs(
    const string& filename, sceneio_model& scene, bool noparallel) {
  // load shapes
  if (noparallel) {
    for (auto& subdiv : scene.subdivs) {
      if (!subdiv.positions.empty()) continue;
      load_subdiv(filename, subdiv);
    }
  } else {
    parallel_foreach(scene.subdivs, [filename](sceneio_subdiv& subdiv) {
      if (!subdiv.positions.empty()) return;
      load_subdiv(filename, subdiv);
    });
  }
}

// Save json meshes
void save_subdivs(
    const string& filename, const sceneio_model& scene, bool noparallel) {
  // save shapes
  if (noparallel) {
    for (auto& subdiv : scene.subdivs) {
      save_subdiv(filename, subdiv);
    }
  } else {
    parallel_foreach(scene.subdivs, [&filename](const sceneio_subdiv& subdiv) {
      save_subdiv(filename, subdiv);
    });
  }
}

// create and cleanup names and filenames
static string make_safe_name(
    const string& name_, const string& base, int count) {
  auto name = name_;
  if (name.empty()) name = base + std::to_string(count);
  if (name.front() == '-') name = "_" + name;
  if (name.front() >= '0' && name.front() <= '9') name = "_" + name;
  for (auto& c : name) {
    if (c == '-' || c == '_') continue;
    if (c >= '0' && c <= '9') continue;
    if (c >= 'a' && c <= 'z') continue;
    if (c >= 'A' && c <= 'Z') continue;
    c = '_';
  }
  std::transform(name.begin(), name.end(), name.begin(),
      [](unsigned char c) { return std::tolower(c); });
  return name;
}
static inline string make_safe_filename(const string& filename_) {
  auto filename = filename_;
  for (auto& c : filename) {
    if (c == ' ') c = '_';
  }
  return filename;
}

}  // namespace yocto

// -----------------------------------------------------------------------------
// YAML SUPPORT
// -----------------------------------------------------------------------------
namespace yocto {

static bool make_image_preset(
    image<vec4f>& hdr, image<vec4b>& ldr, const string& type) {
  if (type.find("sky") == type.npos) {
    auto imgf = make_image_preset(type);
    if (imgf.empty()) return false;
    if (type.find("-normal") == type.npos &&
        type.find("-displacement") == type.npos) {
      ldr = rgb_to_srgbb(imgf);
    } else {
      ldr = float_to_byte(imgf);
    }
    return true;
  } else {
    hdr = make_image_preset(type);
    return true;
  }
}

#if 1

void load_yaml(const string& filename, sceneio_model& scene, bool noparallel) {
  // open file
  auto yaml = yaml_model{};
  load_yaml(filename, yaml);

  auto tmap = unordered_map<string, int>{{"", -1}};
  auto vmap = unordered_map<string, int>{{"", -1}};
  auto mmap = unordered_map<string, int>{{"", -1}};
  auto smap = unordered_map<string, int>{{"", -1}};

  // parse yaml reference
  auto get_yaml_ref = [](const yaml_element& yelment, const string& name,
                          int& value, const unordered_map<string, int>& refs) {
    auto ref = ""s;
    get_yaml_value(yelment, name, ref);
    if (ref == "") {
      value = -1;
    } else {
      if (refs.find(ref) == refs.end())
        throw std::invalid_argument{"missing reference to " + ref};
      value = refs.at(ref);
    }
  };

  // hacked groups for large models
  struct sceneio_group {
    string          filename = "";
    vector<frame3f> frames   = {};
  };
  auto groups  = vector<sceneio_group>{};
  auto igroups = vector<int>{};

  // check for conversion errors
  try {
    // cameras
    for (auto& yelement : yaml.elements) {
      if (yelement.name == "cameras") {
        auto& camera = scene.cameras.emplace_back();
        get_yaml_value(yelement, "name", camera.name);
        get_yaml_value(yelement, "uri", camera.name);
        get_yaml_value(yelement, "frame", camera.frame);
        get_yaml_value(yelement, "orthographic", camera.orthographic);
        get_yaml_value(yelement, "lens", camera.lens);
        get_yaml_value(yelement, "aspect", camera.aspect);
        get_yaml_value(yelement, "film", camera.film);
        get_yaml_value(yelement, "focus", camera.focus);
        get_yaml_value(yelement, "aperture", camera.aperture);
        if (has_yaml_value(yelement, "uri")) {
          auto uri = ""s;
          get_yaml_value(yelement, "uri", uri);
          camera.name = get_basename(uri);
        }
        if (has_yaml_value(yelement, "lookat")) {
          auto lookat = identity3x3f;
          get_yaml_value(yelement, "lookat", lookat);
          camera.frame = lookat_frame(lookat.x, lookat.y, lookat.z);
          camera.focus = length(lookat.x - lookat.y);
        }
      } else if (yelement.name == "textures") {
        auto& texture = scene.textures.emplace_back();
        get_yaml_value(yelement, "name", texture.name);
        get_yaml_value(yelement, "filename", texture.filename);
        if (has_yaml_value(yelement, "preset")) {
          auto preset = ""s;
          get_yaml_value(yelement, "preset", preset);
          make_image_preset(texture.hdr, texture.ldr, preset);
          if (texture.filename.empty()) {
            texture.filename = "textures/ypreset-" + preset +
                               (texture.hdr.empty() ? ".png" : ".hdr");
          }
        }
        if (has_yaml_value(yelement, "uri")) {
          get_yaml_value(yelement, "uri", texture.filename);
          texture.name           = get_basename(texture.filename);
          tmap[texture.filename] = (int)scene.textures.size() - 1;
        }
        tmap[texture.name] = (int)scene.textures.size() - 1;
      } else if (yelement.name == "materials") {
        auto& material = scene.materials.emplace_back();
        get_yaml_value(yelement, "name", material.name);
        get_yaml_value(yelement, "emission", material.emission);
        get_yaml_value(yelement, "diffuse", material.diffuse);
        get_yaml_value(yelement, "metallic", material.metallic);
        get_yaml_value(yelement, "specular", material.specular);
        get_yaml_value(yelement, "roughness", material.roughness);
        get_yaml_value(yelement, "coat", material.coat);
        get_yaml_value(yelement, "transmission", material.transmission);
        get_yaml_value(yelement, "refract", material.refract);
        get_yaml_value(yelement, "voltransmission", material.voltransmission);
        get_yaml_value(yelement, "volmeanfreepath", material.volmeanfreepath);
        get_yaml_value(yelement, "volscatter", material.volscatter);
        get_yaml_value(yelement, "volemission", material.volemission);
        get_yaml_value(yelement, "volanisotropy", material.volanisotropy);
        get_yaml_value(yelement, "volscale", material.volscale);
        get_yaml_value(yelement, "opacity", material.opacity);
        get_yaml_value(yelement, "coat", material.coat);
        get_yaml_ref(yelement, "emission_tex", material.emission_tex, tmap);
        get_yaml_ref(yelement, "diffuse_tex", material.diffuse_tex, tmap);
        get_yaml_ref(yelement, "metallic_tex", material.metallic_tex, tmap);
        get_yaml_ref(yelement, "specular_tex", material.specular_tex, tmap);
        get_yaml_ref(
            yelement, "transmission_tex", material.transmission_tex, tmap);
        get_yaml_ref(yelement, "roughness_tex", material.roughness_tex, tmap);
        get_yaml_ref(yelement, "subsurface_tex", material.subsurface_tex, tmap);
        get_yaml_ref(yelement, "normal_tex", material.normal_tex, tmap);
        get_yaml_ref(yelement, "normal_tex", material.normal_tex, tmap);
        get_yaml_value(yelement, "gltf_textures", material.gltf_textures);
        if (has_yaml_value(yelement, "uri")) {
          get_yaml_value(yelement, "uri", material.name);
          mmap[material.name] = (int)scene.materials.size() - 1;
          material.name       = get_basename(material.name);
        }
        mmap[material.name] = (int)scene.materials.size() - 1;
      } else if (yelement.name == "shapes") {
        auto& shape = scene.shapes.emplace_back();
        get_yaml_value(yelement, "name", shape.name);
        get_yaml_value(yelement, "filename", shape.filename);
        if (has_yaml_value(yelement, "uri")) {
          get_yaml_value(yelement, "uri", shape.filename);
          shape.name           = get_basename(shape.filename);
          smap[shape.filename] = (int)scene.shapes.size() - 1;
        }
        if (has_yaml_value(yelement, "preset")) {
          auto preset = ""s;
          get_yaml_value(yelement, "preset", preset);
          make_shape_preset(shape.points, shape.lines, shape.triangles,
              shape.quads, shape.positions, shape.normals, shape.texcoords,
              shape.colors, shape.radius, preset);
          if (shape.filename.empty()) {
            shape.filename = "shapes/ypreset-" + preset + ".yvol";
          }
        }
        smap[shape.name] = (int)scene.shapes.size() - 1;
      } else if (yelement.name == "subdivs") {
        auto& subdiv = scene.subdivs.emplace_back();
        get_yaml_value(yelement, "name", subdiv.name);
        get_yaml_value(yelement, "filename", subdiv.filename);
        get_yaml_ref(yelement, "shape", subdiv.shape, smap);
        get_yaml_value(yelement, "subdivisions", subdiv.subdivisions);
        get_yaml_value(yelement, "catmullclark", subdiv.catmullclark);
        get_yaml_value(yelement, "smooth", subdiv.smooth);
        get_yaml_value(yelement, "facevarying", subdiv.facevarying);
        get_yaml_ref(
            yelement, "displacement_tex", subdiv.displacement_tex, tmap);
        get_yaml_value(yelement, "displacement", subdiv.displacement);
        if (has_yaml_value(yelement, "uri")) {
          get_yaml_value(yelement, "uri", subdiv.filename);
          subdiv.name = get_basename(subdiv.filename);
        }
        if (has_yaml_value(yelement, "preset")) {
          auto preset = ""s;
          get_yaml_value(yelement, "preset", preset);
          make_shape_preset(subdiv.points, subdiv.lines, subdiv.triangles,
              subdiv.quads, subdiv.quadspos, subdiv.quadsnorm,
              subdiv.quadstexcoord, subdiv.positions, subdiv.normals,
              subdiv.texcoords, subdiv.colors, subdiv.radius, preset);
          if (subdiv.filename.empty()) {
            subdiv.filename = "shapes/ypreset-" + preset + ".yvol";
          }
        }
      } else if (yelement.name == "instances") {
        auto& instance = scene.instances.emplace_back();
        get_yaml_value(yelement, "name", instance.name);
        get_yaml_value(yelement, "frame", instance.frame);
        get_yaml_ref(yelement, "shape", instance.shape, smap);
        get_yaml_ref(yelement, "material", instance.material, mmap);
        if (has_yaml_value(yelement, "uri")) {
          auto uri = ""s;
          get_yaml_value(yelement, "uri", uri);
          instance.name = get_basename(uri);
        }
        if (has_yaml_value(yelement, "lookat")) {
          auto lookat = identity3x3f;
          get_yaml_value(yelement, "lookat", lookat);
          instance.frame = lookat_frame(lookat.x, lookat.y, lookat.z, true);
        }
        if (has_yaml_value(yelement, "instances")) {
          auto& group = groups.emplace_back();
          get_yaml_value(yelement, "instances", group.filename);
          while (igroups.size() < scene.instances.size())
            igroups.emplace_back() = -1;
          igroups.back() = (int)groups.size() - 1;
        }
      } else if (yelement.name == "environments") {
        auto& environment = scene.environments.emplace_back();
        get_yaml_value(yelement, "name", environment.name);
        get_yaml_value(yelement, "frame", environment.frame);
        get_yaml_value(yelement, "emission", environment.emission);
        get_yaml_ref(yelement, "emission_tex", environment.emission_tex, tmap);
        if (has_yaml_value(yelement, "uri")) {
          auto uri = ""s;
          get_yaml_value(yelement, "uri", uri);
          environment.name = get_basename(uri);
        }
        if (has_yaml_value(yelement, "lookat")) {
          auto lookat = identity3x3f;
          get_yaml_value(yelement, "lookat", lookat);
          environment.frame = lookat_frame(lookat.x, lookat.y, lookat.z, true);
        }
      }
    }

  } catch (std::invalid_argument& e) {
    throw std::runtime_error{filename + ": parse error [" + e.what() + "]"};
  }

  // instance groups
  if (!groups.empty()) {
    // load groups
    if (noparallel) {
      for (auto& group : groups) {
        auto ply = ply_model{};
        try {
          load_ply(get_dirname(filename) + group.filename, ply);
        } catch (std::exception& e) {
          throw_dependent_error(filename, e.what());
        }
        group.frames = get_ply_values(ply, "frame",
            array<string, 12>{"xx", "xy", "xz", "yx", "yy", "yz", "zx", "zy",
                "zz", "ox", "oy", "oz"});
      }
    } else {
      parallel_foreach(groups, [filename](sceneio_group& group) {
        auto ply = ply_model{};
        try {
          load_ply(get_dirname(filename) + group.filename, ply);
        } catch (std::exception& e) {
          throw_dependent_error(filename, e.what());
        }
        group.frames = get_ply_values(ply, "frame",
            array<string, 12>{"xx", "xy", "xz", "yx", "yy", "yz", "zx", "zy",
                "zz", "ox", "oy", "oz"});
      });
    }
    auto instances = scene.instances;
    scene.instances.clear();
    for (auto idx = 0; idx < instances.size(); idx++) {
      auto& base  = instances[idx];
      auto& group = groups[igroups[idx]];
      auto  count = 0;
      for (auto& frame : group.frames) {
        auto& instance    = scene.instances.emplace_back();
        instance.name     = base.name + std::to_string(count++);
        instance.shape    = base.shape;
        instance.material = base.material;
        instance.frame    = frame;
      }
    }
  }
}

#else

sceneio_status load_yaml(const string& filename, sceneio_model& scene) {
  // open file
  auto fs = fopen(filename.c_str(), "rt");
  if (!fs) return {filename + ": file not found"};
  auto fs_guard = std::unique_ptr<FILE, decltype(&fclose)>{fs, fclose};

  auto tmap = unordered_map<string, int>{{"", -1}};
  auto vmap = unordered_map<string, int>{{"", -1}};
  auto mmap = unordered_map<string, int>{{"", -1}};
  auto smap = unordered_map<string, int>{{"", -1}};

  // parse yaml reference
  auto get_yaml_ref_ = [](const yaml_element& yelment, const string& name,
                           int&                              value,
                           const unordered_map<string, int>& refs) -> bool {
    auto ref = ""s;
    get_yaml_value(yelment, name, ref)) return false;
    if (ref == "") {
      value = -1;
      return true;
    } else {
      if (refs.find(ref) == refs.end()) return false;
      value = refs.at(ref);
      return true;
    }
  };
  auto get_yaml_ref = [](const yaml_value& yvalue, int& value,
                          const unordered_map<string, int>& refs) -> bool {
    auto ref = ""s;
    get_yaml_value(yvalue, ref)) return false;
    if (ref == "") {
      value = -1;
      return true;
    } else {
      if (refs.find(ref) == refs.end()) return false;
      value = refs.at(ref);
      return true;
    }
  };

  // loop over commands
  auto group  = ""s;
  auto key    = ""s;
  auto newobj = false;
  auto done   = false;
  auto value  = yaml_value{};
  while (read_yaml_property(filename, fs, group, key, newobj, done, value)) {
    // check done
    if (done) break;
    if (key == "") continue;

    // cameras
    if (group == "cameras") {
      if (newobj) scene.cameras.emplace_back();
      auto& camera = scene.cameras.back();
      if (key == "name")
        get_yaml_value(value, camera.name))
          return {filename + ": parse error"};
      if (key == "uri")
        get_yaml_value(value, camera.name))
          return {filename + ": parse error"};
      if (key == "frame")
        get_yaml_value(value, camera.frame))
          return {filename + ": parse error"};
      if (key == "orthographic")
        get_yaml_value(value, camera.orthographic))
          return {filename + ": parse error"};
      if (key == "lens")
        get_yaml_value(value, camera.lens))
          return {filename + ": parse error"};
      if (key == "aspect")
        get_yaml_value(value, camera.aspect))
          return {filename + ": parse error"};
      if (key == "film")
        get_yaml_value(value, camera.film))
          return {filename + ": parse error"};
      if (key == "focus")
        get_yaml_value(value, camera.focus))
          return {filename + ": parse error"};
      if (key == "aperture")
        get_yaml_value(value, camera.aperture))
          return {filename + ": parse error"};
      if (key == "uri") {
        auto uri = ""s;
        get_yaml_value(value, uri)) return {filename + ": parse error"};
        camera.name = get_basename(uri);
      }
      if (key == "lookat") {
        auto lookat = identity3x3f;
        get_yaml_value(value, lookat)) return {filename + ": parse error"};
        camera.frame = lookat_frame(lookat.x, lookat.y, lookat.z);
        camera.focus = length(lookat.x - lookat.y);
      }
    } else if (group == "textures") {
      if (newobj) scene.textures.emplace_back();
      auto& texture = scene.textures.back();
      if (key == "name")
        get_yaml_value(value, texture.name))
          return {filename + ": parse error"};
      if (key == "filename")
        get_yaml_value(value, texture.filename))
          return {filename + ": parse error"};
      if (key == "preset") {
        auto preset = ""s;
        get_yaml_value(value, preset)) return {filename + ": parse error"};
        make_image_preset(texture.hdr, texture.ldr, preset);
        if (texture.filename.empty()) {
          texture.filename = "textures/ypreset-" + preset +
                             (texture.hdr.empty() ? ".png" : ".hdr");
        }
      }
      if (key == "uri") {
        get_yaml_value(value, texture.filename))
          return {filename + ": parse error"};
        texture.name           = get_basename(texture.filename);
        tmap[texture.filename] = (int)scene.textures.size() - 1;
      }
      tmap[texture.name] = (int)scene.textures.size() - 1;
    } else if (group == "materials") {
      if (newobj) scene.materials.emplace_back();
      auto& material = scene.materials.back();
      if (key == "name")
        get_yaml_value(value, material.name))
          return {filename + ": parse error"};
      if (key == "emission")
        get_yaml_value(value, material.emission))
          return {filename + ": parse error"};
      if (key == "diffuse")
        get_yaml_value(value, material.diffuse))
          return {filename + ": parse error"};
      if (key == "metallic")
        get_yaml_value(value, material.metallic))
          return {filename + ": parse error"};
      if (key == "specular")
        get_yaml_value(value, material.specular))
          return {filename + ": parse error"};
      if (key == "roughness")
        get_yaml_value(value, material.roughness))
          return {filename + ": parse error"};
      if (key == "coat")
        get_yaml_value(value, material.coat))
          return {filename + ": parse error"};
      if (key == "transmission")
        get_yaml_value(value, material.transmission))
          return {filename + ": parse error"};
      if (key == "refract")
        get_yaml_value(value, material.refract))
          return {filename + ": parse error"};
      if (key == "voltransmission")
        if (get_yaml_value(value, material.voltransmission))
          return {filename + ": parse error"};
      if (key == "volmeanfreepath")
        if (get_yaml_value(value, material.volmeanfreepath))
          return {filename + ": parse error"};
      if (key == "volscatter")
        get_yaml_value(value, material.volscatter))
          return {filename + ": parse error"};
      if (key == "volemission")
        get_yaml_value(value, material.volemission))
          return {filename + ": parse error"};
      if (key == "volanisotropy")
        get_yaml_value(value, material.volanisotropy))
          return {filename + ": parse error"};
      if (key == "volscale")
        get_yaml_value(value, material.volscale))
          return {filename + ": parse error"};
      if (key == "opacity")
        get_yaml_value(value, material.opacity))
          return {filename + ": parse error"};
      if (key == "coat")
        get_yaml_value(value, material.coat))
          return {filename + ": parse error"};
      if (key == "emission_tex")
        get_yaml_ref(value, material.emission_tex, tmap))
          return {filename + ": parse error"};
      if (key == "diffuse_tex")
        get_yaml_ref(value, material.diffuse_tex, tmap))
          return {filename + ": parse error"};
      if (key == "metallic_tex")
        get_yaml_ref(value, material.metallic_tex, tmap))
          return {filename + ": parse error"};
      if (key == "specular_tex")
        get_yaml_ref(value, material.specular_tex, tmap))
          return {filename + ": parse error"};
      if (key == "transmission_tex")
        get_yaml_ref(value, material.transmission_tex, tmap))
          return {filename + ": parse error"};
      if (key == "roughness_tex")
        get_yaml_ref(value, material.roughness_tex, tmap))
          return {filename + ": parse error"};
      if (key == "subsurface_tex")
        get_yaml_ref(value, material.subsurface_tex, tmap))
          return {filename + ": parse error"};
      if (key == "normal_tex")
        get_yaml_ref(value, material.normal_tex, tmap))
          return {filename + ": parse error"};
      if (key == "gltf_textures")
        get_yaml_value(value, material.gltf_textures))
          return {filename + ": parse error"};
      if (key == "uri") {
        get_yaml_value(value, material.name))
          return {filename + ": parse error"};
        mmap[material.name] = (int)scene.materials.size() - 1;
        material.name       = get_basename(material.name);
      }
      mmap[material.name] = (int)scene.materials.size() - 1;
    } else if (group == "shapes") {
      if (newobj) scene.shapes.emplace_back();
      auto& shape = scene.shapes.back();
      if (key == "name")
        get_yaml_value(value, shape.name))
          return {filename + ": parse error"};
      if (key == "filename")
        get_yaml_value(value, shape.filename))
          return {filename + ": parse error"};
      if (key == "subdivisions")
        get_yaml_value(value, shape.subdivisions))
          return {filename + ": parse error"};
      if (key == "catmullclark")
        get_yaml_value(value, shape.catmullclark))
          return {filename + ": parse error"};
      if (key == "smooth")
        get_yaml_value(value, shape.smooth))
          return {filename + ": parse error"};
      if (key == "facevarying")
        get_yaml_value(value, shape.facevarying))
          return {filename + ": parse error"};
      if (key == "displacement_tex")
        get_yaml_ref(value, shape.displacement_tex, tmap))
          return {filename + ": parse error"};
      if (key == "displacement")
        get_yaml_value(value, shape.displacement))
          return {filename + ": parse error"};
      if (key == "uri") {
        get_yaml_value(value, shape.filename))
          return {filename + ": parse error"};
        shape.name           = get_basename(shape.filename);
        smap[shape.filename] = (int)scene.shapes.size() - 1;
      }
      if (key == "preset") {
        auto preset = ""s;
        get_yaml_value(value, preset)) return {filename + ": parse error"};
        make_shape_preset(shape.points, shape.lines, shape.triangles,
            shape.quads, shape.quadspos, shape.quadsnorm, shape.quadstexcoord,
            shape.positions, shape.normals, shape.texcoords, shape.colors,
            shape.radius, preset);
        if (shape.filename.empty()) {
          shape.filename = "shapes/ypreset-" + preset + ".yvol";
        }
      }
      smap[shape.name] = (int)scene.shapes.size() - 1;
    } else if (group == "instances") {
      if (newobj) scene.instances.emplace_back();
      auto& instance = scene.instances.back();
      if (key == "name")
        get_yaml_value(value, instance.name))
          return {filename + ": parse error"};
      if (key == "frame")
        get_yaml_value(value, instance.frame))
          return {filename + ": parse error"};
      if (key == "shape")
        get_yaml_ref(value, instance.shape, smap))
          return {filename + ": parse error"};
      if (key == "material")
        get_yaml_ref(value, instance.material, mmap))
          return {filename + ": parse error"};
      if (key == "uri") {
        auto uri = ""s;
        get_yaml_value(value, uri)) return {filename + ": parse error"};
        instance.name = get_basename(uri);
      }
      if (key == "lookat") {
        auto lookat = identity3x3f;
        get_yaml_value(value, lookat)) return {filename + ": parse error"};
        instance.frame = lookat_frame(lookat.x, lookat.y, lookat.z, true);
      }
    } else if (group == "environments") {
      if (newobj) scene.environments.emplace_back();
      auto& environment = scene.environments.back();
      if (key == "name")
        get_yaml_value(value, environment.name))
          return {filename + ": parse error"};
      if (key == "frame")
        get_yaml_value(value, environment.frame))
          return {filename + ": parse error"};
      if (key == "emission")
        get_yaml_value(value, environment.emission))
          return {filename + ": parse error"};
      if (key == "emission_tex")
        get_yaml_ref(value, environment.emission_tex, tmap))
          return {filename + ": parse error"};
      if (key == "uri") {
        auto uri = ""s;
        get_yaml_value(value, uri)) return {filename + ": parse error"};
        environment.name = get_basename(uri);
      }
      if (key == "lookat") {
        auto lookat = identity3x3f;
        get_yaml_value(value, lookat)) return {filename + ": parse error"};
        environment.frame = lookat_frame(lookat.x, lookat.y, lookat.z, true);
      }
    }
  }

  return {};
}

#endif

// Save a scene in the builtin YAML format.
static void load_yaml_scene(
    const string& filename, sceneio_model& scene, bool noparallel) {
  scene = {};

  // Parse yaml
  load_yaml(filename, scene, noparallel);

  // load shape and textures
  load_shapes(filename, scene, noparallel);
  load_subdivs(filename, scene, noparallel);
  load_textures(filename, scene, noparallel);

  // fix scene
  scene.name = get_basename(filename);
  add_cameras(scene);
  add_materials(scene);
  add_radius(scene);
  trim_memory(scene);
}

// Save yaml
static void save_yaml(const string& filename, const sceneio_model& scene,
    bool ply_instances = false, const string& instances_name = "") {
  auto yaml = yaml_model{};

  for (auto stat : scene_stats(scene)) yaml.comments.push_back(stat);

  for (auto& camera : scene.cameras) {
    auto& yelement = yaml.elements.emplace_back();
    yelement.name  = "cameras";
    add_yaml_value(yelement, "name", camera.name);
    add_yaml_value(yelement, "frame", camera.frame);
    if (camera.orthographic)
      add_yaml_value(yelement, "orthographic", camera.orthographic);
    add_yaml_value(yelement, "lens", camera.lens);
    add_yaml_value(yelement, "aspect", camera.aspect);
    add_yaml_value(yelement, "film", camera.film);
    add_yaml_value(yelement, "focus", camera.focus);
    add_yaml_value(yelement, "aperture", camera.aperture);
  }

  for (auto& texture : scene.textures) {
    auto& yelement = yaml.elements.emplace_back();
    yelement.name  = "textures";
    add_yaml_value(yelement, "name", texture.name);
    add_yaml_value(yelement, "filename", texture.filename);
  }

  for (auto& material : scene.materials) {
    auto& yelement = yaml.elements.emplace_back();
    yelement.name  = "materials";
    add_yaml_value(yelement, "name", material.name);
    add_yaml_value(yelement, "emission", material.emission);
    add_yaml_value(yelement, "diffuse", material.diffuse);
    add_yaml_value(yelement, "specular", material.specular);
    if (material.metallic)
      add_yaml_value(yelement, "metallic", material.metallic);
    if (material.transmission != zero3f)
      add_yaml_value(yelement, "transmission", material.transmission);
    add_yaml_value(yelement, "roughness", material.roughness);
    if (material.refract) add_yaml_value(yelement, "refract", material.refract);
    if (material.voltransmission != zero3f)
      add_yaml_value(yelement, "voltransmission", material.voltransmission);
    if (material.volmeanfreepath != zero3f)
      add_yaml_value(yelement, "volmeanfreepath", material.volmeanfreepath);
    if (material.volscatter != zero3f)
      add_yaml_value(yelement, "volscatter", material.volscatter);
    if (material.volemission != zero3f)
      add_yaml_value(yelement, "volemission", material.volemission);
    if (material.volanisotropy)
      add_yaml_value(yelement, "volanisotropy", material.volanisotropy);
    if (material.voltransmission != zero3f ||
        material.volmeanfreepath != zero3f)
      add_yaml_value(yelement, "volscale", material.volscale);
    if (material.coat != zero3f)
      add_yaml_value(yelement, "coat", material.coat);
    if (material.opacity != 1)
      add_yaml_value(yelement, "opacity", material.opacity);
    if (material.emission_tex >= 0)
      add_yaml_value(
          yelement, "emission_tex", scene.textures[material.emission_tex].name);
    if (material.diffuse_tex >= 0)
      add_yaml_value(
          yelement, "diffuse_tex", scene.textures[material.diffuse_tex].name);
    if (material.metallic_tex >= 0)
      add_yaml_value(
          yelement, "metallic_tex", scene.textures[material.metallic_tex].name);
    if (material.specular_tex >= 0)
      add_yaml_value(
          yelement, "specular_tex", scene.textures[material.specular_tex].name);
    if (material.roughness_tex >= 0)
      add_yaml_value(yelement, "roughness_tex",
          scene.textures[material.roughness_tex].name);
    if (material.transmission_tex >= 0)
      add_yaml_value(yelement, "transmission_tex",
          scene.textures[material.transmission_tex].name);
    if (material.subsurface_tex >= 0)
      add_yaml_value(yelement, "subsurface_tex",
          scene.textures[material.subsurface_tex].name);
    if (material.coat_tex >= 0)
      add_yaml_value(
          yelement, "coat_tex", scene.textures[material.coat_tex].name);
    if (material.opacity_tex >= 0)
      add_yaml_value(
          yelement, "opacity_tex", scene.textures[material.opacity_tex].name);
    if (material.normal_tex >= 0)
      add_yaml_value(
          yelement, "normal_tex", scene.textures[material.normal_tex].name);
    if (material.gltf_textures)
      add_yaml_value(yelement, "gltf_textures", material.gltf_textures);
  }

  for (auto& shape : scene.shapes) {
    auto& yelement = yaml.elements.emplace_back();
    yelement.name  = "shapes";
    add_yaml_value(yelement, "name", shape.name);
    add_yaml_value(yelement, "filename", shape.filename);
  }

  for (auto& subdiv : scene.subdivs) {
    auto& yelement = yaml.elements.emplace_back();
    yelement.name  = "subdivs";
    add_yaml_value(yelement, "name", subdiv.name);
    add_yaml_value(yelement, "filename", subdiv.filename);
    if (subdiv.shape >= 0)
      add_yaml_value(yelement, "shape", scene.shapes[subdiv.shape].name);
    add_yaml_value(yelement, "subdivisions", subdiv.subdivisions);
    add_yaml_value(yelement, "catmullclark", subdiv.catmullclark);
    add_yaml_value(yelement, "smooth", subdiv.smooth);
    if (subdiv.facevarying)
      add_yaml_value(yelement, "facevarying", subdiv.facevarying);
    if (subdiv.displacement_tex >= 0)
      add_yaml_value(yelement, "displacement_tex",
          scene.textures[subdiv.displacement_tex].name);
    if (subdiv.displacement_tex >= 0)
      add_yaml_value(yelement, "displacement", subdiv.displacement);
  }

  for (auto& instance : scene.instances) {
    auto& yelement = yaml.elements.emplace_back();
    yelement.name  = "instances";
    add_yaml_value(yelement, "name", instance.name);
    add_yaml_value(yelement, "frame", instance.frame);
    if (instance.shape >= 0)
      add_yaml_value(yelement, "shape", scene.shapes[instance.shape].name);
    if (instance.material >= 0)
      add_yaml_value(
          yelement, "material", scene.materials[instance.material].name);
  }

  for (auto& environment : scene.environments) {
    auto& yelement = yaml.elements.emplace_back();
    yelement.name  = "environments";
    add_yaml_value(yelement, "name", environment.name);
    add_yaml_value(yelement, "frame", environment.frame);
    add_yaml_value(yelement, "emission", environment.emission);
    if (environment.emission_tex >= 0)
      add_yaml_value(yelement, "emission_tex",
          scene.textures[environment.emission_tex].name);
  }

  save_yaml(filename, yaml);
}

// Save a scene in the builtin YAML format.
static void save_yaml_scene(
    const string& filename, const sceneio_model& scene, bool noparallel) {
  // save yaml file
  save_yaml(filename, scene);
  save_shapes(filename, scene, noparallel);
  save_subdivs(filename, scene, noparallel);
  save_textures(filename, scene, noparallel);
}

}  // namespace yocto

// -----------------------------------------------------------------------------
// OBJ CONVERSION
// -----------------------------------------------------------------------------
namespace yocto {

static void load_obj(const string& filename, sceneio_model& scene) {
  // load obj
  auto obj = obj_model{};
  load_obj(filename, obj, false, true, true);

  // convert cameras
  for (auto& ocam : obj.cameras) {
    auto& camera = scene.cameras.emplace_back();
    camera.name  = make_safe_name(ocam.name, "cam", (int)scene.cameras.size());
    camera.frame = ocam.frame;
    camera.orthographic = ocam.ortho;
    camera.film         = max(ocam.width, ocam.height);
    camera.aspect       = ocam.width / ocam.height;
    camera.focus        = ocam.focus;
    camera.lens         = ocam.lens;
    camera.aperture     = ocam.aperture;
  }

  // helper to create texture maps
  auto texture_map = unordered_map<string, int>{{"", -1}};
  auto get_texture = [&texture_map, &scene](const obj_texture_info& info) {
    if (info.path == "") return -1;
    auto it = texture_map.find(info.path);
    if (it != texture_map.end()) return it->second;
    auto& texture = scene.textures.emplace_back();
    texture.name  = make_safe_name(
        get_basename(info.path), "texture", (int)scene.textures.size());
    texture.filename       = info.path;
    texture_map[info.path] = (int)scene.textures.size() - 1;
    return (int)scene.textures.size() - 1;
  };

  // convert materials and textures
  auto material_map = unordered_map<string, int>{{"", -1}};
  for (auto& omat : obj.materials) {
    auto& material = scene.materials.emplace_back();
    material.name  = make_safe_name(
        omat.name, "material", (int)scene.materials.size());
    material.emission         = omat.emission;
    material.diffuse          = omat.diffuse;
    material.specular         = omat.specular;
    material.roughness        = obj_exponent_to_roughness(omat.exponent);
    material.metallic         = omat.pbr_metallic;
    material.coat             = omat.reflection;
    material.transmission     = omat.transmission;
    material.voltransmission  = omat.vol_transmission;
    material.volmeanfreepath  = omat.vol_meanfreepath;
    material.volemission      = omat.vol_emission;
    material.volscatter       = omat.vol_scattering;
    material.volanisotropy    = omat.vol_anisotropy;
    material.volscale         = omat.vol_scale;
    material.opacity          = omat.opacity;
    material.emission_tex     = get_texture(omat.emission_map);
    material.diffuse_tex      = get_texture(omat.diffuse_map);
    material.specular_tex     = get_texture(omat.specular_map);
    material.metallic_tex     = get_texture(omat.pbr_metallic_map);
    material.roughness_tex    = get_texture(omat.pbr_roughness_map);
    material.transmission_tex = get_texture(omat.transmission_map);
    material.coat_tex         = get_texture(omat.reflection_map);
    material.opacity_tex      = get_texture(omat.opacity_map);
    material.normal_tex       = get_texture(omat.normal_map);
    material_map[omat.name]   = (int)scene.materials.size() - 1;
  }

  // convert shapes
  auto shape_name_counts = unordered_map<string, int>{};
  for (auto& oshape : obj.shapes) {
    auto& shape = scene.shapes.emplace_back();
    shape.name  = oshape.name;
    if (shape.name == "") shape.name = "shape";
    shape_name_counts[shape.name] += 1;
    if (shape_name_counts[shape.name] > 1)
      shape.name += std::to_string(shape_name_counts[shape.name]);
    shape.name = make_safe_name(shape.name, "shape", (int)scene.shapes.size());
    shape.filename  = make_safe_filename("shapes/" + shape.name + ".ply");
    auto materials  = vector<string>{};
    auto ematerials = vector<int>{};
    auto has_quads  = has_obj_quads(oshape);
    if (!oshape.faces.empty() && !has_quads) {
      get_obj_triangles(obj, oshape, shape.triangles, shape.positions,
          shape.normals, shape.texcoords, materials, ematerials, true);
    } else if (!oshape.faces.empty() && has_quads) {
      get_obj_quads(obj, oshape, shape.quads, shape.positions, shape.normals,
          shape.texcoords, materials, ematerials, true);
    } else if (!oshape.lines.empty()) {
      get_obj_lines(obj, oshape, shape.lines, shape.positions, shape.normals,
          shape.texcoords, materials, ematerials, true);
    } else if (!oshape.points.empty()) {
      get_obj_points(obj, oshape, shape.points, shape.positions, shape.normals,
          shape.texcoords, materials, ematerials, true);
    } else {
      throw_emptyshape_error(filename, oshape.name);
    }
    // get material
    if (oshape.materials.size() != 1) {
      throw_missing_reference_error(filename, "material for", oshape.name);
    }
    if (material_map.find(oshape.materials.at(0)) == material_map.end()) {
      throw_missing_reference_error(
          filename, "material", oshape.materials.at(0));
    }
    auto material = material_map.at(oshape.materials.at(0));
    // make instances
    if (oshape.instances.empty()) {
      auto& instance    = scene.instances.emplace_back();
      instance.name     = shape.name;
      instance.material = material;
      instance.shape    = (int)scene.shapes.size() - 1;
    } else {
      for (auto& frame : oshape.instances) {
        auto& instance    = scene.instances.emplace_back();
        instance.name     = shape.name;
        instance.frame    = frame;
        instance.material = material;
        instance.shape    = (int)scene.shapes.size() - 1;
      }
    }
  }

  // convert environments
  for (auto& oenvironment : obj.environments) {
    auto& environment = scene.environments.emplace_back();
    environment.name  = make_safe_name(
        oenvironment.name, "environment", scene.environments.size());
    environment.frame        = oenvironment.frame;
    environment.emission     = oenvironment.emission;
    environment.emission_tex = get_texture(oenvironment.emission_map);
  }
}

// Loads an OBJ
static void load_obj_scene(
    const string& filename, sceneio_model& scene, bool noparallel) {
  // Parse obj
  load_obj(filename, scene);
  load_textures(filename, scene, noparallel);

  // fix scene
  scene.name = get_basename(filename);
  add_cameras(scene);
  add_materials(scene);
  add_radius(scene);
}

static void save_obj(
    const string& filename, const sceneio_model& scene, bool instances) {
  auto obj = obj_model{};

  for (auto stat : scene_stats(scene)) obj.comments.push_back(stat);

  // convert cameras
  for (auto& camera : scene.cameras) {
    auto& ocamera    = obj.cameras.emplace_back();
    ocamera.name     = camera.name;
    ocamera.frame    = camera.frame;
    ocamera.ortho    = camera.orthographic;
    ocamera.width    = camera.film;
    ocamera.height   = camera.film / camera.aspect;
    ocamera.focus    = camera.focus;
    ocamera.lens     = camera.lens;
    ocamera.aperture = camera.aperture;
  }

  // textures
  auto get_texture = [&scene](int tex) {
    if (tex < 0) return obj_texture_info{};
    auto info = obj_texture_info{};
    info.path = scene.textures[tex].filename;
    return info;
  };

  // convert materials and textures
  for (auto& material : scene.materials) {
    auto& omaterial             = obj.materials.emplace_back();
    omaterial.name              = material.name;
    omaterial.illum             = 2;
    omaterial.emission          = material.emission;
    omaterial.diffuse           = material.diffuse;
    omaterial.specular          = material.specular;
    omaterial.exponent          = obj_roughness_to_exponent(material.roughness);
    omaterial.pbr_metallic      = material.metallic;
    omaterial.reflection        = material.coat;
    omaterial.transmission      = material.transmission;
    omaterial.opacity           = material.opacity;
    omaterial.emission_map      = get_texture(material.emission_tex);
    omaterial.diffuse_map       = get_texture(material.diffuse_tex);
    omaterial.specular_map      = get_texture(material.specular_tex);
    omaterial.pbr_metallic_map  = get_texture(material.metallic_tex);
    omaterial.pbr_roughness_map = get_texture(material.roughness_tex);
    omaterial.transmission_map  = get_texture(material.transmission_tex);
    omaterial.reflection_map    = get_texture(material.coat_tex);
    omaterial.opacity_map       = get_texture(material.opacity_tex);
    omaterial.normal_map        = get_texture(material.normal_tex);
    if (material.voltransmission != zero3f ||
        material.volmeanfreepath != zero3f) {
      omaterial.vol_transmission = material.voltransmission;
      omaterial.vol_meanfreepath = material.volmeanfreepath;
      omaterial.vol_emission     = material.volemission;
      omaterial.vol_scattering   = material.volscatter;
      omaterial.vol_anisotropy   = material.volanisotropy;
      omaterial.vol_scale        = material.volscale;
    }
  }

  // convert shapes
  if (instances) {
    for (auto& shape : scene.shapes) {
      if (!shape.triangles.empty()) {
        add_obj_triangles(obj, shape.name, shape.triangles, shape.positions,
            shape.normals, shape.texcoords, {}, {}, true);
      } else if (!shape.quads.empty()) {
        add_obj_quads(obj, shape.name, shape.quads, shape.positions,
            shape.normals, shape.texcoords, {}, {}, true);
      } else if (!shape.lines.empty()) {
        add_obj_lines(obj, shape.name, shape.lines, shape.positions,
            shape.normals, shape.texcoords, {}, {}, true);
      } else if (!shape.points.empty()) {
        add_obj_points(obj, shape.name, shape.points, shape.positions,
            shape.normals, shape.texcoords, {}, {}, true);
      } else {
        throw_emptyshape_error(filename, shape.name);
      }
    }
    for (auto& instance : scene.instances) {
      obj.shapes[instance.shape].instances.push_back(instance.frame);
    }
  } else {
    for (auto& instance : scene.instances) {
      auto& shape     = scene.shapes[instance.shape];
      auto  materials = vector<string>{scene.materials[instance.material].name};
      auto  positions = shape.positions, normals = shape.normals;
      for (auto& p : positions) p = transform_point(instance.frame, p);
      for (auto& n : normals) n = transform_normal(instance.frame, n);
      if (!shape.triangles.empty()) {
        add_obj_triangles(obj, instance.name, shape.triangles, positions,
            normals, shape.texcoords, materials, {}, true);
      } else if (!shape.quads.empty()) {
        add_obj_quads(obj, instance.name, shape.quads, positions, normals,
            shape.texcoords, materials, {}, true);
      } else if (!shape.lines.empty()) {
        add_obj_lines(obj, instance.name, shape.lines, positions, normals,
            shape.texcoords, materials, {}, true);
      } else if (!shape.points.empty()) {
        add_obj_points(obj, instance.name, shape.points, positions, normals,
            shape.texcoords, materials, {}, true);
      } else {
        throw_emptyshape_error(filename, shape.name);
      }
    }
  }

  // convert environments
  for (auto& environment : scene.environments) {
    auto& oenvironment        = obj.environments.emplace_back();
    oenvironment.name         = environment.name;
    oenvironment.frame        = environment.frame;
    oenvironment.emission     = environment.emission;
    oenvironment.emission_map = get_texture(environment.emission_tex);
  }

  // save obj
  save_obj(filename, obj);
}

static void save_obj_scene(const string& filename, const sceneio_model& scene,
    bool instances, bool noparallel) {
  save_obj(filename, scene, instances);
  save_textures(filename, scene, noparallel);
}

void print_obj_camera(const sceneio_camera& camera) {
  printf("c %s %d %g %g %g %g %g %g %g %g %g %g%g %g %g %g %g %g %g\n",
      camera.name.c_str(), (int)camera.orthographic, camera.film,
      camera.film / camera.aspect, camera.lens, camera.focus, camera.aperture,
      camera.frame.x.x, camera.frame.x.y, camera.frame.x.z, camera.frame.y.x,
      camera.frame.y.y, camera.frame.y.z, camera.frame.z.x, camera.frame.z.y,
      camera.frame.z.z, camera.frame.o.x, camera.frame.o.y, camera.frame.o.z);
}

}  // namespace yocto

// -----------------------------------------------------------------------------
// PLY CONVERSION
// -----------------------------------------------------------------------------
namespace yocto {

static void load_ply_scene(
    const string& filename, sceneio_model& scene, bool noparallel) {
  scene = {};

  // load ply mesh
  scene.shapes.push_back({});
  auto& shape    = scene.shapes.back();
  shape.name     = "shape";
  shape.filename = get_filename(filename);
  try {
    load_shape(filename, shape.points, shape.lines, shape.triangles,
        shape.quads, shape.positions, shape.normals, shape.texcoords,
        shape.colors, shape.radius);
  } catch (std::exception& e) {
    throw_dependent_error(filename, e.what());
  }

  // add instance
  auto instance  = sceneio_instance{};
  instance.name  = shape.name;
  instance.shape = 0;
  scene.instances.push_back(instance);

  // fix scene
  scene.name = get_basename(filename);
  add_cameras(scene);
  add_materials(scene);
  add_radius(scene);
}

static void save_ply_scene(
    const string& filename, const sceneio_model& scene, bool noparallel) {
  if (scene.shapes.empty()) throw_emptyshape_error(filename, "");
  auto& shape = scene.shapes.front();
  save_shape(filename, shape.points, shape.lines, shape.triangles, shape.quads,
      shape.positions, shape.normals, shape.texcoords, shape.colors,
      shape.radius);
}

}  // namespace yocto

// -----------------------------------------------------------------------------
// GLTF CONVESION
// -----------------------------------------------------------------------------
namespace yocto {

// convert gltf to scene
static void load_gltf(const string& filename, sceneio_model& scene) {
  auto gltf = gltf_model{};
  load_gltf(filename, gltf);

  // convert textures
  for (auto& gtexture : gltf.textures) {
    auto& texture = scene.textures.emplace_back();
    if (!gtexture.name.empty()) {
      texture.name = make_safe_name(
          gtexture.name, "texture", (int)scene.textures.size());
    } else {
      texture.name = make_safe_name(get_basename(gtexture.filename), "texture",
          (int)scene.textures.size());
    }
    texture.filename = gtexture.filename;
  }

  // convert materials
  for (auto& gmaterial : gltf.materials) {
    auto& material = scene.materials.emplace_back();
    material.name  = make_safe_name(
        gmaterial.name, "material", (int)scene.materials.size());
    material.emission     = gmaterial.emission;
    material.emission_tex = gmaterial.emission_tex;
    if (gmaterial.has_specgloss) {
      material.diffuse      = xyz(gmaterial.sg_diffuse);
      material.opacity      = gmaterial.sg_diffuse.w;
      material.specular     = gmaterial.sg_specular;
      material.diffuse_tex  = gmaterial.sg_diffuse_tex;
      material.specular_tex = gmaterial.sg_specular_tex;
    } else if (gmaterial.has_metalrough) {
      material.diffuse      = xyz(gmaterial.mr_base);
      material.opacity      = gmaterial.mr_base.w;
      material.specular     = vec3f{0.04f};
      material.diffuse_tex  = gmaterial.mr_base_tex;
      material.metallic_tex = gmaterial.mr_metallic_tex;
    }
    material.normal_tex = gmaterial.normal_tex;
  }

  // convert shapes
  auto shape_indices = vector<vector<vec2i>>{};
  for (auto& gmesh : gltf.meshes) {
    shape_indices.push_back({});
    for (auto& gprim : gmesh.primitives) {
      auto& shape = scene.shapes.emplace_back();
      shape_indices.back().push_back(
          {(int)scene.shapes.size() - 1, gprim.material});
      shape.name =
          gmesh.name.empty()
              ? ""s
              : (gmesh.name + std::to_string(shape_indices.back().size()));
      make_safe_name(shape.name, "shape", (int)scene.shapes.size());
      shape.filename = make_safe_filename(
          "shapes/shape" + std::to_string(scene.shapes.size()));
      shape.positions = gprim.positions;
      shape.normals   = gprim.normals;
      shape.texcoords = gprim.texcoords;
      shape.colors    = gprim.colors;
      shape.radius    = gprim.radius;
      shape.tangents  = gprim.tangents;
      shape.triangles = gprim.triangles;
      shape.lines     = gprim.lines;
      shape.points    = gprim.points;
    }
  }

  // convert cameras
  auto cameras = vector<sceneio_camera>{};
  for (auto& gcamera : gltf.cameras) {
    auto& camera  = cameras.emplace_back();
    camera.name   = gcamera.name;
    camera.aspect = gcamera.aspect;
    camera.film   = 0.036;
    camera.lens   = gcamera.aspect >= 1
                      ? (2 * camera.aspect * tan(gcamera.yfov / 2))
                      : (2 * tan(gcamera.yfov / 2));
    camera.focus = 10;
  }

  // convert scene nodes
  for (auto& gnode : gltf.nodes) {
    if (gnode.camera >= 0) {
      auto& camera = scene.cameras.emplace_back(cameras[gnode.camera]);
      camera.name  = make_safe_name(
          camera.name, "caemra", (int)scene.cameras.size());
      camera.frame = gnode.frame;
    }
    if (gnode.mesh >= 0) {
      for (auto [shape, material] : shape_indices[gnode.mesh]) {
        auto& instance = scene.instances.emplace_back();
        instance.name  = make_safe_name(
            scene.shapes[shape].name, "instance", (int)scene.instances.size());
        instance.frame    = gnode.frame;
        instance.shape    = shape;
        instance.material = material;
      }
    }
  }
}

// Load a scene
static void load_gltf_scene(
    const string& filename, sceneio_model& scene, bool noparallel) {
  // load gltf
  load_gltf(filename, scene);
  load_textures(filename, scene, noparallel);

  // fix scene
  scene.name = get_basename(filename);
  add_cameras(scene);
  add_materials(scene);
  add_radius(scene);

  // fix cameras
  auto bbox = compute_bounds(scene);
  for (auto& camera : scene.cameras) {
    auto center   = (bbox.min + bbox.max) / 2;
    auto distance = dot(-camera.frame.z, center - camera.frame.o);
    if (distance > 0) camera.focus = distance;
  }
}

}  // namespace yocto

// -----------------------------------------------------------------------------
// IMPLEMENTATION OF PBRT
// -----------------------------------------------------------------------------
namespace yocto {

static void load_pbrt(
    const string& filename, sceneio_model& scene, bool noparallel) {
  // load pbrt
  auto pbrt = pbrt_model{};
  load_pbrt(filename, pbrt);

  // convert cameras
  for (auto& pcamera : pbrt.cameras) {
    auto& camera  = scene.cameras.emplace_back();
    camera.name   = make_safe_name("", "camera", (int)scene.cameras.size());
    camera.frame  = pcamera.frame;
    camera.aspect = pcamera.aspect;
    camera.film   = 0.036;
    camera.lens   = pcamera.lens;
    camera.focus  = pcamera.focus;
  }

  // convert textures
  auto texture_map = unordered_map<string, int>{{"", -1}};
  for (auto& ptexture : pbrt.textures) {
    if (ptexture.filename.empty()) continue;
    auto& texture = scene.textures.emplace_back();
    texture.name  = make_safe_name(
        ptexture.name, "texture", (int)scene.textures.size());
    texture.filename           = ptexture.filename;
    texture_map[ptexture.name] = (int)scene.textures.size() - 1;
  }

  // convert materials
  auto get_texture = [&texture_map](const string& name) {
    if (name == "") return -1;
    if (texture_map.find(name) == texture_map.end())
      throw std::runtime_error("cannot find texture " + name);
    return texture_map.at(name);
  };
  auto material_map = unordered_map<string, int>{{"", -1}};
  for (auto& pmaterial : pbrt.materials) {
    auto& material = scene.materials.emplace_back();
    material.name  = make_safe_name(
        pmaterial.name, "material", (int)scene.materials.size());
    material.diffuse      = pmaterial.diffuse;
    material.specular     = pmaterial.sspecular;
    material.transmission = pmaterial.transmission;
    material.roughness    = mean(pmaterial.roughness);
    material.opacity      = pmaterial.opacity == vec3f{1} ? 1
                                                     : mean(pmaterial.opacity);
    material.diffuse_tex         = get_texture(pmaterial.diffuse_map);
    material_map[pmaterial.name] = (int)scene.materials.size() - 1;
  }

  // convert arealights
  auto arealight_map = unordered_map<string, int>{{"", -1}};
  for (auto& parealight : pbrt.arealights) {
    auto& material = scene.materials.emplace_back();
    material.name  = make_safe_name(
        parealight.name, "arealight", (int)arealight_map.size());
    material.emission              = parealight.emission;
    arealight_map[parealight.name] = (int)scene.materials.size() - 1;
  }

  // convert shapes
  for (auto& pshape : pbrt.shapes) {
    auto& shape = scene.shapes.emplace_back();
    shape.name  = make_safe_name(
        get_basename(shape.filename), "shape", (int)scene.shapes.size());
    if (pshape.filename.empty()) {
      shape.name     = make_safe_name("", "shape", (int)scene.shapes.size());
      shape.filename = make_safe_filename(
          "shapes/shape" + std::to_string(scene.shapes.size()) + ".ply");
    } else {
      shape.filename = pshape.filename;
      shape.name     = make_safe_name(
          get_basename(pshape.filename), "shape", (int)scene.shapes.size());
    }
    shape.positions = pshape.positions;
    shape.normals   = pshape.normals;
    shape.texcoords = pshape.texcoords;
    shape.triangles = pshape.triangles;
    for (auto& uv : shape.texcoords) uv.y = 1 - uv.y;
    auto material_id  = material_map.at(pshape.material);
    auto arealight_id = arealight_map.at(pshape.arealight);
    if (pshape.instance_frames.empty()) {
      auto& instance    = scene.instances.emplace_back();
      instance.name     = shape.name;
      instance.frame    = pshape.frame;
      instance.material = arealight_id >= 0 ? arealight_id : material_id;
      instance.shape    = (int)scene.shapes.size() - 1;
    } else {
      auto instance_id = 0;
      for (auto& frame : pshape.instance_frames) {
        auto& instance    = scene.instances.emplace_back();
        instance.name     = shape.name + (pshape.instance_frames.empty()
                                             ? ""s
                                             : std::to_string(instance_id++));
        instance.frame    = frame * pshape.frame;
        instance.material = arealight_id >= 0 ? arealight_id : material_id;
        instance.shape    = (int)scene.shapes.size() - 1;
      }
    }
  }

  // convert environments
  for (auto& penvironment : pbrt.environments) {
    auto& environment = scene.environments.emplace_back();
    environment.name  = make_safe_name(
        "", "environment", (int)scene.environments.size());
    environment.frame    = penvironment.frame;
    environment.emission = penvironment.emission;
    if (!penvironment.filename.empty()) {
      auto& texture    = scene.textures.emplace_back();
      texture.name     = make_safe_name(get_basename(penvironment.filename),
          "environment", (int)scene.environments.size());
      texture.filename = penvironment.filename;
      environment.emission_tex = (int)scene.textures.size() - 1;
    } else {
      environment.emission_tex = -1;
    }
  }

  // lights
  for (auto& plight : pbrt.lights) {
    auto& shape       = scene.shapes.emplace_back();
    shape.name        = make_safe_name("", "light", (int)scene.shapes.size());
    shape.filename    = make_safe_filename("shapes/" + shape.name + ".ply");
    shape.triangles   = plight.area_triangles;
    shape.positions   = plight.area_positions;
    shape.normals     = plight.area_normals;
    auto& material    = scene.materials.emplace_back();
    material.name     = shape.name;
    material.emission = plight.area_emission;
    auto& instance    = scene.instances.emplace_back();
    instance.name     = shape.name;
    instance.frame    = plight.area_frame;
    instance.shape    = (int)scene.shapes.size() - 1;
    instance.material = (int)scene.materials.size() - 1;
  }
}

// load pbrt scenes
static void load_pbrt_scene(
    const string& filename, sceneio_model& scene, bool noparallel) {
  // Parse pbrt
  load_pbrt(filename, scene, noparallel);
  load_shapes(filename, scene, noparallel);
  load_textures(filename, scene, noparallel);

  // fix scene
  scene.name = get_basename(filename);
  add_cameras(scene);
  add_materials(scene);
  add_radius(scene);
}

// Convert a scene to pbrt format
static void save_pbrt(const string& filename, const sceneio_model& scene) {
  auto pbrt = pbrt_model{};

  // embed data
  for (auto stat : scene_stats(scene)) pbrt.comments.push_back(stat);

  // convert camera
  auto& camera     = scene.cameras.front();
  auto& pcamera    = pbrt.cameras.emplace_back();
  pcamera.frame    = camera.frame;
  pcamera.lens     = camera.lens;
  pcamera.aspect   = camera.aspect;
  auto& pfilm      = pbrt.films.emplace_back();
  pfilm.filename   = "out.png";
  pfilm.resolution = {1280, (int)(1280 / pcamera.aspect)};

  // convert textures
  for (auto& texture : scene.textures) {
    auto& ptexture    = pbrt.textures.emplace_back();
    ptexture.name     = texture.name;
    ptexture.filename = texture.filename;
  }

  // convert materials
  for (auto& material : scene.materials) {
    auto& pmaterial        = pbrt.materials.emplace_back();
    pmaterial.name         = material.name;
    pmaterial.diffuse      = material.diffuse;
    pmaterial.specular     = material.specular;
    pmaterial.transmission = material.transmission;
    pmaterial.roughness    = {material.roughness, material.roughness};
    pmaterial.diffuse_map  = material.diffuse_tex >= 0
                                ? scene.textures[material.diffuse_tex].name
                                : ""s;
    auto& parealight    = pbrt.arealights.emplace_back();
    parealight.name     = material.name;
    parealight.emission = material.emission;
  }

  // convert instances
  for (auto& instance : scene.instances) {
    auto& shape      = scene.shapes[instance.shape];
    auto& material   = scene.materials[instance.material];
    auto& pshape     = pbrt.shapes.emplace_back();
    pshape.filename  = replace_extension(shape.filename, ".ply");
    pshape.frame     = instance.frame;
    pshape.material  = material.name;
    pshape.arealight = material.emission == zero3f ? ""s : material.name;
  }

  // convert environments
  for (auto& environment : scene.environments) {
    auto& penvironment    = pbrt.environments.emplace_back();
    penvironment.emission = environment.emission;
    if (environment.emission_tex >= 0) {
      penvironment.filename = scene.textures[environment.emission_tex].filename;
    }
  }

  save_pbrt(filename, pbrt);
}

// Save a pbrt scene
void save_pbrt_scene(
    const string& filename, const sceneio_model& scene, bool noparallel) {
  // save pbrt
  save_pbrt(filename, scene);

  // save meshes
  auto dirname = get_dirname(filename);
  for (auto& shape : scene.shapes) {
    save_shape(replace_extension(dirname + shape.filename, ".ply"),
        shape.points, shape.lines, shape.triangles, shape.quads,
        shape.positions, shape.normals, shape.texcoords, shape.colors,
        shape.radius);
  }

  // save textures
  save_textures(filename, scene, noparallel);
}

}  // namespace yocto

// -----------------------------------------------------------------------------
// EXAMPLE SCENES
// -----------------------------------------------------------------------------
namespace yocto {

void make_cornellbox_scene(sceneio_model& scene) {
  scene.name              = "cornellbox";
  auto& camera            = scene.cameras.emplace_back();
  camera.name             = "camera";
  camera.frame            = frame3f{{0, 1, 3.9}};
  camera.lens             = 0.035;
  camera.aperture         = 0.0;
  camera.film             = 0.024;
  camera.aspect           = 1;
  auto& floor_mat         = scene.materials.emplace_back();
  floor_mat.name          = "floor";
  floor_mat.diffuse       = {0.725, 0.71, 0.68};
  auto& ceiling_mat       = scene.materials.emplace_back();
  ceiling_mat.name        = "ceiling";
  ceiling_mat.diffuse     = {0.725, 0.71, 0.68};
  auto& backwall_mat      = scene.materials.emplace_back();
  backwall_mat.name       = "backwall";
  backwall_mat.diffuse    = {0.725, 0.71, 0.68};
  auto& rightwall_mat     = scene.materials.emplace_back();
  rightwall_mat.name      = "rightwall";
  rightwall_mat.diffuse   = {0.14, 0.45, 0.091};
  auto& leftwall_mat      = scene.materials.emplace_back();
  leftwall_mat.name       = "leftwall";
  leftwall_mat.diffuse    = {0.63, 0.065, 0.05};
  auto& shortbox_mat      = scene.materials.emplace_back();
  shortbox_mat.name       = "shortbox";
  shortbox_mat.diffuse    = {0.725, 0.71, 0.68};
  auto& tallbox_mat       = scene.materials.emplace_back();
  tallbox_mat.name        = "tallbox";
  tallbox_mat.diffuse     = {0.725, 0.71, 0.68};
  auto& light_mat         = scene.materials.emplace_back();
  light_mat.name          = "light";
  light_mat.emission      = {17, 12, 4};
  auto& floor_shp         = scene.shapes.emplace_back();
  floor_shp.name          = "floor";
  floor_shp.filename      = "shapes/floor.obj";
  floor_shp.positions     = {{-1, 0, 1}, {1, 0, 1}, {1, 0, -1}, {-1, 0, -1}};
  floor_shp.triangles     = {{0, 1, 2}, {2, 3, 0}};
  auto& ceiling_shp       = scene.shapes.emplace_back();
  ceiling_shp.name        = "ceiling";
  ceiling_shp.name        = "shapes/ceiling.obj";
  ceiling_shp.positions   = {{-1, 2, 1}, {-1, 2, -1}, {1, 2, -1}, {1, 2, 1}};
  ceiling_shp.triangles   = {{0, 1, 2}, {2, 3, 0}};
  auto& backwall_shp      = scene.shapes.emplace_back();
  backwall_shp.name       = "backwall";
  backwall_shp.filename   = "shapes/backwall.obj";
  backwall_shp.positions  = {{-1, 0, -1}, {1, 0, -1}, {1, 2, -1}, {-1, 2, -1}};
  backwall_shp.triangles  = {{0, 1, 2}, {2, 3, 0}};
  auto& rightwall_shp     = scene.shapes.emplace_back();
  rightwall_shp.name      = "rightwall";
  rightwall_shp.filename  = "shapes/rightwall.obj";
  rightwall_shp.positions = {{1, 0, -1}, {1, 0, 1}, {1, 2, 1}, {1, 2, -1}};
  rightwall_shp.triangles = {{0, 1, 2}, {2, 3, 0}};
  auto& leftwall_shp      = scene.shapes.emplace_back();
  leftwall_shp.name       = "leftwall";
  leftwall_shp.filename   = "shapes/leftwall.obj";
  leftwall_shp.positions  = {{-1, 0, 1}, {-1, 0, -1}, {-1, 2, -1}, {-1, 2, 1}};
  leftwall_shp.triangles  = {{0, 1, 2}, {2, 3, 0}};
  auto& shortbox_shp      = scene.shapes.emplace_back();
  shortbox_shp.name       = "shortbox";
  shortbox_shp.filename   = "shapes/shortbox.obj";
  shortbox_shp.positions  = {{0.53, 0.6, 0.75}, {0.7, 0.6, 0.17},
      {0.13, 0.6, 0.0}, {-0.05, 0.6, 0.57}, {-0.05, 0.0, 0.57},
      {-0.05, 0.6, 0.57}, {0.13, 0.6, 0.0}, {0.13, 0.0, 0.0}, {0.53, 0.0, 0.75},
      {0.53, 0.6, 0.75}, {-0.05, 0.6, 0.57}, {-0.05, 0.0, 0.57},
      {0.7, 0.0, 0.17}, {0.7, 0.6, 0.17}, {0.53, 0.6, 0.75}, {0.53, 0.0, 0.75},
      {0.13, 0.0, 0.0}, {0.13, 0.6, 0.0}, {0.7, 0.6, 0.17}, {0.7, 0.0, 0.17},
      {0.53, 0.0, 0.75}, {0.7, 0.0, 0.17}, {0.13, 0.0, 0.0},
      {-0.05, 0.0, 0.57}};
  shortbox_shp.triangles  = {{0, 1, 2}, {2, 3, 0}, {4, 5, 6}, {6, 7, 4},
      {8, 9, 10}, {10, 11, 8}, {12, 13, 14}, {14, 15, 12}, {16, 17, 18},
      {18, 19, 16}, {20, 21, 22}, {22, 23, 20}};
  auto& tallbox_shp       = scene.shapes.emplace_back();
  tallbox_shp.name        = "tallbox";
  tallbox_shp.filename    = "shapes/tallbox.obj";
  tallbox_shp.positions   = {{-0.53, 1.2, 0.09}, {0.04, 1.2, -0.09},
      {-0.14, 1.2, -0.67}, {-0.71, 1.2, -0.49}, {-0.53, 0.0, 0.09},
      {-0.53, 1.2, 0.09}, {-0.71, 1.2, -0.49}, {-0.71, 0.0, -0.49},
      {-0.71, 0.0, -0.49}, {-0.71, 1.2, -0.49}, {-0.14, 1.2, -0.67},
      {-0.14, 0.0, -0.67}, {-0.14, 0.0, -0.67}, {-0.14, 1.2, -0.67},
      {0.04, 1.2, -0.09}, {0.04, 0.0, -0.09}, {0.04, 0.0, -0.09},
      {0.04, 1.2, -0.09}, {-0.53, 1.2, 0.09}, {-0.53, 0.0, 0.09},
      {-0.53, 0.0, 0.09}, {0.04, 0.0, -0.09}, {-0.14, 0.0, -0.67},
      {-0.71, 0.0, -0.49}};
  tallbox_shp.triangles   = {{0, 1, 2}, {2, 3, 0}, {4, 5, 6}, {6, 7, 4},
      {8, 9, 10}, {10, 11, 8}, {12, 13, 14}, {14, 15, 12}, {16, 17, 18},
      {18, 19, 16}, {20, 21, 22}, {22, 23, 20}};
  auto& light_shp         = scene.shapes.emplace_back();
  light_shp.name          = "light";
  light_shp.filename      = "shapes/light.obj";
  light_shp.positions     = {{-0.25, 1.99, 0.25}, {-0.25, 1.99, -0.25},
      {0.25, 1.99, -0.25}, {0.25, 1.99, 0.25}};
  light_shp.triangles     = {{0, 1, 2}, {2, 3, 0}};
  scene.instances.push_back({"floor", identity3x4f, 0, 0});
  scene.instances.push_back({"ceiling", identity3x4f, 1, 1});
  scene.instances.push_back({"backwall", identity3x4f, 2, 2});
  scene.instances.push_back({"rightwall", identity3x4f, 3, 3});
  scene.instances.push_back({"leftwall", identity3x4f, 4, 4});
  scene.instances.push_back({"shortbox", identity3x4f, 5, 5});
  scene.instances.push_back({"tallbox", identity3x4f, 6, 6});
  scene.instances.push_back({"light", identity3x4f, 7, 7});
}

}  // namespace yocto
