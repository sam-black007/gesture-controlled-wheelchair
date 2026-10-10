/*
  Gesture_Transmitter.ino
  Board: ESP32 DevKit V1 / ESP32-WROOM-32 + MPU6050/GY-521

  MPU6050 wiring:
    VCC -> 3V3, GND -> GND, SDA -> GPIO21, SCL -> GPIO22
  Sends gesture, pitch, roll and speed (maximum 80%) to receiver via ESP-NOW.
  Receives sensitivity settings from the receiver dashboard.
  Direct-register reads are used because the MPU6050 library begin() failed
  on the user's module while I2C address 0x68 responded.
*/

#include <WiFi.h>
#include <Wire.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <math.h>

constexpr int SDA_PIN=21, SCL_PIN=22;
constexpr uint8_t WIFI_CHANNEL=1, MPU_ADDR=0x68;
constexpr uint8_t MAX_GESTURE_PERCENT=80;
constexpr float MAX_TILT_DEG=35.0f;
constexpr float MIN_MOVING_PERCENT=35.0f;
constexpr uint32_t SEND_INTERVAL_MS=50;

enum PacketType : uint8_t { PKT_GESTURE=1, PKT_SETTINGS=2 };
enum Command : uint8_t { CMD_STOP=0, CMD_FORWARD=1, CMD_BACKWARD=2, CMD_LEFT=3, CMD_RIGHT=4 };

typedef struct __attribute__((packed)) {
  uint8_t type;
  uint8_t command;
  uint8_t speedPercent;
  uint8_t sensitivityDeg;
  float pitch;
  float roll;
  uint32_t counter;
} RoverPacket;

float pitchOffset=0, rollOffset=0, pitchDeg=0, rollDeg=0;
uint8_t sensitivityDeg=12;
uint32_t packetCounter=0, lastSendMs=0;
bool sensorOK=false, calibrated=false;

bool writeReg(uint8_t reg,uint8_t value) {
  Wire.beginTransmission(MPU_ADDR); Wire.write(reg); Wire.write(value);
  return Wire.endTransmission()==0;
}
bool readRegs(uint8_t reg,uint8_t* data,uint8_t len) {
  Wire.beginTransmission(MPU_ADDR); Wire.write(reg);
  if(Wire.endTransmission(false)!=0)return false;
  int n=Wire.requestFrom((int)MPU_ADDR,(int)len,(int)true);
  if(n!=len)return false;
  for(uint8_t i=0;i<len;i++)data[i]=Wire.read();
  return true;
}
bool readRaw(int16_t &ax,int16_t &ay,int16_t &az) {
  uint8_t b[6];
  if(!readRegs(0x3B,b,6))return false;
  ax=(int16_t)((b[0]<<8)|b[1]);
  ay=(int16_t)((b[2]<<8)|b[3]);
  az=(int16_t)((b[4]<<8)|b[5]);
  return true;
}
bool getAngles(float &pitch,float &roll) {
  int16_t ax,ay,az;
  if(!readRaw(ax,ay,az))return false;
  float x=ax,y=ay,z=az;
  pitch=atan2f(-x,sqrtf(y*y+z*z))*180.0f/PI;
  roll=atan2f(y,z)*180.0f/PI;
  return true;
}
void calibrate() {
  Serial.println("Hold controller still and level for calibration...");
  float ps=0,rs=0; int good=0;
  for(int i=0;i<100;i++) {
    float p,r;
    if(getAngles(p,r)){ps+=p;rs+=r;good++;}
    delay(20);
  }
  if(good>20) {
    pitchOffset=ps/good; rollOffset=rs/good; calibrated=true;
    Serial.printf("Calibration complete: pitch offset %.2f, roll offset %.2f\n",pitchOffset,rollOffset);
  } else Serial.println("Calibration failed. Check MPU6050 wiring and I2C.");
}
void onDataRecv(const esp_now_recv_info_t *info,const uint8_t *data,int len) {
  if(len!=sizeof(RoverPacket))return;
  RoverPacket p; memcpy(&p,data,sizeof(p));
  if(p.type==PKT_SETTINGS)sensitivityDeg=constrain(p.sensitivityDeg,6,25);
}
void sendPacket(uint8_t cmd,uint8_t speed) {
  RoverPacket p={};
  p.type=PKT_GESTURE; p.command=cmd; p.speedPercent=min((int)speed,(int)MAX_GESTURE_PERCENT);
  p.sensitivityDeg=sensitivityDeg; p.pitch=pitchDeg; p.roll=rollDeg; p.counter=++packetCounter;
  uint8_t bcast[6]={255,255,255,255,255,255};
  esp_now_send(bcast,(uint8_t*)&p,sizeof(p));
}

