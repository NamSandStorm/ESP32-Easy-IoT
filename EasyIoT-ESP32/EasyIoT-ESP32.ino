/* [General & Notes]: ------------------------------------------------------------------------------------- */ 
  /*   [License]: ----------------------------------------------------------------------------------------- */
    /*
      MIT License

      Copyright (c) 2021-2023 FlexTech Engineering CC & Kai Eysselein

      Permission is hereby granted, free of charge, to any person obtaining a copy
      of this software and associated documentation files (the "Software"), to deal
      in the Software without restriction, including without limitation the rights
      to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
      copies of the Software, and to permit persons to whom the Software is
      furnished to do so, subject to the following conditions:

      The above copyright notice and this permission notice shall be included in
      all copies or substantial portions of the Software.

      THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
      IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
      FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
      AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
      LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
      OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
      THE SOFTWARE.
    */
  /*   [CAUTION !!!]: ------------------------------------------------------------------------------------- */
    /*
      Always change the usernames and passwords to avoid the related risks !!!
      This code is generic and the default settings will expose the device to various risks !!!    
    */
  /*   [Notes]: ------------------------------------------------------------------------------------------- */
    /*
      1. before compiling ensure the following are installed
        1.1 WiFiManager   			  		  https://github.com/tzapu/WiFiManager
        1.2 MQTT						            https://github.com/knolleary/pubsubclient
        1.3 ArduinoJSON                 https://github.com/bblanchon/ArduinoJson    
        1.4 Timer Interrupts			      https://github.com/khoih-prog/ESP32TimerInterrupt
        1.5 NTP time                    https://github.com/arduino-libraries/NTPClient
        1.6 Async web server			      https://github.com/me-no-dev/ESPAsyncWebServer
        1.7 Required for WiFi Manager	  https://github.com/me-no-dev/AsyncTCP
        
      2. MQTT and Console Commands (first login via the command below)
        2.1 Console Topic on which the system is listening:
          /console
          2.2.1 Public Commands:
            hello
          2.3.2 Admin Commands: 
            2.3.2.1 User Login:
              user login [user] [password] 
              -> default admin: is admin admin
              -> default user: is user user
            2.3.2.2 User Logout
              user logout      
            2.3.2.3 Restart 
              restart
            2.3.2.4 Get IP address
              ip
            2.3.2.5 Get Firmware version
              version
            2.3.2.6 Flash Firmware
              firware update [path to firmware binary]
            2.3.2.7 Firmware Rollback
              firmware rollback           
            2.3.3.8 preferences
              NOTE: - serialdiag and/or mqttdiag has to be enabled to see feedback.
                    - sensitive / potentionally compromising information may be sent via MQTT and posted on a topic for others to see
                Read
                  preferences read startcounter
                  preferences read serialdiagnostics
                  preferences read mqttdiagnostics
                  preferences read mqtthost
                  preferences read mqttport
                  preferences read mqttuser
                  preferences read mqttpass
                  preferences read mqttrootpath
                  preferences read adminuser
                  preferences read adminpassword
                  preferences read all
            2.3.3.9 Enable Diagnostic Messages via MQTT
              mqttdiag
              mqttdiag set
              mqttdiag set persistent
            2.3.3.10 Clear WiFi Settings
              wifisettings cear
            2.3.3.11 Number of starts of device
              starts
    */
/* [Includes / Libraries]: -------------------------------------------------------------------------------- */
  /* [Operating System Level]: ---------------------------------------------------------------------------- */
    #include "Arduino.h"              
    #include "WiFiManager.h"          
    #include <PubSubClient.h>         
    #include <HTTPClient.h>           
    #include <Update.h>               
    #include <ArduinoJson.h>          
    #include <WiFiUdp.h>              
    #include <NTPClient.h>            
  /* [ESP32 Specific]: -------------------------------------------------------------------------------------- */
    #include <ESP.h>                  
    #include "esp_system.h"           
    #include "esp32-hal.h"
    #include "ESP32TimerInterrupt.h"  
    #include "SPIFFS.h"               
    #include <FS.h>                   
    #include <Preferences.h>
      Preferences preferences;      
