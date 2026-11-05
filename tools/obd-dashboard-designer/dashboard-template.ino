#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <Arduino_GFX_Library.h>
#include <Preferences.h>

constexpr int TFT_SCK=18,TFT_MOSI=23,TFT_CS=5,TFT_DC=16,TFT_RST=17,TFT_BL=4;
constexpr int OLED_SDA=21,OLED_SCL=22;
constexpr int ENCODER_CLK=32,ENCODER_DT=25,ENCODER_SW=26,RESET_BTN=33;
constexpr int DISPLAY_MODE_BTN=27;
constexpr uint8_t ESPNOW_CHANNEL=1;
constexpr uint32_t PACKET_MAGIC=0x43414E33;
constexpr uint16_t PACKET_VERSION=3;
constexpr uint32_t HEARTBEAT_MAGIC=0x41434B33;
const uint8_t BROADCAST_ADDRESS[6]={0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

constexpr uint16_t C_BLACK=0x0000,C_WHITE=0xFFFF,C_LIGHT=0xC618,C_MID=0x8410;
constexpr uint16_t C_GOLD=0xD567,C_DARK_GOLD=0x9BC0,C_ORANGE=0xFD20,C_RED=0xF800;

enum ValueBit : uint16_t {
  V_RPM=1<<0,V_LOAD=1<<1,V_THROTTLE=1<<2,V_TORQUE=1<<3,
  V_COOLANT=1<<4,V_OIL=1<<5,V_INTAKE=1<<6,V_MAP=1<<7,
  V_MAF=1<<8,V_RAIL=1<<9,V_FUEL=1<<10,V_AFR=1<<11,
  V_TIMING=1<<12,V_BATTERY=1<<13,V_SPEED=1<<14
};
enum StatusBit : uint16_t {
  S_CAN_STARTED=1<<0,S_ECU_SEEN=1<<1,S_CAN_RECENT=1<<2,S_BUS_WARNING=1<<3,
  S_BUS_OFF=1<<4,S_TX_FAILED=1<<5,S_DETECTING=1<<6,
  S_INCOMPATIBLE_29BIT=1<<7,S_PROTOCOL_NOT_FOUND=1<<8,S_RATE_250K=1<<9,
  S_OBD_RECENT=1<<10
};
enum DiagnosticCode : uint8_t {
  D_OK=0,D_DETECTING=1,D_CAN_INIT_FAILED=2,D_NO_CAN_ACK=3,
  D_ECU_NO_REPLY=4,D_BUS_ACTIVE_NO_OBD=5,D_CAN_RX_ERRORS=6,
  D_BUS_OFF=7,D_OBD_DATA_LOST=8,D_29BIT=29
};

struct __attribute__((packed)) TelemetryPacket {
  uint32_t magic;
  uint16_t version,size;
  uint32_t sequence,senderMillis,canRxFrames,obdReplies,obdTimeouts;
  uint16_t validMask,status;
  uint8_t diagnosticCode,canState,txErrorCounter,rxErrorCounter;
  uint32_t txFailedCount,rxMissedCount,rxOverrunCount;
  float rpm,load,throttle,torque,coolantF,oilF,intakeF,mapKpa;
  float airflowLbMin,fuelRailPsi,fuelPercent,commandedAfr;
  float timingDeg,batteryV,speedMph;
  uint8_t activePid,reserved[3];
};

struct __attribute__((packed)) DashboardHeartbeat {
  uint32_t magic;
  uint32_t receivedSequence;
  uint32_t dashboardMillis;
  uint16_t logMask;
  uint8_t loggingEnabled;
  uint8_t reserved;
};
static_assert(sizeof(TelemetryPacket)==112,"Telemetry packet layout changed");
static_assert(sizeof(DashboardHeartbeat)==16,"Heartbeat layout changed");

Arduino_DataBus *bus=new Arduino_ESP32SPI(TFT_DC,TFT_CS,TFT_SCK,TFT_MOSI,GFX_NOT_DEFINED);
Arduino_GFX *tft=new Arduino_ST7796(bus,TFT_RST,1,false);
U8G2_SSD1309_128X64_NONAME0_F_HW_I2C oled(U8G2_R0,U8X8_PIN_NONE);

TelemetryPacket latest={};
portMUX_TYPE packetMux=portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t packetCount=0,lastPacketMs=0,lostPackets=0;
volatile int8_t radioRssi=-127;
uint32_t previousSequence=0,lastTftMs=0,lastOledMs=0;
uint8_t page=0,encoderPrevious=0;
int8_t encoderMovement=0;
bool redraw=true,oledFound=false,specialScreenShown=false,barMode=false;
float maxSpeed=0,maxRpm=0,maxCoolant=0,maxOil=0;
Preferences preferences;

enum WarningFlag : uint8_t {
  W_COOLANT=1<<0,W_OIL=1<<1,W_INTAKE=1<<2,
  W_MAP=1<<3,W_FUEL=1<<4,W_VOLTAGE=1<<5
};
enum SensorId : uint8_t {
  SENSOR_SPEED,SENSOR_RPM,SENSOR_LOAD,SENSOR_THROTTLE,SENSOR_COOLANT,
  SENSOR_OIL,SENSOR_INTAKE,SENSOR_MAP,SENSOR_MAF,SENSOR_RAIL,
  SENSOR_FUEL,SENSOR_AFR,SENSOR_TIMING,SENSOR_BATTERY,SENSOR_TORQUE,
  SENSOR_OBD_REPLIES,SENSOR_COUNT
};
enum SettingsScreen : uint8_t {
  SETTINGS_HOME,SETTINGS_COLORS,SETTINGS_UNITS,SETTINGS_BRIGHTNESS,
  SETTINGS_FORMAT,SETTINGS_WARNINGS,SETTINGS_SLOTS,SETTINGS_LOGGING,SETTINGS_PROFILES
};
enum TempUnit : uint8_t { TEMP_F,TEMP_C };
enum SpeedUnit : uint8_t { SPEED_MPH,SPEED_KMH };
enum PressureUnit : uint8_t { PRESSURE_KPA,PRESSURE_PSI,PRESSURE_BAR };
enum AirflowUnit : uint8_t { AIRFLOW_LB_MIN,AIRFLOW_G_S };
enum NumberFormat : uint8_t { FORMAT_AUTO,FORMAT_0,FORMAT_1,FORMAT_2 };
enum ProfilePreset : uint8_t { PROFILE_DAILY,PROFILE_PERFORMANCE,PROFILE_TOWING,PROFILE_DIAGNOSTIC,PROFILE_CUSTOM };

struct DashboardSettings {
  uint16_t background=C_LIGHT;
  uint16_t text=C_DARK_GOLD;
  uint16_t value=C_GOLD;
  uint16_t warning=C_RED;
  uint8_t warningMask=W_COOLANT|W_OIL|W_INTAKE|W_MAP|W_FUEL|W_VOLTAGE;
  uint8_t slots[8]={SENSOR_COOLANT,SENSOR_OIL,SENSOR_MAP,SENSOR_BATTERY,SENSOR_FUEL,SENSOR_RPM,SENSOR_SPEED,SENSOR_INTAKE};
  uint8_t slotCount=6;
  uint8_t tempUnit=TEMP_F;
  uint8_t speedUnit=SPEED_MPH;
  uint8_t pressureUnit=PRESSURE_KPA;
  uint8_t airflowUnit=AIRFLOW_LB_MIN;
  uint8_t numberFormat=FORMAT_AUTO;
  uint8_t brightness=204;
  uint8_t loggingEnabled=1;
  uint16_t logMask=0x7FFF;
  uint8_t profile=PROFILE_DAILY;
};

DashboardSettings settings,savedSettings;
bool testMode=false;
bool settingsOpen=false,settingsEditing=false;
bool settingsNeedsFullRedraw=true;
SettingsScreen settingsScreen=SETTINGS_HOME;
uint8_t settingsCursor=0;

const uint16_t colorPalette[]={
  C_BLACK,C_WHITE,C_LIGHT,C_MID,C_GOLD,C_DARK_GOLD,C_ORANGE,C_RED,
  0x001F,0x07E0,0x07FF,0x780F
};
const char *colorNames[]={"BLACK","WHITE","LIGHT GRAY","DARK GRAY","GOLD","DARK GOLD","ORANGE","RED","BLUE","GREEN","CYAN","PURPLE"};
constexpr uint8_t COLOR_COUNT=sizeof(colorPalette)/sizeof(colorPalette[0]);

const char *sensorNames[SENSOR_COUNT]={
  "VEHICLE SPEED","ENGINE RPM","ENGINE LOAD","THROTTLE","COOLANT TEMP",
  "OIL TEMP","INTAKE TEMP","MANIFOLD MAP","AIRFLOW","FUEL RAIL",
  "FUEL LEVEL","CMD AIR/FUEL","IGN TIMING","BATTERY","ENGINE TORQUE","OBD REPLIES"
};
const char *sensorUnits[SENSOR_COUNT]={"MPH","RPM","%","%","F","F","F","kPa","lb/min","PSI","%","AFR","deg","V","%",""};
const float sensorMaximums[SENSOR_COUNT]={160,8000,100,100,300,300,220,250,30,3000,100,20,60,16,100,1000};

const char *labels[2][8]={
  {"VEHICLE SPEED","ENGINE RPM","ENGINE LOAD","THROTTLE",
   "COOLANT TEMP","OIL TEMP","INTAKE TEMP","MANIFOLD MAP"},
  {"AIRFLOW","FUEL RAIL","FUEL LEVEL","CMD AIR/FUEL",
   "IGN TIMING","BATTERY","ENGINE TORQUE","OBD REPLIES"}
};
const char *units[2][8]={
  {"MPH","RPM","%","%","F","F","F","kPa"},
  {"lb/min","PSI","%","AFR","deg","V","%",""}
};

void onReceive(const esp_now_recv_info_t *info,const uint8_t *data,int length){
  if(length!=(int)sizeof(TelemetryPacket))return;
  TelemetryPacket incoming;
  memcpy(&incoming,data,sizeof(incoming));
  if(incoming.magic!=PACKET_MAGIC||incoming.version!=PACKET_VERSION||incoming.size!=sizeof(incoming))return;
  portENTER_CRITICAL(&packetMux);
  if(previousSequence&&incoming.sequence>previousSequence+1)lostPackets+=incoming.sequence-previousSequence-1;
  previousSequence=incoming.sequence;
  latest=incoming; packetCount++; lastPacketMs=millis();
  if(info&&info->rx_ctrl)radioRssi=info->rx_ctrl->rssi;
  portEXIT_CRITICAL(&packetMux);
}

bool radioAlive(){return testMode||(packetCount&&millis()-lastPacketMs<1500);}

TelemetryPacket simulatedPacket(){
  TelemetryPacket p={};
  float wave=(sinf(millis()/1800.0f)+1.0f)*0.5f;
  uint8_t scene=(millis()/4000UL)%7;
  p.magic=PACKET_MAGIC;p.version=PACKET_VERSION;p.size=sizeof(p);
  p.sequence=millis()/100;p.senderMillis=millis();
  p.validMask=0x7FFF;p.status=S_CAN_STARTED|S_ECU_SEEN|S_CAN_RECENT|S_OBD_RECENT;
  p.diagnosticCode=D_OK;p.canState=1;p.canRxFrames=12000+millis()/10;p.obdReplies=3500+millis()/100;
  p.rpm=750+wave*5750;p.load=18+wave*75;p.throttle=5+wave*88;p.torque=12+wave*82;
  p.coolantF=188+wave*18;p.oilF=195+wave*25;p.intakeF=72+wave*48;p.mapKpa=32+wave*155;
  p.airflowLbMin=1.2f+wave*23.0f;p.fuelRailPsi=500+wave*1700;p.fuelPercent=78-wave*48;
  p.commandedAfr=14.2f+wave;p.timingDeg=8+wave*28;p.batteryV=13.5f+wave*0.8f;p.speedMph=wave*125;
  if(scene==1)p.coolantF=315;
  else if(scene==2)p.oilF=315;
  else if(scene==3)p.intakeF=215;
  else if(scene==4)p.mapKpa=235;
  else if(scene==5)p.fuelPercent=7;
  else if(scene==6)p.batteryV=10.8f;
  return p;
}

TelemetryPacket snapshot(){
  TelemetryPacket p;
  portENTER_CRITICAL(&packetMux);p=latest;portEXIT_CRITICAL(&packetMux);
  return p;
}

void printTft(const String &s,int x,int y,uint8_t size,uint16_t color){
  tft->setTextSize(size);tft->setTextColor(color);tft->setCursor(x,y);tft->print(s);
}

bool initializeReceiverOled(){
  Serial.println("Scanning receiver I2C bus on SDA=21 SCL=22...");
  uint8_t address=0;
  for(uint8_t candidate=1;candidate<127;candidate++){
    Wire.beginTransmission(candidate);
    if(Wire.endTransmission()==0){
      Serial.printf("I2C device found at 0x%02X\n",candidate);
      if(candidate==0x3C||candidate==0x3D)address=candidate;
    }
  }
  if(!address){
    Serial.println("OLED ERROR: no display answered at 0x3C or 0x3D");
    return false;
  }
  oled.setI2CAddress(address<<1);
  oled.setBusClock(100000);
  oled.begin();
  oled.setPowerSave(0);
  oled.setContrast(255);
  oled.clearBuffer();
  oled.setFont(u8g2_font_6x10_tf);
  oled.drawFrame(0,0,128,64);
  oled.drawStr(20,20,"OLED TEST OK");
  oled.drawStr(9,36,"SSD1309 RECEIVER");
  char addressLine[22];snprintf(addressLine,sizeof(addressLine),"I2C ADDRESS: 0x%02X",address);
  oled.drawStr(6,52,addressLine);
  oled.sendBuffer();
  Serial.printf("Receiver SSD1309 initialized at 0x%02X\n",address);
  delay(1200);
  return true;
}

void loadSettings(){
  preferences.begin("dash-config",true);
  if(preferences.getBytesLength("settings")==sizeof(DashboardSettings))
    preferences.getBytes("settings",&settings,sizeof(settings));
  preferences.end();
  for(uint8_t &slot:settings.slots)if(slot>=SENSOR_COUNT)slot=SENSOR_RPM;
  if(settings.slotCount!=4&&settings.slotCount!=6&&settings.slotCount!=8)settings.slotCount=6;
  if(settings.tempUnit>TEMP_C)settings.tempUnit=TEMP_F;
  if(settings.speedUnit>SPEED_KMH)settings.speedUnit=SPEED_MPH;
  if(settings.pressureUnit>PRESSURE_BAR)settings.pressureUnit=PRESSURE_KPA;
  if(settings.airflowUnit>AIRFLOW_G_S)settings.airflowUnit=AIRFLOW_LB_MIN;
  if(settings.numberFormat>FORMAT_2)settings.numberFormat=FORMAT_AUTO;
  if(settings.brightness<26)settings.brightness=26;
  if(settings.profile>PROFILE_CUSTOM)settings.profile=PROFILE_CUSTOM;
  savedSettings=settings;
}

void saveSettings(){
  preferences.begin("dash-config",false);
  preferences.putBytes("settings",&settings,sizeof(settings));
  preferences.end();
  savedSettings=settings;
}

uint8_t nearestPaletteIndex(uint16_t color){
  for(uint8_t i=0;i<COLOR_COUNT;i++)if(colorPalette[i]==color)return i;
  return 0;
}

uint16_t sensorBit(uint8_t id){
  const uint16_t bits[SENSOR_COUNT]={
    V_SPEED,V_RPM,V_LOAD,V_THROTTLE,V_COOLANT,V_OIL,V_INTAKE,V_MAP,
    V_MAF,V_RAIL,V_FUEL,V_AFR,V_TIMING,V_BATTERY,V_TORQUE,0
  };
  return id<SENSOR_COUNT?bits[id]:0;
}

float sensorValue(const TelemetryPacket &p,uint8_t id){
  switch(id){
    case SENSOR_SPEED:return p.speedMph; case SENSOR_RPM:return p.rpm;
    case SENSOR_LOAD:return p.load; case SENSOR_THROTTLE:return p.throttle;
    case SENSOR_COOLANT:return p.coolantF; case SENSOR_OIL:return p.oilF;
    case SENSOR_INTAKE:return p.intakeF; case SENSOR_MAP:return p.mapKpa;
    case SENSOR_MAF:return p.airflowLbMin; case SENSOR_RAIL:return p.fuelRailPsi;
    case SENSOR_FUEL:return p.fuelPercent; case SENSOR_AFR:return p.commandedAfr;
    case SENSOR_TIMING:return p.timingDeg; case SENSOR_BATTERY:return p.batteryV;
    case SENSOR_TORQUE:return p.torque; case SENSOR_OBD_REPLIES:return p.obdReplies;
    default:return 0;
  }
}

float sensorDisplayValue(const TelemetryPacket &p,uint8_t id){
  float value=sensorValue(p,id);
  if(id==SENSOR_SPEED&&settings.speedUnit==SPEED_KMH)return value*1.609344f;
  if((id==SENSOR_COOLANT||id==SENSOR_OIL||id==SENSOR_INTAKE)&&settings.tempUnit==TEMP_C)return (value-32.0f)/1.8f;
  if(id==SENSOR_MAP){
    if(settings.pressureUnit==PRESSURE_PSI)return value*0.1450377f;
    if(settings.pressureUnit==PRESSURE_BAR)return value/100.0f;
  }
  if(id==SENSOR_RAIL){
    if(settings.pressureUnit==PRESSURE_KPA)return value*6.89476f;
    if(settings.pressureUnit==PRESSURE_BAR)return value*0.0689476f;
  }
  if(id==SENSOR_MAF&&settings.airflowUnit==AIRFLOW_G_S)return value*7.55987f;
  return value;
}

const char *sensorUnit(uint8_t id){
  if(id==SENSOR_SPEED)return settings.speedUnit==SPEED_KMH?"km/h":"MPH";
  if(id==SENSOR_COOLANT||id==SENSOR_OIL||id==SENSOR_INTAKE)return settings.tempUnit==TEMP_C?"C":"F";
  if(id==SENSOR_MAP||id==SENSOR_RAIL){
    if(settings.pressureUnit==PRESSURE_PSI)return "PSI";
    if(settings.pressureUnit==PRESSURE_BAR)return "bar";
    return "kPa";
  }
  if(id==SENSOR_MAF)return settings.airflowUnit==AIRFLOW_G_S?"g/s":"lb/min";
  return sensorUnits[id];
}

uint8_t decimalsForSensor(uint8_t id){
  if(settings.numberFormat!=FORMAT_AUTO)return settings.numberFormat-1;
  if(id==SENSOR_MAF)return 2;
  if(id==SENSOR_SPEED||id==SENSOR_AFR||id==SENSOR_TIMING||id==SENSOR_BATTERY)return 1;
  return 0;
}

String formattedSensorValue(const TelemetryPacket &p,uint8_t id){
  return String(sensorDisplayValue(p,id),static_cast<unsigned int>(decimalsForSensor(id)));
}

uint8_t sensorAlert(const TelemetryPacket &p,uint8_t id){
  if(id==SENSOR_COOLANT&&(settings.warningMask&W_COOLANT)&&p.coolantF>=300)return 2;
  if(id==SENSOR_OIL&&(settings.warningMask&W_OIL)&&p.oilF>=300)return 2;
  if(id==SENSOR_INTAKE&&(settings.warningMask&W_INTAKE)&&p.intakeF>=200)return 2;
  if(id==SENSOR_MAP&&(settings.warningMask&W_MAP)&&p.mapKpa>=220)return 2;
  if(id==SENSOR_FUEL&&(settings.warningMask&W_FUEL)&&p.fuelPercent<10)return 1;
  if(id==SENSOR_BATTERY&&(settings.warningMask&W_VOLTAGE)&&(p.batteryV<11.5f||p.batteryV>15.2f))return 2;
  return 0;
}

uint16_t bitForCell(int cell){
  const uint16_t bits[2][8]={
    {V_SPEED,V_RPM,V_LOAD,V_THROTTLE,V_COOLANT,V_OIL,V_INTAKE,V_MAP},
    {V_MAF,V_RAIL,V_FUEL,V_AFR,V_TIMING,V_BATTERY,V_TORQUE,0}
  };
  return bits[page][cell];
}

String cellValue(const TelemetryPacket &p,int i){
  const uint8_t ids[2][8]={{SENSOR_SPEED,SENSOR_RPM,SENSOR_LOAD,SENSOR_THROTTLE,SENSOR_COOLANT,SENSOR_OIL,SENSOR_INTAKE,SENSOR_MAP},
                            {SENSOR_MAF,SENSOR_RAIL,SENSOR_FUEL,SENSOR_AFR,SENSOR_TIMING,SENSOR_BATTERY,SENSOR_TORQUE,SENSOR_OBD_REPLIES}};
  return formattedSensorValue(p,ids[page][i]);
}

uint8_t alertLevel(const TelemetryPacket &p,int i){
  if(page==0){
    if(i==4)return sensorAlert(p,SENSOR_COOLANT);
    if(i==5)return sensorAlert(p,SENSOR_OIL);
    if(i==6)return sensorAlert(p,SENSOR_INTAKE);
    if(i==7)return sensorAlert(p,SENSOR_MAP);
  }else{
    if(i==2)return sensorAlert(p,SENSOR_FUEL);
    if(i==5)return sensorAlert(p,SENSOR_BATTERY);
  }
  return 0;
}

const char *alertText(int i){
  if(page==0){if(i==4)return "ENGINE OVERHEAT";if(i==5)return "OIL OVERHEAT";
    if(i==6)return "INTAKE AIR DANGER";if(i==7)return "MANIFOLD DANGER";}
  else {if(i==2)return "LOW FUEL";if(i==5)return "CHARGING DANGER";}
  return "WARNING";
}

void drawPage(){
  tft->fillScreen(settings.background);tft->fillRect(0,0,480,35,C_MID);
  printTft(page?"FUEL / ENGINE DATA":"LIVE VEHICLE DATA",10,9,2,settings.value);
  printTft(testMode?"TEST":"",390,11,1,C_ORANGE);printTft(String(page+1)+"/2",442,11,1,settings.value);tft->drawFastHLine(0,35,480,settings.value);
  for(int i=0;i<8;i++){
    int x=(i/4)*240,y=45+(i%4)*68;
    tft->drawRect(x+5,y,230,61,settings.text);
  }
  redraw=false;
}

void drawBarPage(){
  tft->fillScreen(settings.background);tft->fillRect(0,0,480,35,C_MID);
  printTft("CUSTOM OBD DASHBOARD",10,9,2,settings.value);
  printTft(testMode?"TEST":"",350,11,1,C_ORANGE);printTft(String(settings.slotCount)+" SLOTS",405,11,1,settings.value);
  tft->drawFastHLine(0,35,480,settings.value);
  int rows=settings.slotCount/2,rowHeight=270/rows;
  for(int i=0;i<settings.slotCount;i++){
    int x=6+(i%2)*240,y=42+(i/2)*rowHeight,w=228,h=rowHeight-6;
    uint8_t id=settings.slots[i];
    tft->drawRect(x,y,w,h,settings.text);
    printTft(sensorNames[id],x+8,y+7,settings.slotCount==8?1:2,settings.text);
    int barY=y+h-22;
    tft->drawRect(x+8,barY,w-16,16,settings.text);
    tft->fillRect(x+10,barY+2,w-20,12,C_WHITE);
  }
  redraw=false;
}

void drawCurrentScreen(){if(barMode)drawBarPage();else drawPage();}

void updateBars(const TelemetryPacket &p){
  bool online=radioAlive();
  int rows=settings.slotCount/2,rowHeight=270/rows;
  for(int i=0;i<settings.slotCount;i++){
    uint8_t id=settings.slots[i];
    int x=6+(i%2)*240,y=42+(i/2)*rowHeight,w=228,h=rowHeight-6,barY=y+h-22;
    tft->fillRect(x+10,barY+2,w-20,12,C_WHITE);
    tft->fillRect(x+w-112,y+4,104,22,settings.background);
    if(!online){printTft("LINK LOST",x+w-105,y+7,1,settings.warning);continue;}
    uint16_t required=sensorBit(id);
    if(required&&!(p.validMask&required)){printTft("N/A",x+w-35,y+7,1,settings.warning);continue;}
    float value=sensorValue(p,id);
    float ratio=constrain(value/sensorMaximums[id],0.0f,1.0f);
    uint8_t warning=sensorAlert(p,id);
    uint16_t color=warning?settings.warning:settings.value;
    int fillWidth=int((w-20)*ratio);
    if(fillWidth>0)tft->fillRect(x+10,barY+2,fillWidth,12,color);
    String shown=formattedSensorValue(p,id)+" "+sensorUnit(id);
    tft->setTextSize(settings.slotCount==4?2:1);int16_t x1,y1;uint16_t tw,th;
    tft->getTextBounds(shown,0,0,&x1,&y1,&tw,&th);
    printTft(shown,x+w-8-(int)tw,y+7,settings.slotCount==4?2:1,warning?settings.warning:settings.text);
  }
}

void showDiagnosticError(const TelemetryPacket &p){
  uint16_t bg=(p.diagnosticCode==D_DETECTING)?C_LIGHT:
              ((p.diagnosticCode==D_ECU_NO_REPLY||p.diagnosticCode==D_BUS_ACTIVE_NO_OBD)?C_ORANGE:C_RED);
  uint16_t fg=(bg==C_LIGHT)?C_DARK_GOLD:C_BLACK;
  tft->fillScreen(bg);
  String code="ERROR E";
  if(p.diagnosticCode<10)code+="0";
  code+=String(p.diagnosticCode);
  printTft(code,145,38,4,fg);
  switch(p.diagnosticCode){
    case D_DETECTING:
      printTft("DETECTING OBD SYSTEM",55,125,3,fg);printTft("PLEASE WAIT",160,180,2,C_GOLD);break;
    case D_CAN_INIT_FAILED:
      printTft("CAN CONTROLLER FAILED",65,115,3,fg);printTft("RESTART ESP32",145,175,2,fg);break;
    case D_NO_CAN_ACK:
      printTft("NO CAN ACKNOWLEDGEMENT",43,105,3,fg);
      printTft("CHECK CAN PAL VCC + SLNT",67,165,2,fg);printTft("CHECK H / L / GROUND",91,210,2,fg);break;
    case D_ECU_NO_REPLY:
      printTft("ECU NOT REPLYING",98,105,3,fg);
      printTft("START ENGINE - IGNITION ON",58,165,2,fg);printTft("VERIFY OBD PINS 6, 14, 5",59,210,2,fg);break;
    case D_BUS_ACTIVE_NO_OBD:
      printTft("CAN BUS IS ACTIVE",96,105,3,fg);
      printTft("NO MODE 01 OBD RESPONSE",73,165,2,fg);printTft("ADDRESS / VEHICLE PROFILE",67,210,2,fg);break;
    case D_CAN_RX_ERRORS:
      printTft("CORRUPTED CAN SIGNAL",72,105,3,fg);
      printTft("CHECK H/L AND CONNECTIONS",55,165,2,fg);printTft("TERMINATION MUST BE OFF",66,210,2,fg);break;
    case D_BUS_OFF:
      printTft("CAN CONTROLLER BUS-OFF",47,105,3,fg);
      printTft("CHECK H/L, SLNT, GROUND",70,165,2,fg);printTft("RESET AFTER REPAIR",112,210,2,fg);break;
    case D_OBD_DATA_LOST:
      printTft("OBD DATA STOPPED",90,105,3,fg);
      printTft("CONNECTION WAS WORKING",84,165,2,fg);printTft("CHECK PLUG / IGNITION",96,210,2,fg);break;
    case D_29BIT:
      printTft("29-BIT OBD-II DETECTED",35,105,3,fg);
      printTft("CURRENT PROFILE DISABLED",76,165,2,fg);break;
    default:printTft("UNKNOWN CAN / OBD ERROR",52,135,3,fg);break;
  }
}

void updateTft(const TelemetryPacket &p){
  bool online=radioAlive(),flash=(millis()/500)%2;
  if(!online){
    specialScreenShown=true;tft->fillScreen(C_RED);
    printTft("ERROR E30",145,55,4,C_BLACK);
    printTft("TRANSMITTER LINK LOST",48,120,3,C_BLACK);
    printTft("CHECK TRANSMITTER POWER",75,180,2,C_BLACK);
    printTft("CHECK ESP-NOW CHANNEL 1",72,225,2,C_BLACK);
    return;
  }
  if(online&&p.diagnosticCode!=D_OK){specialScreenShown=true;showDiagnosticError(p);return;}
  if(specialScreenShown){specialScreenShown=false;drawCurrentScreen();}
  if(barMode){updateBars(p);return;}
  for(int i=0;i<8;i++){
    int x=(i/4)*240,y=45+(i%4)*68;
    uint16_t required=bitForCell(i);
    bool valid=online&&(required==0||(p.validMask&required));
    uint8_t warning=valid?alertLevel(p,i):0;
    uint16_t bg=settings.background,fg=settings.text;
    if(warning==1){bg=settings.warning;fg=C_BLACK;}
    if(warning==2){bg=flash?settings.warning:C_MID;fg=flash?C_BLACK:C_WHITE;}
    tft->fillRect(x+6,y+1,228,59,bg);tft->drawRect(x+5,y,230,61,warning?C_WHITE:settings.text);
    if(!online){printTft("RADIO LINK LOST",x+12,y+7,2,settings.warning);printTft("--",x+145,y+32,3,settings.warning);}
    else if(!valid){printTft(labels[page][i],x+12,y+7,2,settings.text);printTft("NOT SUPPORTED",x+48,y+35,2,settings.warning);}
    else if(warning){
      const uint8_t ids[2][8]={{SENSOR_SPEED,SENSOR_RPM,SENSOR_LOAD,SENSOR_THROTTLE,SENSOR_COOLANT,SENSOR_OIL,SENSOR_INTAKE,SENSOR_MAP},
                                {SENSOR_MAF,SENSOR_RAIL,SENSOR_FUEL,SENSOR_AFR,SENSOR_TIMING,SENSOR_BATTERY,SENSOR_TORQUE,SENSOR_OBD_REPLIES}};
      printTft(alertText(i),x+12,y+7,2,fg);printTft(cellValue(p,i)+" "+sensorUnit(ids[page][i]),x+12,y+35,2,fg);
    }else {
      const uint8_t ids[2][8]={{SENSOR_SPEED,SENSOR_RPM,SENSOR_LOAD,SENSOR_THROTTLE,SENSOR_COOLANT,SENSOR_OIL,SENSOR_INTAKE,SENSOR_MAP},
                                {SENSOR_MAF,SENSOR_RAIL,SENSOR_FUEL,SENSOR_AFR,SENSOR_TIMING,SENSOR_BATTERY,SENSOR_TORQUE,SENSOR_OBD_REPLIES}};
      printTft(labels[page][i],x+12,y+7,2,settings.text);printTft(cellValue(p,i),x+78,y+31,3,settings.value);printTft(sensorUnit(ids[page][i]),x+190,y+43,1,settings.text);
    }
  }
}

void updateOled(const TelemetryPacket &p){
  if(!oledFound)return;
  char line[24];
  oled.clearBuffer();oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(0,9,"CAN/OBD STATUS");oled.drawHLine(0,11,128);
  if(testMode)snprintf(line,sizeof(line),"MODE: SIMULATION");
  else snprintf(line,sizeof(line),"RADIO:%s %d dBm",radioAlive()?"OK":"LOST",(int)radioRssi);oled.drawStr(0,22,line);
  snprintf(line,sizeof(line),"CAN:%s ECU:%s",(p.status&S_CAN_STARTED)?"ON":"OFF",(p.status&S_ECU_SEEN)?"YES":"NO");oled.drawStr(0,33,line);
  snprintf(line,sizeof(line),"RX:%lu OBD:%lu",(unsigned long)p.canRxFrames,(unsigned long)p.obdReplies);oled.drawStr(0,44,line);
  snprintf(line,sizeof(line),"TXE:%u RXE:%u F:%lu",p.txErrorCounter,p.rxErrorCounter,(unsigned long)p.txFailedCount);oled.drawStr(0,55,line);
  bool alternate=(millis()/2000)%2;
  if(!radioAlive())oled.drawStr(0,64,"E30 RADIO LINK LOST");
  else switch(p.diagnosticCode){
    case D_OK:oled.drawStr(0,64,"SYSTEM: DATA OK");break;
    case D_DETECTING:oled.drawStr(0,64,"E00 DETECTING");break;
    case D_CAN_INIT_FAILED:oled.drawStr(0,64,"E01 CAN START FAIL");break;
    case D_NO_CAN_ACK:oled.drawStr(0,64,alternate?"E03 NO CAN ACK":"CHECK VCC/SLNT/H/L");break;
    case D_ECU_NO_REPLY:oled.drawStr(0,64,alternate?"E04 ECU NO REPLY":"IGN ON; PINS 6/14/5");break;
    case D_BUS_ACTIVE_NO_OBD:oled.drawStr(0,64,alternate?"E05 BUS, NO OBD":"MODE01 NO RESPONSE");break;
    case D_CAN_RX_ERRORS:oled.drawStr(0,64,alternate?"E06 RX ERRORS":"CHECK H/L; TERM OFF");break;
    case D_BUS_OFF:oled.drawStr(0,64,alternate?"E07 CAN BUS OFF":"CHECK WIRING/SLNT");break;
    case D_OBD_DATA_LOST:oled.drawStr(0,64,alternate?"E08 DATA STOPPED":"CHECK PLUG/IGNITION");break;
    case D_29BIT:oled.drawStr(0,64,"E29 29-BIT OBD");break;
    default:oled.drawStr(0,64,"E99 UNKNOWN ERROR");break;
  }
  oled.sendBuffer();
}

uint8_t settingsItemCount(){
  switch(settingsScreen){
    case SETTINGS_HOME:return 11; case SETTINGS_COLORS:return 5;
    case SETTINGS_UNITS:return 5; case SETTINGS_BRIGHTNESS:return 2;
    case SETTINGS_FORMAT:return 2; case SETTINGS_WARNINGS:return 7;
    case SETTINGS_SLOTS:return settings.slotCount+2; case SETTINGS_LOGGING:return SENSOR_COUNT+2;
    case SETTINGS_PROFILES:return 5;
  }
  return 1;
}

void applyBrightness(){ledcWrite(TFT_BL,settings.brightness);}

void applyProfile(uint8_t profile){
  settings.profile=profile;
  settings.warningMask=W_COOLANT|W_OIL|W_INTAKE|W_MAP|W_FUEL|W_VOLTAGE;
  settings.loggingEnabled=1;
  settings.slotCount=6;
  if(profile==PROFILE_DAILY){
    const uint8_t s[6]={SENSOR_COOLANT,SENSOR_OIL,SENSOR_MAP,SENSOR_BATTERY,SENSOR_FUEL,SENSOR_RPM};memcpy(settings.slots,s,6);
    settings.logMask=V_RPM|V_SPEED|V_COOLANT|V_OIL|V_MAP|V_FUEL|V_BATTERY;
  }else if(profile==PROFILE_PERFORMANCE){
    const uint8_t s[6]={SENSOR_RPM,SENSOR_LOAD,SENSOR_THROTTLE,SENSOR_MAP,SENSOR_TIMING,SENSOR_SPEED};memcpy(settings.slots,s,6);
    settings.logMask=V_RPM|V_SPEED|V_LOAD|V_THROTTLE|V_MAP|V_MAF|V_TIMING;
  }else if(profile==PROFILE_TOWING){
    const uint8_t s[6]={SENSOR_COOLANT,SENSOR_OIL,SENSOR_LOAD,SENSOR_BATTERY,SENSOR_MAP,SENSOR_SPEED};memcpy(settings.slots,s,6);
    settings.logMask=V_RPM|V_SPEED|V_LOAD|V_COOLANT|V_OIL|V_MAP|V_BATTERY;
  }else if(profile==PROFILE_DIAGNOSTIC){
    const uint8_t s[6]={SENSOR_RPM,SENSOR_COOLANT,SENSOR_BATTERY,SENSOR_MAP,SENSOR_MAF,SENSOR_OBD_REPLIES};memcpy(settings.slots,s,6);
    settings.logMask=0x7FFF;
  }
}

void drawSettings(){
  if(settingsNeedsFullRedraw){tft->fillScreen(C_BLACK);tft->fillRect(0,0,480,39,C_MID);}
  String title="DASHBOARD SETUP";
  if(settingsScreen==SETTINGS_COLORS)title="SETUP / COLORS";
  else if(settingsScreen==SETTINGS_UNITS)title="SETUP / UNITS";
  else if(settingsScreen==SETTINGS_BRIGHTNESS)title="SETUP / BRIGHTNESS";
  else if(settingsScreen==SETTINGS_FORMAT)title="SETUP / NUMBER FORMAT";
  else if(settingsScreen==SETTINGS_WARNINGS)title="SETUP / WARNINGS";
  else if(settingsScreen==SETTINGS_SLOTS)title="SETUP / SENSOR SLOTS";
  else if(settingsScreen==SETTINGS_LOGGING)title="SETUP / SD LOGGING";
  else if(settingsScreen==SETTINGS_PROFILES)title="SETUP / PROFILES";
  if(settingsNeedsFullRedraw)printTft(title,12,10,2,C_GOLD);
  tft->fillRect(245,8,230,24,C_MID);
  printTft(settingsEditing?"TURN TO CHANGE - PRESS TO SET":"TURN TO MOVE - PRESS TO SELECT",250,14,1,C_WHITE);

  const char *homeItems[]={"DISPLAY COLORS","DISPLAY UNITS","SCREEN BRIGHTNESS","NUMBER FORMATTING","WARNING ENABLE / DISABLE","SENSOR SLOT ASSIGNMENT","SENSOR SD LOGGING","PROFILE PRESETS","SIMULATED TEST MODE","SAVE AND EXIT","CANCEL CHANGES"};
  const char *colorItems[]={"BACKGROUND","TEXT / OUTLINES","VALUES / BARS","WARNING COLOR","BACK"};
  const char *unitItems[]={"TEMPERATURE","VEHICLE SPEED","PRESSURE","AIRFLOW","BACK"};
  const char *warningItems[]={"ENGINE COOLANT","ENGINE OIL","INTAKE TEMPERATURE","MANIFOLD PRESSURE","LOW FUEL","BATTERY VOLTAGE","BACK"};
  const char *profileItems[]={"DAILY DRIVING","PERFORMANCE","TOWING","DIAGNOSTIC","BACK"};
  const uint8_t warningBits[]={W_COOLANT,W_OIL,W_INTAKE,W_MAP,W_FUEL,W_VOLTAGE};
  int rowHeight=40;
  uint8_t count=settingsItemCount();
  uint8_t first=0;if(settingsCursor>=6)first=settingsCursor-5;if(first+6>count)first=count>6?count-6:0;
  for(uint8_t i=first;i<count&&i<first+6;i++){
    int y=47+(i-first)*rowHeight;
    bool selected=i==settingsCursor;
    uint16_t rowBg=selected?C_MID:C_BLACK;
    uint16_t rowFg=selected?C_WHITE:C_LIGHT;
    tft->fillRoundRect(12,y,456,rowHeight-6,8,rowBg);
    String left,right;
    if(settingsScreen==SETTINGS_HOME){left=homeItems[i];if(i==8)right=testMode?"ON":"OFF";}
    else if(settingsScreen==SETTINGS_COLORS){
      left=colorItems[i];
      if(i<4){
        uint16_t values[]={settings.background,settings.text,settings.value,settings.warning};
        right=colorNames[nearestPaletteIndex(values[i])];
        tft->fillRect(334,y+9,25,25,values[i]);tft->drawRect(333,y+8,27,27,C_WHITE);
      }
    }else if(settingsScreen==SETTINGS_UNITS){
      left=unitItems[i];
      if(i==0)right=settings.tempUnit==TEMP_C?"CELSIUS":"FAHRENHEIT";
      else if(i==1)right=settings.speedUnit==SPEED_KMH?"KM/H":"MPH";
      else if(i==2)right=settings.pressureUnit==PRESSURE_KPA?"KPA":settings.pressureUnit==PRESSURE_PSI?"PSI":"BAR";
      else if(i==3)right=settings.airflowUnit==AIRFLOW_G_S?"G/S":"LB/MIN";
    }else if(settingsScreen==SETTINGS_BRIGHTNESS){
      left=i==0?"DISPLAY BRIGHTNESS":"BACK";if(i==0)right=String((settings.brightness*100+127)/255)+"%";
    }else if(settingsScreen==SETTINGS_FORMAT){
      left=i==0?"DECIMAL PLACES":"BACK";
      if(i==0){const char *formats[]={"AUTO","0 DECIMALS","1 DECIMAL","2 DECIMALS"};right=formats[settings.numberFormat];}
    }else if(settingsScreen==SETTINGS_WARNINGS){
      left=warningItems[i];
      if(i<6)right=(settings.warningMask&warningBits[i])?"ON":"OFF";
    }else if(settingsScreen==SETTINGS_SLOTS){
      if(i==0){left="VISIBLE SLOT COUNT";right=String(settings.slotCount);}
      else if(i==settings.slotCount+1)left="BACK";
      else {left=String("SLOT ")+String(i);right=sensorNames[settings.slots[i-1]];}
    }else if(settingsScreen==SETTINGS_LOGGING){
      if(i==0){left="MASTER SD LOGGING";right=settings.loggingEnabled?"ON":"OFF";}
      else if(i==SENSOR_COUNT+1)left="BACK";
      else {uint8_t id=i-1;left=sensorNames[id];uint16_t bit=sensorBit(id);right=(bit&&(settings.logMask&bit))?"ON":"OFF";}
    }else if(settingsScreen==SETTINGS_PROFILES){
      left=profileItems[i];if(i<4)right=settings.profile==i?"ACTIVE":"APPLY";
    }
    printTft(left,25,y+12,2,rowFg);
    if(right.length()){
      tft->setTextSize(1);int16_t x1,y1;uint16_t w,h;tft->getTextBounds(right,0,0,&x1,&y1,&w,&h);
      printTft(right,450-(int)w,y+16,1,(settingsEditing&&selected)?C_GOLD:rowFg);
    }
  }
  printTft("HOLD ENCODER: SAVE + EXIT     MODE BUTTON: BACK",64,305,1,C_GOLD);
  settingsNeedsFullRedraw=false;
}

void enterSettings(){
  settingsOpen=true;settingsEditing=false;settingsScreen=SETTINGS_HOME;settingsCursor=0;
  settingsNeedsFullRedraw=true;
  savedSettings=settings;drawSettings();
}

void closeSettings(bool save){
  if(save)saveSettings();else {settings=savedSettings;applyBrightness();}
  settingsOpen=false;settingsEditing=false;specialScreenShown=false;redraw=true;
}

void settingsBack(){
  if(settingsEditing){settingsEditing=false;drawSettings();return;}
  if(settingsScreen==SETTINGS_HOME){closeSettings(true);return;}
  settingsScreen=SETTINGS_HOME;settingsCursor=0;settingsNeedsFullRedraw=true;drawSettings();
}

void turnSettings(int direction){
  if(settingsEditing){
    if(settingsScreen==SETTINGS_COLORS&&settingsCursor<4){
      uint16_t *targets[]={&settings.background,&settings.text,&settings.value,&settings.warning};
      int index=nearestPaletteIndex(*targets[settingsCursor]);
      index=(index+direction+COLOR_COUNT)%COLOR_COUNT;*targets[settingsCursor]=colorPalette[index];
      settings.profile=PROFILE_CUSTOM;
    }else if(settingsScreen==SETTINGS_UNITS&&settingsCursor<4){
      uint8_t *targets[]={&settings.tempUnit,&settings.speedUnit,&settings.pressureUnit,&settings.airflowUnit};
      const uint8_t counts[]={2,2,3,2};int value=*targets[settingsCursor]+direction;
      if(value<0)value=counts[settingsCursor]-1;if(value>=counts[settingsCursor])value=0;*targets[settingsCursor]=value;
    }else if(settingsScreen==SETTINGS_BRIGHTNESS&&settingsCursor==0){
      int value=settings.brightness+direction*26;settings.brightness=constrain(value,26,255);applyBrightness();
    }else if(settingsScreen==SETTINGS_FORMAT&&settingsCursor==0){
      int value=settings.numberFormat+direction;if(value<0)value=FORMAT_2;if(value>FORMAT_2)value=FORMAT_AUTO;settings.numberFormat=value;
    }else if(settingsScreen==SETTINGS_SLOTS&&settingsCursor==0){
      int count=settings.slotCount;
      if(direction>0)count=count==4?6:count==6?8:4;
      else count=count==8?6:count==6?4:8;
      settings.slotCount=count;settings.profile=PROFILE_CUSTOM;
    }else if(settingsScreen==SETTINGS_SLOTS&&settingsCursor>=1&&settingsCursor<=settings.slotCount){
      int id=settings.slots[settingsCursor-1];id=(id+direction+SENSOR_COUNT)%SENSOR_COUNT;settings.slots[settingsCursor-1]=id;
      settings.profile=PROFILE_CUSTOM;
    }
  }else{
    int count=settingsItemCount();int next=settingsCursor+direction;
    if(next<0)next=count-1;if(next>=count)next=0;settingsCursor=next;
  }
  drawSettings();
}

void pressSettings(){
  if(settingsScreen==SETTINGS_HOME){
    if(settingsCursor<=7){settingsScreen=(SettingsScreen)(SETTINGS_COLORS+settingsCursor);settingsCursor=0;settingsNeedsFullRedraw=true;}
    else if(settingsCursor==8){testMode=!testMode;specialScreenShown=false;redraw=true;}
    else if(settingsCursor==9){closeSettings(true);return;}
    else {closeSettings(false);return;}
  }else if(settingsScreen==SETTINGS_COLORS){
    if(settingsCursor==4){settingsScreen=SETTINGS_HOME;settingsCursor=0;settingsEditing=false;settingsNeedsFullRedraw=true;}
    else settingsEditing=!settingsEditing;
  }else if(settingsScreen==SETTINGS_UNITS){
    if(settingsCursor==4){settingsScreen=SETTINGS_HOME;settingsCursor=0;settingsEditing=false;settingsNeedsFullRedraw=true;}else settingsEditing=!settingsEditing;
  }else if(settingsScreen==SETTINGS_BRIGHTNESS||settingsScreen==SETTINGS_FORMAT){
    if(settingsCursor==1){settingsScreen=SETTINGS_HOME;settingsCursor=0;settingsEditing=false;settingsNeedsFullRedraw=true;}else settingsEditing=!settingsEditing;
  }else if(settingsScreen==SETTINGS_WARNINGS){
    const uint8_t bits[]={W_COOLANT,W_OIL,W_INTAKE,W_MAP,W_FUEL,W_VOLTAGE};
    if(settingsCursor==6){settingsScreen=SETTINGS_HOME;settingsCursor=0;settingsNeedsFullRedraw=true;}
    else settings.warningMask^=bits[settingsCursor];
    if(settingsCursor<6)settings.profile=PROFILE_CUSTOM;
  }else if(settingsScreen==SETTINGS_SLOTS){
    if(settingsCursor==settings.slotCount+1){settingsScreen=SETTINGS_HOME;settingsCursor=0;settingsEditing=false;settingsNeedsFullRedraw=true;}
    else settingsEditing=!settingsEditing;
  }else if(settingsScreen==SETTINGS_LOGGING){
    if(settingsCursor==0)settings.loggingEnabled=!settings.loggingEnabled;
    else if(settingsCursor==SENSOR_COUNT+1){settingsScreen=SETTINGS_HOME;settingsCursor=0;settingsNeedsFullRedraw=true;}
    else {uint16_t bit=sensorBit(settingsCursor-1);if(bit)settings.logMask^=bit;}
  }else if(settingsScreen==SETTINGS_PROFILES){
    if(settingsCursor==4){settingsScreen=SETTINGS_HOME;settingsCursor=0;settingsNeedsFullRedraw=true;}
    else applyProfile(settingsCursor);
  }
  drawSettings();
}

void readControls(){
  static const int8_t transition[16]={0,-1,1,0,1,0,0,-1,-1,0,0,1,0,1,-1,0};
  static bool previousModeReading=HIGH,stableModeState=HIGH;
  static bool previousEncoderReading=HIGH,stableEncoderState=HIGH,longPressHandled=false;
  static uint32_t modeChangedMs=0;
  static uint32_t encoderChangedMs=0,encoderPressedMs=0;
  uint8_t state=(digitalRead(ENCODER_CLK)<<1)|digitalRead(ENCODER_DT);
  encoderMovement+=transition[(encoderPrevious<<2)|state];encoderPrevious=state;
  if(encoderMovement>=4){encoderMovement=0;if(settingsOpen)turnSettings(1);else if(!barMode){page=(page+1)%2;redraw=true;}}
  else if(encoderMovement<=-4){encoderMovement=0;if(settingsOpen)turnSettings(-1);else if(!barMode){page=(page+1)%2;redraw=true;}}

  bool encoderReading=digitalRead(ENCODER_SW);
  if(encoderReading!=previousEncoderReading){previousEncoderReading=encoderReading;encoderChangedMs=millis();}
  if(millis()-encoderChangedMs>=35&&encoderReading!=stableEncoderState){
    stableEncoderState=encoderReading;
    if(stableEncoderState==LOW){encoderPressedMs=millis();longPressHandled=false;}
    else if(!longPressHandled&&millis()-encoderPressedMs<1000){if(settingsOpen)pressSettings();}
  }
  if(stableEncoderState==LOW&&!longPressHandled&&millis()-encoderPressedMs>=1000){
    longPressHandled=true;if(settingsOpen)closeSettings(true);else enterSettings();
  }

  if(!settingsOpen&&digitalRead(RESET_BTN)==LOW){maxSpeed=maxRpm=maxCoolant=maxOil=0;}
  bool modeReading=digitalRead(DISPLAY_MODE_BTN);
  if(modeReading!=previousModeReading){previousModeReading=modeReading;modeChangedMs=millis();}
  if(millis()-modeChangedMs>=40&&modeReading!=stableModeState){
    stableModeState=modeReading;
    if(stableModeState==LOW){
      if(settingsOpen)settingsBack();
      else {barMode=!barMode;page=0;specialScreenShown=false;redraw=true;}
    }
  }
}

void setup(){
  Serial.begin(115200);
  pinMode(ENCODER_CLK,INPUT_PULLUP);pinMode(ENCODER_DT,INPUT_PULLUP);
  pinMode(ENCODER_SW,INPUT_PULLUP);pinMode(RESET_BTN,INPUT_PULLUP);
  pinMode(DISPLAY_MODE_BTN,INPUT_PULLUP);
  encoderPrevious=(digitalRead(ENCODER_CLK)<<1)|digitalRead(ENCODER_DT);
  loadSettings();
  ledcAttach(TFT_BL,5000,8);applyBrightness();
  tft->begin(40000000);tft->setRotation(1);drawCurrentScreen();
  Wire.begin(OLED_SDA,OLED_SCL,100000);delay(250);
  oledFound=initializeReceiverOled();
  WiFi.mode(WIFI_STA);WiFi.setChannel(ESPNOW_CHANNEL);
  if(esp_now_init()!=ESP_OK){Serial.println("FATAL: ESP-NOW failed");while(true)delay(1000);}
  esp_now_register_recv_cb(onReceive);
  esp_now_peer_info_t peer={};
  memcpy(peer.peer_addr,BROADCAST_ADDRESS,6);
  peer.channel=ESPNOW_CHANNEL;peer.encrypt=false;
  if(esp_now_add_peer(&peer)!=ESP_OK&&!esp_now_is_peer_exist(BROADCAST_ADDRESS))
    Serial.println("WARNING: heartbeat broadcast peer failed");
  Serial.println("REAL CAN/OBD DASHBOARD - waiting for transmitter");
  Serial.print("Receiver MAC: ");Serial.println(WiFi.macAddress());
}

void loop(){
  static uint32_t lastOledRetryMs=0;
  if(!oledFound&&millis()-lastOledRetryMs>=5000){lastOledRetryMs=millis();oledFound=initializeReceiverOled();}
  readControls();TelemetryPacket p=testMode?simulatedPacket():snapshot();
  if(p.validMask&V_SPEED)maxSpeed=max(maxSpeed,p.speedMph);
  if(p.validMask&V_RPM)maxRpm=max(maxRpm,p.rpm);
  if(p.validMask&V_COOLANT)maxCoolant=max(maxCoolant,p.coolantF);
  if(p.validMask&V_OIL)maxOil=max(maxOil,p.oilF);
  uint32_t now=millis();
  if(!settingsOpen){
    if(redraw)drawCurrentScreen();
    if(now-lastTftMs>=250){lastTftMs=now;updateTft(p);}
  }
  if(now-lastOledMs>=250){lastOledMs=now;updateOled(p);}
  static uint32_t lastHeartbeatMs=0;
  if(now-lastHeartbeatMs>=500){
    lastHeartbeatMs=now;
    DashboardHeartbeat heartbeat={HEARTBEAT_MAGIC,p.sequence,now,settings.logMask,settings.loggingEnabled,0};
    esp_now_send(BROADCAST_ADDRESS,reinterpret_cast<uint8_t*>(&heartbeat),sizeof(heartbeat));
  }
  delay(2);
}
