#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <HTTPUpdate.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <esp_sleep.h>
#include <MFRC522v2.h>
#include <MFRC522DriverI2C.h>
#include <MFRC522DriverSPI.h>
#include <MFRC522DriverPinSimple.h>
#include "GxEPD2_3C_min.h"
#include <Fonts/FreeMonoBold9pt7b.h>

#ifndef RFID_DEBUG
#define RFID_DEBUG 0
#endif

static const char* DEFAULT_WIFI_SSID = "";
static const char* DEFAULT_WIFI_PASSWORD = "";
static const char* MDNS_NAME = "epaper";
static const char* AP_SSID_PREFIX = "ESP32_e-paper_4.2V2_";
static const char* FIRMWARE_DEVICE = "esp32c3-epaper-nfc-display";
static const char* FIRMWARE_VERSION = "1.0.1";
static const char* FIRMWARE_BUILD = "2026-07-09";
static const char* UPDATE_MANIFEST_URL = "https://raw.githubusercontent.com/fongying/esp32-epaper-nfc-display/main/firmware/manifest.json";
static const IPAddress AP_IP(192, 168, 0, 1);
static const IPAddress AP_GATEWAY(192, 168, 0, 1);
static const IPAddress AP_SUBNET(255, 255, 255, 0);
static constexpr uint8_t DNS_PORT = 53;
static constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 30000;

static constexpr uint16_t EPD_WIDTH = 400;
static constexpr uint16_t EPD_HEIGHT = 300;
static constexpr size_t IMAGE_BYTES = (EPD_WIDTH / 8) * EPD_HEIGHT;
static constexpr size_t MAX_UPLOAD_BYTES = IMAGE_BYTES * 2;
static constexpr uint8_t MAX_NFC_BINDINGS = 20;

static constexpr uint8_t EPD_BUSY = 0;
static constexpr uint8_t EPD_RST = 1;
static constexpr uint8_t EPD_DC = 3;
static constexpr uint8_t EPD_CS = 7;
static constexpr uint8_t EPD_SCK = 4;
static constexpr int8_t EPD_MISO = -1;
static constexpr uint8_t EPD_MOSI = 6;

static constexpr uint8_t NFC_SDA = 8;
static constexpr uint8_t NFC_SCL = 9;
static constexpr uint8_t NFC_I2C_ADDRESS = 0x28;
static constexpr uint8_t NFC_SPI_MISO = 20;
static constexpr uint8_t NFC_SPI_CS = 10;
static constexpr uint8_t NFC_SPI_RST = 21;

static constexpr uint8_t WAKE_BUTTON = 5;
static constexpr uint8_t BATTERY_ADC_PIN = 2;
static constexpr float BATTERY_DIVIDER_RATIO = 2.0f;
static constexpr uint16_t BATTERY_EMPTY_MV = 3300;
static constexpr uint16_t BATTERY_FULL_MV = 4200;
static constexpr uint16_t BATTERY_PRESENT_MIN_MV = 500;
static constexpr uint8_t LOW_BATTERY_PERCENT = 20;
static constexpr uint8_t OTA_MIN_BATTERY_PERCENT = 20;
static constexpr uint32_t SLEEP_TIMEOUT_MS = 5UL * 60UL * 1000UL;
static constexpr uint32_t NFC_POLL_MS = 250;
static constexpr uint32_t SAME_UID_REFRESH_GUARD_MS = 8000;

struct BatteryStatus
{
  uint16_t mv;
  uint8_t percent;
  bool present;
  bool low;
  bool otaAllowed;
};

class MFRC522DriverSPIPinned : public MFRC522DriverSPI
{
public:
  MFRC522DriverSPIPinned(
    MFRC522DriverPin& chipSelectPin,
    SPIClass& spiClass,
    int8_t sck,
    int8_t miso,
    int8_t mosi,
    int8_t ss,
    const SPISettings spiSettings = SPISettings(1000000u, MSBFIRST, SPI_MODE0)
  ) : MFRC522DriverSPI(chipSelectPin, spiClass, spiSettings),
      _sck(sck),
      _miso(miso),
      _mosi(mosi),
      _ss(ss)
  {
  }

  bool init() override
  {
    // MFRC522DriverSPI::init() calls SPI.begin() without pins. On ESP32-C3 that
    // can silently switch back to default SPI pins, so keep the PCB pin mapping here.
    _spiClass.begin(_sck, _miso, _mosi, _ss);
    if (_chipSelectPin.init() == false) return false;
    _chipSelectPin.high();
    return true;
  }

private:
  int8_t _sck;
  int8_t _miso;
  int8_t _mosi;
  int8_t _ss;
};

GxEPD2_3C<GxEPD2_420c_GDEY042Z98, GxEPD2_420c_GDEY042Z98::HEIGHT> display(
  GxEPD2_420c_GDEY042Z98(/*CS=*/ EPD_CS, /*DC=*/ EPD_DC, /*RST=*/ EPD_RST, /*BUSY=*/ EPD_BUSY)
);

WebServer server(80);
DNSServer dnsServer;
Preferences preferences;
MFRC522DriverI2C nfcI2cDriver{NFC_I2C_ADDRESS, Wire};
MFRC522 nfcI2cReader{nfcI2cDriver};
MFRC522DriverPinSimple nfcSpiCsPin{NFC_SPI_CS};
MFRC522DriverSPIPinned nfcSpiDriver{nfcSpiCsPin, SPI, EPD_SCK, NFC_SPI_MISO, EPD_MOSI, NFC_SPI_CS};
MFRC522 nfcSpiReader{nfcSpiDriver};
MFRC522* activeNfcReader = nullptr;

