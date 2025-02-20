#pragma once

#include <Arduino.h>
#include <memory.h>
#include "esp_wifi.h"
#include "lwip/sockets.h"
#include <vector>

namespace Wifi{

    enum WIFIFLAGS{
        WIFICONNECTED = 1,
        WIFISTARTED = 2
    };

    struct NetworkData{
        char* ssid;
        int8_t rssi;
        uint8_t channel = 0;
        uint8_t bssid[6]{0};
    };

    enum MESSAGECODES{
        SEND_POSITION_X,
        SEND_POSITION_Y,
        SEND_SIGNALSTRENGTH,
        ADD_ROUTER,
        RESET_ROUTERS,
        SETSENDIP,
        ACK,
        REQUEST_AVG,
        REQUEST_SCANS,
        SCAN_INFO,
        SEND_STATUS
    };
    /*  Nachrichtenformate:
        1 Byte (0x00) SEND_POSITION_X
        1 Byte (0x01) SEND_POSITION_Y
        1 Byte (0x02) SEND_SIGNALSTRENGTH   | 1 Byte RSSI Router 1  | 1 Byte RSSI Router 2  | 1 Byte RSSI Router 3,...
        1 Byte (0x03) ADD_ROUTER            | n Bytes SSID
        1 Byte (0x04) RESET_ROUTERS
        1 Byte (0x05) SETSENDIP             | 4 Bytes IP            | 2 Bytes PORT
        1 Byte (0x06) ACK
        1 BYTE (0x07) REQUEST_AVG
        1 BYTE (0x08) REQUEST_SCANS         | 2 Bytes Count        //TODO Scantyp angeben können
        1 BYTE (0x09) SCAN_INFO             | 2 Bytes Anzahl Erfolgreicher Scans            | 2 Bytes Fehlerhafte Scans | 2 Bytes Durchscnittliche Zeit pro Scan
        1 BYTE (0x0A) SEND_STATUS           | 4 Bytes IP            | 2 Bytes Port          | 1 Byte SSID Anzahl        | n Bytes SSIDs
    */

    struct WifiStation{
        esp_netif_t* netif = nullptr;
        volatile uint8_t flags = 0;
        esp_netif_ip_info_t ipInfo = {};
        uint8_t mac[6];
    }; static WifiStation client;

    void setFlag(WIFIFLAGS flag){client.flags |= flag;}
    void resetFlag(WIFIFLAGS flag){client.flags &= ~flag;}
    bool getFlag(WIFIFLAGS flag){return (client.flags & flag);}

    void eventHandler(void* event_handler_arg, esp_event_base_t event_base, int32_t event_id, void* event_data){
        if(event_base == WIFI_EVENT){
            switch(event_id){
                case WIFI_EVENT_STA_START:{
                    Serial.println("Wifi started Event");
                    setFlag(WIFISTARTED);
                    break;
                }
                case WIFI_EVENT_STA_STOP:{
                    Serial.println("Wifi stopped Event");
                    resetFlag(WIFISTARTED);
                    break;
                }
                case WIFI_EVENT_STA_CONNECTED:{
                    Serial.println("Wifi connected Event");
                    break;
                }
                case WIFI_EVENT_STA_DISCONNECTED:{
                    Serial.println("Wifi disconnected Event");
                    resetFlag(WIFICONNECTED);
                    break;
                }
            }
        }else if(event_base == IP_EVENT){
            switch(event_id){
                case IP_EVENT_STA_GOT_IP:{
                    ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
                    client.ipInfo = event->ip_info;
                    Serial.println("Wifi got ip Event");
                    setFlag(WIFICONNECTED);
                    break;
                }
            }
        }
    }

    void printPacketTag(uint8_t* buffer, uint32_t length){
        for(uint16_t i=0; i < length;){
            Serial.print("Tag: ");
            Serial.println(buffer[i++]);
            uint16_t tagLength = buffer[i++];
            Serial.print("Länge: ");
            Serial.println(tagLength);
            Serial.print("Daten: ");
            for(uint16_t j=0; j < tagLength; ++j){
                Serial.print(buffer[i++]);
                Serial.print(" ");
            }
            Serial.println();
        }
        Serial.println("--------------------------------------");
    }

