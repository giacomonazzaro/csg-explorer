//
// # Yocto/Image: Tiny imaging Library mostly for rendering and color support
//
//
// Yocto/Image is a collection of image utilities useful when writing rendering
// algorithms. These include a simple image data structure, color conversion
// utilities and tone mapping. We provinde loading and saving functionality for
// images and support PNG, JPG, TGA, BMP, HDR, EXR formats.
//
// This library depends on stb_image.h, stb_image_write.h, stb_image_resize.h,
// tinyexr.h for the IO features. If thoese are not needed, it can be safely
// used without dependencies.
//
//
// ## Images
//
// Yocto/Math contains a simple image container that can be used to store
// generic images. The container is similar in spirit to `std::vector`.
// We provide only minimal image functions including lookup and sampling.
//
//
// ## Image Utilities
//
// Yocto/Image supports a very small set of color and image utilities including
// color utilities, example image creation, tone mapping, image resizing, and
// sunsky procedural images. Yocto/Image is written to support the need of a
// global illumination renderer, rather than the need of generic image editing.
// We support 4-channels float images (assumed to be in linear color) and
// 4-channels byte images (assumed to be in sRGB).
//
//
// 1. store images using the image<T> structure
// 2. load and save images with `load_image()` and `save_image()`
// 3. resize images with `resize()`
// 4. tonemap images with `tonemap()` that convert from linear HDR to
//    sRGB LDR with exposure and an optional filmic curve
// 5. make various image examples with the `make_proc_image()` functions
// 6. create procedural sun-sky images with `make_sunsky()`
// 7. many color conversion functions are available in the code below
//
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
//
//  LICENSE for blackbody code
//
// Copyright (c) 2015 Neil Bartlett
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
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
//

#ifndef _YOCTO_IMAGE_H_
#define _YOCTO_IMAGE_H_

// -----------------------------------------------------------------------------
// INCLUDES
// -----------------------------------------------------------------------------

#include <algorithm>

#include "yocto_math.h"

// -----------------------------------------------------------------------------
// IMAGE DATA AND UTILITIES
// -----------------------------------------------------------------------------
namespace yocto {

// Image container.
template <typename T>
struct image {
  // constructors
  image();
  image(const vec2i& size, const T& value = {});
  image(const vec2i& size, const T* value);

  // size
  bool   empty() const;
  vec2i  size() const;
  size_t count() const;
  bool   contains(const vec2i& ij) const;
  void   resize(const vec2i& size);
  void   assign(const vec2i& size, const T& value = {});
  void   shrink_to_fit();
  void   swap(image& other);

  // element access
  T&       operator[](int i);
  const T& operator[](int i) const;
  T&       operator[](const vec2i& ij);
  const T& operator[](const vec2i& ij) const;

  // data access
  T*       data();
  const T* data() const;

  // iteration
  T*       begin();
  T*       end();
  const T* begin() const;
  const T* end() const;

 private:
  // data
  vec2i     extent = zero2i;
  vector<T> pixels = {};
};

// equality
template <typename T>
inline bool operator==(const image<T>& a, const image<T>& b);
template <typename T>
inline bool operator!=(const image<T>& a, const image<T>& b);

// swap
template <typename T>
inline void swap(image<T>& a, image<T>& b);

}  // namespace yocto

// -----------------------------------------------------------------------------
// IMAGE SAMPLING
// -----------------------------------------------------------------------------
namespace yocto {

// Evaluates a color image at a point `uv`.
vec4f eval_image(const image<vec4f>& img, const vec2f& uv,
    bool no_interpolation, bool clamp_to_edge);
vec4f eval_image(const image<vec4b>& img, const vec2f& uv, bool as_linear,
    bool no_interpolation, bool clamp_to_edge);

}  // namespace yocto

