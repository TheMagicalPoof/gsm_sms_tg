#include <ArduinoJson.h>
#include <AsyncTCP.h>
#include "FS.h"
#include "SPIFFS.h"
#include "WiFi.h"
#include "HTTPClient.h"
#include "ESPAsyncWebServer.h"
#include <esp_task_wdt.h>

#define RXD2 16
#define TXD2 17
int TURN_ON_PIN = 2;

class Ethernet {
  public:
    typedef struct {
      const char* ssid;
      const char* password;
      bool isHotspot;
    } EthernetSettings;

    Ethernet(const String &path, const char* ssid, const char* password, bool isHotspot = false) : _path(path){
      _settings = {ssid, password, isHotspot};
      FileWrite();
      Connect();
    }
    Ethernet(const String &path) : _path(path){
      Serial.println("Ethernet init");
      if(!FileRead()){
        Serial.println("FileRead is false");
        _settings = {"KarmaGSM", "12345678", true};
      }
      Connect();
    }

    void SetSettings(const char* ssid, const char* password, bool isHotspot = false){
      _settings = {ssid, password, isHotspot};
      FileWrite();
      Connect();
    }
    void FileWrite(){
      File file = SPIFFS.open(_path.c_str(), FILE_WRITE);
      file.write((uint8_t*)&_settings, sizeof(EthernetSettings));
      file.close();
    }

    bool FileRead(){
      File file = SPIFFS.open(_path.c_str(), FILE_READ);
      if(!file){
        return false;
      }
      file.readBytes((char*)&_settings, sizeof(EthernetSettings));
      file.close();
      return true;
    }

    void NetworksScan(){
      esp_task_wdt_init(30, false);
      _nwNum = WiFi.scanNetworks();
      esp_task_wdt_init(5, false);
    }

    DynamicJsonDocument NetworksToJson(){ 
      DynamicJsonDocument wifiJson(JsonSize());
      if(_nwNum == 0){
        return wifiJson;
      }else{
        for (int i = 0; i < _nwNum; i++){
          wifiJson[i]["SSID"] = WiFi.SSID(i);
          wifiJson[i]["RSSI"] = WiFi.RSSI(i);
        }
      }
      return wifiJson;
    }

    size_t JsonSize(){
      if(_nwNum == 0){
        return 70;
      }
      return _nwNum * 70;
    }

    void Connect(){
      if(_settings.isHotspot){
        if(_settings.password == ""){
          WiFi.softAP(_settings.ssid);
        } else {
          WiFi.softAP(_settings.ssid, _settings.password);
        } 
      } else {
        WiFi.begin(_settings.ssid, _settings.password);
      }
      if(!_settings.isHotspot){
        while (WiFi.status() != WL_CONNECTED){
          Serial.print(".");
          delay(500);
        }
      }
      Serial.println(".");
      Serial.println("WiFi Connected");
      Serial.println(WiFi.softAPIP());
      Serial.println(WiFi.localIP());
    }

  private:
    EthernetSettings _settings;
    String _path;
    int _nwNum;
};

Ethernet * ETHERNET;

class TGSend {
  public:
    typedef struct {
      String botToken;
      String chatId;
    } botCredentials;

    TGSend(){
      _Read();
    }

    void Write(){
      File file = SPIFFS.open("botcredentials.bin", FILE_WRITE);
      file.write((uint8_t*)&_credentials, sizeof(botCredentials));
      file.close();
    }

    void SetToken(String token){
      Serial.println(token);
      _Read();
      _credentials.botToken = token;
      Write();
    }

    void SetChatId(String chatId){
      Serial.println(chatId);
      _Read();
      _credentials.chatId = chatId;
      Write();
    }

    void Send(String text){
      String getStr = "https://api.telegram.org/bot" + _credentials.botToken +"/sendMessage?chat_id=" + _credentials.chatId + "&text=" + text;
      HTTPClient http;
      http.begin(getStr);
      int httpCode = http.GET();

      if (httpCode > 0) {
        Serial.println(httpCode);
        Serial.println("Message has been delivered to Telegram.");
      }
    }

