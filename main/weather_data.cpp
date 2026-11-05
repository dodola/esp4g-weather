#include "weather_data.h"

#include <cmath>
#include <memory>
#include "cJSON.h"

namespace {
bool Number(const cJSON* object, const char* key, double& value) {
    const auto* field = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsNumber(field) || !std::isfinite(field->valuedouble)) return false;
    value = field->valuedouble;
    return true;
}
}

bool ParseWeather(std::string_view body, WeatherData& data, std::string& error) {
    // 严格拒绝截断的 JSON、API 错误以及 null 字段，避免把异常当成零度。
    const std::string terminated(body);
    std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(
        cJSON_ParseWithLengthOpts(terminated.c_str(), terminated.size() + 1, nullptr, true), cJSON_Delete);
    if (!root) { error = "Invalid or incomplete weather JSON"; return false; }
    const auto* api_error = cJSON_GetObjectItemCaseSensitive(root.get(), "error");
    if (cJSON_IsTrue(api_error)) {
        const auto* reason = cJSON_GetObjectItemCaseSensitive(root.get(), "reason");
        error = cJSON_IsString(reason) ? reason->valuestring : "Weather API error";
        return false;
    }
    const auto* current = cJSON_GetObjectItemCaseSensitive(root.get(), "current");
    const auto* time = cJSON_GetObjectItemCaseSensitive(current, "time");
    WeatherData parsed;
    double code = 0;
    if (!cJSON_IsObject(current) || !cJSON_IsString(time) || !time->valuestring[0] ||
        !Number(current, "temperature_2m", parsed.temperature_c) ||
        !Number(current, "apparent_temperature", parsed.apparent_temperature_c) ||
        !Number(current, "relative_humidity_2m", parsed.humidity_percent) ||
        !Number(current, "wind_speed_10m", parsed.wind_mps) ||
        !Number(current, "weather_code", code) ||
        code < 0 || code > 99 || std::floor(code) != code ||
        parsed.humidity_percent < 0 || parsed.humidity_percent > 100 || parsed.wind_mps < 0) {
        error = "Missing or invalid current weather fields";
        return false;
    }
    parsed.time = time->valuestring;
    parsed.code = static_cast<int>(code);
    data = std::move(parsed);
    error.clear();
    return true;
}

const char* WeatherDescription(int code) {
    switch (code) {
        case 0: return "晴";
        case 1: return "大部晴朗";
        case 2: return "多云";
        case 3: return "阴";
        case 45: case 48: return "雾";
        case 51: case 53: case 55: return "毛毛雨";
        case 56: case 57: return "冻毛毛雨";
        case 61: return "小雨";
        case 63: return "中雨";
        case 65: return "大雨";
        case 66: case 67: return "冻雨";
        case 71: return "小雪";
        case 73: return "中雪";
        case 75: return "大雪";
        case 77: return "雪粒";
        case 80: case 81: case 82: return "阵雨";
        case 85: case 86: return "阵雪";
        case 95: return "雷雨";
        case 96: case 99: return "雷雨伴冰雹";
        default: return "未知天气";
    }
}

std::string WeatherUrl(std::string_view latitude, std::string_view longitude) {
    return "https://api.open-meteo.com/v1/forecast?latitude=" + std::string(latitude) +
        "&longitude=" + std::string(longitude) +
        "&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,wind_speed_10m"
        "&wind_speed_unit=ms&timezone=Asia%2FShanghai&forecast_days=1";
}
