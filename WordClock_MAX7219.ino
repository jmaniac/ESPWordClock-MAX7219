// Program to implement a Word Clock using the MD_MAX72XX library.
// Original by Marco Colli (MajicDesigns) - https://github.com/MajicDesigns/WordClock-MAX7219
// Forked for Wemo D1 Mini and a single 8x8 LED matrix for tiny, cute device
//
// --- THIS VERSION ---
// Adapted for the Wemos/LOLIN D1 Mini (ESP8266).
// The DS3231 RTC has been REMOVED. Time is instead obtained over WiFi
// from an NTP server and kept in sync automatically while powered on.
// There is no mode switch and no button - the device just connects to
// WiFi, gets the time, and keeps the word-clock display updated for as
// long as it's powered.
//
// Wiring (Wemos D1 Mini -> MAX7219 matrix):
//   D5 (GPIO14) -> CLK
//   D8 (GPIO15) -> DIN/DATA
//   D7 (GPIO13) -> CS/LOAD
//   3V3 / 5V    -> VCC (module dependent, most single 8x8 MAX7219 boards run fine on 5V from USB)
//   G           -> GND
//
// Note: D8 (GPIO15) is one of the ESP8266's boot-strapping pins (must be
// low at boot to run from flash normally). Since it's only driven as an
// output by this sketch after boot, it works fine here - just don't tie
// it high externally.
//
// Because the D1 Mini has no battery-backed RTC, the clock re-syncs itself
// from NTP every time it (re)connects to WiFi and periodically thereafter.
//
// Library dependencies:
// ---------------------
// MD_MAX72xx library: https://github.com/MajicDesigns/MD_MAX72XX
// ESP8266 Arduino core (provides <ESP8266WiFi.h> and <time.h>)
//
#include <ESP8266WiFi.h>
#include <time.h>
#include <MD_MAX72xx.h>

// --------------------------------------
// WiFi / NTP configuration - EDIT THESE FOR YOUR NETWORK
const char* WIFI_SSID = "YOUR_SSID";
const char* WIFI_PASSWORD = "WIFI_PASSWORD";

const char* NTP_SERVER_1 = "pool.ntp.org";
const char* NTP_SERVER_2 = "time.nist.gov";

// Base UTC offset for your timezone, in seconds. Default here is India
// Standard Time (UTC+5:30). Change this for your location, e.g. -18000
// for US Eastern Standard Time.
const long GMT_OFFSET_SEC = 5 * 3600 + 30 * 60;

// Compile-time daylight-saving flag - there's no button to toggle this,
// so flip it and reflash if your region observes DST and it's currently
// in effect.
const bool SUMMER_TIME_ACTIVE = false;
const int DAYLIGHT_OFFSET_SEC = SUMMER_TIME_ACTIVE ? 3600 : 0;

const uint32_t WIFI_CONNECT_TIMEOUT = 20000; // ms to wait for WiFi on boot
const uint32_t NTP_SYNC_TIMEOUT = 15000; // ms to wait for a valid NTP time

// --------------------------------------
// Hardware definitions
// NOTE: D1 Mini pin labels (Dx) map to these GPIO numbers.
const uint8_t CLK_PIN = D5;  // (or SCK) connect to matrix CLK
const uint8_t DATA_PIN = D8; // (or MOSI) connect to matrix DATA
const uint8_t CS_PIN = D7;   // (or SS) connect to matrix LOAD

// --------------------------------------
// Miscellaneous defines
const uint8_t CLOCK_UPDATE_TIME = 5; // in seconds - time resolution to nearest 5 minutes does not need rapid updates!
const uint32_t NTP_RETRY_INTERVAL = 3600000UL; // in milliseconds - background re-sync every hour

// --------------------------------------
// END OF USER CONFIGURABLE INFORMATION
// --------------------------------------

#define DEBUG 1

// --------------------------------------
// Global variables
MD_MAX72XX display = MD_MAX72XX(MD_MAX72XX::FC16_HW, DATA_PIN, CLK_PIN, CS_PIN, 1); // arbitrary-pin (software) SPI interface

#define ARRAY_SIZE(a) (sizeof(a)/sizeof(a[0]))

