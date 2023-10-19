#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "driver/gpio.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "mqtt_client.h"
#include "lwip/apps/sntp.h"
#include "cJSON.h"

#include "dht.h"
#include "timeinterval.h"

/*definizione macro per wifi*/

#define EXAMPLE_ESP_WIFI_SSID      "mywifi"
#define EXAMPLE_ESP_WIFI_PASS      "qwerty"
#define EXAMPLE_ESP_MAXIMUM_RETRY  CONFIG_ESP_MAXIMUM_RETRY

/*definizione macro per mqtt*/

#define CONFIG_BROKER_URL   "mqtt://atrent.it"
#define MQTT_COMMAND_SUBSCRIBE_TOPIC "tamba/test/comandi"
#define MQTT_DATA_PUBLISH_TOPIC "tamba/test/dati"

/*definizione dei gpio*/

#define LED_BUILTIN GPIO_NUM_2
#define LED_BUILTIN_MASK GPIO_Pin_2

#define RELAY GPIO_NUM_4
#define RELAY_MASK GPIO_Pin_4

/*definizione estremi per valori di temperatura in gradi centigradi*/

#define MIN_TARGET_TEMP 15
#define MAX_TARGET_TEMP 30

#define MIN_BASE_TEMP 5
#define MAX_BASE_TEMP 15

#define MIN_DELTA_TEMP 0
#define MAX_DELTA_TEMP 1

/*definizione per la programmazione dei giorni della settimana*/

#define TIME_INTERVALS_PER_DAY 10
#define DAYS_PER_WEEK 7


/* definizione dei bits per task di connessione e riconnessione*/

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define MQTT_CONNECTED_BIT BIT2
#define MQTT_FAIL_BIT      BIT3

/* definizione dei bits per event group task publisher mqtt e task termostato*/

#define CURRENT_TEMP_HUMI_UPDATE_BIT_PUBLISHER_TASK BIT0
#define TARGET_TEMP_UPDATE_BIT_PUBLISHER_TASK BIT1
#define BASE_TEMP_UPDATE_BIT_PUBLISHER_TASK BIT2
#define DELTA_TEMP_UPDATE_BIT_PUBLISHER_TASK BIT3
#define MAIN_SWITCH_UPDATE_BIT_PUBLISHER_TASK BIT4
#define PROG_SWITCH_UPDATE_BIT_PUBLISHER_TASK BIT5
#define THERMO_STATUS_UPDATE_BIT_PUBLISHER_TASK BIT6
#define NODE_ONLINE_STATUS_BIT_PUBLISHER_TASK BIT7
#define DHT_SENSOR_STATUS_PUBLISHER_TASK BIT8
#define WEEK_PROG_UPDATE_BIT_PUBLISHER_TASK BIT9
#define UPDATE_REQUEST_BIT_PUBLISHER_TASK BIT10

#define WAKE_UP_BIT_THERMO_TASK BIT11

static const char *TAG = "thermo_app";

/* variabili di stato globali deifinizione e inizializzazione*/

double target_temp = 20;     //temperatura target desiderata inizialittata a 20°C
double base_temp = 12;       //temperatura minima sotto la quale il riscaldamento parte comunque, inizializzata a 12°C
double delta_temp = 0.2;    //differenza di temperatura dal target per lo spegnimento del riscaldamento
double current_temp = 0;    //temperatura corrente rilevata
double current_humi = 0;    //umidità corrente rilevata
bool main_switch = false;   //switch generale termostato
bool prog_switch = false;   //switch attivazione / disattivazoine programmazione oraria
bool thermo_on = false;     //stato riscaldamento acceso / spento
bool node_online = false;   //stato connessione wi-fi e mqtt
bool dht_ok = false;        //stato sensore dht per rilevazione temperatura e umidità
daytime_interval_sec_t week_prog[DAYS_PER_WEEK][TIME_INTERVALS_PER_DAY];   //programmazione oraria settimanale, matrice di programmazioni giornaliere

const char *weekday_json_key_names[] = {"sundayProg", "mondayProg", "tuesdayProg", "wednesdayProg", "thursdayProg", "fridayProg", "saturdayProg"};

/*event groups handlers*/

static EventGroupHandle_t connection_event_group;
static EventGroupHandle_t reconnection_request_group;
static EventGroupHandle_t global_variable_update_group;

/*client mqtt*/

