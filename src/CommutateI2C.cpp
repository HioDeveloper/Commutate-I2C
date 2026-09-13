#include "CommutateI2C.h"
#include <string.h>
#include <avr/wdt.h>

/* ============================================================================
 *  Протокол
 * ==========================================================================*/
#define CMD_POLL       0x01
#define CMD_DELIVER    0x10
#define CMD_ACK        0x20

#define ACK_OK         0x01
#define ACK_NACK       0x00

/* Singleton для ISR */
CommutateI2C *CommutateI2C::_instance = nullptr;

/* ============================================================================
 *  Вспомогательные макросы
 * ==========================================================================*/
#if COMMUTATE_I2C_USE_LED
  #define LED_ON()      digitalWrite(COMMUTATE_I2C_LED_PIN, HIGH)
  #define LED_OFF()     digitalWrite(COMMUTATE_I2C_LED_PIN, LOW)
  #define LED_TOGGLE()  digitalWrite(COMMUTATE_I2C_LED_PIN, !digitalRead(COMMUTATE_I2C_LED_PIN))
#else
  #define LED_ON()
  #define LED_OFF()
  #define LED_TOGGLE()
#endif

#if COMMUTATE_I2C_USE_SERIAL
  #define DLOG(x)    Serial.print(x)
  #define DLOGLN(x)  Serial.println(x)
  #define DLOGF(x)   Serial.print(F(x))
  #define DLOGFLN(x) Serial.println(F(x))
#else
  #define DLOG(x)
  #define DLOGLN(x)
  #define DLOGF(x)
  #define DLOGFLN(x)
#endif

#define WDT_FEED()  do { wdt_reset(); } while (0)

/* ============================================================================
 *  CRC-8/ATM
 * ==========================================================================*/
uint8_t CommutateI2C::_crc8(const uint8_t *data, uint8_t len) {
  uint8_t crc = 0;
  while (len--) {
    crc ^= *data++;
    for (uint8_t i = 0; i < 8; i++) {
      if (crc & 0x80) crc = (crc << 1) ^ 0x07;
      else            crc <<= 1;
    }
  }
  return crc;
}

/* ============================================================================
 *  Утилиты вывода (без snprintf)
 * ==========================================================================*/
static char *u32toa(uint32_t v, char *dst) {
  char tmp[11];
  uint8_t i = 0;
  if (v == 0) { *dst++ = '0'; return dst; }
  while (v > 0) { tmp[i++] = '0' + (uint8_t)(v % 10); v /= 10; }
  while (i--) *dst++ = tmp[i];
  return dst;
}

static char *u8hex(uint8_t v, char *dst) {
  const char *hc = "0123456789ABCDEF";
  *dst++ = hc[(v >> 4) & 0x0F];
  *dst++ = hc[v & 0x0F];
  return dst;
}

static void padLine(char *line, uint8_t cols) {
  for (uint8_t i = 0; i < cols; i++) line[i] = ' ';
  line[cols] = 0;
}

/* ============================================================================
 *  Конструктор
 * ==========================================================================*/
CommutateI2C::CommutateI2C(Role role, uint8_t address, uint8_t peer)
  : _role(role), _address(address), _peer(peer),
    _cfgScan(COMMUTATE_I2C_SCAN_MS),
    _cfgPollDelay(COMMUTATE_I2C_POLL_DELAY),
    _cfgAutosend(COMMUTATE_I2C_AUTOSEND_MS),
    _cfgRetries(COMMUTATE_I2C_RETRIES),
    _cfgAutosendOn(true),
    _onMsg(nullptr), _onDeliver(nullptr),
    _slaveCount(0), _lastScan(0),
    _busErrStreak(0), _lastErr(0),
    _lastSrc(0), _lastDst(0), _lastOk(false),
    _lcdDirty(true), _lcdScreen(0), _lastScreenSw(0),
    _qHead(0), _qTail(0), _qCount(0),
    _inSrc(0), _inLen(0), _inPending(false),
    _lastAck(-1), _lastCmd(0),
    _lastAutosend(0), _autosendCounter(0)
#if COMMUTATE_I2C_USE_LCD
    , _lcd(nullptr)
#endif
{
  memset(&_stats, 0, sizeof(_stats));
  memset(_slaves, 0, sizeof(_slaves));
  memset(_lastMsg, 0, sizeof(_lastMsg));
}

