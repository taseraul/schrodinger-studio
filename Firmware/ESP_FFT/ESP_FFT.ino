#include "webserver.hpp"
#include "i2s.hpp"
#include "now.hpp"
#include "fft.hpp"
#include "colors.hpp"

void setup() {
  Serial.begin(115200);

  i2s_init();
  initFS();

  wifi_init();
  now_init();
  webserver_init();

  Serial.println("START");
}


void loop() {

  i2s_read_samples();

  process_fft();

  process_colors();

  send_light_data();
}
