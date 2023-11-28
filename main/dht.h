#ifndef _DHT_H
#define _DHT_H

#define _DHT_11_SAFE_DELAY 1500 / portTICK_PERIOD_MS
#define _DHT_22_SAFE_DELAY 2500 / portTICK_PERIOD_MS
#define _DHT_11_WAKEUP_PULLDOWN_TIME_MS 20 / portTICK_PERIOD_MS
#define _DHT_22_WAKEUP_PULLDOWN_TIME_MS 10 / portTICK_PERIOD_MS

#define _DHT_RSP_BIT_MIN_INTERVAL_RANGE 0
#define _DHT_RSP_BIT_MAX_INTERVAL_RANGE 40
#define _DHT_ZERO_BIT_MIN_INTERVAL_RANGE 70
#define _DHT_ZERO_BIT_MAX_INTERVAL_RANGE 90
#define _DHT_ONE_BIT_MIN_INTERVAL_RANGE 105
#define _DHT_ONE_BIT_MAX_INTERVAL_RANGE 125
#define _DHT_ACK_BIT_MIN_INTERVAL_RANGE 135
#define _DHT_ACK_BIT_MAX_INTERVAL_RANGE 190

#include "driver/gpio.h"

typedef enum {DHT_11 = 1, DHT_22} dht_sensor_type;

typedef struct {
    dht_sensor_type dht_type;
    gpio_num_t dht_gpio;
    bool safe_mode;
} dht_config_t;

esp_err_t dht_config(const dht_config_t *);
bool dht_measure(double *temp, double *humi);

#endif