enum class NfcBus : uint8_t
{
  None,
  I2C,
  SPI
};

NfcBus activeNfcBus = NfcBus::None;

uint8_t blackBuffer[IMAGE_BYTES];
uint8_t redBuffer[IMAGE_BYTES];
size_t uploadOffset = 0;
bool uploadFailed = false;
bool uploadHasRed = false;
bool nfcReady = false;
bool fsReady = false;
bool displayBusy = false;
bool wakeButtonReleased = false;
bool wifiStaConnected = false;
bool wirelessEnabled = false;
bool mdnsStarted = false;
bool apModeActive = false;
bool restartPending = false;
String uploadError;
String apSsid;
String wifiSsid;
String wifiPassword;
String bindTargetUid;
String lastNfcUid;
String lastNfcStatus = "尚未讀取卡片";
String lastDisplayedUid;
unsigned long lastNfcPollMs = 0;
unsigned long lastNfcSeenMs = 0;
unsigned long lastNfcDisplayMs = 0;
unsigned long lastActivityMs = 0;
unsigned long buttonLowSinceMs = 0;
unsigned long restartAtMs = 0;

#include "web_index.h"
#include "nfc_index.h"
#include "config_index.h"


void markActivity()
{
  lastActivityMs = millis();
}

uint8_t batteryPercentFromMv(uint16_t mv)
{
  if (mv <= BATTERY_EMPTY_MV) return 0;
  if (mv >= BATTERY_FULL_MV) return 100;
  return (uint8_t)(((uint32_t)(mv - BATTERY_EMPTY_MV) * 100UL) / (BATTERY_FULL_MV - BATTERY_EMPTY_MV));
}

BatteryStatus readBatteryStatus()
{
  uint32_t sumMv = 0;
  for (uint8_t i = 0; i < 16; i++)
  {
    sumMv += analogReadMilliVolts(BATTERY_ADC_PIN);
    delay(2);
  }

  uint16_t batteryMv = (uint16_t)((sumMv / 16.0f) * BATTERY_DIVIDER_RATIO + 0.5f);
  bool present = batteryMv >= BATTERY_PRESENT_MIN_MV;
  uint8_t percent = present ? batteryPercentFromMv(batteryMv) : 0;

  BatteryStatus status;
  status.mv = present ? batteryMv : 0;
  status.percent = percent;
  status.present = present;
  status.low = present && percent < LOW_BATTERY_PERCENT;
  status.otaAllowed = !present || percent >= OTA_MIN_BATTERY_PERCENT;
  return status;
}

String batteryStatusJson()
{
  BatteryStatus battery = readBatteryStatus();
  String json = "{\"mv\":" + String(battery.mv) +
    ",\"percent\":" + String(battery.percent) +
    ",\"present\":" + String(battery.present ? "true" : "false") +
    ",\"low\":" + String(battery.low ? "true" : "false") +
    ",\"otaAllowed\":" + String(battery.otaAllowed ? "true" : "false") + "}";
  return json;
}

String sanitizeUid(const String& uid)
{
  String out;
  for (size_t i = 0; i < uid.length(); i++)
  {
    char c = uid[i];
    if (c >= 'a' && c <= 'f') c -= 32;
    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F')) out += c;
  }
  return out;
}

String jsonEscape(const String& value)
{
  String out;
  out.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); i++)
  {
    char c = value[i];
    if (c == '"' || c == '\\')
    {
      out += '\\';
      out += c;
    }
    else if (c == '\n')
    {
      out += "\\n";
    }
    else if (c == '\r')
    {
      out += "\\r";
    }
    else
    {
      out += c;
    }
  }
  return out;
}

String bindingPathForUid(const String& uid)
{
  return "/" + sanitizeUid(uid) + ".bin";
}

String notePathForUid(const String& uid)
{
  return "/" + sanitizeUid(uid) + ".txt";
}

bool isBindingFileName(const String& name)
{
  String fileName = name;
  if (fileName.startsWith("/")) fileName.remove(0, 1);
  if (!fileName.endsWith(".bin")) return false;
  String uid = fileName.substring(0, fileName.length() - 4);
  return sanitizeUid(uid) == uid && uid.length() > 0;
}

String uidFromBindingFileName(const String& name)
{
  String fileName = name;
  if (fileName.startsWith("/")) fileName.remove(0, 1);
  if (!fileName.endsWith(".bin")) return "";
  return sanitizeUid(fileName.substring(0, fileName.length() - 4));
}

String readBindingNote(const String& uid)
{
  if (!fsReady) return "";
  String clean = sanitizeUid(uid);
  if (clean.length() == 0) return "";
  File file = LittleFS.open(notePathForUid(clean), "r");
  if (!file) return "";
  String note = file.readString();
  file.close();
  note.trim();
  if (note.length() > 64) note = note.substring(0, 64);
  return note;
}

bool saveBindingNote(const String& uid, const String& note)
{
  if (!fsReady) return false;
  String clean = sanitizeUid(uid);
  if (clean.length() == 0 || !LittleFS.exists(bindingPathForUid(clean))) return false;
  String trimmed = note;
  trimmed.trim();
  if (trimmed.length() > 64) trimmed = trimmed.substring(0, 64);
  if (trimmed.length() == 0)
  {
    LittleFS.remove(notePathForUid(clean));
    return true;
  }
  File file = LittleFS.open(notePathForUid(clean), "w");
  if (!file) return false;
  bool ok = file.print(trimmed) == trimmed.length();
  file.close();
  return ok;
}

bool bindingExists(const String& uid)
{
  if (!fsReady) return false;
  String clean = sanitizeUid(uid);
  return clean.length() > 0 && LittleFS.exists(bindingPathForUid(clean));
}

