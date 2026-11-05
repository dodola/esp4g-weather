# ESP32-C3 + ML307 4G 天气应用

适用于飞书资料中的 **WiFi + 4G V5.1 一体板**。ESP32 通过 UART 控制 ML307，使用 SIM 卡的 4G 网络请求 Open-Meteo，把当前天气输出到 USB 串口。默认查询北京，无需 Wi-Fi 或天气 API Key。

## 硬件

| ESP32-C3 | 板上 ML307 |
| --- | --- |
| GPIO3 / TX | UART0_RXD |
| GPIO4 / RX | UART0_TXD |
| GPIO5 / MODEM_EN | 经三极管控制 NET_PWR / PWR_ON/OFF |
| USB Type-C | 供电及 ESP32 USB Serial/JTAG 日志、下载 |

一体板已连接以上线路。插好可用的 SIM 卡和 4G 天线，再用可传输数据的 USB 线连接电脑。将以下命令中的 `COM39` 换成实际串口号。

GPIO5 单独控制开机，**不能作为 `AtModem::Detect()` 的 DTR 参数**。本项目给 DTR 传入 `GPIO_NUM_NC`。针对本机 ML307C，默认启用 `WEATHER_PWRKEY_PULSE`，GPIO5 高电平持续 2.5 秒后释放；高电平经三极管对应模组 PWR_ON/OFF 低电平。手册规定开机脉冲 2～3.5 秒，关机脉冲 3.5～4 秒；持续拉高 GPIO5 在 ESP32 重启而模组未断电时可能触发关机，因此这里采用开机脉冲。只有需要持续使能的其他模组/电路才应关闭此选项。

## 构建与烧录（Windows PowerShell）

使用 **ESP-IDF 5.5.2** 和 ESP32-C3 工具链。第三方依赖固定为 `78/esp-ml307 3.8.1` 和 `78/uart-uhci 0.4.1`，首次构建需要联网下载。在仓库根目录执行：

```powershell
powershell -ExecutionPolicy Bypass -File tools\idf.ps1 build
powershell -ExecutionPolicy Bypass -File tools\idf.ps1 flash -Port COM39
powershell -ExecutionPolicy Bypass -File tools\idf.ps1 monitor -Port COM39
```

退出 IDF monitor：`Ctrl+]`。其他安装路径可传 `-IdfPath`、`-ToolsPath`，或在已激活的 IDF 终端直接使用 `idf.py build` / `idf.py -p COM39 flash monitor`。

在已激活的 IDF 终端执行 `idf.py merge-bin` 可生成完整合并固件，从地址 `0x0` 烧录。单独烧录 `build/esp4g_weather.bin` 时使用工程的应用分区地址。

也可有限时间记录 USB 日志：

```powershell
python tools\serial_log.py --port COM39 --seconds 120 --until-weather
```

## 更换城市与刷新间隔

```powershell
powershell -ExecutionPolicy Bypass -File tools\idf.ps1 menuconfig
```

进入 **4G Weather**，修改城市显示名、纬度、经度，保存后重新 `build` 和 `flash`。

| 城市 | 纬度 | 经度 |
| --- | --- | --- |
| 北京（默认） | 39.9042 | 116.4074 |
| 上海 | 31.2304 | 121.4737 |
| 深圳 | 22.5431 | 114.0579 |

成功后每 600 秒刷新，失败后每 30 秒重试。日志以 `WEATHER_OK` 标记成功，包含接口给出的北京时间（CST）、天气描述、摄氏温度、体感温度、湿度、风速（m/s）和 WMO 代码。Open-Meteo 的当前天气来自天气模型，并非板载传感器测量。

## 实现与排查

