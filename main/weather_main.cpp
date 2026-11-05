#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <string>

#include "sdkconfig.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "at_modem.h"
#include "weather_data.h"

namespace {
constexpr const char* TAG = "WEATHER";
constexpr size_t kMaxBody = 8192;

void Delay(int seconds) { vTaskDelay(pdMS_TO_TICKS(seconds * 1000)); }

bool ValidCoordinate(const char* text, double limit) {
    char* end = nullptr;
    const double value = std::strtod(text, &end);
    return end != text && *end == '\0' && std::isfinite(value) && std::abs(value) <= limit;
}

bool Command(AtModem& modem, const std::string& command, int timeout = 5000) {
    auto result = modem.GetAtUart()->SendCommand(command, timeout);
    if (!result) ESP_LOGW(TAG, "AT command failed: %s", result.error().ToString().c_str());
    return result.has_value();
}

void StartModemPower() {
    // 飞书 V5.1 的 GPIO5 通过三极管拉低 PWR_ON/OFF，并非 Detect() 的 DTR。
    gpio_config_t config{};
    config.pin_bit_mask = 1ULL << CONFIG_WEATHER_EN_GPIO;
    config.mode = GPIO_MODE_OUTPUT;
    ESP_ERROR_CHECK(gpio_config(&config));
    ESP_ERROR_CHECK(gpio_set_level(static_cast<gpio_num_t>(CONFIG_WEATHER_EN_GPIO), 0));
    Delay(2);
    ESP_ERROR_CHECK(gpio_set_level(static_cast<gpio_num_t>(CONFIG_WEATHER_EN_GPIO), 1));
#ifdef CONFIG_WEATHER_PWRKEY_PULSE
    vTaskDelay(pdMS_TO_TICKS(2500));
    ESP_ERROR_CHECK(gpio_set_level(static_cast<gpio_num_t>(CONFIG_WEATHER_EN_GPIO), 0));
#endif
    Delay(6);
}

NetworkStatus WaitForNetwork(AtModem& modem) {
    // 3.8.1 对单参数 CEREG URC 的处理不完整；周期查询可覆盖晚驻网场景。
    const auto deadline = esp_timer_get_time() + 60000000;
    NetworkStatus status;
    do {
        status = modem.WaitForNetworkReady(5000);
        if (status != NetworkStatus::ErrorTimeout || !modem.pin_ready()) return status;
    } while (esp_timer_get_time() < deadline);
    return status;
}

bool FetchWeather(AtModem& modem, const std::string& url) {
    // 每次创建客户端，确保失败后的 HTTP 实例和响应状态都能释放。
    auto http = modem.CreateHttp(0);
    if (!http) return false;
    http->SetTimeout(30000);
    http->SetHeader("Accept", "application/json");
    http->SetHeader("Accept-Encoding", "identity");
    http->SetHeader("User-Agent", "ESP4G-Weather/1.0");
    const auto close = [&http]() { http->Close(); };
    if (auto opened = http->Open("GET", url); !opened) {
        ESP_LOGE(TAG, "HTTP open: %s", opened.error().ToString().c_str());
        close(); return false;
    }
    const auto status = http->GetStatusCode();
    if (!status) {
        ESP_LOGE(TAG, "HTTP status: %s", status.error().ToString().c_str());
        close(); return false;
    }
    ESP_LOGI(TAG, "HTTP status=%d", *status);
    if (*status != 200) { close(); return false; }
    if (http->GetBodyLength() > kMaxBody) {
        ESP_LOGE(TAG, "Weather response exceeds %u bytes", static_cast<unsigned>(kMaxBody));
        close(); return false;
    }
    std::string body;
    char buffer[512];
    const auto deadline = esp_timer_get_time() + 60000000;
    while (true) {
        auto count = http->Read(buffer, sizeof(buffer));
        if (!count) {
            ESP_LOGE(TAG, "HTTP read: %s", count.error().ToString().c_str());
            close(); return false;
        }
        if (*count == 0) break;
        if (*count < 0 || body.size() + *count > kMaxBody || esp_timer_get_time() > deadline) {
            ESP_LOGE(TAG, "Weather response exceeded size/time limit");
            close(); return false;
        }
        body.append(buffer, *count);
    }
    close();
    WeatherData weather;
    std::string error;
    if (!ParseWeather(body, weather, error)) {
        ESP_LOGE(TAG, "Weather parse: %s", error.c_str());
        return false;
    }
    ESP_LOGI(TAG, "WEATHER_OK city=%s time=%s CST | %s | %.1f C (feels %.1f C) | RH %.0f%% | wind %.2f m/s | WMO %d",
             CONFIG_WEATHER_CITY, weather.time.c_str(), WeatherDescription(weather.code),
             weather.temperature_c, weather.apparent_temperature_c, weather.humidity_percent,
             weather.wind_mps, weather.code);
    ESP_LOGI(TAG, "天气数据来源: Open-Meteo (https://open-meteo.com/)");
    return true;
}
}