uint8_t countBindings()
{
  if (!fsReady) return 0;
  uint8_t count = 0;
  File root = LittleFS.open("/");
  File file = root.openNextFile();
  while (file)
  {
    if (!file.isDirectory() && isBindingFileName(file.name())) count++;
    file = root.openNextFile();
  }
  root.close();
  return count;
}

String bindingsJson()
{
  String json = "{\"max\":" + String(MAX_NFC_BINDINGS) +
    ",\"count\":" + String(countBindings()) +
    ",\"used\":" + String(LittleFS.usedBytes()) +
    ",\"total\":" + String(LittleFS.totalBytes()) +
    ",\"items\":[";

  bool first = true;
  File root = LittleFS.open("/");
  File file = root.openNextFile();
  while (file)
  {
    if (!file.isDirectory() && isBindingFileName(file.name()))
    {
      String uid = uidFromBindingFileName(file.name());
      size_t size = file.size();
      String mode = size > IMAGE_BYTES + 1 ? "bwr" : "bw";
      String note = readBindingNote(uid);
      if (!first) json += ",";
      json += "{\"uid\":\"" + uid + "\",\"bytes\":" + String(size) + ",\"mode\":\"" + mode + "\",\"note\":\"" + jsonEscape(note) + "\"}";
      first = false;
    }
    file = root.openNextFile();
  }
  root.close();
  json += "]}";
  return json;
}

bool deleteBinding(const String& uid)
{
  if (!fsReady) return false;
  String clean = sanitizeUid(uid);
  if (clean.length() == 0) return false;
  bool ok = LittleFS.remove(bindingPathForUid(clean));
  LittleFS.remove(notePathForUid(clean));
  return ok;
}

String uidToString(const MFRC522::Uid& uid)
{
  const char hex[] = "0123456789ABCDEF";
  String out;
  for (byte i = 0; i < uid.size; i++)
  {
    byte v = uid.uidByte[i];
    out += hex[v >> 4];
    out += hex[v & 0x0F];
  }
  return out;
}

#if RFID_DEBUG
byte readRc522SpiVersionRaw()
{
  digitalWrite(EPD_CS, HIGH);
  digitalWrite(NFC_SPI_CS, HIGH);
  delayMicroseconds(5);

  SPI.beginTransaction(SPISettings(1000000u, MSBFIRST, SPI_MODE0));
  digitalWrite(NFC_SPI_CS, LOW);
  delayMicroseconds(5);
  SPI.transfer(0x80 | (0x37 << 1)); // VersionReg, read command.
  byte version = SPI.transfer(0x00);
  digitalWrite(NFC_SPI_CS, HIGH);
  SPI.endTransaction();
  return version;
}

void printNfcSpiLineDiagnostics()
{
  pinMode(NFC_SPI_MISO, INPUT);
  delay(2);
  int misoInput = digitalRead(NFC_SPI_MISO);

  pinMode(NFC_SPI_MISO, INPUT_PULLUP);
  delay(2);
  int misoPullup = digitalRead(NFC_SPI_MISO);

  pinMode(NFC_SPI_MISO, INPUT_PULLDOWN);
  delay(2);
  int misoPulldown = digitalRead(NFC_SPI_MISO);

  pinMode(NFC_SPI_MISO, INPUT);

  Serial.print("NFC SPI MISO diag: input=");
  Serial.print(misoInput);
  Serial.print(" pullup=");
  Serial.print(misoPullup);
  Serial.print(" pulldown=");
  Serial.println(misoPulldown);
}
#endif

bool saveBindingImage(const String& uid)
{
  if (!fsReady) return false;
  String clean = sanitizeUid(uid);
  if (clean.length() == 0) return false;
  if (!bindingExists(clean) && countBindings() >= MAX_NFC_BINDINGS)
  {
    uploadError = "NFC binding storage is full.";
    return false;
  }

  File file = LittleFS.open(bindingPathForUid(clean), "w");
  if (!file) return false;

  uint8_t header = uploadHasRed ? 1 : 0;
  bool ok = file.write(&header, 1) == 1;
  ok = ok && file.write(blackBuffer, IMAGE_BYTES) == IMAGE_BYTES;
  if (uploadHasRed) ok = ok && file.write(redBuffer, IMAGE_BYTES) == IMAGE_BYTES;
  file.close();
  return ok;
}

bool loadBindingImage(const String& uid)
{
  if (!fsReady) return false;
  String clean = sanitizeUid(uid);
  if (clean.length() == 0) return false;

  File file = LittleFS.open(bindingPathForUid(clean), "r");
  if (!file) return false;

  int header = file.read();
  if (header < 0)
  {
    file.close();
    return false;
  }

  uploadHasRed = header == 1;
  size_t blackRead = file.read(blackBuffer, IMAGE_BYTES);
  size_t redRead = 0;
  if (uploadHasRed) redRead = file.read(redBuffer, IMAGE_BYTES);
  file.close();

  if (blackRead != IMAGE_BYTES) return false;
  if (uploadHasRed && redRead != IMAGE_BYTES) return false;
  if (!uploadHasRed) memset(redBuffer, 0, sizeof(redBuffer));
  return true;
}

void setupFileSystem()
{
  fsReady = LittleFS.begin(true);
  Serial.println(fsReady ? "LittleFS ready" : "LittleFS failed");
}

