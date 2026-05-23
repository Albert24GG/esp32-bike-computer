#pragma once

#include "ble_location_service.hpp"

#include "lvgl.h"

#include <cstdint>

namespace app {

class MapRenderer {
public:
  MapRenderer() noexcept = default;
  ~MapRenderer();

  MapRenderer(const MapRenderer &) = delete;
  MapRenderer &operator=(const MapRenderer &) = delete;

  void render_location(const LocationCoordinates &location) noexcept;
  void reset() noexcept;

private:
  struct ProjectedPoint {
    double x_px{};
    double y_px{};
  };

  [[nodiscard]] bool ensure_canvas() noexcept;
  [[nodiscard]] bool allocate_canvas_buffer(int32_t width,
                                            int32_t height) noexcept;
  [[nodiscard]] static ProjectedPoint project(double latitude,
                                              double longitude) noexcept;

  void draw_map(const ProjectedPoint &center) noexcept;
  void draw_tile(int32_t tile_x, int32_t tile_y, int32_t dest_x,
                 int32_t dest_y) noexcept;
  void draw_location_dot() noexcept;
  void fill_canvas(uint16_t color) noexcept;

  lv_obj_t *canvas_parent_{nullptr};
  lv_obj_t *canvas_{nullptr};
  uint16_t *canvas_buffer_{nullptr};
  size_t canvas_buffer_size_{0};
  int32_t canvas_width_{0};
  int32_t canvas_height_{0};
  int64_t last_center_x_px_{};
  int64_t last_center_y_px_{};
  bool has_last_center_{false};
};

} // namespace app
