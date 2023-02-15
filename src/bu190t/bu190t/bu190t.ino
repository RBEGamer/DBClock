#define DS1307_CTRL_ID 0x68  //I2C ADDRESS OF DS3231 OR DS1307
#define EEPROM_INDEX_LED_MODE 0
#define EEPROM_INDEX_DLS 1
#define RTC_TIMEZONE 0 //0=UTC ,1=MEZ
#define VERSION "1.0.0"
#define WIFIEXTENTION_SERIAL Serial3 // TO ESP8266
#define CLOCK_SERIAL Serial2
#define DEBUG_SERIAL Serial //USB
#define LIGHTMODE_AUTO_HOUR_START 20
#define LIGHTMODE_AUTO_HOUR_END 22
#define LIGHTMODE_AUTO_HOUR_MAX_PWM 150

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
String getValue(const String data, const char separator, const int index);

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


String readString;
bool cmd_started = false;
#define CMD_START_CHARACTER '%'
#define CMD_SEPERATION_CHARACTER '_'
//EEPROM MANAGED VARIABLES
uint8_t led_mode = 0;
uint8_t daylightsaving_enabled = 0;


void setup() {
  // put your setup code here, to run once:
  DEBUG_SERIAL.begin(9600);
  //IMPORTANT FOR THE BU 190t WATCH DRIVES
  CLOCK_SERIAL.begin(9600,SERIAL_7E1); // FIRST CLOCK DRIVE
  //FOR WIFI INTERFACE
  WIFIEXTENTION_SERIAL.begin(9600);
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


String getValue(const String data, const char separator, const int index) {
  int found = 0;
  int strIndex[] = {
    0,
    -1
  };
  int maxIndex = data.length() - 1;
  for (int i = 0; i <= maxIndex && found <= index; i++) {
    if (data.charAt(i) == separator || i == maxIndex) {
      found++;
      strIndex[0] = strIndex[1] + 1;
      strIndex[1] = (i == maxIndex) ? i + 1 : i;
    }
  }
  return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}

void send_status(const String _command, const String _payload){
 DEBUG_SERIAL.println("%" + _command + "_" + _payload + "_" + crc16(_command + _payload));
 WIFIEXTENTION_SERIAL.println("%" + _command + "_" + _payload + "_" + crc16(_command + _payload));
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

  const int hour = _h%24;
  String h = String(hour);
  if(hour<10){
    h="0"+h;
  }

  String signok = "M";
  if(_signal_ok){
    signok = "A";
  }
  // USE WITH Serial.begin(9600,SERIAL_7E1);
  CLOCK_SERIAL.print("O" + signok + "L230212F"+ h + m + s +"\r");
  //Serial3.print("O" + signok + "L230212F"+ h + m + s +"\r");
  send_status("ct", h + ":" + m + ":" + s);
}


void update_according_led_mode(const uint8_t _led_mode){
    const uint8_t led_mode = _led_mode % 7;
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
       target_led_pwm = 0;
       current_led_pwm = 50;//INDICATE CLOCK IS IN AUTO
      //AUTO
    }
}
  

// SET TIME
//SET DATE
// SET DLS
// SET BRIGHNESS target_led_pwm




void loop() {

  
  

  /// BUTTOM FOR LIGHTMODE TOGGLE
  if(digitalRead(PIN_BUTTON_BRGHT) == LOW && last_brght_button_state == true){
    last_brght_button_state = false;
    led_mode++;
    if(led_mode > 6){
      led_mode = 0;
    }
    
    send_status("log", "LedMode" + String(led_mode));
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
      //SEND TIME TO CLOCK-DRIVES
      send_time_to_clock(h, minute(),second(), true);

      //ALSO HANDLE LED MODE AUTO
      if(led_mode == 6){
        // FADE 0-max and max-0
        //light up 3times as fast as for light down
        if(h == LIGHTMODE_AUTO_HOUR_START){
          target_led_pwm = map(max(minute()*3, 60),0,60,0, LIGHTMODE_AUTO_HOUR_MAX_PWM);
        }else if(h == LIGHTMODE_AUTO_HOUR_END){
          target_led_pwm = map(60-minute(),0,60,0, LIGHTMODE_AUTO_HOUR_MAX_PWM);
        }
        
      }
    }

    if(target_led_pwm != current_led_pwm){
      send_status("led", String(target_led_pwm));
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


    while (WIFIEXTENTION_SERIAL.available()) {
      delay(30); //delay to allow buffer to fill
      if (WIFIEXTENTION_SERIAL.available() > 0) {
        const char c = WIFIEXTENTION_SERIAL.read(); //gets one byte from serial buffer  
        
        if (c == '\n') {
          WIFIEXTENTION_SERIAL.flush();
          cmd_started = false;
          readString += CMD_SEPERATION_CHARACTER;
          break;
        }else if(c == CMD_START_CHARACTER && !cmd_started){
          cmd_started = true;
          readString = "";
        }else if(cmd_started){
          readString += c; //makes the string readString
        }
          
      }
    }

    if (readString.length() > 0) {
      
      const String cmd = getValue(readString, CMD_SEPERATION_CHARACTER, 0);
      String cmd_value = getValue(readString, CMD_SEPERATION_CHARACTER, 1);
      //st
      //sd
      if(cmd == "sb" && cmd_value != ""){
        const int tmp = cmd_value.toInt();
        if(tmp >= 0 && tmp < 7){
            led_mode = tmp;
            EEPROM.write(EEPROM_INDEX_LED_MODE,led_mode);
            update_according_led_mode(led_mode);
            DEBUG_SERIAL.println(led_mode);
        }
      }else if(cmd == "dls" && cmd_value != ""){
        const int dlsi = cmd_value.toInt();
        if(dlsi >= 0 && dlsi < 2){
          daylightsaving_enabled = dlsi;
          EEPROM.write(EEPROM_INDEX_DLS, daylightsaving_enabled);
        }
      }else if(cmd == "st" && cmd_value != ""){ //SET TIME
        const String sth = getValue(cmd_value, ':', 0);
        const String stm = getValue(cmd_value, ':', 1);
        const String sts =  getValue(getValue(cmd_value, ':', 2), '_', 0);;
        
        if(sth != "" && stm != ""){
          const int ih = sth.toInt();
          const int im = stm.toInt();
          const int is = sts.toInt();
          
          if(ih >0 && ih < 24 && im >0 && im < 60 && is >0 && is < 60){
            tmElements_t tm;
            RTC.read(tm);
            setTime(ih, im, is, tm.Day, tm.Month, tm.Year);
          }
        }
        DEBUG_SERIAL.println(sth + "::" + stm + "::" + sts);
      }else if(cmd == "sd" && cmd_value != ""){ //SET DATE
        const String sdday = getValue(cmd_value, '.', 0);
        const String sdmonth = getValue(cmd_value, '.', 1);
        const String sdyear = getValue(getValue(cmd_value, '.', 2), '_', 0);
        
        if(sdday != "" && sdmonth != "" && sdyear != ""){
          const int iday = sdday.toInt();
          const int imonth = sdmonth.toInt();
          const int iyear = sdyear.toInt();
          
          if(iday > 0 && iday < 31 && imonth >0 && imonth < 12){
            tmElements_t tm;
            RTC.read(tm);
            setTime(tm.Hour, tm.Minute, tm.Second, iday, imonth, tm.Year);
          }
        }
        DEBUG_SERIAL.println(sdday + ".." + sdmonth + ".." + sdyear);
      }

      send_status("gotcmd", cmd + "-"+cmd_value+"-");
      readString = "";
    } 

  delay(10);
}