void setupNfc()
{
  activeNfcReader = nullptr;
  activeNfcBus = NfcBus::None;
  nfcReady = false;

  pinMode(NFC_SPI_RST, OUTPUT);
  digitalWrite(NFC_SPI_RST, HIGH);
  pinMode(NFC_SPI_CS, OUTPUT);
  digitalWrite(NFC_SPI_CS, HIGH);
  pinMode(NFC_SPI_MISO, INPUT);
  pinMode(EPD_CS, OUTPUT);
  digitalWrite(EPD_CS, HIGH);

  Wire.begin(NFC_SDA, NFC_SCL);
  Wire.setClock(400000);

  if (nfcI2cReader.PCD_Init())
  {
    nfcReady = true;
    activeNfcReader = &nfcI2cReader;
    activeNfcBus = NfcBus::I2C;
    lastNfcStatus = "NFC 已就緒，請刷卡（I2C）";
    Serial.println("NFC ready on I2C address 0x28");
    return;
  }

  Serial.println("NFC I2C not detected, trying SPI...");
#if RFID_DEBUG
  printNfcSpiLineDiagnostics();
#endif

  // Keep the shared SPI bus on the pins used by the e-paper and the SPI RFID module.
  SPI.begin(EPD_SCK, NFC_SPI_MISO, EPD_MOSI, NFC_SPI_CS);
#if RFID_DEBUG
  Serial.print("NFC SPI idle: MISO=");
  Serial.print(digitalRead(NFC_SPI_MISO));
  Serial.print(" SS=");
  Serial.print(digitalRead(NFC_SPI_CS));
  Serial.print(" RST=");
  Serial.println(digitalRead(NFC_SPI_RST));
#endif

  // Some RC522 boards need a real reset edge before they answer over SPI.
  digitalWrite(NFC_SPI_RST, LOW);
  delay(10);
  digitalWrite(NFC_SPI_RST, HIGH);
  delay(50);
#if RFID_DEBUG
  printNfcSpiLineDiagnostics();

  byte rawBeforeInit = readRc522SpiVersionRaw();
  Serial.print("NFC SPI VersionReg before init: 0x");
  if (rawBeforeInit < 0x10) Serial.print("0");
  Serial.println(rawBeforeInit, HEX);
#endif

  if (nfcSpiReader.PCD_Init())
  {
    nfcReady = true;
    activeNfcReader = &nfcSpiReader;
    activeNfcBus = NfcBus::SPI;
    lastNfcStatus = "NFC 已就緒，請刷卡（SPI）";
    Serial.println("NFC ready on SPI");
#if RFID_DEBUG
    Serial.print("NFC SPI pins: SCK=");
    Serial.print(EPD_SCK);
    Serial.print(" MOSI=");
    Serial.print(EPD_MOSI);
    Serial.print(" MISO=");
    Serial.print(NFC_SPI_MISO);
    Serial.print(" SS=");
    Serial.print(NFC_SPI_CS);
    Serial.print(" RST=");
    Serial.println(NFC_SPI_RST);
#endif
  }
  else
  {
#if RFID_DEBUG
    byte spiVersion = readRc522SpiVersionRaw();
    Serial.print("NFC SPI VersionReg after init: 0x");
    if (spiVersion < 0x10) Serial.print("0");
    Serial.println(spiVersion, HEX);
#endif
    lastNfcStatus = "NFC 初始化失敗，請檢查 I2C 或 SPI 接線";
    Serial.println("NFC init failed on I2C and SPI");
  }
}

void showUploadedImage();

void handleNfcCard(const String& uid)
{
  lastNfcUid = uid;
  lastNfcSeenMs = millis();
  markActivity();

  Serial.print("NFC UID: ");
  Serial.println(uid);

  if (!bindingExists(uid))
  {
    lastNfcStatus = "UID " + uid + " 尚未綁定圖片";
    return;
  }

  if (uid == lastDisplayedUid && millis() - lastNfcDisplayMs < SAME_UID_REFRESH_GUARD_MS)
  {
    lastNfcStatus = "UID " + uid + " 已讀取，避免重複刷新";
    return;
  }

  if (loadBindingImage(uid))
  {
    lastNfcStatus = "UID " + uid + " 已載入並刷新電子紙";
    lastDisplayedUid = uid;
    lastNfcDisplayMs = millis();
    Serial.println("NFC binding found. Updating display.");
    showUploadedImage();
  }
  else
  {
    lastNfcStatus = "UID " + uid + " 圖片讀取失敗";
  }
}

void pollNfc()
{
  if (!nfcReady || activeNfcReader == nullptr || displayBusy) return;
  if (millis() - lastNfcPollMs < NFC_POLL_MS) return;
  lastNfcPollMs = millis();

  if (!activeNfcReader->PICC_IsNewCardPresent() || !activeNfcReader->PICC_ReadCardSerial()) return;

  String uid = uidToString(activeNfcReader->uid);
  activeNfcReader->PICC_HaltA();
  activeNfcReader->PCD_StopCrypto1();
  handleNfcCard(uid);
}

void disableWireless(const char* reason)
{
  if (!wirelessEnabled)
  {
    return;
  }

  Serial.print("Turning WiFi off: ");
  Serial.println(reason);

  if (mdnsStarted)
  {
    MDNS.end();
    mdnsStarted = false;
  }

  if (apModeActive)
  {
    dnsServer.stop();
    apModeActive = false;
  }

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  wifiStaConnected = false;
  wirelessEnabled = false;
  delay(100);

  Serial.println("WiFi is off. NFC and e-paper remain active; press GPIO5 to enable WiFi again.");
}

void setupNetwork();

void enableWireless(const char* reason)
{
  if (wirelessEnabled)
  {
    return;
  }

  Serial.print("Turning WiFi on: ");
  Serial.println(reason);
  setupNetwork();
  markActivity();
}

