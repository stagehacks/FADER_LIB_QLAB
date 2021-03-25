// FADER TRIM SETTINGS
#define TOP 960
#define BOT 70
int faderTrimTop[8] = {TOP, TOP, TOP, TOP, TOP, TOP, TOP, TOP}; // ADJUST THIS IF A SINGLE FADER ISN'T READING 255 AT THE TOP OF ITS TRAVEL
int faderTrimBottom[8] = {BOT, BOT, BOT, BOT, BOT, BOT, BOT, BOT}; // ADJUST THIS IF A SINGLE FADER ISN'T READING 0 AT THE BOTTOM OF ITS TRAVEL

// MOTOR SETTINGS
#define MOTOR_MIN_SPEED 180
#define MOTOR_MAX_SPEED 255
#define TOUCH_THRESHOLD 16

// ETHERNET SETTINGS
byte MAC_ADDRESS[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
int IP_ADDRESS[] = {192, 168, 1, 130};
int QLAB_ADDRESS[] = {192, 168, 1, 120};

#define DEBUG false



#include <NativeEthernetUdp.h>
#include <OSCMessage.h>
#include <OSCBundle.h>

#define HEARTBEAT_INTERVAL 5
#define QLAB_MIN_VOLUME -60
#define QLAB_MAX_VOLUME 12
elapsedMillis sinceHeartbeat = -1000;
EthernetUDP Udp;
uint8_t packetBuffer[UDP_TX_PACKET_MAX_SIZE];
int packetSize = 0;
IPAddress DESTINATION_IP(QLAB_ADDRESS[0], QLAB_ADDRESS[1], QLAB_ADDRESS[2], QLAB_ADDRESS[3]);


void loop() {
  if (getEthernetStatus() != 0) {
    if (sinceHeartbeat > HEARTBEAT_INTERVAL*1000) {
      sinceHeartbeat = 0;
      heartbeat();
    }
    packetSize = Udp.parsePacket();
    OSCMessage oscMsg;
    if (packetSize) {
      for (int j = 0; j < packetSize; j += UDP_TX_PACKET_MAX_SIZE - 1) {
        Udp.read(packetBuffer, UDP_TX_PACKET_MAX_SIZE - 1);
        oscMsg.fill(packetBuffer, UDP_TX_PACKET_MAX_SIZE - 1);
      }
      onOSCMessage(oscMsg);
    }
  }
  faderLoop();
}

void setup() {
  Serial.begin(9600);
  delay(1500);
  faderSetup();
}

void ethernetSetup(){
  Udp.begin(53001);
}

void faderHasMoved(byte i) {
  if (getEthernetStatus() != 0) {
    float faderValueLog = getFaderValueLogarithmic(getFaderValue(i), QLAB_MIN_VOLUME, QLAB_MAX_VOLUME);
    char addr[] = "/cue/selected/level/0/x";
    addr[22] = i + 48;
    OSCMessage msg(addr);
    msg.add((float) faderValueLog);

    Udp.beginPacket(DESTINATION_IP, 53000);
    msg.send(Udp);
    Udp.endPacket();
  }
}


void onOSCMessage(OSCMessage &msg) {
  msg.dispatch("/reply/workspaces", OSCReplyWorkspaces);
  msg.dispatch("/update/workspace/*/cueList/*/playbackPosition", OSCSliderLevels);
  msg.dispatch("/update/workspace/*/cue_id/*", OSCSliderLevels);
  msg.dispatch("/update/workspace/*/dashboard", OSCDashboard);
  msg.dispatch("/reply/cue_id/*/sliderLevels", OSCSliderLevelsReply);
}
elapsedMillis sinceOSCSliderLevelsReply = 0;
void OSCSliderLevelsReply(OSCMessage &msg) { //reply/cue_id/*/sliderLevels
  char param[800]; // this is big because QLab sends floats sometimes for Slider Levels, 20+ char per slider
  msg.getString(0, param, 800);
  String str = String(param);

  int s = str.indexOf("\"data\":[") + 8;
  for (byte i = 0; i < 8; i++) {
    int val = str.substring(s, str.indexOf(",", s)).toInt();
    float mapped = setFaderValueLogarithmic(val, QLAB_MIN_VOLUME, QLAB_MAX_VOLUME);
    setFaderTarget(i, mapped);
    s = str.indexOf(",", s) + 1;
  }
  sinceOSCSliderLevelsReply = 0;
}
void OSCSliderLevels(OSCMessage &msg) { //update/workspace/*/cue_id/*
  if(msg.getType(0)!='s'){
    OSCMessage outMsg("/cue/playbackPosition/sliderLevels");
    Udp.beginPacket(DESTINATION_IP, 53000);
    outMsg.send(Udp);
    Udp.endPacket();
  }else{
    char param[37];
    char str[] = "/cue_id/AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA/sliderLevels";
    msg.getString(0, param, 37);
    for(byte i=0; i<36; i++){
      str[i+8] = param[i];
    }
    OSCMessage outMsg(str);
    Udp.beginPacket(DESTINATION_IP, 53000);
    outMsg.send(Udp);
    Udp.endPacket();
  }
}
void OSCReplyWorkspaces(OSCMessage &msg) { //reply/workspaces
  char param[255];
  msg.getString(0, param, 255);
  String str = String(param);
  Serial.println("thump");

  // ToDo: get updates for all workspaces, not just the first in the list
  int s = str.indexOf("uniqueID\":\"");
  int e = str.indexOf("\",\"", s);
  String workspace = str.substring(s + 11, e);
  String address = "/workspace/" + workspace + "/updates";
  char addr[address.length() + 1];
  address.toCharArray(addr, address.length() + 1);

  OSCMessage outMsg(addr);
  outMsg.add((int32_t) 1);
  Udp.beginPacket(DESTINATION_IP, 53000);
  outMsg.send(Udp);
  Udp.endPacket();
}
void OSCDashboard(OSCMessage &msg){ //update/workspace/*/dashboard/
  // if a dashboard update message arrives without a sliderLevels message recently,
  // we know a cue with no sliderLevels is selected, and should move the faders to 0
  if(sinceOSCSliderLevelsReply>100){
    for(byte i=0; i<8; i++){
      setFaderTarget(i, 0);
    }
  }
}
void heartbeat() {
  OSCMessage msg("/workspaces");
  Udp.beginPacket(DESTINATION_IP, 53000);
  msg.send(Udp);
  Udp.endPacket();
  Serial.print("Thump-");
}

float getFaderValueLogarithmic(int val, int low, int high) {
  return max(low, min(high, map(pow(val / 255.0, 0.6666), 0, 1, low, high)));
}
float setFaderValueLogarithmic(float val, int low, int high) {
  return pow(map(val, -60, 12, 0, 127) / 127.0, 1.5) * 255.0;
}
