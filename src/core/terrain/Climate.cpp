#include "core/terrain/Climate.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <numbers>
#include <sstream>
#include <stdexcept>

namespace orbis {

namespace {
constexpr double kPi = std::numbers::pi;
constexpr double kRadius = 6371000.0;
constexpr double kStep = 50000.0; // metres along the wind

/// +1 where the prevailing wind blows from the west, -1 from the east, by |latitude| degrees.
double westerly(double latitudeDeg) {
    constexpr std::array<double, 6> x = {0.0, 25.0, 35.0, 60.0, 70.0, 90.0};
    constexpr std::array<double, 6> y = {-1.0, -1.0, 1.0, 1.0, -1.0, -1.0};
    const double a = std::min(std::abs(latitudeDeg), 90.0);
    for (std::size_t i = 1; i < x.size(); ++i) {
        if (a <= x[i]) {
            return y[i - 1] + (y[i] - y[i - 1]) * (a - x[i - 1]) / (x[i] - x[i - 1]);
        }
    }
    return y.back();
}
} // namespace

Climate::Climate(const std::function<bool(double, double)>& isSea, const ClimateParameters& p)
    : p_(p), map_(static_cast<std::size_t>(kMapWidth) * kMapHeight) {
    const auto cellLatitude = [](int r) { return kPi / 2.0 - (r + 0.5) * kPi / kMapHeight; };
    const auto cellLongitude = [](int c) { return -kPi + (c + 0.5) * 2.0 * kPi / kMapWidth; };
    std::vector<unsigned char> sea(map_.size());
    for (int r = 0; r < kMapHeight; ++r) {
        for (int c = 0; c < kMapWidth; ++c) {
            sea[static_cast<std::size_t>(r * kMapWidth + c)] =
                isSea(cellLatitude(r), cellLongitude(c)) ? 1 : 0;
        }
    }

    std::vector<double> weights;
    double total = 0.0;
    for (double d = 0.0; d < p_.upwindRange; d += kStep) {
        weights.push_back(std::exp(-d / p_.upwindLength));
        total += weights.back();
    }
    for (double& w : weights) {
        w /= total;
    }

    const double cellAngle = 2.0 * kPi / kMapWidth;
    for (int r = 0; r < kMapHeight; ++r) {
        const double latitude = cellLatitude(r);
        const double s = westerly(latitude * 180.0 / kPi);
        // Poleward of ~78 degrees a step would wrap the whole parallel; the cap keeps it local.
        const double metresPerCell = kRadius * std::max(std::cos(latitude), 0.2) * cellAngle;
        const unsigned char* row = sea.data() + static_cast<std::size_t>(r) * kMapWidth;
        for (int c = 0; c < kMapWidth; ++c) {
            double west = 0.0;
            double east = 0.0;
            for (std::size_t k = 0; k < weights.size(); ++k) {
                const int cells = static_cast<int>(std::lround(static_cast<double>(k) * kStep /
                                                               metresPerCell));
                west += weights[k] * row[((c - cells) % kMapWidth + kMapWidth) % kMapWidth];
                east += weights[k] * row[(c + cells) % kMapWidth];
            }
            map_[static_cast<std::size_t>(r * kMapWidth + c)] =
                static_cast<float>(0.5 * (1.0 + s) * west + 0.5 * (1.0 - s) * east);
        }
    }
}

double Climate::oceanicity(double latitude, double longitude) const {
    const double x = (longitude + kPi) / (2.0 * kPi) * kMapWidth - 0.5;
    const double y = std::clamp((kPi / 2.0 - latitude) / kPi * kMapHeight - 0.5, 0.0,
                                static_cast<double>(kMapHeight - 1));
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = std::min(static_cast<int>(y), kMapHeight - 2);
    const double tx = x - x0;
    const double ty = y - y0;
    const auto at = [this](int row, int column) {
        column = (column % kMapWidth + kMapWidth) % kMapWidth;
        return static_cast<double>(map_[static_cast<std::size_t>(row * kMapWidth + column)]);
    };
    const double top = at(y0, x0) + tx * (at(y0, x0 + 1) - at(y0, x0));
    const double bottom = at(y0 + 1, x0) + tx * (at(y0 + 1, x0 + 1) - at(y0 + 1, x0));
    return top + ty * (bottom - top);
}

double Climate::winterness(double latitude, double dayOfYear) const {
    const double phase = std::cos(2.0 * kPi * (dayOfYear - p_.coldestDay) / kDaysPerYear);
    return latitude < 0.0 ? -phase : phase;
}

double Climate::temperature(double latitude, double longitude, double dayOfYear,
                            double height) const {
    const double oceanic = oceanicity(latitude, longitude);
    const double s2 = std::sin(latitude) * std::sin(latitude);
    double ice = std::clamp((-latitude * 180.0 / kPi - 50.0) / 20.0, 0.0, 1.0);
    ice = ice * ice * (3.0 - 2.0 * ice);
    const double mean = p_.equatorTemperature - p_.polarDrop * s2 -
                        p_.polarExtraDrop * s2 * s2 * s2 * s2 +
                        p_.maritimeWarmth * (oceanic - 0.5) * s2 - p_.antarcticCold * ice;
    const double southern = p_.southernDamping + (1.0 - p_.southernDamping) * ice;
    const double amplitude = (p_.equatorAmplitude + p_.polarAmplitude * s2) *
                             (latitude < 0.0 ? southern : 1.0) *
                             (1.0 + p_.continentality * (0.5 - oceanic));
    return mean - amplitude * winterness(latitude, dayOfYear) - p_.lapseRate * height;
}

std::string Climate::toGlsl() const {
    std::ostringstream s;
    s << std::setprecision(9) << std::showpoint;
    s << "// Generated from ClimateParameters (src/core/terrain/Climate.hpp). Do not edit.\n"
      << "const float kClimateEquator = " << p_.equatorTemperature << ";\n"
      << "const float kClimatePolarDrop = " << p_.polarDrop << ";\n"
      << "const float kClimatePolarExtraDrop = " << p_.polarExtraDrop << ";\n"
      << "const float kClimateEquatorAmplitude = " << p_.equatorAmplitude << ";\n"
      << "const float kClimatePolarAmplitude = " << p_.polarAmplitude << ";\n"
      << "const float kClimateSouthernDamping = " << p_.southernDamping << ";\n"
      << "const float kClimateAntarcticCold = " << p_.antarcticCold << ";\n"
      << "const float kClimateContinentality = " << p_.continentality << ";\n"
      << "const float kClimateMaritimeWarmth = " << p_.maritimeWarmth << ";\n"
      << "const float kClimateColdestDay = " << p_.coldestDay << ";\n"
      << "const float kClimateLapseRate = " << p_.lapseRate << ";\n"
      << "const float kClimateDaysPerYear = " << kDaysPerYear << ";\n";
    return s.str();
}

int dayOfYear(int month, int day) {
    constexpr std::array<int, 12> days = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12 || day < 1 || day > days[static_cast<std::size_t>(month - 1)]) {
        throw std::invalid_argument("invalid date");
    }
    int n = day - 1;
    for (int m = 1; m < month; ++m) {
        n += days[static_cast<std::size_t>(m - 1)];
    }
    return n;
}

} // namespace orbis
