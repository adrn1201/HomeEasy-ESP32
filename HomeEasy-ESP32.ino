#include<WiFi.h>
#include<Firebase_ESP_Client.h> 
#include"addons/TokenHelper.h" 
#include"addons/RTDBHelper.h"

#include <DHT.h>
#include <DHT_U.h>
#include <ESP32Servo.h>

#define WIFI_SSID "<YOUR_WIFI_SSID >"
#define WIFI_PASSWORD "<YOUR_WIFI_PASSWORD>"
#define API_KEY "<YOUR_API_KEY>"
#define DATABASE_URL "<YOUR_DB_URL>"

#define RED_LED_PIN 15
#define GREEN_LED_PIN 2

#define BUZZER_PIN 27

#define SERVO_PIN 23

#define trigPin 18
#define echoPin 19

#define DHT_SENSOR_PIN 13
#define DHT_SENSOR_TYPE DHT11

#define LDR_PIN 36

#define LDR_LED_PIN 21

#define ROOM_LED_PIN 12
#define LR_LED_PIN 14

#define PWMChannel1 3
#define PWMChannel2 4

const int freq = 5000;
const int resolution = 8;

const unsigned int interval_dht = 2000, interval_buzzer = 2000;
unsigned long startTime_dht = 0, startTime_buzzer = 0, sendDHTDataPrevMillis = 0, sendMotionDataPrevMillis = 0;

long duration;
bool isSecured, ldr_led_enabled, signupOK = false;
int ldrData = 0, humi;
float distanceCm, temp;

FirebaseData fbdo, fbdo_s1, fbdo_s2; 
FirebaseAuth auth; 
FirebaseConfig config; 

DHT dht_sensor(DHT_SENSOR_PIN, DHT_SENSOR_TYPE);
Servo servo;

void setup() {
  ledcSetup(PWMChannel1, freq, resolution);
  ledcSetup(PWMChannel2, freq, resolution);

  ledcAttachPin(ROOM_LED_PIN, PWMChannel1);
  ledcAttachPin(LR_LED_PIN, PWMChannel2);
  
  pinMode(trigPin, OUTPUT); 
  pinMode(echoPin, INPUT);
  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(LDR_LED_PIN, OUTPUT);
  pinMode(DHT_SENSOR_PIN, INPUT);

  servo.attach(SERVO_PIN);
  dht_sensor.begin();
  Serial.begin(115200);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  
  while(WiFi.status() != WL_CONNECTED){
    Serial.print("."); delay(300);
  }
  
  Serial.println();
  Serial.print("Connected with IP: ");
  Serial.println(WiFi.localIP());
  Serial.println();

  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  if(Firebase.signUp(&config, &auth, "", "")){
    Serial.println("Signup OK");
    signupOK = true;
  }else{
    Serial.printf("%s\n", config.signer.signupError.message.c_str());
  }

  config.token_status_callback = tokenStatusCallback; 

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  
  if(Firebase.RTDB.beginStream(&fbdo_s1, "/componentStatus"))
    Serial.println("Stream 1 begin success");

  if(Firebase.RTDB.beginStream(&fbdo_s2, "/houseLED"))
    Serial.println("Stream 2 begin success");
}

void loop() {
  unsigned long currentTime = millis();

  if(Firebase.ready() && signupOK){
    readStream1();
    readStream2();
  }
  
  control_LDR_LED();
  control_security(currentTime);

  read_DHT_11(currentTime);
  send_DHT11_data(currentTime);
 
}

void readStream1(){
  if(!Firebase.RTDB.readStream(&fbdo_s1))
    Serial.printf("Stream 1 read error, %s\n\n", fbdo_s1.errorReason().c_str());
  
  if(fbdo_s1.streamAvailable() && fbdo_s1.dataType() == "boolean"){
    if(fbdo_s1.dataPath() == "/doorLocked"){
      control_Servo_Motor();
    }else if(fbdo_s1.dataPath() == "/ldrEnabled"){
      ldr_led_enabled = fbdo_s1.boolData();
    }else if(fbdo_s1.dataPath() == "/secured"){
      isSecured = fbdo_s1.boolData();
    }
  }
}