/* [Generic Functions]: ----------------------------------------------------------------------------------- */
  /* [GF: remove text from a string]: --------------------------------------------------------------------- */
    String removeStringFromString(String sourceText, String removeString) {
      String cleanedText = "";
      // loop through the characters in the source text
      for (int i = 0; i < sourceText.length(); i++) {
        // if the current substring doesn't match the remove string, add it to the cleaned text
        if (sourceText.substring(i, i + removeString.length()) != removeString) {
          cleanedText += sourceText.charAt(i);
          // skip over the characters that were part of the remove string
          i += removeString.length() - 1;
        }
      }
      return cleanedText;
    }
  /* [GF: Convert IP Address to String] ------------------------------------------------------------------- */
    String ip2String(const IPAddress& ipAddress){
      return String(ipAddress[0]) + String(".") +\
      String(ipAddress[1]) + String(".") +\
      String(ipAddress[2]) + String(".") +\
      String(ipAddress[3])  ; 
    }
/* [Network]: --------------------------------------------------------------------------------------------- */
  //required during first phase of Setup
  /* [Network: get ESP32 Efuse mac as string]: ------------------------------------------------------------ */
    String getESP32UID() {
      uint64_t chipid = ESP.getEfuseMac();
      char uid[17];
      snprintf(uid, sizeof(uid), "%04X%08X", (uint16_t)(chipid>>32), (uint32_t)chipid);
      return String(uid);
    }
/* [Variables]: ------------------------------------------------------------------------------------------- */
  /* [V: Preferences, data that can be changed and stored]: ----------------------------------------------- */
    long totalStarts;
    bool messagingDiagSerial; //Diagnostic notifications
    bool messagingDiagMQTT; //Diagnostic notifications
    String adminUserName                        = "admin";
    String adminPassword                    = "adminpass";    
      
  /* [V: MQTT related]: ----------------------------------------------------------------------------------- */
    String MQTThost                         = "mozzie.iot.com.na";
    int MQTTport                            = 1883;
    String MQTTrootPath                     = "EasyIoT/";
    String MQTTuserName                     = "EasyIoT";
    String MQTTpassword                     = "EasyIoT";
  /* [V: Device / Server etc. Unique]: -------------------------------------------------------------------- */
    #define UIDproductName                   "EasyIoT-"    
    #define firmwareVersion                  "00.00.00.01"    
    #define joinWiFiSSID                     "Join your EasyIoT device to WiFi"
    #define joinWiFiPassword                 "password"
    #define timeZoneOffset                  0
  /* [V: Authentication and Security]: -------------------------------------------------------------------- */
    bool adminActive                       = false;
  /* [V: Environment and other]: -------------------------------------------------------------------------- */
    bool internetConnected                  = false;
    String UID                              = UIDproductName +getESP32UID();   //only works with ESP32
    int WiFiManagerConfigPortalTimeout      = 120;    //how long the connect portal should remain live
    String firmwareRollBackUrl              = "";
  /* [V: Detail Settings]: -------------------------------------------------------------------------------- */
    int bootDelay                           = 2;     //delay until loop can execute, avoids crazy initial readings
    float timer0Interval                    = 1.2;    //in seconds: may need to increase, depending on loop load
    float timer1Interval                    = 60;     //every in seconds:MQTT status update
  /* [V: Pin Assignments]: -------------------------------------------------------------------------------- */
    #define heartBeatLEDPin                   32  //Specific to M5Stack Stamp Pico
    #define resetButtonPin                    39  //Specific to M5Stack Stamp Pico, note that it default high 
  /* [V: MQTT]: --------------------------------------------------------------------------------------------*/
    String MQTTconsoleTopic                 = MQTTrootPath+UID+"/console";
    String MQTTwillTopic                    = MQTTrootPath+UID+"/online"; 
    int MQTTwillQoSstate                    = 0;
    bool MQTTwillRetain                     = true;  
    String MQTTwillMessage                  = "false";  

/* [Serial Monitor]: -------------------------------------------------------------------------------------- */
    void serialMonitorSetup(){
      Serial.begin(115200);
      delay(100);
    }
    void serialMonitorLoop(){
      String data="";
      data=Serial.readString();
      if(data!=""){
        data.remove(data.length()-1,1);
        consoleEvaluate(data);
      }
    }

/* [Spiffs]: ---------------------------------------------------------------------------------------------- */  
    void spiffsSetup(){
      if(!SPIFFS.begin(true)){
        Serial.println("An Error has occurred while mounting SPIFFS");
      return;
      }      
    }
