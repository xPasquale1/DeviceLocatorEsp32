#include <Arduino.h>
#include "network.h"
#include "constants.h"
#include <vector>

/*
    TODOS:
    Sollte Informationen an die Anwendung senden, sollte es Verbindungsprobleme,... geben, auch Statusupdates über den Scanfortschritt oder ähnliches wäre gut

    Die Anwendung sollte auch Statusmeldungen anfragen können, um z.B. die konfigurierten Router abzufragen, IP,...
*/

std::vector<Wifi::NetworkData> networkData;

Wifi::UDPServer server;

uint32_t customReceiverIP = 0;
uint16_t customReceiverPort = 0;
bool usesCustomReceiver = false;

uint16_t scanCount = 0;

unsigned long _idle = 0;

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
        if(!usesCustomReceiver){
            Wifi::changeUDPServerDestination(server, Wifi::client.ipInfo.gw.addr, 4984);
            Serial.print("Default Gateway: ");
            Serial.println(inet_ntoa(Wifi::client.ipInfo.gw.addr));
        }
    Serial.print("Scan hat "); Serial.print(millis() - startTime); Serial.println(" ms gedauert");
    if(Wifi::sendMessagecode(server, Wifi::SEND_SIGNALSTRENGTH, networkData.data(), networkData.size()) <= 0) Serial.println("Fehler beim senden von scan Daten");
    delay(10);  //TODO Sockets sind blöd, keine Ahnung was die Funktionen genau machen, die Beschreibungen sind viel zu schlicht, der delay ist nur da, weil sonst die Pakete nicht ankommen
    _idle = millis();
    return;
}

void checkNetwork(){
    sockaddr_in transmitter;
    int length = Wifi::recvData(server, &transmitter);
    if(length > 0){
        switch(server.recvBuffer[0]){
            case Wifi::RESET_ROUTERS:{
                networkData.clear();
                Serial.println("Routereinträge gelöscht");
                if(Wifi::sendMessagecode(server, Wifi::ACK, nullptr, 0) <= 0) Serial.println("Fehler beim senden von scan Daten");
                delay(10);  //TODO Sockets sind blöd, keine Ahnung was die Funktionen genau machen, die Beschreibungen sind viel zu schlicht, der delay ist nur da, weil sonst die Pakete nicht ankommen
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
                if(Wifi::sendMessagecode(server, Wifi::ACK, nullptr, 0) <= 0) Serial.println("Fehler beim senden von scan Daten");
                delay(10);  //TODO Sockets sind blöd, keine Ahnung was die Funktionen genau machen, die Beschreibungen sind viel zu schlicht, der delay ist nur da, weil sonst die Pakete nicht ankommen
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
                customReceiverIP = ip;
                customReceiverPort = port;
                usesCustomReceiver = true;
                Serial.print("Neue Ziel-IP erhalten: ");
                Serial.print(inet_ntoa(ip));
                Serial.print(":");
                Serial.println(port);
                if(Wifi::sendMessagecode(server, Wifi::ACK, nullptr, 0) <= 0) Serial.println("Fehler beim senden von scan Daten");
                delay(10);  //TODO Sockets sind blöd, keine Ahnung was die Funktionen genau machen, die Beschreibungen sind viel zu schlicht, der delay ist nur da, weil sonst die Pakete nicht ankommen
                break;
            }
            case Wifi::REQUEST_SCANS:{
                Serial.println("Request Scans bekommen!");
                scanCount = (server.recvBuffer[2]<<8) | server.recvBuffer[1];
                Serial.println(scanCount);
                if(Wifi::sendMessagecode(server, Wifi::ACK, nullptr, 0) <= 0) Serial.println("Fehler beim senden von scan Daten");
                delay(10);  //TODO Sockets sind blöd, keine Ahnung was die Funktionen genau machen, die Beschreibungen sind viel zu schlicht, der delay ist nur da, weil sonst die Pakete nicht ankommen
                do{
                    runScan(false);
                    if(scanCount > 0) scanCount--;
                }while(scanCount > 0);
                break;
            }
            case Wifi::REQUEST_AVG:{
                Serial.println("Request Avg bekommen!");
                if(Wifi::sendMessagecode(server, Wifi::ACK, nullptr, 0) <= 0) Serial.println("Fehler beim senden von scan Daten");
                delay(10);  //TODO Sockets sind blöd, keine Ahnung was die Funktionen genau machen, die Beschreibungen sind viel zu schlicht, der delay ist nur da, weil sonst die Pakete nicht ankommen
                runScan(true);
                break;
            }
            case Wifi::REQ_STATUS:{
                Serial.println("Request Status bekommen!");
                uint8_t buffer[400];
                uint32_t ip = server.receiver.sin_addr.s_addr;
                uint16_t port = server.receiver.sin_port;
                memcpy(buffer, (void*)&ip, 4);
                memcpy(buffer+4, (void*)&port, 2);
                buffer[6] = networkData.size();
                uint32_t offset = 7;
                for(uint32_t i=0; i < networkData.size(); ++i){
                    for(uint32_t j=0; j < strlen(networkData[i].ssid); ++j){
                        buffer[offset++] = networkData[i].ssid[j];
                    }
                    buffer[offset++] = '\0';
                }
                if(Wifi::sendMessagecode(server, Wifi::ACK, buffer, offset) <= 0) Serial.println("Fehler beim senden von scan Daten");
                delay(10);  //TODO Sockets sind blöd, keine Ahnung was die Funktionen genau machen, die Beschreibungen sind viel zu schlicht, der delay ist nur da, weil sonst die Pakete nicht ankommen
                break;
            }
        }
    }
}

void setup(){
	Serial.begin(9600);
    if(Wifi::init() != ERR_OK){
        Serial.println("Fehler bei Wifi::init");
        while(1);
    }
    if(Wifi::createUDPServer(server, "192.168.178.1", 4984) != ERR_OK){     //Schickt per default ans Standardgateway
        Serial.println("Fehler bei createUDPServer");
        while(1);
    }
    if(Wifi::setNetwork(WIFISSID0, WIFIPASSWORD0) != ERR_OK) while(1);
    Serial.println("Setup fertig. Suche Verbindung...");
    esp_err_t err;
	while(!Wifi::getFlag(Wifi::WIFICONNECTED)){
        err = Wifi::connect();
        if(err == ERR_OK){
            if(!usesCustomReceiver){
                Wifi::changeUDPServerDestination(server, Wifi::client.ipInfo.gw.addr, 4984);
                Serial.print("Default Gateway: ");
                Serial.println(inet_ntoa(Wifi::client.ipInfo.gw.addr));
            }
        }
        Serial.println(err);
    }
	Serial.println(inet_ntoa(Wifi::client.ipInfo.ip.addr));
    Serial.println("Alles initialisiert");
}

void loop(){
    while(!Wifi::getFlag(Wifi::WIFICONNECTED)){
        if(Wifi::connect(3000) == ERR_OK){
            if(!usesCustomReceiver){
                Wifi::changeUDPServerDestination(server, Wifi::client.ipInfo.gw.addr, 4984);
                Serial.print("Default Gateway: ");
                Serial.println(inet_ntoa(Wifi::client.ipInfo.gw.addr));
            }
        }
    }
    unsigned long cur = millis();
    if((cur - _idle) >= 10000){     //Force einen Disconnect ca. alle 10 Sekunden, da das WIFI_DISCONNECTED Event nicht immer funktioniert... //TODO Sollte mir REQ und ACK erstetzt werden
        if(Wifi::getFlag(Wifi::WIFICONNECTED)) esp_wifi_disconnect();
        _idle = cur;
    }
    checkNetwork();
}
