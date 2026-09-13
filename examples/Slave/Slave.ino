/*
 * CommutateI2C — пример SLAVE
 * Прошейте этим скетчем вторую и третью Nano.
 * Для SLAVE #2 поменяйте SLAVE_ADDRESS на 9 и PEER_ADDRESS на 8.
 */
#include <CommutateI2C.h>

#define SLAVE_ADDRESS   8
#define PEER_ADDRESS    9

CommutateI2C bus(CommutateI2C::ROLE_SLAVE, SLAVE_ADDRESS, PEER_ADDRESS);

void onMsg(uint8_t src, const uint8_t *data, uint8_t len) {
  Serial.print(F("RX from 0x"));
  if (src < 16) Serial.print('0');
  Serial.print(src, HEX);
  Serial.print(F(": "));
  for (uint8_t i = 0; i < len; i++) Serial.write(data[i]);
  Serial.println();
}

void onDelivered(bool ok) {
  Serial.println(ok ? F("-> ACKed") : F("-> NACKed"));
}

static char cliBuf[16];
static uint8_t cliLen = 0;

static void handleCli() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r' || c == '\n') {
      cliBuf[cliLen] = 0;
      if (cliLen == 1) {
        switch (cliBuf[0]) {
          case 's': bus.printStats(Serial); break;
          case 'r': bus.resetStats(); Serial.println(F("reset")); break;
          case 'h':
            Serial.println(F("s=stats r=reset h=help"));
            break;
        }
      }
      cliLen = 0;
    } else if (cliLen < sizeof(cliBuf) - 1) {
      cliBuf[cliLen++] = c;
    }
  }
}

void setup() {
  Serial.begin(9600);
  Serial.print(F("\n=== CommutateI2C — SLAVE 0x"));
  if (SLAVE_ADDRESS < 16) Serial.print('0');
  Serial.print(SLAVE_ADDRESS, HEX);
  Serial.print(F(" -> peer 0x"));
  if (PEER_ADDRESS < 16) Serial.print('0');
  Serial.println(PEER_ADDRESS, HEX);

  bus.onMessage(onMsg);
  bus.onDelivered(onDelivered);
  bus.setAutosendInterval(3000);
  bus.enableAutosend(true);

  bus.begin();
  Serial.println(F("ready."));
}

void loop() {
  bus.update();
  handleCli();

  /* Пример: ручная отправка по событию.
   * Раскомментируйте, если хотите слать по кнопке/датчику. */
  // if (digitalRead(2) == LOW) {
  //   bus.sendToPeer("manual ping");
  //   delay(300);
  // }
}