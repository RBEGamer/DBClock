#define VERSION "4.1.0"
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
#include <NTPClient.h>
#include <time.h>
#include <pico/unique_id.h>

// NETWORK MODE IS DETECTED AT BOOT:
// PICO W + ETHERNET HAT  => LAN (+ WIFI IF CONFIGURED)
// PICO W                 => WIFI ONLY
// PICO + ETHERNET HAT    => LAN ONLY
// THE PICO W BUILD ALSO RUNS ON A PICO WITHOUT WIFI, THE CORE DETECTS THE CYW43 CHIP AT BOOT


// CONFIG -------------------------------------
#define WEBSERVER_PORT 80                                 // set the port for the webserver eg 80 8080
#define MDNS_NAME "dbclock"                               // set hostname
#define WEBSITE_TITLE "DB Network Module Configuration"   // name your device
#define SERIAL_BAUD_RATE 9600
#define NTP_SEND_TIME_INTERVAL 60*60                      // sende zeit an uhr alle x sekunden
#define NTP_RETRY_INTERVAL 30                             // retry after x seconds if the ntp sync failed
#define DEFAULT_NTP_SERVER "pool.ntp.org"
#define DEFAULT_TIMEZONE 1
#define ETH_CONNECT_TIMEOUT 10000                         // ms to wait for a dhcp lease on the lan port
#define WIFI_CONNECT_TIMEOUT 20000                        // ms to wait for the wifi connection
#define WIFI_RECONNECT_INTERVAL 30000                     // ms between reconnect attempts if the wifi got lost
#define CONFIG_PORTAL_TIMEOUT 300                         // s, reboot and retry if no network got configured
#define WATCHDOG_TIMEOUT 8000                             // ms, max 8300

// SERIAL TO THE ARDUINO MEGA (Serial3), PICO IS 3.3V ONLY!
#define CLOCK_SERIAL Serial1
#define CLOCK_SERIAL_TX_PIN 0  // GP0 -> MEGA RX3
#define CLOCK_SERIAL_RX_PIN 1  // GP1 <- MEGA TX3, ONLY WITH LEVEL SHIFTER, NOT USED
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




String time_last = "not synced";
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
//VARS

int timezone = 0;
String ntp_server_url = DEFAULT_NTP_SERVER;
String wifi_ssid = "";
String wifi_pass = "";
String last_error = "";
String chip_id = "";
String hostname = "";
int rtc_hours = 0;
int rtc_mins = 0;
int rtc_secs = 0;
int rtc_day = 1;
int rtc_month = 1;
int rtc_year = 2020;

// NETWORK STATE
bool wifi_present = false;
bool eth_present = false;
bool config_portal_active = false;
unsigned long config_portal_started = 0;
unsigned long wifi_last_reconnect = 0;
String reported_ip = "";


// INSTANCES --------------------------------------------
WebServer server(WEBSERVER_PORT);
HTTPUpdateServer http_updater;
DNSServer dns_server;
Wiznet5100lwIP eth(ETH_PIN_CS, SPI, ETH_PIN_INT);

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, DEFAULT_NTP_SERVER, 0, 60000);






