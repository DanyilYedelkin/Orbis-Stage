#include "core/terrain/HighResolutionTerrain.hpp"

#include <algorithm>
#include <cmath>

namespace orbis {

namespace {
constexpr double kMeanRadius = 6371000.0;

double smoothstep(double edge0, double edge1, double x) {
    const double t = std::clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}
} // namespace

HgtMosaicTerrain::HgtMosaicTerrain(ElevationGrid grid)
    : grid_(std::move(grid)), coverage_(grid_.validityMask()),
      spacing_(grid_.nodeSpacing(0, kMeanRadius)) {}

std::optional<HighResolutionTerrain::Sample>
HgtMosaicTerrain::sample(double latitude, double longitude, double sampleSpacing) const {
    if (!grid_.contains(latitude, longitude)) {
        return std::nullopt;
    }
    const double level = sampleSpacing > 0.0 ? std::max(std::log2(sampleSpacing / spacing_), 0.0) : 0.0;
    const double real = grid_.sample(latitude, longitude, level);
    if (std::isnan(real)) {
        return std::nullopt;
    }
    // Blend into the base layer near ANY boundary of the data - the outer edge of the tiles, a
    // tile that was not downloaded, a void - so there is never a cliff. The coverage mask read
    // at its coarsest level is a blurred 0..1 "is there data around".
    const double coverage =
        coverage_.sample(latitude, longitude, static_cast<double>(coverage_.levelCount() - 1));
    const double edge = grid_.angularDistanceToEdge(latitude, longitude) * kMeanRadius;
    const double weight =
        smoothstep(0.55, 0.95, coverage) * smoothstep(0.0, kHighResolutionFeather, edge);
    return Sample{real, weight, spacing_};
}

} // namespace orbis