esp_mqtt_client_handle_t mqtt_client;

/*task handlers*/

TaskHandle_t led_builtin_blinker_task_handler;
TaskHandle_t connection_event_manager_task_handler;
TaskHandle_t mqtt_publish_json_task_handler;
TaskHandle_t try_to_reconnect_task_handler;
TaskHandle_t app_time_update_task_handler;
TaskHandle_t json_decode_global_variables_update_task_handler;
TaskHandle_t thermo_task_handler;

/*queue handler per mqtt data*/

QueueHandle_t mqtt_data_pointers_queue_handler;

/*PROTOTIPI DI FUNZIONI LOCALI*/

void wifi_setup(void);
void sntp_setup(void);
void gpio_setup(void);
void dht_setup(void);
void mqtt_client_setup(void);
void week_prog_setup(void);

/*TASKS RTOS*/

/*task blinker per segnalare un'anormalità di rete*/

static void led_builtin_blinker_task(void *arg)
{
    for (;;) 
    {
        gpio_set_level(LED_BUILTIN, 0);
        vTaskDelay(250 / portTICK_PERIOD_MS);
        gpio_set_level(LED_BUILTIN, 1);
        vTaskDelay(250 / portTICK_PERIOD_MS);
    }

    vTaskDelete(NULL);
}

/*task che gestisce gli eventi di connessione, i protocolli di rete e i task che ne fanno uso*/