/* [Communication Functions]: ----------------------------------------------------------------------------- */
  /* [COMSF: WifiManager]: -------------------------------------------------------------------------------- */
    WiFiManager wm;                   
    bool wmNonblocking = false;
    WiFiManagerParameter custom_field;
    void WiFiManagerSetup(){
      Serial.println("WiFi config: starting...");
      WiFi.mode(WIFI_STA); 
      Serial.setDebugOutput(true);  
      if(wmNonblocking) wm.setConfigPortalBlocking(false);
      std::vector<const char *> menu = {"wifi","sep","exit"};
      wm.setMenu(menu);
      wm.setClass("invert");
      wm.setConfigPortalTimeout(WiFiManagerConfigPortalTimeout); // auto close configportal after n seconds
      bool res;
      res = wm.autoConnect(joinWiFiSSID,joinWiFiPassword); // password protected ap
      if(!res){
        Serial.println("Failed to connect or hit timeout");
        internetConnected=false;
      } 
      else {
        Serial.println("WiFi connected...");
        internetConnected=true;
      }
      Serial.println("WiFi config: concluded");
    }
    void WifiManagerLoop(){
      if(wmNonblocking){
        wm.process(); // avoid delays() in loop when non-blocking and other long running code  
      }
    }
    void WiFiManagerClear(){
      wm.resetSettings();
    }    
  /* [COMSF: MQTT]: --------------------------------------------------------------------------------------- */
    WiFiClient MQTTclient;
    PubSubClient MQTT(MQTTclient);
    bool MQTTconnected=false;
    void MQTTreconnect() {
      while (!MQTT.connected()) {
        if (MQTT.connect(UID.c_str(),MQTTuserName.c_str(),MQTTpassword.c_str(), MQTTwillTopic.c_str(), MQTTwillQoSstate, MQTTwillRetain, MQTTwillMessage.c_str())) {
          MQTT.subscribe(MQTTconsoleTopic.c_str());
        } 
        else {
        }
      }
    }
    void MQTTcallback(char* topic, byte* message, unsigned int length) {
      String messageTemp;
      for (int i = 0; i < length; i++) {
        messageTemp += (char)message[i];
      }
      if (String(topic) == MQTTconsoleTopic.c_str()) {
        MQTTevaluate(messageTemp);
      }
    }
    void MQTTmessage(String channel="", String message=""){
      MQTT.publish(channel.c_str(), message.c_str());
    }
    void MQTTevaluate(String message){
      Serial.println("received mqtt message");
      Serial.print("sending message to be evaluated: ");
      Serial.println(message);
      if( message!=""){
        consoleEvaluate(message);
      }
    }
    void MQTTdisconnect(){
      MQTT.disconnect();
    }
    void MQTTsetup(){
      Serial.println("MQTT config: starting...");
      char Server[MQTThost.length() + 1];
      MQTThost.toCharArray(Server, sizeof(Server));       
      MQTT.setServer(Server, MQTTport);
      MQTT.setCallback(MQTTcallback);
      MQTTloop();
      MQTTmessage(MQTTwillTopic, "true");
      Serial.println("MQTT config: complete");
    }
    void MQTTloop(){
      if (!MQTT.connected()) {
        MQTTreconnect();
      }
      MQTT.loop();
      if(MQTTconnected==false){
        MQTTconnected=true;
        Serial.println("MQTT config: concluded");
      }
      else{}
    }    
  /* [COMSF: Messaging]: ---------------------------------------------------------------------------------- */
    void message (String Message="", String MessagePath="", bool SysMessage=false){
      if(SysMessage){
        if (messagingDiagSerial){
          Serial.println (MessagePath+"/"+Message);
        }
        if (messagingDiagMQTT){
          String MQTTPathSend=MQTTrootPath+UID+MessagePath;
          MQTTmessage(MQTTPathSend, Message);
        }
      }
      else{
        String MQTTPathSend=MQTTrootPath+UID+MessagePath;
        MQTTmessage(MQTTPathSend, Message);      
        Serial.println (Message);
      }
    }
    void messagingLoop(){
      MQTTloop();
    }
/* [User Functions]: -------------------------------------------------------------------------------------- */
  /* [U: User Login]: ------------------------------------------------------------------------------------- */
    void userLogin(String user, String password){
      message("checking credentials","/console/",true); 
      if (user == adminUserName && password == adminPassword){
          adminActive = true;
          message("login successful","/console/");  
      }
      else{
        message("login failed","/console/");   
      }
    }
  /* [U: User Logout]: ------------------------------------------------------------------------------------ */
    void UserLogout(){
      adminActive=false;
    }
