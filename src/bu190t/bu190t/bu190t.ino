#define DS1307_CTRL_ID 0x68  //I2C ADDRESS OF DS3231 OR DS1307
#define EEPROM_INDEX_LED_MODE 0
#define EEPROM_INDEX_DLS 1
#define RTC_TIMEZONE 0 //0=UTC ,1=MEZ
#define VERSION "1.0.0"


#include <TimeLib.h>
#include <EEPROM.h>
#include <Wire.h>
#include <DS1307RTC.h>

void send_status(const String _command, const String _payload);
void update_according_led_mode(const uint8_t _led_mode);
void send_time_to_clock(const int _h, const int _m, const int _s, bool _signal_ok = true);
uint16_t crc16(const String _data, const uint16_t _poly = 0x8408);
bool check_for_i2c_device(const uint8_t _addr);
bool summertime_eu(const int _year, const signed char _month, const signed char _day, const signed char _hour);

int clock_hour = 0;
int clock_min = 0;
int clock_sec = 0;

const uint8_t PIN_BUTTON_BRGHT = 2;
const uint8_t PIN_LED_PWM = 8;
const long CLOCK_UPDATE_INTERAL = 1000;


unsigned long previousMillis = 0;
uint8_t current_led_pwm = 0; // to fade from 0-255
uint8_t target_led_pwm = 0;
bool last_brght_button_state = false;

//EEPROM MANAGED VARIABLES
uint8_t led_mode = 0;
uint8_t daylightsaving_enabled = 0;


void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);
  //IMPORTANT FOR THE BU 190t WATCH DRIVES
  Serial1.begin(9600,SERIAL_7E1); // FIRST CLOCK DRIVE
  Serial2.begin(9600,SERIAL_7E1); // SECOND CLOCK DRIVE
  //FOR WIFI INTERFACE
  Serial3.begin(9600);
  // INIT PINS
  pinMode(PIN_BUTTON_BRGHT, INPUT_PULLUP);
  pinMode(PIN_LED_PWM, OUTPUT);
  analogWrite(PIN_LED_PWM, 0);

  //READ EEEPROM VALUES
  led_mode = EEPROM.read(EEPROM_INDEX_LED_MODE);
  daylightsaving_enabled = EEPROM.read(EEPROM_INDEX_DLS);

  update_according_led_mode(led_mode);
  //INIT I2C
  Wire.begin();
  //CHECK IF RTC IS PRESENT
  //SETUP TIME
  if(check_for_i2c_device(DS1307_CTRL_ID)){
    //CHECK IF RTC IS IN STOP MODE SO ONE INITIAL SET IS NEEDED
    // ITS NOT WORKING IF THE TIMLIB IS USED DIRECTLY
    if(RTC.chipPresent()){
      send_status("log", "RtcChipPresent");
      tmElements_t tm;
      //SOME DEFAULT TIME
      tm.Hour = 0;
      tm.Minute = 0;
      tm.Second = 0;
      tm.Day = 1;
      tm.Month = 1;
      tm.Wday = 4;
      tm.Year = CalendarYrToTm(1970);
      //WRITE TIME TO FLASH
      if (RTC.write(tm)) {
        setSyncProvider(RTC.get);   // FINALLY SET TIMESOURCE TO RTC
        send_status("log", "RtcWriteInitialTime");
      }
    }else{
      send_status("log", "!RtcChipPresent");
      setSyncProvider(RTC.get);   // DEFAULT RTC
    }
    send_status("log", "RtcFound");
  }else{
    send_status("log", "RtcNotFound");
  }
  //INIT TimeLIB
  if(timeStatus()!= timeSet){ 
     send_status("log", "TimeLibStatusError"); 
  }else{
     send_status("log", "TimeLibStatusOk"); 
  }   

  send_status("version", VERSION); 
  
}



void send_status(const String _command, const String _payload){
 Serial.println("%" + _command + "_" + _payload + "_" + crc16(_command + _payload));
 Serial3.println("%" + _command + "_" + _payload + "_" + crc16(_command + _payload));
}


bool check_for_i2c_device(const uint8_t _addr){
    Wire.beginTransmission(_addr);
    uint8_t error = Wire.endTransmission();
    if (error == 0) {
      return true;
    }
    return false;
}