const String phead_1 = "<!DOCTYPE html><html><head><title>";
const String phead_2 = "</title>"
                       "<meta http-equiv='content-type' content='text/html; charset=utf-8'>"
                       "<meta charset='utf-8'>"
                       "<meta name='viewport' content='width=device-width, initial-scale=1'>"
                       "<style>"
                       "html, body {"
                       "  background: #F2F2F2;"
                       " width: 100%;"
                       " height: 100%;"
                       " margin: 0px;"
                       " padding: 0px;"
                       " font-family: 'Verdana';"
                       " font-size: 16px;"
                       " color: #404040;"
                       " }"
                       "img {"
                       " border: 0px;"
                       "}"
                       "span.title {"
                       " display: block;"
                       " color: #000000;"
                       " font-size: 30px;"
                       "}"
                       "span.subtitle {"
                       " display: block;"
                       " color: #000000;"
                       " font-size: 20px;"
                       "}"
                       ".sidebar {"
                       " background: #FFFFFF;"
                       " width: 250px;"
                       " min-height: 100%;"
                       " height: 100%;"
                       " height: auto;"
                       " position: fixed;"
                       " top: 0px;"
                       " left: 0px;"
                       " border-right: 1px solid #D8D8D8;"
                       "}"
                       ".logo {"
                       " padding: 25px;"
                       " text-align: center;"
                       " border-bottom: 1px solid #D8D8D8;"
                       "}"
                       ".menu {"
                       " padding: 25px 0px 25px 0px;"
                       " border-bottom: 1px solid #D8D8D8;"
                       "}"
                       ".menu a {"
                       " padding: 15px 25px 15px 25px;"
                       " display: block;"
                       " color: #000000;"
                       " text-decoration: none;"
                       " transition: all 0.25s;"
                       "}"
                       ".menu a:hover {"
                       " background: #0088CC;"
                       " color: #FFFFFF;"
                       "}"
                       ".right {"
                       " margin-left: 250px;"
                       " padding: 50px;"
                       "}"
                       ".content {"
                       " background: #FFFFFF;"
                       " padding: 25px;"
                       " border-radius: 5px;"
                       " border: 1px solid #D8D8D8;"
                       "}"
                       "@media (max-width: 700px) {"
                       " .sidebar { position: static; width: 100%; min-height: 0; border-right: 0; }"
                       " .right { margin-left: 0px; padding: 16px; }"
                       "}"
                       "</style>";

const String pstart = "</head>"
                      "<body>"
                      "<div class='sidebar'>"
                      "<div class='logo'>"
                      "<span class='title'>DBClock - NETWORK MODULE</span>"
                      "<span class='subtitle'>- Configuration -</span>"
                      "</div>"
                      "<div class='menu'>"
                      "<a href='index.html'>Settings</a>"
                      "<a href='update'>Firmware Update</a>"
                      "</div>"
                      "</div>"
                      "<div class='right'>"
                      "<div class='content'>";

const String pend = "</div></div></body></html>";





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

void send_cmd_str(const String _command, const String _payload) {
  const String cmd = "%" + _command + "_" + _payload + "_" + crc16(_command + _payload);
  CLOCK_SERIAL.println(cmd);
  DEBUG_SERIAL.println(cmd);
}

void set_clock_time(const int _h, const int _m, const int _s) {
  send_cmd_str("st", String(_h) + ":" + String(_m) + ":" + String(_s));
}

void set_clock_date(const int _day, const int _month, const int _year) {
  send_cmd_str("sd", String(_day) + "." + String(_month) + "." + String(_year));
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

// NTPCLIENT ONLY STORES THE POINTER, SO SET IT AGAIN AFTER EACH CHANGE OF ntp_server_url
void apply_ntp_settings() {
  timeClient.setPoolServerName(ntp_server_url.c_str());
  timeClient.setTimeOffset(timezone * 3600);
}

void restore_eeprom_values() {
  ntp_server_url = read_file(file_ntp_server, DEFAULT_NTP_SERVER);
  timezone = read_file(file_timezone, String(DEFAULT_TIMEZONE)).toInt();
  wifi_ssid = read_file(file_wifi_ssid, "");
  wifi_pass = read_file(file_wifi_pass, "");
  apply_ntp_settings();
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
    } else if (server.argName(i) == "reboot") {
      last_error = "rebooting";
      reboot_requested = millis();
    } else if (server.argName(i) == "plain") {
      // RAW POST BODY, NOTHING TO DO
    } else {
      send_cmd_str(server.argName(i), server.arg(i));  //SEND ALL OTHER COMMANDS TO THE CLOCK DIRECTLY
    }
  }
  //SAVE THESE DATA
  apply_ntp_settings();
  if (save) {
    save_values_to_eeprom();
  }

  server.send(200, "text/html", "<html><head><meta http-equiv='refresh' content='" + String(reboot_requested ? 10 : 1) + "; url=/' /></head><body>SAVE SETTINGS PLEASE WAIT</body></html>");
}