    bool ssidCmp(const char* ssid1, uint8_t length1, const char* ssid2, uint8_t length2){
        if(length1 != length2) return false;
        for(uint16_t i=0; i < length1; ++i){
            if(ssid1[i] != ssid2[i]) return false;
        }
        return true;
    }

    struct ExpectedScanData{
        const char* ssid;
        uint8_t ssidLength;
        int8_t rssi;
        uint8_t channel;
        bool validData;
    }; static volatile ExpectedScanData scanData;

    void promiscuousPacketHandler(void* buffer, wifi_promiscuous_pkt_type_t type){
        if(type == WIFI_PKT_MGMT){  //TODO unnötig, da alle anderen Pakete eh gefiltert werden sollten
            wifi_promiscuous_pkt_t* packet = (wifi_promiscuous_pkt_t*)buffer;
            uint8_t managmentType = packet->payload[0]>>4;
            if(managmentType == 0b0101){
                uint8_t* ssidField = packet->payload+38;
                uint8_t ssidLength = packet->payload[37];
                if(ssidCmp(scanData.ssid, scanData.ssidLength, (char*)ssidField, ssidLength)){
                    scanData.rssi = packet->rx_ctrl.rssi;
                    scanData.channel = packet->rx_ctrl.channel;
                    scanData.validData = true;
                }
                // for(uint8_t i=0; i < ssidLength; ++i){
                //     Serial.print((char)ssidField[i]);
                // }
                // Serial.println();
            }
            // if(managmentType == 0b0100){
            //     Serial.print("Probe Request der Länge ");
            //     Serial.print(packet->rx_ctrl.sig_len);
            //     Serial.print(": ");
            //     for(uint8_t i=0; i < 24; ++i){
            //         Serial.print(packet->payload[i]);
            //         Serial.print(" ");
            //     }
            //     Serial.println();
            //     printPacketTag(&packet->payload[24], packet->rx_ctrl.sig_len-24);
            // }
        }
    }

