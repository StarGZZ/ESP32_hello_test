#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_psram.h"
#include "esp_mac.h" /* 坑D: IDF v5.x 需显式引入，否则 esp_efuse_mac_get_default 隐式声明报错 */
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "led_strip.h"

#define WS2812_GPIO     48
#define WS2812_LED_NUM  1
#define WS2812_MAX_BRIGHTNESS 8   /* 恒定亮度 0-255，越小越暗 */
#define SCAN_PERIOD_S   10

static const char *TAG = "hello_test";
static led_strip_handle_t s_led = NULL;

static void print_chip_info(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);

    uint32_t flash_size = 0;
    esp_flash_get_size(esp_flash_default_chip, &flash_size);

    uint32_t psram_size = esp_psram_get_size();

    uint8_t mac[6] = {0};
    esp_efuse_mac_get_default(mac);

    printf("=============== Chip Info ===============\n");
    printf("Model          : %s\n",
           chip.model == CHIP_ESP32S3 ? "ESP32-S3" : "Unknown");
    printf("Cores          : %d\n", chip.cores);
    printf("Flash size     : %lu KB (%lu MB)\n",
           (unsigned long)flash_size, (unsigned long)(flash_size >> 20));
    if (psram_size) {
        printf("PSRAM size     : %lu KB (%lu MB)\n",
               (unsigned long)(psram_size >> 10), (unsigned long)(psram_size >> 20));
    } else {
        printf("PSRAM size     : 0 (not detected)\n");
    }
    printf("MAC            : %02X:%02X:%02X:%02X:%02X:%02X\n",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    printf("IDF version    : %s\n", esp_get_idf_version());
    printf("=========================================\n");
}

static void ws2812_init(void)
{
    led_strip_config_t strip_cfg = {
        .strip_gpio_num = WS2812_GPIO,
        .max_leds = WS2812_LED_NUM,
    };
    led_strip_rmt_config_t rmt_cfg = {
        .resolution_hz = 10 * 1000 * 1000, /* 10MHz */
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_led));
    led_strip_clear(s_led);
}

/* 彩虹流光灯：亮度恒定，色相约30s流转一圈 */
static void ws2812_breathe_task(void *arg)
{
    uint16_t hue = 0;
    int frame = 0;
    while (1) {
        led_strip_set_pixel_hsv(s_led, 0, (uint32_t)hue * 360 / 256, 255, WS2812_MAX_BRIGHTNESS);
        led_strip_refresh(s_led);
        vTaskDelay(pdMS_TO_TICKS(12));

        if (++frame >= 10) { /* 每10帧推进1级色相：约30s流转一圈 */
            frame = 0;
            hue = (hue + 1) & 0xFF;
        }
    }
}

static void wifi_scan_task(void *arg)
{
    while (1) {
        wifi_scan_config_t cfg = {
            .show_hidden = true,
        };
        uint16_t ap_num = 0;
        if (esp_wifi_scan_start(&cfg, true) == ESP_OK &&
            esp_wifi_scan_get_ap_num(&ap_num) == ESP_OK && ap_num > 0) {
            wifi_ap_record_t *aps = calloc(ap_num, sizeof(wifi_ap_record_t));
            if (aps) {
                if (esp_wifi_scan_get_ap_records(&ap_num, aps) == ESP_OK) {
                    ESP_LOGI(TAG, "WiFi scan: %u AP(s) found", ap_num);
                    for (int i = 0; i < ap_num; i++) {
                        ESP_LOGI(TAG, "  [%02d] %-32s ch=%2d rssi=%d auth=%d",
                                 i + 1,
                                 aps[i].ssid[0] ? (char *)aps[i].ssid : "<hidden>",
                                 aps[i].primary, aps[i].rssi, aps[i].authmode);
                    }
                }
                free(aps);
            }
        } else {
            ESP_LOGW(TAG, "WiFi scan: no AP found or scan failed");
        }
        vTaskDelay(pdMS_TO_TICKS(SCAN_PERIOD_S * 1000));
    }
}

void app_main(void)
{
    print_chip_info();

    /* NVS + WiFi 初始化（仅扫描，不连接） */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    ws2812_init();

    xTaskCreate(ws2812_breathe_task, "led_breathe", 4096, NULL, 5, NULL);
    xTaskCreate(wifi_scan_task, "wifi_scan", 4096, NULL, 4, NULL);
}