- `main/weather_main.cpp`：模组上电、探测、SIM/驻网检查、PDP 激活、HTTPS 请求和重试。
- `main/weather_data.cpp`：请求 URL、JSON 校验、天气代码的中文解释。
- 使用 `std::expected` 检查 3.8.1 的错误结果；HTTP 流式读取，正文最多 8 KiB，拒绝截断 JSON 与 null 数据。
- 自动 APN 默认留空。专用物联网卡可在菜单指定运营商提供的 APN；不应盲目给所有卡设置 `cmnet`。
- HTTPS 在创建客户端前开启 SSL 上下文 0 的 **SNI** 和 TLS 1.2。本机 ML307C 在 SNI 关闭时连接异常断开，开启后已实测 HTTP 200。
- 网络注册成功后使用 `MIPCALL`，避免用 `CGACT` 去激活默认承载。
- 每 5 秒重新查询驻网状态，避免仅依赖组件 3.8.1 对单参数 `CEREG` 通知的处理。
- `Modem detect` 失败：检查供电、模组是否开机、TX/RX 和 115200 波特率。
- `SIM absent or PIN locked`：检查卡座、SIM 和 PIN 状态。
- 注册超时或拒绝：检查天线、信号、SIM 套餐与运营商支持。
- HTTP/TLS/DNS 失败：日志保留组件的错误分类与原始错误码；检查 SIM 数据业务、APN 和模组固件的 HTTPS 支持。

HTTPS 使用模组内置 SSL，证书校验行为由模组 SSL 上下文决定。本例没有配置自有 CA，不能据此声称完成了服务器身份校验。生产环境应根据本地 SSL 手册配置 CA、时间与校验模式。

组件 3.8.1 对 chunked 响应的空 EOF 通知可能打印 `Ml307Http: Missing content`。若紧接着看到 `WEATHER_OK`，正文已完整接收并通过 JSON 校验，该条日志来自组件对零长度结束标记的处理。

## 验证

使用 ESP-IDF 5.5.2 编译并烧录到 ESP32-C3 + ML307C-DC-CN-MBRH0S00。在中国电信 SIM 上通过 4G 收到 Open-Meteo HTTPS 200。烧录后及仅重启 ESP32 后均在约 14 秒内输出 `WEATHER_OK`。以下为重启实测节选：

```text
I (10681) WEATHER: Cellular network registered
I (13791) WEATHER: HTTP status=200
I (13911) WEATHER: WEATHER_OK city=Beijing time=2026-11-06T00:15 CST | 晴 | 5.5 C (feels 2.5 C) | RH 52% | wind 0.65 m/s | WMO 0
```

解析逻辑的本机测试覆盖成功响应、实际 API 响应、负温度、截断 JSON、尾随垃圾、API 错误、缺失/null 字段和越界湿度。使用本机 MinGW 和 IDF 自带的 cJSON 可复跑：

```powershell
powershell -ExecutionPolicy Bypass -File tools\test.ps1 -IdfPath $env:IDF_PATH -CompilerBin 'C:\msys64\ucrt64\bin'
```

## 参考资料

- [V5.1 一体板原理图与资料](https://my.feishu.cn/wiki/UIK3wQoRKiFccFkwiumccAIynTf)
- [V5.1 一体板资料文字版 PDF](output/pdf/wifi-4g-board-v5.1-text.pdf)
- [短信转发项目（接线和开机控制参考）](https://github.com/MineSunshineone/sms_forwarding)
- [esp-ml307 3.8.1](https://components.espressif.com/components/78/esp-ml307/versions/3.8.1/readme)
- [Open-Meteo API 文档](https://open-meteo.com/en/docs)，天气数据来源为 Open-Meteo，使用需遵守其条款及署名要求。
- [飞书原文中的 ML307C 资料包](https://my.feishu.cn/wiki/UIK3wQoRKiFccFkwiumccAIynTf#PhaYdUFOhoMZfwxAa8fcfoz7nkd)。其中的《ML307C 硬件设计手册（AT 版本适用）》《ML307C 通信流程示例》《HTTP/HTTPS 用户手册》和《SSL 用户手册》可分别查阅开机时序、驻网流程、HTTP 命令和 TLS 设置。

原固件备份放在 `backups/`，串口验证日志放在 `logs/`；二者均不会进入 Git。
