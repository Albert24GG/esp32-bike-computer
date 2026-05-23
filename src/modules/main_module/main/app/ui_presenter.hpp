#pragma once

#include "app_settings.hpp"
#include "ride_metrics.hpp"

namespace app::ui_presenter {

void write_metrics(const ride_metrics::RideMetrics::Snapshot &snapshot,
                   const Settings &settings) noexcept;

void write_settings(const Settings &settings) noexcept;

Settings read_settings(const Settings &fallback) noexcept;

void write_maps_not_connected() noexcept;

void write_maps_connecting() noexcept;

void write_maps_connected_waiting() noexcept;

void write_maps_location(double latitude, double longitude,
                         float accuracy_m) noexcept;

} // namespace app::ui_presenter