#if DEBUG
#define PRINT(s, x) { Serial.print(F(s)); Serial.print(x); }
#define PRINTS(x) Serial.print(F(x))
#define PRINTD(x) Serial.println(x, DEC)
#else
#define PRINT(s, x)
#define PRINTS(x)
#define PRINTD(x)
#endif

// --------------------------------------
// Define the data for the words on the clock face.
// The clock face has the following letter matrix
// 7 6 5 4 3 2 1 0 <-- column
// A T W E N T Y D <-- row 0
// Q U A R T E R Y <-- row 1
// F I V E H A L F <-- row 2
// D P A S T O R O <-- row 3
// F I V E I G H T <-- row 4
// S I X T H R E E <-- row 5
// T W E L E V E N <-- row 6
// F O U R N I N E <-- row 7
//
// - Minutes to/past the hour are all in the rows 0-2 of the display.
// - Past/to text is on row 3
// - The hour name is in rows 4-7
//
// The words may be defined in one or more rows. So to define the bit
// pattern to illuminate for a word, just need to know the row number(s)
// and the bit pattern(s) to turn on for that row.
typedef struct clockWord_t
{
  uint8_t row;
  uint8_t data;
};

// Minutes and to/past are always on the same row, so they can be defined as
// individual elements.
const PROGMEM clockWord_t M_05 = { 2, 0b11110000 };
const PROGMEM clockWord_t M_10 = { 0, 0b01011000 };
const PROGMEM clockWord_t M_15 = { 1, 0b11111110 };
const PROGMEM clockWord_t M_20 = { 0, 0b01111110 };
const PROGMEM clockWord_t M_30 = { 2, 0b00001111 };
const PROGMEM clockWord_t TO   = { 3, 0b00001100 };
const PROGMEM clockWord_t PAST = { 3, 0b01111000 };

// Some hour names are split across rows, so use more than one definition
// per word - make them all arrays for consistent handling in loop code.
const PROGMEM clockWord_t H_01[] = { { 7, 0b01001001 } }; // 1-1-1 symmetrical option
const PROGMEM clockWord_t H_02[] = { { 6, 0b11000000 }, { 7, 0b01000000 } };
const PROGMEM clockWord_t H_03[] = { { 5, 0b00011111 } };
const PROGMEM clockWord_t H_04[] = { { 7, 0b11110000 } };
const PROGMEM clockWord_t H_05[] = { { 4, 0b11110000 } };
const PROGMEM clockWord_t H_06[] = { { 5, 0b11100000 } };
const PROGMEM clockWord_t H_07[] = { { 5, 0b10000000 }, { 6, 0b00001111 } };
const PROGMEM clockWord_t H_08[] = { { 4, 0b00011111 } };
const PROGMEM clockWord_t H_09[] = { { 7, 0b00001111 } };
const PROGMEM clockWord_t H_10[] = { { 4, 0b00000001 }, { 5, 0b00000001 }, { 6, 0b00000001 } }; // vertical option
const PROGMEM clockWord_t H_11[] = { { 6, 0b00111111 } };
const PROGMEM clockWord_t H_12[] = { { 6, 0b11110110 } };

// --------------------------------------
// WiFi + NTP handling
// --------------------------------------

bool connectWiFi(uint32_t timeoutMs)
// Connect to WiFi, returns true on success.
{
  PRINTS("\nConnecting to WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs)
  {
    delay(250);
    PRINTS(".");
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    PRINTS("\nWiFi connected, IP: ");
    PRINT("", WiFi.localIP().toString());
    return(true);
  }

  PRINTS("\nWiFi connect FAILED");
  return(false);
}

bool syncTimeNTP(uint32_t timeoutMs)
// Kick off an NTP sync and block until we get a plausible time back
// (or timeoutMs elapses). Returns true if the time looks valid.
{
  if (WiFi.status() != WL_CONNECTED)
    return(false);

  PRINTS("\nSyncing time from NTP");
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER_1, NTP_SERVER_2);

  time_t now = time(nullptr);
  uint32_t start = millis();

  // A freshly booted ESP8266 clock starts at 0 (1 Jan 1970). Wait until
  // it looks like a real, recent timestamp before trusting it.
  while (now < 1700000000UL && millis() - start < timeoutMs)
  {
    delay(200);
    PRINTS(".");
    now = time(nullptr);
  }

  if (now >= 1700000000UL)
  {
    PRINTS("\nTime synced");
    return(true);
  }

  PRINTS("\nNTP sync FAILED");
  return(false);
}