// -----------------------------------------------------------------------------
// IMAGE UTILITIES
// -----------------------------------------------------------------------------
namespace yocto {

// Conversion from/to floats.
image<vec4f> byte_to_float(const image<vec4b>& bt);
image<vec4b> float_to_byte(const image<vec4f>& fl);

// Conversion between linear and gamma-encoded images.
image<vec4f> srgb_to_rgb(const image<vec4f>& srgb);
image<vec4f> rgb_to_srgb(const image<vec4f>& rgb);
image<vec4f> srgb_to_rgb(const image<vec4b>& srgb);
image<vec4b> rgb_to_srgbb(const image<vec4f>& rgb);

// Apply tone mapping
vec3f tonemap(
    const vec3f& hdr, float exposure, bool filmic = false, bool srgb = true);
vec4f tonemap(
    const vec4f& hdr, float exposure, bool filmic = false, bool srgb = true);

// Apply tone mapping
image<vec4f> tonemap_image(const image<vec4f>& hdr, float exposure,
    bool filmic = false, bool srgb = true);
image<vec4b> tonemap_imageb(const image<vec4f>& hdr, float exposure,
    bool filmic = false, bool srgb = true);

// minimal color grading
struct colorgrade_params {
  float exposure         = 0;
  vec3f tint             = {1, 1, 1};
  float lincontrast      = 0.5;
  float logcontrast      = 0.5;
  float linsaturation    = 0.5;
  bool  filmic           = false;
  bool  srgb             = true;
  float contrast         = 0.5;
  float saturation       = 0.5;
  float shadows          = 0.5;
  float midtones         = 0.5;
  float highlights       = 0.5;
  vec3f shadows_color    = {1, 1, 1};
  vec3f midtones_color   = {1, 1, 1};
  vec3f highlights_color = {1, 1, 1};
};

// Apply color grading from a linear or srgb color to an srgb color.
vec3f colorgrade(
    const vec3f& rgb, bool linear, const colorgrade_params& params);
vec4f colorgrade(
    const vec4f& rgb, bool linear, const colorgrade_params& params);

// Color grade a linear or srgb image to an srgb image.
image<vec4f> colorgrade_image(
    const image<vec4f>& img, bool linear, const colorgrade_params& params);

// determine white balance colors
vec3f compute_white_balance(const image<vec4f>& img);

// Resize an image.
image<vec4f> resize_image(const image<vec4f>& img, const vec2i& size);
image<vec4b> resize_image(const image<vec4b>& img, const vec2i& size);

// Compute the difference between two images
image<vec4f> image_difference(
    const image<vec4f>& a, const image<vec4f>& b, bool disply_diff);

}  // namespace yocto

// -----------------------------------------------------------------------------
// IMAGE IO
// -----------------------------------------------------------------------------
namespace yocto {

// Check if an image is HDR based on filename.
bool is_hdr_filename(const string& filename);

// Loads/saves a 4 channels float/byte image in linear/srgb color space.
// Throws exception on error.
image<vec4f> load_image(const string& filename);
void         load_image(const string& filename, image<vec4f>& img);
void         save_image(const string& filename, const image<vec4f>& img);
image<vec4b> load_imageb(const string& filename);
void         load_imageb(const string& filename, image<vec4b>& img);
void         save_imageb(const string& filename, const image<vec4b>& img);

}  // namespace yocto

