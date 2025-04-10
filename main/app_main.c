#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "driver/gpio.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "lwip/err.h"
#include "lwip/sys.h"


#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "nvs_flash.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define EXAMPLE_ESP_MAXIMUM_RETRY  5


#define IR_SENSOR_1 GPIO_NUM_4
#define IR_SENSOR_2 GPIO_NUM_5
#define IR_SENSOR_3 GPIO_NUM_18
#define IR_SENSOR_4 GPIO_NUM_19
#define IR_SENSOR_5 GPIO_NUM_21
#define IR_SENSOR_6 GPIO_NUM_22
static const gpio_num_t ir_pins[6] = {IR_SENSOR_1, IR_SENSOR_2, IR_SENSOR_3, IR_SENSOR_4, IR_SENSOR_5, IR_SENSOR_6};

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
char *ssid = "OPPO A54";
char *password = "12356789";

static uint8_t ESP_WIFI_SSID[32];
static uint8_t ESP_WIFI_PASS[64];

static const char *TAG = "MQTT_EXAMPLE";
ESP_EVENT_DEFINE_BASE(APP_MQTT_EVENT);
static esp_mqtt_client_handle_t client;
typedef struct{
    void *data;
    size_t data_len;
}app_mqtt_data_t;

//hàm xử lí sự kiện wifi
static void event_handler(void* arg, esp_event_base_t event_base,
    int32_t event_id, void* event_data)
{

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
    esp_wifi_connect();
    s_retry_num++;
    ESP_LOGI(TAG, "retry to connect to the AP");
    } else {
    xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT); //ko connect được wifi thì set bit fail
    }
    ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
    ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
    s_retry_num = 0;
    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT); // connect được wifi thì set bit connected
    }
}

void wifi_init_sta(void)
{
    //tạo event group
    s_wifi_event_group = xEventGroupCreate();

    //init network interface & tạo loop xử lí sự kiện
    ESP_ERROR_CHECK(esp_netif_init());

    // ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    //config và init setting wifi mặc định
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    //tạo handler để register/unregister hàm xử lí sự kiện
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;

    //đăng ký hàm xử lí sự kiện
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

    //set wifi mode, set config, start wifi
    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };
    
    // Copy ssid và password
    snprintf((char *)wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), "%s", ESP_WIFI_SSID);
    snprintf((char *)wifi_config.sta.password, sizeof(wifi_config.sta.password), "%s", ESP_WIFI_PASS);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    //đợi bits từ hàm xử lí sự kiện wifi
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
    ESP_WIFI_SSID, ESP_WIFI_PASS);
    } else if (bits & WIFI_FAIL_BIT) {
    ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
    ESP_WIFI_SSID, ESP_WIFI_PASS);
    } else {
    ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }

    //hủy đăng kí hàm xử lí sự kiện
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
    vEventGroupDelete(s_wifi_event_group);
}

void wifi_setup(char *ssid, char *password)
{
    snprintf((char *)ESP_WIFI_SSID, sizeof(ESP_WIFI_SSID), "%s", ssid);
    snprintf((char *)ESP_WIFI_PASS, sizeof(ESP_WIFI_PASS), "%s", password);
}

static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{
    // esp_mqtt_client_handle_t client = event->client;
    // int msg_id;
    // your_context_t *context = event->context;
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED: //kết nối thành công với broker -> subscribe 
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            esp_mqtt_client_subscribe(client, "v1/devices/me/rpc/request/+", 1);
            break;
        case MQTT_EVENT_DISCONNECTED: // ngắt kết nối khỏi broker
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");

            break;

        case MQTT_EVENT_SUBSCRIBED: //subscribe vào topic -> publish 1 tin 
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED: //unsubscribe khỏi topic
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);

            break;
        case MQTT_EVENT_PUBLISHED: //client gửi thành công tin đến broker
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);

            break;
        case MQTT_EVENT_DATA: //Nhận được dữ liệu từ một topic mà client đã đăng ký -> in ra dữ liệu nhận được
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            char data[64];
            printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
            printf("DATA=%.*s\r\n", event->data_len, event->data);
            memcpy(data, event->data,event->data_len);
            data[event->data_len] = '\0';
            printf("data = %s \n", data);
            
            break;
        case MQTT_EVENT_ERROR: //lỗi
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            break;
        default:
            ESP_LOGI(TAG, "Other event id:%d", event->event_id);
            break;
    }
    return ESP_OK;
}

void mqtt_app_start(void)
{
    const esp_mqtt_client_config_t mqtt_cfg = {
        //địa chỉ của broker mqtt
        .uri = "mqtt://thingsboard.cloud",
        .username = "GvK2QXqBzZ1a2ecMhk6Z",
        //hàm xử lí skien
        .event_handle = mqtt_event_handler,
    };

    ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_start(client);
}


void mqtt_app_publish(char *topic, char *data, size_t len)
{
    esp_mqtt_client_publish(client,topic,data,len,1,0);
}

void mqtt_app_subscribe(char *topic)
{
    esp_mqtt_client_subscribe(client,topic,1);
}

//cảm biến phát hiện vật cản IR nè
void ir_sensor_init() {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL<<IR_SENSOR_1) | (1ULL<<IR_SENSOR_2) | 
                        (1ULL<<IR_SENSOR_3) | (1ULL<<IR_SENSOR_4) | 
                        (1ULL<<IR_SENSOR_5) | (1ULL<<IR_SENSOR_6),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
}

void read_ir_sensors_task(void *pvParameters) {
    while (1) {
        char *Position[6];
        for (int i = 0; i < 6; i++) {
            if (gpio_get_level(ir_pins[i]) ==0) {
                Position[i] = "Full";
            }
            else Position[i] = "Empty";
        }

        char payload[256]; // Tăng kích thước để tránh tràn bộ nhớ
        snprintf(payload, sizeof(payload),
                "{"
                "\"Position_1\": \"%s\", \"Position_2\": \"%s\", \"Position_3\": \"%s\","
                "\"Position_4\": \"%s\", \"Position_5\": \"%s\", \"Position_6\": \"%s\""
                "}",
                Position[0], Position[1], Position[2], 
                Position[3], Position[4], Position[5]);

        mqtt_app_publish("v1/devices/me/attributes", payload, strlen(payload));

        ESP_LOGI(TAG, "Sent JSON: %s", payload); // In ra monitor để debug

        vTaskDelay(pdMS_TO_TICKS(5000));  // Gửi mỗi 5 giây
    }
}



void app_main(void)
{
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());


    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    // ESP_ERROR_CHECK(esp_event_handler_register(APP_MQTT_EVENT, ESP_EVENT_ANY_ID, &app_mqtt_event_handler, NULL));

    wifi_setup(ssid, password); //setup wf info
    wifi_init_sta(); //ket noi wf
    mqtt_app_start();

    ir_sensor_init();
    xTaskCreate(read_ir_sensors_task, "IR_Sensor_Task", 4096, NULL, 1, NULL);
    //tạo một task mới, sử dụng 4096 byte stack, có độ ưu tiên 1, và không truyền tham số đầu vào.

}
