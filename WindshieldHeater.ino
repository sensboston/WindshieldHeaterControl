#include <WiFiManager.h>
#include <EEPROM.h>
#ifdef ESP8266
#include <ESP8266WiFi.h>
#include <ESP8266Ping.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include "Hash.h"
#else
#include <WiFi.h>
#include <ESP32Ping.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include "mbedtls/md.h"
//#define LED_BUILTIN 22  // uncomment for Lolin32
#define LED_BUILTIN 2  
#endif
#include <ESPNtpClient.h>
#include <Arduino_JSON.h>

WiFiManager wifiManager;

#define RELEY_PIN 15   // GPIO 15, on NodeMCU marked as "D8"
#define NTP_SERVER "time.google.com"
#define TIME_ZONE TZ_America_New_York
const String apiKey = "2305ac279be717ac617b149e72bff1e8";
const String city = "Waltham";
const String countryCode = "US";

#ifdef ESP8266
ESP8266WebServer server(80);
#else
WebServer server(80);
String sha1(String payloadStr)
{
    const char *payload = payloadStr.c_str();
    int size = 20;
    byte shaResult[size];
    mbedtls_md_context_t ctx;
    mbedtls_md_type_t md_type = MBEDTLS_MD_SHA1;
    const size_t payloadLength = strlen(payload);
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 0);
    mbedtls_md_starts(&ctx);
    mbedtls_md_update(&ctx, (const unsigned char *) payload, payloadLength);
    mbedtls_md_finish(&ctx, shaResult);
    mbedtls_md_free(&ctx);
    String hashStr = "";
    for(uint16_t i = 0; i < size; i++) {
        String hex = String(shaResult[i], HEX);
        if(hex.length() < 2) {
            hex = "0" + hex;
        }
        hashStr += hex;
    }
    return hashStr;
}
#endif

#define GO_BACK server.sendHeader("Location", "/",true); server.send(302, "text/plane","");
bool tokenAuthenticated();
#define CHECK_AUTH bool tokenExist=tokenAuthenticated(); if (!tokenExist) { if ((user != "" || password != "") && !server.authenticate(user.c_str(), password.c_str())) return server.requestAuthentication(); }

boolean timeSynchronized = false;
boolean tempSynchronized = false;
boolean isConnected = false; 
short int currTemp = 0;
short int numRuns = 0;
#define HEAT_IS_ON (numRuns & 0x1)

// EEPROM variables
short int tempCondition = 0;
#define TEMP_COND_ADDR 0
byte startHours[2] = {8, 8};
#define START_HOURS_ADDR 2
byte startMinutes[2] = {15, 30};
#define START_MINUTES_ADDR 4
byte weekRange = 0;
#define WEEK_RANGE_ADDR 6
byte numStarts = 2;
#define NUM_STARTS_ADDR 7
byte restDuration = 1;
#define REST_DURATION_ADDR 8
short int prevTemp = 0;
#define PREV_TEMP_ADDR 10
short int heatDuration = 2;
#define HEAT_DURATION_ADDR 12
String user = "";
#define USER_ADDR 14
String password = "";
#define PASS_ADDR USER_ADDR+20

const String sessionIDName = "ESP32ID=";
String getToken() { return sha1(String(user) + ":" + String(password) + ":" + server.client().remoteIP().toString()); }

bool tokenAuthenticated() 
{
    if (server.hasHeader("Cookie")) 
    {
        String cookie = server.header("Cookie");
        if (cookie.indexOf(sessionIDName + getToken()) != -1) return true;
    }
    return false;
}

// EEPROM helpers
void writeIntToEEPROM(int address, int number) 
{ 
    EEPROM.write(address, number >> 8); 
    EEPROM.write(address + 1, number & 0xFF); 
}

int readIntFromEEPROM(int address)
{
  byte byte1 = EEPROM.read(address);
  byte byte2 = EEPROM.read(address + 1);
  return (byte1 << 8) + byte2;
}

