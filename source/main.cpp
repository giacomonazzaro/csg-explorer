#include "csg.h"
#include "parser.h"
//
#include "ext/yocto-gl/apps/yocto_opengl.h"
#include "ext/yocto-gl/yocto/yocto_common.h"
#include "ext/yocto-gl/yocto/yocto_commonio.h"
#include "ext/yocto-gl/yocto/yocto_trace.h"
using namespace yocto;

#include <future>
#include <memory>
using namespace std;

float get_seconds() { return get_time() * 1e-9; }

// Application state
struct app_state {
  // loading options
  string filename  = "scene.csg";
  string imagename = "out.png";
  string name      = "";

  trace_camera camera;

  // options
  trace_params params            = {};
  int          preview_downscale = 6;

  Csg csg      = {};
  int selected = 0;

  // rendering state
  trace_state  state    = {};
  image<vec4f> render   = {};
  image<vec4f> display  = {};
  float        exposure = 0;

  // view scene
  opengl_image        glimage  = {};
  draw_glimage_params glparams = {};

  // computation
  int          render_sample  = 0;
  atomic<bool> render_stop    = {};
  future<void> render_future  = {};
  int          render_counter = 0;

  // Enqueued commands
  vector<function<void()>> commands = {};

  void run(function<void()>&& f) { commands.push_back(f); }

  ~app_state() {
    render_stop = true;
    if (render_future.valid()) render_future.get();
  }
};

// Simple parallel for used since our target platforms do not yet support
// parallel algorithms. `Func` takes the integer index.
template <typename Func>
inline void parallel_for(const vec2i& size, Func&& func) {
  auto        futures  = vector<future<void>>{};
  auto        nthreads = thread::hardware_concurrency();
  atomic<int> next_idx(0);
  for (auto thread_id = 0; thread_id < nthreads; thread_id++) {
    futures.emplace_back(async(launch::async, [&func, &next_idx, size]() {
      while (true) {
        auto j = next_idx.fetch_add(1);
        if (j >= size.y) break;
        for (auto i = 0; i < size.x; i++) func({i, j});
      }
    }));
  }
  for (auto& f : futures) f.get();
}

// Eyelight for quick previewing.
vec3f raymarch(
    const trace_camera& camera, const Csg& csg, ray3f ray, rng_state& rng) {
  auto box            = bbox3f{{0, 0, 0}, {1, 1, 1}};
  auto intersect_bbox = [](const ray3f& ray, const bbox3f& bbox) -> float {
    auto invd = 1.0f / ray.d;
    auto t0   = (bbox.min - ray.o) * invd;
    auto t1   = (bbox.max - ray.o) * invd;
    if (invd.x < 0.0f) swap(t0.x, t1.x);
    if (invd.y < 0.0f) swap(t0.y, t1.y);
    if (invd.z < 0.0f) swap(t0.z, t1.z);
    auto tmin = max(t0.z, max(t0.y, max(t0.x, ray.tmin)));
    auto tmax = min(t1.z, min(t1.y, min(t1.x, ray.tmax)));
    if (tmax < tmin) return -1;
    return tmin;
  };

  auto values = vector<float>(csg.nodes.size());
  auto sdf    = [&](vec3f p) -> float {
    p -= vec3f(0.5);
    return eval_csg(values, csg, p);
  };

  auto compute_normal = [&sdf](const vec3f& p) {
    float eps = 0.001;
    auto  o   = sdf(p);
    auto  x   = sdf(p + vec3f{eps, 0, 0});
    auto  y   = sdf(p + vec3f{0, eps, 0});
    auto  z   = sdf(p + vec3f{0, 0, eps});
    return normalize(vec3f{x, y, z} - vec3f(o));
  };

  auto material      = material_point{};
  material.diffuse   = vec3f(0.9, 0.3, 0.2);
  material.specular  = vec3f(0.04);
  material.roughness = 0.2;

  auto t = intersect_bbox(ray, box);
  if (t < 0) {
    return vec3f(0.0);
  }

  ray.o += ray.d * (t + 0.01);

  for (int i = 0; i < 1000; i++) {
    float distance = sdf(ray.o);
    if (fabs(distance) <= 0.001) {
      auto normal   = compute_normal(ray.o);
      auto light    = normalize(vec3f{0.2, 1, 0});
      auto clr      = vec3f{1, 1, 1};
      auto ambient  = min((normal.y + 1) * 0.1f, 0.1f);
      auto radiance = vec3f(0);
      radiance += clr * eval_brdfcos(material, normal, -ray.d, light);
      radiance += ambient * material.diffuse;
      return radiance;
    }

    if (ray.o.x > 1) return vec3f(0.01);
    if (ray.o.y > 1) return vec3f(0.01);
    if (ray.o.z > 1) return vec3f(0.01);
    if (ray.o.x < 0) return vec3f(0.01);
    if (ray.o.y < 0) return vec3f(0.01);
    if (ray.o.z < 0) return vec3f(0.01);
    ray.o += ray.d * distance;
  }

  return {1, 0, 0};
}

