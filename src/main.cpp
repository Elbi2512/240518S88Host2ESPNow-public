// Intellibox timing gebruikt
// tijdelijke hostmac: 0C:DC:7E:5F:19:04
// 251210 IRQ met timer er uit..
// web opgegeve..
// 251120 Inlees helemaal op den kop en ECOS look alike gemaakt
// 251118 15:30-16:00 debug printfs toegevoegd
// 251118 13:56-14:20
// 251118 09:00
// 251117 21:57-06-2024 ESPNow S88 Host
#include <arduino.h>
#include <spi.h>
#include <Wire.h>        // Only needed for Arduino 1.6.5 and earlier
#include "SSD1306Wire.h" // legacy: #include "SSD1306.h"

// ==================================================
// 1) Zet hieronder uit als je klaar bent met debuggen
#define DEBUG_PRINT
// ==================================================

#if defined DEBUG_PRINT
#define baud 115200
#define debug_begin(baud) Serial.begin(baud)
#define debug(x) Serial.print(x)
#define debugln(x) Serial.println(x)
#else
#define debug_begin(baud)
#define debug(x)
#define debugln(x)
#endif

#define debugf(fmt, ...)               \
  {                                    \
    Serial.print(__FILE__);            \
    Serial.print(' : ');               \
    Serial.print(__LINE__);            \
    Serial.print(': ');                \
    Serial.printf(fmt, ##__VA_ARGS__); \
  }

#if defined(ARDUINO_ARCH_ESP8266)
#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <espnow.h>
#elif defined(ARDUINO_ARCH_ESP32)
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#endif

#define NEUSTAD 1
#define LOCLIFT 2
#define PFORZHEIM 3
#define INDUSTRIE 4
// 1=neustad, 2=loclift, 3=pforzheim
#define doelomgeving 3

#if NEUSTAD == doelomgeving
#define BOARDID 2              // 4e uitgave ontvangreeks
#define OFFSET1 1              // de S88 waar deze zich moet melden bij de ECOS
#define NUMBER_OF_S88MODULES 4 // The number of S88 units we want to simulate with the Arduino.
char doelomgevingstring[] = "NEUSTAD     ";
#endif

#if LOCLIFT == doelomgeving
#define BOARDID 3              // 4e uitgave ontvangreeks
#define OFFSET1 5              // de S88 waar deze zich moet melden bij de ECOS
#define NUMBER_OF_S88MODULES 9 // The number of S88 units we want to simulate with the Arduino.
char doelomgevingstring[] = "LOCLIFT     ";
#endif

#if PFORZHEIM == doelomgeving
#define BOARDID 4               // 4e uitgave ontvangreeks
#define OFFSET1 14              // de S88 waar deze zich moet melden bij de ECOS
#define NUMBER_OF_S88MODULES 13 // The number of S88 units we want to simulate with the Arduino.
char doelomgevingstring[] = "PFORZHEIM   ";
#endif

#if INDUSTRIE == doelomgeving
#define BOARDID 5              // 4e uitgave ontvangreeks
#define OFFSET1 23             // de S88 waar deze zich moet melden bij de ECOS
#define NUMBER_OF_S88MODULES 5 // The number of S88 units we want to simulate with the Arduino.
char doelomgevingstring[] = "INDUSTRIE   ";
#endif

#define SDA 22
#define SCL 21
#define SCREEN_I2C_ADDRESS 0x3C
#define SCREEN_HEIGHT 64
#define SCREEN_WIDTH 128
#define SCREEN_VERTICAL_FLIP true
SSD1306Wire display(0x3c, SDA, SCL);

uint8_t broadcastAddress[] =
    //    {0x0C, 0xDC, 0x7E, 0x5F, 0x19, 0x04}; // ope de bench
    {0x0C, 0xDC, 0x7E, 0x5E, 0x99, 0xD4}; // one in de kelder
// tijdelijke host voor debuggen 30:AE:A4:57:EE:C8
// uint8_t broadcastAddress[] = {0x30, 0xAE, 0xA4, 0x57, 0xEE, 0xC8}; // one in de slaapkamer beneden

const int NUM_BITS = NUMBER_OF_S88MODULES * 16; // 4 modules van 16 ingangen

bool rawBits[NUM_BITS];
bool stableBits[NUM_BITS];
bool previousStableBits[NUM_BITS];
unsigned long lastChange[NUM_BITS];
const unsigned long debounceTime = 20; // ms
char tekst0[40] = "";
char tekst1[40] = "";
char tekst2[40] = "";
char opmaak[40];
// Structure example to send data
// Must match the receiver structure
typedef struct struct_message
{
  u8_t ID;
  u8_t OFFSET;
  u8_t AANTAL;
  u8_t buffer[NUMBER_OF_S88MODULES * 2]; // geen idee waarom TOTAL_BITS niet werkt
} struct_message;
// Create a struct_message called myData
struct_message myData;
esp_now_peer_info_t peerInfo;
//--------------------------------------------------------------
// Pinbezetting bij de decoderprint:
//
// Uitgang:
#define S88ResetPin 2 // A3 //S88 Reset
#define S88_CLOCK 23  // CLOCK line = HIGH? -> Do something NOW!
#define S88_LOAD 18   // LOAD line = HIGH? -> Master request the data NOW!
// Ingang:
#define S88_DATA 19 // DATA line = This line will be used to write out the requested data in sync with the CLOCK pulses when the LOAD line is triggered.

// Functie voor Core 0
// Flags & Tasks
TaskHandle_t TaskDisplay;
volatile bool bShow = false;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
volatile bool sendOk = false;

void TaskCore0(void *pvParameters)
{
  for (;;)
  {
    if (bShow == true)
    {
      //   portENTER_CRITICAL(&mux);
      bShow = false;
      display.clear();
      display.drawString(1, 0, opmaak);
      display.drawString(1, 15, tekst0);
      display.drawString(1, 30, tekst1);
      display.drawString(1, 45, tekst2);
      display.display();
      //   portEXIT_CRITICAL(&mux);
    }
    vTaskDelay(1);
  }
}

// ==================================================
// ESP-NOW Callback
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status)
{
  sendOk = (status == ESP_NOW_SEND_SUCCESS);
}

