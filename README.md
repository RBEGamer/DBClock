# DBClock

![final_clock](https://user-images.githubusercontent.com/9280991/218873674-c37656a7-354a-447a-a6b4-41e935bf3d74.jpg)


## Features

* NTP Sync
* LED lightning, dimmable
* Stand
* 12V 
* Webinterface for configuration
* Daylight Saving


## Bahnhofsuhr-DB

[bahnhofsuhr-db-shop](https://bahnshop.de/db-originale/sonstiges/1749/bahnhofsuhr-db)

Dark blue metal case, acrylic glass panes, dial white,with black hour and minute markers, red hole second hand, red DB logo unlit.

Condition: used, in new design, the sale is in the current condition as a decorative object, a function can not be guaranteed, rust spots, paint chips and scratches in the glass and paint are part of the character. The holder is not included in the delivery. The article picture is an exemplary representation.

Dimensions: 60 cm diameter
Weight: approx. 30 kg

Shipping is by freight forwarding on one-way pallet (free curbside). Disposal of the pallet is done by the customer.

In my clock, `BU 190t S230` the clock-drives were used.


### MORE LINKS

* [PROFILINE-Analoguhr.pdf]([https://bahnshop.de/db-originale/sonstiges/1749/bahnhofsuhr-db](https://www.mobatime.com/wp-content/uploads/2021/11/LD-800078.24-PROFILINE-Analoguhr.pdf))

## BU 190t S230


A complete manual for the drive can be found on the manufacturers website:
[Uhrwerk-BU-190-230-Installation.pdf](https://www.buerk-mobatime.de/wp-content/uploads/2020/01/BD-800603.01-Uhrwerk-BU-190-230-Installation.pdf)

### POWER THE DRIVE

![2023-02-12 22 21 10](https://user-images.githubusercontent.com/9280991/218875750-ed3e288c-a10a-4c93-b43a-8ce384c02066.jpg)

I got the `BU 190t S230` version which needs 230v.
After a teardown, a small rectifier is found so that the drive uses a lower voltage for the motors and logic.
After further investigation on the transformers, the drive is internally operated with 12 V. 
So if you solder some wires directly to the rectifier and run it with a 12 V power supply, the drive works fine.
It also uses its own crystal, so no need for a stable 60Hz sync from the mains.




### SET THE TIME USING AN ARDUINO

In general it is simple to set the clock using a serial connection to the `IN` connector of the drive.
The connector is a `RJ12` jack it is connected to the Arduino using the following pins.

| Arduino | BU 190t      | RJ12 pin |
|---------|--------------|----------|
| TX      | TTL IN (RxD) | 1        |
| GND     | GND          | 2        |




The following Arduino code function sends the current time to the drive.
After running the code, the clockdrive syncs up.
This takes up the 6 minutes.

```c++
void send_time_to_clock(const uint8_t _h, const uint8_t _m, const uint8_t _s, bool _signal_ok = true);

void setup(){
  // NOTE THE SPEICAL SERIAL INIT ACCORDING THE DRIVES DATASHEET
  Serial.begin(9600,SERIAL_7E1);
}


void loop(){

send_time_to_clock(13,14,15)
//SEND THE CURRENT TIME EACH SECOND
// USE A DEDICATED TIMER FOR THIS
delay(1000); 
}
void send_time_to_clock(const uint8_t _h, const uint8_t _m, const uint8_t _s, bool _signal_ok = true){
  const uint8_t sec = _s % 60;
  String s = String(sec);
  if(sec<10){
    s="0"+s;
  }

  const uint8_t min = _m % 60;
  String m = String(min);
  if(min<10){
    m="0"+m;
  }

  const uint8_t hour = _h % 24;
  String h = String(hour);
  if(hour<10){
    h="0"+h;
  }

  String signok = "M";
  if(_signal_ok){
    signok = "A";
  }
  // USE WITH IN SETUP Serial.begin(9600,SERIAL_7E1);
  //DATE SET TO 10.10.2000 => NOT NEEDED HERE
  Serial.print("O" + signok + "L010100F"+ h + m + s +"\r");
}
```

In my case i used a Arduino Mega, with more than one hardware serial interface.
The desing is using `Serial2` for the clock drive and `Serial` to communicate with the PC or ESP8266 for NTP Sync.
By using a RTC like the `DS3231` in combination with the `ESP8266` for NTP sync, you get a very stable self syncing clock!
See the `src` directory for the Arduino and `ESP8266` sources.

 ![Arduino](https://user-images.githubusercontent.com/9280991/218880766-d55ab539-9287-48b1-9fd1-46a60e43f065.jpg)


## FIRMWARE

All firmwares are [PlatformIO](https://platformio.org) projects. All used third party libraries are stored in the `lib` folder of each project, so no library downloads are needed.

| PROJECT              | HARDWARE                                  | DEFAULT ENV       |
|----------------------|-------------------------------------------|-------------------|
| `src/bu190t`         | Arduino Mega 2560 (clock drive, RTC, LED) | `megaatmega2560`  |
| `src/wifiinterface`  | ESP8266 (NodeMCU or ESP-01)               | `nodemcu`, `esp01_1m` |
| `src/picowinterface` | Raspberry Pi Pico W + WIZnet Ethernet HAT | `rpipicow`        |

```bash
pio run -d src/bu190t -t upload
```

### DAYLIGHT SAVING

The RTC of the Mega runs on standard time (`CET`, set timezone `1` in the webinterface).
If daylight saving is enabled, the Mega adds one hour from the last Sunday in March (02:00) until the last Sunday in October (03:00 CEST).



## ALTERNATIVE NETWORK INTERFACE: PICO W + ETHERNET HAT (WIFI AND LAN)

Instead of the `ESP8266`, a `Raspberry Pi Pico W` with the `WIZnet Ethernet HAT for Pico` (`W5100S-L`) can be used.
It supports WiFi and LAN at the same time; LAN is preferred if both are connected.
The firmware is located in `src/picowinterface` and uses the same serial protocol, so the Arduino Mega firmware does not need to be changed.

* [Raspberry Pi Pico W](https://www.raspberrypi.com/products/raspberry-pi-pico/)
* [WIZnet Ethernet HAT for Pico](https://docs.wiznet.io/Product/Open-Source-Hardware/wiznet_ethernet_hat)

### BUILD

The project uses the [arduino-pico](https://github.com/earlephilhower/arduino-pico) core via the PlatformIO platform `maxgerhardt/platform-raspberrypi`, with 1MB of the 2MB flash as filesystem; it stores the settings and is needed for OTA updates.

```bash
pio run -d src/picowinterface
```

For the first upload, hold `BOOTSEL` while plugging in the Pico W and copy `src/picowinterface/.pio/build/rpipicow/firmware.uf2` to the `RPI-RP2` drive.

### WIRING

The HAT is stacked onto the Pico W and uses `SPI0`: `GP16` MISO, `GP17` CS, `GP18` SCK, `GP19` MOSI, `GP20` RST.

| Pico W           | Arduino Mega  | NOTE                                                  |
|------------------|---------------|-------------------------------------------------------|
| GP0 (UART0 TX)   | RX3 (pin 15)  | 3.3V signal, the Mega reads it fine                   |
| GND              | GND           |                                                       |
| VSYS             | 5V            | or a separate 5V supply, see below                    |

**NOTE** The Pico W is not 5V tolerant. Do not connect the Mega `TX3` to `GP1` without a level shifter; the firmware does not need it.

**NOTE** WiFi + W5100S draw up to ~200 mA. If the Mega is powered with 12V on `VIN`, its linear regulator gets hot; a small 12V to 5V buck converter for the Pico W is recommended.

### USAGE

* On boot, the module tries LAN (DHCP) first, then WiFi with the stored credentials.
* If neither connects, an open access point `DBClockConfiguration_<id>` with a captive portal is started. Enter the WiFi credentials there; the module reboots and connects. If a LAN cable is plugged in, the access point is closed automatically.
* Leave the WiFi settings empty (or press `DELETE WIFI (LAN ONLY)`) to use LAN only.
* Webinterface: `http://dbclock<id>.local` or the IP printed on the Pico W USB serial (115200 baud). The IP is also sent to the Mega as `%ip_` command.
* Firmware updates: upload `.pio/build/rpipicow/firmware.bin` on `http://<ip>/update`, or use Arduino OTA.



## LED

![led](https://user-images.githubusercontent.com/9280991/218877529-a87ac514-6470-402f-b00f-8c9af99b9be0.jpg)


* [IRF520 MOSFET Treiber Modul](https://de.aliexpress.com/item/32667789271.html)
* [LED-Streifen 5m weiß](https://www.ledpoint.it/de/led-streifen-5m-warm-weiß-3500k-3step-2835-120ledm-12v-144wm)


The old lamp was replaced with a neutral-white 12B led strip with 144 LEDs/m.
Using a mosfet connected to the Arduino PWM GPIO `Pin 8`, so it is possible to control the strip using software.
The current implementation, allows fade up/down on predefined times, on/off from 0-100%.

The brightness levels are perceived brightness: a gamma curve (`LED_GAMMA 2.2`) converts them to the PWM duty cycle, so the `10%`, `20%`, `50%`, `70%`, `100%` modes and the fades look even to the eye.
In `AUTO` mode the strip fades in from 20:00 to 20:20, stays on until 23:00 and fades out until 23:59.


### BUTTON

A pushbutton from `GND` to Arduino GPIO `2` can be connected to set the LED intensity modes manually.


## FINAL PINOUT

| Arduino | BU 190t RJ12 pin      | DS3131   | PUSHBUTTON PIN |   MOSFET PIN |   ESP8266    |
|---------|-----------------------|----------|----------------|--------------|--------------|
| VIN     | + pin on fullbridge   |          |                |              |              |
| TX      | TTL IN (RxD)  1       |          |                |              |              |
| GND     | GND           2       | GND      |  1             |    GND       |   GND        |
| 2       |                       |          |  2             |              |              |
| 8       |                       |          |                |    SIG       |              |
| 5V      |                       | VCC      |                |    VCC       |   VIN        |
| RX      |                       |          |                |              |   TX         |
| SDA     |                       | SDA      |                |              |              |
| SCL     |                       | SCL      |                |              |              |



## Stand

![stand](https://user-images.githubusercontent.com/9280991/218878291-089b0718-5398-4150-b824-e7dbf4db6409.png)

A clock stand/holder was not included, so a simple millable (or 3d printable ABS 100% infill) was designed.
The bottom holes are designed to be mounted on 30x30 n-nut profiles.
The files can be found in the `src/3d_print/clock_mount` directory.

Four `M8x30` with lock nuts were used to attach the clock to the milled stand.

**NOTE** The clock weighs about 30 kg, so the stand should be durable...


## PICTURES

![2023-02-14 22 11 03](https://user-images.githubusercontent.com/9280991/219017128-bdec6231-8f9d-43a7-a9d2-3d14baaa5de2.png)


### CLOCK FINGERS
![2023-02-12 22 24 04](https://user-images.githubusercontent.com/9280991/219017098-3d16878f-0d2f-4925-8360-469b16b6c4e4.jpg)





## THIRD PARTY LIBRARIES AND LICENSES

The firmware in this repository is licensed under the Apache License 2.0 (see `LICENSE`).
The following third party libraries are included unmodified in the `lib` folders and keep their own licenses; the license text is part of each library folder or source header.

| LIBRARY                                                         | VERSION    | USED BY                          | LICENSE      | AUTHOR             |
|-----------------------------------------------------------------|------------|----------------------------------|--------------|--------------------|
| [RTClib](https://github.com/adafruit/RTClib)                    | 2.1.0      | `bu190t`                         | MIT          | Adafruit           |
| [Adafruit BusIO](https://github.com/adafruit/Adafruit_BusIO)    | 1.14.1     | `bu190t`                         | MIT          | Adafruit           |
| [NTPClient](https://github.com/arduino-libraries/NTPClient)     | 3.2.1      | `wifiinterface`, `picowinterface` | MIT         | Fabrice Weinberg   |
| [Time](https://github.com/PaulStoffregen/Time)                  | 1.6.1      | `wifiinterface`                  | LGPL-2.1     | Michael Margolis   |
| [WiFiManager](https://github.com/tzapu/WiFiManager)             | 2.0.0-rc.1 | `wifiinterface`                  | MIT          | tzapu              |

The Arduino frameworks are installed by PlatformIO and are not part of this repository. They also provide the core libraries (`Wire`, `EEPROM`, `SPI`, `WiFi`, `WebServer`, `LittleFS`, `ArduinoOTA`, lwIP and the W5100 driver):

| FRAMEWORK                                                              | USED BY          | LICENSE   |
|------------------------------------------------------------------------|------------------|-----------|
| [ArduinoCore-avr](https://github.com/arduino/ArduinoCore-avr)           | `bu190t`         | LGPL-2.1  |
| [ESP8266 Arduino core](https://github.com/esp8266/Arduino)              | `wifiinterface`  | LGPL-2.1  |
| [arduino-pico](https://github.com/earlephilhower/arduino-pico)          | `picowinterface` | LGPL-2.1  |
