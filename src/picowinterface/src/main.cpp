#define VERSION "4.4.0"
// NETWORK INTERFACE FOR THE DBCLOCK
// RASPBERRY PI PICO W (WIFI) + WIZNET ETHERNET HAT FOR PICO (W5100S, LAN)
// REPLACES THE ESP8266 WIFI INTERFACE, SAME SERIAL PROTOCOL TO THE ARDUINO MEGA
//
// PLATFORMIO PROJECT, SEE platformio.ini. USES THE ARDUINO-PICO CORE: https://github.com/earlephilhower/arduino-pico

#include <Arduino.h>
#include <SPI.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <W5100lwIP.h>
#include <WebServer.h>
#include <SimpleMDNS.h>  // ALSO USED BY ArduinoOTA, ANNOUNCES ON LAN AND WIFI
#include <DNSServer.h>
#include <HTTPUpdateServer.h>
#include <ArduinoOTA.h>
#include <WiFiUdp.h>
#include <time.h>
#include <pico/unique_id.h>
#include <hardware/gpio.h>
#include <PubSubClient.h>

// NETWORK MODE IS DETECTED AT BOOT:
// PICO W + ETHERNET HAT  => LAN (+ WIFI IF CONFIGURED)
// PICO W                 => WIFI ONLY
// PICO + ETHERNET HAT    => LAN ONLY
// THE PICO W BUILD ALSO RUNS ON A PICO WITHOUT WIFI, THE CORE DETECTS THE CYW43 CHIP AT BOOT


// CONFIG -------------------------------------
#define WEBSERVER_PORT 80                                 // set the port for the webserver eg 80 8080
#define MDNS_NAME "dbclock"                               // set hostname
#define SERIAL_BAUD_RATE 9600
#define NTP_SEND_TIME_INTERVAL 60*60                      // sende zeit an uhr alle x sekunden
#define NTP_RETRY_INTERVAL 30                             // retry after x seconds if the ntp sync failed
#define DEFAULT_NTP_SERVER "pool.ntp.org"
#define NTP_LOCAL_PORT 2390
#define NTP_TIMEOUT 1500                                  // ms to wait for the ntp answer
#define NTP_UNIX_OFFSET 2208988800ULL                     // s BETWEEN 1900 AND 1970
#define DEFAULT_TIMEZONE 1
#define ETH_CONNECT_TIMEOUT 10000                         // ms to wait for a dhcp lease on the lan port
#define WIFI_CONNECT_TIMEOUT 20000                        // ms to wait for the wifi connection
#define WIFI_RECONNECT_INTERVAL 30000                     // ms between reconnect attempts if the wifi got lost
#define CONFIG_PORTAL_TIMEOUT 300                         // s, reboot and retry if no network got configured
#define WATCHDOG_TIMEOUT 8000                             // ms, max 8300
#define MQTT_DEFAULT_PORT 1883
#define MQTT_RECONNECT_INTERVAL 15000                     // ms between connection attempts to the broker, doubled after each failure
#define MQTT_RECONNECT_MAX 300000                         // ms, max interval, a failed attempt blocks the loop for up to MQTT_TIMEOUT
#define MQTT_TIMEOUT 2000                                 // ms for dns and tcp connect, below the watchdog timeout
#define MQTT_DISCOVERY_PREFIX "homeassistant"
#define MEGA_LINK_TIMEOUT 5000                            // ms, THE MEGA SENDS ITS CLOCK TIME EVERY SECOND

// SERIAL TO THE ARDUINO MEGA (Serial3), PICO IS 3.3V ONLY!
#define CLOCK_SERIAL Serial1
#define CLOCK_SERIAL_TX_PIN 0  // GP0 -> MEGA RX3
#define CLOCK_SERIAL_RX_PIN 1  // GP1 <- MEGA TX3, OPTIONAL, ONLY WITH VOLTAGE DIVIDER: LIGHT STATE FEEDBACK
#define DEBUG_SERIAL Serial    // USB

// WIZNET ETHERNET HAT FOR PICO PINOUT (SPI0)
#define ETH_PIN_MISO 16
#define ETH_PIN_CS 17
#define ETH_PIN_SCK 18
#define ETH_PIN_MOSI 19
#define ETH_PIN_RST 20
#define ETH_PIN_INT 21
#define ETH_SPI_SPEED 20000000
// END CONFIG ---------------------------------




unsigned long last = 0;
unsigned long next_sync_in = 0;
bool ntp_force_update = true;
unsigned long reboot_requested = 0;
String BOARD_INFO = "DBCLOCKNETMODULE_FW_" + String(VERSION);

const String ap_name = "DBClockConfiguration";

//FILES FOR STORiNG CONFIGURATION DATA
const char* file_ntp_server = "/ntp_server.txt";
const char* file_timezone = "/timezone.txt";
const char* file_wifi_ssid = "/wifi_ssid.txt";
const char* file_wifi_pass = "/wifi_pass.txt";
const char* file_mqtt_host = "/mqtt_host.txt";
const char* file_mqtt_port = "/mqtt_port.txt";
const char* file_mqtt_user = "/mqtt_user.txt";
const char* file_mqtt_pass = "/mqtt_pass.txt";
const char* file_mqtt_topic = "/mqtt_topic.txt";
const char* file_light_percent = "/light_percent.txt";
//VARS

int timezone = 0;
String ntp_server_url = DEFAULT_NTP_SERVER;
String wifi_ssid = "";
String wifi_pass = "";
String last_error = "";
String chip_id = "";
String hostname = "";

// NTP STATE, MICROSECOND RESOLUTION
bool ntp_time_set = false;
uint64_t ntp_utc_us = 0;       // UTC AT ntp_sync_us
uint64_t ntp_sync_us = 0;      // time_us_64() OF THE LAST SYNC
int32_t ntp_delay_ms = 0;      // NETWORK ROUND TRIP OF THE LAST SYNC
String ntp_server_ip = "";

// NETWORK STATE
bool wifi_present = false;
bool eth_present = false;
bool config_portal_active = false;
unsigned long config_portal_started = 0;
unsigned long wifi_last_reconnect = 0;
String reported_ip = "";

// LIGHT STATE, MODES OF THE MEGA: 0 OFF, 1-5 FIXED, 6 AUTO, 7 MANUAL (sl COMMAND)
#define LIGHT_MODE_AUTO 6
#define LIGHT_MODE_MANUAL 7
const uint8_t LIGHT_MODE_PERCENT[] = { 0, 10, 20, 50, 70, 100 };
uint8_t light_mode = 0;
uint8_t light_percent = 0;          // PERCEIVED BRIGHTNESS 0-100
uint8_t light_last_percent = 100;   // USED FOR ON WITHOUT BRIGHTNESS
bool light_confirmed = false;       // TRUE IF THE STATE WAS REPORTED BY THE MEGA

// FEEDBACK FROM THE MEGA
bool mega_rx_seen = false;
bool mega_rx_started = false;
unsigned long mega_last_rx = 0;
String mega_rx_line = "";
String mega_clock_time = "";

// MQTT, EMPTY HOST = DISABLED
String mqtt_host = "";
uint16_t mqtt_port = MQTT_DEFAULT_PORT;
String mqtt_user = "";
String mqtt_pass = "";
String mqtt_topic = "";  // BASE TOPIC, EMPTY = dbclock/<id>
String mqtt_old_base = "";  // RETAINED MESSAGES BELOW THIS TOPIC ARE REMOVED AFTER THE NEXT CONNECT
String mqtt_state = "disabled";
unsigned long mqtt_last_connect = 0;
unsigned long mqtt_retry_interval = MQTT_RECONNECT_INTERVAL;


// INSTANCES --------------------------------------------
WebServer server(WEBSERVER_PORT);
HTTPUpdateServer http_updater;
DNSServer dns_server;
Wiznet5100lwIP eth(ETH_PIN_CS, SPI, ETH_PIN_INT);