// ALL WIFI CALLS MUST BE GUARDED, THEY HANG ON A PICO WITHOUT WIFI CHIP
// IN AP MODE WiFi.status() ALSO REPORTS WL_CONNECTED, SO THE STATION IP IS CHECKED AS WELL
bool wifi_connected() {
  return wifi_present && !config_portal_active && WiFi.status() == WL_CONNECTED && WiFi.localIP().isSet();
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

String network_status() {
  String s = "<hr><h2>NETWORK</h2><p><b>MODE:</b> " + network_mode() + "<br>";
  // LAN
  s += "<b>LAN (W5100S):</b> ";
  if (!eth_present) {
    s += "not detected";
  } else if (eth.connected()) {
    s += "connected, " + eth.localIP().toString();
  } else {
    // THE W5100S DRIVER CAN NOT DETECT THE LINK STATE
    s += "no cable or waiting for DHCP";
  }
  s += "<br>";
  // WIFI
  s += "<b>WIFI:</b> ";
  if (!wifi_present) {
    s += "not available (board without WiFi)";
  } else if (config_portal_active) {
    s += "configuration access point " + html_escape(ap_name + "_" + chip_id) + ", " + WiFi.softAPIP().toString();
  } else if (wifi_ssid.length() == 0) {
    s += "not configured";
  } else if (wifi_connected()) {
    s += "connected to " + html_escape(wifi_ssid) + ", " + WiFi.localIP().toString() + ", " + String(WiFi.RSSI()) + " dBm";
  } else {
    s += "connecting to " + html_escape(wifi_ssid);
  }
  s += "<br><b>HOSTNAME:</b> " + hostname + ".local</p>";
  return s;
}

void handleRoot() {
  String control_forms = "<hr><h2>DEVICE INFO</h2><h3>" + hostname + "<br><br>" + BOARD_INFO + "</h3><br>";

  control_forms += "<hr><h2>CURRENT NTP TIME</h2><h1>" + String(rtc_hours) + ":" + String(rtc_mins) + ":" + String(rtc_secs) + " " + String(rtc_day) + "." + String(rtc_month) + "." + String(rtc_year) + "</h1>";
  control_forms += "<p>last sync: " + time_last + "</p>";

  control_forms += network_status();

  // WIFI CREDENTIALS, ONLY ON BOARDS WITH WIFI CHIP
  if (wifi_present) {
    control_forms += "<br><h3> WIFI SETTINGS </h3>"
                     "<form name='wifi' action='/save' method='POST'>"
                     "<input type='text' value='" + html_escape(wifi_ssid) + "' name='wifi_ssid' placeholder='SSID'/><br>"
                     "<input type='password' value='' name='wifi_pass' placeholder='"
                     + String(wifi_pass.length() ? "(unchanged)" : "PASSWORD") + "'/><br>"
                     "<input type='submit' value='SAVE WIFI AND REBOOT'/>"
                     "</form><br>"
                     "<form name='wifi_forget' action='/save' method='POST'>"
                     "<input type='hidden' value='1' name='wifi_forget' />"
                     "<input type='submit' value='DELETE WIFI (LAN ONLY)'/>"
                     "</form><br>";
  }

  control_forms += "<br><h3> CLOCK CONTROLS </h3>"
                   "<form name='btn_off' action='/save' method='GET'>"
                   "<input type='number' value='" + String(timezone) + "' name='timezone' min='-12' max='12' required placeholder='1'/>"
                   "<input type='submit' value='SET TIMEZONE'/>"
                   "</form><br>"
                   "<form name='btn_off' action='/save' method='GET'>"
                   "<input type='text' value='" + html_escape(ntp_server_url) + "' name='ntp_server_url' required placeholder='pool.ntp.org'/>"
                   "<input type='submit' value='SET NTP SERVER URL'/>"
                   "</form><br>"
                   "<form name='btn_on' action='/save' method='GET' >"
                   "<input type='hidden' value='sendtime' name='sendtime' />"
                   "<input type='submit' value='SEND NTP TIME TO CLOCK'/>"
                   "</form><br>";
  // DAYLIGHTSAVING
  control_forms += "<p>DAYLIGHTSAVING: </p><form name='btn_on' action='/save' method='GET'><select name='dls' id='dls'><option value='0'>DISABLED</option><option value='1'>ENABLED</option></select><input type='submit' value='SAVE'/></form><br>";
  //BRIGHTNESS
  control_forms += "<p>BRIGHTNESS-MODE<br> </p><br>";
  control_forms += "<form name='btn_off' action='/save' method='GET'><input type='hidden' name='sb' id='sb' value='0'/><input type='submit' value='OFF'/></form><br>";
  control_forms += "<form name='btn_off' action='/save' method='GET'><input type='hidden' name='sb' id='sb' value='1'/><input type='submit' value='10%'/></form><br>";
  control_forms += "<form name='btn_off' action='/save' method='GET'><input type='hidden' name='sb' id='sb' value='2'/><input type='submit' value='20%'/></form><br>";
  control_forms += "<form name='btn_off' action='/save' method='GET'><input type='hidden' name='sb' id='sb' value='3'/><input type='submit' value='50%'/></form><br>";
  control_forms += "<form name='btn_off' action='/save' method='GET'><input type='hidden' name='sb' id='sb' value='4'/><input type='submit' value='70%'/></form><br>";
  control_forms += "<form name='btn_off' action='/save' method='GET'><input type='hidden' name='sb' id='sb' value='5'/><input type='submit' value='100%'/></form><br>";
  control_forms += "<form name='btn_off' action='/save' method='GET'><input type='hidden' name='sb' id='sb' value='6'/><input type='submit' value='AUTO'/></form><br>";

  // SYSTEM SETTINGS
  control_forms += "<form name='btn_on' action='/save' method='GET' required ><input type='hidden' value='eepromread' name='eepromread' /><input type='submit' value='READ STORED CONFIG'/></form><br>";
  control_forms += "<form name='btn_on' action='/save' method='GET' required ><input type='hidden' value='fsformat' name='fsformat' /><input type='submit' value='DELETE CONFIGURATION'/></form><br>";
  control_forms += "<form name='btn_on' action='/save' method='GET' required ><input type='hidden' value='factreset' name='factreset' /><input type='submit' value='FACTORY RESET'/></form><br>";
  control_forms += "<form name='btn_on' action='/save' method='GET' required ><input type='hidden' value='1' name='reboot' /><input type='submit' value='REBOOT NETWORK MODULE'/></form><br>";


  control_forms += "<br><hr><h3>LAST SYSTEM MESSAGE</h3><br>" + html_escape(last_error);

  server.send(200, "text/html", phead_1 + WEBSITE_TITLE + phead_2 + pstart + control_forms + pend);
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
  apply_ntp_settings();
  timeClient.begin();
  // REBOOT IF SOMETHING HANGS
  rp2040.wdt_begin(WATCHDOG_TIMEOUT);
}



bool sync_clock_from_ntp() {
  if (!has_network()) {
    last_error = "ntp sync skipped, no network";
    return false;
  }
  if (!timeClient.forceUpdate() && !timeClient.isTimeSet()) {
    last_error = "ntp sync failed: " + ntp_server_url;
    return false;
  }
  time_last = timeClient.getFormattedTime();
  rtc_hours = timeClient.getHours();
  rtc_mins = timeClient.getMinutes();
  rtc_secs = timeClient.getSeconds();
  // SEND TIME
  set_clock_time(rtc_hours, rtc_mins, rtc_secs);
  // DATE
  time_t epochTime = timeClient.getEpochTime();
  struct tm* ptm = gmtime((time_t*)&epochTime);

  rtc_day = ptm->tm_mday;
  rtc_month = ptm->tm_mon + 1;
  rtc_year = ptm->tm_year + 1900;
  set_clock_date(rtc_day, rtc_month, rtc_year);
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
  //HANDLE OTA
  ArduinoOTA.handle();
  MDNS.update();
  // REBOOT AFTER THE HTTP RESPONSE WAS SENT
  if (reboot_requested && (millis() - reboot_requested) > 2000) {
    rp2040.reboot();
  }
  delay(10);
}