// This is HTML page for settings
const char index_html[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html>
<head>
  <title>Remote windshield heater control</title>
  <meta name='viewport' content='width=device-width, initial-scale=1' />
  <link rel='icon' href='http://senssoft.com/wind.ico' type='image/x-icon' />
  <link rel='shortcut icon' href='http://senssoft.com/wind.ico' type='image/x-icon' />  
  <style>
    table {
      border-collapse: separate;
      border-spacing: 1px 1px;
    }
    th, td {
      width: 170px;
      height: 30px;
    }
    input {
      float: align;
      text-align: left;
      width: 95%%;
    }
    select {
      float: align;
      width: 95%%;
    }
    button {
      display: block;
      width: 100%%;
      border: none;
      background-color: #3a853d;
      color: white;
      padding: 10px 22px;
      font-size: 12px;
      cursor: pointer;
      text-align: center;
    }
    button:hover {
      background-color: #4CAF50;
      color: white;
    }
  </style>
  <script>
    var serverTime, upTime;

    function pad(s) {
      return (s < 10 ? '0' : '') + s;
    }

    function formatUptime(seconds) {
      var hours = Math.floor(seconds / (60 * 60));
      var days = Math.floor(hours / 24);
      if (days > 0) hours -= days * 24;
      var minutes = Math.floor(seconds %% (60 * 60) / 60);
      var seconds = Math.floor(seconds %% 60);
      var daystr = ' day' + (days == 1 ? '' : 's') + ' ';
      return (days > 0 ? days + daystr : '') + pad(hours) + ':' + pad(minutes) + ':' + pad(seconds);
    }

    function setTime() {
      upTime = %d;
      document.getElementById('upTime').innerHTML = formatUptime(upTime);
      serverTime = new Date('%s');
      document.addEventListener("visibilitychange", function() { window.location.reload(); });
      startTime();
    }

    function startTime() {
      const monthName = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];
      serverTime = new Date(serverTime.getTime() + 1000);
      upTime++;
      var today = new Date(serverTime);
      document.getElementById('serverTime').innerHTML = monthName[today.getMonth()] + ' ' + today.getDate() + ', ' + today.getFullYear() + '   ' + pad(today.getHours()) + ':' + pad(today.getMinutes()) + ':' + pad(today.getSeconds());
      document.getElementById('upTime').innerHTML = formatUptime(upTime);
      var t = setTimeout(function() { startTime() }, 1000);
    }
  </script>
</head>

<body onload='setTime()'>
  <table>
    <tr style='background-color:#EAFAF1;'>
      <td style='text-align:center'>System uptime:</td>
      <td align='center' id='upTime'>00:00:00</td>
    </tr>
    <tr height='10px'/>
    <tr>
      <td>
        <button onclick="window.location.href='/updateTime'">Server time</button>
      </td>
      <td>
        <button onclick="window.location.href='/updateTemperature'">Temperature</button>
      </td>
    </tr>
    <tr>
      <td id='serverTime' align='center'></td>
      <td align='center'>%i &#176;C</td>
    </tr>
  </table>
  <form action="/settings" method="POST">
    <table>
      <tr>
        <td colspan='2' align='center'><b>Heater start conditions:</b></td>
      </tr>
      <tr>
        <td>Temperature below:</td>
        <td>
          <input type='number' name='tempCondition' id='tempCondition' value='%d'></input>
        </td>
      </tr>
      <tr>
        <td>Time:</td>
        <td>
          <input type='time' name='startTime' id='startTime' value='%02d:%02d:00'></input>
        </td>
      </tr>
      <tr>
        <td>Weekly range:</td>
        <td>
          <select name='weekRange' id='weekRange'>
            <option %s>Workdays</option>
            <option %s>All week</option>
          </select>
        </td>
      </tr>
      <tr>
        <td>How many times:</td>
        <td>
          <input type='number' name='numStarts' id='numStarts' min='0' max='5' value='%d'></input>
        </td>
      </tr>
      <tr>
        <td>Heat duration (min):</td>
        <td>
          <input type='number' name='heatDuration' id='heatDuration' min='1' max='10' value='%d'></input>
        </td>
      </tr>
      <tr>
        <td>Rest duration (min):</td>
        <td>
          <input type='number' name='restDuration' id='restDuration' min='1' max='10' value='%d'></input>
        </td>
      </tr>
      <tr>
        <td colspan='2' align='center'><b>Login credentials:</b></td>
      </tr>
      <tr>
        <td>User:</td>
        <td>
          <input name='user' id='user' style='text-align:left;' value='%s'></input>
        </td>
      </tr>
      <tr>
        <td>Password:</td>
        <td>
          <input type='password' name='password' id='password' style='text-align:left;' value='%s'></input>
        </td>
      </tr>
      <tr height='10px' />
      <tr>
        <td colspan='2'>
          <button type='submit'>Save settings</button>
        </td>
      </tr>
      <tr height='10px' />
    </table>
  </form>
  <table style='width: 346px;'>
    <tr>
      <td>
         <button style='border:none; background-color:#e60000; color:white;' onclick="window.location.href='/resetWiFi'">Reset WiFi</button>
      </td>
      <td>
        <button style='border:none; background-color:#0020d4; color:white;' onclick="window.location.href='/startHeater'">Start heater</button>
      </td>
      <td>
        <button style='border:none; background-color:#0020d4; color:white;' onclick="window.location.href='/stopHeater'">Stop heater</button>
      </td>
    </tr>
  </table>