  private:
    botCredentials _credentials;

    void _Read(){
      File file = SPIFFS.open("botcredentials.bin", FILE_READ);
      file.readBytes((char*)&_credentials, sizeof(botCredentials));
      file.close();
    }
};

TGSend * TGSEND;

class Web {
  public:
    Web(int port = 80) : _server(port){
      _ServerInitialize();
      
    }

  private:
    AsyncWebServer _server;
    void _ServerInitialize(){
      _server.on("/", HTTP_GET, [this](AsyncWebServerRequest* request){ _RootHandler(request); });
      _server.on("/wifi", HTTP_GET, [this](AsyncWebServerRequest* request){ _WifiHandler(request); });
      _server.on("/action", HTTP_GET, [this](AsyncWebServerRequest* request) { _ActionHandler(request); });
      _server.on("/give", HTTP_GET, [this](AsyncWebServerRequest* request) { _GiveHandler(request); });
      _server.on("/take", HTTP_GET, [this](AsyncWebServerRequest* request) { _TakeHandler(request); });

      _server.begin();
    }

    void _TakeHandler(AsyncWebServerRequest* request){
      if(request->hasParam("chat_id")){
        AsyncWebParameter* chatId = request->getParam("chat_id");
        TGSEND->SetChatId(chatId->value());
      }

      if(request->hasParam("bot_token")){
        AsyncWebParameter* botToken = request->getParam("bot_token");
        TGSEND->SetToken(botToken->value());
      }

      if(request->hasParam("wifi_name") && request->hasParam("wifi_password")){
        AsyncWebParameter* wifiName = request->getParam("wifi_name");
        AsyncWebParameter* wifiPassword = request->getParam("wifi_password");
        ETHERNET->SetSettings(wifiName->value().c_str(), wifiPassword->value().c_str());
      }

    }

    void _RootHandler(AsyncWebServerRequest* request){
      // request->send(SPIFFS, "/index.html", "text/html");
      request->send(200, "text/html", "Govno");

    }

    void _GiveHandler(AsyncWebServerRequest* request){

    }

    void _WifiHandler(AsyncWebServerRequest* request){
      ETHERNET->NetworksScan();
      DynamicJsonDocument wifiJson(ETHERNET->JsonSize() + 60);
      wifiJson["wifi"] = ETHERNET->NetworksToJson();
      String finishJson;
      serializeJson(wifiJson, finishJson);
      request->send(200, "text/html", finishJson);
    }

    void _ActionHandler(AsyncWebServerRequest* request){
      if(request->hasParam("reset")){

      }

      if(request->hasParam("restart")){
        ESP.restart();
      }
    }

};

void SendData(String rowData){
  Serial2.println(rowData);
}

String SerialExtract(Stream & serial, bool lineFeed = false){
  String rowData = "";
  while(serial.available()){
    char c = serial.read();
    if((c == 10 || c == 13) && lineFeed){
      continue;
    }
    rowData += c;
    delay(2);
  }
  return rowData;
}

String SimCommand(String command){
  SendData(command);
  delay(500);
  String rowData = SerialExtract(Serial2, true);
  rowData.replace(command, "");
  rowData.replace("OK", "");
  return rowData;
}

void Power(bool is_on){
  if (is_on) {
    return;
  }
  pinMode(TURN_ON_PIN, OUTPUT);
  digitalWrite(TURN_ON_PIN, HIGH);
  delay(1000);
  digitalWrite(TURN_ON_PIN, LOW);
  delay(15000);
}

void Power(){
  Power(PowerIsOn());
}

bool PowerIsOn(){
  String data = SimCommand("AT+GSN");
  if (data == ""){
    Serial.println("Power alredy is off");
    return false;
  } else {
    Serial.println("Power alredy is on");
    return true;
  }
}