extern "C" void app_main() {
    ESP_LOGI(TAG, "ESP32-C3 + ML307 4G weather; TX=%d RX=%d EN=%d, baud=%d",
             CONFIG_WEATHER_TX_GPIO, CONFIG_WEATHER_RX_GPIO, CONFIG_WEATHER_EN_GPIO, CONFIG_WEATHER_BAUD_RATE);
    if (CONFIG_WEATHER_TX_GPIO == CONFIG_WEATHER_RX_GPIO ||
        CONFIG_WEATHER_TX_GPIO == CONFIG_WEATHER_EN_GPIO || CONFIG_WEATHER_RX_GPIO == CONFIG_WEATHER_EN_GPIO ||
        !ValidCoordinate(CONFIG_WEATHER_LATITUDE, 90) || !ValidCoordinate(CONFIG_WEATHER_LONGITUDE, 180)) {
        ESP_LOGE(TAG, "Invalid GPIO/coordinate configuration; check menuconfig -> 4G Weather");
        return;
    }
    const std::string apn = CONFIG_WEATHER_APN;
    if (apn.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-") != std::string::npos) {
        ESP_LOGE(TAG, "Invalid APN"); return;
    }
    StartModemPower();
    std::unique_ptr<AtModem> modem;
    while (!modem) {
        // GPIO5 是开机控制；没有接 DTR/RI，因此必须传 GPIO_NUM_NC。
        auto detected = AtModem::Detect(static_cast<gpio_num_t>(CONFIG_WEATHER_TX_GPIO),
            static_cast<gpio_num_t>(CONFIG_WEATHER_RX_GPIO), GPIO_NUM_NC, CONFIG_WEATHER_BAUD_RATE, 10000);
        if (detected) { modem = std::move(*detected); break; }
        ESP_LOGE(TAG, "Modem detect: %s; check power/UART/115200 baud. Retry in 30s",
                 detected.error().ToString().c_str());
        Delay(30);
    }
    modem->OnNetworkStateChanged([](bool ready) {
        // 回调处于 AT 事件任务，禁止在此发送 AT 指令或请求 HTTP。
        ESP_LOGI(TAG, "Cellular network %s", ready ? "registered" : "disconnected");
    });
#ifdef CONFIG_WEATHER_AT_DEBUG
    modem->GetAtUart()->SetDebug(true);
#endif
    ESP_LOGI(TAG, "Modem revision: %s", modem->GetModuleRevision().c_str());
    Command(*modem, "ATE0");
    Command(*modem, "AT+CMEE=2");
    Command(*modem, "AT+CFUN?");
    // ML307C 的 SNI 默认关闭；共享 HTTPS 域名需要在创建 HTTP 实例前启用。
    if (!Command(*modem, "AT+MSSLCFG=\"sni\",0,1") ||
        !Command(*modem, "AT+MSSLCFG=\"version\",0,3")) {
        ESP_LOGW(TAG, "TLS setup failed; check module firmware SSL support");
    }
    std::atomic<bool> pdp_ready{false};
    modem->GetAtUart()->RegisterUrcCallback([&pdp_ready](const std::string& command,
                                                      const std::vector<AtArgumentValue>& args) {
        if (command == "MIPCALL" && args.size() >= 2 && args[0].int_value == 1) {
            pdp_ready.store(args[1].int_value == 1);
        } else if (command == "MATREADY") {
            pdp_ready.store(false);
        }
    });
    if (!apn.empty() && !Command(*modem, "AT+CGDCONT=1,\"IPV4V6\",\"" + apn + "\"")) {
        ESP_LOGE(TAG, "APN configuration failed");
    }
    const auto url = WeatherUrl(CONFIG_WEATHER_LATITUDE, CONFIG_WEATHER_LONGITUDE);
    while (true) {
        const auto network = WaitForNetwork(*modem);
        if (network != NetworkStatus::Ready || !modem->pin_ready()) {
            const char* reason = "AT/network error";
            if (network == NetworkStatus::ErrorInsertPin || !modem->pin_ready()) reason = "SIM absent or PIN locked";
            else if (network == NetworkStatus::ErrorRegistrationDenied) reason = "registration denied: check SIM service/antenna";
            else if (network == NetworkStatus::ErrorTimeout) reason = "registration timed out: check antenna/signal/SIM";
            ESP_LOGE(TAG, "Network unavailable: %s", reason);
            Delay(CONFIG_WEATHER_RETRY_SECONDS); continue;
        }
        // ML307C 默认自动拨号；MIPCALL 补充激活，不能混用 CGACT 去激活默认承载。
        pdp_ready.store(false);
        Command(*modem, "AT+MIPCALL?");
        if (!pdp_ready.load() && !Command(*modem, "AT+MIPCALL=1,1", 30000)) {
            ESP_LOGW(TAG, "PDP activation failed; checking HTTP request next");
        }
        ESP_LOGI(TAG, "Carrier=%s CSQ=%d (99 means unknown)", modem->GetCarrierName().c_str(), modem->GetCsq());
        const bool success = FetchWeather(*modem, url);
        const int interval = success ? CONFIG_WEATHER_INTERVAL_SECONDS : CONFIG_WEATHER_RETRY_SECONDS;
        ESP_LOGI(TAG, "Next weather request in %d seconds", interval);
        Delay(interval);
    }
}
