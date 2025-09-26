#include <string.h>
#include <nvs_flash.h>
#include <sys/param.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"

#include "esp_log.h"
#include "esp_system.h"

#include "esp_camera.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_timer.h"

// support IDF 5.x
#ifndef portTICK_RATE_MS
#define portTICK_RATE_MS portTICK_PERIOD_MS
#endif

// WiFi相关信息
#define EXAMPLE_ESP_WIFI_SSID      "your_wifi_ssid"
#define EXAMPLE_ESP_WIFI_PASS      "your_wifi_password"
#define EXAMPLE_ESP_MAXIMUM_RETRY  5

#define CAM_PIN_PWDN 12
#define CAM_PIN_RESET 11   //software reset will be performed
#define CAM_PIN_VSYNC 39
#define CAM_PIN_HREF 40

#define CAM_PIN_PCLK 21
#define CAM_PIN_XCLK 38
#define CAM_PIN_SIOD 1
#define CAM_PIN_SIOC 2

#define CAM_PIN_D0 8
#define CAM_PIN_D1 9
#define CAM_PIN_D2 10
#define CAM_PIN_D3 4
#define CAM_PIN_D4 3
#define CAM_PIN_D5 45
#define CAM_PIN_D6 47
#define CAM_PIN_D7 48

static const char *TAG = "esp32s3_ov7670";

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static int s_retry_num = 0;

static camera_config_t camera_config = {
    .pin_pwdn = CAM_PIN_PWDN,
    .pin_reset = CAM_PIN_RESET,
    .pin_xclk = CAM_PIN_XCLK,
    .pin_sccb_sda = CAM_PIN_SIOD,
    .pin_sccb_scl = CAM_PIN_SIOC,

    .pin_d7 = CAM_PIN_D7,
    .pin_d6 = CAM_PIN_D6,
    .pin_d5 = CAM_PIN_D5,
    .pin_d4 = CAM_PIN_D4,
    .pin_d3 = CAM_PIN_D3,
    .pin_d2 = CAM_PIN_D2,
    .pin_d1 = CAM_PIN_D1,
    .pin_d0 = CAM_PIN_D0,
    .pin_vsync = CAM_PIN_VSYNC,
    .pin_href = CAM_PIN_HREF,
    .pin_pclk = CAM_PIN_PCLK,

    .xclk_freq_hz = 10000000,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,

    .pixel_format = PIXFORMAT_RGB565, //YUV422,GRAYSCALE,RGB565
    .frame_size = FRAMESIZE_QVGA,

    .jpeg_quality = 40, //0-63, for OV series camera sensors, lower number means higher quality
    .fb_count = 1,       //When jpeg mode is used, if fb_count more than one, the driver will work in continuous mode.
    .fb_location = CAMERA_FB_IN_PSRAM,
    // .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
    .grab_mode = CAMERA_GRAB_LATEST,  // 改为LATEST避免缓冲积累
};

static esp_err_t init_camera(void)
{
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Camera init failed with error 0x%x", err);
        return err;
    }

    ESP_LOGI(TAG, "Camera hardware initialized successfully");

    return ESP_OK;
}

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void init_wifi(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
            /* Setting a password implies station will connect to all security modes including WEP/WPA.
             * However these modes are deprecated and not advisable to be used. Incase your Access point
             * doesn't support WPA2, these mode can be enabled by commenting below line */
	     .threshold.authmode = WIFI_AUTH_WPA2_PSK,

            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}

#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %zu\r\n\r\n";

esp_err_t jpg_stream_httpd_handler(httpd_req_t *req){
    camera_fb_t * fb = NULL;
    esp_err_t res = ESP_OK;
    size_t jpg_buf_len = 0;
    uint8_t * jpg_buf = NULL;
    char part_buf[64];
    static int64_t last_frame = 0;
    if(!last_frame) {
        last_frame = esp_timer_get_time();
    }

    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if(res != ESP_OK){
        return res;
    }

    // uint8_t last_fb_count = 0;
    uint8_t skip_frame_count = 0;
    
    while(true){
        fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGE(TAG, "Camera capture failed");
            res = ESP_FAIL;
            break;
        }
        
        // 跳过前几帧以确保稳定性
        if (skip_frame_count < 3) {
            skip_frame_count++;
            esp_camera_fb_return(fb);
            continue;
        }
        
        // 检查帧数据是否完整
        if (fb->len == 0) {
            esp_camera_fb_return(fb);
            continue;
        }
        
        // 对于OV7670摄像头，需要将数据转换为JPEG
        if(fb->format != PIXFORMAT_JPEG){
            bool jpeg_converted = frame2jpg(fb, 12, &jpg_buf, &jpg_buf_len);
            if(!jpeg_converted){
                ESP_LOGE(TAG, "JPEG compression failed");
                esp_camera_fb_return(fb);
                res = ESP_FAIL;
                break;
            }
        } else {
            jpg_buf_len = fb->len;
            jpg_buf = fb->buf;
        }

        if(res == ESP_OK){
            res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
        }
        if(res == ESP_OK){
            int hlen = snprintf(part_buf, sizeof(part_buf), _STREAM_PART, jpg_buf_len);
            if(hlen < 0 || hlen >= sizeof(part_buf)){
                ESP_LOGE(TAG, "Header truncated (%d bytes needed >= %zu buffer)",
                         hlen, sizeof(part_buf));
                res = ESP_FAIL;
            } else {
                res = httpd_resp_send_chunk(req, part_buf, (size_t)hlen);
            }
        }
        if(res == ESP_OK){
            res = httpd_resp_send_chunk(req, (const char *)jpg_buf, jpg_buf_len);
        }
        if(fb->format != PIXFORMAT_JPEG){
            free(jpg_buf);
        }
        esp_camera_fb_return(fb);
        if(res != ESP_OK){
            break;
        }
        
        // // 控制帧率以提高稳定性
        // vTaskDelay(30 / portTICK_PERIOD_MS);
    }
    last_frame = 0;
    return res;
}