// -----------------------------------------------------------------------------
// EXAMPLE IMAGES
// -----------------------------------------------------------------------------
namespace yocto {

// Parameters for make_proc_image
struct proc_image_params {
  // clang-format off
  enum struct type_t {
    grid, checker, bumps, ramp, gammaramp, uvramp, uvgrid, blackbody, noise,
    turbulence, fbm, ridge };
  // clang-format on
  type_t type    = type_t::grid;
  vec2i  size    = {1024, 1024};
  float  scale   = 1;
  vec4f  color0  = {0, 0, 0, 1};
  vec4f  color1  = {1, 1, 1, 1};
  vec4f  noise   = {2, 0.5, 8, 1};  // lacunarity, gain, octaves, offset
  float  borderw = 0;
  vec4f  borderc = {0, 0, 0, 1};
};

// Make a grid image.
image<vec4f> make_grid(const vec2i& size, float scale = 1,
    const vec4f& color0 = vec4f{0.2, 0.2, 0.2, 1},
    const vec4f& color1 = vec4f{0.5, 0.5, 0.5, 1});
// Make a checker image.
image<vec4f> make_checker(const vec2i& size, float scale = 1,
    const vec4f& color0 = vec4f{0.2, 0.2, 0.2, 1},
    const vec4f& color1 = vec4f{0.5, 0.5, 0.5, 1});
// Make a bump map.
image<vec4f> make_bumps(const vec2i& size, float scale = 1,
    const vec4f& color0 = vec4f{0, 0, 0, 1},
    const vec4f& color1 = vec4f{1, 1, 1, 1});
// Make a ramp
image<vec4f> make_ramp(const vec2i& size, float scale = 1,
    const vec4f& color0 = vec4f{0, 0, 0, 1},
    const vec4f& color1 = vec4f{1, 1, 1, 1});
// Make a gamma ramp.
image<vec4f> make_gammaramp(const vec2i& size, float scale = 1,
    const vec4f& color0 = vec4f{0, 0, 0, 1},
    const vec4f& color1 = vec4f{1, 1, 1, 1});
// Make a uv ramp
image<vec4f> make_uvramp(const vec2i& size, float scale = 1);
// Make a uv grid
image<vec4f> make_uvgrid(
    const vec2i& size, float scale = 1, bool colored = true);
// Make blackbody ramp.
image<vec4f> make_blackbodyramp(
    const vec2i& size, float scale = 1, float from = 1000, float to = 12000);
// Make a noise image. Noise parameters: lacunarity, gain, octaves, offset.
image<vec4f> make_noisemap(const vec2i& size, float scale = 1,
    const vec4f& color0 = {0, 0, 0, 1}, const vec4f& color1 = {0, 0, 0, 1});
image<vec4f> make_fbmmap(const vec2i& size, float scale = 1,
    const vec4f& noise = {2, 0.5, 8, 1}, const vec4f& color0 = {0, 0, 0, 1},
    const vec4f& color1 = {0, 0, 0, 1});
image<vec4f> make_turbulencemap(const vec2i& size, float scale = 1,
    const vec4f& noise = {2, 0.5, 8, 1}, const vec4f& color0 = {0, 0, 0, 1},
    const vec4f& color1 = {0, 0, 0, 1});
image<vec4f> make_ridgemap(const vec2i& size, float scale = 1,
    const vec4f& noise = {2, 0.5, 8, 1}, const vec4f& color0 = {0, 0, 0, 1},
    const vec4f& color1 = {0, 0, 0, 1});

// Make a sunsky HDR model with sun at sun_angle elevation in [0,pif/2],
// turbidity in [1.7,10] with or without sun. The sun can be enabled or
// disabled with has_sun. The sun parameters can be slightly modified by
// changing the sun intensity and temperature. Has a convention, a temperature
// of 0 sets the eath sun defaults (ignoring intensity too).
image<vec4f> make_sunsky(const vec2i& size, float sun_angle,
    float turbidity = 3, bool has_sun = false, float sun_intensity = 1,
    float sun_radius = 1, const vec3f& ground_albedo = {0.2, 0.2, 0.2});
// Make an image of multiple lights.
image<vec4f> make_lights(const vec2i& size, const vec3f& le = {1, 1, 1},
    int nlights = 4, float langle = pif / 4, float lwidth = pif / 16,
    float lheight = pif / 16);

// Comvert a bump map to a normal map. All linear color spaces.
image<vec4f> bump_to_normal(const image<vec4f>& img, float scale = 1);

// Add a border to an image
image<vec4f> add_border(
    const image<vec4f>& img, float width, const vec4f& color = {0, 0, 0, 1});

// Make logo images. Image is resized to proper size.
image<vec4b> make_logo(const string& name);
image<vec4f> add_logo(
    const image<vec4f>& img, const string& name = "logo-medium");
image<vec4b> add_logo(
    const image<vec4b>& img, const string& name = "logo-medium");

// Make an image preset, useful for testing. See implementation for types.
image<vec4f> make_image_preset(const string& type);
image<vec4b> make_image_presetb(const string& type);

}  // namespace yocto

// -----------------------------------------------------------------------------
// VOLUME TYPE AND UTILITIES (EXPERIMENTAL)
// -----------------------------------------------------------------------------
namespace yocto {

// Volume container.
template <typename T>
struct volume {
  // constructors
  volume();
  volume(const vec3i& size, const T& value);
  volume(const vec3i& size, const T* value);

  // size
  bool   empty() const;
  vec3i  size() const;
  size_t count() const;
  void   resize(const vec3i& size);
  void   assign(const vec3i& size, const T& value);
  void   shrink_to_fit();
  void   swap(volume& other);

  // element access
  T&       operator[](size_t i);
  const T& operator[](size_t i) const;
  T&       operator[](const vec3i& ijk);
  const T& operator[](const vec3i& ijk) const;

  // data access
  T*       data();
  const T* data() const;

  // iteration
  T*       begin();
  T*       end();
  const T* begin() const;
  const T* end() const;

