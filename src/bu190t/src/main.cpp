#define DS1307_CTRL_ID 0x68  //I2C ADDRESS OF DS3231 OR DS1307
#define EEPROM_INDEX_LED_MODE 0
#define EEPROM_INDEX_DLS 1
#define EEPROM_INDEX_LED_PERCENT 2
#define RTC_TIMEZONE 1 //0=UTC ,1=MEZ, RTC RUNS ON STANDARD TIME, MUST MATCH THE TIMEZONE OF THE NETWORK MODULE
#define VERSION "2.4.0"
#define WIFIEXTENTION_SERIAL Serial3 // TO ESP8266
#define CLOCK_SERIAL Serial2
#define DEBUG_SERIAL Serial //USB
#define LIGHTMODE_AUTO_HOUR_START 20
#define LIGHTMODE_AUTO_HOUR_END 23
#define LIGHTMODE_AUTO_MAX_LEVEL 167 // PERCEIVED BRIGHTNESS 0-255, 167 = SAME LIGHT AS THE OLD PWM 100
#define LED_GAMMA 2.2 // EXPONENTIAL CURVE SO THE BRIGHTNESS STEPS AND FADES LOOK LINEAR
#define LED_FADE_INTERVAL 8 // ms PER BRIGHTNESS STEP => ~2s FOR A FULL FADE
#define LED_MODE_AUTO 6
#define LED_MODE_MANUAL 7 // BRIGHTNESS SET WITH THE sl COMMAND, E.G. FROM HOME ASSISTANT
#define LED_STATE_INTERVAL 30000 // ms, THE LIGHT STATE IS ALSO REPORTED PERIODICALLY

#include <Arduino.h>
#include <EEPROM.h>
#include <Wire.h>
#include "RTClib.h"


void send_status(const String _command, const String _payload);
void update_according_led_mode(const uint8_t _led_mode);
uint8_t led_level_to_pwm(const uint8_t _level);
uint8_t get_auto_led_level(const int _h, const int _m);
void send_led_state();
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
const long CLOCK_UPDATE_INTERAL = 1000; // FALLBACK IF THE RTC DOES NOT TICK


unsigned long last_clock_update = 0;
int last_rtc_second = -1;
uint8_t current_led_level = 0; // PERCEIVED BRIGHTNESS to fade from 0-255
uint8_t target_led_level = 0;
unsigned long last_led_fade = 0;
unsigned long last_led_state = 0;
uint8_t reported_led_mode = 0xFF;
uint8_t reported_led_level = 0;
bool last_brght_button_state = false;


String readString;
bool cmd_started = false;
bool cmd_complete = false;
#define CMD_START_CHARACTER '%'
#define CMD_SEPERATION_CHARACTER '_'
//EEPROM MANAGED VARIABLES
uint8_t led_mode = 0;
uint8_t manual_led_percent = 100;
uint8_t daylightsaving_enabled = 0;
bool summertime_now = false; // EU SUMMER TIME IS ACTIVE ACCORDING TO THE RTC, INDEPENDENT OF daylightsaving_enabled


