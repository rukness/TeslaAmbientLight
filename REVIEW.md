# Tesla Ambient Light — Code Review & Suggestions

## Parts Needed

| Component | Qty | Purpose |
|---|---|---|
| ESP32 (with USB) | 5 | 1 master + 4 door modules |
| MCP2515 CAN module | 2 | Vehicle CAN + Chassis CAN (SPI) |
| DC-DC converter (12V→5V) | 1+ | Power supply for ESP32s and LED strips |
| Tesla 6-pin CAN connector | 1-2 | Tap into VCAN + CCAN under glovebox |
| Tesla Diagnostic Breakout | 1 (optional) | Power from console socket without cutting wires |
| WS2812B LED strip (136 pixels) | 2 | Front door ambient strips |
| WS2812B LED strip (100 pixels) | 2 | Rear door ambient strips |
| WS2812B LED strip (25 pixels) | 4 | Pillar accent strips |
| WS2812B LED strip (15 pixels) | 2 | Side mirror strips |
| Simple LEDs or WS2812B (3 pixels) | 2 | Footwell lights (left/right) |
| 5A fuse + holder | 1 | Overcurrent protection |
| Dupont connectors/wire | misc | Inter-module wiring |
| Insulating tape | misc | Wire protection |
| Soundproofing tape | misc | Vibration dampening |

**Tools:** Pliers, soldering iron, Dupont crimper (optional), pry tool, Torx set, pin needle.

---

## Critical Bugs

### BUG-1: Assignment vs comparison in fade logic (DoorLight.cpp:205)

```cpp
// BROKEN — comparison, does nothing:
_currentColor[c][i] == _targetColor[c][i];

// FIX — assignment, snaps color to target:
_currentColor[c][i] = _targetColor[c][i];
```

**Impact:** Color transitions never snap to their final value. They overshoot or oscillate instead of settling cleanly. This affects every LED in the system.

### BUG-2: Wrong base in age deserialization (CarLight.cpp:181)

```cpp
// BROKEN — base 255:
age += packetBuffer[2 + i + doorNum * doorLen] * pow(255, i);

// FIX — base 256 (matching the bit-shift encoding):
age += (unsigned long)(uint8_t)packetBuffer[2 + i + doorNum * doorLen] << (8 * i);
```

**Impact:** State age is reconstructed incorrectly on door modules, causing animation timing to be wrong for any state lasting >255ms.

### BUG-3: Signed char buffer for UDP (CarLight.cpp:165)

```cpp
// BROKEN:
char packetBuffer[255];

// FIX:
uint8_t packetBuffer[255];
```

**Impact:** Brightness values >127 are interpreted as negative numbers, causing incorrect brightness on door modules.

### BUG-4: Duplicate TWAI_ALERT_BUS_ERROR handler (Car.cpp:270 and 289)

The identical error handler block appears twice. Remove the duplicate at line 289.

### BUG-5: twai_clear_receive_queue() drops messages (Car.cpp:307)

```cpp
// BROKEN — drops messages that arrived during processing:
while (twai_receive(&message, 0) == ESP_OK) {
    _handle_twai_rx_message(message);
}
twai_clear_receive_queue();  // ← DELETE THIS LINE
```

The `while` loop already drains the queue. The subsequent clear drops any messages that arrived during processing.

---

## Responsiveness

### R1: Read ALL pending CAN messages per loop iteration

Currently `Car::process()` reads only **one** MCP2515 message per call for both VCAN and CCAN. At 500kbps with many nodes broadcasting, the MCP2515's 2-frame RX buffer overflows rapidly, dropping messages silently.

```cpp
// CURRENT (Car.cpp:245):
if (_v_enabled && _VCAN->checkReceive()) { /* one read */ }

// FIX:
while (_v_enabled && _VCAN->checkReceive()) { /* read all */ }
// Same for CCAN at line 311
```

### R2: Replace blocking delay() with non-blocking timing

