# termostato_iot
termostato iot realizzato con esp8266 per il controllo della temperatura e la programmazione del riscaldamento in ambiente domestico.
sviluppato con ESP8266 RTOS SDK, l'ambiente open source di sviluppo ufficiale di espressif, utilizzando freertos e il linguaggio C.

## interfaccia utente:
-> visualizzazione di temperatura e umidità in tempo reale.
-> andamento temperatura e umidità.
-> impostazione temperatura target.
-> impostazione temperatura di base (temperatura al di sotto della quale il riscaldamento parte comunque anche "off").
-> impostazione temperatura delta (differenza tra target desiderato e spegnimeto del riscaldamento per evitare accensioni e spegnimenti continui a ridosso del target).
-> programmazione settimanale al minuto, con gestione intelligente della sovrapposizione degli intervalli.
-> switch di accensione e spegnimento generale.
-> switch di attivazione e disattivazione della programmazione temporale.

## app smartphone per la realizzazione dell'interfaccia utente:
Iot MQTT Panel, disponibile su appstore e playstore

## files sorgente:
main.c: file main con i task RTOS e gli event handler che gestiscono il termostato.
dht.h, dht.c: libreria per la gestione e la lettura del sensore di temperatura e umidità di tipo dht11 o dht22, specifica per esp8266 RTOS SDK.
timeinterval.c, timeinterval.h: libreria per la gestione degli intervalli temporali.

## installazione:
lanciare il comando make flash dopo la configurazione dell'ambiente di sviluppo ESP8266 RTOS SDK
https://docs.espressif.com/projects/esp8266-rtos-sdk/en/latest/get-started/

 