void getCurrentTime(uint8_t &h, uint8_t &m, uint8_t &s)
// Fetch current local time (already offset by GMT_OFFSET_SEC and
// DAYLIGHT_OFFSET_SEC via configTime) and convert to 12-hour format
// for the display logic.
{
  time_t now = time(nullptr);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);

  uint8_t h24 = timeinfo.tm_hour; // 0-23
  uint8_t h12 = h24 % 12;
  if (h12 == 0) h12 = 12;

  h = h12;
  m = timeinfo.tm_min;
  s = timeinfo.tm_sec;
}

void dumpTime()
// Show displayed time to the debug display
{
  uint8_t h, m, s;
  getCurrentTime(h, m, s);

  if (h < 10) PRINTS("0");
  PRINT("", h);
  PRINTS(":");
  if (m < 10) PRINTS("0");
  PRINT("", m);
  PRINTS(":");
  if (s < 10) PRINTS("0");
  PRINT("", s);
  PRINTS(" ");
}

// --------------------------------------
// Display helpers (unchanged from the original)
// --------------------------------------

void updateClock(uint8_t h, uint8_t m)
// Work out what current time it is in words and turn on the right
// parts of the display. The time is passed to the function so that
// it is dependent of the time source.
// This logic tries to copy the approximations people make when reading
// analog time. It is consistent but arbitrary - note that any changes need
// to be made consistently across all the checks in this part of the code.
{
  const uint8_t PRE_DELTA = 2;  // minutes before the actual min
  const uint8_t POST_DELTA = 2; // minutes after the actual min

  const clockWord_t *H;
  uint8_t numElements;

  PRINTS("\nT: ");
  dumpTime(); // debug output only

  // freeze the clock display while we make changes to the matrix
  display.control(MD_MAX72XX::UPDATE, MD_MAX72XX::OFF);
  display.clear();

  // minutes - are worked out in an interval [-PRE_DELTA, POST_DELTA] around the time
  // to select the choice of words.
  switch (m)
  {
  case 0 ... 0+POST_DELTA:
  case 60-PRE_DELTA ... 59:
    // nothing to say at top of the hour
    break;

  case 5-PRE_DELTA ... 5+POST_DELTA:
  case 55-PRE_DELTA ... 55+POST_DELTA:
    PRINTS("FIVE");
    display.setRow(pgm_read_byte(&M_05.row), pgm_read_byte(&M_05.data));
    break;

  case 10-PRE_DELTA ... 10+POST_DELTA:
  case 50-PRE_DELTA ... 50+POST_DELTA:
    PRINTS("TEN");
    display.setRow(pgm_read_byte(&M_10.row), pgm_read_byte(&M_10.data));
    break;

  case 15-PRE_DELTA ... 15+POST_DELTA:
  case 45-PRE_DELTA ... 45+POST_DELTA:
    PRINTS("QUARTER");
    display.setRow(pgm_read_byte(&M_15.row), pgm_read_byte(&M_15.data));
    break;

  case 20-PRE_DELTA ... 20+POST_DELTA:
  case 40-PRE_DELTA ... 40+POST_DELTA:
    PRINTS("TWENTY");
    display.setRow(pgm_read_byte(&M_20.row), pgm_read_byte(&M_20.data));
    break;

  case 25-PRE_DELTA ... 25+POST_DELTA:
  case 35-PRE_DELTA ... 35+POST_DELTA:
    PRINTS("TWENTY-FIVE");
    display.setRow(pgm_read_byte(&M_05.row), pgm_read_byte(&M_05.data));
    display.setRow(pgm_read_byte(&M_20.row), pgm_read_byte(&M_20.data));
    break;

  case 30-PRE_DELTA ... 30+POST_DELTA:
    PRINTS("HALF");
    display.setRow(pgm_read_byte(&M_30.row), pgm_read_byte(&M_30.data));
    break;
  }

  // To/past display
  if (m > 0+POST_DELTA && m < 60-PRE_DELTA) // top of the hour interval displays the hour only
  {
    if (m <= 30+POST_DELTA) // in the first half hour it is 'past' and ...
    {
      PRINTS(" PAST ");
      display.setRow(pgm_read_byte(&PAST.row), pgm_read_byte(&PAST.data));
    }
    else // ... after the half hour it becomes 'to'
    {
      PRINTS(" TO ");
      display.setRow(pgm_read_byte(&TO.row), pgm_read_byte(&TO.data));
    }
  }

  // After the half hour we also have to adjust the hour number!
  if (m > 30 + POST_DELTA)
  {
    if (h < 12) h++;
    else h = 1;
  }

  // hour - straight translation of number to data. However, the word can
  // span more than one line so the data is set up in arrays.
  switch (h)
  {
  case 1:  H = H_01; numElements = ARRAY_SIZE(H_01); PRINTS("ONE"); break;
  case 2:  H = H_02; numElements = ARRAY_SIZE(H_02); PRINTS("TWO"); break;
  case 3:  H = H_03; numElements = ARRAY_SIZE(H_03); PRINTS("THREE"); break;
  case 4:  H = H_04; numElements = ARRAY_SIZE(H_04); PRINTS("FOUR"); break;
  case 5:  H = H_05; numElements = ARRAY_SIZE(H_05); PRINTS("FIVE"); break;
  case 6:  H = H_06; numElements = ARRAY_SIZE(H_06); PRINTS("SIX"); break;
  case 7:  H = H_07; numElements = ARRAY_SIZE(H_07); PRINTS("SEVEN"); break;
  case 8:  H = H_08; numElements = ARRAY_SIZE(H_08); PRINTS("EIGHT"); break;
  case 9:  H = H_09; numElements = ARRAY_SIZE(H_09); PRINTS("NINE"); break;
  case 10: H = H_10; numElements = ARRAY_SIZE(H_10); PRINTS("TEN"); break;
  case 11: H = H_11; numElements = ARRAY_SIZE(H_11); PRINTS("ELEVEN"); break;
  case 12: H = H_12; numElements = ARRAY_SIZE(H_12); PRINTS("TWELVE"); break;
  }

  for (uint8_t i = 0; i < numElements; i++)
    display.setRow(pgm_read_byte(&H[i].row), pgm_read_byte(&H[i].data));

  // finally, update the display with new data
  display.control(MD_MAX72XX::UPDATE, MD_MAX72XX::ON);
}

