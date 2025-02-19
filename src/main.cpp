#include <Arduino.h>
#include "network.h"
#include "constants.h"
#include <vector>

/*
    TODOS:
    Sollte Informationen an die Anwendung senden, sollte es Verbindungsprobleme,... geben, auch Statusupdates über den Scanfortschritt oder ähnliches wäre gut

    Die Anwendung sollte auch Statusmeldungen anfragen können, um z.B. die konfigurierten Router abzufragen, IP,...
*/

#define SENDPIN 35
#define INCXPIN 34
#define INCYPIN 32
#define SPAMPIN 33

#define BUTTONCOUNT 4
unsigned char debounceCounter[BUTTONCOUNT]{1};
unsigned char buttonPressed[BUTTONCOUNT]{0};

TaskHandle_t buttonTaskHandle;

std::vector<Wifi::NetworkData> networkData;

Wifi::UDPServer server;

uint16_t scanCount = 0;

unsigned long _idle = 0;

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

void checkNetwork(){
    sockaddr_in transmitter;
    int length = Wifi::recvData(server, &transmitter);
    if(length > 0){
        switch(server.recvBuffer[0]){
            case Wifi::RESET_ROUTERS:{
                networkData.clear();
                Serial.println("Routereinträge gelöscht");
                break;
            }
            case Wifi::ADD_ROUTER:{
                Wifi::NetworkData router;
                router.ssid = new char[length];     //+1 für \0
                for(int i=0; i < length; ++i){
                    router.ssid[i-1] = server.recvBuffer[i];
                }
                router.ssid[length-1] = '\0';
                bool alreadySet = false;
                for(size_t i=0; i < networkData.size(); ++i){
                    if(strcmp(router.ssid, networkData[i].ssid) == 0){
                        alreadySet = true;
                        break;
                    }
                }
                if(alreadySet){
                    Serial.println("Router bereits vorhanden");
                    break;
                }
                networkData.push_back(router);
                Serial.print("Neuen Router erhalten: ");
                Serial.println(router.ssid);
                break;
            }
            case Wifi::SETSENDIP:{
                uint32_t ip;
                uint16_t port;
                memcpy(&ip, server.recvBuffer + 1, 4);
                memcpy(&port, server.recvBuffer + 5, 2);
                ip = ntohl(ip);
                port = ntohs(port);
                Wifi::changeUDPServerDestination(server, ip, port);
                Serial.print("Neue Ziel-IP erhalten: ");
                Serial.print(inet_ntoa(ip));
                Serial.print(":");
                Serial.println(port);
                break;
            }
            case Wifi::REQUEST_SCANS:{
                Serial.println("Request bekommen!");
                scanCount = (server.recvBuffer[2]<<8) | server.recvBuffer[1];
                Serial.println(scanCount);
                buttonPressed[3] = 1;
                break;
            }
            case Wifi::REQUEST_AVG:{
                Serial.println("Request bekommen!");
                buttonPressed[0] = 1;
                break;
            }
        }
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

void runScan(bool avgScan=false){
    if(networkData.size() < 1) return;
    unsigned long startTime = millis();
    for(uint8_t i=0; i < networkData.size(); ++i){
        Serial.print("Scanne: ");
        Serial.println(networkData[i].ssid);
        esp_err_t err = avgScan ? Wifi::scanForNetworkAvg(networkData[i], 512, Wifi::AVERAGE, 10, 4, 40) : Wifi::scanForNetwork(networkData[i], 6, 30);
        if(err != ERR_OK){
            Serial.println("Scan fehlgeschlagen :c");
            return;
        }
        Serial.println(networkData[i].rssi);
    }
    if(!Wifi::getFlag(Wifi::WIFICONNECTED))
        if(Wifi::connect(2000) != ERR_OK){
            Serial.println("Reconnect Fehler nach Scan");
            return;
        }
    Serial.print("Scan hat "); Serial.print(millis() - startTime); Serial.println(" ms gedauert");
    if(Wifi::sendMessagecode(server, Wifi::SEND_SIGNALSTRENGTH, networkData.data(), networkData.size()) <= 0) Serial.println("Fehler beim senden von scan Daten");
    delay(10);  //TODO Sockets sind blöd, keine Ahnung was die Funktionen genau machen, die Beschreibungen sind viel zu schlicht, der delay ist nur da, weil sonst die Pakete nicht ankommen
    _idle = millis();
    return;
}

void setup(){
	Serial.begin(9600);
    pinMode(SENDPIN, INPUT);
    pinMode(INCXPIN, INPUT);
    pinMode(INCYPIN, INPUT);
    pinMode(SPAMPIN, INPUT);
    if(Wifi::init() != ERR_OK){
        Serial.println("Fehler bei Wifi::init");
        while(1);
    }
    xTaskCreatePinnedToCore(buttonTask, "buttonTask", 2000, nullptr, 0, &buttonTaskHandle, 0);    //TODO 1000 war zu wenig scheinbar...
    if(Wifi::createUDPServer(server, "255.255.255.255", 4984) != ERR_OK){
        Serial.println("Fehler bei createUDPServer");
        while(1);
    }
    if(Wifi::setNetwork(WIFISSID0, WIFIPASSWORD0) != ERR_OK) while(1);
    Serial.println("Setup fertig. Suche Verbindung...");
    esp_err_t err;
	while(!Wifi::getFlag(Wifi::WIFICONNECTED)){
        err = Wifi::connect();
        Serial.println(err);
    }
	Serial.println(inet_ntoa(Wifi::client.ipInfo.ip.addr));
    Serial.println("Alles initialisiert");
}

void loop(){
    while(!Wifi::getFlag(Wifi::WIFICONNECTED)) Wifi::connect(3000);
    unsigned long cur = millis();
    if((cur - _idle) >= 30000){     //Force einen Disconnect ca. alle 30 Sekunden, da das WIFI_DISCONNECTED Event nicht immer funktioniert... 
        if(Wifi::getFlag(Wifi::WIFICONNECTED)) esp_wifi_disconnect();
        _idle = cur;
    }
    if(buttonPressed[0] == 1){
        buttonPressed[0] = 2;
        runScan(true);
    }
    if(buttonPressed[1] == 1){
        buttonPressed[1] = 2;
        if(Wifi::sendMessagecode(server, Wifi::SEND_POSITION_X, nullptr, 0) <= 0) Serial.println("Fehler beim senden von x");
    }
    if(buttonPressed[2] == 1){
        buttonPressed[2] = 2;
        if(Wifi::sendMessagecode(server, Wifi::SEND_POSITION_Y, nullptr, 0) <= 0) Serial.println("Fehler beim senden von y");
    }
    if(buttonPressed[3] == 1){
        do{
            runScan(false);
            if(scanCount > 0) scanCount--;
        }while(scanCount > 0);
    }
    checkNetwork();
}
