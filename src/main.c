#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/drivers/display.h>
#include <math.h>

#include "camera_service.h"

LOG_MODULE_REGISTER(radar_main, LOG_LEVEL_INF);

#define STACK_SIZE 1024
#define PRIORITY 5

#define SENSOR_DISTANCE_M ( (float)CONFIG_RADAR_SENSOR_DISTANCE_MM / 1000.0f )

const struct gpio_dt_spec s1 = DT_ALIAS(s1_sensor);
const struct gpio_dt_spec s2 = DT_ALIAS(s2_sensor);

struct sensor_data {
    uint32_t delta_time_ms; 
    int axles;              
};
K_MSGQ_DEFINE(sensor_msg_queue, sizeof(struct sensor_data), 10, 4);

enum radar_status {
    STATUS_NORMAL,
    STATUS_WARNING,
    STATUS_INFRACTION
};
struct display_data {
    enum radar_status status;
    float speed_kmh;
    float limit_kmh;
    const char *vehicle_type;
    char plate_info[9]; 
};
ZBUS_CHAN_DECLARE(display_data_chan);
ZBUS_CHAN_DEFINE(display_data_chan, struct display_data, NULL, NULL, ZBUS_OBS_SINGLE);

ZBUS_OBS_DEFINE(main_obs_camera_evt, ZBUS_CHAN_GET(chan_camera_evt), K_MSEC(100));


static struct gpio_callback s1_cb;
static struct gpio_callback s2_cb;

enum sensor_state {
    STATE_IDLE,
    STATE_S1_ACTIVE,
    STATE_S2_ACTIVE
};

static enum sensor_state current_state = STATE_IDLE;
static uint64_t s1_active_time;
static uint32_t start_time_ms;
static int axles_count;

void s1_handler(const struct device *dev, void *user_data)
{
    uint64_t now_ticks = k_uptime_ticks();

    switch (current_state) {
        case STATE_IDLE:
            start_time_ms = k_uptime_get_32();
            axles_count = 1;
            s1_active_time = now_ticks;
            current_state = STATE_S1_ACTIVE;
            LOG_DBG("S1 Ativado. Passagem iniciada.");
            break;

        case STATE_S1_ACTIVE:
            if (k_ticks_to_ms_near32(now_ticks - s1_active_time) > 100) { 
                axles_count++;
                s1_active_time = now_ticks;
                LOG_DBG("Eixo %d detectado (S1)", axles_count);
            }
            break;

        case STATE_S2_ACTIVE:
            break;
    }
}

void s2_handler(const struct device *dev, void *user_data)
{
    if (current_state != STATE_S1_ACTIVE) {
        LOG_WRN("S2 ativado fora de sequência. Ignorando.");
        return;
    }

    uint32_t end_time_ms = k_uptime_get_32();
    uint32_t delta_time_ms = end_time_ms - start_time_ms;
    
    int final_axles = (axles_count < 2) ? 2 : axles_count;

    struct sensor_data data = {
        .delta_time_ms = delta_time_ms,
        .axles = final_axles
    };

    if (k_msgq_put(&sensor_msg_queue, &data, K_NO_WAIT) != 0) {
        LOG_ERR("Fila de sensores cheia! Descartando dado.");
    } else {
        LOG_INF("Passagem completa. Eixos: %d, Tempo: %dms. Enviado.", final_axles, delta_time_ms);
    }

    current_state = STATE_S2_ACTIVE;
}

void sensor_thread_entry(void *p1, void *p2, void *p3)
{
    if (!device_is_ready(s1.port) || !device_is_ready(s2.port)) {
        LOG_ERR("Erro: Um dos dispositivos GPIO não está pronto!");
        return;
    }

    if (gpio_pin_configure_dt(&s1, GPIO_INPUT | GPIO_PULL_DOWN) < 0 ||
        gpio_pin_interrupt_configure_dt(&s1, GPIO_INT_EDGE_TO_ACTIVE) < 0) {
        LOG_ERR("Falha na configuração do GPIO S1.");
        return;
    }
    gpio_init_callback(&s1_cb, s1_handler, BIT(s1.pin));
    gpio_add_callback(s1.port, &s1_cb);

    if (gpio_pin_configure_dt(&s2, GPIO_INPUT | GPIO_PULL_DOWN) < 0 ||
        gpio_pin_interrupt_configure_dt(&s2, GPIO_INT_EDGE_TO_ACTIVE) < 0) {
        LOG_ERR("Falha na configuração do GPIO S2.");
        return;
    }
    gpio_init_callback(&s2_cb, s2_handler, BIT(s2.pin));
    gpio_add_callback(s2.port, &s2_cb);

    LOG_INF("Thread Sensores inicializada. Aguardando passagens.");

    while (1) {
        k_sleep(K_FOREVER);
    }
}
K_THREAD_DEFINE(sensor_thread, STACK_SIZE, sensor_thread_entry, NULL, NULL, NULL, PRIORITY, 0, 0);



#define ANSI_COLOR_RED      "\x1b[31m"
#define ANSI_COLOR_YELLOW   "\x1b[33m"
#define ANSI_COLOR_GREEN    "\x1b[32m"
#define ANSI_COLOR_RESET    "\x1b[0m"