void readStream2(){
  if(!Firebase.RTDB.readStream(&fbdo_s2))
    Serial.printf("Stream 2 read error, %s\n\n", fbdo_s2.errorReason().c_str());
  
  if(fbdo_s2.streamAvailable() && fbdo_s2.dataType() == "int"){
    if(fbdo_s2.dataPath() == "/bedroom")control_PWM_Led(PWMChannel1);
    else if(fbdo_s2.dataPath() == "/livingRoom")control_PWM_Led(PWMChannel2);
  }
}

void control_Servo_Motor(){
  if(fbdo_s1.boolData())servo.write(90);
  else servo.write(180);
}

void control_PWM_Led(int pwmChannel){
  ledcWrite(pwmChannel, fbdo_s2.intData());
}

void control_LDR_LED(){
  if(ldr_led_enabled){
    ldrData = analogRead(LDR_PIN);
    Serial.println(ldrData);
    if(ldrData < 3000)set_LDR_LED_status(HIGH);
    else set_LDR_LED_status(LOW);
    
  }else{
    set_LDR_LED_status(LOW);
  }
}

void set_LDR_LED_status(bool ldr_led_status){
  digitalWrite(LDR_LED_PIN, ldr_led_status);
}

void control_security(unsigned long currentMillis){
     if(isSecured){
      digitalWrite(GREEN_LED_PIN, LOW);
     
      digitalWrite(trigPin, LOW);
      delayMicroseconds(2);
    
      digitalWrite(trigPin, HIGH);
      delayMicroseconds(10);
      digitalWrite(trigPin, LOW);
    
      duration = pulseIn(echoPin, HIGH);
  
      distanceCm = duration * 0.034 / 2;
   
      Serial.print("Distance (cm): ");
      Serial.println(distanceCm);
      if(distanceCm < 6 && distanceCm > 0){
        if(currentMillis - startTime_buzzer >= interval_buzzer){
          startTime_buzzer = currentMillis;
          tone(BUZZER_PIN, 500, 1000); 
        }
        digitalWrite(RED_LED_PIN, HIGH);
        set_Motion_Data(true, currentMillis);
      }
    }else{
      digitalWrite(RED_LED_PIN, LOW);
      digitalWrite(GREEN_LED_PIN, HIGH);
      set_Motion_Data(false, currentMillis);
    }
}

void set_Motion_Data(bool motionData, unsigned long currentMillis){
  if(Firebase.ready() && signupOK && (currentMillis - sendMotionDataPrevMillis > 1000 || sendMotionDataPrevMillis == 0)){
    sendMotionDataPrevMillis = currentMillis;
    if(Firebase.RTDB.setBool(&fbdo, "sensor/motion", motionData)){
      Serial.print("- successfully saved to: " + fbdo.dataPath());
      Serial.println(" (" + fbdo.dataType() + ")");
    }
  }  
}

void read_DHT_11(unsigned long currentMillis){
  if(currentMillis - startTime_dht >= interval_dht){
    startTime_dht = currentMillis;
    
    humi = dht_sensor.readHumidity();
    temp = dht_sensor.readTemperature();
    
  }  
}

void send_DHT11_data(unsigned long currentMillis){
  if(Firebase.ready() && signupOK && (currentMillis - sendDHTDataPrevMillis > 10000 || sendDHTDataPrevMillis == 0)){
    sendDHTDataPrevMillis = currentMillis;
    
    if(Firebase.RTDB.setFloat(&fbdo, "sensor/temperature", temp)){
      Serial.print("- successfully saved to: " + fbdo.dataPath());
      Serial.println(" (" + fbdo.dataType() + ")");
    }

    if(Firebase.RTDB.setInt(&fbdo, "sensor/humidity", humi)){
      Serial.print("- successfully saved to: " + fbdo.dataPath());
      Serial.println(" (" + fbdo.dataType() + ")");
    }
  }
}