/* [Sensors]: --------------------------------------------------------------------------------------------- */
  /* [S: Reset Button]: ----------------------------------------------------------------------------------- */
    int resetButtonCounter = 0;
    void resetButtonSetup(){
      pinMode(resetButtonPin, INPUT);
    }
    void resetButtonLoop(){
      if (digitalRead(resetButtonPin)==LOW){
        resetButtonCounter++;                
      }
      else{
        resetButtonCounter=0;
      }
      if (resetButtonCounter>=3){
        message("to default settings, including WiFi","/status/reset",false);
        WiFiManagerClear();
        delay(200);
        preferences.begin("preferences", false);
        preferences.clear();
        preferences.end();
        delay(200);
        restartDevice();
      }
    }
    void sensorLoop(){
      resetButtonLoop();
    }
/* [Actuators]: ------------------------------------------------------------------------------------------- */
  /* [A: Heart Beat LED]: --------------------------------------------------------------------------------- */
    void heartBeatSetup(){
      pinMode(heartBeatLEDPin, OUTPUT);
    }
    void heartBeatLoop(){
      message ("triggering ...","/diagnostics/heartbeat",true);
      bool LEDstate =(digitalRead(heartBeatLEDPin));
      digitalWrite(heartBeatLEDPin,!LEDstate);      
    }
    void actuatorLoop(){
      heartBeatLoop();      
    }
    
/* [Timers]: ---------------------------------------------------------------------------------------------- */
  /* [T: Interrupt Timers]: ------------------------------------------------------------------------------- */
    #define _TimerINTERRUPT_LOGLEVEL_ 4 // To be included only in main(), .ino with setup() to avoid `Multiple Definitions` Linker Error you can't use Serial.print/println in ISR or crash.
    ESP32Timer timer0(0);
    ESP32Timer timer1(1);
    volatile bool timer0Triggered=false;
    volatile bool timer1Triggered=false;
    bool IRAM_ATTR timer0ISR(void * TimerNo){ 
      timer0Triggered=true;
      return true;
    }
    bool IRAM_ATTR timer1ISR(void * TimerNo){ 
      timer1Triggered=true;
      return true;
    } 
    void timerInterruptSetup(){
      if (timer0.attachInterruptInterval(timer0Interval * 1000000, timer0ISR)){
        message ("complete", "/diagnostics/setup/timerinterrupt/timer0", true);
      }
      else{
        message ("failed", "/diagnostics/setup/timerinterrupt/timer0", true);
      }
      if (timer1.attachInterruptInterval(timer1Interval * 1000000, timer1ISR)){
        message ("complete", "/diagnostics/setup/timerinterrupt/timer1", true);
      }
      else{
        message ("failed", "/diagnostics/setup/timerinterrupt/timer1", true);
      }  
    }
    void timerInterruptLoop(){
      if(timer0Triggered){
        message ("triggered", "/diagnostics/timerinterrupt/timer0", true);
        timer0Triggered=false;
        timer0Payload();
      }
      if(timer1Triggered){
        message ("triggered", "/diagnostics/timerinterrupt/timer1", true);
        timer1Triggered=false;
        timer1Payload();
      }
    }
    void timer0Payload(){
      message ("started ...", "/diagnostics/timerinterrupt/timer0/payload", true);
      actuatorLoop();
      sensorLoop();          
      serialMonitorLoop();
      messagingLoop();
      WifiManagerLoop();
      JSONeventsUpdate();
      message ("complete", "/diagnostics/timerinterrupt/timer0/payload", true);
    }
    void timer1Payload(){ 
      message ("started ...", "/diagnostics/timerinterrupt/timer1/payload", true);
      JSONstatusUpdate();         
      MQTTmessage(MQTTwillTopic, "true");
      message ("complete", "/diagnostics/timerinterrupt/timer1/payload", true);
    }
/* [Firmware Update] : ------------------------------------------------------------------------------------ */
  HTTPClient client;      
  int firmwareTotalLength;       //total size of firmware
  int firmwareCurrentLength = 0; //current size of written firmware
  void firmwareUpdate(uint8_t *data, size_t len){
    Update.write(data, len);
    firmwareCurrentLength += len;
    Serial.print('.');
    if(firmwareCurrentLength != firmwareTotalLength) return;
    Update.end(true);
  }
  void firmwareFileUpdate(String firmwareHost){
    message("attempting update: "+firmwareHost,"/firmware",true);  
    client.begin(firmwareHost);
    int resp = client.GET();
    if(resp == 200){
      firmwareTotalLength = client.getSize();
      int len = firmwareTotalLength;
      Update.begin(UPDATE_SIZE_UNKNOWN);
      uint8_t buff[128] = { 0 };
      WiFiClient * stream = client.getStreamPtr();
      while(client.connected() && (len > 0 || len == -1)) {
        size_t size = stream->available();
        if(size) {
          int c = stream->readBytes(buff, ((size > sizeof(buff)) ? sizeof(buff) : size));
          firmwareUpdate(buff, c);
            if(len > 0) {
              len -= c;
            }
          }
          delay(1);
        }
        message("update successful, resarting... ","/firmware");  
      }
      else{
        message("update not successful, error: "+String(resp),"/firmware");  
      }
      client.end();
    }
