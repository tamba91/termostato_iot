#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "dht.h"

static dht_sensor_type _dht_sensor_type_configured;
static gpio_num_t _dht_gpio_configured;
static uint32_t _dht_wakeup_pulldown_time_ms_configured;
static uint32_t _dht_safe_delay_ms_configured;
static bool _dht_safe_mode_configured;
static bool _dht_configuration_done = false;
static volatile uint32_t _dht_prev_interrupt_time;
static volatile int32_t _dht_serial_bit_number;
static volatile uint32_t _dht_buffer[2];
static const char *_dht_tag = "DHT_LIB: ";

/*funzione che azzera il buffer di ricezione*/

static void _dht_clean_buffer(void)
{
    _dht_buffer[0] = 0;
    _dht_buffer[1] = 0;
}

/*routine ISR che riceve i bit sul canale seriale e posiziona gli 1 all'interno del buffer di ricezione, 
poichè il buffer viene azzerato prima di ogni misurazione non è necessario scrivere gli 0*/

static void IRAM_ATTR _dht_isr_handler(void *arg)
{
    uint32_t current_time = (uint32_t)esp_timer_get_time();
    uint32_t interval = current_time - _dht_prev_interrupt_time; //misurazione del'intervallo temporale tra 2 falling cfr datasheet DHT
    _dht_prev_interrupt_time = current_time;
    if (interval >= _DHT_ZERO_BIT_MIN_INTERVAL_RANGE && interval <= _DHT_ZERO_BIT_MAX_INTERVAL_RANGE) //ricevuto 0
    {
        --_dht_serial_bit_number;
        return;
    }
    else if (interval >= _DHT_ONE_BIT_MIN_INTERVAL_RANGE && interval <= _DHT_ONE_BIT_MAX_INTERVAL_RANGE) //ricevuto 1
    {
        _dht_buffer[_dht_serial_bit_number >> 5] |= 1 << (_dht_serial_bit_number & 31); //equivale a /32 e %32
        --_dht_serial_bit_number;
        return;
    }
    else
        return;
}

/*configurazione del dht, un solo dht alla volta può essere configurato e utilizzato*/

esp_err_t dht_config(const dht_config_t *dht_cfg)
{
    if (!dht_cfg)
        return ESP_ERR_INVALID_ARG;

    if (GPIO_IS_VALID_GPIO(dht_cfg->dht_gpio) && !RTC_GPIO_IS_VALID_GPIO(dht_cfg->dht_gpio))
    {
        _dht_gpio_configured = dht_cfg->dht_gpio;
        gpio_config_t io_conf;
        io_conf.intr_type = GPIO_INTR_DISABLE;
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pin_bit_mask = BIT(_dht_gpio_configured);
        io_conf.pull_down_en = 0;
        io_conf.pull_up_en = 0;
        gpio_config(&io_conf);
        gpio_set_level(_dht_gpio_configured, 1);
        gpio_install_isr_service(0);
    }
    else
    {
        ESP_LOGE(_dht_tag, "Invalid DHT GPIO: %d", dht_cfg->dht_gpio);
        return ESP_ERR_INVALID_ARG;
    }

    if (dht_cfg->safe_mode == false)
        _dht_safe_mode_configured = false;
    else
        _dht_safe_mode_configured = true;

    if (dht_cfg->dht_type == DHT_11)
    {
        _dht_sensor_type_configured = DHT_11;
        _dht_safe_delay_ms_configured = _DHT_11_SAFE_DELAY;
        _dht_wakeup_pulldown_time_ms_configured = _DHT_11_WAKEUP_PULLDOWN_TIME_MS;
    }
    else if (dht_cfg->dht_type == DHT_22)
    {
        _dht_sensor_type_configured = DHT_22;
        _dht_safe_delay_ms_configured = _DHT_22_SAFE_DELAY;
        _dht_wakeup_pulldown_time_ms_configured = _DHT_22_WAKEUP_PULLDOWN_TIME_MS;
    }
    else
    {
        ESP_LOGE(_dht_tag, "Invalid DHT TYPE: %d", dht_cfg->dht_type);
        return ESP_ERR_INVALID_ARG;
    }

    _dht_configuration_done = true;
    return ESP_OK;
}

/*funzione di misurazione*/

bool dht_measure(double *temp, double *humi)
{
    if (!_dht_configuration_done)
    {
        ESP_LOGE(_dht_tag, "dht configuration missing");
        return false;
    }

    char *buffer_byte_pointer = (char *)_dht_buffer;

    if (_dht_safe_mode_configured)
        vTaskDelay(_dht_safe_delay_ms_configured);

    _dht_clean_buffer();
    gpio_set_level(_dht_gpio_configured, 0);        //linea dati bassa per svegliare il sensore
    vTaskDelay(_dht_wakeup_pulldown_time_ms_configured);
    _dht_serial_bit_number = 63;        //contatore bit in arrivo inizializzato a 63, ultimo bit dell'ultimo byte del buffer di 64 bit
    gpio_set_direction(_dht_gpio_configured, GPIO_MODE_INPUT);
    gpio_set_pull_mode(_dht_gpio_configured, GPIO_PULLUP_ONLY);     // pull up e attesa di risposta sulla linea dati
    gpio_set_intr_type(_dht_gpio_configured, GPIO_INTR_NEGEDGE);    //interrupt falling su linea dati
    gpio_isr_handler_add(_dht_gpio_configured, _dht_isr_handler, (void *)1);    //attach della funzione di interrupt sulla linea dati
    _dht_prev_interrupt_time = (uint32_t)esp_timer_get_time();  //campionamento tempo attuale
    vTaskDelay(10 / portTICK_PERIOD_MS);    //delay minimo, al termine del delay la trasmissione è sicuramente conclusa
    gpio_isr_handler_remove(_dht_gpio_configured);  //detach della funzione di interrupt
    gpio_set_direction(_dht_gpio_configured, GPIO_MODE_OUTPUT); //ripristino della condizione di sleep per il sensore dht
    gpio_set_level(_dht_gpio_configured, 1);
    
    //calcolo della checksum, 
    //se la checksum è corretta vengono aggiornate le variabili di umidità e temperatura e ritorna true
    //altrimenti ritorna false

    if (_dht_serial_bit_number == 23 && ((*(buffer_byte_pointer + 3)) == (uint8_t)((*(buffer_byte_pointer + 4)) + (*(buffer_byte_pointer + 5)) + (*(buffer_byte_pointer + 6)) + (*(buffer_byte_pointer + 7)))))
    {
        if (_dht_sensor_type_configured == DHT_11)
        {
            if (humi)
                *humi = *(buffer_byte_pointer + 7) + ((double)(*(buffer_byte_pointer + 6))) / 10.0;
            if (temp)
                *temp = *(buffer_byte_pointer + 5) + ((double)(*(buffer_byte_pointer + 4))) / 10.0;
        }

        else if (_dht_sensor_type_configured == DHT_22)
        {
            if (humi)
                *humi = (double)(*((int16_t *)(buffer_byte_pointer + 6))) / 10.0;
            if (temp) {
                bool neg = (bool)((*(buffer_byte_pointer + 5)) & BIT7);
                *(buffer_byte_pointer + 5) &= ~BIT7;
                *temp = neg ? -((double)(*((int16_t *)(buffer_byte_pointer + 4))) / 10.0) : (double)(*((int16_t *)(buffer_byte_pointer + 4))) / 10.0;             
            }
                
        }
        return true;
    }
    else
        return false;
}