/* ============================================================================
 *  begin()
 * ==========================================================================*/
bool CommutateI2C::begin() {
#if COMMUTATE_I2C_USE_LED
  pinMode(COMMUTATE_I2C_LED_PIN, OUTPUT);
  LED_OFF();
#endif

#if COMMUTATE_I2C_USE_WATCHDOG
  cli();
  wdt_reset();
  MCUSR &= ~(1 << WDRF);
  WDTCSR |= (1 << WDCE) | (1 << WDE);
  WDTCSR = (1 << WDP0) | (1 << WDP1) | (1 << WDP2);  /* 2 s */
  sei();
#endif

  if (_role == ROLE_MASTER) {
    Wire.begin();
    /* setClock() не вызываем — на AVR значения < 100 кГц ломают TWBR */
    _lastScan = millis();
    _lastScreenSw = millis();
    _scanBus();
    DLOGFLN("CommutateI2C: master ready");
  } else {
    Wire.begin(_address);
    Wire.onReceive(_isrReceive);
    Wire.onRequest(_isrRequest);
    _instance = this;
    DLOGF("CommutateI2C: slave 0x");
    if (_address < 16) DLOG('0');
    DLOGLN(_address, HEX);
  }
  return true;
}

/* ============================================================================
 *  update()
 * ==========================================================================*/
void CommutateI2C::update() {
  WDT_FEED();

  if (_role == ROLE_MASTER) {
    if (millis() - _lastScan > _cfgScan) {
      _scanBus();
      _lastScan = millis();
    }
    for (uint8_t i = 0; i < _slaveCount; i++) {
      _pollSlave(_slaves[i]);
      WDT_FEED();
    }
#if COMMUTATE_I2C_USE_LCD
    if (millis() - _lastScreenSw > COMMUTATE_I2C_LCD_SWAP_MS) {
      _lcdScreen = !_lcdScreen;
      _lastScreenSw = millis();
      _lcdDirty = true;
    }
    if (_lcdDirty) _updateLCD();
#endif
  } else {
    /* --- Ведомый --- */
    if (_inPending) {
      _inPending = false;
      _stats.recv++;
      LED_TOGGLE();
      if (_onMsg) _onMsg(_inSrc, (const uint8_t*)_inData, _inLen);
    }
    if (_lastAck >= 0) {
      bool ok = (_lastAck == 1);
      if (_onDeliver) _onDeliver(ok);
      _lastAck = -1;
    }
    _autoSendTick();
  }
}

/* ============================================================================
 *  Slave: send()
 * ==========================================================================*/
bool CommutateI2C::send(uint8_t dst, const uint8_t *data, uint8_t len) {
  if (_role != ROLE_SLAVE) return false;
  if (len == 0) return false;
  if (len > COMMUTATE_I2C_MAX_MSG) len = COMMUTATE_I2C_MAX_MSG;
  return _queuePush(dst, data, len);
}

bool CommutateI2C::send(uint8_t dst, const char *str) {
  return send(dst, (const uint8_t*)str, (uint8_t)strlen(str));
}

bool CommutateI2C::sendToPeer(const char *str) {
  return send(_peer, str);
}

/* ============================================================================
 *  Slave: очередь
 * ==========================================================================*/
bool CommutateI2C::_queuePush(uint8_t dst, const uint8_t *data, uint8_t len) {
  if (_qCount >= COMMUTATE_I2C_QUEUE_SIZE) return false;
  uint8_t idx = _qHead;
  _queue[idx].dst = dst;
  _queue[idx].len = len;
  memcpy(_queue[idx].data, data, len);
  _queue[idx].tries = 0;
  _qHead = (_qHead + 1) % COMMUTATE_I2C_QUEUE_SIZE;
  _qCount++;
  return true;
}

bool CommutateI2C::_queuePeek(Msg **out) {
  if (_qCount == 0) return false;
  *out = &_queue[_qTail];
  return true;
}

void CommutateI2C::_queuePop() {
  if (_qCount == 0) return;
  _qTail = (_qTail + 1) % COMMUTATE_I2C_QUEUE_SIZE;
  _qCount--;
}

/* ============================================================================
 *  Slave: autosend
 * ==========================================================================*/
