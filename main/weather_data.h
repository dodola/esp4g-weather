#pragma once

#include <string>
#include <string_view>

struct WeatherData {
    std::string time;
    double temperature_c = 0;
    double apparent_temperature_c = 0;
    double humidity_percent = 0;
    double wind_mps = 0;
    int code = 0;
};

bool ParseWeather(std::string_view body, WeatherData& data, std::string& error);
const char* WeatherDescription(int code);
std::string WeatherUrl(std::string_view latitude, std::string_view longitude);
