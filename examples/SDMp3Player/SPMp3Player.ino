/*
 * Wiring modules :
 * (ESP8266 = Wemos D1 mini / NodeMCU ESP12) 
 * 
 * VS1053  ESP8266  MicroSD
 * SCK     D5       SCLK  (signal clock)
 * MISO    D6       MISO  (DO:output)
 * MOSI    D7       MOSI  (DI:input)
 *         D8       SS    (CS:chip select)
 *         3V3      VDD   (VCC:3.3V)
 * DGND    G        VSS   (0V:ground)
 * 5V      5V
 * XRST    RST
 * XCS     D1
 * XDCS    D0
 * DREQ    D3
 */
#include <ESP8266WiFi.h>
#include <SPI.h>
#include <SD.h>
#include "VS1053.h"  // version simplifiée ("ArduinoLog" ==> "SerialPrint")

#define SD_CHIP_SELECT  D8
#define CHUNK_SD        1024  // 32
uint8_t sdBuf[CHUNK_SD];

#define MAX_FILE   10
#define PAUSE      (10 * 1000)

File    mp3File;
uint8_t nextFile = MAX_FILE - 1;  // pour demarrer a 0 : (ligne 73) = 0
char    fileName[14]              =  "/MUSIC00.MP3\0";  // "/TRACK00.MP3\0"
uint32_t prevMillis = PAUSE;

// Wiring of VS1053 board (SPI connected in a standard way)
#define VS1053_CS     D1
#define VS1053_DCS    D0
#define VS1053_DREQ   D3
#define CHUNK_VS1053  32  // buffer VS1053
#define VOLUME        75  // volume level 0-100
#define PAUSE         (3 * 1000)

VS1053 player (VS1053_CS, VS1053_DCS, VS1053_DREQ);

enum datamode_t { INIT = 1, DATA = 2, STOPREQD = 4, STOPPED = 8, UPDATE = 16, ERROR = 32 };  // State for dataMode
datamode_t dataMode;

void setup () {
  Serial.begin (9600);  // init Serial
  WiFi.mode (WIFI_OFF);  // Wifi OFF
  WiFi.forceSleepBegin ();
  delay (1);
  
  SPI.begin ();  // init SPI
  player.begin ();  // init player VS1053
  player.switchToMp3Mode ();  // optional, some boards require this
    
  Serial.print ("\nInit SD card... ");  // init SD card (FAT32)
  if (!SD.begin (SD_CHIP_SELECT))
    return (void)srPrint ("failed!");
  srPrint ("done.");
  pinMode (LED_BUILTIN, OUTPUT);  // init Output LED (D4=2)
  dataMode = INIT;
}  // End setup ()

bool getNextFile () {
  for (uint8_t i = 1; i <= MAX_FILE; i++) {
    nextFile = (nextFile + i) % MAX_FILE;
    fileName[6] = nextFile / 10 + '0';  // '0' = 48
    fileName[7] = nextFile % 10 + '0';
    if (SD.exists (fileName))
      return true;
  }
  return false;
}  // End getNextFile ()

void loop () { 
  uint32_t maxFileChunk;  // Max number of bytes to read from
  uint16_t sdBufCnt;
  
  if (dataMode == INIT)  // open data
    if (getNextFile ()) {
      srPrint ("Open file : %s", fileName);
      mp3File = SD.open (fileName, O_READ);
      if (!mp3File)
        srPrint ("Error opening file.");
      else {
        maxFileChunk = sdBufCnt = 0;
        dataMode = DATA;
        player.setVolume (VOLUME);
        digitalWrite (LED_BUILTIN, LOW);
        srPrint ("Read data ...");
      }
    }

  if (dataMode == DATA)  // read data
    if ((maxFileChunk == 0) && player.data_request ()) {
      maxFileChunk = mp3File.available ();  // Bytes left in file
      if (maxFileChunk != 0) {
        if (maxFileChunk > CHUNK_SD)
          maxFileChunk = CHUNK_SD;
        mp3File.read (sdBuf, maxFileChunk);
        yield ();
      }
      sdBufCnt = 0;
    }

  for (uint8_t sizeBuf = CHUNK_VS1053; maxFileChunk != 0; maxFileChunk -= sizeBuf) {  // play data
    sizeBuf = (maxFileChunk > CHUNK_VS1053) ? CHUNK_VS1053 : maxFileChunk;
    player.playChunk (&(sdBuf[sdBufCnt]), sizeBuf);
    sdBufCnt += sizeBuf;
    yield ();
  }
  
  if (dataMode == DATA)  // end data
    if ((mp3File.available () == 0) && (maxFileChunk == 0))
      dataMode = STOPREQD;

  if (dataMode == STOPREQD) {  // stop data
    srPrint ("Close file.");
    digitalWrite (LED_BUILTIN, HIGH);
    mp3File.close ();
    player.setVolume (0);  // Mute
    player.stopSong ();    // Stop playing
    dataMode = STOPPED;
    prevMillis = millis ();
  }

  if (dataMode == STOPPED)  // next data
    if ((uint32_t)(millis () - prevMillis) >= PAUSE) {
      dataMode = INIT;
      srPrint ("Start next file ...");
    }
  yield ();
}  // End loop ()
