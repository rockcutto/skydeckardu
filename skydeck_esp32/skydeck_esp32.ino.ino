/*
  skydeck.ino
  ESP32-S2/S3 Arduino-framework

  Принимает данные с Steam Deck по USB-Serial (Serial),
  упаковывает их в CRSF и шлёт на UART2 → ELRS-TX модуль.
  Два внешних светодиода-индикатора:
    • Зелёный (GPIO4) — связь нормальная (горит постоянно).
    • Красный  (GPIO5) — связь потеряна >1 с (горит постоянно).
*/

#include <Arduino.h>
#include <HardwareSerial.h>

// — UART и протоколы —
#define SERIAL_BAUD       115200   // монитор + Steam Deck
#define CRSF_BAUD         400000   // CRSF TX
#define UART2_RX_PIN      -1       // RX2 не используется
#define UART2_TX_PIN      17       // TX2 → ELRS-TX

#define CRSF_INTERVAL_US  2000     // 500 Hz
#define TIMEOUT_MS        1000     // 1 с без данных → ошибка
#define CHANNEL_COUNT     8        // читаем 8 каналов

// — Внешние LED-индикаторы —
#define LED_OK_PIN   4  // внешняя плата: зелёный
#define LED_ERR_PIN  5  // внешняя плата: красный

// — CRSF-протокол —
#define CRSF_MAX_CHANNEL  16
#define CRSF_PACKET_SIZE  26
#define CRSF_CH_MIN       172
#define CRSF_CH_MID       991
#define CRSF_CH_MAX       1811
#define ADDR_MODULE       0xEE
#define TYPE_CHANNELS     0x16

HardwareSerial Serial2(1);

// — данные каналов и буферы —
int     ch_val[CRSF_MAX_CHANNEL];
uint8_t crsf_pkt[CRSF_PACKET_SIZE];
uint32_t next_crsf_us;
int64_t last_rx_us;
String  in_buf;

// — CRC-8/MAXIM таблица (256 байт) —
static const uint8_t crc8tab[256] = {
  0x00,0xD5,0x7F,0xAA,0xFE,0x2B,0x81,0x54,0x29,0xFC,0x56,0x83,0xD7,0x02,0xA8,0x7D,
  0x52,0x87,0x2D,0xF8,0xAC,0x79,0xD3,0x06,0x7B,0xAE,0x04,0xD1,0x85,0x50,0xFA,0x2F,
  0xA4,0x71,0xDB,0x0E,0x5A,0x8F,0x25,0xF0,0x8D,0x58,0xF2,0x27,0x73,0xA6,0x0C,0xD9,
  0xF6,0x23,0x89,0x5C,0x08,0xDD,0x77,0xA2,0xDF,0x0A,0xA0,0x75,0x21,0xF4,0x5E,0x8B,
  0x9D,0x48,0xE2,0x37,0x63,0xB6,0x1C,0xC9,0xB4,0x61,0xCB,0x1E,0x4A,0x9F,0x35,0xE0,
  0xCF,0x1A,0xB0,0x65,0x31,0xE4,0x4E,0x9B,0xE6,0x33,0x99,0x4C,0x18,0xCD,0x67,0xB2,
  0x39,0xEC,0x46,0x93,0xC7,0x12,0xB8,0x6D,0x10,0xC5,0x6F,0xBA,0xEE,0x3B,0x91,0x44,
  0x6B,0xBE,0x14,0xC1,0x95,0x40,0xEA,0x3F,0x42,0x97,0x3D,0xE8,0xBC,0x69,0xC3,0x16,
  0xEF,0x3A,0x90,0x45,0x11,0xC4,0x6E,0xBB,0xC6,0x13,0xB9,0x6C,0x38,0xED,0x47,0x92,
  0xBD,0x68,0xC2,0x17,0x43,0x96,0x3C,0xE9,0x94,0x41,0xEB,0x3E,0x6A,0xBF,0x15,0xC0,
  0x4B,0x9E,0x34,0xE1,0xB5,0x60,0xCA,0x1F,0x62,0xB7,0x1D,0xC8,0x9C,0x49,0xE3,0x36,
  0x19,0xCC,0x66,0xB3,0xE7,0x32,0x98,0x4D,0x30,0xE5,0x4F,0x9A,0xCE,0x1B,0xB1,0x64,
  0x72,0xA7,0x0D,0xD8,0x8C,0x59,0xF3,0x26,0x5B,0x8E,0x24,0xF1,0xA5,0x70,0xDA,0x0F,
  0x20,0xF5,0x5F,0x8A,0xDE,0x0B,0xA1,0x74,0x09,0xDC,0x76,0xA3,0xF7,0x22,0x88,0x5D,
  0xD6,0x03,0xA9,0x7C,0x28,0xFD,0x57,0x82,0xFF,0x2A,0x80,0x55,0x01,0xD4,0x7E,0xAB,
  0x84,0x51,0xFB,0x2E,0x7A,0xAF,0x05,0xD0,0xAD,0x78,0xD2,0x07,0x53,0x86,0x2C,0xF9
};

