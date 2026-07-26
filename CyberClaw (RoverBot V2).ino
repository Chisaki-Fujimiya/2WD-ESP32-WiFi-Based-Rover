#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>

Adafruit_PWMServoDriver pca = Adafruit_PWMServoDriver();

//pinouts
#define IN1 32
#define IN2 33
#define IN3 26
#define IN4 25
#define ENA 19
#define ENB 18

//acceleration
int targetL = 0,targetR = 0;
int currentL = 0,currentR = 0;

//robotic arm initial angle
int grip = 90;
int joint1 = 90;
int currentGrip = 90;
int currentJoint1 = 90;

//acceleration rate
const int servoStep = 1;
const int servoDelay = 20;  
const int accel=5;

unsigned long lastServoUpdate = 0;

WebServer server(80);
WebSocketsServer webSocket(81);

const char* html=R"rawliteral(
<!DOCTYPE html>
<html>
  <head>
  <meta name='viewport' content='width=device-width,initial-scale=1,user-scalable=no'>
    <style>
      body{
        margin:0;
        background:#111;
        color:white;
        font-family:Arial, sans-serif;

        display:flex;
        flex-direction:column;
        justify-content:center;
        align-items:center;
        gap: 100px;
        width:100vw;
        height:100vh;
      }

    .control{
        display:flex;
        flex-direction:row;
        align-items:center;
        gap:15px;

        width:95vw;
        margin:12px 0;
    }

    .label{
        width:55px;
        height:55px;

        display:flex;
        justify-content:center;
        align-items:center;

        font-weight:bold;
        color:white;

        writing-mode:vertical-rl;
        text-orientation:mixed;
    }

    .s{
        -webkit-appearance:none;
        appearance:none;

        width:100%;
        height:55px;

        background:transparent;
        margin:0;
    }

    .s::-webkit-slider-runnable-track{
        height:55px;
        background:#4a4a4a;
        border-radius:12px;
    }

    .s::-webkit-slider-thumb{
        -webkit-appearance:none;

        width:55px;
        height:55px;
        border-radius:50%;

        background:#10e8c7;
        border:3px solid #00ffaa;

        margin-top:0;
    }

    #v{
        margin-top:20px;
        font-size:20px;
    }

    </style>
  </head>
  <body>

  <div class="control">
      <div class="label">LEFT</div>
      <input type="range" min="-255" max="255" value="0" id="L" class="s">
  </div>

  <div class="control">
      <div class="label">ARM</div>
      <input type="range" min="0" max="180" value="90" id="J1" class="s">
  </div>

  <div class="control">
      <div class="label">GRIPPER</div>
      <input type="range" min="0" max="180" value="90" id="G" class="s">
  </div>

  <div class="control">
      <div class="label">RIGHT</div>
      <input type="range" min="-255" max="255" value="0" id="R" class="s">
  </div>

  <div id='v'>L:0 | R:0 | G:90 | J1:0</div>

  <script>
  let ws;
  let L = document.getElementById("L");
  let R = document.getElementById("R");
  let G = document.getElementById("G");
  let J1 = document.getElementById("J1");
  let V = document.getElementById("v");

  function connect(){
    ws=new WebSocket("ws://"+location.hostname+":81");
    ws.onopen=function(){
      V.innerHTML="CONNECTED";
    };
    ws.onclose=function(){
      V.innerHTML="DISCONNECTED";
      setTimeout(connect,1000);
    };
  }

  connect();

  function send(){
    V.innerHTML =
    "L:" + L.value +
    " | R:" + R.value +
    " | G:" + G.value +
    " | J1:" + J1.value;

    if(ws && ws.readyState===1){
      ws.send(
        L.value + "," +
        R.value + "," +
        G.value + "," +
        J1.value
      );
    }
  }

  function reset(e){
    e.target.value=0;
    send();
  }

  L.oninput = send;
  R.oninput = send;
  G.oninput = send;
  J1.oninput = send;

  L.addEventListener("mouseup",reset);
  R.addEventListener("mouseup",reset);
  L.addEventListener("touchend",reset);
  R.addEventListener("touchend",reset);

  </script>
  </body>
</html>
)rawliteral";

void wsEvent(uint8_t n, WStype_t t, uint8_t *p, size_t len){
  if(t != WStype_TEXT) return;
  String s = (char*)p;

  int c1 = s.indexOf(',');
  int c2 = s.indexOf(',', c1 + 1);
  int c3 = s.indexOf(',', c2 + 1);

  targetL = s.substring(0, c1).toInt();
  targetR = s.substring(c1 + 1, c2).toInt();
  grip = constrain(s.substring(c2 + 1, c3).toInt(), 60, 145);
  joint1 = constrain(s.substring(c3 + 1).toInt(), 45, 145);

  Serial.printf("L:%d R:%d G:%d J1:%d\n", targetL, targetR, grip, joint1);
}

void setMotor(int spd,int a,int b,int en){
  spd = constrain(spd,-255,255);
  int pwm = abs(spd);

  if(spd > 0){
    digitalWrite(a,LOW);
    digitalWrite(b,HIGH);
    ledcWrite(en,pwm);
  }
  else if(spd < 0){
    digitalWrite(a,HIGH);
    digitalWrite(b,LOW);
    ledcWrite(en,pwm);
  }
  else{
    digitalWrite(a,LOW);
    digitalWrite(b,LOW);
    ledcWrite(en,0);  
  }
}

void writeServo(uint8_t ch, int angle){
  angle = constrain(angle, 0, 180);
  int pulse = map(angle, 0, 180, 100, 500);
  pca.setPWM(ch, 0, pulse);
}

void setup(){
  Serial.begin(115200);
  Wire.begin(16, 17);

  pca.begin();
  pca.setPWMFreq(50);
  writeServo(0, grip);
  writeServo(1, joint1);

  pinMode(IN1,OUTPUT);
  pinMode(IN2,OUTPUT);
  pinMode(IN3,OUTPUT);
  pinMode(IN4,OUTPUT);

  ledcAttach(ENA,1000,8);
  ledcAttach(ENB,1000,8);

  WiFi.mode(WIFI_AP);
  WiFi.softAP("CyberClaw","Mahiru1");

  server.on("/",[](){
    String p=html;
    p.replace("{{IP}}",WiFi.softAPIP().toString());
    server.send(200,"text/html",p);
  });

  server.begin();
  webSocket.begin();
  webSocket.onEvent(wsEvent);
}

int smoothServo(int current, int target){
  if(current < target)
    current += servoStep;
  else if(current > target)
    current -= servoStep;
  if(abs(current-target) < servoStep)
    current = target;
  return current;
}

void loop(){
  webSocket.loop();
  server.handleClient();

  if(currentL < targetL)
    currentL += accel;
  else if(currentL > targetL)
    currentL -= accel;
  if(currentR < targetR)
    currentR += accel;
  else if(currentR > targetR)
    currentR -= accel;

  if(abs(currentL - targetL) < accel)
    currentL = targetL;
  if(abs(currentR - targetR) < accel)
    currentR = targetR;

  setMotor(currentL, IN1, IN2, ENA);
  setMotor(currentR, IN3, IN4, ENB);

  if(millis() - lastServoUpdate >= servoDelay){
    lastServoUpdate = millis();
    currentGrip = smoothServo(currentGrip, grip);
    currentJoint1 = smoothServo(currentJoint1, joint1);

    writeServo(0, currentJoint1);
    writeServo(1, currentGrip);
    writeServo(2, 180 - currentGrip);
  }
}
