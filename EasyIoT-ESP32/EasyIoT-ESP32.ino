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
                  preferences read mqttpath
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
  /*   [Changelog]: --------------------------------------------------------------------------------------- */
    /*
    [00.00.00.01]
      2023.03.07
        - moved code from internal private repository to public
        - changed private passwords to default passwords
        - sanitised code
        - added MIT license
        - included WiFi Settings clear command in console
        - changed login command to user [username] [password]
        - changed logout command to user logout
        - added factory reset option in console
      2023.03.08
        - added preferences functionality
        - broke a lot
      2023.03.09
        - added factory reset functionality
    */
  /*   [To Do]: ------------------------------------------------------------------------------------------- */
      /*
      1. firmware update process
      2. remote admin functionality
      3. nice to have: change variables to  PEP 8, while not being the standard for C++, this is easier to read as is the standard for Python
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
  /* [GF: convert a MAC Address in string format to an integer]: ------------------------------------------ */
    String M2ulli (String MacString){
      unsigned long long int value = 0;
      int digits=12;
      char Buf[15];
      for (int i = 0; i <= 17; i++) {
        if (MacString.substring(i, i+1)!=":"){
          digits = digits-1;
          if (digits>-1){
            String TestString=MacString.substring(i, i+1);
            TestString.toCharArray(Buf, 3);
            value =value+pow(16,digits)*x2i(Buf);
          }
        }
      }
      String ReturnString=ltoa(value/1000000,Buf,10);
      ReturnString+=ltoa((value-((value/1000000)*1000000)),Buf,10);
      return (ReturnString);
    }
  /* [GF: convert a hex to integer]: ---------------------------------------------------------------------- */  
    int x2i(char *s){
      int x = 0;
      for(;;) {
        char c = *s;
        if (c >= '0' && c <= '9') {
          x *= 16;
          x += c - '0'; 
        }
        else if (c >= 'A' && c <= 'F') {
          x *= 16;
          x += (c - 'A') + 10; 
        }
        else break;
        s++;
      }
      return x;
    }
  /* [GF: mac address to string]: ------------------------------------------------------------------------- */  
    String mac2String(byte ar[]) {
      String s;
      for (byte i = 0; i < 6; ++i){
        char buf[3];
        sprintf(buf, "%02X", ar[i]); // J-M-L: slight modification, added the 0 in the format for padding 
        s += buf;
        if (i < 5) s += ':';
      }
      return s;
    }
  /* [GF: remove text from a string]: --------------------------------------------------------------------- */
    String RemoveStringFromString(String SourceText, String RemoveString) {
      String CleanedText = "";
      // loop through the characters in the source text
      for (int i = 0; i < SourceText.length(); i++) {
        // if the current substring doesn't match the remove string, add it to the cleaned text
        if (SourceText.substring(i, i + RemoveString.length()) != RemoveString) {
          CleanedText += SourceText.charAt(i);
          // skip over the characters that were part of the remove string
          i += RemoveString.length() - 1;
        }
      }
      return CleanedText;
    }
  /* [GF: Convert IP Address to String] ------------------------------------------------------------------- */
    String IpAddress2String(const IPAddress& ipAddress){
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
  /* [Network: get mac address as string]: ---------------------------------------------------------------- */
    String getWifiMac(){
      String temp=RemoveStringFromString(WiFi.macAddress(),":");
      return temp;  
    }

/* [Variables]: ------------------------------------------------------------------------------------------- */
  /* [V: Preferences, data that can be changed and stored]: ----------------------------------------------- */
    long TotalStarts;
    bool SerialDiagnosticMessagesEnabled; //Diagnostic notifications
    bool MQTTDiagnosticMessagesEnabled; //Diagnostic notifications
    String MQTThost                         = "mozzie.iot.com.na";
    int MQTTport                            = 1883;
    String MQTTControllerRootPath           = "EasyIoT/";
    String MQTTusername                     = "EasyIoT";
    String MQTTpassword                     = "EasyIoT";
    String AdminUserName                    = "admin";
    String AdminPassword                    = "adminpass";
  /* [V: Device / Server etc. Unique]: -------------------------------------------------------------------- */
    #define ProductName                      "EasyIoT"   
    #define UIDProductName                   "EasyIoT-"    
    #define FirmwareVersion                  "00.00.00.01"    
    #define JoinWiFiSSID                     "Join your EasyIoT device to WiFi"
    #define JoinWiFiPassword                 "password"
    #define timezone_offset                  0
  /* [V: Authentication and Security]: -------------------------------------------------------------------- */
    String UserType[2]                     = {"user","admin"};
    bool AdminUserLoggedIn                 = false;
  /* [V: Environment and other]: -------------------------------------------------------------------------- */
    bool InternetConnected                  = false;
    String UID                              = UIDProductName +getESP32UID();   //only works with ESP32
    int wm_ConfigPortalTimeout              = 120;    //how long the connect portal should remain live
    String FirmwareRollBackUrl              = "";
  /* [V: Detail Settings]: -------------------------------------------------------------------------------- */
    int BootDelay                           = 2;     //delay until loop can execute, avoids crazy initial readings
    float Timer0Interval                    = 1.2;    //in seconds: may need to increase, depending on loop load
    float Timer1Interval                    = 60;     //every in seconds:MQTT status update
  /* [V: Pin Assignments]: -------------------------------------------------------------------------------- */
    #define HeartBeatLEDPin                   32  //Specific to M5Stack Stamp Pico
    #define ResetButtonPin                    39  //Specific to M5Stack Stamp Pico, note that it default high 
  /* [V: MQTT]: --------------------------------------------------------------------------------------------*/
    String MQTTConsole                      = MQTTControllerRootPath+UID+"/console";
    String MQTTwillTopic                    = MQTTControllerRootPath+UID+"/online"; 
    int MQTTwillQoS                         = 0;
    bool MQTTwillRetain                     = true;  
    String MQTTwillMessage                  = "false";  

/* [Serial Monitor]: -------------------------------------------------------------------------------------- */
    void SerialMonitorSetup(){
      Serial.begin(115200);
      delay(100);
    }
    void SerialMonitorLoop(){
      String Data="";
      Data=Serial.readString();
      if(Data!=""){
        Data.remove(Data.length()-1,1);
        ConsoleEvaluate(Data);
      }
    }

/* [Spiffs]: ---------------------------------------------------------------------------------------------- */  
    void SpiffsSetup(){
      if(!SPIFFS.begin(true)){
        Serial.println("An Error has occurred while mounting SPIFFS");
      return;
      }      
    }
    void SpiffsLoop(){
    }  
/* [Communication Functions]: ----------------------------------------------------------------------------- */
  /* [COMSF: WifiManager]: -------------------------------------------------------------------------------- */
    WiFiManager wm;                   
    bool wm_nonblocking = false;
    WiFiManagerParameter custom_field;
    void WiFiManagerSetup(){
      Serial.println("WiFi config: starting...");
      WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP  
      Serial.setDebugOutput(true);  
      if(wm_nonblocking) wm.setConfigPortalBlocking(false);
      std::vector<const char *> menu = {"wifi","sep","exit"};
      wm.setMenu(menu);
      wm.setClass("invert");
      wm.setConfigPortalTimeout(wm_ConfigPortalTimeout); // auto close configportal after n seconds
      bool res;
      res = wm.autoConnect(JoinWiFiSSID,JoinWiFiPassword); // password protected ap
      if(!res){
        Serial.println("Failed to connect or hit timeout");
        InternetConnected=false;
      } 
      else {
        Serial.println("WiFi connected...");
        InternetConnected=true;
      }
      Serial.println("WiFi config: concluded");
    }
    void WifiManagerLoop(){
      if(wm_nonblocking){
        wm.process(); // avoid delays() in loop when non-blocking and other long running code  
      }
    }
    void WiFiManagerClear(){
      wm.resetSettings();
    }    
  /* [COMSF: MQTT]: --------------------------------------------------------------------------------------- */
    WiFiClient MQTTClient;
    PubSubClient MQTT(MQTTClient);
    bool MQTTconnected=false;
    void MQTTreconnect() {
      while (!MQTT.connected()) {
        if (MQTT.connect(UID.c_str(),MQTTusername.c_str(),MQTTpassword.c_str(), MQTTwillTopic.c_str(), MQTTwillQoS, MQTTwillRetain, MQTTwillMessage.c_str())) {
          MQTT.subscribe(MQTTConsole.c_str());
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
      if (String(topic) == MQTTConsole.c_str()) {
        MQTTevaluate(messageTemp);
      }
    }
    void MQTTMessage(String channel="", String message=""){
      MQTT.publish(channel.c_str(), message.c_str());
    }
    void MQTTevaluate(String message){
      Serial.println("received mqtt message");
      Serial.print("sending message to be evaluated: ");
      Serial.println(message);
      if( message!=""){
        ConsoleEvaluate(message);
      }
    }
    void MQTTdisconnect(){
      MQTT.disconnect();
    }
    void MQTTSetup(){
      Serial.println("MQTT config: starting...");
      char Server[MQTThost.length() + 1];
      MQTThost.toCharArray(Server, sizeof(Server));       
      MQTT.setServer(Server, MQTTport);
      MQTT.setCallback(MQTTcallback);
      MQTTLoop();
      MQTTMessage(MQTTwillTopic, "true");
      Serial.println("MQTT config: complete");
    }
    void MQTTLoop(){
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
    void Message (String Message="", String MessagePath="", bool SysMessage=false){
      if(SysMessage){
        if (SerialDiagnosticMessagesEnabled){
          Serial.println (MessagePath+"/"+Message);
        }
        if (MQTTDiagnosticMessagesEnabled){
          String MQTTPathSend=MQTTControllerRootPath+UID+MessagePath;
          MQTTMessage(MQTTPathSend, Message);
        }
      }
      else{
        String MQTTPathSend=MQTTControllerRootPath+UID+MessagePath;
        MQTTMessage(MQTTPathSend, Message);      
        Serial.println (Message);
      }
    }
    void MessagingLoop(){
      MQTTLoop();
    }
/* [User Functions]: -------------------------------------------------------------------------------------- */
  /* [U: User Login]: ------------------------------------------------------------------------------------- */
    void UserLogin(String user, String password){
      Message("checking credentials","/console/",true); 
      if (user == AdminUserName && password == AdminPassword){
          AdminUserLoggedIn = true;
          Message("login successful","/console/");  
      }
      else{
        Message("login failed","/console/");   
      }
    }
  /* [U: User Logout]: ------------------------------------------------------------------------------------ */
    void UserLogout(){
      AdminUserLoggedIn=false;
    }
/* [Sensors]: --------------------------------------------------------------------------------------------- */
  /* [S: Reset Button]: ----------------------------------------------------------------------------------- */
    int ResetButtonCounter = 0;
    void ResetButtonSetup(){
      pinMode(ResetButtonPin, INPUT);
    }
    void ResetButtonLoop(){
      if (digitalRead(ResetButtonPin)==LOW){
        ResetButtonCounter++;                
      }
      else{
        ResetButtonCounter=0;
      }
      if (ResetButtonCounter>=3){
        Message("to default settings, including WiFi","/status/reset",false);
        WiFiManagerClear();
        delay(200);
        preferences.begin("preferences", false);
        preferences.clear();
        preferences.end();
        delay(200);
        restart_device();
      }
    }
    void SensorLoop(){
      ResetButtonLoop();
    }
/* [Actuators]: ------------------------------------------------------------------------------------------- */
  /* [A: Heart Beat LED]: --------------------------------------------------------------------------------- */
    void HeartBeatSetup(){
      pinMode(HeartBeatLEDPin, OUTPUT);
    }
    
    void HeartBeatLoop(){
      Message ("triggering ...","/diagnostics/heartbeat",true);
      bool LEDState =(digitalRead(HeartBeatLEDPin));
      digitalWrite(HeartBeatLEDPin,!LEDState);      
    }
    void ActuatorLoop(){
      HeartBeatLoop();      
    }
    
/* [Timers]: ---------------------------------------------------------------------------------------------- */
  /* [T: Interrupt Timers]: ------------------------------------------------------------------------------- */
    #define _TimerINTERRUPT_LOGLEVEL_ 4 // To be included only in main(), .ino with setup() to avoid `Multiple Definitions` Linker Error you can't use Serial.print/println in ISR or crash.
      ESP32Timer ITimer0(0);
      ESP32Timer ITimer1(1);
      volatile bool Timer0Triggered=false;
      volatile bool Timer1Triggered=false;
      bool IRAM_ATTR Timer0ISR(void * TimerNo){ 
        Timer0Triggered=true;
        return true;
      }
      bool IRAM_ATTR Timer1ISR(void * TimerNo){ 
        Timer1Triggered=true;
        return true;
      } 
      void TimerInterruptSetup(){
        if (ITimer0.attachInterruptInterval(Timer0Interval * 1000000, Timer0ISR)){
          Message ("complete", "/diagnostics/setup/timerinterrupt/timer0", true);
        }
        else{
          Message ("failed", "/diagnostics/setup/timerinterrupt/timer0", true);
        }
        if (ITimer1.attachInterruptInterval(Timer1Interval * 1000000, Timer1ISR)){
          Message ("complete", "/diagnostics/setup/timerinterrupt/timer1", true);
        }
        else{
          Message ("failed", "/diagnostics/setup/timerinterrupt/timer1", true);
        }  
      }
      void TimerInterruptLoop(){
        if(Timer0Triggered){
          Message ("triggered", "/diagnostics/timerinterrupt/timer0", true);
          Timer0Triggered=false;
          Timer0Payload();
        }
        if(Timer1Triggered){
          Message ("triggered", "/diagnostics/timerinterrupt/timer1", true);
          Timer1Triggered=false;
          Timer1Payload();
        }
      }
      void Timer0Payload(){
        Message ("started ...", "/diagnostics/timerinterrupt/timer0/payload", true);
          ActuatorLoop();
          SensorLoop();          
          SerialMonitorLoop();
          MessagingLoop();
          WifiManagerLoop();
          JSONEventsUpdate();
        Message ("complete", "/diagnostics/timerinterrupt/timer0/payload", true);
      }
      void Timer1Payload(){ 
        Message ("started ...", "/diagnostics/timerinterrupt/timer1/payload", true);
          JSONStatusUpdate();         
          MQTTMessage(MQTTwillTopic, "true");
        Message ("complete", "/diagnostics/timerinterrupt/timer1/payload", true);
      }
/* [Firmware Update] : ------------------------------------------------------------------------------------ */
  HTTPClient client;      
  int FirmwareTotalLength;       //total size of firmware
  int FirmwareCurrentLength = 0; //current size of written firmware
  void FirmwareUpdateSetup(){
    //if required to run at bootup
  }
  void FirmwareUpdate(uint8_t *data, size_t len){
    Update.write(data, len);
    FirmwareCurrentLength += len;
    Serial.print('.');
    if(FirmwareCurrentLength != FirmwareTotalLength) return;
    Update.end(true);
  }
  void FirmwareFileUpdate(String FirmwareHost){
    Message("attempting update: "+FirmwareHost,"/firmware",true);  
    client.begin(FirmwareHost);
    int resp = client.GET();
    if(resp == 200){
      FirmwareTotalLength = client.getSize();
      int len = FirmwareTotalLength;
      Update.begin(UPDATE_SIZE_UNKNOWN);
      uint8_t buff[128] = { 0 };
      WiFiClient * stream = client.getStreamPtr();
      while(client.connected() && (len > 0 || len == -1)) {
        size_t size = stream->available();
        if(size) {
          int c = stream->readBytes(buff, ((size > sizeof(buff)) ? sizeof(buff) : size));
          FirmwareUpdate(buff, c);
            if(len > 0) {
              len -= c;
            }
          }
          delay(1);
        }
        Message("update successful, resarting... ","/firmware");  
      }
      else{
        Message("update not successful, error: "+String(resp),"/firmware");  
      }
      client.end();
    }
/* [Device Restart] : ------------------------------------------------------------------------------------- */
    void restart_device(){
      Message("in 5","/status/restart",false);digitalWrite(HeartBeatLEDPin,LOW);
      delay(1000);
      Message("in 4","/status/restart",false);
      delay(1000);
      Message("in 3","/status/restart",false);
      delay(1000);
      Message("in 2","/status/restart",false);
      delay(1000);
      Message("in 1","/status/restart",false);
      delay(1000);
      Message("forced restart initiated","/status/restart",false);digitalWrite(HeartBeatLEDPin,HIGH);
      MQTTdisconnect();
      ESP.restart();        
    }
/* [NTP Time] --------------------------------------------------------------------------------------------- */
  WiFiUDP ntpUDP;
  NTPClient timeClient(ntpUDP, "pool.ntp.org", timezone_offset * 60);
  void NTPSetup (){
    timeClient.begin(); // initialize the time client
  }
/* [ArduinoJson] ------------------------------------------------------------------------------------------ */
    const size_t status_capacity = JSON_OBJECT_SIZE(10);
    const size_t events_capacity = JSON_OBJECT_SIZE(2);
    DynamicJsonDocument status_doc(status_capacity);
    DynamicJsonDocument events_doc(events_capacity);
    void JSONStatusUpdate(){
      //#1 source
        status_doc["src"]= UID;
      //#2 power cycles
        status_doc["pwrcyc"] =TotalStarts;
      //#3 get time since startup
        status_doc["millis"] = millis();
      //#4 get timestamp
        timeClient.update();
        status_doc["ts"] = timeClient.getEpochTime();
      //#5 get free memory
        status_doc["free_heap"] = ESP.getFreeHeap();
      //#6 get total memory
        status_doc["total_heap"] = ESP.getHeapSize();
      //#7 get CPU temperature      
        status_doc["MCU_temp"] = temperatureRead();
      //convert to JSON
        String status_jsonString;
        serializeJson(status_doc, status_jsonString);
      //Send message
        Message (status_jsonString, "/status/system");
    }
    void JSONEventsUpdate(){
      //#1 get time since startup
        events_doc["millis"] = millis();
      //#2 get timestamp
        timeClient.update();
        events_doc["ts"] = timeClient.getEpochTime();
      //convert to JSON
        String events_jsonString;
        serializeJson(events_doc, events_jsonString);
      //Send message
        Message (events_jsonString, "/events/reading");      
    }
    void JSONSetup(){
      JSONStatusUpdate();
      JSONEventsUpdate();
    }    
    void JSONLoop(){
    }
/* [Preferences]: ----------------------------------------------------------------------------------------- */
    String PreferencesTest="";
    void PreferencesRead(String instruction="none"){
      preferences.begin("preferences", false);
      if (instruction=="startcounter"||instruction=="all"){
        //Start Counter
        TotalStarts = preferences.getLong("TotalStarts", 0);
        Message ("success", "/console",false);        
        Message (String(TotalStarts), "/preferences/startcounter",true);      
      }
      if (instruction=="serialdiagnostics"||instruction=="all"){
        //SerialDiagnostics     SerialDiagnosticMessagesEnabled
        SerialDiagnosticMessagesEnabled = preferences.getBool("serialdiag",false);
        Message (String(SerialDiagnosticMessagesEnabled), "/preferences/serialdiagnostics",false);  
      }
      if (instruction=="mqttdiagnostics"||instruction=="all"){
        //MQTTDiagnostics
        MQTTDiagnosticMessagesEnabled = preferences.getBool("mqttdiag",false);
        Message (String(MQTTDiagnosticMessagesEnabled), "/preferences/mqttdiagnostics",false);
      }
      if (instruction=="mqtthost"||instruction=="all"){
        //MQTT host
        MQTThost=preferences.getString("MQTThost",MQTThost);
        Message (MQTThost, "/preferences/mqtthost",false);
      }
      if (instruction=="mqttport"||instruction=="all"){
        //MQTT port
        MQTTport=preferences.getInt("MQTTport",MQTTport);
        Message (String(MQTTport), "/preferences/mqttport",false);
      }
      if (instruction=="mqttuser"||instruction=="all"){
        //MQTT user
        MQTTusername=preferences.getString("MQTTusername",MQTTusername);
        Message (MQTTusername, "/preferences/mqttuser",false);
      }
      if (instruction=="mqttpass"||instruction=="all"){
        //MQTT password
        MQTTpassword=preferences.getString("MQTTpassword",MQTTpassword);
        Message ("******", "/preferences/mqttpass",false);        
      }
      if (instruction=="mqttpath"||instruction=="all"){
        //MQTT root path
        MQTTControllerRootPath=preferences.getString("MQTTControllerRootPath",MQTTControllerRootPath);    
        Message (MQTTControllerRootPath, "/preferences/mqttpath",false);        
      }
      if (instruction=="adminusername"||instruction=="all"){
        //admin user name
        AdminUserName = preferences.getString("AdminUserName",AdminUserName);
        Message ("success", "/console",false);        
        Message (AdminUserName, "/preferences/adminusername",true);      
      }
      if (instruction=="adminpassword"||instruction=="all"){
        //admin password
        AdminPassword = preferences.getString("AdminPassword",AdminPassword);
        Message ("success", "/console",false);        
        Message (AdminPassword, "/preferences/adminpassword",true);
      }
      preferences.end();    
    }     
    void PreferencesWrite(String instruction="none"){
      preferences.begin("preferences", false);
      if (instruction=="startcounter"||instruction=="all"){
        //Start Counter
        preferences.putLong("TotalStarts",TotalStarts);   
        Message ("success", "/console",false);   
        Message ("written", "/preferences/startcounter",true);
      }
      if (instruction=="serialdiagnostics"||instruction=="all"){
        //SerialDiagnostics    SerialDiagnosticMessagesEnabled
        preferences.putBool("serialdiag",SerialDiagnosticMessagesEnabled);
        Message ("success", "/console",false);        
        Message ("written", "/preferences/serialdiagnostics",false);        
      }
      if (instruction=="mqttdiagnostics"||instruction=="all"){
        //MQTTDiagnostics
        preferences.putBool("mqtt",MQTTDiagnosticMessagesEnabled);
        Message ("success", "/console",false);        
        Message ("written", "/preferences/mqttdiagnostics",false);
      }
      if (instruction=="mqtthost"||instruction=="all"){
        //MQTT host
        preferences.putString("MQTThost",MQTThost);
        Message ("written", "/preferences/mqtthost",false);
      }
      if (instruction=="mqttport"||instruction=="all"){
        //MQTT port
        preferences.putInt("MQTTport",MQTTport);
        Message ("written", "/preferences/mqttport",false);
      }
      if (instruction=="mqttuser"||instruction=="all"){
        //MQTT user
        preferences.putString("MQTTusername",MQTTusername);
        Message ("written", "/preferences/mqttuser",false);
      }
      if (instruction=="mqttpass"||instruction=="all"){
        //MQTT password
        preferences.putString("MQTTpassword",MQTTpassword);
        Message ("written", "/preferences/mqttpass",false);
      }
      if (instruction=="mqttpath"||instruction=="all"){
        //MQTT root path
        preferences.putString("MQTTControllerRootPath",MQTTControllerRootPath);
        Message ("written", "/preferences/mqttpath",false);
      }
      if (instruction=="adminusername"||instruction=="all"){
        //admin user name
        preferences.putString("AdminUserName",AdminUserName);
        Message ("success", "/console",false);
        Message ("written", "/preferences/adminusername",true);
      }
      if (instruction=="adminpassword"||instruction=="all"){
        //admin password
        preferences.putString("AdminPassword",AdminPassword);
        Message ("success", "/console",false);
        Message ("written", "/preferences/adminpassword",true);
      }
      preferences.end();
    }
    void PreferencesClear(String instruction="none"){
      preferences.begin("preferences", false);
      if (instruction=="startcounter"||instruction=="all"){
        //Start Counter
        preferences.remove("TotalStarts");   
        Message ("success", "/console",false);  
        Message ("cleared", "/preferences/startcounter",true);
      }
      if (instruction=="serialdiagnostics"||instruction=="all"){
        //SerialDiagnostics
        preferences.remove("serialdiag");
        Message ("success", "/console",false);        
        Message ("cleared", "/preferences/serialdiagnostics",false);        
      }
      if (instruction=="mqttdiagnostics"||instruction=="all"){
        //MQTTDiagnostics
        preferences.remove("mqttdiag");
        Message ("success", "/console",false);
        Message ("cleared", "/preferences/mqttdiagnostics",false);
      }
      if (instruction=="mqtthost"||instruction=="all"){
        //MQTT host
        preferences.remove("MQTThost");
        Message ("cleared", "/preferences/mqtthost",false);
      }
      if (instruction=="mqttport"||instruction=="all"){
        //MQTT port
        preferences.remove("MQTTport");
        Message ("cleared", "/preferences/mqttport",false);
      }
      if (instruction=="mqttuser"||instruction=="all"){
        //MQTT user
        preferences.remove("MQTTusername");
        Message ("cleared", "/preferences/mqttuser",false);
      }
      if (instruction=="mqttpass"||instruction=="all"){
        //MQTT password
        preferences.remove("MQTTpassword");
        Message ("cleared", "/preferences/mqttpass",false);
      }
      if (instruction=="mqttpath"||instruction=="all"){
        //MQTT root path
        preferences.remove("MQTTControllerRootPath");
        Message ("cleared", "/preferences/mqttpath",false);
      }
      if (instruction=="adminusername"||instruction=="all"){
        //admin user name
        preferences.remove("AdminUserName");
        Message ("success", "/console",false);
        Message ("cleared", "/preferences/adminusername",true);
      }
      if (instruction=="adminpassword"||instruction=="all"){
        //admin password
        preferences.remove("AdminPassword");
        Message ("success", "/console",false);    
        Message ("cleared", "/preferences/adminpassword",true);
      }
      preferences.end();
    }
    void PreferencesSet(String instruction="none", String value=""){
      preferences.begin("preferences", false);
      if (instruction=="startcounter"){
        //Start Counter
        TotalStarts = atol(value.c_str());
        Message ("success", "/console",false);
        Message (String(TotalStarts), "/preferences/startcounter",true);
      }
      if (instruction=="serialdiagnostics"){
        //SerialDiagnostics
        if (value=="1"||value=="true"){
          SerialDiagnosticMessagesEnabled=true;
        }
        else{
          SerialDiagnosticMessagesEnabled=false;
        }
        Message ("success", "/console",false);        
        Message (String(SerialDiagnosticMessagesEnabled), "/preferences/serialdiagnostics",true);
      }
      if (instruction=="mqttdiagnostics"){
        //MQTTDiagnostics
        if (value=="1"||value=="true"){
          MQTTDiagnosticMessagesEnabled=true;
        }
        else{
          MQTTDiagnosticMessagesEnabled=false;
        }
        Message ("success", "/console",false);        
        Message (String(MQTTDiagnosticMessagesEnabled), "/preferences/mqttdiagnostics",true);
      }
      if (instruction=="mqtthost"){
        //MQTT host
        MQTThost=value;
        Message (MQTThost, "/preferences/mqtthost",false);
      }
      if (instruction=="mqttport"){
        //MQTT port
        MQTTport=atol(value.c_str());
        Message (String(MQTTport), "/preferences/mqttport",false);
      }
      if (instruction=="mqttuser"){
        //MQTT user
        MQTTusername=value;
        Message (MQTTusername, "/preferences/mqttuser",false);
      }
      if (instruction=="mqttpass"){
        //MQTT password
        MQTTpassword=value;
        Message (MQTTpassword, "/preferences/mqttpass",false);
      }
      if (instruction=="mqttpath"){
        //MQTT root path
        preferences.remove("MQTTControllerRootPath");
        Message ("cleared", "/preferences/mqttpath",false);
      }
      if (instruction=="adminusername"){
        //admin user name
        AdminUserName=value;
        Message ("success", "/console",false);        
        Message ("set", "/preferences/adminusername",true);
      }
      if (instruction=="adminpassword"){
        //admin password
        AdminPassword=value;
        Message ("success", "/console",false);
        Message ("set", "/preferences/adminpassword",true);
      }
      preferences.end();
    }
    void PreferencesSetup(){
      preferences.begin("preferences", false);
      //Start Counter
        TotalStarts = preferences.getLong("TotalStarts", 0);
        ++TotalStarts;
        preferences.putLong("TotalStarts",TotalStarts);
        PreferencesRead("all");
      preferences.end();
      }    
/* [Console Functions]: ----------------------------------------------------------------------------------- */      
  /* [C: Console Evaluate]: ------------------------------------------------------------------------------- */     
    String ConsoleCommand[6];
    void SplitCommand(String text,String separator=" "){
      //break up the instruction into max 6 parameters
      text.replace("\n","");
      int length=text.length();
      int ConsoleCommands=0;
      ConsoleCommand[0]="";
      String test="";
      for (int i = 0; i < length; i++) {
        test=text.substring(i,i+1);
        if (test==separator){
          ConsoleCommands++;
          ConsoleCommand[ConsoleCommands]="";
        }
        else {
          ConsoleCommand[ConsoleCommands]=ConsoleCommand[ConsoleCommands]+test;
        }
      }
    }
    void ConsoleEvaluate(String message){
      SplitCommand(message);
      if(message!="message received ..."){
        //clear old commands from console, important for public facing MQTT        
        Message("message received ...","/console");
      }
      if(!AdminUserLoggedIn){      
        //no user logged in
        if (ConsoleCommand[0]=="hello"){
          Message("world","/console/"); 
        }
        if (ConsoleCommand[0]=="user"){
          if (ConsoleCommand[1]=="login"){
            UserLogin(ConsoleCommand[2], ConsoleCommand[3]);
          }
        }
      }
      else {      
        //admin user logged in
        if (ConsoleCommand[0]=="user"){
          if (ConsoleCommand[1]=="logout"){
            UserLogout();
            Message("logged out","/console/");
          }  
        }
        if (ConsoleCommand[0]=="millis"){
          //millis
          Message(String(millis(),DEC),"/console/"); 
        }        
        if (ConsoleCommand[0]=="ip"){
          //get ip
          Message(IpAddress2String(WiFi.localIP()),"/console/"); 
        }
        if (ConsoleCommand[0]=="starts"){
          //number of starts of device
          Message(String(TotalStarts),"/console/");  
        }
        if (ConsoleCommand[0]=="restart"){
          //restart
          Message("restarting","/console/"); 
          restart_device();
        }
        if (ConsoleCommand[0]=="firmware"){
          //firmware
          if (ConsoleCommand[1]== "version"){
            Message(FirmwareVersion,"/console/"); 
          }
          if (ConsoleCommand[1]== "update"){
            Message("Updating Firmware to: "+ConsoleCommand[2],"/console/"); 
            FirmwareFileUpdate(ConsoleCommand[2]);
            Message("Firmware update complete, restarting ..."+ConsoleCommand[2],"/console/"); 
            delay(1000);
            restart_device();
          }
          if (ConsoleCommand[1]== "rollback"){
            Message("Restoring firmware to previous version ...","/console/firmware"); 
            FirmwareFileUpdate(FirmwareRollBackUrl);
            Message("Firmware update complete, restarting ..."+ConsoleCommand[2],"/console/");
            delay(1000);
            restart_device();
          }
        }
        if (ConsoleCommand[0]=="preferences"){
          if (ConsoleCommand[1]=="read"){
            PreferencesRead(ConsoleCommand[2]);
          }
          if (ConsoleCommand[1]=="write"){
            PreferencesWrite(ConsoleCommand[2]);
          }
          if (ConsoleCommand[1]=="clear"){
            PreferencesClear(ConsoleCommand[2]);
          } 
          if (ConsoleCommand[1]=="set"){
            PreferencesSet(ConsoleCommand[2],ConsoleCommand[3]);
          }
        }
      }
    }   
/* [Default Arduino Functions]: --------------------------------------------------------------------------- */
  /* [DAF: Arduino Setup] : ------------------------------------------------------------------------------- */
    void setup(){
      SerialMonitorSetup(); Serial.println("startup commencing ....");
      Serial.println("PreferencesSetup ...");PreferencesSetup(); Serial.println("... PreferencesSetup complete");
      Serial.println("running WiFiManagerSetup ...");WiFiManagerSetup();Serial.println("... WiFiManagerSetup complete");
      Serial.println("running MQTTSetup ...");MQTTSetup();Serial.println("... MQTTSetup complete");
      Serial.println("running FirmwareUpdateSetup ...");FirmwareUpdateSetup();Serial.println("... FirmwareUpdateSetup complete");
      Serial.println("running SpiffsSetup ...");SpiffsSetup();Serial.println("... SpiffsSetup complete");
      Serial.println("running TimerInterruptSetup ...");TimerInterruptSetup();Serial.println("... TimerInterruptSetup complete");
      Serial.println("running HeartBeatSetup ...");HeartBeatSetup();Serial.println("... HeartBeatSetup complete");
      Serial.println("running ResetButtonSetup ...");ResetButtonSetup();Serial.println("... ResetButtonSetup complete");
      Serial.println("running JSONSetup ...");JSONSetup();Serial.println("... JSONSetup complete");
      Serial.println("running planned bootup delay to stabilise the system ...");while (BootDelay*1000>millis()){}Serial.println("... bootup delay complete");
      Serial.println("... startup complete");
    }
  /* [DAF: Arduino Loop] : -------------------------------------------------------------------------------- */
    void loop(){
      TimerInterruptLoop();
    }