void checkWirelessButton()
{
  int buttonState = digitalRead(WAKE_BUTTON);
  if (buttonState == HIGH)
  {
    if (buttonLowSinceMs != 0 && wakeButtonReleased)
    {
      unsigned long pressMs = millis() - buttonLowSinceMs;
      if (pressMs > 1200)
      {
        if (wirelessEnabled) disableWireless("GPIO5 long press");
      }
      else if (!wirelessEnabled)
      {
        enableWireless("GPIO5 short press");
      }
    }
    wakeButtonReleased = true;
    buttonLowSinceMs = 0;
  }
  else if (wakeButtonReleased)
  {
    if (buttonLowSinceMs == 0) buttonLowSinceMs = millis();
  }

  if (millis() - lastActivityMs >= SLEEP_TIMEOUT_MS) disableWireless("5 minute timeout");
}
void drawCenteredText(const char* text, int16_t y)
{
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(text, 0, y, &x1, &y1, &w, &h);
  int16_t x = (EPD_WIDTH - w) / 2;
  display.setCursor(x, y);
  display.print(text);
}

void drawLowBatteryIcon(bool useRed)
{
  const int16_t x = EPD_WIDTH - 42;
  const int16_t y = 8;
  const int16_t w = 30;
  const int16_t h = 16;
  uint16_t alertColor = useRed ? GxEPD_RED : GxEPD_BLACK;

  display.fillRect(x - 2, y - 2, w + 8, h + 4, GxEPD_WHITE);
  display.drawRect(x, y, w, h, GxEPD_BLACK);
  display.fillRect(x + w, y + 5, 3, 6, GxEPD_BLACK);
  display.fillRect(x + 3, y + 3, 6, h - 6, alertColor);
  display.drawLine(x + 17, y + 3, x + 17, y + 9, alertColor);
  display.drawPixel(x + 17, y + 12, alertColor);
  display.drawPixel(x + 18, y + 12, alertColor);
}

void showStatusScreen(const IPAddress& ip)
{
  displayBusy = true;
  BatteryStatus battery = readBatteryStatus();
  display.setRotation(0);
  display.setFullWindow();
  display.firstPage();
  do
  {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);
    display.setFont(&FreeMonoBold9pt7b);
    drawCenteredText(apModeActive ? "Setup AP" : (wifiStaConnected ? "WiFi connected" : "Network off"), 48);
    drawCenteredText(ip.toString().c_str(), 86);
    drawCenteredText(apModeActive ? "Open 192.168.0.1" : "Open epaper.local", 140);
    if (battery.low) drawLowBatteryIcon(true);
  } while (display.nextPage());
  display.powerOff();
  displayBusy = false;
}

void showUploadedImage()
{
  displayBusy = true;
  BatteryStatus battery = readBatteryStatus();
  display.setRotation(0);
  display.setFullWindow();
  display.firstPage();
  do
  {
    display.fillScreen(GxEPD_WHITE);
    display.drawBitmap(0, 0, blackBuffer, EPD_WIDTH, EPD_HEIGHT, GxEPD_BLACK);
    if (uploadHasRed)
    {
      display.drawBitmap(0, 0, redBuffer, EPD_WIDTH, EPD_HEIGHT, GxEPD_RED);
    }
    if (battery.low) drawLowBatteryIcon(uploadHasRed);
  } while (display.nextPage());
  display.powerOff();
  displayBusy = false;
}

void resetUploadState()
{
  uploadOffset = 0;
  uploadFailed = false;
  uploadHasRed = false;
  uploadError = "";
  memset(blackBuffer, 0, sizeof(blackBuffer));
  memset(redBuffer, 0, sizeof(redBuffer));
}

void copyUploadData(const uint8_t* data, size_t length)
{
  size_t copied = 0;
  while (copied < length)
  {
    size_t absoluteOffset = uploadOffset + copied;
    if (absoluteOffset >= MAX_UPLOAD_BYTES)
    {
      uploadFailed = true;
      uploadError = "Upload is too large.";
      return;
    }

    uint8_t* layer = absoluteOffset < IMAGE_BYTES ? blackBuffer : redBuffer;
    size_t layerOffset = absoluteOffset < IMAGE_BYTES ? absoluteOffset : absoluteOffset - IMAGE_BYTES;
    size_t room = IMAGE_BYTES - layerOffset;
    size_t remaining = length - copied;
    size_t chunk = remaining < room ? remaining : room;

    memcpy(layer + layerOffset, data + copied, chunk);
    copied += chunk;
  }
  uploadOffset += length;
}

String makeApSsid()
{
  uint32_t chip = (uint32_t)(ESP.getEfuseMac() & 0xFFFFFF);
  char suffix[7];
  snprintf(suffix, sizeof(suffix), "%06lX", (unsigned long)chip);
  return String(AP_SSID_PREFIX) + suffix;
}

void startConfigAp()
{
  apSsid = makeApSsid();
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  WiFi.softAPConfig(AP_IP, AP_GATEWAY, AP_SUBNET);
  WiFi.softAP(apSsid.c_str());
  dnsServer.start(DNS_PORT, "*", AP_IP);

  wifiStaConnected = false;
  wirelessEnabled = true;
  apModeActive = true;

  Serial.print("Config AP SSID: ");
  Serial.println(apSsid);
  Serial.print("Config AP IP: ");
  Serial.println(WiFi.softAPIP());
}

void redirectToRoot()
{
  markActivity();
  String target = wifiStaConnected ? String("http://") + WiFi.localIP().toString() + "/" :
    (apModeActive ? String("http://") + AP_IP.toString() + "/" : "/");
  server.sendHeader("Location", target, true);
  server.send(302, "text/plain", "");
}