WiFiUDP ntpUDP;
WiFiClient mqtt_net;
PubSubClient mqtt(mqtt_net);

bool has_network();
bool wifi_connected();
String get_primary_ip();
String hardware_label();
String active_connection();
void mqtt_publish_state();
uint64_t get_local_us();
struct tm get_local_tm(const uint64_t _local_us);






// WEBINTERFACE, NO EXTERNAL RESOURCES SO IT ALSO WORKS IN THE CONFIGURATION ACCESS POINT
const char PAGE_HEAD[] PROGMEM = R"HTML(<!DOCTYPE html><html lang='en'><head>
<meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1'>
<title>DBClock</title>
<style>
:root{--bg:#f3f4f6;--card:#fff;--text:#111827;--muted:#6b7280;--line:#e5e7eb;--soft:#f9fafb;--accent:#d61f26;--ok:#15803d;--okbg:#dcfce7;--warn:#b45309;--warnbg:#fef3c7;--danger:#b91c1c}
@media (prefers-color-scheme:dark){:root{--bg:#0d1117;--card:#161b22;--text:#e6edf3;--muted:#8b949e;--line:#30363d;--soft:#0d1117;--ok:#3fb950;--okbg:#12261a;--warn:#d29922;--warnbg:#2b2111;--danger:#f85149}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font:16px/1.45 -apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Helvetica,Arial,sans-serif}
.wrap{max-width:620px;margin:0 auto;padding:16px}
header{display:flex;align-items:center;justify-content:space-between;gap:12px;margin:4px 0 16px}
.brand{display:flex;align-items:center;gap:10px;min-width:0}
.brand svg{flex:none}
.brand b{display:block;font-size:19px}
.brand small{display:block;color:var(--muted);font-size:13px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.card{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:16px;margin-bottom:14px}
h2{font-size:12px;font-weight:700;letter-spacing:.08em;text-transform:uppercase;color:var(--muted);margin:0 0 12px}
.time{font-size:clamp(44px,14vw,64px);font-weight:700;line-height:1.05;font-variant-numeric:tabular-nums}
.sub{color:var(--muted);font-size:14px;margin-top:4px}
.pill{flex:none;font-size:12px;font-weight:700;padding:5px 10px;border-radius:99px;background:var(--okbg);color:var(--ok)}
.pill.off{background:var(--warnbg);color:var(--warn)}
.grid{display:grid;grid-template-columns:repeat(3,1fr);gap:8px}
.grid .wide{grid-column:1/-1}
.slider{display:flex;align-items:center;gap:12px}
.slider input{flex:1;min-height:0;padding:0;border:0;background:none;accent-color:var(--accent)}
.slider output{min-width:52px;text-align:right;font-weight:600;font-variant-numeric:tabular-nums}
.two{display:grid;grid-template-columns:1fr 1fr;gap:8px}
button,.btn{display:flex;align-items:center;justify-content:center;width:100%;min-height:44px;padding:10px 12px;border:1px solid var(--line);border-radius:10px;background:var(--soft);color:var(--text);font:inherit;font-weight:600;text-align:center;text-decoration:none;cursor:pointer}
button:active,.btn:active{transform:scale(.98)}
button.primary{background:var(--accent);border-color:var(--accent);color:#fff}
button.danger{color:var(--danger)}
input{width:100%;min-height:44px;padding:10px 12px;border:1px solid var(--line);border-radius:10px;background:var(--soft);color:var(--text);font:inherit}
label{display:block;font-size:14px;color:var(--muted);margin:0 0 6px}
.field{margin-bottom:12px}
.mt{margin-top:12px}
.row{display:flex;justify-content:space-between;gap:16px;padding:9px 0;border-top:1px solid var(--line);font-size:15px}
.row:first-of-type{border-top:0;padding-top:0}
.row span:first-child{color:var(--muted);flex:none}
.row span:last-child{text-align:right;overflow-wrap:anywhere}
.dot{display:inline-block;width:8px;height:8px;border-radius:50%;background:var(--ok);margin-right:6px;vertical-align:middle}
.dot.off{background:var(--muted)}
form{margin:0}
a{color:inherit;text-underline-offset:3px}
.msg{font-size:14px;color:var(--muted);overflow-wrap:anywhere}
footer{text-align:center;color:var(--muted);font-size:12px;padding:4px 0 24px}
</style></head><body><div class='wrap'>
<header><div class='brand'><svg width='36' height='36' viewBox='0 0 36 36' aria-hidden='true'><circle cx='18' cy='18' r='16' fill='none' stroke='currentColor' stroke-width='2.5'/><path d='M18 8v10l6 4' fill='none' stroke='currentColor' stroke-width='2.5' stroke-linecap='round'/><circle cx='18' cy='18' r='2' fill='#d61f26'/></svg><div><b>DBClock</b><small>)HTML";

const char PAGE_END[] PROGMEM = R"HTML(</div></body></html>)HTML";





uint16_t crc16(String _data, uint16_t _poly = 0x8408) {
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

// %<CMD>_<PAYLOAD>_<CRC>\r\n
String build_cmd_frame(const String _command, const String _payload) {
  return "%" + _command + "_" + _payload + "_" + crc16(_command + _payload) + "\r\n";
}

void send_cmd_str(const String _command, const String _payload) {
  const String frame = build_cmd_frame(_command, _payload);
  CLOCK_SERIAL.print(frame);
  DEBUG_SERIAL.print(frame);
}

String two_digits(const int _v) {
  return (_v < 10 ? "0" : "") + String(_v);
}

String format_time(const struct tm& _t) {
  return two_digits(_t.tm_hour) + ":" + two_digits(_t.tm_min) + ":" + two_digits(_t.tm_sec);
}

String format_date(const struct tm& _t) {
  return two_digits(_t.tm_mday) + "." + two_digits(_t.tm_mon + 1) + "." + String(_t.tm_year + 1900);
}

void set_restdefaults(){
  send_cmd_str("res", "1");
}
// ONLY READ THE FIRST LINE UNTIL NEW LINE !!!!!
String read_file(const char* _file, String _default = "") {
  // NOT SAVED YET IS NO ERROR, USE THE DEFAULT
  if (!LittleFS.exists(_file)) {
    return _default;
  }
  File f = LittleFS.open(_file, "r");
  String tmp = _default;
  if (!f) {
    last_error = "open filesystem " + String(_file) + " failed";
  } else {
    tmp = f.readStringUntil('\n');
    f.close();
  }
  return tmp;
}

void restore_eeprom_values() {
  ntp_server_url = read_file(file_ntp_server, DEFAULT_NTP_SERVER);
  timezone = read_file(file_timezone, String(DEFAULT_TIMEZONE)).toInt();
  wifi_ssid = read_file(file_wifi_ssid, "");
  wifi_pass = read_file(file_wifi_pass, "");
  mqtt_host = read_file(file_mqtt_host, "");
  mqtt_port = read_file(file_mqtt_port, String(MQTT_DEFAULT_PORT)).toInt();
  mqtt_user = read_file(file_mqtt_user, "");
  mqtt_pass = read_file(file_mqtt_pass, "");
  mqtt_topic = read_file(file_mqtt_topic, "");
  light_last_percent = constrain(read_file(file_light_percent, "100").toInt(), 1, 100);
}

bool write_file(const char* _file, String _content) {
  File f = LittleFS.open(_file, "w");
  if (!f) {
    last_error = "Oeffnen der Datei fehlgeschlagen";
    return false;
  }
  f.print(_content);
  f.close();
  return true;
}

void save_values_to_eeprom() {
  write_file(file_ntp_server, ntp_server_url);
  write_file(file_timezone, String(timezone));
  write_file(file_wifi_ssid, wifi_ssid);
  write_file(file_wifi_pass, wifi_pass);
  write_file(file_mqtt_host, mqtt_host);
  write_file(file_mqtt_port, String(mqtt_port));
  write_file(file_mqtt_user, mqtt_user);
  write_file(file_mqtt_pass, mqtt_pass);
  write_file(file_mqtt_topic, mqtt_topic);
}

void write_deffault_to_eeprom() {
  timezone = DEFAULT_TIMEZONE;
  ntp_server_url = DEFAULT_NTP_SERVER;
  save_values_to_eeprom();
  restore_eeprom_values();

  set_restdefaults();
}



String get_chip_id() {
  char id[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
  pico_get_unique_board_id_string(id, sizeof(id));
  String tmp = String(id);
  tmp.toLowerCase();
  // LAST 6 DIGITS ARE ENOUGH TO DISTINGUISH THE CLOCKS
  return tmp.substring(tmp.length() - 6);
}

String html_escape(String _in) {
  _in.replace("&", "&amp;");
  _in.replace("<", "&lt;");
  _in.replace(">", "&gt;");
  _in.replace("'", "&#39;");
  _in.replace("\"", "&quot;");
  return _in;
}



// LIGHT ------------------------------------------------
// THE MEGA DRIVES THE LED STRIP, THE NETWORK MODULE SENDS sb (MODE) AND sl (BRIGHTNESS 0-100%)
// THE STATE IS CONFIRMED BY THE %ls FRAMES OF THE MEGA IF ITS TX3 IS CONNECTED TO GP1

String light_mode_key() {
  if (light_mode == 0) {
    return "off";
  } else if (light_mode == LIGHT_MODE_AUTO) {
    return "auto";
  } else if (light_mode == LIGHT_MODE_MANUAL) {
    return "manual";
  }
  return String(LIGHT_MODE_PERCENT[light_mode]);
}

bool light_is_on() {
  return light_mode != 0;
}

void light_state_changed() {
  light_confirmed = false;
  mqtt_publish_state();
}

void light_remember_percent() {
  if (light_percent > 0 && light_mode != LIGHT_MODE_AUTO && light_percent != light_last_percent) {
    light_last_percent = light_percent;
    write_file(file_light_percent, String(light_last_percent));
  }
}

// 0 OFF, 1-5 = 10/20/50/70/100%, 6 AUTO, 7 LAST MANUAL BRIGHTNESS OF THE MEGA
void light_set_mode(const uint8_t _mode) {
  if (_mode > LIGHT_MODE_MANUAL) {
    return;
  }
  send_cmd_str("sb", String(_mode));
  light_mode = _mode;
  if (_mode < LIGHT_MODE_AUTO) {
    light_percent = LIGHT_MODE_PERCENT[_mode];
  } else if (_mode == LIGHT_MODE_MANUAL) {
    light_percent = light_last_percent;
  }
  light_remember_percent();
  light_state_changed();
}

void light_set_percent(int _percent) {
  _percent = constrain(_percent, 0, 100);
  send_cmd_str("sl", String(_percent));
  light_mode = _percent > 0 ? LIGHT_MODE_MANUAL : 0;
  light_percent = _percent;
  light_remember_percent();
  light_state_changed();
}

// ON WITHOUT BRIGHTNESS RESTORES THE LAST BRIGHTNESS
void light_set_on(const bool _on) {
  if (!_on) {
    light_set_mode(0);
  } else if (!light_is_on()) {
    light_set_percent(light_last_percent);
  }
}

String light_status_text() {
  if (!light_is_on()) {
    return "Off";
  }
  String s = "On, " + String(light_percent) + " %";
  if (light_mode == LIGHT_MODE_AUTO) {
    s += " (auto)";
  }
  return s;
}



// FEEDBACK FROM THE MEGA (TX3 -> GP1) ---------------------
// %ls_<MODE>,<PERCENT>_<CRC> LIGHT STATE, %ct_<HH:MM:SS>_<CRC> TIME SENT TO THE CLOCK DRIVES

bool mega_link_ok() {
  return mega_rx_seen && (millis() - mega_last_rx) < MEGA_LINK_TIMEOUT;
}

void handle_mega_frame(const String& _frame) {
  const int a = _frame.indexOf('_');
  const int b = _frame.lastIndexOf('_');
  if (a < 1 || b <= a) {
    return;
  }
  const String cmd = _frame.substring(0, a);
  const String payload = _frame.substring(a + 1, b);
  // IGNORE NOISE, E.G. IF GP1 IS NOT CONNECTED
  if ((uint16_t)_frame.substring(b + 1).toInt() != crc16(cmd + payload)) {
    return;
  }
  mega_last_rx = millis();
  mega_rx_seen = true;
  if (cmd == "ls") {
    const int c = payload.indexOf(',');
    if (c < 1) {
      return;
    }
    const int mode = payload.substring(0, c).toInt();
    const int percent = payload.substring(c + 1).toInt();
    if (mode < 0 || mode > LIGHT_MODE_MANUAL || percent < 0 || percent > 100) {
      return;
    }
    const bool changed = mode != light_mode || percent != light_percent || !light_confirmed;
    light_mode = mode;
    light_percent = percent;
    light_remember_percent();
    if (changed) {
      mqtt_publish_state();
    }
    light_confirmed = true;
  } else if (cmd == "ct") {
    mega_clock_time = payload;
  }
}

void read_mega() {
  while (CLOCK_SERIAL.available() > 0) {
    const char c = CLOCK_SERIAL.read();
    if (c == '%') {
      mega_rx_line = "";
      mega_rx_started = true;
    } else if (c == '\n') {
      if (mega_rx_started) {
        handle_mega_frame(mega_rx_line);
      }
      mega_rx_started = false;
    } else if (mega_rx_started && c != '\r') {
      mega_rx_line += c;
      if (mega_rx_line.length() > 64) {
        mega_rx_started = false;
      }
    }
  }
}



// MQTT / HOME ASSISTANT ----------------------------------
// THE CLOCK APPEARS IN HOME ASSISTANT AS DIMMABLE LIGHT WITH THE EFFECT "Auto" AND A "Sync time" BUTTON (MQTT DISCOVERY)

String mqtt_base() {
  return mqtt_topic.length() ? mqtt_topic : "dbclock/" + chip_id;
}

// TRIM SPACES AND SLASHES
String mqtt_clean_topic(String _topic) {
  _topic.trim();
  while (_topic.startsWith("/")) {
    _topic.remove(0, 1);
  }
  while (_topic.endsWith("/")) {
    _topic.remove(_topic.length() - 1);
  }
  return _topic;
}

// REMOVE THE RETAINED MESSAGES BELOW THE OLD BASE TOPIC
void mqtt_clear_retained(const String _base) {
  mqtt.publish((_base + "/status").c_str(), "", true);
  mqtt.publish((_base + "/light/state").c_str(), "", true);
  mqtt.publish((_base + "/light/brightness").c_str(), "", true);
  mqtt.publish((_base + "/light/effect").c_str(), "", true);
}

String mqtt_device_json() {
  return "\"device\":{\"identifiers\":[\"dbclock_" + chip_id + "\"],\"name\":\"DBClock\",\"manufacturer\":\"DIY\","
         "\"model\":\"" + hardware_label() + "\",\"sw_version\":\"" + String(VERSION) + "\","
         "\"configuration_url\":\"http://" + get_primary_ip() + "/\"}";
}

void mqtt_publish_discovery() {
  const String b = mqtt_base();
  const String id = "dbclock_" + chip_id;
  const String availability = "\"availability_topic\":\"" + b + "/status\",";
  const String light = "{\"name\":\"Light\",\"unique_id\":\"" + id + "_light\"," + availability +
                       "\"command_topic\":\"" + b + "/light/set\",\"state_topic\":\"" + b + "/light/state\","
                       "\"brightness_command_topic\":\"" + b + "/light/brightness/set\",\"brightness_state_topic\":\"" + b + "/light/brightness\",\"brightness_scale\":100,"
                       "\"effect_command_topic\":\"" + b + "/light/effect/set\",\"effect_state_topic\":\"" + b + "/light/effect\",\"effect_list\":[\"Auto\",\"None\"],"
                       "\"on_command_type\":\"brightness\"," + mqtt_device_json() + "}";
  mqtt.publish((String(MQTT_DISCOVERY_PREFIX) + "/light/" + id + "/light/config").c_str(), light.c_str(), true);
  const String sync = "{\"name\":\"Sync time\",\"unique_id\":\"" + id + "_sync\"," + availability +
                      "\"command_topic\":\"" + b + "/sync/set\",\"icon\":\"mdi:clock-check-outline\"," + mqtt_device_json() + "}";
  mqtt.publish((String(MQTT_DISCOVERY_PREFIX) + "/button/" + id + "/sync/config").c_str(), sync.c_str(), true);
}

void mqtt_publish_state() {
  if (!mqtt.connected()) {
    return;
  }
  const String b = mqtt_base();
  mqtt.publish((b + "/light/state").c_str(), light_is_on() ? "ON" : "OFF", true);
  mqtt.publish((b + "/light/brightness").c_str(), String(light_percent).c_str(), true);
  mqtt.publish((b + "/light/effect").c_str(), light_mode == LIGHT_MODE_AUTO ? "Auto" : "None", true);
}

void mqtt_callback(char* _topic, byte* _payload, unsigned int _length) {
  // COPY FIRST, PUBLISHING REUSES THE BUFFER OF PubSubClient
  const String topic = String(_topic);
  String msg;
  for (unsigned int i = 0; i < _length; i++) {
    msg += (char)_payload[i];
  }
  msg.trim();
  const String b = mqtt_base();
  if (topic == b + "/light/set") {
    light_set_on(msg.equalsIgnoreCase("ON"));
  } else if (topic == b + "/light/brightness/set") {
    light_set_percent(msg.toInt());
  } else if (topic == b + "/light/effect/set") {
    if (msg == "Auto") {
      light_set_mode(LIGHT_MODE_AUTO);
    } else if (light_mode == LIGHT_MODE_AUTO) {
      light_set_percent(light_last_percent);
    }
  } else if (topic == b + "/sync/set") {
    ntp_force_update = true;
  } else if (topic == String(MQTT_DISCOVERY_PREFIX) + "/status" && msg == "online") {
    // HOME ASSISTANT RESTARTED
    mqtt_publish_discovery();
    mqtt_publish_state();
  }
  last_error = "mqtt: " + topic + " = " + msg;
}

void mqtt_reconnect_now() {
  mqtt.disconnect();
  mqtt_last_connect = 0;
  mqtt_retry_interval = MQTT_RECONNECT_INTERVAL;
}

void mqtt_connect_failed(const String _reason) {
  mqtt_retry_interval = min(mqtt_retry_interval * 2, (unsigned long)MQTT_RECONNECT_MAX);
  mqtt_state = _reason + ", retry in " + String(mqtt_retry_interval / 1000) + " s";
}

void mqtt_loop() {
  if (mqtt_host.length() == 0) {
    mqtt_state = "disabled";
    return;
  }
  if (mqtt.connected()) {
    mqtt.loop();
    return;
  }
  if (!has_network() || (mqtt_last_connect != 0 && (millis() - mqtt_last_connect) < mqtt_retry_interval)) {
    return;
  }
  mqtt_last_connect = millis();
  // RESOLVE AND CONNECT WITH SHORT TIMEOUTS, THE WATCHDOG RUNS
  IPAddress broker;
  rp2040.wdt_reset();
  if (!broker.fromString(mqtt_host) && !hostByName(mqtt_host.c_str(), broker, MQTT_TIMEOUT)) {
    mqtt_connect_failed("broker " + mqtt_host + " could not be resolved");
    return;
  }
  rp2040.wdt_reset();
  mqtt.setServer(broker, mqtt_port);
  const String b = mqtt_base();
  const String client_id = "dbclock_" + chip_id;
  const String will = b + "/status";
  bool ok;
  if (mqtt_user.length() > 0) {
    ok = mqtt.connect(client_id.c_str(), mqtt_user.c_str(), mqtt_pass.c_str(), will.c_str(), 0, true, "offline");
  } else {
    ok = mqtt.connect(client_id.c_str(), will.c_str(), 0, true, "offline");
  }
  rp2040.wdt_reset();
  if (!ok) {
    mqtt_connect_failed("connection to " + broker.toString() + ":" + String(mqtt_port) + " failed (" + String(mqtt.state()) + ")");
    return;
  }
  mqtt_retry_interval = MQTT_RECONNECT_INTERVAL;
  mqtt_state = "connected to " + broker.toString() + ":" + String(mqtt_port);
  if (mqtt_old_base.length() && mqtt_old_base != b) {
    mqtt_clear_retained(mqtt_old_base);
  }
  mqtt_old_base = "";
  mqtt.publish(will.c_str(), "online", true);
  mqtt.subscribe((b + "/light/set").c_str());
  mqtt.subscribe((b + "/light/+/set").c_str());
  mqtt.subscribe((b + "/sync/set").c_str());
  mqtt.subscribe((String(MQTT_DISCOVERY_PREFIX) + "/status").c_str());
  mqtt_publish_discovery();
  mqtt_publish_state();
}



void handleSave() {
  bool save = true;
  const String old_wifi_ssid = wifi_ssid;
  // PARSE ALL GET/POST ARGUMENTS
  for (uint8_t i = 0; i < server.args(); i++) {
    // timezone +-12hours
    if (server.argName(i) == "timezone") {
      timezone = server.arg(i).toInt();
      if (timezone > 12 || timezone < -12) {
        timezone = 0;
      }
      last_error = "set timezone to " + String(timezone);
      ntp_force_update = true;
    } else if (server.argName(i) == "ntp_server_url") {
      if (server.arg(i) != "") {
        ntp_server_url = server.arg(i);
        last_error = "set ntp_server_url to " + ntp_server_url;
        ntp_force_update = true;
      }
    } else if (server.argName(i) == "wifi_ssid") {
      wifi_ssid = server.arg(i);
      last_error = "set wifi ssid to " + wifi_ssid + ", rebooting";
      reboot_requested = millis();
    } else if (server.argName(i) == "wifi_pass") {
      // EMPTY FIELD = KEEP THE STORED PASSWORD, UNLESS THE SSID CHANGED
      if (server.arg(i) != "" || server.arg("wifi_ssid") != old_wifi_ssid) {
        wifi_pass = server.arg(i);
      }
    } else if (server.argName(i) == "wifi_forget") {
      wifi_ssid = "";
      wifi_pass = "";
      last_error = "wifi credentials deleted, rebooting";
      reboot_requested = millis();
    } else if (server.argName(i) == "fsformat") {
      if (LittleFS.format()) {  // formats the filesystem= resets all settings
        last_error = "Datei-System formatiert";
      } else {
        last_error = "Datei-System formatiert Error";
      }
      restore_eeprom_values();
      save = false;
    } else if (server.argName(i) == "eepromread") {
      restore_eeprom_values();  //LOAD CURRENT SAVED DATA
      last_error = "stored config loaded";
    } else if (server.argName(i) == "factreset") {
      write_deffault_to_eeprom();  //LOAD FACOTRY RESET
    } else if (server.argName(i) == "sendtime") {
      ntp_force_update = true;
      last_error = "time will be sent to the clock";
    } else if (server.argName(i) == "reboot") {
      last_error = "rebooting";
      reboot_requested = millis();
    } else if (server.argName(i) == "sb") {
      light_set_mode(server.arg(i).toInt());
      last_error = "light: " + light_status_text();
    } else if (server.argName(i) == "sl") {
      light_set_percent(server.arg(i).toInt());
      last_error = "light: " + light_status_text();
    } else if (server.argName(i) == "mqtt_host") {
      mqtt_host = server.arg(i);
      mqtt_host.trim();
      last_error = mqtt_host.length() ? "mqtt broker set to " + mqtt_host : "mqtt disabled";
      mqtt_reconnect_now();
    } else if (server.argName(i) == "mqtt_port") {
      mqtt_port = constrain(server.arg(i).toInt(), 1, 65535);
    } else if (server.argName(i) == "mqtt_user") {
      mqtt_user = server.arg(i);
    } else if (server.argName(i) == "mqtt_topic") {
      const String topic = mqtt_clean_topic(server.arg(i));
      if (topic.indexOf('+') >= 0 || topic.indexOf('#') >= 0) {
        last_error = "mqtt base topic must not contain + or #";
      } else if (topic != mqtt_topic) {
        if (mqtt_old_base.length() == 0) {
          mqtt_old_base = mqtt_base();
        }
        mqtt_topic = topic;
        last_error = "mqtt base topic set to " + mqtt_base();
        mqtt_reconnect_now();  // RESUBSCRIBES AND SENDS THE DISCOVERY WITH THE NEW TOPICS
      }
    } else if (server.argName(i) == "mqtt_discovery") {
      if (mqtt.connected()) {
        mqtt_publish_discovery();
        mqtt_publish_state();
        last_error = "mqtt discovery sent";
      } else {
        last_error = "mqtt not connected, discovery not sent";
      }
      save = false;
    } else if (server.argName(i) == "mqtt_pass") {
      // EMPTY FIELD = KEEP THE STORED PASSWORD, UNLESS THE USER WAS REMOVED
      if (server.arg(i) != "" || server.arg("mqtt_user") == "") {
        mqtt_pass = server.arg(i);
      }
    } else if (server.argName(i) == "plain") {
      // RAW POST BODY, NOTHING TO DO
    } else {
      send_cmd_str(server.argName(i), server.arg(i));  //SEND ALL OTHER COMMANDS TO THE CLOCK DIRECTLY
      last_error = "sent to clock: " + server.argName(i) + " = " + server.arg(i);
    }
  }
  //SAVE THESE DATA
  if (save) {
    save_values_to_eeprom();
  }

  String page = FPSTR(PAGE_HEAD);
  page += html_escape(hostname) + "</small></div></div></header>";
  // BACK TO THE MAIN PAGE, LONGER AFTER A REBOOT
  page += "<script>setTimeout(function(){location.href='/'}," + String(reboot_requested ? 10000 : 1200) + ")</script>";
  page += "<section class='card'><h2>" + String(reboot_requested ? "Rebooting" : "Saved") + "</h2><div class='msg'>" + html_escape(last_error) + "</div></section>";
  page += FPSTR(PAGE_END);
  server.send(200, "text/html", page);
}



// ALL WIFI CALLS MUST BE GUARDED, THEY HANG ON A PICO WITHOUT WIFI CHIP
// IN AP MODE WiFi.status() ALSO REPORTS WL_CONNECTED, SO THE STATION IP IS CHECKED AS WELL
bool wifi_connected() {
  return wifi_present && !config_portal_active && WiFi.status() == WL_CONNECTED && WiFi.localIP().isSet();
}

String hardware_label() {
  return String(wifi_present ? "Pico W" : "Pico") + (eth_present ? " + W5100S" : "");
}

String active_connection() {
  if (eth_present && eth.connected()) {
    return "LAN";
  } else if (wifi_connected()) {
    return "WiFi";
  } else if (config_portal_active) {
    return "Setup AP";
  }
  return "Offline";
}

String network_mode() {
  if (eth_present && wifi_present) {
    return "LAN+WIFI";
  } else if (eth_present) {
    return "LAN ONLY";
  } else if (wifi_present) {
    return "WIFI ONLY";
  }
  return "NO NETWORK HARDWARE";
}

String status_row(const String _label, const String _value) {
  return "<div class='row'><span>" + _label + "</span><span>" + _value + "</span></div>";
}

String dot(const bool _on) {
  return String("<i class='dot") + (_on ? "" : " off") + "'></i>";
}

String network_status() {
  String s = "<section class='card'><h2>Network</h2>";
  // LAN
  String lan;
  if (!eth_present) {
    lan = dot(false) + "not detected";
  } else if (eth.connected()) {
    lan = dot(true) + eth.localIP().toString();
  } else {
    // THE W5100S DRIVER CAN NOT DETECT THE LINK STATE
    lan = dot(false) + "no cable or waiting for DHCP";
  }
  s += status_row("LAN", lan);
  // WIFI
  String wifi;
  if (!wifi_present) {
    wifi = dot(false) + "not available";
  } else if (config_portal_active) {
    wifi = dot(true) + "setup access point " + html_escape(ap_name + "_" + chip_id);
  } else if (wifi_ssid.length() == 0) {
    wifi = dot(false) + "not configured";
  } else if (wifi_connected()) {
    wifi = dot(true) + html_escape(wifi_ssid) + ", " + WiFi.localIP().toString() + " (" + String(WiFi.RSSI()) + " dBm)";
  } else {
    wifi = dot(false) + "connecting to " + html_escape(wifi_ssid);
  }
  s += status_row("WiFi", wifi);
  s += status_row("Hostname", hostname + ".local");
  s += status_row("Hardware", hardware_label());

  // WIFI CREDENTIALS, ONLY ON BOARDS WITH WIFI CHIP
  if (wifi_present) {
    s += "<form class='mt' action='/save' method='POST'>"
         "<div class='field'><label for='ssid'>WiFi name</label><input id='ssid' name='wifi_ssid' value='" + html_escape(wifi_ssid) + "' autocomplete='off' autocapitalize='none'></div>"
         "<div class='field'><label for='pass'>WiFi password</label><input id='pass' type='password' name='wifi_pass' placeholder='" + String(wifi_pass.length() ? "unchanged" : "") + "'></div>"
         "<div class='two'><button class='primary'>Save and reboot</button>"
         "<button class='danger' name='wifi_forget' value='1' formnovalidate onclick=\"return confirm('Delete the WiFi settings and use LAN only?')\">Forget WiFi</button></div>"
         "</form>";
  }
  s += "</section>";
  return s;
}

void handleRoot() {
  String page = FPSTR(PAGE_HEAD);
  page += html_escape(hostname) + "</small></div></div>";
  page += String("<span class='pill") + (has_network() ? "'>" : " off'>") + active_connection() + "</span></header>";

  // TIME
  const String utc_offset = "UTC" + String(timezone >= 0 ? "+" : "") + String(timezone);
  page += "<section class='card'><h2>Time</h2>";
  if (ntp_time_set) {
    const uint64_t local_us = get_local_us();
    const struct tm now_tm = get_local_tm(local_us);
    const struct tm sync_tm = get_local_tm(ntp_utc_us + (int64_t)timezone * 3600LL * 1000000LL);
    page += "<div class='time' id='clk'>" + format_time(now_tm) + "</div>";
    page += "<div class='sub'>" + format_date(now_tm) + " &middot; " + utc_offset + " standard time</div>";
    page += "<div class='sub'>Last sync " + format_time(sync_tm) + " from " + html_escape(ntp_server_ip) + ", " + String(ntp_delay_ms) + " ms delay</div>";
    // LET THE CLOCK TICK IN THE BROWSER
    page += "<script>(function(){var s=" + String((uint32_t)((local_us / 1000ULL) % 86400000ULL)) + ",t=Date.now(),e=document.getElementById('clk');"
            "function p(n){return(n<10?'0':'')+n}"
            "setInterval(function(){var m=(s+Date.now()-t)%86400000;e.textContent=p(Math.floor(m/3600000))+':'+p(Math.floor(m/60000)%60)+':'+p(Math.floor(m/1000)%60)},250)})()</script>";
  } else {
    page += "<div class='time'>--:--:--</div><div class='sub'>Not synced yet</div>";
  }
  page += "<form class='mt' action='/save'><button class='primary' name='sendtime' value='1'>Send time to clock</button></form></section>";

  // LIGHT
  page += "<section class='card'><h2>Light</h2>";
  page += "<div class='sub' style='margin:0 0 12px'>" + light_status_text() + (light_confirmed ? "" : (mega_link_ok() ? " (waiting for the clock)" : " (no feedback from the clock)")) + "</div>";
  page += "<form action='/save' class='slider'><input type='range' name='sl' min='1' max='100' value='" + String(light_is_on() && light_mode != LIGHT_MODE_AUTO ? light_percent : light_last_percent) + "' "
          "oninput=\"this.nextElementSibling.textContent=this.value+' %'\" onchange='this.form.submit()' aria-label='Brightness'><output>" + String(light_is_on() && light_mode != LIGHT_MODE_AUTO ? light_percent : light_last_percent) + " %</output></form>";
  page += "<form action='/save'><div class='grid mt'>"
          "<button name='sb' value='0'>Off</button>"
          "<button name='sb' value='1'>10%</button>"
          "<button name='sb' value='2'>20%</button>"
          "<button name='sb' value='3'>50%</button>"
          "<button name='sb' value='4'>70%</button>"
          "<button name='sb' value='5'>100%</button>"
          "<button class='wide' name='sb' value='6'>Auto (20:00 to 24:00)</button>"
          "</div></form></section>";

  // CLOCK SETTINGS
  page += "<section class='card'><h2>Clock</h2><form action='/save'>"
          "<div class='two'><div class='field'><label for='tz'>Timezone (UTC offset)</label><input id='tz' type='number' name='timezone' min='-12' max='12' value='" + String(timezone) + "' required></div>"
          "<div class='field'><label for='ntp'>NTP server</label><input id='ntp' name='ntp_server_url' value='" + html_escape(ntp_server_url) + "' autocapitalize='none' required></div></div>"
          "<button class='primary'>Save</button></form>"
          "<label class='mt'>Daylight saving (EU summer time)</label>"
          "<form action='/save' class='two'><button name='dls' value='1'>On</button><button name='dls' value='0'>Off</button></form></section>";

  // HOME ASSISTANT
  page += "<section class='card'><h2>Home Assistant</h2>";
  page += status_row("MQTT", html_escape(mqtt_state));
  page += status_row("API", "<a href='/api/status'>/api/status</a>, <a href='/api/light'>/api/light</a>");
  page += "<form class='mt' action='/save' method='POST'>"
          "<div class='two'><div class='field'><label for='mh'>MQTT broker</label><input id='mh' name='mqtt_host' value='" + html_escape(mqtt_host) + "' placeholder='empty = off' autocapitalize='none'></div>"
          "<div class='field'><label for='mp'>Port</label><input id='mp' type='number' name='mqtt_port' min='1' max='65535' value='" + String(mqtt_port) + "'></div></div>"
          "<div class='two'><div class='field'><label for='mu'>User</label><input id='mu' name='mqtt_user' value='" + html_escape(mqtt_user) + "' autocomplete='off' autocapitalize='none'></div>"
          "<div class='field'><label for='mw'>Password</label><input id='mw' type='password' name='mqtt_pass' placeholder='" + String(mqtt_pass.length() ? "unchanged" : "") + "'></div></div>"
          "<div class='field'><label for='mt'>Base topic</label><input id='mt' name='mqtt_topic' value='" + html_escape(mqtt_topic) + "' placeholder='dbclock/" + chip_id + "' autocapitalize='none'></div>"
          "<button class='primary'>Save</button></form>"
          "<form class='mt' action='/save' method='POST'><button name='mqtt_discovery' value='1'" + String(mqtt.connected() ? "" : " disabled") + ">Send discovery</button></form>"
          "<div class='sub mt'>With MQTT discovery the clock appears as device DBClock with a dimmable light and a Sync time button.</div></section>";

  page += network_status();

  // SYSTEM
  page += "<section class='card'><h2>System</h2>";
  page += status_row("Firmware", String(VERSION) + " &middot; " + hardware_label());
  page += status_row("Clock link", mega_link_ok() ? dot(true) + "receiving, clock at " + html_escape(mega_clock_time) : dot(false) + "no data (Mega TX3 to GP1 not connected)");
  page += status_row("Last message", "<span class='msg'>" + html_escape(last_error) + "</span>");
  page += "<div class='two mt'><a class='btn' href='/update'>Update</a>"
          "<form action='/save'><button name='reboot' value='1'>Reboot</button></form></div>"
          "<div class='two mt'><form action='/save' onsubmit=\"return confirm('Delete all settings of the network module?')\"><button class='danger' name='fsformat' value='1'>Delete settings</button></form>"
          "<form action='/save' onsubmit=\"return confirm('Reset the network module and the clock to factory defaults?')\"><button class='danger' name='factreset' value='1'>Factory reset</button></form></div>"
          "</section>";

  page += "<footer>DBClock network module " + String(VERSION) + "</footer>";
  page += FPSTR(PAGE_END);
  server.send(200, "text/html", page);
}

// REST API -----------------------------------------------
// GET /api/light                                  => {"state":"on","brightness":50,"mode":"manual","confirmed":true}
// GET|POST /api/light?state=on|off|toggle&brightness=0-100&mode=off|10|20|50|70|100|auto
// GET /api/status                                 => time, ntp, network, clock link, light, mqtt

String json_escape(String _in) {
  _in.replace("\\", "\\\\");
  _in.replace("\"", "\\\"");
  return _in;
}

String light_json() {
  return "{\"state\":\"" + String(light_is_on() ? "on" : "off") + "\",\"brightness\":" + String(light_percent) +
         ",\"mode\":\"" + light_mode_key() + "\",\"confirmed\":" + String(light_confirmed ? "true" : "false") + "}";
}

void send_json(const int _code, const String& _json) {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(_code, "application/json", _json);
}

bool is_number(const String& _s) {
  if (_s.length() == 0) {
    return false;
  }
  for (unsigned int i = 0; i < _s.length(); i++) {
    if (!isDigit(_s[i])) {
      return false;
    }
  }
  return true;
}

void handleApiLight() {
  if (server.hasArg("mode")) {
    const String m = server.arg("mode");
    int mode = -1;
    if (m == "off") {
      mode = 0;
    } else if (m == "auto") {
      mode = LIGHT_MODE_AUTO;
    } else {
      for (int i = 1; i < LIGHT_MODE_AUTO; i++) {
        if (m == String(LIGHT_MODE_PERCENT[i])) {
          mode = i;
        }
      }
    }
    if (mode < 0) {
      send_json(400, "{\"error\":\"mode must be off, 10, 20, 50, 70, 100 or auto\"}");
      return;
    }
    light_set_mode(mode);
  }
  if (server.hasArg("brightness")) {
    const String v = server.arg("brightness");
    if (!is_number(v) || v.toInt() > 100) {
      send_json(400, "{\"error\":\"brightness must be 0-100\"}");
      return;
    }
    light_set_percent(v.toInt());
  }
  if (server.hasArg("state")) {
    const String v = server.arg("state");
    if (v == "on") {
      light_set_on(true);
    } else if (v == "off") {
      light_set_on(false);
    } else if (v == "toggle") {
      light_set_on(!light_is_on());
    } else {
      send_json(400, "{\"error\":\"state must be on, off or toggle\"}");
      return;
    }
  }
  send_json(200, light_json());
}

void handleApiStatus() {
  String time_json = "\"synced\":false";
  if (ntp_time_set) {
    const struct tm now_tm = get_local_tm(get_local_us());
    time_json = "\"synced\":true,\"time\":\"" + format_time(now_tm) + "\",\"date\":\"" + format_date(now_tm) + "\",\"utc_offset\":" + String(timezone) +
                ",\"ntp_server\":\"" + json_escape(ntp_server_ip) + "\",\"ntp_delay_ms\":" + String(ntp_delay_ms);
  }
  const String json = "{\"version\":\"" + String(VERSION) + "\",\"hostname\":\"" + hostname + "\",\"hardware\":\"" + hardware_label() + "\","
                      "\"connection\":\"" + active_connection() + "\",\"ip\":\"" + get_primary_ip() + "\","
                      "\"time\":{" + time_json + "},"
                      "\"clock_link\":{\"ok\":" + String(mega_link_ok() ? "true" : "false") + ",\"clock_time\":\"" + json_escape(mega_clock_time) + "\"},"
                      "\"light\":" + light_json() + ","
                      "\"mqtt\":\"" + json_escape(mqtt_state) + "\",\"mqtt_topic\":\"" + json_escape(mqtt_base()) + "\"}";
  send_json(200, json);
}



void handleNotFound() {
  // ALSO CATCHES THE CAPTIVE PORTAL CHECKS OF PHONES AND LAPTOPS IN AP MODE
  String target = "/";
  if (config_portal_active) {
    target = "http://" + WiFi.softAPIP().toString() + "/";
  }
  server.sendHeader("Location", target, true);
  server.send(302, "text/plain", "");
}




// LAN IS PREFERRED, THEN WIFI, THEN THE CONFIGURATION ACCESS POINT
String get_primary_ip() {
  if (eth_present && eth.connected()) {
    return eth.localIP().toString();
  }
  if (wifi_connected()) {
    return WiFi.localIP().toString();
  }
  // EMPTY UNTIL THE AP HAS ITS IP, OTHERWISE "(IP unset)" IS SENT TO THE CLOCK
  if (config_portal_active && WiFi.softAPIP().isSet()) {
    return WiFi.softAPIP().toString();
  }
  return "";
}

bool has_network() {
  return (eth_present && eth.connected()) || wifi_connected();
}

void display_ip() {
  const String ip = get_primary_ip();
  if (ip != reported_ip) {
    reported_ip = ip;
    DEBUG_SERIAL.println("IP: " + (ip != "" ? ip : String("none")));
    if (ip != "") {
      send_cmd_str("ip", ip);
    }
  }
}




// RETURNS FALSE IF NO W5100S IS CONNECTED
bool setup_ethernet() {
  pinMode(ETH_PIN_RST, OUTPUT);
  digitalWrite(ETH_PIN_RST, LOW);
  delay(1);
  digitalWrite(ETH_PIN_RST, HIGH);
  delay(100);

  SPI.setRX(ETH_PIN_MISO);
  SPI.setCS(ETH_PIN_CS);
  SPI.setSCK(ETH_PIN_SCK);
  SPI.setTX(ETH_PIN_MOSI);
  eth.setSPISpeed(ETH_SPI_SPEED);
  eth.hostname(hostname.c_str());
  // LAN SHOULD BE USED FOR NTP IF BOTH INTERFACES ARE CONNECTED
  eth.setDefault(true);
  if (!eth.begin()) {
    return false;
  }
  // DHCP KEEPS RUNNING IN THE BACKGROUND, SO A CABLE CAN ALSO BE PLUGGED IN LATER
  const unsigned long start = millis();
  while (!eth.connected() && (millis() - start) < ETH_CONNECT_TIMEOUT) {
    delay(100);
  }
  return true;
}

void wifi_connect() {
  if (wifi_pass.length() > 0) {
    WiFi.beginNoBlock(wifi_ssid.c_str(), wifi_pass.c_str());
  } else {
    WiFi.beginNoBlock(wifi_ssid.c_str());
  }
  wifi_last_reconnect = millis();
}

bool setup_wifi() {
  if (!wifi_present || wifi_ssid.length() == 0) {
    return false;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(hostname.c_str());
  wifi_connect();
  const unsigned long start = millis();
  while (!wifi_connected() && (millis() - start) < WIFI_CONNECT_TIMEOUT) {
    delay(250);
  }
  return wifi_connected();
}

// REPLACES THE WIFIMANAGER OF THE ESP8266 VERSION
void start_config_portal() {
  WiFi.disconnect();
  WiFi.mode(WIFI_AP);
  WiFi.softAP((ap_name + "_" + chip_id).c_str());
  // THE AP GETS ITS IP A BIT LATER, THE DNS SERVER NEEDS IT FOR THE CAPTIVE PORTAL
  const unsigned long start = millis();
  while (!WiFi.softAPIP().isSet() && (millis() - start) < 3000) {
    delay(50);
  }
  dns_server.start(53, "*", WiFi.softAPIP());
  config_portal_active = true;
  config_portal_started = millis();
  last_error = "no network, configuration access point started";
}

void stop_config_portal() {
  dns_server.stop();
  WiFi.softAPdisconnect(true);
  config_portal_active = false;
}



void setup(void) {
  DEBUG_SERIAL.begin(115200);
  // GIVE THE USB SERIAL SOME TIME SO THE BOOT LOG IS NOT LOST
  while (!DEBUG_SERIAL && millis() < 3000) {
    delay(10);
  }
  CLOCK_SERIAL.setTX(CLOCK_SERIAL_TX_PIN);
  CLOCK_SERIAL.setRX(CLOCK_SERIAL_RX_PIN);
  CLOCK_SERIAL.begin(SERIAL_BAUD_RATE);
  // IDLE HIGH IF THE MEGA TX3 IS NOT CONNECTED
  gpio_pull_up(CLOCK_SERIAL_RX_PIN);
  CLOCK_SERIAL.println("");
  pinMode(LED_BUILTIN, OUTPUT);

  chip_id = get_chip_id();
  hostname = MDNS_NAME + chip_id;
  // START THE FILESYSTEM
  if (!LittleFS.begin()) {
    last_error = "__err_littlefs_init_error_try_to_format__";
    DEBUG_SERIAL.println(last_error);
    LittleFS.format();
    if (!LittleFS.begin()) {
      last_error = "__err_littlefs_format failed__";
      DEBUG_SERIAL.println(last_error);
    }
  }
  // LOAD SETTINGS
  restore_eeprom_values();
  // NETWORK: DETECT THE HARDWARE, LAN AND WIFI CAN BE USED AT THE SAME TIME
  wifi_present = rp2040.isPicoW();
  eth_present = setup_ethernet();
  BOARD_INFO += String("_BOARD_") + (wifi_present ? "PICOW" : "PICO") + (eth_present ? "_W5100S" : "");
  DEBUG_SERIAL.println(BOARD_INFO);
  DEBUG_SERIAL.println("WIFI CHIP: " + String(wifi_present ? "found" : "not found"));
  DEBUG_SERIAL.println("W5100S: " + String(eth_present ? "found" : "not found"));
  DEBUG_SERIAL.println("NETWORK MODE: " + network_mode());
  setup_wifi();
  // NO NETWORK AT ALL => OPEN THE CONFIGURATION ACCESS POINT, WITHOUT WIFI CHIP WAIT FOR THE LAN
  if (!has_network()) {
    if (wifi_present) {
      start_config_portal();
    } else {
      last_error = "no network, waiting for a LAN connection";
    }
  }
  DEBUG_SERIAL.println(last_error);
  //DISPLAY IP ON THE CLOCK
  display_ip();
  // SETUP MDNS
  MDNS.begin(hostname.c_str());
  MDNS.addService("http", "tcp", WEBSERVER_PORT);
  //WEBSERVER ROUTES
  server.on("/", handleRoot);
  server.on("/save", handleSave);
  server.on("/index.html", handleRoot);
  server.on("/api/light", handleApiLight);
  server.on("/api/status", handleApiStatus);
  server.onNotFound(handleNotFound);
  // FIRMWARE UPLOAD USING THE BROWSER: http://<ip>/update
  http_updater.setup(&server);
  server.begin();
  //START OTA LIB
  ArduinoOTA.setHostname(hostname.c_str());
  ArduinoOTA.onStart([]() {
    rp2040.wdt_reset();
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    rp2040.wdt_reset();
  });
  ArduinoOTA.begin();
  //SETUP  NTP CLIENT
  ntpUDP.begin(NTP_LOCAL_PORT);
  //SETUP MQTT
  mqtt_net.setTimeout(MQTT_TIMEOUT);
  mqtt.setSocketTimeout(MQTT_TIMEOUT / 1000);
  mqtt.setBufferSize(1024);
  mqtt.setCallback(mqtt_callback);
  // REBOOT IF SOMETHING HANGS
  rp2040.wdt_begin(WATCHDOG_TIMEOUT);
}



uint32_t read_u32(const uint8_t* _b) {
  return ((uint32_t)_b[0] << 24) | ((uint32_t)_b[1] << 16) | ((uint32_t)_b[2] << 8) | _b[3];
}

// NTP TIMESTAMP (s SINCE 1900 + 32 BIT FRACTION) => UNIX us, ERA 1 STARTS IN 2036
uint64_t ntp_to_unix_us(const uint8_t* _b) {
  const uint32_t secs = read_u32(_b);
  const uint32_t frac = read_u32(_b + 4);
  const uint64_t unix_secs = (uint64_t)secs + ((secs & 0x80000000UL) ? 0 : 0x100000000ULL) - NTP_UNIX_OFFSET;
  return unix_secs * 1000000ULL + (((uint64_t)frac * 1000000ULL) >> 32);
}

// SNTP REQUEST WITH MICROSECOND RESOLUTION, NTPClient ONLY PROVIDES FULL SECONDS
bool ntp_request() {
  IPAddress server_ip;
  if (!hostByName(ntp_server_url.c_str(), server_ip)) {
    last_error = "ntp server could not be resolved: " + ntp_server_url;
    return false;
  }
  ntp_server_ip = server_ip.toString();

  uint8_t pkt[48] = { 0 };
  pkt[0] = 0x23;  // LI 0, VERSION 4, MODE 3 = CLIENT
  // DROP OLD ANSWERS
  while (ntpUDP.parsePacket() > 0) {
    ntpUDP.flush();
  }
  const uint64_t t1 = time_us_64();
  ntpUDP.beginPacket(server_ip, 123);
  ntpUDP.write(pkt, sizeof(pkt));
  ntpUDP.endPacket();

  while ((time_us_64() - t1) < (1000ULL * NTP_TIMEOUT)) {
    if (ntpUDP.parsePacket() >= (int)sizeof(pkt)) {
      const uint64_t t4 = time_us_64();
      ntpUDP.read(pkt, sizeof(pkt));
      // MODE 4 = SERVER, STRATUM 0 = KISS OF DEATH
      if ((pkt[0] & 0x07) != 4 || pkt[1] == 0) {
        last_error = "invalid ntp answer from " + ntp_server_ip;
        return false;
      }
      const uint64_t t2 = ntp_to_unix_us(pkt + 32);  // SERVER RECEIVE
      const uint64_t t3 = ntp_to_unix_us(pkt + 40);  // SERVER TRANSMIT
      // ROUND TRIP WITHOUT THE PROCESSING TIME OF THE SERVER, HALF OF IT IS THE WAY BACK
      const int64_t delay_us = (int64_t)(t4 - t1) - (int64_t)(t3 - t2);
      ntp_utc_us = t3 + (delay_us > 0 ? delay_us / 2 : 0);
      ntp_sync_us = t4;
      ntp_delay_ms = delay_us / 1000;
      ntp_time_set = true;
      return true;
    }
    delay(1);
  }
  last_error = "ntp timeout: " + ntp_server_url + " (" + ntp_server_ip + ")";
  return false;
}

// LOCAL STANDARD TIME IN us, THE MEGA ADDS THE DAYLIGHT SAVING HOUR
uint64_t get_local_us() {
  return ntp_utc_us + (time_us_64() - ntp_sync_us) + (int64_t)timezone * 3600LL * 1000000LL;
}

struct tm get_local_tm(const uint64_t _local_us) {
  const time_t t = _local_us / 1000000ULL;
  struct tm tm_local;
  gmtime_r(&t, &tm_local);
  return tm_local;
}

// THE TIME FRAME IS SENT RIGHT BEFORE A FULL SECOND, SO ITS LAST BYTE ARRIVES AT THE MEGA EXACTLY AT THAT SECOND
bool sync_clock_from_ntp() {
  if (!has_network()) {
    last_error = "ntp sync skipped, no network";
    return false;
  }
  if (!ntp_request()) {
    return false;
  }
  CLOCK_SERIAL.flush();
  uint64_t target_us = (get_local_us() / 1000000ULL + 1) * 1000000ULL;
  String frame = build_cmd_frame("st", format_time(get_local_tm(target_us)));
  // 10 BITS PER BYTE (8N1)
  const uint64_t frame_us = (uint64_t)frame.length() * 10ULL * 1000000ULL / SERIAL_BAUD_RATE;
  if (target_us - get_local_us() < frame_us + 2000) {
    target_us += 1000000ULL;
    frame = build_cmd_frame("st", format_time(get_local_tm(target_us)));
  }
  const uint64_t send_at_us = target_us - frame_us;
  while (get_local_us() + 2000 < send_at_us) {
    delay(1);
  }
  while (get_local_us() < send_at_us) {
    tight_loop_contents();
  }
  CLOCK_SERIAL.print(frame);
  CLOCK_SERIAL.flush();
  DEBUG_SERIAL.print(frame);
  send_cmd_str("sd", format_date(get_local_tm(target_us)));
  last_error = "time sent to clock, ntp server " + ntp_server_ip + ", delay " + String(ntp_delay_ms) + " ms";
  return true;
}


bool led_state = false;

void loop(void) {
  rp2040.wdt_reset();
  //HANDLE SERVER
  server.handleClient();
  if (config_portal_active) {
    dns_server.processNextRequest();
    // LAN CABLE GOT PLUGGED IN, THE ACCESS POINT IS NOT NEEDED ANYMORE
    if (eth_present && eth.connected()) {
      stop_config_portal();
      last_error = "LAN connected, configuration access point closed";
    } else if ((millis() - config_portal_started) > (1000UL * CONFIG_PORTAL_TIMEOUT) && WiFi.softAPgetStationNum() == 0) {
      // NOBODY CONFIGURED THE MODULE, TRY AGAIN WITH THE STORED SETTINGS
      rp2040.reboot();
    }
  } else if (wifi_present && wifi_ssid.length() > 0 && !wifi_connected() && (millis() - wifi_last_reconnect) > WIFI_RECONNECT_INTERVAL) {
    wifi_connect();
  }
  // SEND THE IP IF IT CHANGED, E.G. LAN CABLE PLUGGED IN LATER
  display_ip();
  //UPDATE NTP AND SEND IT TO THE CLOCK
  if ((millis() - last) > next_sync_in || ntp_force_update) {
    ntp_force_update = false;
    last = millis();

    led_state = !led_state;
    digitalWrite(LED_BUILTIN, led_state);
    if (sync_clock_from_ntp()) {
      next_sync_in = 1000UL * NTP_SEND_TIME_INTERVAL;
    } else {
      next_sync_in = 1000UL * NTP_RETRY_INTERVAL;
    }
  }
  // LIGHT STATE FROM THE MEGA AND HOME ASSISTANT
  read_mega();
  mqtt_loop();
  //HANDLE OTA
  ArduinoOTA.handle();
  MDNS.update();
  // REBOOT AFTER THE HTTP RESPONSE WAS SENT
  if (reboot_requested && (millis() - reboot_requested) > 2000) {
    rp2040.reboot();
  }
  delay(10);
}
