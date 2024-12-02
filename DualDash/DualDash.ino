#include <WiFi.h>
#include <esp_now.h>
#include <TFT_eSPI.h>  // Graphics and font library for ST7735 driver chip
#include <SPI.h>

TFT_eSPI tft = TFT_eSPI();  // Invoke library, pins defined in User_Setup.h

String cmd1 = "";
String cmd2 = "";
volatile bool scheduleCmd1Send = false;
volatile bool scheduleCmd2Send = false;

String cmdRecvd = "";
const String waitingCmd = "Wait for cmds";
bool redrawCmdRecvd = false;

int health = 100;  // Local health value for player
int energy = 100;  // Local energy value for player
int attackStrength = 100;  // Local attack strength value for player
int progress = 0;
bool redrawProgress = true;
int lastRedrawTime = 0;

// Button and ESP-NOW related setup
#define BUTTON_LEFT 0
#define BUTTON_RIGHT 35
#define BUTTON_PIN 2 // External Button
#define POTENTIOMETER_PIN 13

void formatMacAddress(const uint8_t *macAddr, char *buffer, int maxLength)
{
  snprintf(buffer, maxLength, "%02x:%02x:%02x:%02x:%02x:%02x", macAddr[0], macAddr[1], macAddr[2], macAddr[3], macAddr[4], macAddr[5]);
}

void receiveCallback(const esp_now_recv_info_t *macAddr, const uint8_t *data, int dataLen)
{
  char buffer[ESP_NOW_MAX_DATA_LEN + 1];
  int msgLen = min(ESP_NOW_MAX_DATA_LEN, dataLen);
  strncpy(buffer, (const char *)data, msgLen);

  buffer[msgLen] = 0;
  String recvd = String(buffer);
  
  char macStr[18];

  if (recvd[0] == 'D') {  // This is a command to reduce health or modify stats
    // If the received command is related to an attack, apply the attack
    if (recvd.substring(3) == "A") {
      // Assume the other player is attacking, reduce their health
      health -= 10;  // Decrease health by 10
      if (health < 0) health = 0;  // Ensure health doesn't go below 0
      redrawControls();  // Redraw the controls to update health
    } else if (recvd.substring(3) == "B") {
      // Handle another attack type
      attackStrength += 1;  // Example logic for another type of attack
      redrawControls();  // Redraw to show updated attack strength
    }
  } else if (recvd[0] == 'P') {  // Update progress
    recvd.remove(0, 3);
    progress = recvd.toInt();
    redrawProgress = true;
  }
}

void sentCallback(const uint8_t *macAddr, esp_now_send_status_t status)
{
  char macStr[18];
  formatMacAddress(macAddr, macStr, 18);
  Serial.print("Last Packet Sent to: ");
  Serial.println(macStr);
  Serial.print("Last Packet Send Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}

void broadcast(const String &message)
{
  uint8_t broadcastAddress[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
  esp_now_peer_info_t peerInfo = {};
  memcpy(&peerInfo.peer_addr, broadcastAddress, 6);
  if (!esp_now_is_peer_exist(broadcastAddress)) {
    esp_now_add_peer(&peerInfo);
  }
  esp_now_send(broadcastAddress, (const uint8_t *)message.c_str(), message.length());
}

void IRAM_ATTR sendCmd1() {
  String cmd1 = "D: A";  // Example: Attack command to the other player
  scheduleCmd1Send = true;
  broadcast(cmd1);  // Send the attack command to the other player
  int potVal = analogRead(POTENTIOMETER_PIN);
  energy -= 1;
  redrawControls(); // Redraw the controls
}

void IRAM_ATTR sendCmd2() {
  String cmd2 = "D: B";  // Another type of attack command
  scheduleCmd2Send = true;
  broadcast(cmd2);  // Send the attack command to the other player
  redrawControls(); // Redraw the controls
}

void buttonSetup() {
  pinMode(BUTTON_LEFT, INPUT);
  pinMode(BUTTON_RIGHT, INPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), sendCmd1, FALLING);
  attachInterrupt(digitalPinToInterrupt(BUTTON_RIGHT), sendCmd2, FALLING);
}

void textSetup() {
  tft.init();
  tft.setRotation(0);
  tft.setTextSize(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  drawControls();
}

void drawControls() {
  tft.drawString("Health: " + String(health), 0, 90, 2);
  tft.drawString("Energy: " + String(energy), 0, 110, 2);
  tft.drawString("Impact: " + String(attackStrength), 0, 130, 2);
}

void redrawControls() {
  tft.fillRect(0, 90, 128, 20, TFT_BLACK);  // Clear the health area
  tft.fillRect(0, 130, 128, 20, TFT_BLACK);  // Clear the impact area
  drawControls();  // Redraw all controls
}

void setup() {
  Serial.begin(115200);
  textSetup();
  buttonSetup();
  espnowSetup();
}

void espnowSetup() {
  delay(500);
  WiFi.mode(WIFI_STA);
  Serial.println("ESP-NOW Broadcast Demo");

  WiFi.disconnect();

  if (esp_now_init() == ESP_OK) {
    Serial.println("ESP-NOW Init Success");
    esp_now_register_recv_cb(receiveCallback);
    esp_now_register_send_cb(sentCallback);
  } else {
    Serial.println("ESP-NOW Init Failed");
    delay(3000);
    ESP.restart();
  }
}

void loop() {
  if (scheduleCmd1Send) {
    broadcast("D: " + cmd1);
    scheduleCmd1Send = false;
  }
  if (scheduleCmd2Send) {
    broadcast("D: " + cmd2);
    scheduleCmd2Send = false;
  }

  if (cmdRecvd == waitingCmd) {
    redrawCmdRecvd = false;
    redrawProgress = false;
  }

  if (redrawCmdRecvd || redrawProgress) {
    redrawCmdRecvd = false;
    redrawProgress = false;
  }
}
