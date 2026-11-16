#pragma once
#include <string>

namespace horizon {
// Export clips from actual simulation frames; no display or graphics dependency.
bool export_preview(const std::string& directory);
bool export_ship_preview(const std::string& directory);
}