</body>
</html>)rawliteral";

#define HTML_SIZE sizeof(index_html)+40
char temp[HTML_SIZE];
void handleRoot() 
{
    CHECK_AUTH

    String range_1 = (weekRange == 0) ? "selected=''" : "";
    String range_2 = (weekRange == 1) ? "selected=''" : "";

    snprintf(temp, HTML_SIZE, index_html, 
        millis()/1000, 
        NTP.getTimeDateStringForJS(),
        currTemp, 
        tempCondition, 
        startHours[0], startMinutes[0],
        range_1.c_str(), range_2.c_str(),
        numStarts,
        heatDuration,
        restDuration,
        user.c_str(),
        password.c_str());

    // Set persistent cookie
    if (!tokenExist) server.sendHeader("Set-Cookie", sessionIDName + getToken() + ";Expires=Mon, 30 Dec 2030 23:59:59 GMT");
    server.send(200, "text/html", temp);
}

bool checkConnection() { isConnected = ((WiFi.status() == WL_CONNECTED) && Ping.ping("api.openweathermap.org")); return isConnected; }

void processSyncEvent (NTPEvent_t ntpEvent) 
{
    switch (ntpEvent.event) 
    {
        // Real time sync should happened every hour
        case timeSyncd:
            timeSynchronized = true;
            tempSynchronized = false;
        case partlySync:
        case syncNotNeeded:
        case accuracyError:
            Serial.printf ("[NTP-event] %s\n", NTP.ntpEvent2str (ntpEvent));
            break;
        default:
            break;
    }
}

void getCurrentTemperature() 
{
    tempSynchronized = false;
    Serial.println("getCurrentTemperature()");
    
    HTTPClient http;
    String url = "http://api.openweathermap.org/data/2.5/weather?q=" + city + "," + countryCode + "&APPID=" + apiKey +"&mode=json&units=metric&cnt=1";
    http.begin(url);
  
    // Send HTTP POST request
    int httpResponseCode = http.GET();
    String payload = "{}"; 
  
    if (httpResponseCode > 0) 
    {
        JSONVar weatherObject = JSON.parse(http.getString());
        if (JSON.typeof(weatherObject) != "undefined") 
        {
            currTemp = (int)round((double)weatherObject["main"]["temp"]);
            Serial.print("Current temperature: "); Serial.println(currTemp);
            writeIntToEEPROM(PREV_TEMP_ADDR, currTemp);
            EEPROM.commit();
            tempSynchronized = true;
        }
    }
    else 
    {
        Serial.print("[HTTP] error code: ");
        Serial.println(httpResponseCode);
    }
    
    // Free resources
    http.end();
}