esp_err_t index_httpd_handler(httpd_req_t *req){
    httpd_resp_set_type(req, "text/html");
    const char* html_content = 
        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "<title>ESP32-CAM</title>"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
        "<style>"
        "body { font-family: Arial; text-align: center; margin: 0; padding: 0; background-color: #000; }"
        "#video { width: 100%; max-width: 320px; image-rendering: pixelated; }"
        ".container { display: flex; flex-direction: column; align-items: center; justify-content: center; min-height: 100vh; }"
        "</style>"
        "</head>"
        "<body>"
        "<div class=\"container\">"
        "<h1 style=\"color: white;\">ESP32 Camera Stream</h1>"
        "<img id=\"video\" src=\"/stream\" />"
        "<p style=\"color: white;\">Stable camera stream</p>"
        "</div>"
        "<script>"
        "document.addEventListener('DOMContentLoaded', function() {"
        "    var img = document.getElementById('video');"
        "    var streamUrl = '/stream?' + new Date().getTime();"
        "    "
        "    function updateImage() {"
        "        var xhr = new XMLHttpRequest();"
        "        xhr.open('GET', streamUrl, true);"
        "        xhr.responseType = 'blob';"
        "        "
        "        xhr.onload = function() {"
        "            if (xhr.status === 200) {"
        "                var blob = xhr.response;"
        "                if (blob) {"
        "                    var url = URL.createObjectURL(blob);"
        "                    img.src = url;"
        "                    img.onload = function() {"
        "                        URL.revokeObjectURL(url);"
        "                    };"
        "                }"
        "            }"
        "            setTimeout(updateImage, 100);"
        "        };"
        "        "
        "        xhr.onerror = function() {"
        "            setTimeout(updateImage, 1000);"
        "        };"
        "        "
        "        xhr.send();"
        "    }"
        "    "
        "    updateImage();"
        "});"
        "</script>"
        "</body>"
        "</html>";
    return httpd_resp_send(req, html_content, strlen(html_content));
}

httpd_uri_t index_uri = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = index_httpd_handler,
    .user_ctx  = NULL
};

httpd_uri_t stream_uri = {
    .uri       = "/stream",
    .method    = HTTP_GET,
    .handler   = jpg_stream_httpd_handler,
    .user_ctx  = NULL
};

httpd_handle_t init_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &index_uri);
        httpd_register_uri_handler(server, &stream_uri);
        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

void app_main(void) {
    // 检查PSRAM
    multi_heap_info_t psram_info;
    heap_caps_get_info(&psram_info, MALLOC_CAP_SPIRAM);
    
    if (psram_info.total_allocated_bytes + psram_info.total_free_bytes > 0) {
        ESP_LOGI(TAG, "PSRAM is available");
        ESP_LOGI(TAG, "PSRAM total: %zu bytes (%.2f MB)", 
                 psram_info.total_allocated_bytes + psram_info.total_free_bytes,
                 (float)(psram_info.total_allocated_bytes + psram_info.total_free_bytes) / (1024 * 1024));
        ESP_LOGI(TAG, "PSRAM free: %zu bytes (%.2f MB)", 
                 psram_info.total_free_bytes, 
                 (float)psram_info.total_free_bytes / (1024 * 1024));
    } else {
        ESP_LOGE(TAG, "PSRAM is not available");
        return;
    }
    
    // 检查内部RAM
    multi_heap_info_t dram_info;
    heap_caps_get_info(&dram_info, MALLOC_CAP_INTERNAL);
    ESP_LOGI(TAG, "Internal RAM free: %zu bytes (%.2f KB)", dram_info.total_free_bytes, (float)dram_info.total_free_bytes / 1024);

    // 初始化摄像头
    if (ESP_OK != init_camera()) {
        ESP_LOGE(TAG, "Camera initialization failed");
        return;
    }

    // 初始化 WIFI
    init_wifi();

    // 初始化 web server
    init_webserver();
    
    ESP_LOGI(TAG, "Web server started at http://<your_esp32_ip>");
}