String UCS2ToString(String s) {                                             // Функция декодирования UCS2 строки
  String result = "";
  if(s.length() < 4){
    return result;  
  } 
  unsigned char c[5] = "";                                                  // Массив для хранения результата
  for (int i = 0; i < s.length() - 3; i += 4) {                             // Перебираем по 4 символа кодировки
    unsigned long code = (((unsigned int)HexSymbolToChar(s[i])) << 12) +    // Получаем UNICODE-код символа из HEX представления
                         (((unsigned int)HexSymbolToChar(s[i + 1])) << 8) +
                         (((unsigned int)HexSymbolToChar(s[i + 2])) << 4) +
                         ((unsigned int)HexSymbolToChar(s[i + 3]));
    if (code <= 0x7F) {                                                     // Теперь в соответствии с количеством байт формируем символ
      c[0] = (char)code;                              
      c[1] = 0;                                                             // Не забываем про завершающий ноль
    } else if (code <= 0x7FF) {
      c[0] = (char)(0xC0 | (code >> 6));
      c[1] = (char)(0x80 | (code & 0x3F));
      c[2] = 0;
    } else if (code <= 0xFFFF) {
      c[0] = (char)(0xE0 | (code >> 12));
      c[1] = (char)(0x80 | ((code >> 6) & 0x3F));
      c[2] = (char)(0x80 | (code & 0x3F));
      c[3] = 0;
    } else if (code <= 0x1FFFFF) {
      c[0] = (char)(0xE0 | (code >> 18));
      c[1] = (char)(0xE0 | ((code >> 12) & 0x3F));
      c[2] = (char)(0x80 | ((code >> 6) & 0x3F));
      c[3] = (char)(0x80 | (code & 0x3F));
      c[4] = 0;
    }
    result += String((char*)c);                       // Добавляем полученный символ к результату
  }
  return (result);
}

unsigned char HexSymbolToChar(char c) {
  if      ((c >= 0x30) && (c <= 0x39)) return (c - 0x30);
  else if ((c >= 'A') && (c <= 'F'))   return (c - 'A' + 10);
  else                                 return (0);
}



void setup() { 
  Serial.begin(115200);
  if(!SPIFFS.begin()){
    Serial.println("SPIFFS Mount Failed");
  }
  Serial.println("setup start");
  ETHERNET = new Ethernet("ethernet.bin");
  TGSEND = new TGSend();
  new Web(80);
 
  Serial2.begin(57600, SERIAL_8N1, RXD2, TXD2);
  Serial2.setRxBufferSize(1024);
  Power();
} 


class SMS {
  public:
  String Number;
  String Message;
  String Who;
  
  SMS(String &sms) : _sms(sms){
    _Parse();
  }

  private:
  String _sms;
  
  void _Parse(){
    int len = _sms.length();
    int quotCount = 0;
    for(int i = 0; i < len; i++){
      if(_sms[i] == '"'){
        quotCount ++;
      } else {
        switch(quotCount){
          case 3:
            Number += _sms[i];
            break;
          case 5:
            Who += _sms[i];
            break;
          case 8:
            Message += _sms[i];
            break;
        }
      }  
    }
  }
};

void loop() {
  String command = SerialExtract(Serial, true);
  if(command != ""){
    String data = SimCommand(command);
    if(data != ""){
      Serial.println(data);
    }
  }
  
  if(Serial2.available()){
    String msgNum = SerialExtract(Serial2, true);

    if(msgNum.startsWith("+CMTI:")){
      Serial.println("New Message!");
      msgNum.remove(0, msgNum.lastIndexOf(",")+1);
      String cmgrCommand = "AT+CMGR=" + msgNum;
      String data = SimCommand(cmgrCommand);

      SMS sms = SMS(data);
      // HTTPClient http;
      String text = "-> " +UCS2ToString(sms.Number) + "<-" + "%0A";

      if (sms.Who != ""){
        text += "От:" + UCS2ToString(sms.Who) + "%0A";
      }

      text += UCS2ToString(sms.Message);
      TGSEND->Send(text);

      String delCommand = "AT+CMGD=" + msgNum;
      SimCommand(delCommand);
      Serial.println("Message №" + msgNum + " has been deleted from GSM module.");
    }
  }
}
