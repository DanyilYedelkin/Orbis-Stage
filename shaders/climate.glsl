// Twin of Climate::temperature(); the constants are generated from ClimateParameters.
uniform sampler2D uOceanicity;
uniform float uDayOfYear;

float climatePhase() {
    return 2.0 * kPi * (uDayOfYear - kClimateColdestDay) / kClimateDaysPerYear;
}

// 1 at the local temperature minimum, -1 at the maximum.
float climateWinterness(float latitude) {
    float c = cos(climatePhase());
    return latitude < 0.0 ? -c : c;
}

// 1 while temperatures fall fastest (autumn), -1 in spring.
float climateCooling(float latitude) {
    float s = sin(climatePhase());
    return latitude < 0.0 ? s : -s;
}

float climateTemperature(float latitude, float longitude, float height) {
    float oceanic = texture(uOceanicity, vec2((longitude + kPi) / (2.0 * kPi),
                                              (0.5 * kPi - latitude) / kPi)).r;
    float s2 = sin(latitude) * sin(latitude);
    float s8 = s2 * s2 * s2 * s2;
    float ice = smoothstep(50.0, 70.0, -degrees(latitude));
    float mean = kClimateEquator - kClimatePolarDrop * s2 - kClimatePolarExtraDrop * s8 +
                 kClimateMaritimeWarmth * (oceanic - 0.5) * s2 - kClimateAntarcticCold * ice;
    float southern = mix(kClimateSouthernDamping, 1.0, ice);
    float amplitude = (kClimateEquatorAmplitude + kClimatePolarAmplitude * s2) *
                      (latitude < 0.0 ? southern : 1.0) *
                      (1.0 + kClimateContinentality * (0.5 - oceanic));
    return mean - amplitude * climateWinterness(latitude) - kClimateLapseRate * height;
}