// ==================================================
// S88 Read (strakke GPIO timing)
#define CLK_HIGH GPIO.out_w1ts = (1 << S88_CLOCK)
#define CLK_LOW GPIO.out_w1tc = (1 << S88_CLOCK)
void readS88() //  service routine
{
  /* Inlezen in de sfeer van een HSI-88
  digitalWrite(S88_LOAD, HIGH);
  delayMicroseconds(20); // aangezet
  // digitalWrite(S88_CLOCK, LOW); // aangepast: is al low
  digitalWrite(S88_CLOCK, HIGH);
  delayMicroseconds(42);
  digitalWrite(S88_CLOCK, LOW); // aangepast van high
  delayMicroseconds(42);
  //  weg: digitalWrite(S88_LOAD, LOW);
  digitalWrite(S88ResetPin, HIGH);
  delayMicroseconds(35);
  digitalWrite(S88ResetPin, LOW);
  // aangepast, was: delayMicroseconds(70);
  delayMicroseconds(30);
  digitalWrite(S88_LOAD, LOW);
  delayMicroseconds(19);

  // uint8_t WaitTable[] = {11, 16, 20, 25, 30, 35, 40, 45, 50, 55, 60, 65, 70, 75, 80, 85};
  uint8_t WaitTable[] = {   11, 16, 20, 25, 29, 34, 39, 44, 48, 59, 64, 65, 69, 71, 75, 80};

  // Lees alle bits (modules × 16)
  for (int j = 0; j < 16; j++)
  {
    for (int i = 0; i < NUMBER_OF_S88MODULES; i++)
    {
      if (i == 0 && j == 0)
      { // Clock staat nog laag.en hebben de wachttijd gehad. Valide data nu op Data
        rawBits[j * NUMBER_OF_S88MODULES + i] = ((GPIO.in & (1 << S88_DATA)) != 0);
        delayMicroseconds(150);
      }
      else
      {
        digitalWrite(S88_CLOCK, HIGH);
        delayMicroseconds(WaitTable[j]);
        // rawBits[j*NUMBER_OF_S88MODULES + i] = (GPIO.in & (1 << S88_DATA));
        rawBits[j * NUMBER_OF_S88MODULES + i] = ((GPIO.in & (1 << S88_DATA)) != 0);
        delayMicroseconds(160 - WaitTable[j]);
        // CLOCK hoog → volgende bit
        digitalWrite(S88_CLOCK, LOW);
        delayMicroseconds(190);
      }
    }
  }
    */
  /* inlezen in de sfeer van een intellibox
  digitalWrite(S88_LOAD, HIGH);
  delayMicroseconds(20); // aangezet // +20
  digitalWrite(S88_CLOCK, HIGH);
  delayMicroseconds(50); // +70
  digitalWrite(S88_CLOCK, LOW);
  delayMicroseconds(48);       // +118
  digitalWrite(S88ResetPin, HIGH); // =48
  delayMicroseconds(37);           // +155 ==65
  digitalWrite(S88ResetPin, LOW);
  // aangepast, was: delayMicroseconds(70);
  delayMicroseconds(30); // +185 uS;  ==85
  digitalWrite(S88_LOAD, LOW);
  delayMicroseconds(50); // ==135 uS

  for (u8_t j = 0; j < 16; j++)
  {
    for (u8_t i = 0; i < NUMBER_OF_S88MODULES; i++)
    {
      digitalWrite(S88_CLOCK, HIGH);
      if (i == 0 && j == 0) // eerste bit krijgt bijzondere behandeling
      { // Clock staat gaat laag naar hoog .en hebben de wachttijd gehad.
        // Valide data nu op Data
        rawBits[j * NUMBER_OF_S88MODULES + i] = ((GPIO.in & (1 << S88_DATA)) != 0);
      }
      else
      {
        delayMicroseconds(30);
        digitalWrite(S88_CLOCK, LOW);
        rawBits[j * NUMBER_OF_S88MODULES + i] = ((GPIO.in & (1 << S88_DATA)) != 0);
        delayMicroseconds(25);
      }
    }
  } */
  // --- INIT SEQUENCE (Intellibox timing) ---
  digitalWrite(S88_LOAD, HIGH);
  delayMicroseconds(20);

  digitalWrite(S88_CLOCK, HIGH);
  delayMicroseconds(50);

  digitalWrite(S88_CLOCK, LOW);
  delayMicroseconds(48);

  digitalWrite(S88ResetPin, HIGH);
  delayMicroseconds(37);

  digitalWrite(S88ResetPin, LOW);
  delayMicroseconds(30);

  digitalWrite(S88_LOAD, LOW);
  delayMicroseconds(50);

  // ============================================================
  // 1) EERSTE BIT (bit 0) APART INLEZEN
  // ============================================================

  // Clock HIGH → data geldig
  digitalWrite(S88_CLOCK, HIGH); // low naar high, inlezen dus
  // delayMicroseconds(30); // deze wil Leendert niet..

  rawBits[0] = ((GPIO.in & (1 << S88_DATA)) != 0);

  // Clock LOW om in een nette toestand te komen
  // digitalWrite(S88_CLOCK, LOW); // deze wil ik ook niet..
  // delayMicroseconds(30); // aangepast

  // ============================================================
  // 2) REST VAN DE BITS (1 t/m NUM_BITS-1) IN IDENTIEKE CYCLUS
  // ============================================================

  for (int bit = 1; bit < NUM_BITS; bit++)
  {
    delayMicroseconds(30);
    // Clock LOW
    digitalWrite(S88_CLOCK, LOW);
    // Lees data
    rawBits[bit] = ((GPIO.in & (1 << S88_DATA)) != 0);
    delayMicroseconds(25);
    // Clock HIGH
    digitalWrite(S88_CLOCK, HIGH);
  }
  digitalWrite(S88_CLOCK, LOW); // laat hem laag achter
}