/* [Device Restart] : ------------------------------------------------------------------------------------- */
  void restartDevice(){
    message("in 5","/status/restart",false);digitalWrite(heartBeatLEDPin,LOW);
    delay(1000);
    message("in 4","/status/restart",false);
    delay(1000);
    message("in 3","/status/restart",false);
    delay(1000);
    message("in 2","/status/restart",false);
    delay(1000);
    message("in 1","/status/restart",false);
    delay(1000);
    message("forced restart initiated","/status/restart",false);digitalWrite(heartBeatLEDPin,HIGH);
    MQTTdisconnect();
    ESP.restart();        
  }
/* [NTP Time] --------------------------------------------------------------------------------------------- */
  WiFiUDP ntpUDP;
  NTPClient timeClient(ntpUDP, "pool.ntp.org", timeZoneOffset * 60);
  void NTPsetup(){
    timeClient.begin(); // initialize the time client
  }
/* [ArduinoJson] ------------------------------------------------------------------------------------------ */
    const size_t statusCapacity = JSON_OBJECT_SIZE(10);
    const size_t eventsCapacity = JSON_OBJECT_SIZE(2);
    DynamicJsonDocument JSONdocStatus(statusCapacity);
    DynamicJsonDocument JSONdocEvents(eventsCapacity);
    void JSONstatusUpdate(){
      //#1 source
        JSONdocStatus["src"]= UID;
      //#2 power cycles
        JSONdocStatus["pwrcyc"] =totalStarts;
      //#3 get time since startup
        JSONdocStatus["millis"] = millis();
      //#4 get timestamp
        timeClient.update();
        JSONdocStatus["ts"] = timeClient.getEpochTime();
      //#5 get free memory
        JSONdocStatus["free_heap"] = ESP.getFreeHeap();
      //#6 get total memory
        JSONdocStatus["total_heap"] = ESP.getHeapSize();
      //#7 get CPU temperature      
        JSONdocStatus["MCU_temp"] = temperatureRead();
      //convert to JSON
        String JSONstringStatus;
        serializeJson(JSONdocStatus, JSONstringStatus);
      //Send message
        message (JSONstringStatus, "/status/system");
    }
    void JSONeventsUpdate(){
      //#1 get time since startup
        JSONdocEvents["millis"] = millis();
      //#2 get timestamp
        timeClient.update();
        JSONdocEvents["ts"] = timeClient.getEpochTime();
      //convert to JSON
        String JSONstringEvents;
        serializeJson(JSONdocEvents, JSONstringEvents);
      //Send message
        message (JSONstringEvents, "/events/reading");      
    }
    void JSONsetup(){
      JSONstatusUpdate();
      JSONeventsUpdate();
    }    
