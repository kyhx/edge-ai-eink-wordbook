/*
 * 启动自检程序
 *
 * 目标（对应 Issue #1 的第一步）：
 *   1. 确认工具链、烧录、串口日志链路可用
 *   2. 读出芯片型号、Flash 容量、PSRAM 容量，确认 N16R8 配置正确
 *   3. 把功放与麦克风置于关断状态，避免上电即工作
 *   4. 周期打印心跳与按键状态，便于验证 GPIO 读取
 */
#include <inttypes.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_psram.h"
#include "esp_system.h"

#include "board_config.h"

static const char *TAG = "selftest";

static const char *chip_model_name(esp_chip_model_t model)
{
    switch (model) {
    case CHIP_ESP32S3: return "ESP32-S3";
    default:           return "unknown";
    }
}

static void print_info(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);

    ESP_LOGI(TAG, "================ 启动自检 ================");
    ESP_LOGI(TAG, "芯片      : %s, %d 核, 版本 v%d.%d",
             chip_model_name(chip.model), chip.cores,
             chip.revision / 100, chip.revision % 100);

    uint32_t flash_size = 0;
    if (esp_flash_get_size(NULL, &flash_size) == ESP_OK) {
        ESP_LOGI(TAG, "Flash     : %" PRIu32 " MB", flash_size / (1024 * 1024));
    } else {
        ESP_LOGW(TAG, "Flash     : 读取失败");
    }

    size_t psram = esp_psram_get_size();
    if (psram > 0) {
        ESP_LOGI(TAG, "PSRAM     : %u KB", (unsigned)(psram / 1024));
    } else {
        ESP_LOGW(TAG, "PSRAM     : 未检测到（请确认 sdkconfig 为 Octal 模式）");
    }

    ESP_LOGI(TAG, "内部RAM   : 空闲 %u KB / 总 %u KB",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_total_size(MALLOC_CAP_INTERNAL) / 1024));
    if (psram > 0) {
        ESP_LOGI(TAG, "PSRAM堆   : 空闲 %u KB",
                 (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
    }

    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    ESP_LOGI(TAG, "Wi-Fi MAC : %02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    ESP_LOGI(TAG, "==========================================");
}

static void init_outputs(void)
{
    /* 功放默认关断，避免上电即工作、也避免悬空导致静态耗电 */
    gpio_config_t out = {
        .pin_bit_mask = (1ULL << PIN_AMP_EN) | (1ULL << PIN_MIC_PWR_EN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&out));
    gpio_set_level(PIN_AMP_EN, 0);
    gpio_set_level(PIN_MIC_PWR_EN, 0);
}

static void init_keys(void)
{
    gpio_config_t in = {
        .pin_bit_mask = (1ULL << PIN_KEY_PREV) | (1ULL << PIN_KEY_NEXT) |
                        (1ULL << PIN_KEY_OK)   | (1ULL << PIN_KEY_BACK),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&in));
}

void app_main(void)
{
    print_info();
    init_outputs();
    init_keys();

    ESP_LOGI(TAG, "自检开始：每秒打印一次心跳与按键电平");
    ESP_LOGI(TAG, "按键为低电平有效，未按下应全为 1");

    uint32_t tick = 0;
    while (true) {
        ESP_LOGI(TAG, "心跳 %" PRIu32 "s | 按键 PREV=%d NEXT=%d OK=%d BACK=%d | 空闲堆 %u KB",
                 tick,
                 gpio_get_level(PIN_KEY_PREV),
                 gpio_get_level(PIN_KEY_NEXT),
                 gpio_get_level(PIN_KEY_OK),
                 gpio_get_level(PIN_KEY_BACK),
                 (unsigned)(esp_get_free_heap_size() / 1024));
        tick++;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