```cpp
// CURRENT (TeslaAmbientLight.ino:167-170):
if (!car.displayOn)
    delay(10);
else
    delay(1);

// FIX: Remove delays entirely. Rate-limit specific operations:
static unsigned long lastLightUpdate = 0;
car.process();  // always process CAN immediately
if (millis() - lastLightUpdate > 10) {
    lastLightUpdate = millis();
    carLight.processCarState(car);
    carLight.sendLightState();
    // ...footwell updates...
}
```

### R3: Remove delay(10) on door modules (TeslaAmbientLight.ino:176)

The 10ms delay adds a hard latency floor. The NeoPixel `show()` call (~3.5ms for 136 pixels) already provides natural pacing. Replace with `yield()` or remove entirely.

---

## Efficiency

### E1: Replace double arrays with uint8_t (DoorLight.h:31-34)

Current RAM usage per DoorLight instance:
```
double _currentColor[3][261]  = 6,264 bytes
double _targetColor[3][261]   = 6,264 bytes
uint32_t _oldColor[261]       = 1,044 bytes
uint32_t _oldTargetColor[261] = 1,044 bytes
Total: ~14,616 bytes
```

Colors are integers 0-255. Using `uint8_t` instead of `double`:
```
uint8_t _currentColor[3][261]  = 783 bytes
uint8_t _targetColor[3][261]   = 783 bytes
uint32_t _oldColor[261]        = 1,044 bytes  (can also be eliminated)
Total: ~2,610 bytes (82% reduction)
```

This also eliminates all `round()` calls in the hot path.

### E2: Limit _fadeColor() to active pixel range

```cpp
// CURRENT:
for (int i = 0; i < _numPixels; i++) { ... }

// FIX:
for (int i = _firstPixel; i <= _lastPixel; i++) { ... }
```

Rear doors currently fade 261 pixels when only 100 are used.

### E3: Eliminate redundant Color() packing in _setTargetColor()

Compare R/G/B components directly instead of packing into uint32_t for comparison:

```cpp
void DoorLight::_setTargetColor(int i, uint8_t r, uint8_t g, uint8_t b) {
    if (_targetColor[0][i] != r || _targetColor[1][i] != g || _targetColor[2][i] != b) {
        _targetColor[0][i] = r;
        _targetColor[1][i] = g;
        _targetColor[2][i] = b;
        _targetStateMs = millis();
    }
}
```

### E4: FootLight::initRGB allocates wrong pixel count

```cpp
// BROKEN:
_strip = new Adafruit_NeoPixel(numPixels, ledPin, ...);  // 261 pixels!

// FIX:
_strip = new Adafruit_NeoPixel(numPixelsFoot, ledPin, ...);  // 3 pixels
```

### E5: WiFi.config() called before WiFi.softAP() has no effect

```cpp
// CURRENT (does nothing for AP mode):
WiFi.config(local_IP, gateway, subnet);
WiFi.mode(WIFI_AP);
WiFi.softAP(ssid, password, 1, 0, 10, false);

// FIX:
WiFi.mode(WIFI_AP);
WiFi.softAP(ssid, password, 1, 0, 10, false);
WiFi.softAPConfig(local_IP, gateway, subnet);
```

---

## Stability

### S1: Add WiFi reconnection with backoff on door modules

Currently, door modules reboot in an infinite 1-second loop if WiFi drops. Add a reconnection handler:

```cpp
WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
        WiFi.begin(ssid, password, 1);
    }
});
```

And show a visual "disconnected" pattern on the LEDs.

### S2: Add UDP heartbeat timeout on door modules

If no UDP packet is received for N seconds, fade LEDs to off:

```cpp
if (millis() - _lastUdp > 15000) {
    // Fade to safe default (off or dim)
    for (byte i = 0; i < 4; i++)
        doorLightState[i] = WAIT;
}
```

### S3: Add MCP2515 bus-off recovery

Periodically check MCP2515 error register. On bus-off, reinitialize:

