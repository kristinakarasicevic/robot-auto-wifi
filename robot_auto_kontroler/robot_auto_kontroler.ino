/*
  ===========================================================
  Robot auto - upravljacki kod za ESP32-S3 (glavni kontroler)
  -----------------------------------------------------------
  NOVO u ovoj verziji:
   * /wifi?ssid=...&pass=...  -> menja WiFi mrezu iz aplikacije
   * mreza/lozinka se cuvaju u trajnoj memoriji (NVS/Preferences)
     pa prezive restart i nestanak struje
   * ako sacuvana mreza ne proradi za 15 s -> auto se sam vrati
     na PODRAZUMEVANU mrezu (da ne ostane zakljucan na pogresnoj)

  Napomena: ENA/ENB imaju jumper (pun gas), nema PWM regulacije
  brzine. ESP32-CAM je ODVOJEN uredjaj na istoj WiFi mrezi.
  ===========================================================
*/

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include "secrets.h"

// ---------- Pinovi ----------
const int L_IN1 = 4;   // leva strana: napred = HIGH
const int L_IN2 = 5;   // leva strana: nazad  = HIGH
const int R_IN1 = 6;   // desna strana: napred = HIGH
const int R_IN2 = 7;   // desna strana: nazad  = HIGH
const int LED_PIN = 21;
const int TRIG = 17;
const int ECHO = 18;   // preko delitelja 1k/2k!

// ---------- Ponasanje ----------
const float STOP_CM = 20.0;
const unsigned long CMD_TIMEOUT_MS  = 5000;
const unsigned long DIST_PERIOD_MS  = 60;
const unsigned long WIFI_CONNECT_MS = 15000;  // koliko cekamo na povezivanje

WebServer server(80);
Preferences prefs;

char  currentDir   = 'S';
float lastDistance = 999.0;
bool  lastBlocked  = false;
unsigned long lastCmdMs  = 0;
unsigned long lastDistMs = 0;

// zakazana promena mreze (obraduje se u loop-u, van HTTP handlera)
bool   wifiChangePending = false;
String pendingSsid, pendingPass;

// trenutno aktivna mreza (za ispis)
String curSsid;

// ---------- Motori ----------
void leftSide(int dir) {
  digitalWrite(L_IN1, dir > 0 ? HIGH : LOW);
  digitalWrite(L_IN2, dir < 0 ? HIGH : LOW);
}
void rightSide(int dir) {
  digitalWrite(R_IN1, dir > 0 ? HIGH : LOW);
  digitalWrite(R_IN2, dir < 0 ? HIGH : LOW);
}
void stopMotors() { leftSide(0); rightSide(0); }

void applyDir(char d) {
  switch (d) {
    case 'F': leftSide(-1); rightSide(-1); break;  // napred (bilo nazad)
    case 'B': leftSide(1);  rightSide(1);  break;  // nazad  (bilo napred)
    case 'L': leftSide(-1); rightSide(1);  break;  // okret levo (u mestu)
    case 'R': leftSide(1);  rightSide(-1); break;  // okret desno (u mestu)
    default:  stopMotors();                break;
  }
}

// ---------- Senzor ----------
float measureDistance() {
  digitalWrite(TRIG, LOW);  delayMicroseconds(2);
  digitalWrite(TRIG, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG, LOW);
  long us = pulseIn(ECHO, HIGH, 25000UL);
  if (us == 0) return 999.0;
  return us / 58.0;
}

// ---------- WiFi ----------
bool connectWiFi(const String& ssid, const String& pass, unsigned long timeoutMs) {
  Serial.print("Povezivanje na \""); Serial.print(ssid); Serial.print("\"");
  WiFi.disconnect(true, true);
  delay(100);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(300); Serial.print(".");
  }
  Serial.println();
  return WiFi.status() == WL_CONNECTED;
}

// ---------- HTTP odgovori ----------
void sendJson() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  String body = "{\"dist\":" + String(lastDistance, 1) +
                ",\"blocked\":" + (lastBlocked ? "true" : "false") +
                ",\"dir\":\"" + String(currentDir) + "\"}";
  server.send(200, "application/json", body);
}

void handleMove() {
  if (server.hasArg("dir")) {
    char d = server.arg("dir").charAt(0);
    if (d=='F'||d=='B'||d=='L'||d=='R'||d=='S') currentDir = d;
  }
  lastCmdMs = millis();
  sendJson();
}