void CommutateI2C::_autoSendTick() {
  if (!_cfgAutosendOn) return;
  if (_qCount >= COMMUTATE_I2C_QUEUE_SIZE) return;
  if (millis() - _lastAutosend < _cfgAutosend) return;
  _lastAutosend = millis();

  char buf[COMMUTATE_I2C_MAX_MSG + 1];
  memcpy(buf, "msg #", 5);
  char *end = u32toa(++_autosendCounter, buf + 5);
  uint8_t len = (uint8_t)(end - buf);

  if (_queuePush(_peer, (const uint8_t*)buf, len)) {
    _stats.sent++;
    LED_TOGGLE();
  }
}

/* ============================================================================
 *  ISR делегаты
 * ==========================================================================*/
void CommutateI2C::_isrReceive(int n) {
  if (_instance) _instance->_handleReceive(n);
}
void CommutateI2C::_isrRequest() {
  if (_instance) _instance->_handleRequest();
}

/* ============================================================================
 *  ISR: приём от мастера
 * ==========================================================================*/
void CommutateI2C::_handleReceive(int howMany) {
  if (howMany < 1) return;
  uint8_t cmd = Wire.read();

  if (cmd == CMD_POLL) {
    _lastCmd = CMD_POLL;

  } else if (cmd == CMD_DELIVER) {
    if (howMany < 4) return;
    uint8_t buf[COMMUTATE_I2C_MAX_MSG + 3];
    uint8_t idx = 0;
    while (Wire.available() && idx < sizeof(buf)) buf[idx++] = Wire.read();
    if (idx < 3) return;

    uint8_t crc = _crc8(buf, idx - 1);
    if (crc != buf[idx - 1]) {
      _stats.crcErr++;
      return;
    }
    uint8_t src = buf[0];
    uint8_t len = buf[1];
    if (len > COMMUTATE_I2C_MAX_MSG) len = COMMUTATE_I2C_MAX_MSG;
    _inSrc = src;
    _inLen = len;
    for (uint8_t i = 0; i < len; i++) _inData[i] = buf[2 + i];
    _inPending = true;
    _lastCmd = CMD_DELIVER;

  } else if (cmd == CMD_ACK) {
    if (howMany < 2) return;
    uint8_t st = Wire.read();
    if (st == ACK_OK) {
      _lastAck = 1;
      _queuePop();
    } else {
      _lastAck = 0;
      _stats.nack++;
    }
    _lastCmd = CMD_ACK;
  }
}

/* ============================================================================
 *  ISR: ответ мастеру
 * ==========================================================================*/
void CommutateI2C::_handleRequest() {
  uint8_t c = _lastCmd;
  _lastCmd = 0;

  if (c == CMD_POLL) {
    Msg *m = nullptr;
    if (_queuePeek(&m)) {
      uint8_t buf[2 + COMMUTATE_I2C_MAX_MSG];
      buf[0] = m->dst;
      buf[1] = m->len;
      memcpy(buf + 2, m->data, COMMUTATE_I2C_MAX_MSG);
      for (uint8_t i = m->len; i < COMMUTATE_I2C_MAX_MSG; i++) buf[2 + i] = 0;
      uint8_t crc = _crc8(buf, 2 + COMMUTATE_I2C_MAX_MSG);
      for (uint8_t i = 0; i < 2 + COMMUTATE_I2C_MAX_MSG; i++) Wire.write(buf[i]);
      Wire.write(crc);
    } else {
      uint8_t buf[2 + COMMUTATE_I2C_MAX_MSG] = {0};
      uint8_t crc = _crc8(buf, 2 + COMMUTATE_I2C_MAX_MSG);
      for (uint8_t i = 0; i < 2 + COMMUTATE_I2C_MAX_MSG; i++) Wire.write(buf[i]);
      Wire.write(crc);
    }
  } else {
    Wire.write((uint8_t)0x00);
  }
}

/* ============================================================================
 *  Master: сканирование
 * ==========================================================================*/
void CommutateI2C::_scanBus() {
  uint8_t found = 0;
  uint8_t newList[COMMUTATE_I2C_MAX_SLAVES];

  for (uint8_t addr = 1; addr < 127; addr++) {
#if COMMUTATE_I2C_USE_LCD
    if (addr == COMMUTATE_I2C_LCD_ADDR) continue;
#endif
    WDT_FEED();
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0 && found < COMMUTATE_I2C_MAX_SLAVES) {
      newList[found++] = addr;
    }
  }

  bool changed = (found != _slaveCount);
  if (!changed && found) changed = (memcmp(_slaves, newList, found) != 0);
  if (!changed) return;

  _slaveCount = found;
  memcpy(_slaves, newList, found);
  _lcdDirty = true;