void readSettings()
{
    byte b = EEPROM.read(TEMP_COND_ADDR);
    // Do we have values stored?
    if (b != 255)
    {
        tempCondition = readIntFromEEPROM(TEMP_COND_ADDR);
        startHours[0] = EEPROM.read(START_HOURS_ADDR);
        startHours[1] = EEPROM.read(START_HOURS_ADDR+1);
        startMinutes[0] = EEPROM.read(START_MINUTES_ADDR);
        startMinutes[1] = EEPROM.read(START_MINUTES_ADDR+1);
        weekRange = EEPROM.read(WEEK_RANGE_ADDR);
        numStarts = EEPROM.read(NUM_STARTS_ADDR);
        heatDuration = EEPROM.read(HEAT_DURATION_ADDR);
        restDuration = EEPROM.read(REST_DURATION_ADDR);
        currTemp = prevTemp = readIntFromEEPROM(PREV_TEMP_ADDR);
        user = password = "";
        for(int i=0;i<20;i++) { b = EEPROM.read(USER_ADDR+i); if (b>0) user += char(b); else break; }
        for(int i=0;i<20;i++) { b = EEPROM.read(PASS_ADDR+i); if (b>0) password += char(b); else break; }
    }
}

void writeSettings()
{
    writeIntToEEPROM(TEMP_COND_ADDR, tempCondition);
    EEPROM.write(START_HOURS_ADDR, startHours[0]);
    EEPROM.write(START_HOURS_ADDR+1, startHours[1]);
    EEPROM.write(START_MINUTES_ADDR, startMinutes[0]);
    EEPROM.write(START_MINUTES_ADDR+1, startMinutes[1]);
    EEPROM.write(WEEK_RANGE_ADDR, weekRange);
    EEPROM.write(NUM_STARTS_ADDR, numStarts);
    EEPROM.write(HEAT_DURATION_ADDR, heatDuration);
    EEPROM.write(REST_DURATION_ADDR, restDuration);
    for(int i=0;i<20;i++) EEPROM.write(USER_ADDR+i, 0);
    for(int i=0;i<user.length();i++) EEPROM.write(USER_ADDR+i, user[i]);
    for(int i=0;i<20;i++) EEPROM.write(PASS_ADDR+i, 0);
    for(int i=0;i<password.length();i++) EEPROM.write(PASS_ADDR+i, password[i]);
    // store data to EEPROM
    EEPROM.commit();
}

void saveSettings()
{
    if (server.method() == HTTP_POST)
    {
        Serial.println("saveSettings()");
        String value = "";
        if (server.hasArg("tempCondition")) { value = server.arg("tempCondition"); tempCondition = value.toInt(); }
        if (server.hasArg("numStarts")) { value = server.arg("numStarts"); numStarts = value.toInt(); } 
        if (server.hasArg("heatDuration")) { value = server.arg("heatDuration"); heatDuration = value.toInt(); } 
        if (server.hasArg("restDuration")) { value = server.arg("restDuration"); restDuration = value.toInt(); } 
        if (server.hasArg("user")) { user = server.arg("user"); } 
        if (server.hasArg("password")) { password = server.arg("password"); } 
        if (server.hasArg("weekRange")) { weekRange = server.arg("weekRange") == "Workdays" ? 0 : 1; }
        if (server.hasArg("startTime"))
        {
            struct tm startTime;
            memset(&startTime, 0, sizeof(tm));
            value = "1970-01-01 " + server.arg("startTime");
            strptime(value.c_str(), "%Y-%m-%d %H:%M:%S", &startTime);
            startHours[0] = startTime.tm_hour;
            startMinutes[0] = startTime.tm_min;
            
            // Add end of work time
            time_t t = mktime(&startTime);
            t += (numStarts*heatDuration + (numStarts-1)*restDuration) *60;
            startTime = *localtime(&t);
            startHours[1] = startTime.tm_hour;
            startMinutes[1] = startTime.tm_min;
        }
        writeSettings();
    }

    GO_BACK
}