// --------------------------------------
// Setup / Loop
// --------------------------------------

void setup()
{
#if DEBUG
  Serial.begin(115200);
  delay(200);
#endif
  PRINTS("\n[MD_MAX72XX_WordClock - Wemos D1 Mini / WiFi Edition]");

  display.begin();
  display.control(MD_MAX72XX::INTENSITY, 2 + (MAX_INTENSITY / 2));

  // Get on WiFi and pull down the current time. If this fails (e.g. router
  // is off), we carry on anyway - the clock will just show 1 Jan 1970 until
  // the next successful sync in the main loop.
  if (connectWiFi(WIFI_CONNECT_TIMEOUT))
    syncTimeNTP(NTP_SYNC_TIMEOUT);
}

void loop()
{
  static uint32_t timeLastUpdate = 0;
  static uint32_t timeLastNtpSync = 0;

  // Opportunistic background re-sync so long-running clocks don't drift
  // and so we recover automatically if WiFi/NTP was down at boot.
  if (millis() - timeLastNtpSync >= NTP_RETRY_INTERVAL)
  {
    timeLastNtpSync = millis();
    if (WiFi.status() == WL_CONNECTED)
      syncTimeNTP(NTP_SYNC_TIMEOUT);
    else
      connectWiFi(WIFI_CONNECT_TIMEOUT);
  }

  // update the word-clock display at a fixed interval
  if (millis() - timeLastUpdate >= CLOCK_UPDATE_TIME * 1000UL)
  {
    timeLastUpdate = millis();
    uint8_t h, m, s;
    getCurrentTime(h, m, s);
    updateClock(h, m);
  }
}