/* [Preferences]: ----------------------------------------------------------------------------------------- */
    void preferencesRead(String instruction="none"){
      preferences.begin("preferences", false);
      if (instruction=="startcounter"||instruction=="all"){
        //Start Counter
        totalStarts = preferences.getLong("totalStarts", 0);
        message ("success", "/console",false);        
        message (String(totalStarts), "/preferences/startcounter",true);      
      }
      if (instruction=="mqttdiagnostics"||instruction=="all"){
        //MQTTDiagnostics
        messagingDiagMQTT = preferences.getBool("mqttdiag",false);
        message (String(messagingDiagMQTT), "/preferences/mqttdiagnostics",false);
      }      
      if (instruction=="serialdiagnostics"||instruction=="all"){
        //SerialDiagnostics     
        messagingDiagSerial = preferences.getBool("serialdiag",false);
        message (String(messagingDiagSerial), "/preferences/serialdiagnostics",false);  
      }
      if (instruction=="mqtthost"||instruction=="all"){
        //MQTT host
        MQTThost=preferences.getString("MQTThost",MQTThost);
        message (MQTThost, "/preferences/mqtthost",false);
      }
      if (instruction=="mqttport"||instruction=="all"){
        //MQTT port
        MQTTport=preferences.getInt("MQTTport",MQTTport);
        message (String(MQTTport), "/preferences/mqttport",false);
      }
      if (instruction=="mqttusername"||instruction=="all"){
        //MQTT user
        MQTTuserName=preferences.getString(",mqttusername",MQTTuserName);
        message (MQTTuserName, "/preferences/mqttusername",false);
      }
      if (instruction=="mqttpass"||instruction=="all"){
        //MQTT password
        MQTTpassword=preferences.getString("MQTTpassword",MQTTpassword);
        message ("******", "/preferences/mqttpass",false);        
      }
      if (instruction=="mqttrootpath"||instruction=="all"){
        //MQTT root path
        MQTTrootPath=preferences.getString("mqttrootpath",MQTTrootPath);    
        message (MQTTrootPath, "/preferences/mqttrootpath",false);        
      }
      if (instruction=="adminusername"||instruction=="all"){
        //admin user name
        adminUserName = preferences.getString("adminusername",adminUserName);
        message ("success", "/console",false);        
        message (adminUserName, "/preferences/adminusername",true);      
      }
      if (instruction=="adminpassword"||instruction=="all"){
        //admin password
        adminPassword = preferences.getString("adminpassword",adminPassword);
        message ("success", "/console",false);        
        message (adminPassword, "/preferences/adminpassword",true);
      }
      preferences.end();    
    }     
    void preferencesWrite(String instruction="none"){
      preferences.begin("preferences", false);
      if (instruction=="startcounter"||instruction=="all"){
        //Start Counter
        preferences.putLong("totalStarts",totalStarts);   
        message ("success", "/console",false);   
        message ("written", "/preferences/startcounter",true);
      }
      if (instruction=="mqttdiagnostics"||instruction=="all"){
        //MQTTDiagnostics
        preferences.putBool("mqtt",messagingDiagMQTT);
        message ("success", "/console",false);        
        message ("written", "/preferences/mqttdiagnostics",false);
      }
      if (instruction=="serialdiagnostics"||instruction=="all"){
        //SerialDiagnostics    
        preferences.putBool("serialdiag",messagingDiagSerial);
        message ("success", "/console",false);        
        message ("written", "/preferences/serialdiagnostics",false);        
      }
      if (instruction=="mqtthost"||instruction=="all"){
        //MQTT host
        preferences.putString("MQTThost",MQTThost);
        message ("written", "/preferences/mqtthost",false);
      }
      if (instruction=="mqttport"||instruction=="all"){
        //MQTT port
        preferences.putInt("MQTTport",MQTTport);
        message ("written", "/preferences/mqttport",false);
      }
      if (instruction=="mqttusername"||instruction=="all"){
        //MQTT user
        preferences.putString("mqttusername",MQTTuserName);
        message ("written", "/preferences/mqttusername",false);
      }
      if (instruction=="mqttpass"||instruction=="all"){
        //MQTT password
        preferences.putString("MQTTpassword",MQTTpassword);
        message ("written", "/preferences/mqttpass",false);
      }
      if (instruction=="mqttrootpath"||instruction=="all"){
        //MQTT root path
        preferences.putString("mqttrootpath",MQTTrootPath);
        message ("written", "/preferences/mqttrootpath",false);
      }
      if (instruction=="adminusername"||instruction=="all"){
        //admin user name
        preferences.putString("adminusername",adminUserName);
        message ("success", "/console",false);
        message ("written", "/preferences/adminusername",true);
      }
      if (instruction=="adminpassword"||instruction=="all"){
        //admin password
        preferences.putString("adminpassword",adminPassword);
        message ("success", "/console",false);
        message ("written", "/preferences/adminpassword",true);
      }
      preferences.end();
    }
    void preferencesClear(String instruction="none"){
      preferences.begin("preferences", false);
      if (instruction=="startcounter"||instruction=="all"){
        //Start Counter
        preferences.remove("totalStarts");   
        message ("success", "/console",false);  
        message ("cleared", "/preferences/startcounter",true);
      }
      if (instruction=="mqttdiagnostics"||instruction=="all"){
        //MQTTDiagnostics
        preferences.remove("mqttdiag");
        message ("success", "/console",false);
        message ("cleared", "/preferences/mqttdiagnostics",false);
      }
      if (instruction=="serialdiagnostics"||instruction=="all"){
        //SerialDiagnostics
        preferences.remove("serialdiag");
        message ("success", "/console",false);        
        message ("cleared", "/preferences/serialdiagnostics",false);        
      }
      if (instruction=="mqtthost"||instruction=="all"){
        //MQTT host
        preferences.remove("MQTThost");
        message ("cleared", "/preferences/mqtthost",false);
      }
      if (instruction=="mqttport"||instruction=="all"){
        //MQTT port
        preferences.remove("MQTTport");
        message ("cleared", "/preferences/mqttport",false);
      }
      if (instruction=="mqttuser"||instruction=="all"){
        //MQTT user
        preferences.remove("mqttusername");
        message ("cleared", "/preferences/mqttusername",false);
      }
      if (instruction=="mqttpass"||instruction=="all"){
        //MQTT password
        preferences.remove("MQTTpassword");
        message ("cleared", "/preferences/mqttpass",false);
      }
      if (instruction=="mqttrootpath"||instruction=="all"){
        //MQTT root path
        preferences.remove("mqttrootpath");
        message ("cleared", "/preferences/mqttrootpath",false);
      }
      if (instruction=="adminusername"||instruction=="all"){
        //admin user name
        preferences.remove("adminusername");
        message ("success", "/console",false);
        message ("cleared", "/preferences/adminusername",true);
      }
      if (instruction=="adminpassword"||instruction=="all"){
        //admin password
        preferences.remove("adminpassword");
        message ("success", "/console",false);    
        message ("cleared", "/preferences/adminpassword",true);
      }
      preferences.end();
    }
    void preferencesSet(String instruction="none", String value=""){
      preferences.begin("preferences", false);
      if (instruction=="startcounter"){
        //Start Counter
        totalStarts = atol(value.c_str());
        message ("success", "/console",false);
        message (String(totalStarts), "/preferences/startcounter",true);
      }
      if (instruction=="mqttdiagnostics"){
        //MQTTDiagnostics
        if (value=="1"||value=="true"){
          messagingDiagMQTT=true;
        }
        else{
          messagingDiagMQTT=false;
        }
        message ("success", "/console",false);        
        message (String(messagingDiagMQTT), "/preferences/mqttdiagnostics",true);
      }
      if (instruction=="serialdiagnostics"){
        //SerialDiagnostics
        if (value=="1"||value=="true"){
          messagingDiagSerial=true;
        }
        else{
          messagingDiagSerial=false;
        }
        message ("success", "/console",false);        
        message (String(messagingDiagSerial), "/preferences/serialdiagnostics",true);
      }
      if (instruction=="mqtthost"){
        //MQTT host
        MQTThost=value;
        message (MQTThost, "/preferences/mqtthost",false);
      }
      if (instruction=="mqttport"){
        //MQTT port
        MQTTport=atol(value.c_str());
        message (String(MQTTport), "/preferences/mqttport",false);
      }
      if (instruction=="mqttuser"){
        //MQTT user
        MQTTuserName=value;
        message (MQTTuserName, "/preferences/mqttusername",false);
      }
      if (instruction=="mqttpass"){
        //MQTT password
        MQTTpassword=value;
        message (MQTTpassword, "/preferences/mqttpass",false);
      }
      if (instruction=="mqttrootpath"){
        //MQTT root path
        preferences.remove("mqttrootpath");
        message ("cleared", "/preferences/mqttrootpath",false);
      }
      if (instruction=="adminusername"){
        //admin user name
        adminUserName=value;
        message ("success", "/console",false);        
        message ("set", "/preferences/adminusername",true);
      }
      if (instruction=="adminpassword"){
        //admin password
        adminPassword=value;
        message ("success", "/console",false);
        message ("set", "/preferences/adminpassword",true);
      }
      preferences.end();
    }
    void preferencesSetup(){
      preferences.begin("preferences", false);
      //Start Counter
        totalStarts = preferences.getLong("totalStarts", 0);
        ++totalStarts;
        preferences.putLong("totalStarts",totalStarts);
        preferencesRead("all");
      preferences.end();
      }    