void setup() 
{
    pinMode(RELEY_PIN, OUTPUT);
    digitalWrite(RELEY_PIN, LOW);
    
    Serial.begin(115200);
    
    pinMode(LED_BUILTIN, OUTPUT);
    // turn off built-in LED (inverse logic)
    digitalWrite(LED_BUILTIN, HIGH);

    // Initialize EEPROM and read settings
    EEPROM.begin(64);
    readSettings();

    // Connect to WiFi & Internet
    wifiManager.setHostname("Heater");
    wifiManager.autoConnect("Heater");

    // Turn built-in LED on if we're connected
    digitalWrite(LED_BUILTIN, checkConnection() ? LOW : HIGH);

    // setup NTP client
    NTP.setTimeZone (TIME_ZONE);
    // Sync time and temperature once per hour (in seconds)
    NTP.setInterval (3600);  
    // NTP timeout in msec           
    NTP.setNTPTimeout (1000);
    NTP.onNTPSyncEvent (processSyncEvent);
    NTP.begin(NTP_SERVER);

    // Setup web server
    server.on("/", handleRoot);
    server.on("/favicon.ico", []() { server.sendHeader("Location", "http://senssoft.com/car.ico",true); server.send(302, "text/plane",""); });
    server.on("/startHeater", []() { CHECK_AUTH; digitalWrite(RELEY_PIN, HIGH); GO_BACK });
    server.on("/stopHeater", []() { CHECK_AUTH; digitalWrite(RELEY_PIN, LOW); GO_BACK });
    server.on("/updateTime", []() { CHECK_AUTH; NTP.getTime(); GO_BACK });
    server.on("/updateTemperature", []() { CHECK_AUTH; timeSynchronized = true; tempSynchronized = false; GO_BACK });
    server.on("/resetWiFi", []() { CHECK_AUTH; wifiManager.resetSettings(); ESP.restart(); });
    server.on("/settings", HTTP_POST, saveSettings);    
    server.onNotFound( [](){ GO_BACK });
    server.begin();
    Serial.println("System started");
}

unsigned long prevMillis = 0, prevBlinkMillis = 0, runTimeMillis = 0;
bool blinkState = false;
void loop() 
{
    // Check windshield heater start conditions every minute
    if(timeSynchronized && millis()-prevMillis > 60 * 1000) 
    {
        prevMillis = millis();
        Serial.println (NTP.getTimeDateStringUs ());
        // Check internet connection
        checkConnection();
        // Turn built-in LED on if we're connected
        digitalWrite(LED_BUILTIN, isConnected ? LOW : HIGH);

        // Should we check start conditions?
        if (numStarts > 0)
        {
            // Check day of week first
            time_t t = time(NULL);
            struct tm now = *localtime(&t);
            if ((weekRange == 0 && (now.tm_wday > 0 && now.tm_wday < 6)) || weekRange == 1)
            {
                // Is it work time?
                if ( (now.tm_hour >= startHours[0] && now.tm_min >= startMinutes[0]) &&
                     (now.tm_hour <= startHours[1] && now.tm_min <= startMinutes[1]) )
                {
                    if (isConnected && numRuns == 0) getCurrentTemperature();
                    if (currTemp < tempCondition)
                    {
                        // Start hitting
                        if (numRuns == 0)
                        {
                            numRuns++;
                            digitalWrite(RELEY_PIN, HIGH);
                            runTimeMillis = millis();
                        }
                        else 
                        {
                            if (HEAT_IS_ON)
                            {
                                if (millis()-runTimeMillis > heatDuration * 60 * 1000)
                                { 
                                    numRuns++;
                                    digitalWrite(RELEY_PIN, LOW);
                                    runTimeMillis = millis();
                                }
                            }
                            else
                            {
                                if (millis()-runTimeMillis > restDuration * 60 * 1000)
                                { 
                                    numRuns++;
                                    digitalWrite(RELEY_PIN, HIGH);
                                    runTimeMillis = millis();
                                }                                
                            }
                        }

                    }
                }
                else
                {
                    // Turn off the heat
                    if (HEAT_IS_ON)
                    {
                        digitalWrite(RELEY_PIN, LOW);
                        numRuns = 0;
                    }
                }
            }
        }
    }

    // Blink if we're out of internet access
    if (!isConnected && millis()-prevBlinkMillis > 200)
    {
        prevBlinkMillis = millis();
        blinkState ^= true; // invert boolean
        digitalWrite(LED_BUILTIN, blinkState ? LOW : HIGH);        
    }

    // Should we sync current temperature?
    if (timeSynchronized && !tempSynchronized) getCurrentTemperature();

    server.handleClient();
}