static void connection_event_manager_task(void *arg)
{   
    for (;;) 
    {
        EventBits_t bits = xEventGroupWaitBits(connection_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT | MQTT_CONNECTED_BIT | MQTT_FAIL_BIT, pdTRUE, pdFALSE, portMAX_DELAY);

        if (bits & WIFI_CONNECTED_BIT) //connessione wi-fi stabilita
        {
            ESP_LOGI(TAG, "connected to ap SSID:%s password:%s", EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
            sntp_init();                            //avvio servizio sntp
            esp_mqtt_client_start(mqtt_client);     //avvio client mqtt
        }

        else if (bits & WIFI_FAIL_BIT) //connessione wi-fi persa o non stabilita
        {
            ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s", EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
            node_online = false;
            vTaskSuspend(mqtt_publish_json_task_handler);   //sospensione task publisher mqtt
            esp_mqtt_client_stop(mqtt_client);              //stop client mqtt
            sntp_stop();                                    //stop servizio sntp
            xEventGroupSetBits(reconnection_request_group, WIFI_FAIL_BIT);  //notifica perdita connessione wi-fi per il task di riconnessione
            vTaskResume(led_builtin_blinker_task_handler);  //lampeggio led builtin segnala il problema
        }

        else if (bits & MQTT_CONNECTED_BIT) //connessione al broker mqtt stabilita
        {
            ESP_LOGI(TAG, "mqtt client connected to broker");
            node_online = true;
            esp_mqtt_client_subscribe(mqtt_client, MQTT_COMMAND_SUBSCRIBE_TOPIC, 0); //sottoscrizione del topic pree i comandi
            vTaskResume(mqtt_publish_json_task_handler);    //attivazione del publisher mqtt
            vTaskSuspend(led_builtin_blinker_task_handler); //sospensione lampeggio led builtin 
            gpio_set_level(LED_BUILTIN, 1); //spegnimento del led builtin attivo basso
            xEventGroupSetBits(global_variable_update_group, UPDATE_REQUEST_BIT_PUBLISHER_TASK); //set del bit per inviare lo stato globale del sistema tramite mqtt publisher task   
        }

        else if (bits & MQTT_FAIL_BIT) //connessione al broker mqtt persa o non stabilita
        {
            ESP_LOGI(TAG, "mqtt client disconnected");
            node_online = false;
            vTaskSuspend(mqtt_publish_json_task_handler); //stop client mqtt
            xEventGroupSetBits(reconnection_request_group, MQTT_FAIL_BIT); //notifica perdita connessione broker mqtt per il task di riconnessione
            vTaskResume(led_builtin_blinker_task_handler); //lampeggio led builtin segnala il problema
        }

        else
            ESP_LOGE(TAG, "UNEXPECTED EVENT"); 

    }
    
    vTaskDelete(NULL);
}

/*
task che pubblica lo stato del sistema tramite messaggi mqtt in formato json, i task che vogliono pubblicare una informazione settano
il bit specifico nell'event group global_variable_update_group
*/

static void mqtt_publish_json_task(void *arg)
{  
    cJSON *root = NULL;
    char *rendered = NULL;

    vTaskSuspend(NULL);

    for(;;)
    {
        EventBits_t bits = xEventGroupWaitBits(global_variable_update_group, CURRENT_TEMP_HUMI_UPDATE_BIT_PUBLISHER_TASK | TARGET_TEMP_UPDATE_BIT_PUBLISHER_TASK | BASE_TEMP_UPDATE_BIT_PUBLISHER_TASK | DELTA_TEMP_UPDATE_BIT_PUBLISHER_TASK | MAIN_SWITCH_UPDATE_BIT_PUBLISHER_TASK | THERMO_STATUS_UPDATE_BIT_PUBLISHER_TASK | NODE_ONLINE_STATUS_BIT_PUBLISHER_TASK | DHT_SENSOR_STATUS_PUBLISHER_TASK | WEEK_PROG_UPDATE_BIT_PUBLISHER_TASK | UPDATE_REQUEST_BIT_PUBLISHER_TASK, pdFALSE, pdFALSE, portMAX_DELAY);

        root = cJSON_CreateObject();
       
        if(root)
        {

            if(bits & CURRENT_TEMP_HUMI_UPDATE_BIT_PUBLISHER_TASK)  //pubblicazione temperatura e umidità ambiente
            {   
                cJSON_AddNumberToObject(root, "currentTemp", current_temp);
                cJSON_AddNumberToObject(root, "currentHumi", current_humi);
                xEventGroupClearBits(global_variable_update_group, CURRENT_TEMP_HUMI_UPDATE_BIT_PUBLISHER_TASK);
            }

            else if(bits & NODE_ONLINE_STATUS_BIT_PUBLISHER_TASK)   //pubblicazione stato connessione, per disconnessione è necessario messaggio di last will mqtt
            {
                if(node_online)
                    cJSON_AddTrueToObject(root, "nodeOnline");
                else
                    cJSON_AddFalseToObject(root, "nodeOnline");
                xEventGroupClearBits(global_variable_update_group, NODE_ONLINE_STATUS_BIT_PUBLISHER_TASK);
            }

            else if(bits & TARGET_TEMP_UPDATE_BIT_PUBLISHER_TASK)   //pubblicazione temperatura desiderata
            {   
                cJSON_AddNumberToObject(root, "targetTemp", target_temp);
                xEventGroupClearBits(global_variable_update_group, TARGET_TEMP_UPDATE_BIT_PUBLISHER_TASK);
            }

            else if(bits & BASE_TEMP_UPDATE_BIT_PUBLISHER_TASK)   //pubblicazione temperatura di base
            {   
                cJSON_AddNumberToObject(root, "baseTemp", base_temp);
                xEventGroupClearBits(global_variable_update_group, BASE_TEMP_UPDATE_BIT_PUBLISHER_TASK);
            }

            else if(bits & DELTA_TEMP_UPDATE_BIT_PUBLISHER_TASK)    //pubblicazione della delta temp
            {
                cJSON_AddNumberToObject(root, "deltaTemp", delta_temp);
                xEventGroupClearBits(global_variable_update_group, DELTA_TEMP_UPDATE_BIT_PUBLISHER_TASK);
            }

            else if(bits & MAIN_SWITCH_UPDATE_BIT_PUBLISHER_TASK)   //pubblicazione stato interrutore generale
            {
                if(main_switch)
                    cJSON_AddTrueToObject(root, "mainSwitch");
                else
                    cJSON_AddFalseToObject(root, "mainSwitch");
                xEventGroupClearBits(global_variable_update_group, MAIN_SWITCH_UPDATE_BIT_PUBLISHER_TASK);

            }

            else if(bits & PROG_SWITCH_UPDATE_BIT_PUBLISHER_TASK)   //pubblicazione stato interrutore programmazione attiva disattiva
            {
                if(prog_switch)
                    cJSON_AddTrueToObject(root, "progSwitch");
                else
                    cJSON_AddFalseToObject(root, "progSwitch");
                xEventGroupClearBits(global_variable_update_group, PROG_SWITCH_UPDATE_BIT_PUBLISHER_TASK);

            }

            else if(bits & THERMO_STATUS_UPDATE_BIT_PUBLISHER_TASK) //pubblicazione stato riscaldamento
            {
                if(thermo_on)
                    cJSON_AddTrueToObject(root, "thermoOn");
                else
                    cJSON_AddFalseToObject(root, "thermoOn");
                xEventGroupClearBits(global_variable_update_group, THERMO_STATUS_UPDATE_BIT_PUBLISHER_TASK);
            }
            
            else if(bits & DHT_SENSOR_STATUS_PUBLISHER_TASK)    //pubblicazione stato sensore dht
            {
                if(dht_ok)
                    cJSON_AddTrueToObject(root, "dhtOk");
                else    
                    cJSON_AddFalseToObject(root, "dhtOk");
                xEventGroupClearBits(global_variable_update_group, DHT_SENSOR_STATUS_PUBLISHER_TASK);
            }

            else if(bits & WEEK_PROG_UPDATE_BIT_PUBLISHER_TASK)  //pubblicazione programmazione settimanale
            {
                for(int i=0; i<DAYS_PER_WEEK; i++)
                {
                    char string_buffer[13 * TIME_INTERVALS_PER_DAY];
                    sprint_intervals(week_prog[i], TIME_INTERVALS_PER_DAY, string_buffer, 130);
                    cJSON_AddStringToObject(root, weekday_json_key_names[i], string_buffer);
                }

                xEventGroupClearBits(global_variable_update_group, WEEK_PROG_UPDATE_BIT_PUBLISHER_TASK);
            }

            else if( bits & UPDATE_REQUEST_BIT_PUBLISHER_TASK)   //pubblicazione dello stato completo
            {
                cJSON_AddNumberToObject(root, "currentTemp", current_temp);
                cJSON_AddNumberToObject(root, "currentHumi", current_humi);

                cJSON_AddNumberToObject(root, "targetTemp", target_temp);

                cJSON_AddNumberToObject(root, "baseTemp", base_temp);

                cJSON_AddNumberToObject(root, "deltaTemp", delta_temp);

                if(main_switch)
                    cJSON_AddTrueToObject(root, "mainSwitch");
                else
                    cJSON_AddFalseToObject(root, "mainSwitch");
                
                if(prog_switch)
                    cJSON_AddTrueToObject(root, "progSwitch");
                else
                    cJSON_AddFalseToObject(root, "progSwitch");
                
                if(thermo_on)
                    cJSON_AddTrueToObject(root, "thermoOn");
                else
                    cJSON_AddFalseToObject(root, "thermoOn");
                
                if(node_online)
                    cJSON_AddTrueToObject(root, "nodeOnline");
                else
                    cJSON_AddFalseToObject(root, "nodeOnline");
                
                if(dht_ok)
                    cJSON_AddTrueToObject(root, "dhtOk");
                else    
                    cJSON_AddFalseToObject(root, "dhtOk");
                
                for(int i=0; i<DAYS_PER_WEEK; i++)
                {
                    char string_buffer[130];
                    sprint_intervals(week_prog[i], TIME_INTERVALS_PER_DAY, string_buffer, 130);
                    cJSON_AddStringToObject(root, weekday_json_key_names[i], string_buffer);
                }

                xEventGroupClearBits(global_variable_update_group, UPDATE_REQUEST_BIT_PUBLISHER_TASK);
            }

            rendered = cJSON_Print(root);   //stringify dell'oggetto json
            esp_mqtt_client_publish(mqtt_client, MQTT_DATA_PUBLISH_TOPIC, rendered, strlen(rendered), 0, 0);  //pubblicazione messaggio mqtt
            cJSON_Delete(root);
            root = NULL;
            free(rendered);
            rendered = NULL;

        }
    }

    vTaskDelete(NULL);
}

/*task che implementa la funzionalità di termostato eseguendo confronti di temperatura e orario*/

static void thermo_task()
{
    for(;;)
    {
        xEventGroupWaitBits(global_variable_update_group, WAKE_UP_BIT_THERMO_TASK, pdTRUE ,pdFALSE, portMAX_DELAY);
        time_t raw;
        struct tm current_time_struct;

        time(&raw);
        localtime_r(&raw, &current_time_struct);

        /*
        caso con programmazione oraria settimanale attiva, verifica se l'ora corrente è compresa in un intervallo di programmazione
        e se la temperatura corrrente è inferiore alla temperatura desiderata
        */

        if(main_switch == true && prog_switch == true && time_in_interval(&current_time_struct, week_prog[current_time_struct.tm_wday], TIME_INTERVALS_PER_DAY) && current_temp < target_temp)
        {
            gpio_set_level(RELAY, 1);
            thermo_on = true;
            xEventGroupSetBits(global_variable_update_group, THERMO_STATUS_UPDATE_BIT_PUBLISHER_TASK);
        }

        /*
        caso con programmazione oraria settimanale disattivata, verifica solo se la temperatura corrrente è inferiore alla temperatura 
        desiderata
        */

        else if(main_switch == true && prog_switch == false && current_temp < target_temp)
        {
            gpio_set_level(RELAY, 1);
            thermo_on = true;
            xEventGroupSetBits(global_variable_update_group, THERMO_STATUS_UPDATE_BIT_PUBLISHER_TASK);
        }

        //raggiungimento della temperatura desiderata più il delta (programmazione temporale attiva)

        else if(main_switch == true && prog_switch == true && time_in_interval(&current_time_struct, week_prog[current_time_struct.tm_wday], TIME_INTERVALS_PER_DAY) && thermo_on == true && current_temp <= (target_temp + delta_temp));

        //raggiungimento della temperatura desiderata più il delta (programmazione temporale non attiva)

        else if(main_switch == true && prog_switch == false && thermo_on == true && current_temp <= (target_temp + delta_temp));
        
        //sotto la temperatura di base il riscaldamento parte comunque

        else if(current_temp < base_temp)   
        {
            gpio_set_level(RELAY, 1);
            thermo_on = true;
            xEventGroupSetBits(global_variable_update_group, THERMO_STATUS_UPDATE_BIT_PUBLISHER_TASK);
        }

        //nessun caso verificato, spegne il riscaldamento

        else
        {   
            gpio_set_level(RELAY, 0);
            thermo_on = false;
            xEventGroupSetBits(global_variable_update_group, THERMO_STATUS_UPDATE_BIT_PUBLISHER_TASK);
        }
    }

    vTaskDelete(NULL);
}

/*task di misurazione della temperatura con sensore dht*/

static void measure_task()
{   
    for(;;)
    {   
        //misurazione riuscita, aggiornamento variabili globali di stato e nuova misurazione allo scoccare del minuto successivo
        //viene settato il bit per la pubblicazione dei valori correnti di temperatura e umidità e il bit per lo stato del sensore dht

        if(dht_measure(&current_temp, &current_humi))
        {   
            dht_ok = true;
            xEventGroupSetBits(global_variable_update_group, CURRENT_TEMP_HUMI_UPDATE_BIT_PUBLISHER_TASK | DHT_SENSOR_STATUS_PUBLISHER_TASK | WAKE_UP_BIT_THERMO_TASK);
            time_t now;
            time(&now);
            vTaskDelay((60 - now % 60) * 1000 / portTICK_PERIOD_MS);
        }

        //misurazione fallita, ritenta un'altra misurazione immediatamente (safe mode attiva nella configurazione del sensore dht)

        else
        {
            dht_ok = false;
            ESP_LOGE(TAG, "dht error");
            xEventGroupSetBits(global_variable_update_group, DHT_SENSOR_STATUS_PUBLISHER_TASK);
        }   
    }

    vTaskDelete(NULL);
}

/*task di riconnessione wifi e mqtt*/

static void try_to_reconnect_task() 
{   
    EventBits_t bits;

    for(;;)
    { 
        xEventGroupWaitBits(reconnection_request_group, WIFI_FAIL_BIT | MQTT_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

        vTaskDelay(30000 / portTICK_PERIOD_MS); //attesa di 30 secondi per verificare che cosa si è disconnesso

        bits = xEventGroupGetBits(reconnection_request_group);

        if(bits & WIFI_FAIL_BIT)
        {
            ESP_LOGI(TAG, "wifi try to reconnect");
            esp_wifi_connect();
        }

        else if (bits & MQTT_FAIL_BIT)
        {   
            ESP_LOGI(TAG, "mqtt try to reconnect");
            esp_mqtt_client_reconnect(mqtt_client);
        }
        xEventGroupClearBits(reconnection_request_group, WIFI_FAIL_BIT | MQTT_FAIL_BIT);
    }

    vTaskDelete(NULL);
}


/*task che aggiorna le variabili globali relative ai comandi impartiti dall'utente*/ 

static void json_decode_global_variables_update_task(void *arg)
{   
    cJSON * root = NULL;
    char* buffer = NULL;
    int day_selected = -1;
    char start_time[9] = {'\0'};
    char end_time[9] = {'\0'};

    for(;;)
    {
        xQueueReceive(mqtt_data_pointers_queue_handler, &buffer, portMAX_DELAY);
        
        root = cJSON_Parse(buffer);     //parsing dei dati in formato json ricevuti e copiati nel buffer dall'event handler mqtt
        
        if(root)
        {  
            if(cJSON_HasObjectItem(root, "syncRequest")) //richiesta stato nodo online/offline
            {
                cJSON *boolean_value = cJSON_GetObjectItem(root, "syncRequest");
                if(cJSON_IsTrue(boolean_value))
                    xEventGroupSetBits(global_variable_update_group, NODE_ONLINE_STATUS_BIT_PUBLISHER_TASK);
            }

            else if(cJSON_HasObjectItem(root, "targetTemp") && cJSON_GetObjectItem(root, "targetTemp")->valuedouble >= MIN_TARGET_TEMP && cJSON_GetObjectItem(root, "targetTemp")->valuedouble <= MAX_TARGET_TEMP)     //nuova temperatura target del termostato
            {
                target_temp = cJSON_GetObjectItem(root, "targetTemp")->valuedouble;
                xEventGroupSetBits(global_variable_update_group, TARGET_TEMP_UPDATE_BIT_PUBLISHER_TASK | WAKE_UP_BIT_THERMO_TASK);
            }

            else if(cJSON_HasObjectItem(root, "deltaTemp") && cJSON_GetObjectItem(root, "deltaTemp")->valuedouble >= MIN_DELTA_TEMP && cJSON_GetObjectItem(root, "deltaTemp")->valuedouble <= MAX_DELTA_TEMP)
            {
                delta_temp = cJSON_GetObjectItem(root, "deltaTemp")->valuedouble;
                xEventGroupSetBits(global_variable_update_group, DELTA_TEMP_UPDATE_BIT_PUBLISHER_TASK | WAKE_UP_BIT_THERMO_TASK);
            }

            else if(cJSON_HasObjectItem(root, "mainSwitch"))    //interruttore generale termostato true->acceso, false->spento
            {
                cJSON *boolean_value = cJSON_GetObjectItem(root, "mainSwitch");
                if(cJSON_IsTrue(boolean_value))
                    main_switch = true;
                else
                    main_switch = false;

                xEventGroupSetBits(global_variable_update_group, MAIN_SWITCH_UPDATE_BIT_PUBLISHER_TASK | WAKE_UP_BIT_THERMO_TASK);
            }

            else if(cJSON_HasObjectItem(root, "progSwitch"))    //interruttore programmazione oraria settimanale abilitata/disabilitata true->abilitata, false->disabilitata
            {
                cJSON *boolean_value = cJSON_GetObjectItem(root, "progSwitch");
                if(cJSON_IsTrue(boolean_value))
                    prog_switch = true;
                else
                    prog_switch = false;

                xEventGroupSetBits(global_variable_update_group, PROG_SWITCH_UPDATE_BIT_PUBLISHER_TASK | WAKE_UP_BIT_THERMO_TASK);
            }

            //programmazione settimanale, orario di inizio per un determintato giorno
            else if(cJSON_HasObjectItem(root, "startTime") && cJSON_HasObjectItem(root, "weekdaySelected") && cJSON_GetObjectItem(root, "weekdaySelected")->valueint >= 0 && cJSON_GetObjectItem(root, "weekdaySelected")->valueint <= 6)
            {
                day_selected = cJSON_GetObjectItem(root, "weekdaySelected")->valueint;
                strncpy(start_time, cJSON_GetObjectItem(root, "startTime")->valuestring, 8);
                
            }

            //programmazione settimanale, orario di fine per il giorno selezionato precedentemente
            else if(cJSON_HasObjectItem(root, "endTime") && cJSON_HasObjectItem(root, "weekdaySelected") && cJSON_GetObjectItem(root, "weekdaySelected")->valueint >= 0 && cJSON_GetObjectItem(root, "weekdaySelected")->valueint <= 6 && cJSON_GetObjectItem(root, "weekdaySelected")->valueint == day_selected)
            {
                strncpy(end_time, cJSON_GetObjectItem(root, "endTime")->valuestring, 8);
                insert_into_interval_array(week_prog[day_selected], start_time, end_time, TIME_INTERVALS_PER_DAY);
                day_selected = -1;
                xEventGroupSetBits(global_variable_update_group, WEEK_PROG_UPDATE_BIT_PUBLISHER_TASK | WAKE_UP_BIT_THERMO_TASK);
            }

            //elimina programmazione per un giorno della settimana
            else if(cJSON_HasObjectItem(root, "weekdayClear") && cJSON_GetObjectItem(root, "weekdayClear")->valueint >= 0 && cJSON_GetObjectItem(root, "weekdayClear")->valueint <= 6)
            {
                day_selected = cJSON_GetObjectItem(root, "weekdayClear")->valueint;
                init_interval_array(week_prog[day_selected], TIME_INTERVALS_PER_DAY);
                xEventGroupSetBits(global_variable_update_group, WEEK_PROG_UPDATE_BIT_PUBLISHER_TASK | WAKE_UP_BIT_THERMO_TASK);
            }

            else if (cJSON_HasObjectItem(root, "baseTemp") && cJSON_GetObjectItem(root, "baseTemp")->valuedouble >= MIN_BASE_TEMP && cJSON_GetObjectItem(root, "baseTemp")->valuedouble <= MAX_BASE_TEMP)
            {
                base_temp = cJSON_GetObjectItem(root, "baseTemp")->valuedouble;
                xEventGroupSetBits(global_variable_update_group, BASE_TEMP_UPDATE_BIT_PUBLISHER_TASK | WAKE_UP_BIT_THERMO_TASK);
            }
            
            //richiesta di aggiornamento forzato dello stato da parte dell'app, invio di tutti i valori di stato
            else if(cJSON_HasObjectItem(root, "updateRequest"))
            {
                cJSON *boolean_value = cJSON_GetObjectItem(root, "updateRequest");
                if(cJSON_IsTrue(boolean_value))
                    xEventGroupSetBits(global_variable_update_group, UPDATE_REQUEST_BIT_PUBLISHER_TASK);
            }

            cJSON_Delete(root);
            root = NULL;    
        }
        
        free(buffer);   // deallocazione del buffer creato al ricevimento dei dati
    }

    vTaskDelete(NULL);
}

/*EVENT HANDLERS*/

/*event handler per eventi mqtt*/

static void mqtt_client_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{    
    if(event_id == MQTT_EVENT_CONNECTED)    //connessione al broker riuscita
    {
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        xEventGroupSetBits(connection_event_group, MQTT_CONNECTED_BIT);     
    }
    else if(event_id == MQTT_EVENT_DISCONNECTED)    //connessione al broker non riuscita o persa
    {
        xEventGroupSetBits(connection_event_group, MQTT_FAIL_BIT);
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
    }
    else if (event_id == MQTT_EVENT_DATA)   //dati mqtt per topic sottoscritto
    {
        esp_mqtt_event_handle_t event = event_data;
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        char * buffer = (char*)malloc((event->data_len) * sizeof(char) + 1); //allocazione dinamica di un buffer di memoria in base alla lungezza dei dati in ingresso
        memcpy(buffer, event->data, event->data_len  * sizeof(char)); //copia dei dati in ingesso nel buffer
        buffer[event->data_len] = '\0';                               //terminatore di stringa nel casoi dati in ingresso non siano null terminated
        xQueueSend(mqtt_data_pointers_queue_handler, &buffer, portMAX_DELAY);     //il puntantore viene inserito nella queue

        // la free avviene nel task json_decode_global_variables_update_task
    }
}

/*event handler per gli eventi di sistema, wi-fi event e ip event*/

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{   
    static int s_retry_num;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) 
        esp_wifi_connect();

    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) 
        {
            esp_wifi_connect();
            s_retry_num++;    
        } 

        else 
        {
            s_retry_num = 0;
            xEventGroupSetBits(connection_event_group, WIFI_FAIL_BIT);
        }

    } 
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) 
    {
        s_retry_num = 0;
        xEventGroupSetBits(connection_event_group, WIFI_CONNECTED_BIT);
    }
}

