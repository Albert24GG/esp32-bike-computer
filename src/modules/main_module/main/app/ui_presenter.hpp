#pragma once

#include "app_settings.hpp"
#include "ride_metrics.hpp"

namespace app::ui_presenter {

void write_metrics(const ride_metrics::RideMetrics::Snapshot &snapshot,
                   const Settings &settings) noexcept;

void write_settings(const Settings &settings) noexcept;

Settings read_settings(const Settings &fallback) noexcept;

} // namespace app::ui_presenter
