# E-Paper Notice Designer

ESP32-C3 + GxEPD2 sketch for a 4.2 inch 400x300 three-color e-paper panel.

Confirmed working panel driver:

```cpp
GxEPD2_420c_GDEY042Z98
```

Wiring:

```text
E-paper CS   -> GPIO7
E-paper DC   -> GPIO3
E-paper RST  -> GPIO1
E-paper BUSY -> GPIO0
E-paper SCK  -> GPIO4
E-paper SDA  -> GPIO6

I2C NFC VCC  -> 3V3
I2C NFC GND  -> GND
I2C NFC SDA  -> GPIO8
I2C NFC SCL  -> GPIO9

WiFi-off button -> GPIO5 and GND
```

Network behavior:

- Tries Wi-Fi STA mode first using the SSID/password stored in ESP32 NVS
- If STA does not connect within 30 seconds, starts a configuration AP
- Configuration AP SSID is `ESP32_e-paper_4.2V2_<random>` and IP is `192.168.0.1`
- The AP runs a DNS captive portal so phones should open the web page automatically
- First boot defaults to `ICSLab` / `CSIEB323`
- The `/config` page provides Wi-Fi scanning and settings; saving a new SSID/password restarts the ESP32
- Wi-Fi settings survive normal firmware updates and OTA because they are stored in NVS
- Wi-Fi settings are erased only when the full flash/NVS partition is erased
- Prints the assigned IP address in the serial monitor
- Starts mDNS at `http://epaper.local` when Wi-Fi is connected
- NFC remains active in STA mode, AP mode, and after Wi-Fi is turned off

Firmware update check:

- Current firmware version is exposed at `GET /version`
- The designer page checks for a newer manifest when it opens
- `UPDATE_MANIFEST_URL` points to the GitHub manifest:
  `https://raw.githubusercontent.com/fongying/esp32-epaper-nfc-display/main/firmware/manifest.json`
- Open `http://epaper.local/?mockUpdate=1` or `http://<STA-IP>/?mockUpdate=1` to force a test update prompt after flashing
- Opening `web/index.html` locally also uses a mock update prompt for UI testing
- When the user accepts an update, the browser calls `POST /update/apply`
- OTA requires STA Wi-Fi with internet access; AP configuration mode cannot download updates
- The manifest must provide `firmware_url`, normally a GitHub Release `.bin` URL
- Arduino IDE / PlatformIO partition scheme must include OTA app partitions
- Do not use full flash erase for normal updates, or NVS Wi-Fi settings will be erased
- The current OTA implementation uses HTTPS with GitHub redirect support; SHA256 is kept in the manifest for future verification but is not enforced in this build

Behavior:

- Shows AP connection information on the e-paper
- Serves a Traditional Chinese light theme web designer page
- Supports `Black / White` and `Black / White / Red` modes
- Lets the user upload a background image, add text, drag items, and add simple shapes
- Lets the user fill the canvas background with white, black, or red
- Lets the selected text or shape change color to white, black, or red from the property panel
- Lets the selected text or shape rotate left, rotate right, and mirror horizontally
- Red objects render as black in `Black / White` mode and as red in `Black / White / Red` mode
- The browser preview is quantized to the same white/black/red output that will be sent to the e-paper
- Browser renders the final 400x300 design and sends 1-bit bitmap data to the ESP32
- Black/white mode uploads 15000 bytes
- Black/white/red mode uploads 30000 bytes: black layer first, red layer second
- Reads NFC UID through I2C using `Arduino_MFRC522v2`
- Shows the latest NFC UID on the web page
- Lets the current 400x300 canvas bind to the latest UID
- Saves UID bindings in LittleFS as `/<UID>.bin`
- Limits NFC bindings to 20 saved images
- Provides `/nfc` as a binding management page
- Refreshes the e-paper automatically when a bound UID is scanned
- Turns Wi-Fi off after 5 minutes of inactivity so NFC can keep running
- Holding the GPIO5 button for about 1.2 seconds turns Wi-Fi off
- When Wi-Fi is off, short-press GPIO5 to turn Wi-Fi on again
- If Wi-Fi cannot reconnect within 30 seconds, the device starts AP configuration mode

NFC binding flow:

1. Connect your phone or computer to the same `ICSLab` network.
2. Open `http://epaper.local` or the STA IP shown in the serial monitor.
3. Tap an IC card on the NFC reader and wait for the UID to appear in the NFC section.
4. Design the canvas normally.
5. Click `綁定目前畫布`.
6. Tap the same card again later; the saved image will be loaded from LittleFS and shown on the e-paper.

Notes:

- The I2C NFC address is set to `0x28`.
- GPIO8/GPIO9 are used only for NFC I2C in this sketch.
- GPIO5 uses the ESP32 internal pull-up, so the button should be normally open and short GPIO5 to GND when pressed.
- After Wi-Fi is turned off, short-press GPIO5 to reconnect Wi-Fi. NFC card scanning remains active.

Open `epaper_image_upload.ino` in Arduino IDE and upload to the ESP32-C3.

Web UI editing:

```text
web/index.html      -> 可直接用瀏覽器開啟檢查排版
web/nfc.html        -> NFC 綁定管理頁，可直接用瀏覽器開啟檢查排版
web_index.h         -> Arduino 實際燒入用的 HTML header
nfc_index.h         -> Arduino 實際燒入用的 NFC 管理頁 header
tools/export_web_header.ps1 -> 將 web/index.html 與 web/nfc.html 同步成 Arduino header
```

修改網頁時先編輯 `web/index.html`。版面確認完成後，在專案根目錄執行：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\export_web_header.ps1
```

再回到 Arduino IDE 燒入。`epaper_image_upload.ino` 會透過 `#include "web_index.h"` 與 `#include "nfc_index.h"` 使用同步後的網頁內容。

NFC management page:

- Open `http://epaper.local/nfc`
- Shows the current binding count, maximum count, and LittleFS storage usage
- Lists each bound UID with mode and file size
- Adds an editable note for each bound UID
- Shows a bound image on the e-paper without tapping the NFC card
- Deletes unused UID bindings
- The main designer page has an `NFC 管理` link to this page