void loadWifiConfig()
{
  preferences.begin("wifi", true);
  wifiSsid = preferences.getString("ssid", DEFAULT_WIFI_SSID);
  wifiPassword = preferences.getString("pass", DEFAULT_WIFI_PASSWORD);
  preferences.end();
  wifiSsid.trim();
}

bool saveWifiConfig(const String& ssid, const String& password)
{
  String cleanSsid = ssid;
  cleanSsid.trim();
  if (cleanSsid.length() == 0 || cleanSsid.length() > 32) return false;
  if (password.length() > 63) return false;

  preferences.begin("wifi", false);
  bool ok = preferences.putString("ssid", cleanSsid) > 0;
  ok = preferences.putString("pass", password) > 0 && ok;
  preferences.end();

  if (ok)
  {
    wifiSsid = cleanSsid;
    wifiPassword = password;
  }
  return ok;
}

String wifiConfigJson()
{
  String ip = wifiStaConnected ? WiFi.localIP().toString() : (apModeActive ? AP_IP.toString() : "");
  String mode = wifiStaConnected ? "sta" : (apModeActive ? "ap" : "off");
  String json = "{\"ssid\":\"" + jsonEscape(wifiSsid) +
    "\",\"connected\":" + String(wifiStaConnected ? "true" : "false") +
    ",\"apMode\":" + String(apModeActive ? "true" : "false") +
    ",\"mode\":\"" + mode + "\"" +
    ",\"apSsid\":\"" + jsonEscape(apSsid) + "\"" +
    ",\"ip\":\"" + ip + "\"" +
    ",\"mdns\":\"http://" + String(MDNS_NAME) + ".local\"" +
    ",\"passwordSet\":" + String(wifiPassword.length() ? "true" : "false") + "}";
  return json;
}

String wifiScanJson()
{
  int networkCount = WiFi.scanNetworks(false, true);
  String json = "{\"count\":" + String(networkCount < 0 ? 0 : networkCount) + ",\"items\":[";
  if (networkCount > 0)
  {
    uint8_t emitted = 0;
    for (int i = 0; i < networkCount && emitted < 20; i++)
    {
      String ssid = WiFi.SSID(i);
      if (ssid.length() == 0) continue;

      bool duplicate = false;
      for (int j = 0; j < i; j++)
      {
        if (WiFi.SSID(j) == ssid)
        {
          duplicate = true;
          break;
        }
      }
      if (duplicate) continue;

      if (emitted > 0) json += ",";
      json += "{\"ssid\":\"" + jsonEscape(ssid) +
        "\",\"rssi\":" + String(WiFi.RSSI(i)) +
        ",\"secure\":" + String(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "false" : "true") + "}";
      emitted++;
    }
  }
  json += "]}";
  WiFi.scanDelete();
  return json;
}

bool isAllowedFirmwareUrl(const String& url)
{
  return url.startsWith("https://github.com/fongying/esp32-epaper-nfc-display/releases/download/") ||
    url.startsWith("https://raw.githubusercontent.com/fongying/esp32-epaper-nfc-display/");
}

bool applyFirmwareUpdate(const String& firmwareUrl, const String& sha256)
{
  (void)sha256;
  BatteryStatus battery = readBatteryStatus();
  if (battery.present && battery.percent < OTA_MIN_BATTERY_PERCENT)
  {
    Serial.print("OTA rejected: battery low ");
    Serial.print(battery.percent);
    Serial.println("%.");
    return false;
  }
  if (!wifiStaConnected)
  {
    Serial.println("OTA rejected: STA WiFi is not connected.");
    return false;
  }
  if (!isAllowedFirmwareUrl(firmwareUrl))
  {
    Serial.println("OTA rejected: firmware URL is not allowed.");
    return false;
  }

  Serial.print("OTA URL: ");
  Serial.println(firmwareUrl);

  if (mdnsStarted)
  {
    MDNS.end();
    mdnsStarted = false;
  }
  display.powerOff();

  WiFiClientSecure client;
  client.setInsecure();
  httpUpdate.rebootOnUpdate(true);
  httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

  t_httpUpdate_return result = httpUpdate.update(client, firmwareUrl);
  if (result == HTTP_UPDATE_FAILED)
  {
    Serial.printf("OTA failed: %d %s\n", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
    return false;
  }
  if (result == HTTP_UPDATE_NO_UPDATES)
  {
    Serial.println("OTA no update.");
    return false;
  }

  Serial.println("OTA update OK. Rebooting.");
  return true;
}

void setupNetwork()
{
  loadWifiConfig();
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);

  if (wifiSsid.length() == 0)
  {
    Serial.println("WiFi SSID is empty; starting config AP.");
    startConfigAp();
    return;
  }

  WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());

  Serial.print("Connecting to WiFi");
  Serial.print(" SSID=");
  Serial.print(wifiSsid);
  unsigned long startMs = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startMs < WIFI_CONNECT_TIMEOUT_MS)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  wifiStaConnected = WiFi.status() == WL_CONNECTED;
  if (wifiStaConnected)
  {
    wirelessEnabled = true;
    apModeActive = false;
    Serial.print("STA IP: ");
    Serial.println(WiFi.localIP());
  }
  else
  {
    WiFi.disconnect(true);
    Serial.println("STA WiFi not connected after 30 seconds; starting config AP.");
    startConfigAp();
  }
}