 private:
  // data
  vec3i         extent = zero3i;
  vector<float> voxels = {};
};

// equality
template <typename T>
inline bool operator==(const volume<T>& a, const volume<T>& b);
template <typename T>
inline bool operator!=(const volume<T>& a, const volume<T>& b);

// swap
template <typename T>
inline void swap(volume<T>& a, volume<T>& b);

}  // namespace yocto

// -----------------------------------------------------------------------------
// VOLUME SAMPLING
// -----------------------------------------------------------------------------
namespace yocto {

// Evaluates a color image at a point `uv`.
float eval_volume(const image<float>& img, const vec3f& uvw,
    bool no_interpolation = false, bool clamp_to_edge = false);

}  // namespace yocto

// -----------------------------------------------------------------------------
// VOLUME IO
// -----------------------------------------------------------------------------
namespace yocto {

// Loads/saves a 1 channel volume.
void load_volume(const string& filename, volume<float>& vol);
void save_volume(const string& filename, const volume<float>& vol);

}  // namespace yocto

// -----------------------------------------------------------------------------
// EXAMPLE VOLUMES
// -----------------------------------------------------------------------------
namespace yocto {

// make a simple example volume
void make_voltest(volume<float>& vol, const vec3i& size, float scale = 10,
    float exponent = 6);
void make_volume_preset(volume<float>& vol, const string& type);

}  // namespace yocto

// -----------------------------------------------------------------------------
// COLOR CONVERSION UTILITIES
// -----------------------------------------------------------------------------
namespace yocto {

// RGB color spaces
enum struct color_space {
  rgb,         // default linear space (srgb linear)
  srgb,        // srgb color space (non-linear)
  adobe,       // Adobe rgb color space (non-linear)
  prophoto,    // ProPhoto Kodak rgb color space (non-linear)
  rec709,      // hdtv color space (non-linear)
  rec2020,     // uhtv color space (non-linear)
  rec2100pq,   // hdr color space with perceptual quantizer (non-linear)
  rec2100hlg,  // hdr color space with hybrid log gamma (non-linear)
  aces2065,    // ACES storage format (linear)
  acescg,      // ACES CG computation (linear)
  acescc,      // ACES color correction (non-linear)
  acescct,     // ACES color correction 2 (non-linear)
  p3dci,       // P3 DCI (non-linear)
  p3d60,       // P3 variation for D60 (non-linear)
  p3d65,       // P3 variation for D65 (non-linear)
  p3display,   // Apple display P3
};

// Conversion between rgb color spaces
vec3f color_to_xyz(const vec3f& col, color_space from);
vec3f xyz_to_color(const vec3f& xyz, color_space to);

}  // namespace yocto

// -----------------------------------------------------------------------------
// IMAGE DATA AND UTILITIES
// -----------------------------------------------------------------------------
namespace yocto {

// constructors
template <typename T>
inline image<T>::image() : extent{0, 0}, pixels{} {}
template <typename T>
inline image<T>::image(const vec2i& size, const T& value)
    : extent{size}, pixels((size_t)size.x * (size_t)size.y, value) {}
template <typename T>
inline image<T>::image(const vec2i& size, const T* value)
    : extent{size}, pixels(value, value + (size_t)size.x * (size_t)size.y) {}

// size
template <typename T>
inline bool image<T>::empty() const {
  return pixels.empty();
}
template <typename T>
inline vec2i image<T>::size() const {
  return extent;
}
template <typename T>
inline size_t image<T>::count() const {
  return pixels.size();
}
template <typename T>
inline bool image<T>::contains(const vec2i& ij) const {
  return ij.x > 0 && ij.x < extent.x && ij.y > 0 && ij.y < extent.y;
}
template <typename T>
inline void image<T>::resize(const vec2i& size) {
  if (size == extent) return;
  extent = size;
  pixels.resize((size_t)size.x * (size_t)size.y);
}
template <typename T>
inline void image<T>::assign(const vec2i& size, const T& value) {
  extent = size;
  pixels.assign((size_t)size.x * (size_t)size.y, value);
}
template <typename T>
inline void image<T>::shrink_to_fit() {
  pixels.shrink_to_fit();
}
template <typename T>
inline void image<T>::swap(image<T>& other) {
  std::swap(extent, other.extent);
  pixels.swap(other.pixels);
}

// element access
template <typename T>
inline T& image<T>::operator[](int i) {
  return pixels[i];
}
template <typename T>
inline const T& image<T>::operator[](int i) const {
  return pixels[i];
}
template <typename T>
inline T& image<T>::operator[](const vec2i& ij) {
  return pixels[ij.y * extent.x + ij.x];
}
template <typename T>
inline const T& image<T>::operator[](const vec2i& ij) const {
  return pixels[ij.y * extent.x + ij.x];
}

// data access
template <typename T>
inline T* image<T>::data() {
  return pixels.data();
}
template <typename T>
inline const T* image<T>::data() const {
  return pixels.data();
}

// iteration
template <typename T>
inline T* image<T>::begin() {
  return pixels.data();
}
template <typename T>
inline T* image<T>::end() {
  return pixels.data() + pixels.size();
}
template <typename T>
inline const T* image<T>::begin() const {
  return pixels.data();
}
template <typename T>
inline const T* image<T>::end() const {
  return pixels.data() + pixels.size();
}

// equality
template <typename T>
inline bool operator==(const image<T>& a, const image<T>& b) {
  return a.size() == b.size() && a.pixels == b.pixels;
}
template <typename T>
inline bool operator!=(const image<T>& a, const image<T>& b) {
  return a.size() != b.size() || a.pixels != b.pixels;
}

// swap
template <typename T>
inline void swap(image<T>& a, image<T>& b) {
  a.swap(b);
}

}  // namespace yocto