#if COMMUTATE_I2C_USE_SERIAL
  Serial.print(F("Slaves:"));
  for (uint8_t i = 0; i < _slaveCount; i++) {
    Serial.print(F(" 0x"));
    if (_slaves[i] < 16) Serial.print('0');
    Serial.print(_slaves[i], HEX);
  }
  Serial.println();
#endif
}

/* ============================================================================
 *  Master: опрос одного
 * ==========================================================================*/
void CommutateI2C::_pollSlave(uint8_t addr) {
  Wire.beginTransmission(addr);
  Wire.write(CMD_POLL);
  if (Wire.endTransmission() != 0) return;

  delay(_cfgPollDelay);

  uint8_t want = 2 + COMMUTATE_I2C_MAX_MSG + 1;
  if (Wire.requestFrom((int)addr, (int)want) < 3) return;

  uint8_t buf[2 + COMMUTATE_I2C_MAX_MSG + 1];
  uint8_t idx = 0;
  while (Wire.available() && idx < sizeof(buf)) buf[idx++] = Wire.read();
  if (idx < 3) return;

  uint8_t crc = _crc8(buf, idx - 1);
  if (crc != buf[idx - 1]) {
    _stats.crcErr++;
    return;
  }
  uint8_t dst = buf[0];
  uint8_t len = buf[1];
  if (dst == 0 || len == 0 || len > COMMUTATE_I2C_MAX_MSG) return;

  uint8_t data[COMMUTATE_I2C_MAX_MSG];
  memcpy(data, buf + 2, len);
  _stats.poll++;

  bool ok = false;
  uint8_t lastErr = 0;
  for (uint8_t attempt = 0; attempt < _cfgRetries; attempt++) {
    ok = _deliver(dst, addr, data, len, &lastErr);
    if (ok) break;
    if (lastErr == 2) break;   /* адресат отсутствует — не повторяем */
    if (attempt + 1 < _cfgRetries) {
      _stats.retry++;
      delay(COMMUTATE_I2C_RETRY_PAUSE);
    }
  }

  if (ok) _stats.ok++; else _stats.fail++;
  _lcdDirty = true;
  _lastSrc = addr;
  _lastDst = dst;
  _lastOk  = ok;
  memcpy(_lastMsg, data, len);
  _lastMsg[len] = 0;

  Wire.beginTransmission(addr);
  Wire.write(CMD_ACK);
  Wire.write(ok ? (uint8_t)ACK_OK : (uint8_t)ACK_NACK);
  Wire.endTransmission();

#if COMMUTATE_I2C_USE_SERIAL
  Serial.print(F("Route 0x"));
  if (addr < 16) Serial.print('0');
  Serial.print(addr, HEX);
  Serial.print(F(" -> 0x"));
  if (dst < 16) Serial.print('0');
  Serial.print(dst, HEX);
  Serial.print(' '); Serial.println(ok ? F("OK") : F("FAIL"));
#endif
}

/* ============================================================================
 *  Master: доставка
 * ==========================================================================*/
bool CommutateI2C::_deliver(uint8_t dst, uint8_t src, uint8_t *data, uint8_t len, uint8_t *errOut) {
  uint8_t packet[COMMUTATE_I2C_MAX_MSG + 5];
  uint8_t p = 0;
  packet[p++] = CMD_DELIVER;
  packet[p++] = src;
  packet[p++] = len;
  memcpy(packet + p, data, len);
  p += len;
  packet[p] = _crc8(packet, p);
  p++;

  Wire.beginTransmission(dst);
  for (uint8_t i = 0; i < p; i++) Wire.write(packet[i]);
  uint8_t e = Wire.endTransmission();
  if (errOut) *errOut = e;
  _lastErr = e;

  if (e != 0) {
    _busErrStreak++;
    if (e == 5) _stats.timeout++;
    if (_busErrStreak >= COMMUTATE_I2C_BUS_ERR_MAX) {
      _stats.busRecovery++;
      _busRecovery();
      _busErrStreak = 0;
    }
    return false;
  }
  _busErrStreak = 0;
  return true;
}