void setupServer()
{
  server.on("/", HTTP_GET, []()
  {
    server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
  });

  // Captive portal 偵測用路由：手機或電腦連上 AP 後會打這些網址判斷是否需要跳出登入頁。
  server.on("/generate_204", HTTP_GET, redirectToRoot);
  server.on("/gen_204", HTTP_GET, redirectToRoot);
  server.on("/hotspot-detect.html", HTTP_GET, redirectToRoot);
  server.on("/library/test/success.html", HTTP_GET, redirectToRoot);
  server.on("/ncsi.txt", HTTP_GET, redirectToRoot);
  server.on("/connecttest.txt", HTTP_GET, redirectToRoot);
  server.on("/fwlink", HTTP_GET, redirectToRoot);

  server.on("/nfc", HTTP_GET, []()
  {
    server.send_P(200, "text/html; charset=utf-8", NFC_INDEX_HTML);
  });

  server.on("/config", HTTP_GET, []()
  {
    server.send_P(200, "text/html; charset=utf-8", CONFIG_INDEX_HTML);
  });

  server.on("/uid", HTTP_GET, []()
  {
    markActivity();
    String uid = sanitizeUid(lastNfcUid);
    String bus = activeNfcBus == NfcBus::I2C ? "i2c" : (activeNfcBus == NfcBus::SPI ? "spi" : "none");
    String json = "{\"ready\":" + String(nfcReady ? "true" : "false") +
      ",\"uid\":\"" + uid + "\"" +
      ",\"bound\":" + String(bindingExists(uid) ? "true" : "false") +
      ",\"bus\":\"" + bus + "\"" +
      ",\"status\":\"" + jsonEscape(lastNfcStatus) + "\"}";
    server.send(200, "application/json", json);
  });

  server.on("/bindings", HTTP_GET, []()
  {
    markActivity();
    server.send(200, "application/json", bindingsJson());
  });

  server.on("/version", HTTP_GET, []()
  {
    markActivity();
    String json = "{\"version\":\"" + String(FIRMWARE_VERSION) +
      "\",\"build\":\"" + String(FIRMWARE_BUILD) +
      "\",\"device\":\"" + String(FIRMWARE_DEVICE) + "\"" +
      ",\"staConnected\":" + String(wifiStaConnected ? "true" : "false") +
      ",\"apMode\":" + String(apModeActive ? "true" : "false") +
      ",\"manifestUrl\":\"" + jsonEscape(String(UPDATE_MANIFEST_URL)) + "\"}";
    server.send(200, "application/json", json);
  });

  server.on("/battery", HTTP_GET, []()
  {
    markActivity();
    server.send(200, "application/json", batteryStatusJson());
  });

  server.on("/update/apply", HTTP_POST, []()
  {
    markActivity();
    if (!wifiStaConnected)
    {
      server.send(409, "text/plain", "OTA requires STA WiFi with internet access.");
      return;
    }
    BatteryStatus battery = readBatteryStatus();
    if (battery.present && battery.percent < OTA_MIN_BATTERY_PERCENT)
    {
      server.send(409, "text/plain", "電量低於 20%，請充電後再更新。");
      return;
    }

    String url = server.arg("url");
    String version = server.arg("version");
    String sha256 = server.arg("sha256");
    url.trim();
    version.trim();
    sha256.trim();

    if (url.length() == 0)
    {
      server.send(400, "text/plain", "Missing firmware URL.");
      return;
    }
    if (!isAllowedFirmwareUrl(url))
    {
      server.send(400, "text/plain", "Firmware URL is not allowed.");
      return;
    }

    server.send(200, "text/plain", "OTA started. Device will reboot if update succeeds.");
    delay(200);
    applyFirmwareUpdate(url, sha256);
  });

  server.on("/wifi", HTTP_GET, []()
  {
    markActivity();
    server.send(200, "application/json", wifiConfigJson());
  });

  server.on("/wifi/scan", HTTP_GET, []()
  {
    markActivity();
    server.send(200, "application/json", wifiScanJson());
  });

  server.on("/wifi", HTTP_POST, []()
  {
    markActivity();
    String ssid = server.arg("ssid");
    String password = server.arg("password");
    if (!saveWifiConfig(ssid, password))
    {
      server.send(400, "text/plain", "Invalid WiFi setting.");
      return;
    }

    server.send(200, "text/plain", "WiFi setting saved. Device will restart.");
    restartPending = true;
    restartAtMs = millis() + 1200;
  });

  server.on("/note", HTTP_POST, []()
  {
    markActivity();
    String uid = sanitizeUid(server.arg("uid"));
    String note = server.arg("note");
    if (uid.length() == 0)
    {
      server.send(400, "text/plain", "Missing UID.");
      return;
    }
    if (!bindingExists(uid))
    {
      server.send(404, "text/plain", "UID binding not found.");
      return;
    }
    if (!saveBindingNote(uid, note))
    {
      server.send(500, "text/plain", "Failed to save note.");
      return;
    }
    server.send(200, "text/plain", "UID " + uid + " 註解已儲存。");
  });

  server.on("/show", HTTP_POST, []()
  {
    markActivity();
    String uid = sanitizeUid(server.arg("uid"));
    if (uid.length() == 0)
    {
      server.send(400, "text/plain", "Missing UID.");
      return;
    }
    if (!bindingExists(uid))
    {
      server.send(404, "text/plain", "UID binding not found.");
      return;
    }
    if (!loadBindingImage(uid))
    {
      server.send(500, "text/plain", "Failed to load UID image.");
      return;
    }

    lastNfcUid = uid;
    lastDisplayedUid = uid;
    lastNfcDisplayMs = millis();
    lastNfcStatus = "UID " + uid + " 已從管理頁顯示到電子紙";
    server.send(200, "text/plain", "UID " + uid + " 已送到電子紙。");
    delay(20);
    showUploadedImage();
  });

  server.on("/upload", HTTP_POST,
    []()
    {
      markActivity();
      if (uploadFailed)
      {
        server.send(400, "text/plain", uploadError.length() ? uploadError : "Upload failed.");
        return;
      }
      if (uploadOffset == IMAGE_BYTES)
      {
        uploadHasRed = false;
      }
      else if (uploadOffset == MAX_UPLOAD_BYTES)
      {
        uploadHasRed = true;
      }
      else
      {
        server.send(400, "text/plain", "Invalid image size.");
        return;
      }

      server.send(200, "text/plain", uploadHasRed ? "Uploaded 3-color image. Updating display..." : "Uploaded BW image. Updating display...");
      delay(20);
      Serial.println(uploadHasRed ? "Display full update via BW+Red" : "Display full update via BW");
      showUploadedImage();
    },
    []()
    {
      HTTPUpload& upload = server.upload();
      if (upload.status == UPLOAD_FILE_START)
      {
        markActivity();
        resetUploadState();
        Serial.println("Upload start");
      }
      else if (upload.status == UPLOAD_FILE_WRITE)
      {
        markActivity();
        if (!uploadFailed)
        {
          copyUploadData(upload.buf, upload.currentSize);
        }
      }
      else if (upload.status == UPLOAD_FILE_END)
      {
        Serial.print("Upload bytes: ");
        Serial.println(uploadOffset);
      }
      else if (upload.status == UPLOAD_FILE_ABORTED)
      {
        uploadFailed = true;
        uploadError = "Upload aborted.";
      }
    });

  server.on("/bind", HTTP_POST,
    []()
    {
      markActivity();
      bindTargetUid = sanitizeUid(server.arg("uid"));
      if (bindTargetUid.length() == 0)
      {
        server.send(400, "text/plain", "Missing UID.");
        return;
      }
      if (uploadFailed)
      {
        server.send(400, "text/plain", uploadError.length() ? uploadError : "Bind upload failed.");
        return;
      }
      if (uploadOffset == IMAGE_BYTES)
      {
        uploadHasRed = false;
      }
      else if (uploadOffset == MAX_UPLOAD_BYTES)
      {
        uploadHasRed = true;
      }
      else
      {
        server.send(400, "text/plain", "Invalid image size.");
        return;
      }

      if (!saveBindingImage(bindTargetUid))
      {
        server.send(500, "text/plain", uploadError.length() ? uploadError : "Failed to save NFC binding.");
        return;
      }
      lastNfcStatus = "UID " + bindTargetUid + " 已綁定目前畫布";
      server.send(200, "text/plain", "UID " + bindTargetUid + " 已綁定目前畫布。");
    },
    []()
    {
      HTTPUpload& upload = server.upload();
      if (upload.status == UPLOAD_FILE_START)
      {
        markActivity();
        resetUploadState();
        Serial.println("Bind upload start");
      }
      else if (upload.status == UPLOAD_FILE_WRITE)
      {
        markActivity();
        if (!uploadFailed)
        {
          copyUploadData(upload.buf, upload.currentSize);
        }
      }
      else if (upload.status == UPLOAD_FILE_END)
      {
        Serial.print("Bind upload bytes: ");
        Serial.println(uploadOffset);
      }
      else if (upload.status == UPLOAD_FILE_ABORTED)
      {
        uploadFailed = true;
        uploadError = "Bind upload aborted.";
      }
    });

  server.on("/bind", HTTP_DELETE, []()
  {
    markActivity();
    String uid = sanitizeUid(server.arg("uid"));
    if (uid.length() == 0)
    {
      server.send(400, "text/plain", "Missing UID.");
      return;
    }
    if (!bindingExists(uid))
    {
      server.send(404, "text/plain", "UID binding not found.");
      return;
    }
    if (!deleteBinding(uid))
    {
      server.send(500, "text/plain", "Failed to delete UID binding.");
      return;
    }
    if (uid == lastDisplayedUid) lastDisplayedUid = "";
    lastNfcStatus = "UID " + uid + " 綁定已刪除";
    server.send(200, "text/plain", "UID " + uid + " 綁定已刪除。");
  });

  server.onNotFound([]()
  {
    // AP captive portal 情境下，未知網址多半是系統偵測用網域，直接導回主頁。
    if (server.method() == HTTP_GET)
    {
      redirectToRoot();
      return;
    }
    server.send(404, "text/plain", "Not found");
  });

  server.begin();
}

