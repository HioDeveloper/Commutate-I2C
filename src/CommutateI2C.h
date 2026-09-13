#ifndef COMMUTATE_I2C_H
#define COMMUTATE_I2C_H

#include <Arduino.h>
#include <Wire.h>
#include "CommutateI2C_Config.h"

#if COMMUTATE_I2C_USE_LCD
  #include <LiquidCrystal_I2C.h>
#endif

/* ============================================================================
 *  CommutateI2C — публичный API
 * ==========================================================================*/
class CommutateI2C {
public:
  enum Role : uint8_t {
    ROLE_MASTER = 0,
    ROLE_SLAVE  = 1
  };

  struct Stats {
    uint32_t ok;            /* успешных доставок (master) */
    uint32_t fail;          /* неудачных доставок (master) */
    uint32_t poll;          /* всего опросов с непустым пакетом (master) */
    uint32_t crcErr;        /* ошибок CRC (обе роли) */
    uint32_t retry;         /* повторных попыток (master) */
    uint32_t timeout;       /* таймаутов шины (master) */
    uint32_t busRecovery;   /* восстановлений шины (master) */
    uint32_t sent;          /* отправлено (slave) */
    uint32_t recv;          /* принято (slave) */
    uint32_t nack;          /* NACK на наши сообщения (slave) */
  };

  typedef void (*MessageHandler)(uint8_t src, const uint8_t *data, uint8_t len);
  typedef void (*DeliverHandler)(bool ok);

  /* --- Конструктор --- *
   *  role    — ROLE_MASTER или ROLE_SLAVE
   *  address — собственный адрес (для slave)
   *  peer    — кому слать по умолчанию (для slave) */
  CommutateI2C(Role role, uint8_t address = 8, uint8_t peer = 9);

  /* --- Инициализация и цикл --- */
  bool begin();
  void update();  /* вызывать в loop() */

  /* --- Master API --- */
  uint8_t slaveCount() const { return _slaveCount; }
  uint8_t slaveAt(uint8_t i) const { return i < _slaveCount ? _slaves[i] : 0; }

  /* --- Slave API --- */
  bool send(uint8_t dst, const uint8_t *data, uint8_t len);
  bool send(uint8_t dst, const char *str);
  bool sendToPeer(const char *str);

  /* --- Callbacks --- */
  void onMessage(MessageHandler cb)  { _onMsg     = cb; }
  void onDelivered(DeliverHandler cb){ _onDeliver = cb; }

  /* --- Stats --- */
  const Stats& stats() const { return _stats; }
  void resetStats();
  void printStats(Stream &s) const;

  /* --- Настройки времени выполнения --- */
  void setScanInterval(uint16_t ms)     { _cfgScan      = ms; }
  void setPollDelay(uint16_t ms)        { _cfgPollDelay = ms; }
  void setAutosendInterval(uint16_t ms) { _cfgAutosend  = ms; }
  void setMaxRetries(uint8_t n)         { _cfgRetries   = n; }
  void enableAutosend(bool en)          { _cfgAutosendOn = en; }

#if COMMUTATE_I2C_USE_LCD
  void attachLCD(LiquidCrystal_I2C *lcd) { _lcd = lcd; }
#endif

private:
  /* --- Конфигурация --- */
  Role     _role;
  uint8_t  _address;
  uint8_t  _peer;

  uint16_t _cfgScan;
  uint16_t _cfgPollDelay;
  uint16_t _cfgAutosend;
  uint8_t  _cfgRetries;
  bool     _cfgAutosendOn;

  /* --- Статистика --- */
  Stats _stats;

  /* --- Callbacks --- */
  MessageHandler _onMsg;
  DeliverHandler _onDeliver;

  /* --- Master state --- */
  uint8_t  _slaves[COMMUTATE_I2C_MAX_SLAVES];
  uint8_t  _slaveCount;
  uint32_t _lastScan;
  uint8_t  _busErrStreak;
  uint8_t  _lastErr;

  /* --- Последнее событие (LCD) --- */
  uint8_t  _lastSrc;
  uint8_t  _lastDst;
  bool     _lastOk;
  char     _lastMsg[COMMUTATE_I2C_MAX_MSG + 1];
  bool     _lcdDirty;
  uint8_t  _lcdScreen;
  uint32_t _lastScreenSw;

  /* --- Slave state: очередь --- */
  struct Msg {
    uint8_t dst;
    uint8_t len;
    uint8_t data[COMMUTATE_I2C_MAX_MSG];
    uint8_t tries;
  };
  Msg      _queue[COMMUTATE_I2C_QUEUE_SIZE];
  volatile uint8_t _qHead;
  volatile uint8_t _qTail;
  volatile uint8_t _qCount;

  /* --- Slave state: приём --- */
  volatile uint8_t _inSrc;
  volatile uint8_t _inLen;
  volatile uint8_t _inData[COMMUTATE_I2C_MAX_MSG];
  volatile bool    _inPending;
  volatile int8_t  _lastAck;
  volatile uint8_t _lastCmd;

  /* --- Autosend --- */
  uint32_t _lastAutosend;
  uint16_t _autosendCounter;

  /* --- LCD --- */
#if COMMUTATE_I2C_USE_LCD
  LiquidCrystal_I2C *_lcd;
#endif

  /* --- Singleton для ISR --- */
  static CommutateI2C *_instance;

  /* --- ISRs --- */
  static void _isrReceive(int n);
  static void _isrRequest();
  void _handleReceive(int n);
  void _handleRequest();

  /* --- Внутренние --- */
  void     _scanBus();
  void     _pollSlave(uint8_t addr);
  bool     _deliver(uint8_t dst, uint8_t src, uint8_t *data, uint8_t len, uint8_t *errOut);
  void     _busRecovery();
  void     _autoSendTick();
  bool     _queuePush(uint8_t dst, const uint8_t *data, uint8_t len);
  bool     _queuePeek(Msg **out);
  void     _queuePop();
  void     _updateLCD();
  static uint8_t _crc8(const uint8_t *data, uint8_t len);
};

#endif /* COMMUTATE_I2C_H */