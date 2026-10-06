// sim: the Arduino main task. setup() once, then loop() forever - every delay() inside hands the
// browser its turn (asyncify), exactly where the board would let other tasks run.
#include <Arduino.h>
#include <stdlib.h>
void setup(void);
void loop(void);
extern "C" void __sim_oom(void) { Serial.println("[SIM] out of memory"); abort(); }
extern "C" __attribute__((export_name("sim_main"))) void sim_main(void) {
  setup();
  for (;;) { loop(); yield(); }
}
