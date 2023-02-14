# DBClock

![final_clock](https://user-images.githubusercontent.com/9280991/218873674-c37656a7-354a-447a-a6b4-41e935bf3d74.jpg)


## Features

* NTP Sync
* LED lightning, dimmable
* Stand

## Bahnhofsuhr-DB

[bahnhofsuhr-db-shop](https://bahnshop.de/db-originale/sonstiges/1749/bahnhofsuhr-db)

Dark blue metal case, acrylic glass panes, dial white,with black hour and minute markers, red hole second hand, red DB logo unlit.

Condition: used, in new design, the sale is in the current condition as a decorative object, a function can not be guaranteed, rust spots, paint chips and scratches in the glass and paint are part of the character. The holder is not included in the delivery. The article picture is an exemplary representation.

Dimensions: 60 cm diameter
Weight: approx. 30 kg

Shipping is by freight forwarding on one-way pallet (free curbside). Disposal of the pallet is done by the customer.

In my clock, `BU 190t S230` the clock-drives were used.

## BU 190t S230


A complete manual for the drive can be found on the manufacturers website:
[Uhrwerk-BU-190-230-Installation.pdf](https://www.buerk-mobatime.de/wp-content/uploads/2020/01/BD-800603.01-Uhrwerk-BU-190-230-Installation.pdf)



In general it is simple to set the clock using a serial connection to the `IN` connector of the drive.
The connector is a `RJ12` jack it is connected to the Arduino using the following pins.

| Arduino | BU 190s      | RJ12 pin |
|---------|--------------|----------|
| TX      | TTL IN (RxD) | 1        |
| GND     | GND          | 2        |

The following Arduino code functions

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
