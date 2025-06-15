#include "i2s.hpp"
#include "fft.hpp"
#include "webserver.hpp"
#include "now.hpp"
#include "bt.hpp"
#include "esp_bt.h"
#include "WiFi.h"

void setup() {
    // esp_bt_controller_mem_release(ESP_BT_MODE_BLE);
    // WiFi.mode(WIFI_STA);
    Serial.begin(115200);
    Serial.println("START");
    wifi_init();
    // delay(2000);
    bt_init();
    fs_init();
    now_init();
    webserver_init();
    // fft_task_init();
    // i2s_init();

}

void loop (){

}