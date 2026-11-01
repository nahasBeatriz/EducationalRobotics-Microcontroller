#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>

const char* AP_SSID     = "ESP32-Robotics";
const char* AP_PASSWORD = "12345678";

// ── Pinos ──────────────────────────────────────────────
#define IN1     4
#define IN2     5
#define IN3     6
#define IN4     7
#define BUZZER 10
#define LED_R   2
#define LED_G   3
#define LED_B   8

#define PWM_FREQ        1000
#define PWM_RESOLUTION     8   // 0-255

// Canais LEDC (core 2.x exige canal explícito)
// 0=IN1  1=IN2  2=IN3  3=IN4  4=BUZZER
#define CH_IN1  0
#define CH_IN2  1
#define CH_IN3  2
#define CH_IN4  3
#define CH_BUZ  4

WebServer server(80);
int currentSpeed = 200;

// ── Motor ──────────────────────────────────────────────
void setMotors(int a1, int a2, int b1, int b2) {
  ledcWrite(CH_IN1, a1);
  ledcWrite(CH_IN2, a2);
  ledcWrite(CH_IN3, b1);
  ledcWrite(CH_IN4, b2);
}

void motorStop()     { setMotors(0, 0, 0, 0); }
void motorFrente()   { setMotors(currentSpeed, 0, currentSpeed, 0); }
void motorTras()     { setMotors(0, currentSpeed, 0, currentSpeed); }
void motorEsquerda() { setMotors(0, currentSpeed, currentSpeed, 0); }
void motorDireita()  { setMotors(currentSpeed, 0, 0, currentSpeed); }

// ── RGB ────────────────────────────────────────────────
void setRGB(bool r, bool g, bool b) {
  digitalWrite(LED_R, r ? HIGH : LOW);
  digitalWrite(LED_G, g ? HIGH : LOW);
  digitalWrite(LED_B, b ? HIGH : LOW);
}

// ── Comandos ───────────────────────────────────────────
void executeCommand(const String& action) {
  if      (action == "frente")       motorFrente();
  else if (action == "tras")         motorTras();
  else if (action == "esquerda")     motorEsquerda();
  else if (action == "direita")      motorDireita();
  else if (action == "parar")        motorStop();
  else if (action == "led_vermelho") setRGB(true,  false, false);
  else if (action == "led_verde")    setRGB(false, true,  false);
  else if (action == "led_azul")     setRGB(false, false, true);
}

// ── Buzzer ─────────────────────────────────────────────
void playTone(int freq, int durationMs) {
  if (freq <= 0) { delay(durationMs); return; }
  ledcWriteTone(CH_BUZ, freq);
  delay(durationMs);
  ledcWriteTone(CH_BUZ, 0);
}

// ── HTTP helpers ───────────────────────────────────────
void sendJSON(int code, String body) {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(code, "application/json", body);
}

void handleStatus() {
  sendJSON(200, "{\"speed\":" + String(currentSpeed) + "}");
}

void handleCommand() {
  if (!server.hasArg("action")) {
    sendJSON(400, "{\"error\":\"parametro action ausente\"}");
    return;
  }
  if (server.hasArg("speed")) {
    int s = server.arg("speed").toInt();
    if (s >= 0 && s <= 100)
      currentSpeed = map(s, 0, 100, 0, 255);
  }
  String action = server.arg("action");
  executeCommand(action);
  if (action != "frente" && action != "tras" &&
      action != "esquerda" && action != "direita" && action != "parar") {
    sendJSON(400, "{\"error\":\"action desconhecida: " + action + "\"}");
    return;
  }
  Serial.println("[CMD] " + action + " spd=" + String(currentSpeed));
  sendJSON(200, "{\"result\":\"" + action + "\"}");
}

void handleSequence() {
  String body = server.arg("plain");
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    sendJSON(400, "{\"error\":\"JSON invalido\"}");
    return;
  }
  JsonArray steps = doc["steps"].as<JsonArray>();
  if (steps.isNull() || steps.size() == 0) {
    sendJSON(400, "{\"error\":\"steps ausente ou vazio\"}");
    return;
  }
  JsonArray music = doc["music"].as<JsonArray>();
  bool hasMusic = !music.isNull() && music.size() > 0;

  sendJSON(200, "{\"result\":\"ok\",\"steps\":" + String(steps.size()) + "}");
  Serial.println("[SEQ] Iniciando " + String(steps.size()) + " passo(s)");

  for (JsonObject step : steps) {
    String cmd      = step["command"]  | "parar";
    int    duration = step["duration"] | 1;
    int    speed    = step["speed"]    | 200;
    speed    = constrain(map(speed, 0, 100, 0, 255), 0, 255);
    duration = constrain(duration, 0, 60);
    currentSpeed = speed;
    executeCommand(cmd);
    Serial.println("  -> " + cmd + " | " + String(duration) + "s | spd=" + String(speed));
    unsigned long fim = millis() + (unsigned long)duration * 1000UL;
    while (millis() < fim) delay(10);
  }

  motorStop();
  setRGB(false, false, false);
  currentSpeed = 200;
  Serial.println("[SEQ] Movimento concluído");

  if (hasMusic) {
    Serial.println("[MUS] Tocando " + String(music.size()) + " nota(s)");
    for (JsonObject nota : music) {
      int freq     = nota["frequency"] | 0;
      int duration = nota["duration"]  | 1;
      duration = constrain(duration, 0, 60);
      playTone(freq, duration * 1000);
      delay(50);
    }
    Serial.println("[MUS] Concluída");
  }
}

// ── setup / loop ───────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1000);

  // Motor — API core 2.x: canal + pino separados
  int motorPins[] = {IN1, IN2, IN3, IN4};
  for (int i = 0; i < 4; i++) {
    ledcSetup(i, PWM_FREQ, PWM_RESOLUTION);
    ledcAttachPin(motorPins[i], i);
  }
  motorStop();

  // Buzzer
  ledcSetup(CH_BUZ, 2000, PWM_RESOLUTION);
  ledcAttachPin(BUZZER, CH_BUZ);
  ledcWrite(CH_BUZ, 0);

  // RGB
  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);
  setRGB(false, false, false);

  WiFi.mode(WIFI_AP);
  bool ok = WiFi.softAP(AP_SSID, AP_PASSWORD);
  delay(500);

  Serial.println("\n=== ESP32-C3 Mini AP ===");
  if (!ok) { Serial.println("FALHOU — reiniciando..."); delay(3000); ESP.restart(); }
  Serial.println("SSID: " + String(AP_SSID));
  Serial.println("IP  : " + WiFi.softAPIP().toString());

  server.on("/status",   HTTP_GET,  handleStatus);
  server.on("/cmd",      HTTP_GET,  handleCommand);
  server.on("/sequence", HTTP_POST, handleSequence);
  server.onNotFound([]() { sendJSON(404, "{\"error\":\"rota nao encontrada\"}"); });
  server.begin();
  Serial.println("HTTP na porta 80\n");
}

void loop() { server.handleClient(); }