uint8_t calc_crc8(const uint8_t *p, uint8_t len) {
  uint8_t crc = 0;
  while (len--) crc = crc8tab[crc ^ *p++];
  return crc;
}

void make_crsf_packet() {
  memset(crsf_pkt, 0, CRSF_PACKET_SIZE);
  crsf_pkt[0] = ADDR_MODULE;
  crsf_pkt[1] = 24;
  crsf_pkt[2] = TYPE_CHANNELS;
  // Упаковка 8 каналов по 11 бит
  crsf_pkt[3]  =  ch_val[0] & 0xFF;
  crsf_pkt[4]  = (ch_val[0] >> 8) | ((ch_val[1] & 0x07) << 3);
  crsf_pkt[5]  = (ch_val[1] >> 5) | ((ch_val[2] & 0x3F) << 6);
  crsf_pkt[6]  =  ch_val[2] >> 2;
  crsf_pkt[7]  = (ch_val[2] >>10) | ((ch_val[3] & 0x01) << 1);
  crsf_pkt[8]  = (ch_val[3] >> 7) | ((ch_val[4] & 0x0F) << 4);
  crsf_pkt[9]  = (ch_val[4] >> 4) | ((ch_val[5] & 0x7F) << 7);
  crsf_pkt[10] =  ch_val[5] >> 1;
  crsf_pkt[11] = (ch_val[5] >> 9) | ((ch_val[6] & 0x03) << 2);
  crsf_pkt[12] = (ch_val[6] >> 6) | ((ch_val[7] & 0x1F) << 5);
  crsf_pkt[13] =  ch_val[7] >> 3;
  // остальные каналы — MID
  crsf_pkt[25] = calc_crc8(&crsf_pkt[2], 23);
}

void setup() {
  // USB-Serial
  Serial.begin(SERIAL_BAUD);
  while (!Serial) ;
  Serial.println("\n[SkyDeck] Запуск...");
  Serial.println("OK – плата живая!");

  // LED-индикаторы
  pinMode(LED_OK_PIN,  OUTPUT);
  pinMode(LED_ERR_PIN, OUTPUT);
  // зелёный включен, красный выключен
  digitalWrite(LED_OK_PIN,  HIGH);
  digitalWrite(LED_ERR_PIN, LOW);

  // UART2 → CRSF
  Serial2.begin(CRSF_BAUD, SERIAL_8N1, UART2_RX_PIN, UART2_TX_PIN);
  Serial.printf("[SkyDeck] CRSF TX pin = %d\n", UART2_TX_PIN);

  // инициализация
  for (int i = 0; i < CRSF_MAX_CHANNEL; ++i) ch_val[i] = CRSF_CH_MID;
  next_crsf_us = micros();
  last_rx_us    = micros();

  Serial.println("[SkyDeck] Готов к работе");
}

void loop() {
  uint32_t now = micros();

  // CRSF 500Hz
  if (now >= next_crsf_us) {
    make_crsf_packet();
    Serial2.write(crsf_pkt, CRSF_PACKET_SIZE);
    next_crsf_us += CRSF_INTERVAL_US;
  }

  // Приём от Deck
  if (Serial.available()) {
    in_buf = Serial.readStringUntil(':');
    if (in_buf.length() >= CHANNEL_COUNT * 3) {
      last_rx_us = now;
      // связь есть
      digitalWrite(LED_OK_PIN,  HIGH);
      digitalWrite(LED_ERR_PIN, LOW);
      // парсим 8 каналов
      for (int i = 0; i < CHANNEL_COUNT; ++i) {
        int v = in_buf.substring(i*3, i*3+3).toInt();
        ch_val[i] = map(v, 0, 800, CRSF_CH_MIN, CRSF_CH_MAX);
      }
    }
  }

  // Watchdog: нет данных >1с
  if ((micros() - last_rx_us) > (int64_t)TIMEOUT_MS * 1000) {
    // связь потеряна
    digitalWrite(LED_OK_PIN,  LOW);
    digitalWrite(LED_ERR_PIN, HIGH);
    // каналы в MID
    for (int i = 0; i < CRSF_MAX_CHANNEL; ++i) ch_val[i] = CRSF_CH_MID;
    last_rx_us = micros();
  }
}