/* [Console Functions]: ----------------------------------------------------------------------------------- */      
  /* [C: Console Evaluate]: ------------------------------------------------------------------------------- */     
    String consoleCommand[6];
    void splitCommand(String text,String separator=" "){
      //break up the instruction into max 6 parameters
      text.replace("\n","");
      int length=text.length();
      int consoleCommands=0;
      consoleCommand[0]="";
      String test="";
      for (int i = 0; i < length; i++) {
        test=text.substring(i,i+1);
        if (test==separator){
          consoleCommands++;
          consoleCommand[consoleCommands]="";
        }
        else {
          consoleCommand[consoleCommands]=consoleCommand[consoleCommands]+test;
        }
      }
    }
    void consoleEvaluate(String receivedMessage){
      splitCommand(receivedMessage);
      if(receivedMessage!="message received ..."){
        //clear old commands from console, important for public facing MQTT        
        message("message received ...","/console");
      }
      if(!adminActive){      
        //no user logged in
        if (consoleCommand[0]=="hello"){
          message("world","/console/"); 
        }
        if (consoleCommand[0]=="user"){
          if (consoleCommand[1]=="login"){
            userLogin(consoleCommand[2], consoleCommand[3]);
          }
        }
      }
      else {      
        //admin user logged in
        if (consoleCommand[0]=="user"){
          if (consoleCommand[1]=="logout"){
            UserLogout();
            message("logged out","/console/");
          }  
        }
        if (consoleCommand[0]=="millis"){
          //millis
          message(String(millis(),DEC),"/console/"); 
        }        
        if (consoleCommand[0]=="ip"){
          //get ip
          message(ip2String(WiFi.localIP()),"/console/"); 
        }
        if (consoleCommand[0]=="starts"){
          //number of starts of device
          message(String(totalStarts),"/console/");  
        }
        if (consoleCommand[0]=="restart"){
          //restart
          message("restarting","/console/"); 
          restartDevice();
        }
        if (consoleCommand[0]=="firmware"){
          //firmware
          if (consoleCommand[1]== "version"){
            message(firmwareVersion,"/console/"); 
          }
          if (consoleCommand[1]== "update"){
            message("Updating Firmware to: "+consoleCommand[2],"/console/"); 
            firmwareFileUpdate(consoleCommand[2]);
            message("Firmware update complete, restarting ..."+consoleCommand[2],"/console/"); 
            delay(1000);
            restartDevice();
          }
          if (consoleCommand[1]== "rollback"){
            message("Restoring firmware to previous version ...","/console/firmware"); 
            firmwareFileUpdate(firmwareRollBackUrl);
            message("Firmware update complete, restarting ..."+consoleCommand[2],"/console/");
            delay(1000);
            restartDevice();
          }
        }
        if (consoleCommand[0]=="preferences"){
          if (consoleCommand[1]=="read"){
            preferencesRead(consoleCommand[2]);
          }
          if (consoleCommand[1]=="write"){
            preferencesWrite(consoleCommand[2]);
          }
          if (consoleCommand[1]=="clear"){
            preferencesClear(consoleCommand[2]);
          } 
          if (consoleCommand[1]=="set"){
            preferencesSet(consoleCommand[2],consoleCommand[3]);
          }
        }
      }
    }   