void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_PIN,SCL_PIN); Wire.setClock(100000);
  sensorOK=writeReg(0x6B,0x00); delay(100);
  uint8_t who=0; bool whoOK=readRegs(0x75,&who,1);
  Serial.printf("MPU read: %s, WHO_AM_I=0x%02X\n",whoOK?"OK":"failed",who);
  WiFi.mode(WIFI_STA); WiFi.setSleep(false);
  esp_wifi_set_channel(WIFI_CHANNEL,WIFI_SECOND_CHAN_NONE);
  if(esp_now_init()!=ESP_OK){Serial.println("ESP-NOW init failed");while(true)delay(1000);}
  esp_now_register_recv_cb(onDataRecv);
  esp_now_peer_info_t peer={};
  memset(peer.peer_addr,0xFF,6); peer.channel=WIFI_CHANNEL; peer.encrypt=false;
  if(!esp_now_is_peer_exist(peer.peer_addr) && esp_now_add_peer(&peer)!=ESP_OK)
    Serial.println("Could not add broadcast peer");
  if(sensorOK)calibrate();
}

void loop() {
  if(millis()-lastSendMs<SEND_INTERVAL_MS)return;
  lastSendMs=millis();

  if(!sensorOK || !calibrated || !getAngles(pitchDeg,rollDeg)) {
    pitchDeg=rollDeg=0;
    sendPacket(CMD_STOP,0);
    return;
  }
  pitchDeg-=pitchOffset; rollDeg-=rollOffset;
  uint8_t cmd=CMD_STOP; float tilt=0;
  // Forward/back from pitch; left/right from roll.
  // If your stick is mounted differently, invert the corresponding signs.
  if(fabsf(pitchDeg)>=sensitivityDeg && fabsf(pitchDeg)>=fabsf(rollDeg)) {
    cmd=pitchDeg<0?CMD_FORWARD:CMD_BACKWARD; tilt=fabsf(pitchDeg);
  } else if(fabsf(rollDeg)>=sensitivityDeg) {
    cmd=rollDeg>0?CMD_RIGHT:CMD_LEFT; tilt=fabsf(rollDeg);
  }
  uint8_t speed=0;
  if(cmd!=CMD_STOP) {
    float t=constrain(tilt,(float)sensitivityDeg,MAX_TILT_DEG);
    float ratio=(t-sensitivityDeg)/(MAX_TILT_DEG-sensitivityDeg);
    speed=(uint8_t)constrain(MIN_MOVING_PERCENT+ratio*(MAX_GESTURE_PERCENT-MIN_MOVING_PERCENT),
                             MIN_MOVING_PERCENT,MAX_GESTURE_PERCENT);
  }
  sendPacket(cmd,speed);

  static uint32_t lastPrint=0;
  if(millis()-lastPrint>500) {
    lastPrint=millis();
    const char* names[]={"STOP","FORWARD","BACKWARD","LEFT","RIGHT"};
    Serial.printf("Pitch %.1f Roll %.1f | %s | %u%% | sensitivity %u deg\n",
                  pitchDeg,rollDeg,names[cmd],speed,sensitivityDeg);
  }
}
