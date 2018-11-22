// Libraries
#include "WiFiEsp.h"
#include <SoftwareSerial.h>
#define SSID        "SSID"
#define PASSWORD    "PASSWORD"
#include <dht.h>
#include <Servo.h>
#include <avr/power.h>
#include <avr/sleep.h>
#include <avr/wdt.h>
#include <Wire.h>
#include <BMP180.h>

ISR(WDT_vect) 
  {
  wdt_disable();  
  }

// Initialize webserver, client, dht22, esp8266 serial, servo, bmp180
WiFiEspServer server(80);
RingBuffer buf(8);
WiFiEspClient STAclient;
dht DHT;
#define DHT22_PIN 5
// Esp8266's serial 
SoftwareSerial serial2(6,7); // RX, TX
Servo myservo;
BMP180 barometer;

float vout = 0.0;
float vin = 0.0;
float vinM=0.0;
int anglevinM=40;
float R1 = 10000.0;
float R2 = 10000.0;
int value = 0;
String temp1,umi,temp2,press;
float rain,light,lux,smoke,ppm;
int o;
int status = WL_IDLE_STATUS;
// Change the value according to your city's sea level pressure
float seaLevelPressure = 93534.8;

void setup() {
  Serial.begin(9600);
  serial2.begin(9600);
  WiFi.init(&serial2);
  barometer = BMP180();
  barometer.SoftReset();
  barometer.Initialize();
  Connect();
  ReadValue();
  SendData();
  SunCheck();
}

void loop() {
  // standby for 30 minutes(energy-saving)
  for(int i=0;i<225;i++){
    sleepNow (0b100001);
    // check if users connect to webserver
    WiFiEspClient APclient = server.available();
    CheckServer(APclient);
  }
  // read sensors values
  ReadValue();
  // push data to google sheet
  SendData();
  // if it's sunny change the inclination of solar panel
  if(o > 06 && o < 13){
    SunCheck();
  }
  while ( status != WL_CONNECTED) {
    status = WiFi.begin(SSID, PASSWORD);
  }
}
void Connect(){
  WiFi.beginAP("SSID", 13, "password", ENC_TYPE_WPA2_PSK,false);
  server.begin();
  while ( status != WL_CONNECTED) {
    status = WiFi.begin(SSID, PASSWORD);
  }
}
void ReadValue(){
  // Read DS18B20 value
  float media = 0;
  for (int a = 0; a < 50; a++) {
    float temp= (5.0 * analogRead(A0) * 100.0) / 1024;
    media = temp + media;
  }
  temp1 = String(media / 50.0);
  // Read dht22 temperature and humidity
  DHT.read22(DHT22_PIN);
  umi = String(DHT.humidity, 1);
  temp2 = String(DHT.temperature, 1);
  // Read rain's sensor
  rain = analogRead(A1);
  // Read photoresistor value and convert it into lux
  light = analogRead(A2);
  float Vout=((5.0/1024.0)*light);
  float ldr=(((220.0* 5)/Vout )- 220.0);
  lux = pow((ldr/75000.0), (1.0/-0.66));
  media=0;
  // Read mq-135 value
  for (int a = 0; a < 50; a++) {
    int fumo = analogRead(A3);
    media=media+fumo;
  }
  smoke=media/50.0;
  ppm=map(smoke,0,1024,100,10000);
  // Read voltage of solar panel
  Voltage();
  // Read ntp time
  o=Ora();
  // Read pressure
  long currentPressure = barometer.GetPressure();
  press=String(currentPressure);
}
void SendData(){
  STAclient.stop();
  if (STAclient.connect("api.pushingbox.com", 80)){
    STAclient.println("GET /pushingbox?devid=ID&temp1="+temp1+
    "&press="+press+"&umi="+umi+"&temp2="+temp2+"&rain="+rain+"&light="+light+"&lux="+lux+"&smoke="+smoke+
    "&ppm="+ppm+"&vps="+vin+"&oraNTP="+o+" HTTP/1.1");
    STAclient.println("Host: api.pushingbox.com");
    STAclient.println("Connection: close");
    STAclient.println();
  }
  delay(10);
  STAclient.stop();
}
void MuoviServo(int pos){
  // Move servo
  myservo.attach(9);
  myservo.write(pos);
  delay(500);
  myservo.detach();
}
void Voltage(){
  value = analogRead(A4);
  vout = (value * 5.0) / 1024.0; 
  vin = vout / (R2/(R1+R2)); 
  if (vin<0.09) {
    vin=0.0;
  }
}
void SunCheck(){
  // Change inclination of solar panel place on the servo
  vinM=0.0;
  for(int i=60;i<180;i+=30){
    MuoviServo(i);
    Voltage();
    if(vin > vinM){
      vinM=vin;
      anglevinM=i;
    }
  }
  MuoviServo(anglevinM);
}
int Ora(){
  // Get ntp hour
  STAclient.stop();
  if(STAclient.connect("utcnist2.colorado.edu", 13)){
    String r = serial2.readStringUntil("\r");
    if(r != NULL){
      STAclient.stop();
      int l=r.length();
      int a=0;
      int ora=0;
      while(r[a]!=':'){
        a++;
      }
      r.remove(0,a+17);
      r.remove(2,l);
      if(r!=NULL){
        return(r.toInt()+1);
      }
    }
    else{
      return(100);
    }
  }
}
void CheckServer(WiFiEspClient APclient){
  // Show html page when an user connect to webserver
  boolean currentLineIsBlank = true;
  while (APclient.connected()) {
    if (APclient.available()) {
      char c = APclient.read();
      buf.push(c);
      if (buf.endsWith("\r\n\r\n")) {
        String pack="<!DOCTYPE html>"
                      "<HTML>"
                        "<HEAD>"
                          "<TITLE>Stazione meteo</TITLE>"
                          "<style>"
                          "h1{"
                              "color: red;"
                              "text-align: center;"
                              "font-family: verdana;"
                            "}"
                          "</style>"
                        "</HEAD>"
                      "<BODY>"
                        "<h1>"
                          "<B>Stazione meteo</B>"
                        "</h1>"
                      "</BODY>"
                      "</HTML>";
          APclient.print(
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html\r\n"
            "Connection: close\r\n"  
            "Refresh: 200\r\n"        
            "\r\n");
          APclient.print(pack);
          break;
        }
      }
    }
  delay(10);
  APclient.stop();
}
void sleepNow(const byte interval)
{
  // Energy saving for arduino
  MCUSR = 0;                          
  WDTCSR |= 0b00011000;               
  WDTCSR =  0b01000000 | interval;    
  wdt_reset();
  set_sleep_mode(SLEEP_MODE_IDLE);
  sleep_enable();
  power_adc_disable();
  power_spi_disable();
  power_timer0_disable();
  power_timer1_disable();
  power_timer2_disable();
  power_twi_disable();
  sleep_mode();
  sleep_disable();
  power_all_enable();
} 
