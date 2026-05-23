#include "map_renderer.hpp"

#include "constants/app_config.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "ui.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numbers>

namespace app {
namespace {

constexpr const char *tag = "map_renderer";
constexpr uint16_t missing_tile_color{0x39E7}; // muted dark gray, RGB565
constexpr uint16_t dot_outer_color{0xFFFF};
constexpr uint16_t dot_inner_color{0x07FF};

[[nodiscard]] uint16_t *canvas_row(uint16_t *buffer, int32_t width,
                                   int32_t y) noexcept {
  return buffer + static_cast<size_t>(y) * static_cast<size_t>(width);
}

[[nodiscard]] int32_t floor_div_tile(double pixel) noexcept {
  return static_cast<int32_t>(
      std::floor(pixel / constants::app::maps::tile_size_px));
}

[[nodiscard]] int32_t normalize_tile_x(int32_t x) noexcept {
  const int32_t tiles_per_axis = 1 << constants::app::maps::fixed_zoom;
  x %= tiles_per_axis;
  if (x < 0) {
    x += tiles_per_axis;
  }
  return x;
}

[[nodiscard]] bool valid_tile_y(int32_t y) noexcept {
  const int32_t tiles_per_axis = 1 << constants::app::maps::fixed_zoom;
  return y >= 0 && y < tiles_per_axis;
}

void log_heap_state(const char *context, size_t requested_size) noexcept {
  const auto psram_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
  const auto internal_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  ESP_LOGI(tag,
           "%s: requested=%u, psram free=%u largest=%u, internal free=%u "
           "largest=%u",
           context, static_cast<unsigned>(requested_size),
           static_cast<unsigned>(heap_caps_get_free_size(psram_caps)),
           static_cast<unsigned>(heap_caps_get_largest_free_block(psram_caps)),
           static_cast<unsigned>(heap_caps_get_free_size(internal_caps)),
           static_cast<unsigned>(
               heap_caps_get_largest_free_block(internal_caps)));
}

} // namespace

MapRenderer::~MapRenderer() { reset(); }

void MapRenderer::reset() noexcept {
  canvas_parent_ = nullptr;
  canvas_ = nullptr;
  if (canvas_buffer_ != nullptr) {
    heap_caps_free(canvas_buffer_);
    canvas_buffer_ = nullptr;
  }
  canvas_buffer_size_ = 0;
  canvas_width_ = 0;
  canvas_height_ = 0;
  has_last_center_ = false;
}

void MapRenderer::render_location(const LocationCoordinates &location) noexcept {
  if (!std::isfinite(location.latitude) || !std::isfinite(location.longitude)) {
    return;
  }

  if (!ensure_canvas()) {
    return;
  }

  const ProjectedPoint projected = project(location.latitude, location.longitude);
  const auto center_x = static_cast<int64_t>(std::llround(projected.x_px));
  const auto center_y = static_cast<int64_t>(std::llround(projected.y_px));

  if (has_last_center_) {
    const auto dx = std::llabs(center_x - last_center_x_px_);
    const auto dy = std::llabs(center_y - last_center_y_px_);
    if (dx < constants::app::maps::min_rerender_delta_px &&
        dy < constants::app::maps::min_rerender_delta_px) {
      return;
    }
  }

  draw_map(projected);
  draw_location_dot();
  lv_obj_invalidate(canvas_);

  last_center_x_px_ = center_x;
  last_center_y_px_ = center_y;
  has_last_center_ = true;
}

bool MapRenderer::ensure_canvas() noexcept {
  if (ui_MapViewport == nullptr) {
    canvas_parent_ = nullptr;
    canvas_ = nullptr;
    return false;
  }

  lv_obj_update_layout(ui_MapViewport);
  const int32_t width = lv_obj_get_width(ui_MapViewport);
  const int32_t height = lv_obj_get_height(ui_MapViewport);
  if (width <= 0 || height <= 0) {
    return false;
  }

  if (canvas_parent_ != ui_MapViewport) {
    canvas_parent_ = ui_MapViewport;
    canvas_ = nullptr;
    has_last_center_ = false;
  }

  if (!allocate_canvas_buffer(width, height)) {
    return false;
  }

  if (canvas_ == nullptr) {
    canvas_ = lv_canvas_create(ui_MapViewport);
    if (canvas_ == nullptr) {
      ESP_LOGE(tag, "failed to create map canvas");
      return false;
    }
    lv_obj_remove_flag(canvas_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(canvas_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(canvas_, width, height);
    lv_obj_center(canvas_);
  }

  lv_canvas_set_buffer(canvas_, canvas_buffer_, width, height,
                       LV_COLOR_FORMAT_RGB565);
  lv_obj_set_size(canvas_, width, height);
  lv_obj_center(canvas_);
  return true;
}

bool MapRenderer::allocate_canvas_buffer(int32_t width,
                                         int32_t height) noexcept {
  if (width == canvas_width_ && height == canvas_height_ &&
      canvas_buffer_ != nullptr) {
    return true;
  }

  if (canvas_buffer_ != nullptr) {
    heap_caps_free(canvas_buffer_);
    canvas_buffer_ = nullptr;
  }

  const size_t size = static_cast<size_t>(width) * static_cast<size_t>(height) *
                      sizeof(uint16_t);
  canvas_buffer_ = static_cast<uint16_t *>(heap_caps_aligned_alloc(
      LV_DRAW_BUF_ALIGN, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (canvas_buffer_ == nullptr) {
    canvas_buffer_ = static_cast<uint16_t *>(heap_caps_aligned_alloc(
        LV_DRAW_BUF_ALIGN, size, MALLOC_CAP_8BIT));
  }

  if (canvas_buffer_ == nullptr) {
    log_heap_state("failed to allocate map canvas buffer", size);
    canvas_buffer_size_ = 0;
    canvas_width_ = 0;
    canvas_height_ = 0;
    return false;
  }

  canvas_buffer_size_ = size;
  canvas_width_ = width;
  canvas_height_ = height;
  has_last_center_ = false;

  ESP_LOGI(tag, "map canvas buffer allocated: %ldx%ld, %u bytes",
           static_cast<long>(width), static_cast<long>(height),
           static_cast<unsigned>(size));
  return true;
}

MapRenderer::ProjectedPoint MapRenderer::project(double latitude,
                                                 double longitude) noexcept {
  const double clamped_latitude = std::clamp(latitude, -85.05112878, 85.05112878);
  const double lat_rad = clamped_latitude * std::numbers::pi / 180.0;
  const double tiles_per_axis =
      static_cast<double>(1u << constants::app::maps::fixed_zoom);
  const double world_size_px =
      tiles_per_axis * constants::app::maps::tile_size_px;

  const double x = (longitude + 180.0) / 360.0 * world_size_px;
  const double y =
      (1.0 - std::log(std::tan(lat_rad) + (1.0 / std::cos(lat_rad))) / std::numbers::pi) /
      2.0 * world_size_px;

  return {.x_px = x, .y_px = y};
}

void MapRenderer::draw_map(const ProjectedPoint &center) noexcept {
  if (canvas_buffer_ == nullptr || canvas_width_ <= 0 || canvas_height_ <= 0) {
    return;
  }

  fill_canvas(missing_tile_color);

  const double left = center.x_px - static_cast<double>(canvas_width_) / 2.0;
  const double top = center.y_px - static_cast<double>(canvas_height_) / 2.0;
  const double right = left + canvas_width_ - 1;
  const double bottom = top + canvas_height_ - 1;

  const int32_t start_tile_x = floor_div_tile(left);
  const int32_t end_tile_x = floor_div_tile(right);
  const int32_t start_tile_y = floor_div_tile(top);
  const int32_t end_tile_y = floor_div_tile(bottom);

  for (int32_t tile_y = start_tile_y; tile_y <= end_tile_y; ++tile_y) {
    for (int32_t tile_x = start_tile_x; tile_x <= end_tile_x; ++tile_x) {
      const int32_t tile_origin_x =
          tile_x * constants::app::maps::tile_size_px;
      const int32_t tile_origin_y =
          tile_y * constants::app::maps::tile_size_px;
      const int32_t dest_x =
          static_cast<int32_t>(std::floor(tile_origin_x - left));
      const int32_t dest_y =
          static_cast<int32_t>(std::floor(tile_origin_y - top));
      draw_tile(tile_x, tile_y, dest_x, dest_y);
    }
  }
}

void MapRenderer::draw_tile(int32_t tile_x, int32_t tile_y, int32_t dest_x,
                            int32_t dest_y) noexcept {
  if (!valid_tile_y(tile_y)) {
    return;
  }

  const int32_t normalized_tile_x = normalize_tile_x(tile_x);
  char path[128]{};
  std::snprintf(path, sizeof(path), "%s/%u/%ld/%ld%s",
                constants::app::maps::tile_root,
                constants::app::maps::fixed_zoom,
                static_cast<long>(normalized_tile_x), static_cast<long>(tile_y),
                constants::app::maps::tile_extension);

  FILE *file = std::fopen(path, "rb");
  if (file == nullptr) {
    return;
  }

  const int32_t tile_size = constants::app::maps::tile_size_px;
  const int32_t copy_x0 = std::max<int32_t>(0, dest_x);
  const int32_t copy_y0 = std::max<int32_t>(0, dest_y);
  const int32_t copy_x1 = std::min(canvas_width_, dest_x + tile_size);
  const int32_t copy_y1 = std::min(canvas_height_, dest_y + tile_size);

  if (copy_x0 >= copy_x1 || copy_y0 >= copy_y1) {
    std::fclose(file);
    return;
  }

  const int32_t src_x0 = copy_x0 - dest_x;
  const int32_t src_y0 = copy_y0 - dest_y;
  const int32_t width = copy_x1 - copy_x0;

  std::array<uint16_t, constants::app::maps::tile_size_px> line{};
  for (int32_t y = copy_y0; y < copy_y1; ++y) {
    const int32_t src_y = src_y0 + (y - copy_y0);
    const long offset =
        static_cast<long>((src_y * tile_size + src_x0) * sizeof(uint16_t));
    if (std::fseek(file, offset, SEEK_SET) != 0) {
      break;
    }

    const size_t read =
        std::fread(line.data(), sizeof(uint16_t), static_cast<size_t>(width),
                   file);
    if (read != static_cast<size_t>(width)) {
      break;
    }

    std::memcpy(canvas_row(canvas_buffer_, canvas_width_, y) + copy_x0,
                line.data(), static_cast<size_t>(width) * sizeof(uint16_t));
  }

  std::fclose(file);
}

void MapRenderer::draw_location_dot() noexcept {
  if (canvas_buffer_ == nullptr || canvas_width_ <= 0 || canvas_height_ <= 0) {
    return;
  }

  const int32_t cx = canvas_width_ / 2;
  const int32_t cy = canvas_height_ / 2;
  constexpr int32_t outer_radius = 6;
  constexpr int32_t inner_radius = 4;

  for (int32_t y = -outer_radius; y <= outer_radius; ++y) {
    const int32_t py = cy + y;
    if (py < 0 || py >= canvas_height_) {
      continue;
    }
    for (int32_t x = -outer_radius; x <= outer_radius; ++x) {
      const int32_t px = cx + x;
      if (px < 0 || px >= canvas_width_) {
        continue;
      }

      const int32_t dist2 = x * x + y * y;
      if (dist2 <= inner_radius * inner_radius) {
        canvas_row(canvas_buffer_, canvas_width_, py)[px] = dot_inner_color;
      } else if (dist2 <= outer_radius * outer_radius) {
        canvas_row(canvas_buffer_, canvas_width_, py)[px] = dot_outer_color;
      }
    }
  }
}

void MapRenderer::fill_canvas(uint16_t color) noexcept {
  if (canvas_buffer_ == nullptr) {
    return;
  }

  const size_t pixels =
      static_cast<size_t>(canvas_width_) * static_cast<size_t>(canvas_height_);
  std::fill_n(canvas_buffer_, pixels, color);
}

} // namespace app
