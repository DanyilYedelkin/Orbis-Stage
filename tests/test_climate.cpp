#include "core/terrain/Climate.hpp"
#include "core/terrain/ElevationGrid.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <numbers>

using namespace orbis;
using Catch::Matchers::WithinAbs;

namespace {
constexpr double kDeg = std::numbers::pi / 180.0;

const Climate& earthClimate() {
    static const ElevationGrid grid =
        ElevationGrid::load(std::filesystem::path(ORBIS_TEST_DATA_DIR) / "earth_etopo10.oeg");
    static const Climate climate(
        [](double lat, double lon) { return grid.sample(lat, lon, 1.0) < 0.0; });
    return climate;
}

double temperature(double latDeg, double lonDeg, int month, double height) {
    return earthClimate().temperature(latDeg * kDeg, lonDeg * kDeg, dayOfYear(month, 15), height);
}
} // namespace

TEST_CASE("day of year", "[climate]") {
    CHECK(dayOfYear(1, 1) == 0);
    CHECK(dayOfYear(3, 1) == 59);
    CHECK(dayOfYear(12, 31) == 364);
    CHECK_THROWS(dayOfYear(2, 29));
    CHECK_THROWS(dayOfYear(13, 1));
}

TEST_CASE("oceanicity follows the prevailing winds", "[climate]") {
    const Climate& c = earthClimate();
    const auto at = [&](double lat, double lon) { return c.oceanicity(lat * kDeg, lon * kDeg); };
    CHECK(at(-45.0, -130.0) > 0.95);                // southern Pacific
    CHECK(at(62.0, 129.7) < 0.1);                   // Yakutsk
    // Westerlies: Europe's west coast is maritime, North America's east coast continental.
    CHECK(at(48.4, -4.5) > 0.8);                    // Brest
    CHECK(at(42.4, -71.1) < 0.2);                   // Boston
    CHECK(at(48.4, -4.5) > at(48.7, 21.3) + 0.4);   // Brest vs Kosice
    const auto [lowest, highest] =
        std::minmax_element(c.oceanicityMap().begin(), c.oceanicityMap().end());
    CHECK(*lowest >= 0.0f);
    CHECK(*highest <= 1.0f);
}

TEST_CASE("temperatures follow the seasons of both hemispheres", "[climate]") {
    CHECK(temperature(48.7, 21.3, 1, 206.0) < temperature(48.7, 21.3, 7, 206.0) - 15.0);
    CHECK(temperature(-34.6, -58.4, 1, 25.0) > temperature(-34.6, -58.4, 7, 25.0) + 8.0);
    CHECK(std::abs(temperature(1.3, 103.8, 1, 15.0) - temperature(1.3, 103.8, 7, 15.0)) <
          0.4 * (temperature(48.7, 21.3, 7, 0.0) - temperature(48.7, 21.3, 1, 0.0)));
    // Continuous over new year, colder upwards.
    const Climate& c = earthClimate();
    CHECK_THAT(c.temperature(0.8, 0.3, 0.0, 0.0),
               WithinAbs(c.temperature(0.8, 0.3, Climate::kDaysPerYear, 0.0), 1e-9));
    CHECK_THAT(temperature(46.0, 7.7, 7, 3000.0),
               WithinAbs(temperature(46.0, 7.7, 7, 0.0) - 19.5, 1e-9));
}

TEST_CASE("climate matches well-known places", "[climate]") {
    // Monthly means, 1991-2020; the zonal model is good to a few degrees away from the
    // continental interiors.
    CHECK_THAT(temperature(48.7, 21.3, 1, 206.0), WithinAbs(-2.5, 4.0));   // Kosice
    CHECK_THAT(temperature(48.7, 21.3, 7, 206.0), WithinAbs(20.0, 4.0));
    CHECK_THAT(temperature(48.85, 2.35, 1, 35.0), WithinAbs(5.0, 4.0));    // Paris
    CHECK_THAT(temperature(30.0, 31.2, 7, 23.0), WithinAbs(28.0, 4.0));    // Cairo
    CHECK_THAT(temperature(-33.9, 151.2, 1, 6.0), WithinAbs(23.0, 4.0));   // Sydney
    CHECK_THAT(temperature(-54.8, -68.3, 7, 50.0), WithinAbs(1.5, 4.0));   // Ushuaia
    CHECK_THAT(temperature(-1.3, 36.8, 1, 1795.0), WithinAbs(19.0, 4.0));  // Nairobi
}

TEST_CASE("seasonal snow and sea ice where they belong", "[climate]") {
    // Snow lies where the monthly mean is below freezing.
    CHECK(temperature(46.0, 7.7, 1, 1500.0) < -2.0);  // Alpine valleys in January
    CHECK(temperature(46.0, 7.7, 7, 1500.0) > 8.0);   // and in July
    CHECK(temperature(46.0, 7.7, 7, 3800.0) < 0.0);   // high Alps stay white
    CHECK(temperature(60.0, 100.0, 1, 300.0) < -15.0); // Siberia
    CHECK(temperature(85.0, 0.0, 1, 0.0) < -10.0);    // Arctic sea ice in winter
    CHECK(temperature(85.0, 0.0, 7, 0.0) < 3.0);      // and summer, just above freezing
    CHECK(temperature(-65.0, 0.0, 8, 0.0) < -5.0);    // Antarctic pack ice in August
    CHECK(temperature(-60.0, 0.0, 2, 0.0) > -2.0);    // and its retreat in February
    CHECK(temperature(40.0, -100.0, 7, 500.0) > 20.0); // Kansas summer
}
