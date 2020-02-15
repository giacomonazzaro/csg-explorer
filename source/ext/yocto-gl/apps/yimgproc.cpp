//
// LICENSE:
//
// Copyright (c) 2016 -- 2019 Fabio Pellacini
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
// this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//

#include "../yocto/yocto_commonio.h"
#include "../yocto/yocto_image.h"
#include "../yocto/yocto_math.h"
using namespace yocto;

namespace yocto {

image<vec4f> filter_bilateral(const image<vec4f>& img, float spatial_sigma,
    float range_sigma, const vector<image<vec4f>>& features,
    const vector<float>& features_sigma) {
  auto filtered     = image{img.size(), zero4f};
  auto filter_width = (int)ceil(2.57f * spatial_sigma);
  auto sw           = 1 / (2.0f * spatial_sigma * spatial_sigma);
  auto rw           = 1 / (2.0f * range_sigma * range_sigma);
  auto fw           = vector<float>();
  for (auto feature_sigma : features_sigma)
    fw.push_back(1 / (2.0f * feature_sigma * feature_sigma));
  for (auto j = 0; j < img.size().y; j++) {
    for (auto i = 0; i < img.size().x; i++) {
      auto av = zero4f;
      auto aw = 0.0f;
      for (auto fj = -filter_width; fj <= filter_width; fj++) {
        for (auto fi = -filter_width; fi <= filter_width; fi++) {
          auto ii = i + fi, jj = j + fj;
          if (ii < 0 || jj < 0) continue;
          if (ii >= img.size().x || jj >= img.size().y) continue;
          auto uv  = vec2f{float(i - ii), float(j - jj)};
          auto rgb = img[{i, j}] - img[{i, j}];
          auto w   = (float)exp(-dot(uv, uv) * sw) *
                   (float)exp(-dot(rgb, rgb) * rw);
          for (auto fi = 0; fi < features.size(); fi++) {
            auto feat = features[fi][{i, j}] - features[fi][{i, j}];
            w *= exp(-dot(feat, feat) * fw[fi]);
          }
          av += w * img[{ii, jj}];
          aw += w;
        }
      }
      filtered[{i, j}] = av / aw;
    }
  }
  return filtered;
}

image<vec4f> filter_bilateral(
    const image<vec4f>& img, float spatial_sigma, float range_sigma) {
  auto filtered = image{img.size(), zero4f};
  auto fwidth   = (int)ceil(2.57f * spatial_sigma);
  auto sw       = 1 / (2.0f * spatial_sigma * spatial_sigma);
  auto rw       = 1 / (2.0f * range_sigma * range_sigma);
  for (auto j = 0; j < img.size().y; j++) {
    for (auto i = 0; i < img.size().x; i++) {
      auto av = zero4f;
      auto aw = 0.0f;
      for (auto fj = -fwidth; fj <= fwidth; fj++) {
        for (auto fi = -fwidth; fi <= fwidth; fi++) {
          auto ii = i + fi, jj = j + fj;
          if (ii < 0 || jj < 0) continue;
          if (ii >= img.size().x || jj >= img.size().y) continue;
          auto uv  = vec2f{float(i - ii), float(j - jj)};
          auto rgb = img[{i, j}] - img[{ii, jj}];
          auto w   = exp(-dot(uv, uv) * sw) * exp(-dot(rgb, rgb) * rw);
          av += w * img[{ii, jj}];
          aw += w;
        }
      }
      filtered[{i, j}] = av / aw;
    }
  }
  return filtered;
}

}  // namespace yocto

void run_app(int argc, const char* argv[]) {
  // command line parameters
  auto tonemap_on          = false;
  auto tonemap_exposure    = 0;
  auto tonemap_filmic      = false;
  auto logo                = false;
  auto resize_width        = 0;
  auto resize_height       = 0;
  auto spatial_sigma       = 0.0f;
  auto range_sigma         = 0.0f;
  auto alpha_filename      = ""s;
  auto coloralpha_filename = ""s;
  auto diff_filename       = ""s;
  auto diff_signal         = false;
  auto diff_threshold      = 0.0f;
  auto output              = "out.png"s;
  auto filename            = "img.hdr"s;

  // parse command line
  auto cli = make_cli("yimgproc", "Transform images");
  add_cli_option(cli, "--tonemap/--no-tonemap,-t", tonemap_on, "Tonemap image");
  add_cli_option(cli, "--exposure,-e", tonemap_exposure, "Tonemap exposure");
  add_cli_option(cli, "--filmic/--no-filmic,-f", tonemap_filmic,
      "Tonemap uses filmic curve");
  add_cli_option(cli, "--resize-width", resize_width,
      "resize size (0 to maintain aspect)");
  add_cli_option(cli, "--resize-height", resize_height,
      "resize size (0 to maintain aspect)");
  add_cli_option(cli, "--spatial-sigma", spatial_sigma, "blur spatial sigma");
  add_cli_option(
      cli, "--range-sigma", range_sigma, "bilateral blur range sigma");
  add_cli_option(
      cli, "--set-alpha", alpha_filename, "set alpha as this image alpha");
  add_cli_option(cli, "--set-color-as-alpha", coloralpha_filename,
      "set alpha as this image color");
  add_cli_option(cli, "--logo", logo, "Add logo");
  add_cli_option(
      cli, "--diff", diff_filename, "compute the diff between images");
  add_cli_option(cli, "--diff-signal", diff_signal, "signal a diff as error");
  add_cli_option(cli, "--diff-threshold,", diff_threshold, "diff threshold");
  add_cli_option(cli, "--output,-o", output, "output image filename", true);
  add_cli_option(cli, "filename", filename, "input image filename", true);
  parse_cli(cli, argc, argv);

  // error string buffer
  auto error = ""s;

  // load
  auto img = load_image(filename);

  // set alpha
  if (alpha_filename != "") {
    auto alpha = load_image(alpha_filename);
    if (img.size() != alpha.size()) throw std::runtime_error("bad image size");
    for (auto j = 0; j < img.size().y; j++)
      for (auto i = 0; i < img.size().x; i++) img[{i, j}].w = alpha[{i, j}].w;
  }

  // set alpha
  if (coloralpha_filename != "") {
    auto alpha = load_image(coloralpha_filename);
    if (img.size() != alpha.size()) throw std::runtime_error("bad image size");
    for (auto j = 0; j < img.size().y; j++)
      for (auto i = 0; i < img.size().x; i++)
        img[{i, j}].w = mean(xyz(alpha[{i, j}]));
  }

  // diff
  if (diff_filename != "") {
    auto diff = load_image(diff_filename);
    if (img.size() != diff.size())
      throw std::runtime_error("image sizes are different");
    img = image_difference(img, diff, true);
  }

  // resize
  if (resize_width != 0 || resize_height != 0) {
    img = resize_image(img, {resize_width, resize_height});
  }

  // bilateral
  if (spatial_sigma && range_sigma) {
    img = filter_bilateral(img, spatial_sigma, range_sigma, {}, {});
  }

  // hdr correction
  if (tonemap_on) {
    img = tonemap_image(img, tonemap_exposure, tonemap_filmic, false);
  }

  // save
  save_image(output, logo ? add_logo(img) : img);

  // check diff
  if (diff_filename != "" && diff_signal) {
    for (auto& c : img) {
      if (max(xyz(c)) > diff_threshold)
        throw std::runtime_error("image content differs");
    }
  }
}

int main(int argc, const char* argv[]) {
  try {
    run_app(argc, argv);
    return 0;
  } catch (std::exception& e) {
    print_fatal(e.what());
    return 1;
  }
}
