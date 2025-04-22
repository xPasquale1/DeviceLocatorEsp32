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

Wifi::TCPConnection conn;

uint16_t scanCount = 0;

unsigned long _idle = 0;
unsigned long lastPing = 0;
bool sendScan = false;

void runScan(bool avgScan=false){
    if(networkData.size() < 1) return;
    Wifi::disconnectTCPConnection(conn);
    // delay(400);
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
    int retries = 3;
    if(!Wifi::getFlag(Wifi::WIFICONNECTED)){
        for(int i=0; i < retries; ++i){
            if(Wifi::connect(5000) == ERR_OK) break;
        }
        if(!Wifi::getFlag(Wifi::WIFICONNECTED)){
            Serial.println("Reconnect nach Scan gescheitert");
            return;
        }
    }
    lastPing = millis();
    Serial.print("Scan hat "); Serial.print(millis() - startTime); Serial.println(" ms gedauert");
    sendScan = true;
    if(Wifi::connectTCPConnection(conn, Wifi::client.ipInfo.gw.addr, 4984, 3000) == ESP_OK){
        if(Wifi::sendMessagecodeTCPConnection(conn, Wifi::SEND_SIGNALSTRENGTH, networkData.data(), networkData.size()) <= 0){
            Serial.println("Konnte Scan Daten nicht senden!");
        }else{
            sendScan = false;
        }
    }else{
        Serial.println("Konnte keine Verbindung nach Scan herstellen!");
    }
    _idle = millis();
    return;
}

//TODO Theoretisch könnte man hier auch listen machen...
void checkNetwork(){
    char buffer[1024];
    int length = Wifi::receiveTCPConnection(conn, buffer, sizeof(buffer));
    if(length == 0){
        Serial.println("Verbindung sauber geschlossen");
        Wifi::disconnectTCPConnection(conn);
    }
    int idx = 0;
    while(length > 0){
        switch(buffer[idx]){
            case Wifi::ALIVE_REQ:{
                lastPing = millis();
                if(Wifi::sendMessagecodeTCPConnection(conn, Wifi::ALIVE_ACK, nullptr, 0) <= 0) Serial.println("Fehler beim senden von Alive ACK");
                idx++;
                length -= 1;
                break;
            }
            case Wifi::RESET_ROUTERS:{
                networkData.clear();
                Serial.println("Routereinträge gelöscht");
                if(Wifi::sendMessagecodeTCPConnection(conn, Wifi::ACK, nullptr, 0) <= 0) Serial.println("Fehler beim senden von ACK");
                idx++;
                length -= 1;
                break;
            }
            case Wifi::ADD_ROUTER:{
                int ssidLength = buffer[idx+1];
                Wifi::NetworkData router;
                router.ssid = new char[ssidLength+1];     //+1 für \0
                for(int i=0; i < ssidLength; ++i){
                    router.ssid[i] = buffer[idx+2+i];
                }
                router.ssid[ssidLength] = '\0';
                bool alreadySet = false;
                for(size_t i=0; i < networkData.size(); ++i){
                    if(strcmp(router.ssid, networkData[i].ssid) == 0){
                        alreadySet = true;
                        break;
                    }
                }
                if(alreadySet){
                    Serial.println("Router bereits vorhanden");
                    if(Wifi::sendMessagecodeTCPConnection(conn, Wifi::ACK, nullptr, 0) <= 0) Serial.println("Fehler beim senden von ACK");
                    idx += 2+ssidLength;
                    length -= 2+ssidLength;
                    break;
                }
                networkData.push_back(router);
                Serial.print("Neuen Router erhalten: ");
                Serial.println(router.ssid);
                if(Wifi::sendMessagecodeTCPConnection(conn, Wifi::ACK, nullptr, 0) <= 0) Serial.println("Fehler beim senden von ACK");
                idx += 2+ssidLength;
                length -= 2+ssidLength;
                break;
            }
            case Wifi::REQUEST_SCANS:{
                Serial.println("Request Scans bekommen!");
                scanCount = (buffer[idx+2]<<8) | buffer[idx+1];
                Serial.println(scanCount);
                if(Wifi::sendMessagecodeTCPConnection(conn, Wifi::ACK, nullptr, 0) <= 0) Serial.println("Fehler beim senden von ACK");
                do{
                    runScan(false);
                    if(scanCount > 0) scanCount--;
                }while(scanCount > 0);
                idx += 3;
                length -= 3;
                break;
            }
            case Wifi::REQUEST_AVG:{
                Serial.println("Request Avg bekommen!");
                if(Wifi::sendMessagecodeTCPConnection(conn, Wifi::ACK, nullptr, 0) <= 0) Serial.println("Fehler beim senden von ACK");
                runScan(true);
                idx++;
                length -= 1;
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
    if(Wifi::createTCPConnection(conn, 4984) != ERR_OK){
        Serial.println("Fehler bei createTCPConnection");
        while(1);
    }
    if(Wifi::setNetwork(WIFISSID0, WIFIPASSWORD0) != ERR_OK) while(1);
    Serial.println("Setup fertig. Suche Verbindung...");
    esp_err_t err;
	while(!Wifi::getFlag(Wifi::WIFICONNECTED)){
        err = Wifi::connect();
        if(err == ERR_OK){
            if(Wifi::connectTCPConnection(conn, Wifi::client.ipInfo.gw.addr, 4984, 3000) != ESP_OK) Serial.println("Konnte keine TCP Verbindung herstellen!");
            Serial.print("Default Gateway: ");
            Serial.println(inet_ntoa(Wifi::client.ipInfo.gw.addr));
        }
        Serial.println(err);
    }
	Serial.println(inet_ntoa(Wifi::client.ipInfo.ip.addr));
    Serial.println("Alles initialisiert");
}

void loop(){
    while(!Wifi::getFlag(Wifi::WIFICONNECTED)){
        if(Wifi::connect(3000) == ERR_OK){
            if(conn.transferSocket == -1){
                if(Wifi::connectTCPConnection(conn, Wifi::client.ipInfo.gw.addr, 4984, 3000) != ESP_OK) Serial.println("Konnte keine TCP Verbindung herstellen!");
                Serial.println(conn.transferSocket);
                Serial.print("Default Gateway: ");
                Serial.println(inet_ntoa(Wifi::client.ipInfo.gw.addr));
            }
        }
    }
    if(conn.transferSocket == -1){
        if(Wifi::connectTCPConnection(conn, Wifi::client.ipInfo.gw.addr, 4984, 3000) != ESP_OK) Serial.println("Konnte keine TCP Verbindung herstellen!");
        Serial.println(conn.transferSocket);
        Serial.print("Default Gateway: ");
        Serial.println(inet_ntoa(Wifi::client.ipInfo.gw.addr));
    }
    if(sendScan && conn.transferSocket != -1){
        if(Wifi::sendMessagecodeTCPConnection(conn, Wifi::SEND_SIGNALSTRENGTH, networkData.data(), networkData.size()) <= 0){
            Serial.println("Konnte Scan Daten nicht senden!");
        }else{
            sendScan = false;
        }
    }
    unsigned long cur = millis();
    if((cur - lastPing) >= 6000){
        if(Wifi::disconnectTCPConnection(conn) != ESP_OK) Serial.println("Disconnect gescheitert!");
        if(Wifi::getFlag(Wifi::WIFICONNECTED)) esp_wifi_disconnect();
        lastPing = cur;
    }
    checkNetwork();
}
