#pragma once

#include <Arduino.h>
#include <memory.h>
#include "esp_wifi.h"
#include "lwip/sockets.h"
#include <vector>

namespace Wifi{

    enum WIFIFLAGS{
        WIFICONNECTED = 1
    };

    struct NetworkData{
        const char* ssid;
        const char* password;
        int8_t rssi;
        uint8_t channel = 0;
        uint8_t bssid[6]{0};
    };

    enum MESSAGECODES{
        SEND_POSITION_X,
        SEND_POSITION_Y,
        SEND_SIGNALSTRENGTH
    };

    struct WifiStation{
        esp_netif_t* netif = nullptr;
        uint8_t flags = 0;
        esp_netif_ip_info_t ipInfo = {};
    }; static WifiStation client;

    void setFlag(WIFIFLAGS flag){client.flags |= flag;}
    void resetFlag(WIFIFLAGS flag){client.flags &= flag;}
    bool getFlag(WIFIFLAGS flag){return (client.flags & flag);}

    void eventHandler(void* event_handler_arg, esp_event_base_t event_base, int32_t event_id, void* event_data){
        if(event_base == WIFI_EVENT){
            switch(event_id){
                case WIFI_EVENT_STA_START:{
                    esp_wifi_connect();
                    break;
                }
                case WIFI_EVENT_STA_CONNECTED:{
                    setFlag(WIFICONNECTED);
                    break;
                }
                case WIFI_EVENT_STA_DISCONNECTED:{
                    resetFlag(WIFICONNECTED);
                    esp_wifi_connect();
                    break;
                }
            }
        }else if(event_base == IP_EVENT){
            switch(event_id){
                case IP_EVENT_STA_GOT_IP:{
                    ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
                    client.ipInfo = event->ip_info;
                    Serial.println(inet_ntoa(client.ipInfo.ip.addr));
                    break;
                }
            }
        }
    }

    //Nicht blockend, muss nur einmalig vor allem anderen aufgerufen werden
    esp_err_t init(){
        esp_err_t err;
        if(err = esp_netif_init() != ERR_OK) return err;
        if(err = esp_event_loop_create_default() != ERR_OK) return err;

        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        if(err = esp_wifi_init(&cfg) != ERR_OK) return err;

        if(err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, eventHandler, NULL, NULL) != ERR_OK) return err;
        if(err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, eventHandler, NULL, NULL) != ERR_OK) return err;

        return ERR_OK;
    }

    //Blockend
    esp_err_t connectToNetwork(const char* ssid, const char* password, unsigned long timeoutMillis = 5000){
        unsigned long startTime = millis();
        if(getFlag(WIFICONNECTED)) esp_wifi_disconnect();
        if(!client.netif) client.netif = esp_netif_create_default_wifi_sta();
        if(!client.netif){
            Serial.println("Grrr Fehler bei esp_netif_create_default_wifi_sta");
            return ESP_FAIL;
        };
        wifi_config_t wifiConfig = {};
        memcpy(wifiConfig.sta.ssid, ssid, strlen(ssid)+1);
        memcpy(wifiConfig.sta.password, password, strlen(password)+1);
        wifiConfig.sta.scan_method = WIFI_FAST_SCAN;
        esp_err_t err;
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
        while(!getFlag(WIFICONNECTED)){
            if(millis() - startTime >= timeoutMillis){
                Serial.println("Timeout bei der Verbindung");
                return ESP_FAIL;
            }
        }
        return ERR_OK;
    }

    //Blockend
    //TODO sollte die BSSID nutzen, da die SSID ja nur für das Netzwerk gilt und nicht für den spezifischen Router
    esp_err_t scanForNetwork(NetworkData& data, int8_t retries = 3, uint32_t max = 0, uint32_t min = 0){
        esp_err_t err;
        if(data.channel == 0) retries = 0;
        if(!getFlag(WIFICONNECTED)) return ESP_FAIL;
        wifi_scan_config_t scanConfig = {};
        scanConfig.ssid = (uint8_t*)data.ssid;
        scanConfig.channel = data.channel;
        scanConfig.show_hidden = false;
        scanConfig.bssid = nullptr;
        scanConfig.scan_type = WIFI_SCAN_TYPE_ACTIVE;
        scanConfig.scan_time.active.min = min;
        scanConfig.scan_time.active.max = max;
        uint16_t count;
        do{
            retries -= 1;
            if(err = esp_wifi_scan_start(&scanConfig, true) != ERR_OK) return err;
            if(err = esp_wifi_scan_get_ap_num(&count) != ERR_OK) return err;
            if(count >= 1) break;
        }while(retries >= 0);
        if(count < 1){
            data.channel = 0;
            return ESP_FAIL;
        }
        wifi_ap_record_t records[count];
        if(err = esp_wifi_scan_get_ap_records(&count, records) != ERR_OK) return err;
        data.rssi = records[0].rssi;
        data.channel = records[0].primary;
        for(uint8_t i=0; i < 6; ++i) data.bssid[i] = records[0].bssid[i];
        return ERR_OK;
    }

    enum STATISICMETHOD{
        MEDIAN, AVERAGE, HIGHESTOCCURANCE
    };

    //Blockierend
    //Ruft scanForNetwork einfach samples oft auf und speichert bei Erfolg den Medianwert in der NetworkData
    //TODO STATISICMETHOD implementieren
    esp_err_t scanForNetworkAvg(NetworkData& data, uint8_t samples, int8_t retries = 3, uint32_t max = 0, uint32_t min = 0){
        esp_err_t err;
        int8_t buffer[samples];
        for(uint16_t i=0; i < samples; ++i){
            if((err = scanForNetwork(data, retries, max, min)) != ERR_OK) return err;
            buffer[i] = data.rssi;
        }
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
        return ERR_OK;
    }

    struct UDPClient{
        int socket = -1;
        sockaddr_in receiver = {};
    };

    esp_err_t createUDPClient(UDPClient& client, const char* ip, uint16_t port){
        client.socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if(client.socket == -1) return ESP_FAIL;
        client.receiver.sin_family = AF_INET;
        client.receiver.sin_addr.s_addr = inet_addr(ip);
        client.receiver.sin_port = htons(port);
        return ERR_OK;
    }

    static uint8_t sendBuffer[16];
    static size_t sendBufferSize = 0;
    //Blockend
    int sendData(UDPClient& client, MESSAGECODES code, NetworkData* data, uint8_t dataCount){
        if(client.socket == -1) return -1;
        switch(code){
            case SEND_POSITION_X:{
                sendBuffer[0] = SEND_POSITION_X;
                sendBufferSize = 1;
                break;
            }
            case SEND_POSITION_Y:{
                sendBuffer[0] = SEND_POSITION_Y;
                sendBufferSize = 1;
                break;
            }
            case SEND_SIGNALSTRENGTH:{
                if(data == nullptr) return -1;
                sendBuffer[0] = SEND_SIGNALSTRENGTH;
                for(uint8_t i=0; i < dataCount; ++i){
                    sendBuffer[i+1] = data[i].rssi;
                }
                sendBufferSize = dataCount+1;
                break;
            }
            default: return -1;
        }
        return sendto(client.socket, sendBuffer, sendBufferSize, 0, (sockaddr*)&client.receiver, sizeof(client.receiver));
    }
}