// Trace a block of samples
vec4f raymarch_sample(const Csg& csg, trace_state& state,
    const trace_camera& camera, const vec2i& ij, const trace_params& params) {
  auto& pixel = state.at(ij);
  auto  ray   = sample_camera(
      camera, ij, state.size(), rand2f(pixel.rng), rand2f(pixel.rng));

  auto radiance = raymarch(camera, csg, ray, pixel.rng);

  if (!isfinite(radiance)) radiance = zero3f;
  if (max(radiance) > params.clamp) {
    radiance = radiance * (params.clamp / max(radiance));
  }
  pixel.radiance += radiance;
  pixel.hits += 1;
  pixel.samples += 1;
  return {pixel.hits ? pixel.radiance / pixel.hits : zero3f,
      (float)pixel.hits / (float)pixel.samples};
}

// Progressively compute an image by calling trace_samples multiple times.
image<vec4f> raymarch_image(
    const trace_camera& camera, const Csg& csg, const trace_params& params) {
  auto state = trace_state{};
  init_state(state, camera, params);
  auto render = image{state.size(), zero4f};

  if (params.noparallel) {
    for (auto j = 0; j < render.size().y; j++) {
      for (auto i = 0; i < render.size().x; i++) {
        for (auto s = 0; s < params.samples; s++) {
          render[{i, j}] = raymarch_sample(csg, state, camera, {i, j}, params);
        }
      }
    }
  } else {
    parallel_for(render.size(),
        [&render, &state, &camera, &params, &csg](const vec2i& ij) {
          for (auto s = 0; s < params.samples; s++) {
            render[ij] = raymarch_sample(csg, state, camera, ij, params);
          }
        });
  }

  return render;
}

void reset_display(shared_ptr<app_state> app) {
  // stop render
  app->render_stop = true;
  if (app->render_future.valid()) app->render_future.get();

  for (auto& f : app->commands) {
    f();
  }
  app->commands.clear();

  // reset state
  init_state(app->state, app->camera, app->params);
  app->render.resize(app->state.size());
  app->display.resize(app->state.size());

  // render preview
  auto preview_prms = app->params;
  preview_prms.resolution /= app->preview_downscale;
  preview_prms.samples = 1;
  auto preview         = raymarch_image(app->camera, app->csg, preview_prms);
  preview              = tonemap_image(preview, app->exposure);
  for (auto j = 0; j < app->display.size().y; j++) {
    for (auto i = 0; i < app->display.size().x; i++) {
      auto pi = clamp(i / app->preview_downscale, 0, preview.size().x - 1),
           pj = clamp(j / app->preview_downscale, 0, preview.size().y - 1);
      app->display[{i, j}] = preview[{pi, pj}];
    }
  }

  // start renderer
  app->render_counter = 0;
  app->render_stop    = false;
  app->render_future  = async(launch::async, [app]() {
    for (auto sample = 0; sample < app->params.samples; sample++) {
      if (app->render_stop) return;
      parallel_for(app->render.size(), [app](const vec2i& ij) {
        if (app->render_stop) return;
        app->render[ij] = raymarch_sample(
            app->csg, app->state, app->camera, ij, app->params);
        app->display[ij] = tonemap(app->render[ij], app->exposure);
      });
    }
  });
}

template <typename Type>
bool deferred_slider(const opengl_window& win, shared_ptr<app_state> app,
    const char* name, Type& value, float min, float max) {
  auto copy = value;
  if (draw_glslider(win, name, copy, min, max)) {
    app->commands.push_back([&value, copy]() { value = copy; });
    return 1;
  }
  return 0;
}

