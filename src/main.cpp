#include <Arduino.h>
#include "network.h"
#include "constants.h"

/*
    TODOS:
    Router nicht statisch hier festlegen, am besten einen Server laufen lassen dem man die Routerdaten mitteilen kann, ebenso wie IP-Adresse des UPD-Ziels

    Sollte Informationen an den CLient senden, sollte es Verbindungsprobleme,... geben, auch Statusupdates über den Scanfortschritt oder ähnliches wäre gut

    Der Scan kommt mir immer noch sehr langsam vor... Passives Scanning dauert auch 400ms pro Router... Überlegen woran das liegen könnte
*/

#define SENDPIN 35
#define INCXPIN 34
#define INCYPIN 32
#define SPAMPIN 33

#define BUTTONCOUNT 4
unsigned char debounceCounter[BUTTONCOUNT]{1};
unsigned char buttonPressed[BUTTONCOUNT]{0};

TaskHandle_t buttonTaskHandle;

#define NETWORKCOUNT 3
Wifi::NetworkData networkData[NETWORKCOUNT];

Wifi::UDPClient client;

void debounceButton(uint8_t pin, uint8_t idx){
    digitalRead(pin) == HIGH ? ++debounceCounter[idx] : --debounceCounter[idx];
    if(debounceCounter[idx] > 8) debounceCounter[idx] = 8;
    if(debounceCounter[idx] < 1) debounceCounter[idx] = 1;
    if(debounceCounter[idx] > 6 && buttonPressed[idx] == 0){
        buttonPressed[idx] = 1;
    }else if(debounceCounter[idx] < 3 && buttonPressed[idx] > 0){
        buttonPressed[idx] = 0;
    }
}

void buttonTask(void* params){
    while(1){
        debounceButton(SENDPIN, 0);
        debounceButton(INCXPIN, 1);
        debounceButton(INCYPIN, 2);
        debounceButton(SPAMPIN, 3);
        delay(5);
    }
}

void setup(){
	Serial.begin(9600);
    pinMode(SENDPIN, INPUT);
    pinMode(INCXPIN, INPUT);
    pinMode(INCYPIN, INPUT);
    pinMode(SPAMPIN, INPUT);
    if(Wifi::init() != ERR_OK) while(1);
    xTaskCreatePinnedToCore(buttonTask, "buttonTask", 1000, nullptr, 0, &buttonTaskHandle, 0);    //TODO 1000 wird bestimmt nicht nötig sein
    if(Wifi::createUDPClient(client, "192.168.178.66", 4984) != ERR_OK) while(1);
    Serial.println("Alles initialisiert");
    networkData[0].ssid = WIFISSID0;
    networkData[1].ssid = WIFISSID1;
    networkData[2].ssid = WIFISSID2;
    while(!Wifi::getFlag(Wifi::WIFICONNECTED)){
        if(Wifi::setNetwork(WIFISSID0, WIFIPASSWORD0, 6000) == ERR_OK){
            Serial.println("Verbindung hergestellt!");
            break;
        }
    }
}

void loop(){
    while(!Wifi::getFlag(Wifi::WIFICONNECTED)) Wifi::reconnect(6000);
    if(buttonPressed[0] == 1){
        buttonPressed[0] = 2;
        unsigned long startTime = millis();
        for(uint8_t i=0; i < NETWORKCOUNT; ++i){
            if(Wifi::scanForNetworkAvg(networkData[i], 128, Wifi::MEDIAN, 10, 4, 40) != ERR_OK) Serial.println("Avg Scan Fehler");
            Serial.println(networkData[i].rssi);
        }
        if(Wifi::reconnect(5000) != ERR_OK) Serial.println("Reconnect Fehler nach Scan");
        Serial.print("Scan hat "); Serial.print(millis() - startTime); Serial.println(" ms gedauert");
        for(uint8_t i=0; i < 3; ++i)
            if(Wifi::sendData(client, Wifi::SEND_SIGNALSTRENGTH, networkData, NETWORKCOUNT) <= 0) Serial.println("Fehler beim senden von rssi avg");
    }
    if(buttonPressed[1] == 1){
        buttonPressed[1] = 2;
        if(Wifi::sendData(client, Wifi::SEND_POSITION_X, nullptr, 0) <= 0) Serial.println("Fehler beim senden von x");
    }
    if(buttonPressed[2] == 1){
        buttonPressed[2] = 2;
        if(Wifi::sendData(client, Wifi::SEND_POSITION_Y, nullptr, 0) <= 0) Serial.println("Fehler beim senden von y");
    }
    if(buttonPressed[3] == 1){
        unsigned long startTime = millis();
        for(uint8_t i=0; i < NETWORKCOUNT; ++i){
            if(Wifi::scanForNetwork(networkData[i], 5, 40) != ERR_OK) Serial.println("Scan kaputt :c");
            Serial.println(networkData[i].rssi);
        }
        if(Wifi::reconnect(5000) != ERR_OK) Serial.println("Reconnect Fehler nach Scan");
        Serial.print("Scan hat "); Serial.print(millis() - startTime); Serial.println(" ms gedauert");
        if(Wifi::sendData(client, Wifi::SEND_SIGNALSTRENGTH, networkData, NETWORKCOUNT) <= 0) Serial.println("Fehler beim senden von rssi non avg");
        delay(10);  //TODO Senden ist mal wieder kaputt...
    }
}