/* ============================================================================
 *  Восстановление шины
 * ==========================================================================*/
void CommutateI2C::_busRecovery() {
  pinMode(A4, INPUT_PULLUP);
  pinMode(A5, OUTPUT);
  digitalWrite(A5, HIGH);
  for (uint8_t i = 0; i < 9; i++) {
    digitalWrite(A5, LOW);  delayMicroseconds(5);
    digitalWrite(A5, HIGH); delayMicroseconds(5);
  }
  pinMode(A4, OUTPUT);
  digitalWrite(A4, LOW);  delayMicroseconds(5);
  digitalWrite(A5, HIGH); delayMicroseconds(5);
  digitalWrite(A4, HIGH); delayMicroseconds(5);
  pinMode(A4, INPUT);
  pinMode(A5, INPUT);
  Wire.begin();
  DLOGFLN("Bus recovery done");
}

/* ============================================================================
 *  Статистика
 * ==========================================================================*/
void CommutateI2C::resetStats() {
  memset(&_stats, 0, sizeof(_stats));
}

void CommutateI2C::printStats(Stream &s) const {
  s.print(F("ok="));       s.print(_stats.ok);
  s.print(F(" fail="));    s.print(_stats.fail);
  s.print(F(" poll="));    s.print(_stats.poll);
  s.print(F(" crc="));     s.print(_stats.crcErr);
  s.print(F(" retry="));   s.print(_stats.retry);
  s.print(F(" timeout=")); s.print(_stats.timeout);
  s.print(F(" bus="));     s.print(_stats.busRecovery);
  s.print(F(" sent="));    s.print(_stats.sent);
  s.print(F(" recv="));    s.print(_stats.recv);
  s.print(F(" nack="));    s.println(_stats.nack);
}

/* ============================================================================
 *  LCD
 * ==========================================================================*/
#if COMMUTATE_I2C_USE_LCD
void CommutateI2C::_updateLCD() {
  if (!_lcd) return;
  _lcdDirty = false;
  char line[COMMUTATE_I2C_LCD_COLS + 1];

  if (_lcdScreen == 0) {
    /* Сводка */
    padLine(line, COMMUTATE_I2C_LCD_COLS);
    uint8_t pos = 0;
    line[pos++] = 'S'; line[pos++] = ':';
    for (uint8_t i = 0; i < _slaveCount && pos + 4 <= COMMUTATE_I2C_LCD_COLS; i++) {
      line[pos++] = ' ';
      u8hex(_slaves[i], line + pos);
      pos += 2;
    }
    _lcd->setCursor(0, 0); _lcd->print(line);

    padLine(line, COMMUTATE_I2C_LCD_COLS);
    pos = 0;
    line[pos++] = 'O'; line[pos++] = 'K'; line[pos++] = ':';
    pos = u32toa(_stats.ok, line + pos) - line;
    line[pos++] = ' ';
    line[pos++] = 'F'; line[pos++] = ':';
    pos = u32toa(_stats.fail, line + pos) - line;
    line[pos] = 0;
    _lcd->setCursor(0, 1); _lcd->print(line);
  } else {
    /* Последнее событие */
    padLine(line, COMMUTATE_I2C_LCD_COLS);
    uint8_t pos = 0;
    line[pos++] = '0'; line[pos++] = 'x';
    u8hex(_lastSrc, line + pos); pos += 2;
    line[pos++] = '>';
    line[pos++] = '0'; line[pos++] = 'x';
    u8hex(_lastDst, line + pos); pos += 2;
    line[pos++] = ' ';
    if (_lastOk) { line[pos++] = 'O'; line[pos++] = 'K'; }
    else         { line[pos++] = 'F'; line[pos++] = 'A'; line[pos++] = 'I'; line[pos++] = 'L'; }
    line[pos] = 0;
    _lcd->setCursor(0, 0); _lcd->print(line);

    _lcd->setCursor(0, 1);
    uint8_t n = strlen(_lastMsg);
    for (uint8_t i = 0; i < COMMUTATE_I2C_LCD_COLS; i++) {
      _lcd->print(i < n ? _lastMsg[i] : ' ');
    }
  }
}
#else
void CommutateI2C::_updateLCD() {}
#endif