void draw_glwidgets(const opengl_window& win, shared_ptr<app_state> app,
    const opengl_input& input) {
  auto& node = app->csg.nodes[app->selected];
  int   edit = 0;
  if (node.children == vec2i{-1, -1}) {
    edit += draw_glslider(win, "x", node.primitive.params[0], -1, 1);
    edit += draw_glslider(win, "y", node.primitive.params[1], 0, 1);
    edit += draw_glslider(win, "z", node.primitive.params[2], 0, 1);
    edit += draw_glslider(win, "radius", node.primitive.params[3], 0, 1);
  } else {
    edit += draw_glslider(win, "blend", node.operation.blend, -1, 1);
    edit += draw_glslider(win, "soft", node.operation.softness, 0, 1);
  }
  if (edit > 0) reset_display(app);
}

void run_app(shared_ptr<app_state> app) {
  auto& camera = app->camera;
  auto  from   = vec3f{2, 2, 2};
  auto  to     = vec3f{0.5, 0.5, 0.5};
  camera.film  = {0.024, 0.024};
  camera.frame = lookat_frame(from, to, {0, 1, 0});
  camera.focus = length(from - to);

  // allocate buffers
  init_state(app->state, app->camera, app->params);
  app->render  = image{app->state.size(), zero4f};
  app->display = app->render;
  reset_display(app);

  app->params.samples = 4;

  // window
  auto win = opengl_window{};
  init_glwindow(win, {720 + 320, 720}, "Csg Explorer", true);

  // callbacks
  set_draw_glcallback(
      win, [app](const opengl_window& win, const opengl_input& input) {
        if (!is_initialized(app->glimage)) init_glimage(app->glimage);
        if (!app->render_counter)
          set_glimage(app->glimage, app->display, false, false);
        app->glparams.window      = input.window_size;
        app->glparams.framebuffer = input.framebuffer_viewport;
        update_imview(app->glparams.center, app->glparams.scale,
            app->display.size(), app->glparams.window, app->glparams.fit);
        draw_glimage(app->glimage, app->glparams);
        app->render_counter++;
        if (app->render_counter > 10) app->render_counter = 0;
      });
  set_uiupdate_glcallback(
      win, [app](const opengl_window& win, const opengl_input& input) {
        if ((input.mouse_left || input.mouse_right) && !input.modifier_alt &&
            !input.widgets_active) {
          auto& camera = app->camera;
          auto  dolly  = 0.0f;
          auto  pan    = zero2f;
          auto  rotate = zero2f;
          if (input.mouse_left && !input.modifier_shift)
            rotate = (input.mouse_pos - input.mouse_last) / 100.0f;
          if (input.mouse_right)
            dolly = (input.mouse_pos.x - input.mouse_last.x) / 100.0f;
          if (input.mouse_left && input.modifier_shift)
            pan = (input.mouse_pos - input.mouse_last) * camera.focus / 200.0f;
          pan.x = -pan.x;
          update_turntable(camera.frame, camera.focus, rotate, dolly, pan);
          reset_display(app);
        }
      });

  set_widgets_glcallback(
      win, [app](const opengl_window& win, const opengl_input& input) {
        draw_glwidgets(win, app, input);
      });

  auto keycb = [app](const opengl_window& win, opengl_key key, bool pressed,
                   const opengl_input& input) {
    if (!pressed) return;
    if (key == opengl_key::enter) {
      app->run([app]() { app->csg = load_csg(app->filename); });
      reset_display(app);
    }

    if (key == opengl_key::left) {
      app->selected = yocto::max(app->selected - 1, 0);
    }
    if (key == opengl_key::right) {
      app->selected = yocto::min(app->selected + 1, app->csg.nodes.size() - 1);
    }
  };

  set_key_glcallback(win, keycb);

  // run ui
  run_ui(win);

  // clear
  clear_glwindow(win);
}

int main(int argc, const char* argv[]) {
  // parse command line
  string filename;
  auto   cli = make_cli("michelangelo", "Csg renderer");
  add_cli_option(cli, "Shape", filename, "Shape filename", true);
  parse_cli(cli, argc, argv);

  auto app      = make_shared<app_state>();
  app->csg      = load_csg(filename);
  app->filename = filename;
  run_app(app);
}