    //Nicht blockend, initialisiert den Wifi Treiber im STA Modus
    esp_err_t init(){
        esp_err_t err;
        if(err = esp_netif_init() != ERR_OK) return err;
        if(err = esp_event_loop_create_default() != ERR_OK) return err;

        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        if(err = esp_wifi_init(&cfg) != ERR_OK) return err;

        if(err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, eventHandler, NULL, NULL) != ERR_OK) return err;
        if(err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, eventHandler, NULL, NULL) != ERR_OK) return err;

        if(err = esp_wifi_set_promiscuous(true) != ERR_OK) return err;
        if(err = esp_wifi_set_promiscuous_rx_cb(promiscuousPacketHandler) != ERR_OK) return err;
        wifi_promiscuous_filter_t filters;
        filters.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;
        if(err = esp_wifi_set_promiscuous_filter(&filters) != ERR_OK) return err;

        //TODO idk wie man das macht
        wifi_ant_gpio_config_t antennaPinConfig = {};
        // ESP32-WROOM-DA boards default antenna pins
        antennaPinConfig.gpio_cfg[0] = {.gpio_select = 1, .gpio_num = 21};
        antennaPinConfig.gpio_cfg[1] = {.gpio_select = 1, .gpio_num = 22};
        if(err = esp_wifi_set_ant_gpio(&antennaPinConfig) != ERR_OK) return err;
        wifi_ant_config_t antennaConfig;
        antennaConfig.rx_ant_mode = WIFI_ANT_MODE_ANT0;
        antennaConfig.tx_ant_mode = WIFI_ANT_MODE_ANT0;
        antennaConfig.rx_ant_default = WIFI_ANT_ANT0;
        antennaConfig.enabled_ant0 = 0;
        antennaConfig.enabled_ant1 = 1;
        if(err = esp_wifi_set_ant(&antennaConfig) != ERR_OK) return err;

        if(err = esp_wifi_get_mac(WIFI_IF_STA, client.mac) != ERR_OK) return err;
        return ERR_OK;
    }

    //Blockend
    esp_err_t connect(unsigned long timeoutMillis = 5000){
        esp_err_t err;
        if(getFlag(WIFICONNECTED)) return ERR_OK;
        if(err = esp_wifi_connect() != ERR_OK) return err;
        unsigned long startTime = millis();
        while(!getFlag(WIFICONNECTED)){
            if(millis() - startTime >= timeoutMillis) return ESP_ERR_TIMEOUT;
        }
        return ERR_OK;
    }

    //Blockend, konfiguriert das aktuelle Netzwerk, schließt Verbindung und startet den Wifi Treiber neu, verbindet aber nicht
    esp_err_t setNetwork(const char* ssid, const char* password){
        unsigned long startTime = millis();
        esp_err_t err;
        if(getFlag(WIFICONNECTED)){
            if(err = esp_wifi_disconnect() != ERR_OK){
                Serial.println("Fehler bei esp_wifi_disconnect");
                return err;
            }
        }
        if(getFlag(WIFISTARTED)){
            if(err = esp_wifi_stop() != ERR_OK){
                Serial.println("Fehler bei esp_wifi_stop");
                return err;
            }
        }
        if(!client.netif) client.netif = esp_netif_create_default_wifi_sta();
        if(!client.netif){
            Serial.println("Grrr Fehler bei esp_netif_create_default_wifi_sta");
            return ESP_FAIL;
        };
        wifi_config_t wifiConfig = {};
        memcpy(wifiConfig.sta.ssid, ssid, strlen(ssid)+1);
        memcpy(wifiConfig.sta.password, password, strlen(password)+1);
        wifiConfig.sta.scan_method = WIFI_FAST_SCAN;
        if(err = esp_wifi_set_mode(WIFI_MODE_STA) != ERR_OK){
            Serial.println("Fehler bei esp_wifi_set_mode");
            return err;
        }
        if(err = esp_wifi_set_config(WIFI_IF_STA, &wifiConfig) != ERR_OK){
            Serial.println("Fehler bei esp_wifi_set_config");
            return err;
        }
        if(err = esp_wifi_start() != ERR_OK){
            Serial.println("Fehler bei esp_wifi_start");
            return err;
        }
        while(!getFlag(WIFISTARTED));
        return ERR_OK;
    }

    static uint8_t supportedRates[] = {2, 4, 11, 22};
    static uint8_t supportedRatesExt[] = {12, 18, 24, 36, 48, 72, 96, 108};

    uint16_t addTagToPacket(uint8_t* packet, uint8_t* buffer, uint8_t type, uint8_t length)noexcept{
        packet[0] = type;
        packet[1] = length;
        for(uint16_t i=0; i < length; ++i){
            packet[i+2] = buffer[i];
        }
        return length+2;
    }

    //Länge der SSID OHNE Nullterminierung!
    esp_err_t sendProbeRequest(const char* ssid, uint8_t ssidLength){
        uint8_t buffer[256]{0};             //TODO könnte zu groß/klein sein
        buffer[0] = 0x40;                   //PaketID
        memset(&buffer[4], 0xFF, 6);        //Zielmacadresse
        memcpy(&buffer[10], client.mac, 6); //Sendermacadresse
        memset(&buffer[16], 0xFF, 6);       //BSSID
        uint16_t offset = 0;
        offset += addTagToPacket(&buffer[24], (uint8_t*)ssid, 0, ssidLength);
        offset += addTagToPacket(&buffer[24+offset], supportedRates, 1, sizeof(supportedRates));
        offset += addTagToPacket(&buffer[24+offset], supportedRatesExt, 50, sizeof(supportedRatesExt));
        return esp_wifi_80211_tx(WIFI_IF_STA, buffer, 24+offset, true);
    }

    //Blockend und disconnected die aktive Verbindung, also muss nach allen Scans Wifi::reconnect() aufgerufen werden
    //TODO sollte die BSSID nutzen, da die SSID ja nur für das Netzwerk gilt und nicht für den spezifischen Router
    //TODO man könnte ja alle Probe-Requests auf einmal senden, auf Antworten im Timeoutfenster warten und dann die entsprechend nicht empfangenen neu senden
    //TODO disconnect könnte man verhindern mit einem zweiten esp32
    esp_err_t scanForNetwork(NetworkData& data, int8_t retries = 3, uint32_t timeoutMillis = 100){
        esp_err_t err;
        if(getFlag(WIFICONNECTED))
            if(err = esp_wifi_disconnect() != ERR_OK) return err;
        scanData.validData = false;
        scanData.rssi = 0;
        scanData.ssid = data.ssid;
        scanData.ssidLength = strlen(data.ssid);
        scanData.channel = 0;
        if(data.channel == 0){
            uint8_t retriesTmp = retries;
            for(uint8_t i=1; i <= 13; ++i){     //Teste alle Channel durch
                retries = retriesTmp;
                if(err = esp_wifi_set_channel(i, WIFI_SECOND_CHAN_NONE) != ERR_OK) return err;
                while(retries >= 0){
                    retries -= 1;
                    unsigned long startTime = millis();
                    if(err = sendProbeRequest(data.ssid, strlen(data.ssid)) != ERR_OK) return err;
                    while(!scanData.validData){
                        if(millis() - startTime >= timeoutMillis) break;
                    }
                    if(scanData.validData) goto scanEnd;
                }
            }
        }else{
            if(err = esp_wifi_set_channel(data.channel, WIFI_SECOND_CHAN_NONE) != ERR_OK) return err;
            while(retries >= 0){
                retries -= 1;
                unsigned long startTime = millis();
                if(err = sendProbeRequest(data.ssid, strlen(data.ssid)) != ERR_OK) return err;
                while(!scanData.validData){
                    if(millis() - startTime >= timeoutMillis) break;
                }
                if(scanData.validData) goto scanEnd;
            }
        }
        scanEnd:
        data.channel = scanData.channel;
        data.rssi = scanData.rssi;
        return ERR_OK;
    }

    enum STATISTICMETHOD{
        MEDIAN, AVERAGE, HIGHESTOCCURANCE
    };

    //Blockierend und disconnected die aktive Verbindung, also muss nach allen Scans Wifi::reconnect() aufgerufen werden
    //Ruft scanForNetwork einfach samples oft auf, wartet pauseMillis zwischen Messungen und speichert bei Erfolg den Wert von STATISTICMETHOD auf alle Messungen in der NetworkData
    //(Ein Scan dauert OHNE Probleme ca. 5ms, daher sind pauseMillis Werte unter ca. 5ms nicht möglich)
    esp_err_t scanForNetworkAvg(NetworkData& data, uint16_t samples, STATISTICMETHOD method, uint8_t pauseMillis = 10, int8_t retries = 3, uint32_t timeoutMillis = 100){
        esp_err_t err;
        int8_t buffer[samples];
        for(uint16_t i=0; i < samples; ++i){
            unsigned long startTime = millis();
            uint8_t sleepTime = pauseMillis;
            if((err = scanForNetwork(data, retries, timeoutMillis)) != ERR_OK) return err;
            unsigned long timediff = millis() - startTime;
            buffer[i] = data.rssi;
            if(data.rssi == 0) return ERR_OK;   //TODO vllt sollte man zumindest beim Scan hier einen Fehler melden, da 0 Werte ja ungewünscht sind
            timediff <= pauseMillis ? sleepTime -= timediff : sleepTime = 0;
            delay(sleepTime);
        }
        switch(method){
            case MEDIAN:{   //Bubblesort sollte genügen
                for(uint16_t i=0; i < samples; ++i){
                    for(uint16_t j=0; j < samples; ++j){
                        if(i==j) continue;
                        if(buffer[i] > buffer[j]){
                            int8_t tmp = buffer[i];
                            buffer[i] = buffer[j];
                            buffer[j] = tmp;
                        }
                    }
                }
                data.rssi = buffer[samples/2];
                break;
            }
            case AVERAGE:{
                int32_t sum = 0;
                for(uint16_t i=0; i < samples; ++i){
                    sum += buffer[i];
                }
                data.rssi = sum/samples;
                break;
            }
            case HIGHESTOCCURANCE:{
                //TODO Annahme min. db = -90, max. db = -20
                #define MINDB 20
                #define MAXDB 90
                uint16_t counter[MAXDB-MINDB]{0};
                for(uint16_t i=0; i < samples; ++i){
                    counter[abs(buffer[i])] += 1;
                }
                uint8_t idx = 0;
                uint16_t count = counter[0];
                for(uint8_t i=1; i < MAXDB-MINDB; ++i){
                    if(counter[i] > count){
                        count = counter[i];
                        idx = i;
                    }
                }
                data.rssi = -(idx+MINDB);
                break;
            }
            default:{
                Serial.println("Statistische Methode nicht gefunden...");
                return ESP_FAIL;
            }
        }
        return ERR_OK;
    }

    struct UDPServer{
        int socket = -1;                //Socketdeskriptor
        sockaddr_in serverAddr = {};    //Adresse des Servers
        sockaddr_in receiver = {};      //Adresse des Empfängers
        uint8_t recvBuffer[64];
        uint8_t sendBuffer[16];
    };

    esp_err_t createUDPServer(UDPServer& server, const char* ip, uint16_t port, uint32_t timeoutMillis=10){
        server.socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if(server.socket == -1) return ESP_FAIL;
        server.receiver.sin_family = AF_INET;
        server.receiver.sin_addr.s_addr = inet_addr(ip);
        server.receiver.sin_port = htons(port);

        server.serverAddr.sin_family = AF_INET;
        server.serverAddr.sin_port = htons(port);       //TODO muss nicht
        server.serverAddr.sin_addr.s_addr = INADDR_ANY;

        timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = timeoutMillis*1000;
        if(setsockopt(server.socket, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout)) == -1) return ESP_FAIL;

        if(bind(server.socket, (sockaddr*)&server.serverAddr, sizeof(server.serverAddr)) == -1) return ESP_FAIL;
        return ERR_OK;
    }

    void changeUDPServerDestination(UDPServer& server, in_addr ip, uint16_t port){
        server.receiver.sin_addr = ip;
        server.receiver.sin_port = htons(port);
    }

    void changeUDPServerDestination(UDPServer& server, const char* ip, uint16_t port){
        server.receiver.sin_addr.s_addr = inet_addr(ip);
        server.receiver.sin_port = htons(port);
    }

    void changeUDPServerDestination(UDPServer& server, uint32_t ip, uint16_t port){
        server.receiver.sin_addr.s_addr = ip;
        server.receiver.sin_port = htons(port);
    }

    //Nicht blockend, return > 0 falls Daten vorhanden sind
    int recvData(UDPServer& server, sockaddr_in* transmitter = nullptr){
        socklen_t size = sizeof(sockaddr_in);
        return recvfrom(server.socket, server.recvBuffer, sizeof(server.recvBuffer), 0, (sockaddr*)transmitter, &size);
    }

    //Blockend, sendet die Daten die im server.sendBuffer stehen
    int sendData(UDPServer& server, size_t length){return sendto(server.socket, server.sendBuffer, length, 0, (sockaddr*)&server.receiver, sizeof(server.receiver));}

    //Blockend
    int sendMessagecode(UDPServer& server, MESSAGECODES code, NetworkData* data, uint8_t dataCount){
        if(server.socket == -1) return -1;
        size_t sendBufferSize = 0;
        server.sendBuffer[0] = code;
        switch(code){
            case ACK:{
                sendBufferSize = 1;
                break;
            }
            case SEND_POSITION_X:{
                sendBufferSize = 1;
                break;
            }
            case SEND_POSITION_Y:{
                sendBufferSize = 1;
                break;
            }
            case SEND_SIGNALSTRENGTH:{
                if(data == nullptr) return -1;
                for(uint8_t i=0; i < dataCount; ++i){
                    server.sendBuffer[i+1] = data[i].rssi;
                }
                sendBufferSize = dataCount+1;
                break;
            }
            default: return -1;
        }
        return sendto(server.socket, server.sendBuffer, sendBufferSize, 0, (sockaddr*)&server.receiver, sizeof(server.receiver));
    }
}