/* 
************************* 
    MAIN
*************************
*/

void app_main()
{   
    esp_event_loop_create_default();    //creazione dell'event loop di sistema
    
    //creazione delle strutture degli event group

    connection_event_group = xEventGroupCreate();
    reconnection_request_group = xEventGroupCreate();
    global_variable_update_group = xEventGroupCreate();

    mqtt_data_pointers_queue_handler = xQueueCreate(5, sizeof(char*));  //creazione della queue per i dati mqtt

    //setup dell'applicazione

    wifi_setup();
    sntp_setup();
    mqtt_client_setup();
    week_prog_setup();
    gpio_setup();
    dht_setup();
    
    //creazione dei task

    xTaskCreate(measure_task, "measure_task", 2048, (void*)1, 1, NULL);
    xTaskCreate(led_builtin_blinker_task, "led_builtin_blinker_task", configMINIMAL_STACK_SIZE, (void*)1, 1, &led_builtin_blinker_task_handler);
    xTaskCreate(connection_event_manager_task, "connection_event_manager_task", 2048, (void*)1, 2, &connection_event_manager_task_handler);
    xTaskCreate(mqtt_publish_json_task, "mqtt_publish_json_task", 2048, (void*)1, 1, &mqtt_publish_json_task_handler);
    xTaskCreate(try_to_reconnect_task, "try_to_reconnect_task", 2048, (void*)1, 1, &try_to_reconnect_task_handler);   
    xTaskCreate(json_decode_global_variables_update_task, "json_decode_global_variables_update_task", 2048, (void*)1, 1, json_decode_global_variables_update_task_handler); 
    xTaskCreate(thermo_task, "thermo_task", 2048, (void*)1, 1, thermo_task_handler);
}

