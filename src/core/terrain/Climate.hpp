#pragma once

#include <functional>
#include <string>
#include <vector>

namespace orbis {

/// Monthly-mean near-surface temperature: a zonal mean and annual cycle by latitude, scaled by
/// how maritime the air is, minus a lapse rate. Least-squares fit to January and July means of
/// 40 stations on all continents (tools/fit_climate.py): median error 2.4 C, worst in the
/// winters of Siberia, Canada and the Arctic and on the Norwegian coast, where ocean currents
/// matter more than distance to the sea.
struct ClimateParameters {
    double equatorTemperature = 28.65; ///< C, annual mean at sea level
    double polarDrop = 29.52;          ///< times sin^2(latitude)
    double polarExtraDrop = 9.18;      ///< times sin^8(latitude): the polar caps
    double equatorAmplitude = 3.16;    ///< C, half the annual range
    double polarAmplitude = 10.97;     ///< times sin^2(latitude)
    /// The southern ocean damps the seasons, except where it freezes around Antarctica (50 to
    /// 70 S), which also adds the ice sheet's cold.
    double southernDamping = 0.74;
    double antarcticCold = 8.81;       ///< C
    double continentality = 1.08;      ///< Amplitude gain from maritime to continental air.
    double maritimeWarmth = 3.31;      ///< C, times sin^2(latitude) from continental to maritime
    double coldestDay = 20.0;          ///< Northern hemisphere, days after 1 January.
    double lapseRate = 0.0065;         ///< C per metre
    /// Oceanicity weights the sea along the prevailing wind by exp(-distance / upwindLength).
    double upwindLength = 1.2e6; ///< metres
    double upwindRange = 4.0e6;
};

class Climate {
public:
    static constexpr int kMapWidth = 720;
    static constexpr int kMapHeight = 360;
    static constexpr double kDaysPerYear = 365.25;

    /// `isSea(latitude, longitude)` in radians builds the oceanicity map on a 0.5 degree grid.
    explicit Climate(const std::function<bool(double, double)>& isSea,
                     const ClimateParameters& p = {});

    /// 0 continental .. 1 maritime: the sea fraction upwind of the prevailing zonal wind
    /// (trades below 30 degrees, westerlies to 65, polar easterlies beyond). Bilinear in the
    /// cell-registered map, like the GPU's texture.
    double oceanicity(double latitude, double longitude) const;

    /// Celsius at `height` metres; latitude and longitude in radians, day 0 = 1 January.
    double temperature(double latitude, double longitude, double dayOfYear, double height) const;

    /// 1 at the local temperature minimum, -1 at the maximum.
    double winterness(double latitude, double dayOfYear) const;

    /// Cell-registered, rows from north to south.
    const std::vector<float>& oceanicityMap() const { return map_; }

    /// GLSL constants for climate.glsl.
    std::string toGlsl() const;

    const ClimateParameters& parameters() const { return p_; }

private:
    ClimateParameters p_;
    std::vector<float> map_;
};

/// Day 0 = 1 January, no leap years. Throws std::invalid_argument for an impossible date.
int dayOfYear(int month, int day);

} // namespace orbis
