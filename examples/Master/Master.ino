/*
 * CommutateI2C — пример MASTER
 * Настройки шины, LCD и таймингов меняются в src/CommutateI2C_Config.h
 */
#include <Wire.h>
#include <CommutateI2C.h>

#if COMMUTATE_I2C_USE_LCD
  #include <LiquidCrystal_I2C.h>
  LiquidCrystal_I2C lcd(COMMUTATE_I2C_LCD_ADDR,
                        COMMUTATE_I2C_LCD_COLS,
                        COMMUTATE_I2C_LCD_ROWS);
#endif

CommutateI2C bus(CommutateI2C::ROLE_MASTER);

/* Простой Serial CLI */
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
          case 'l':
            Serial.print(F("slaves: "));
            for (uint8_t i = 0; i < bus.slaveCount(); i++) {
              Serial.print(F("0x"));
              if (bus.slaveAt(i) < 16) Serial.print('0');
              Serial.print(bus.slaveAt(i), HEX);
              Serial.print(' ');
            }
            Serial.println();
            break;
          case 'h':
            Serial.println(F("s=stats r=reset l=list h=help"));
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
  Serial.println(F("\n=== CommutateI2C — MASTER ==="));

#if COMMUTATE_I2C_USE_LCD
  lcd.begin();
  lcd.backlight();
  lcd.setCursor(0, 0); lcd.print(F("CommutateI2C"));
  lcd.setCursor(0, 1); lcd.print(F("scanning..."));
  bus.attachLCD(&lcd);
  delay(500);
#endif

  bus.begin();
  bus.setScanInterval(5000);
  Serial.println(F("ready. press h for help"));
}

void loop() {
  bus.update();
  handleCli();
}