/*FUNZIONI LOCALI*/

//configurazione e avvio del wi-fi
void wifi_setup(void)   
{
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
   esp_wifi_init(&cfg);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS
        },
    };

    if (strlen((char *)wifi_config.sta.password)) 
    {
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    }

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL);
    tcpip_adapter_init();
    esp_wifi_start();
    ESP_LOGI(TAG, "wifi_setup finished.");
}

//configurazione del servizio sntp e impostazione della timezone locale
void sntp_setup(void)
{
    ESP_LOGI(TAG, "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
}

//configurazione dei gpio
void gpio_setup(void)
{
    gpio_config_t io_conf;  //struttura per la configurazione dei gpio

    io_conf.intr_type = GPIO_INTR_DISABLE; //disabilita gli interrupt
    io_conf.mode = GPIO_MODE_OUTPUT;    // output
    io_conf.pin_bit_mask = LED_BUILTIN_MASK | RELAY_MASK;    //maschera per la selezione dei gpio su cui applicare la conf.
    io_conf.pull_down_en = 0;   //pull-down disabilitato
    io_conf.pull_up_en = 0;     //pull-up disabilitato

    gpio_config(&io_conf);      //applica la configurazione ai gpio

    gpio_set_level(LED_BUILTIN, 1); //spegnimento del led builtin attivo basso
    gpio_set_level(RELAY, 0);
}

//configurazione del sensore dht
void dht_setup(void)
{
    dht_config_t dht_conf;
    dht_conf.dht_gpio = GPIO_NUM_5;
    dht_conf.dht_type = DHT_11;
    dht_conf.safe_mode = true;

    dht_config(&dht_conf);
}

//configurazione del client mqtt
void mqtt_client_setup(void)
{   
    char rendered[25];

    ESP_LOGI(TAG, "Initializing MQTT");
    cJSON *root = cJSON_CreateObject(); //messaggio di last will in formato json
    cJSON_AddFalseToObject(root, "nodeOnline");
    cJSON_PrintPreallocated(root, rendered, 25, 0);
    esp_mqtt_client_config_t mqtt_cfg = {
        .uri = CONFIG_BROKER_URL,
        .lwt_msg = rendered, 
        .lwt_topic = MQTT_DATA_PUBLISH_TOPIC, //last will topic
        .lwt_qos = 0,  
    };
    cJSON_Delete(root);
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_client_event_handler, mqtt_client);   
}

//inizializzazione della struttura dati che memorizza la prgrammazione settimanale
void week_prog_setup(void)
{
    for(int i=0; i<DAYS_PER_WEEK; i++)
        init_interval_array(week_prog[i], TIME_INTERVALS_PER_DAY);
}
