#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>

const char* AP_SSID     = "ESP32-Robotics";
const char* AP_PASSWORD = "12345678";

#define IN1 0
#define IN2 1
#define IN3 3
#define IN4 4
#define BUZZER_PIN 5       

#define PWM_FREQ       1000
#define PWM_RESOLUTION    8
#define BUZZER_CHANNEL    4 

WebServer server(80);
int currentSpeed = 200;

// ---------- motor ----------

void setMotors(int a1, int a2, int b1, int b2) {
  ledcWrite(0, a1); ledcWrite(1, a2);
  ledcWrite(2, b1); ledcWrite(3, b2);
}

void motorStop()     { setMotors(0, 0, 0, 0); }
void motorFrente()   { setMotors(currentSpeed, 0, currentSpeed, 0); }
void motorTras()     { setMotors(0, currentSpeed, 0, currentSpeed); }
void motorEsquerda() { setMotors(0, currentSpeed, currentSpeed, 0); }
void motorDireita()  { setMotors(currentSpeed, 0, 0, currentSpeed); }

void executeCommand(const String& action) {
  if      (action == "frente")   motorFrente();
  else if (action == "tras")     motorTras();
  else if (action == "esquerda") motorEsquerda();
  else if (action == "direita")  motorDireita();
  else if (action == "parar")    motorStop();
}

// ---------- buzzer ---------- 

void buzzerTone(int frequency) {
  if (frequency > 0) {
    ledcSetup(BUZZER_CHANNEL, frequency, PWM_RESOLUTION);
    ledcWrite(BUZZER_CHANNEL, 128); // 50% duty cycle = volume máximo
  } else {
    ledcWrite(BUZZER_CHANNEL, 0);   // silêncio
  }
}

void buzzerStop() {
  ledcWrite(BUZZER_CHANNEL, 0);
}

// ---------- HTTP ----------

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
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    sendJSON(400, "{\"error\":\"JSON invalido\"}");
    return;
  }

  JsonArray steps = doc["steps"].as<JsonArray>();
  if (steps.isNull() || steps.size() == 0) {
    sendJSON(400, "{\"error\":\"steps ausente ou vazio\"}");
    return;
  }

  sendJSON(200, "{\"result\":\"ok\",\"steps\":" + String(steps.size()) + "}");
  Serial.println("[SEQ] Iniciando " + String(steps.size()) + " passo(s)");

  for (JsonObject step : steps) {
    String  cmd      = step["command"] | "parar";
    int     duration = step["duration"] | 1;
    int     speed    = step["speed"]    | 200;

    speed    = constrain(map(speed, 0, 100, 0, 255), 0, 255);
    duration = constrain(duration, 0, 60);

    currentSpeed = speed;
    executeCommand(cmd);

    Serial.println("  -> " + cmd + " | " + String(duration) + "s | spd=" + String(speed));

    unsigned long fim = millis() + (unsigned long)duration * 1000UL;
    while (millis() < fim) delay(10);
  }

  motorStop();
  currentSpeed = 200;
  Serial.println("[SEQ] Concluída — motores parados");
}

// toca fila de tons recebida via POST JSON
// Body esperado: { "notes": [{ "frequency": 262, "duration": 1 }, ...] }
void handleMusic() {
  String body = server.arg("plain");

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    sendJSON(400, "{\"error\":\"JSON invalido\"}");
    return;
  }

  JsonArray notes = doc["notes"].as<JsonArray>();
  if (notes.isNull() || notes.size() == 0) {
    sendJSON(400, "{\"error\":\"notes ausente ou vazio\"}");
    return;
  }

  sendJSON(200, "{\"result\":\"ok\",\"notes\":" + String(notes.size()) + "}");
  Serial.println("[MUSIC] Iniciando " + String(notes.size()) + " tom(s)");

  for (JsonObject note : notes) {
    int frequency = note["frequency"] | 0;
    int duration  = note["duration"]  | 1;

    duration = constrain(duration, 0, 60);

    buzzerTone(frequency);
    Serial.println("  -> " + String(frequency) + "Hz | " + String(duration) + "s");

    unsigned long fim = millis() + (unsigned long)duration * 1000UL;
    while (millis() < fim) delay(10);

    buzzerStop();
    delay(50); // pequena pausa entre os tons
  }

  Serial.println("[MUSIC] Concluída");
}

// ---------- setup / loop ----------

void setup() {
  Serial.begin(115200);
  delay(1000);

  int pins[] = {IN1, IN2, IN3, IN4};
  for (int i = 0; i < 4; i++) {
    ledcSetup(i, PWM_FREQ, PWM_RESOLUTION);
    ledcAttachPin(pins[i], i);
  }
  motorStop();

  // inicializa buzzer
  ledcSetup(BUZZER_CHANNEL, PWM_FREQ, PWM_RESOLUTION);
  ledcAttachPin(BUZZER_PIN, BUZZER_CHANNEL);
  buzzerStop();

  WiFi.mode(WIFI_AP);
  bool ok = WiFi.softAP(AP_SSID, AP_PASSWORD);
  delay(500);

  Serial.println("\n=== ESP32 AP ===");
  if (!ok) { Serial.println("FALHOU — reiniciando..."); delay(3000); ESP.restart(); }
  Serial.println("SSID: " + String(AP_SSID));
  Serial.println("IP  : " + WiFi.softAPIP().toString());

  server.on("/status",   HTTP_GET,  handleStatus);
  server.on("/cmd",      HTTP_GET,  handleCommand);
  server.on("/sequence", HTTP_POST, handleSequence);
  server.on("/music",    HTTP_POST, handleMusic);   // NOVO
  server.onNotFound([]() { sendJSON(404, "{\"error\":\"rota nao encontrada\"}"); });
  server.begin();
  Serial.println("HTTP na porta 80\n");
}

void loop() { server.handleClient(); }