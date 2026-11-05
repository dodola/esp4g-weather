#include "weather_data.h"
#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>

int main(int argc, char** argv) {
    const std::string valid = R"({"current":{"time":"2026-11-05T23:45","temperature_2m":5.9,"apparent_temperature":2.8,"relative_humidity_2m":47,"wind_speed_10m":0.65,"weather_code":0}})";
    WeatherData data;
    std::string error;
    assert(ParseWeather(valid, data, error));
    assert(error.empty() && std::abs(data.temperature_c - 5.9) < .0001);
    assert(data.code == 0 && data.humidity_percent == 47);
    assert(!ParseWeather(valid.substr(0, valid.size() - 1), data, error));
    assert(!ParseWeather(valid + "garbage", data, error));
    assert(!ParseWeather(R"({"error":true,"reason":"Invalid latitude"})", data, error));
    assert(error == "Invalid latitude");
    assert(!ParseWeather("{}", data, error));
    assert(!ParseWeather("null", data, error));
    assert(!ParseWeather(R"({"current":null})", data, error));
    assert(!ParseWeather(R"({"current":{"time":"now","temperature_2m":null}})", data, error));
    auto invalid_humidity = valid;
    invalid_humidity.replace(invalid_humidity.find(":47"), 3, ":101");
    assert(!ParseWeather(invalid_humidity, data, error));
    auto valid_freezing = valid;
    valid_freezing.replace(valid_freezing.find(":5.9"), 4, ":-15.5");
    assert(ParseWeather(valid_freezing, data, error) && data.temperature_c == -15.5);
    assert(WeatherUrl("39.9042", "116.4074").find("timezone=Asia%2FShanghai") != std::string::npos);
    if (argc > 1) {
        std::ifstream file(argv[1]);
        assert(file.is_open());
        const std::string body((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        assert(ParseWeather(body, data, error));
        std::cout << "Live API fixture parsed: time=" << data.time << " temperature=" << data.temperature_c << '\n';
    }
    std::cout << "Weather parser checks passed\n";
}