/* [Default Arduino Functions]: --------------------------------------------------------------------------- */
  /* [DAF: Arduino Setup] : ------------------------------------------------------------------------------- */
    void setup(){
      serialMonitorSetup(); Serial.println("startup commencing ....");
      Serial.println("preferencesSetup ...");preferencesSetup(); Serial.println("... preferencesSetup complete");
      Serial.println("running WiFiManagerSetup ...");WiFiManagerSetup();Serial.println("... WiFiManagerSetup complete");
      Serial.println("running MQTTsetup ...");MQTTsetup();Serial.println("... MQTTsetup complete");
      Serial.println("running spiffsSetup ...");spiffsSetup();Serial.println("... spiffsSetup complete");
      Serial.println("running timerInterruptSetup ...");timerInterruptSetup();Serial.println("... timerInterruptSetup complete");
      Serial.println("running NTPsetup ...");NTPsetup();Serial.println("... NTPsetup complete");
      Serial.println("running heartBeatSetup ...");heartBeatSetup();Serial.println("... heartBeatSetup complete");
      Serial.println("running resetButtonSetup ...");resetButtonSetup();Serial.println("... resetButtonSetup complete");
      Serial.println("running JSONsetup ...");JSONsetup();Serial.println("... JSONsetup complete");
      Serial.println("running planned bootup delay to stabilise the system ...");while (bootDelay*1000>millis()){}Serial.println("... bootup delay complete");
      Serial.println("... startup complete");
    }
  /* [DAF: Arduino Loop] : -------------------------------------------------------------------------------- */
    void loop(){
      timerInterruptLoop();
    }