void handleStatus() { sendJson(); }

// NOVO: prijem nove WiFi mreze iz aplikacije
void handleWifi() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  if (!server.hasArg("ssid") || server.arg("ssid").length() == 0) {
    server.send(400, "application/json", "{\"ok\":false,\"err\":\"nedostaje ssid\"}");
    return;
  }
  pendingSsid = server.arg("ssid");
  pendingPass = server.hasArg("pass") ? server.arg("pass") : "";

  // Odgovori PRE prebacivanja (posle ovoga veza pada i auto se restartuje)
  String body = "{\"ok\":true,\"ssid\":\"" + pendingSsid +
                "\",\"msg\":\"Cuvam mrezu i restartujem. Nadji novi IP na novoj mrezi.\"}";
  server.send(200, "application/json", body);

  wifiChangePending = true;   // obradi se u loop-u
}

void handleRoot() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "text/plain",
    "Robot auto kontroler radi. /move?dir=F  /status  /wifi?ssid=..&pass=..");
}

// ---------- Setup / Loop ----------
void setup() {
  Serial.begin(115200);
  int outs[] = {L_IN1, L_IN2, R_IN1, R_IN2, TRIG, LED_PIN};
  for (int p : outs) pinMode(p, OUTPUT);
  pinMode(ECHO, INPUT);
  stopMotors();
  digitalWrite(LED_PIN, LOW);

  // Ucitaj sacuvanu mrezu; ako je nema, koristi podrazumevanu
  prefs.begin("wifi", false);
  curSsid = prefs.getString("ssid", DEF_SSID);
  String pass = prefs.getString("pass", DEF_PASS);

  bool ok = connectWiFi(curSsid, pass, WIFI_CONNECT_MS);
  if (!ok) {
    // Sacuvana mreza ne radi -> vrati se na podrazumevanu
    Serial.println("Sacuvana mreza ne radi -> vracam na podrazumevanu.");
    curSsid = DEF_SSID; pass = DEF_PASS;
    prefs.putString("ssid", DEF_SSID);
    prefs.putString("pass", DEF_PASS);
    ok = connectWiFi(curSsid, pass, WIFI_CONNECT_MS);
  }

  if (ok) {
    Serial.print("Mreza: "); Serial.println(curSsid);
    Serial.print("Auto IP adresa: ");
    Serial.println(WiFi.localIP());   // OVU adresu unosis u aplikaciju
    digitalWrite(LED_PIN, HIGH);      // LED svetli kad je povezan
  } else {
    Serial.println("WiFi: nema veze ni sa podrazumevanom mrezom (proveri hotspot).");
  }

  server.on("/",       handleRoot);
  server.on("/move",   handleMove);
  server.on("/status", handleStatus);
  server.on("/wifi",   handleWifi); 
  server.begin();

  if (MDNS.begin("auto")) {          // ime uređaja
    MDNS.addService("http", "tcp", 80);
    Serial.println("Dostupno na: http://auto.local");
  }
}

void loop() {
  static unsigned long lastIpPrint = 0;
  if (millis() - lastIpPrint > 3000) {
    lastIpPrint = millis();
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("WiFi OK ("); Serial.print(curSsid);
      Serial.print("). Auto IP adresa: ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("WiFi: nije povezan...");
    }
  }

  server.handleClient();

  // Obrada zakazane promene mreze (van HTTP handlera, da odgovor stigne)
  if (wifiChangePending) {
    wifiChangePending = false;
    stopMotors();               // bezbednost pre restarta
    delay(300);                 // da se HTTP odgovor posalje do kraja
    prefs.putString("ssid", pendingSsid);
    prefs.putString("pass", pendingPass);
    Serial.print("Nova mreza sacuvana: "); Serial.println(pendingSsid);
    Serial.println("Restartujem...");
    delay(200);
    ESP.restart();              // na startu ce se povezati na novu mrezu
  }

  if (millis() - lastDistMs >= DIST_PERIOD_MS) {
    lastDistMs = millis();
    lastDistance = measureDistance();
  }
  if (millis() - lastCmdMs > CMD_TIMEOUT_MS) currentDir = 'S';

  char d = currentDir;
  lastBlocked = false;
  if (d == 'F' && lastDistance < STOP_CM) { d = 'S'; lastBlocked = true; }

  applyDir(d);
}