RTC_DS1307 rtc;

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
  manual_led_percent = EEPROM.read(EEPROM_INDEX_LED_PERCENT);
  // EMPTY EEPROM READS 0xFF
  if(led_mode > LED_MODE_MANUAL){
    led_mode = 0;
  }
  if(manual_led_percent > 100){
    manual_led_percent = 100;
  }
  if(daylightsaving_enabled > 1){
    daylightsaving_enabled = 1;
  }

  update_according_led_mode(led_mode);
  //INIT I2C
  Wire.begin();
  //CHECK IF RTC IS PRESENT
  //SETUP TIME
  if(check_for_i2c_device(DS1307_CTRL_ID)){
    //CHECK IF RTC IS IN STOP MODE SO ONE INITIAL SET IS NEEDED
    // ITS NOT WORKING IF THE TIMLIB IS USED DIRECTLY
    if(rtc.begin()){
      if (!rtc.isrunning()) {
        Serial.println("RTC lost power, let's set the time!");
        rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
      }else{
        // KEEP THE RTC TIME, THE NETWORK MODULE SYNCS IT
        send_status("log", "RTC RUNNING");
      }
       
    }else{
      send_status("log", "!RtcChipPresent");
    }
    send_status("log", "RtcFound");
  }else{
    send_status("log", "RtcNotFound");
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

uint16_t crc16(const String _data, const uint16_t _poly) {
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


void send_time_to_clock(const int _h, const int _m, const int _s, bool _signal_ok){
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


// PERCEIVED BRIGHTNESS 0-255 => PWM DUTY 0-255
uint8_t led_level_to_pwm(const uint8_t _level){
  return (uint8_t)(pow(_level / 255.0, LED_GAMMA) * 255.0 + 0.5);
}

// FADE IN DURING THE START HOUR (3 TIMES FASTER), ON UNTIL THE END HOUR, FADE OUT DURING THE END HOUR
uint8_t get_auto_led_level(const int _h, const int _m){
  if(_h == LIGHTMODE_AUTO_HOUR_START){
    return map(min(_m * 3, 60), 0, 60, 0, LIGHTMODE_AUTO_MAX_LEVEL);
  }else if(_h > LIGHTMODE_AUTO_HOUR_START && _h < LIGHTMODE_AUTO_HOUR_END){
    return LIGHTMODE_AUTO_MAX_LEVEL;
  }else if(_h == LIGHTMODE_AUTO_HOUR_END){
    // 0-59 SO THE LED IS OFF AT THE END OF THE HOUR
    return map(_m, 0, 59, LIGHTMODE_AUTO_MAX_LEVEL, 0);
  }
  return 0;
}

// LEVELS ARE PERCEIVED BRIGHTNESS, MATCHING THE 10% 20% 50% 70% 100% BUTTONS OF THE WEBINTERFACE
void update_according_led_mode(const uint8_t _led_mode){
    const uint8_t led_mode = _led_mode % (LED_MODE_MANUAL + 1);
    if(led_mode == 0){
      target_led_level = 0;
    }else if(led_mode == 1){
      target_led_level = 26;
    }else if(led_mode == 2){
      target_led_level = 51;
    }else if(led_mode == 3){
      target_led_level = 128;
    }else if(led_mode == 4){
      target_led_level = 179;
    }else if(led_mode == 5){
      target_led_level = 255;
     }else if(led_mode == LED_MODE_AUTO){
       //AUTO, TARGET IS SET EVERY SECOND IN THE LOOP
       target_led_level = 0;
       current_led_level = 80;//INDICATE CLOCK IS IN AUTO
    }else if(led_mode == LED_MODE_MANUAL){
      target_led_level = (uint16_t)manual_led_percent * 255 / 100;
    }
}

// %ls_<MODE>,<PERCEIVED BRIGHTNESS 0-100>, READ BY THE NETWORK MODULE FOR HOME ASSISTANT
// %ds_<DAYLIGHT SAVING ENABLED 0/1>,<SUMMER TIME NOW 0/1>, SENT TOGETHER WITH THE LIGHT STATE
void send_led_state(){
  reported_led_mode = led_mode;
  reported_led_level = target_led_level;
  last_led_state = millis();
  send_status("ls", String(led_mode) + "," + String(((uint16_t)target_led_level * 100 + 127) / 255));
  send_status("ds", String(daylightsaving_enabled > 0 ? 1 : 0) + "," + String(summertime_now ? 1 : 0));
}
  

// SET TIME
//SET DATE
// SET DLS
// SET BRIGHNESS target_led_level




void loop() {

  
  

  /// BUTTOM FOR LIGHTMODE TOGGLE
  if(digitalRead(PIN_BUTTON_BRGHT) == LOW && last_brght_button_state == true){
    last_brght_button_state = false;
    led_mode++;
    if(led_mode > LED_MODE_AUTO){
      led_mode = 0;
    }
    
    send_status("log", "LedMode" + String(led_mode));
    //SAVE LED MODE
    EEPROM.update(EEPROM_INDEX_LED_MODE,led_mode);
    update_according_led_mode(led_mode);
    send_led_state();
    
  }else if(digitalRead(PIN_BUTTON_BRGHT) == HIGH){
    last_brght_button_state = true;
  }


  // SEND THE TIME TO THE CLOCK DRIVES WHEN THE RTC SECOND CHANGES, SO THE DRIVES RUN IN SYNC WITH THE RTC
  // THE RTC IS READ IN EVERY LOOP (~10ms), FALLBACK EVERY 1s IF IT DOES NOT TICK
  const unsigned long currentMillis = millis();
  const DateTime now = rtc.now();
  if (now.second() != last_rtc_second || (currentMillis - last_clock_update) >= CLOCK_UPDATE_INTERAL) {
    last_rtc_second = now.second();
    last_clock_update = currentMillis;
    DEBUG_SERIAL.println(".");
      int h = now.hour();
      // RTC RUNS ON STANDARD TIME, ADD ONE HOUR DURING SUMMER TIME
      summertime_now = summertime_eu(now.year(), now.month(), now.day(), h);
      if (daylightsaving_enabled > 0 && summertime_now)
      {
        h = (h + 1) % 24;
      }
      //SEND TIME TO CLOCK-DRIVES
      send_time_to_clock(h, now.minute(),now.second(), true);

      //ALSO HANDLE LED MODE AUTO
      if(led_mode == LED_MODE_AUTO){
        target_led_level = get_auto_led_level(h, now.minute());
      }

    // REPORT THE LIGHT STATE ON EVERY CHANGE AND PERIODICALLY
    if(led_mode != reported_led_mode || target_led_level != reported_led_level || (currentMillis - last_led_state) >= LED_STATE_INTERVAL){
      send_led_state();
    }
  }
  
  // UPDATE LEDS WITH A TIME BASED RAMP UP/DOWN, INDEPENDENT OF THE LOOP DURATION
  if(target_led_level != current_led_level && (currentMillis - last_led_fade) >= LED_FADE_INTERVAL){
    last_led_fade = currentMillis;
    if(target_led_level < current_led_level){
      current_led_level--;
    }else{
      current_led_level++;
    }
    analogWrite(PIN_LED_PWM, led_level_to_pwm(current_led_level));
  }


    // READ WITHOUT DELAY, A COMMAND IS ONLY PROCESSED AFTER THE COMPLETE LINE WAS RECEIVED
    // SO THE TIME IS SET RIGHT WHEN THE LAST BYTE ARRIVES
    while (WIFIEXTENTION_SERIAL.available() > 0 && !cmd_complete) {
      const char c = WIFIEXTENTION_SERIAL.read();
      if (c == '\n') {
        if (cmd_started) {
          readString += CMD_SEPERATION_CHARACTER;
          cmd_complete = true;
        }
        cmd_started = false;
      }else if(c == CMD_START_CHARACTER){
        cmd_started = true;
        readString = "";
      }else if(cmd_started && c != '\r'){
        readString += c;
      }
    }

    if (cmd_complete) {
      cmd_complete = false;
      const String cmd = getValue(readString, CMD_SEPERATION_CHARACTER, 0);
      const String cmd_value = getValue(readString, CMD_SEPERATION_CHARACTER, 1);
      const String cmd_crc = getValue(getValue(readString, CMD_SEPERATION_CHARACTER, 2), CMD_SEPERATION_CHARACTER, 0);
      // DROP CORRUPTED FRAMES
      if (cmd_crc == "" || (uint16_t)cmd_crc.toInt() != crc16(cmd + cmd_value)) {
        send_status("crcerr", cmd + "-" + cmd_value + "-");
        readString = "";
        return;
      }
      //st
      //sd
      if(cmd == "sb" && cmd_value != ""){
        const int tmp = cmd_value.toInt();
        if(tmp >= 0 && tmp <= LED_MODE_MANUAL){
            led_mode = tmp;
            EEPROM.update(EEPROM_INDEX_LED_MODE,led_mode);
            update_according_led_mode(led_mode);
            send_led_state();
        }
      }else if(cmd == "sl" && cmd_value != ""){ //SET LIGHT BRIGHTNESS 0-100%
        const int tmp = cmd_value.toInt();
        if(tmp >= 0 && tmp <= 100){
          if(tmp == 0){
            led_mode = 0;
          }else{
            manual_led_percent = tmp;
            led_mode = LED_MODE_MANUAL;
            EEPROM.update(EEPROM_INDEX_LED_PERCENT, manual_led_percent);
          }
          EEPROM.update(EEPROM_INDEX_LED_MODE, led_mode);
          update_according_led_mode(led_mode);
          send_led_state();
        }
      }else if(cmd == "dls" && cmd_value != ""){
        const int dlsi = cmd_value.toInt();
        if(dlsi >= 0 && dlsi < 2){
          daylightsaving_enabled = dlsi;
          EEPROM.write(EEPROM_INDEX_DLS, daylightsaving_enabled);
          send_led_state();
        }
      }else if(cmd == "st" && cmd_value != ""){ //SET TIME
        const String sth = getValue(cmd_value, ':', 0);
        const String stm = getValue(cmd_value, ':', 1);
        const String sts =  getValue(getValue(cmd_value, ':', 2), '_', 0);;
        
        if(sth != "" && stm != ""){
          const int ih = sth.toInt();
          const int im = stm.toInt();
          const int is = sts.toInt();
          
          if(ih >= 0 && ih < 24 && im >= 0 && im < 60 && is >= 0 && is < 60){
            DateTime now = rtc.now();
            rtc.adjust(DateTime(now.year(), now.month(), now.day(), ih, im, is));
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
          
          if(iday > 0 && iday <= 31 && imonth > 0 && imonth <= 12 && iyear >= 2000){
            DateTime now = rtc.now();
            rtc.adjust(DateTime(iyear, imonth, iday, now.hour(), now.minute(), now.second()));
          }
        }
        DEBUG_SERIAL.println(sdday + ".." + sdmonth + ".." + sdyear);
      }

      send_status("gotcmd", cmd + "-"+cmd_value+"-");
      readString = "";
    } 

  delay(10);
}