// void packBitsToBytes(uint8_t out[64])
void packBitsToBytes(const bool *inBits, uint8_t *out)
{
  for (int module = 0; module < NUMBER_OF_S88MODULES; module++)
  {
    for (int byteInModule = 0; byteInModule < 2; byteInModule++)
    {
      uint8_t value = 0;
      for (int bit = 0; bit < 8; bit++)
      {
        const int port = byteInModule * 8 + bit;
        const int inputIndex = port * NUMBER_OF_S88MODULES + module;
        if (inBits[inputIndex])
        {
          value |= (1U << (7 - bit));
        }
      }
      out[module * 2 + byteInModule] = value;
    }
  }
}

bool sentdata() // verzend de data via ESPNow
{
  myData.ID = BOARDID;
  myData.OFFSET = OFFSET1;
  myData.AANTAL = NUMBER_OF_S88MODULES;
  packBitsToBytes(stableBits, myData.buffer);

  sendOk = false;
  esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *)&myData, sizeof(myData));

  if (result != ESP_OK)
  {
    char tekst[80];
    sprintf(tekst, "esp_now_send error: %d", (esp_err_t)result);
    debugln(tekst);
    return false;
  }
  unsigned long t0 = millis();
  while (!sendOk && millis() - t0 < 100)
  {
    delay(1);
  }
  if (!sendOk)
  {
    debugln("ESP-NOW send timeout (no callback within 100 ms)");
  }

  return sendOk;
}