```cpp
byte err = _VCAN->getError();
if (err & MCP_EFLG_TXBO) {
    _VCAN->begin(MCP_STDEXT, CAN_500KBPS, MCP_8MHZ);
    // re-apply filters...
}
```

### S4: Add bounds checking on CAN data parsers

Every `_process*()` function accesses `data[]` at fixed offsets without verifying `len`:

```cpp
void Car::_processLights(unsigned char len, unsigned char data[]) {
    if (len < 3) return;  // Need bytes 0 and 2
    // ...existing code...
}
```

### S5: Add freshness check before sending CAN commands

```cpp
void Car::_processVehicleControl(unsigned char len, unsigned char data[]) {
    _vehicleControlFrameMs = millis();  // track freshness
    // ...
}

void Car::openFrunk() {
    if (millis() - _vehicleControlFrameMs > 1000) return;  // too stale
    // ...
}
```

---

## Simplicity

### P1: Fix AUTOSPORT_ESP32 preprocessor logic

`#define AUTOSPORT_ESP32 false` + `#ifdef AUTOSPORT_ESP32` is always true. Use `#if`:
```cpp
#if AUTOSPORT_ESP32
// autosport pins
#else
// standard pins
#endif
```

### P2: Add platformio.ini for proper build management

Define board variants, library dependencies, and per-role firmware builds instead of EEPROM-based runtime role selection.

### P3: Extract CAN ID constants

```cpp
// can_ids.h
constexpr uint16_t CAN_ID_LIGHTS     = 0x3F5;
constexpr uint16_t CAN_ID_LEFT_DOORS = 0x102;
constexpr uint16_t CAN_ID_RIGHT_DOORS= 0x103;
constexpr uint16_t CAN_ID_GEAR       = 0x118;
constexpr uint16_t CAN_ID_VEHICLE_STATUS = 0x2E1;
constexpr uint16_t CAN_ID_AUTOPILOT  = 0x399;
constexpr uint16_t CAN_ID_VEHICLE_CONTROL = 0x273;
```

### P4: Dynamically allocate color arrays based on actual pixel count

Instead of every subclass (MirrorLight with 15px, FootLight with 3px) carrying 261-element arrays from the DoorLight base class, allocate in the constructor:

```cpp
void DoorLight::init(byte doorNum, byte ledPin, int pixelCount) {
    _numPixels = pixelCount;
    _currentColor[0] = new uint8_t[pixelCount]();
    // ...
}
```

### P5: Extract common CAN dispatch method

```cpp
void Car::_dispatchVehicleMessage(uint16_t rxId, uint8_t len, uint8_t* data) {
    switch (rxId) {
        case 0x3F5: _processLights(len, data); break;
        case 0x103: _processRightDoors(len, data); break;
        case 0x102: _processLeftDoors(len, data); break;
        case 0x2E1: _processVehicleStatus(len, data); break;
        case 0x118: _processGear(len, data); break;
    }
}
```

Used by both MCP2515 and TWAI code paths, eliminating duplication.

---

## Priority Matrix

| Priority | Issue | Category |
|---|---|---|
| **P0** | BUG-1: `==` instead of `=` in fade | Correctness |
| **P0** | BUG-2: `pow(255)` vs `pow(256)` | Correctness |
| **P0** | R1: Read all pending CAN messages | Responsiveness |
| **P1** | R2/R3: Remove blocking delay() | Responsiveness |
| **P1** | E1: double→uint8_t arrays (14KB savings) | Efficiency |
| **P1** | BUG-5: Remove twai_clear_receive_queue() | Stability |
| **P1** | BUG-3: signed char UDP buffer | Correctness |
| **P2** | S1: WiFi reconnection logic | Stability |
| **P2** | S2: UDP heartbeat timeout | Stability |
| **P2** | S3: CAN bus-off recovery | Stability |
| **P2** | S4: CAN data length checks | Stability |
| **P3** | E2-E5: Loop range, alloc fixes | Efficiency |
| **P3** | P1-P5: Build system, constants, DRY | Simplicity |