// https://forum.arduino.cc/t/time-libary-sommerzeit-winterzeit/221884/2
// European Daylight Savings Time calculation by "jurs" for German Arduino Forum
// input parameters: "normal time" for year, month, day, hour and tzHours (0=UTC, 1=MEZ)
// return value: returns true during Daylight Saving Time, false otherwise
// #define RTC_TIMEZONE 0=UTC ,1=MEZ
bool summertime_eu(const int _year, const signed char _month, const signed char _day, const signed char _hour)
{
    if (_month < 3 || _month > 10)
    {
        return false; // keine Sommerzeit in Jan, Feb, Nov, Dez
    }
    else if (_month > 3 && _month < 10)
    {
        return true; // Sommerzeit in Apr, Mai, Jun, Jul, Aug, Sep
    }
    else if (_month == 3 && (_hour + 24 * _day) >= (1 + RTC_TIMEZONE + 24 * (31 - (5 * _year / 4 + 4) % 7)) || _month == 10 && (_hour + 24 * _day) < (1 + RTC_TIMEZONE + 24 * (31 - (5 * _year / 4 + 1) % 7)))
    {
        return true;
    }
    else
    {
        return false;
    }
}

uint16_t crc16(const String _data, const uint16_t _poly = 0x8408) {
  uint16_t crc = 0xFFFF;
  for (int i = 0; i < _data.length(); i++) {
    const uint8_t b = (byte)_data[i];
    uint8_t cur_byte = 0xFF & b;

    for (int it = 0; it < 8; it++) {
      if ((crc & 0x0001) ^ (cur_byte & 0x0001)) {
        crc = (crc >> 1) ^ _poly;
      } else {
        crc >>= 1;
      }
      cur_byte >>= 1;
    }
  }
  crc = (~crc & 0xFFFF);
  crc = (crc << 8) | ((crc >> 8) & 0xFF);
  return crc & 0xFFFF;
}


void send_time_to_clock(const int _h, const int _m, const int _s, bool _signal_ok = true){
  const int sec = _s%60;
  String s = String(sec);
  if(sec<10){
    s="0"+s;
  }

  const int min = _m%60;
  String m = String(min);
  if(min<10){
    m="0"+m;
  }

  const int hour = _m%24;
  String h = String(hour);
  if(hour<10){
    h="0"+h;
  }

  String signok = "M";
  if(_signal_ok){
    signok = "A";
  }
  // USE WITH Serial.begin(9600,SERIAL_7E1);
  Serial2.print("O" + signok + "L230212F"+ h + m + s +"\r");
  Serial3.print("O" + signok + "L230212F"+ h + m + s +"\r");
  send_status("ct", h + ":" + m + ":" + s);
}


void update_according_led_mode(const uint8_t _led_mode){
    const uint8_t led_mode = _led_mode % 6;
    if(led_mode == 0){
      target_led_pwm = 0;
    }else if(led_mode == 1){
      target_led_pwm = 20;
    }else if(led_mode == 2){
      target_led_pwm = 50;
    }else if(led_mode == 3){
      target_led_pwm = 150;
    }else if(led_mode == 4){
      target_led_pwm = 200;
    }else if(led_mode == 5){
      target_led_pwm = 255;  
     }else if(led_mode == 6){
      //AUTO
    }
}
  

// SET TIME
//SET DATE
// SET DLS
// SET BRIGHNESS target_led_pwm




void loop() {

  

 
  if(digitalRead(PIN_BUTTON_BRGHT) == LOW && last_brght_button_state == true){
    last_brght_button_state = false;
    led_mode++;
    if(led_mode > 6){
      led_mode = 0;
    }
    
    Serial.println("btn");
    //SAVE LED MODE
    EEPROM.write(EEPROM_INDEX_LED_MODE,led_mode);
    update_according_led_mode(led_mode);

    
  }else if(digitalRead(PIN_BUTTON_BRGHT) == HIGH){
    last_brght_button_state = true;
  }


  // SEND TIME TO CLOCK EVERY 1s
  const unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= CLOCK_UPDATE_INTERAL) {
    previousMillis = currentMillis;
    
    if (timeStatus() == timeSet) {
      int h = hour();
      if (daylightsaving_enabled > 0 && summertime_eu(year(), month(), day(), h))
      {
        signed char h = h - 1;
        if (h < 0)
        {
            h += 24;
        }
        h = h % 24;
      }
      send_time_to_clock(h, minute(),second(), true);
    } else {
      Serial.println("_log_timenotset_");
      //TIME NOT SET SO SET TIME
      setTime(0,0,0,1,1,23);
    }

    
  }
  
  // UPDATE LEDS WITH SOME RAMP UP/DOWN
  if(target_led_pwm < current_led_pwm){
    if(abs(target_led_pwm-current_led_pwm) > 20){
      current_led_pwm-= 3;      
    }else{
      current_led_pwm--;
    } 
    analogWrite(PIN_LED_PWM, current_led_pwm);

  }else if(target_led_pwm > current_led_pwm){
    if(abs(target_led_pwm-current_led_pwm) > 20){
      current_led_pwm+= 3;
    }else{
      current_led_pwm++;
    }
    analogWrite(PIN_LED_PWM, current_led_pwm);
  }

  delay(30);
}