// -----------------------------------------------------------------------------
// VOLUME TYPE AND UTILITIES
// -----------------------------------------------------------------------------
namespace yocto {

// Volume container ----------

// constructors
template <typename T>
inline volume<T>::volume() : extent{0, 0, 0}, voxels{} {}
template <typename T>
inline volume<T>::volume(const vec3i& size, const T& value)
    : extent{size}
    , voxels((size_t)size.x * (size_t)size.y * (size_t)size.z, value) {}
template <typename T>
inline volume<T>::volume(const vec3i& size, const T* value)
    : extent{size}
    , voxels(value, value + (size_t)size.x * (size_t)size.y * (size_t)size.z) {}

// size
template <typename T>
inline bool volume<T>::empty() const {
  return voxels.empty();
}
template <typename T>
inline vec3i volume<T>::size() const {
  return extent;
}
template <typename T>
inline size_t volume<T>::count() const {
  return voxels.size();
}
template <typename T>
inline void volume<T>::resize(const vec3i& size) {
  if (size == extent) return;
  extent = size;
  voxels.resize((size_t)size.x * (size_t)size.y * (size_t)size.z);
}
template <typename T>
inline void volume<T>::assign(const vec3i& size, const T& value) {
  extent = size;
  voxels.assign((size_t)size.x * (size_t)size.y * (size_t)size.z, value);
}
template <typename T>
inline void volume<T>::shrink_to_fit() {
  voxels.shrink_to_fit();
}
template <typename T>
inline void volume<T>::swap(volume<T>& other) {
  std::swap(extent, other.extent);
  voxels.swap(other.voxels);
}

// element access
template <typename T>
inline T& volume<T>::operator[](size_t i) {
  return voxels[i];
}
template <typename T>
inline const T& volume<T>::operator[](size_t i) const {
  return voxels[i];
}
template <typename T>
inline T& volume<T>::operator[](const vec3i& ijk) {
  return voxels[ijk.z * extent.x * extent.y + ijk.y * extent.x + ijk.x];
}
template <typename T>
inline const T& volume<T>::operator[](const vec3i& ijk) const {
  return voxels[ijk.z * extent.x * extent.y + ijk.y * extent.x + ijk.x];
}

// data access
template <typename T>
inline T* volume<T>::data() {
  return voxels.data();
}
template <typename T>
inline const T* volume<T>::data() const {
  return voxels.data();
}

// iteration
template <typename T>
inline T* volume<T>::begin() {
  return voxels.data();
}
template <typename T>
inline T* volume<T>::end() {
  return voxels.data() + voxels.size();
}
template <typename T>
inline const T* volume<T>::begin() const {
  return voxels.data();
}
template <typename T>
inline const T* volume<T>::end() const {
  return voxels.data() + voxels.size();
}

// equality
template <typename T>
inline bool operator==(const volume<T>& a, const volume<T>& b) {
  return a.size() == b.size() && a.voxels == b.voxels;
}
template <typename T>
inline bool operator!=(const volume<T>& a, const volume<T>& b) {
  return a.size() != b.size() || a.voxels != b.voxels;
}

// swap
template <typename T>
inline void swap(volume<T>& a, volume<T>& b) {
  a.swap(b);
}

}  // namespace yocto

#endif
