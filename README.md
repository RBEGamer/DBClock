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
  const uint8_t sec = max(_s, 60);
  String s = String(sec);
  if(sec<10){
    s="0"+s;
  }

  const uint8_t min = max(_m, 60);
  String m = String(min);
  if(min<10){
    m="0"+m;
  }

  const uint8_t hour = max(_h, 24);
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
The the `src` directory for the Arduino and `ESP866` sources.

 ![Arduino](https://user-images.githubusercontent.com/9280991/218880766-d55ab539-9287-48b1-9fd1-46a60e43f065.jpg)




## LED

![led](https://user-images.githubusercontent.com/9280991/218877529-a87ac514-6470-402f-b00f-8c9af99b9be0.jpg)


* [IRF520 MOSFET Treiber Modul](https://de.aliexpress.com/item/32667789271.html)
* [LED-Streifen 5m weiß](https://www.ledpoint.it/de/led-streifen-5m-warm-weiß-3500k-3step-2835-120ledm-12v-144wm)


The old lamp was replaced with a neutral-white 12B led strip with 144 LEDs/m.
Using a mosfet connected to the Arduino PWM GPIO `Pin 8`, so it is possible to control the strip using software.
The current implementation, allows fade up/down on predefined times, on/off from 0-100%.


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