void setup()
{
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("ESP32-C3 E-Paper notice designer");

  pinMode(WAKE_BUTTON, INPUT_PULLUP);
  pinMode(BATTERY_ADC_PIN, INPUT);
  analogSetPinAttenuation(BATTERY_ADC_PIN, ADC_11db);
  markActivity();
  setupFileSystem();

  SPI.begin(EPD_SCK, NFC_SPI_MISO, EPD_MOSI, EPD_CS);
  display.init(115200, true, 2, false);

  setupNetwork();
  if (wifiStaConnected && MDNS.begin(MDNS_NAME))
  {
    mdnsStarted = true;
    MDNS.addService("http", "tcp", 80);
    Serial.println("mDNS: http://epaper.local");
  }
  else if (wifiStaConnected)
  {
    Serial.println("mDNS failed");
  }

  setupServer();
  showStatusScreen(wifiStaConnected ? WiFi.localIP() : (apModeActive ? AP_IP : IPAddress(0, 0, 0, 0)));
  setupNfc();
  lastDisplayedUid = "";
  Serial.println("setup done");
}

void loop()
{
  if (apModeActive) dnsServer.processNextRequest();
  if (wirelessEnabled) server.handleClient();
  pollNfc();
  checkWirelessButton();
  if (restartPending && millis() >= restartAtMs)
  {
    Serial.println("Restarting to apply WiFi settings.");
    Serial.flush();
    ESP.restart();
  }
}