void display_thread_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    struct display_data data;
    const struct zbus_channel *chan_display = ZBUS_CHAN_GET(display_data_chan);

    LOG_INF("Thread Display inicializada. Aguardando status...");

    while (1) {
        if (zbus_chan_read(chan_display, &data, K_FOREVER) == 0) {
            const char *color;
            const char *status_str;

            switch (data.status) {
                case STATUS_INFRACTION:
                    color = ANSI_COLOR_RED;
                    status_str = "INFRAÇÃO";
                    break;
                case STATUS_WARNING:
                    color = ANSI_COLOR_YELLOW;
                    status_str = "ALERTA";
                    break;
                case STATUS_NORMAL:
                default:
                    color = ANSI_COLOR_GREEN;
                    status_str = "NORMAL";
                    break;
            }

            printk("\n"
                   "========================\n"
                   "|  RADAR STATUS: %s%s%s\n"
                   "|  ----------------------\n"
                   "|  Tipo: %s\n"
                   "|  Limite: %.1f km/h\n"
                   "|  Velocidade: %s%.1f km/h%s\n"
                   "|  Placa: %s\n"
                   "========================\n",
                   color, status_str, ANSI_COLOR_RESET,
                   data.vehicle_type,
                   data.limit_kmh,
                   color, data.speed_kmh, ANSI_COLOR_RESET,
                   data.plate_info
            );
        }
    }
}
K_THREAD_DEFINE(display_thread, STACK_SIZE, display_thread_entry, NULL, NULL, NULL, PRIORITY, 0, 0);


static uint32_t violation_counter = 0;

void main_thread_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    struct sensor_data s_data;
    struct display_data d_data = {0};
    
    char last_plate[9] = "N/A";

    LOG_INF("Thread Principal/Controle inicializada. Distância: %.2f m.", SENSOR_DISTANCE_M);

    while (1) {
        if (k_msgq_get(&sensor_msg_queue, &s_data, K_NO_WAIT) == 0) {
            
            const char *vehicle_type;
            float limit_kmh;

            if (s_data.axles <= 2) {
                vehicle_type = "LEVE (2 eixos)";
                limit_kmh = (float)CONFIG_RADAR_SPEED_LIMIT_LIGHT_KMH;
            } else {
                vehicle_type = "PESADO (>=3 eixos)";
                limit_kmh = (float)CONFIG_RADAR_SPEED_LIMIT_HEAVY_KMH;
            }

            float time_s = (float)s_data.delta_time_ms / 1000.0f;
            float speed_ms = SENSOR_DISTANCE_M / time_s;
            float speed_kmh = speed_ms * 3.6f;

            enum radar_status current_status;
            float warning_limit_kmh = limit_kmh * ((float)CONFIG_RADAR_WARNING_THRESHOLD_PERCENT / 100.0f);

            if (speed_kmh > limit_kmh) {
                current_status = STATUS_INFRACTION;
                violation_counter++;
                LOG_ERR("INFRAÇÃO DETECTADA! Acionando Câmera (ID: %d).", violation_counter);

                // Acionamento da Câmera (API)
                if (camera_api_capture(K_NO_WAIT) == 0) {
                    strncpy(last_plate, "CAPTURA...", sizeof(last_plate) - 1);
                } else {
                    LOG_WRN("Câmera ocupada ou falha ao iniciar.");
                    strncpy(last_plate, "ERRO", sizeof(last_plate) - 1);
                }

            } else if (speed_kmh > warning_limit_kmh) {
                current_status = STATUS_WARNING;
                strncpy(last_plate, "N/A", sizeof(last_plate) - 1);
            } else {
                current_status = STATUS_NORMAL;
                strncpy(last_plate, "N/A", sizeof(last_plate) - 1);
            }

            d_data = (struct display_data){
                .status = current_status,
                .speed_kmh = speed_kmh,
                .limit_kmh = limit_kmh,
                .vehicle_type = vehicle_type
            };
            strncpy(d_data.plate_info, last_plate, sizeof(d_data.plate_info) - 1);
            
            zbus_chan_pub(ZBUS_CHAN_GET(display_data_chan), &d_data, K_NO_WAIT);

            current_state = STATE_IDLE;
        }

        struct msg_camera_evt cam_evt;
        if (zbus_obs_channel_read(ZBUS_OBS_GET(main_obs_camera_evt), ZBUS_CHAN_GET(chan_camera_evt), &cam_evt, K_NO_WAIT) == 0) {
            
            if (cam_evt.type == MSG_CAMERA_EVT_TYPE_DATA) {
                strncpy(last_plate, cam_evt.captured_data->plate, sizeof(last_plate) - 1);
                LOG_INF("Câmera OK: Placa %s.", last_plate);

            } else if (cam_evt.type == MSG_CAMERA_EVT_TYPE_ERROR) {
                strncpy(last_plate, "FALHA", sizeof(last_plate) - 1);
                LOG_WRN("Câmera FALHA.");
            }
            
            zbus_chan_read(ZBUS_CHAN_GET(display_data_chan), &d_data, K_NO_WAIT); 
            strncpy(d_data.plate_info, last_plate, sizeof(d_data.plate_info) - 1); 
            zbus_chan_pub(ZBUS_CHAN_GET(display_data_chan), &d_data, K_NO_WAIT);
        }

        k_sleep(K_MSEC(10));
    }
}
K_THREAD_DEFINE(main_thread, STACK_SIZE, main_thread_entry, NULL, NULL, NULL, PRIORITY, 0, 0);