bool debounceBits()
{
  unsigned long now = millis();
  bool bDirty = false;
  char tekst[80];
  for (int i = 0; i < NUM_BITS; i++)
  {
    // 1. Detecteer verandering in RAW
    if (rawBits[i] != previousStableBits[i])
    {
      // RAW is anders dan vorige stabiele waarde → timer starten
      lastChange[i] = now;
      previousStableBits[i] = rawBits[i];
    }
    // 2. Check of RAW lang genoeg stabiel is
    if ((now - lastChange[i]) >= debounceTime)
    {
      if (stableBits[i] != rawBits[i])
      {
        bool old = stableBits[i];
        stableBits[i] = rawBits[i];

        bDirty = true;
        Serial.printf("Mod %d:%d changed from %d to %d\n\r", i / 16, i % 16, old, stableBits[i]);
        sprintf(tekst, "%02d:%02d, M%02d:%02d ->%d ", (millis() / 10000) % 60, (millis() % 1000) % 60, i / 16, i % 16, stableBits[i]);
        // debugln(tekst);
        strcpy(tekst2, tekst1);
        strcpy(tekst1, tekst0);
        strcpy(tekst0, tekst);
      }
    }
  }
  if (bDirty)
  {
    bShow = true;
  }
  return bDirty;
}

void setup() // Main setup functie
{
  debug_begin(baud);
  debugln();
  debugln("\r\nReboot\r\n");
  for (int i = 0; i < NUM_BITS; i++)
  {
    rawBits[i] = false;
    stableBits[i] = false;
    previousStableBits[i] = false;
    lastChange[i] = millis();
  }
  display.init();
  display.flipScreenVertically();
  display.clear(); // Note the format for setting a serial port is as follows: Serial2.begin(baud-rate, protocol, RX pin, TX pin);
  display.setFont(ArialMT_Plain_10);
  display.println("BOOTING... ");
  display.display();
  debugln("S88 Bus");

  pinMode(S88_LOAD, OUTPUT);
  pinMode(S88_CLOCK, OUTPUT);
  pinMode(S88_DATA, INPUT);
  // _PULLUP);
  pinMode(S88ResetPin, OUTPUT); // Reset
  digitalWrite(S88ResetPin, LOW);
  digitalWrite(S88_LOAD, LOW);
  digitalWrite(S88_CLOCK, LOW);

  WiFi.mode(WIFI_STA);

  if (esp_now_init() != 0)
  {
    debugln("Error initializing ESP-NOW");
    return;
  }
  // Once ESPNow is successfully Init, we will register for Send CB to
  // get the status of Trasnmitted packet
  esp_now_register_send_cb(OnDataSent);
  // Register peer
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.encrypt = false;
  // Add peer
  if (esp_now_add_peer(&peerInfo) != ESP_OK)
  {
    debugln("Failed to add peer");
    return;
  }
  debugln("Init Done");
  char tekst[40];
  display.clear();
  sprintf(tekst, "Booted: %s", doelomgevingstring);
  display.drawString(1, 0, tekst);
  display.display();
  xTaskCreatePinnedToCore(TaskCore0, "TaskDisplay", 2048, NULL, 1, &TaskDisplay, 0);
}
//--------------------------------------------------------------
void loop() // Main loop functie
{
  static long timel = millis();
  readS88(); // alle NUMBER_OF_S88MODULES * 2  bits binnenhalen
  if (debounceBits() || (millis() - timel > 5000))
  { // schoonmaken
    if (!sentdata())
      debugln("Niet goe "); // sturen via ESP-NOW
    if (millis() - timel > 5000)
    {
      debugln("Watchdog triggered send");
      timel = millis();
    }
    else
    {
      debugln("Data changed, sent update");
      timel = millis();
      sprintf(opmaak, "%s %02d:%02d", doelomgevingstring, (millis() / 10000) % 60, (millis() % 1000) % 60);
    }
  }
  else
  {
    delay(10); // kleine pauze
  }
  yield(); // allow background processes to run
}