#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <EEPROM.h>
#include <QueueArray.h> // ✅ เพิ่มไลบรารีเก็บ queue ข้อมูล

#define SW_PIN 5
#define BUZZER_PIN 4
#define EEPROM_SIZE 292

int queueNumber = 1;
bool lastButtonState = HIGH;
unsigned long lastPressTime = 0;
bool status = 1;
bool fist_time = 1;
unsigned long previousMillis1 = 0; // ⬅️ วางนอก loop() เพื่อให้จำค่าได้

char ssid[32];
char password[32];
char mqtt_server[64];
int mqtt_port;
char mqtt_topic[64];
char subscribe_topic[64];
char deviceID[16];

WiFiClient espClient;
PubSubClient client(espClient);
LiquidCrystal_I2C lcd(0x27, 16, 2);

QueueArray<String> unsentQueue(10); // 🆕 Queue สำหรับเก็บข้อความที่ยังไม่ได้ส่ง

String inputBuffer;

#define OFFSET_SSID 0
#define OFFSET_PASSWORD 32
#define OFFSET_MQTT_SERVER 64
#define OFFSET_MQTT_PORT 128
#define OFFSET_MQTT_TOPIC 132
#define OFFSET_SUBSCRIBE_TOPIC 196
#define OFFSET_DEVICE_ID 260

void saveConfig()
{
  Serial.println("\n🚀 Saving Config...");
  Serial.print("deviceID = ");
  Serial.println(deviceID);

  EEPROM.put(OFFSET_SSID, ssid);
  EEPROM.put(OFFSET_PASSWORD, password);
  EEPROM.put(OFFSET_MQTT_SERVER, mqtt_server);
  EEPROM.put(OFFSET_MQTT_PORT, mqtt_port);
  EEPROM.put(OFFSET_MQTT_TOPIC, mqtt_topic);
  EEPROM.put(OFFSET_SUBSCRIBE_TOPIC, subscribe_topic);
  EEPROM.put(OFFSET_DEVICE_ID, deviceID);

  if (EEPROM.commit())
  {
    Serial.println("✅ Config saved to EEPROM successfully");
  }
  else
  {
    Serial.println("❌ EEPROM commit failed");
  }
}

void loadConfig()
{
  EEPROM.get(OFFSET_SSID, ssid);
  EEPROM.get(OFFSET_PASSWORD, password);
  EEPROM.get(OFFSET_MQTT_SERVER, mqtt_server);
  EEPROM.get(OFFSET_MQTT_PORT, mqtt_port);
  EEPROM.get(OFFSET_MQTT_TOPIC, mqtt_topic);
  EEPROM.get(OFFSET_SUBSCRIBE_TOPIC, subscribe_topic);
  EEPROM.get(OFFSET_DEVICE_ID, deviceID);

  // Debug ดู raw deviceID
  Serial.print("EEPROM deviceID raw: ");
  for (int i = 0; i < sizeof(deviceID); i++)
  {
    Serial.print((uint8_t)deviceID[i], HEX);
    Serial.print(" ");
  }
  Serial.println();

  // ตรวจสอบความถูกต้อง
  bool invalid = false;

  if (ssid[0] == '\0' || ssid[0] == 0xFF || strlen(ssid) >= sizeof(ssid) - 1)
    invalid = true;
  else if (password[0] == 0xFF || strlen(password) >= sizeof(password) - 1)
    invalid = true; // อนุญาต password ว่าง
  else if (mqtt_server[0] == '\0' || mqtt_server[0] == 0xFF || strlen(mqtt_server) >= sizeof(mqtt_server) - 1)
    invalid = true;
  else if (mqtt_port <= 0 || mqtt_port > 65535)
    invalid = true;
  else if (mqtt_topic[0] == '\0' || mqtt_topic[0] == 0xFF || strlen(mqtt_topic) >= sizeof(mqtt_topic) - 1)
    invalid = true;
  else if (subscribe_topic[0] == '\0' || subscribe_topic[0] == 0xFF || strlen(subscribe_topic) >= sizeof(subscribe_topic) - 1)
    invalid = true;
  else if (deviceID[0] == '\0' || deviceID[0] == 0xFF || strlen(deviceID) >= sizeof(deviceID) - 1)
    invalid = true;

  if (invalid)
  {
    Serial.println("⚠️ Invalid EEPROM config. Using default values.");

    strcpy(ssid, "Tenda_992D40");
    strcpy(password, "");
    strcpy(mqtt_server, "192.168.0.100");
    mqtt_port = 1883;
    strcpy(deviceID, "MC0001");
    snprintf(mqtt_topic, sizeof(mqtt_topic), "%s/Queue/tel", deviceID);
    snprintf(subscribe_topic, sizeof(subscribe_topic), "%s/Queue/number", deviceID);
  }
}

void resendUnsentMessages()
{
  while (!unsentQueue.isEmpty())
  {
    String msg = unsentQueue.pop();
    client.publish(mqtt_topic, msg.c_str());
    Serial.print("📤 Resent message: ");
    Serial.println(msg);
  }
}

void printConfig()
{
  Serial.println("\n📄 Current Config:");
  Serial.print("SSID: ");
  Serial.println(ssid);
  Serial.print("Password: ");
  Serial.println(password);
  Serial.print("MQTT Server: ");
  Serial.println(mqtt_server);
  Serial.print("MQTT Port: ");
  Serial.println(mqtt_port);
  Serial.print("Publish Topic: ");
  Serial.println(mqtt_topic);
  Serial.print("Subscribe Topic: ");
  Serial.println(subscribe_topic);
  Serial.print("Device ID: ");
  Serial.println(deviceID);
}

void showMenu()
{
  Serial.println("==== ESP32 CONFIG MENU ====");
  Serial.print("1. Set WiFi SSID : ");
  Serial.println(ssid);
  Serial.print("2. Set WiFi Password : ");
  Serial.println(password);
  Serial.print("3. Set MQTT Server : ");
  Serial.println(mqtt_server);
  Serial.print("4. Set MQTT Port : ");
  Serial.println(mqtt_port);
  Serial.print("5. Set MQTT Publish Topic : ");
  Serial.println(mqtt_topic);
  Serial.print("6. Set MQTT Subscribe Topic : ");
  Serial.println(subscribe_topic);
  Serial.print("9. Set Device ID : ");
  Serial.println(deviceID);
  Serial.println("7. Show Current Config");
  Serial.println("8. Save Config");
  Serial.println("0. Exit Menu");
  Serial.print("Choose: ");
}

void handleCommand(String cmd)
{
  int choice = cmd.toInt();
  switch (choice)
  {
  case 1:
    Serial.print("Enter SSID: ");
    while (!Serial.available())
      ;
    memset(ssid, 0, sizeof(ssid));
    Serial.readBytesUntil('\n', ssid, sizeof(ssid) - 1);
    ssid[strcspn(ssid, "\r\n")] = '\0'; // ลบ \r\n ที่อาจติดมา
    break;

  case 2:
    Serial.print("Enter Password (leave blank if none): ");
    while (!Serial.available())
      ;
    memset(password, 0, sizeof(password));
    Serial.readBytesUntil('\n', password, sizeof(password) - 1);
    password[strcspn(password, "\r\n")] = '\0'; // ลบ \r\n ที่อาจติดมา
    break;

  case 3:
    Serial.print("Enter MQTT Server: ");
    while (!Serial.available())
      ;
    memset(mqtt_server, 0, sizeof(mqtt_server));
    Serial.readBytesUntil('\n', mqtt_server, sizeof(mqtt_server));
    mqtt_server[sizeof(mqtt_server) - 1] = '\0';
    break;
  case 4:
    Serial.print("Enter MQTT Port: ");
    while (!Serial.available())
      ;
    mqtt_port = Serial.parseInt();
    Serial.read();
    break;
  case 5:
    Serial.print("Enter Publish Topic (e.g., queue-trc/number): ");
    while (!Serial.available())
      ;
    {
      char userInput[48];
      memset(userInput, 0, sizeof(userInput));
      Serial.readBytesUntil('\n', userInput, sizeof(userInput) - 1);
      snprintf(mqtt_topic, sizeof(mqtt_topic), "%s/%s", deviceID, userInput);
    }
    break;
  case 6:
    Serial.print("Enter Subscribe Topic (e.g., queue-trc/call): ");
    while (!Serial.available())
      ;
    {
      char userInput[48];
      memset(userInput, 0, sizeof(userInput));
      Serial.readBytesUntil('\n', userInput, sizeof(userInput) - 1);
      snprintf(subscribe_topic, sizeof(subscribe_topic), "%s/%s", deviceID, userInput);
    }
    break;
  case 9:
    Serial.print("Enter Device ID: ");
    while (!Serial.available())
      ;
    memset(deviceID, 0, sizeof(deviceID)); // 🔹 เพิ่ม memset ก่อนรับค่าใหม่
    Serial.readBytesUntil('\n', deviceID, sizeof(deviceID));
    deviceID[sizeof(deviceID) - 1] = '\0';

    {
      // สร้าง topic ใหม่จาก deviceID และ path เดิม
      char mqtt_topic_path[32];
      char subscribe_topic_path[32];

      memset(mqtt_topic_path, 0, sizeof(mqtt_topic_path));           // 🔹 ปลอดภัย
      memset(subscribe_topic_path, 0, sizeof(subscribe_topic_path)); // 🔹 ปลอดภัย

      // แยก path จาก mqtt_topic/sub_topic เดิม (หลัง '/')
      char *pubPath = strchr(mqtt_topic, '/');
      char *subPath = strchr(subscribe_topic, '/');

      if (pubPath)
      {
        strncpy(mqtt_topic_path, pubPath + 1, sizeof(mqtt_topic_path) - 1);
        snprintf(mqtt_topic, sizeof(mqtt_topic), "%s/%s", deviceID, mqtt_topic_path);
      }

      if (subPath)
      {
        strncpy(subscribe_topic_path, subPath + 1, sizeof(subscribe_topic_path) - 1);
        snprintf(subscribe_topic, sizeof(subscribe_topic), "%s/%s", deviceID, subscribe_topic_path);
      }
    }
    saveConfig();

    break;

  case 7:
    printConfig();
    break;
  case 8:
    saveConfig();
    break;
  case 0:
    Serial.println("🔚 Exit menu.");
    break;
  default:
    Serial.println("❌ Invalid choice");
  }
  delay(100);
  showMenu();
}

void callback(char *topic, byte *payload, unsigned int length)
{
  String message;
  for (unsigned int i = 0; i < length; i++)
  {
    message += (char)payload[i];
  }
  queueNumber = message.toInt();

  Serial.print("📥 Received [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(message);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Queue No:");
  lcd.setCursor(10, 0);
  lcd.print(message);
  tone(BUZZER_PIN, 2000, 200);
  delay(5000);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Ready to Queue!!");
  lcd.setCursor(0, 1);
  lcd.print("Current");
  lcd.setCursor(8, 1);
  lcd.print(message);

  status = 1;
}

void setup_wifi()
{
  Serial.print("Connecting to WiFi...");

  if (strlen(password) == 0 || password[0] == '\0')
  {
    WiFi.begin(ssid); // ✅ ไม่มีรหัสผ่าน
  }
  else
  {
    WiFi.begin(ssid, password); // ✅ มีรหัสผ่าน
  }

  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Connect to WiFi");
    Serial.print(".");

    if (millis() - startAttemptTime > 60000)
    {
      Serial.println("\n❌ WiFi connection timeout. Please re-enter SSID and Password.");

      Serial.print("Enter SSID: ");
      while (!Serial.available())
        ;
      memset(ssid, 0, sizeof(ssid));
      Serial.readBytesUntil('\n', ssid, sizeof(ssid) - 1);
      ssid[strcspn(ssid, "\r\n")] = '\0'; // ลบ \r หรือ \n ที่ติดมา

      Serial.print("Enter Password (leave blank if none): ");
      while (!Serial.available())
        ;
      memset(password, 0, sizeof(password));
      Serial.readBytesUntil('\n', password, sizeof(password) - 1);
      password[strcspn(password, "\r\n")] = '\0'; // ลบ \r หรือ \n ที่ติดมา

      printConfig();
      saveConfig();
      ESP.restart();
    }
  }

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WiFi Connected!");
  Serial.println(WiFi.localIP());
  Serial.println(" Connected!");
}

void reconnect()
{
  unsigned long mqttStartTime = millis();
  int secondsPassed = 0;

  while (!client.connected())
  {
    if (millis() - mqttStartTime > 30000)
    {
      Serial.println("\n❌ MQTT connection timeout. Please re-enter MQTT Server and Port.");

      // ขอข้อมูลใหม่จาก Serial
      Serial.print("Enter MQTT Server: ");
      while (!Serial.available())
        ;
      memset(mqtt_server, 0, sizeof(mqtt_server));
      Serial.readBytesUntil('\n', mqtt_server, sizeof(mqtt_server) - 1);
      mqtt_server[strcspn(mqtt_server, "\r\n")] = '\0'; // ลบ \r\n ที่อาจติดมา

      Serial.print("Enter MQTT Port: ");
      while (!Serial.available())
        ;
      mqtt_port = Serial.parseInt();
      Serial.read(); // อ่าน \n ทิ้ง

      // บันทึกและเชื่อมต่อใหม่
      saveConfig();
      mqttStartTime = millis(); // รีเซ็ตเวลานับใหม่
      continue;                 // กลับไปพยายามเชื่อมต่อใหม่
    }

    Serial.print("Connecting to MQTT...");
    client.setServer(mqtt_server, mqtt_port);

    if (client.connect(deviceID))
    {
      Serial.println("connected");
      client.subscribe(subscribe_topic);
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("W");
      lcd.setCursor(1, 0);
      lcd.print(WiFi.localIP());
      lcd.setCursor(0, 1);
      lcd.print("M");
      lcd.setCursor(1, 1);
      lcd.print(mqtt_server);
      delay(3000);
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Ready to queue");
      lcd.setCursor(0, 1);
      lcd.print(deviceID);
    }
    else
    {
      Serial.print("failed, rc=");
      Serial.println(client.state());
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("MQTT Failed!");
      delay(1000);
      lcd.setCursor(0, 1);
      lcd.print("MQTT Timeout: ");
      lcd.setCursor(13, 1);
      lcd.print(secondsPassed);
      lcd.print("s ");
      secondsPassed++;
    }
  }
}

void setup()
{
  uint8_t mac[6];
  WiFi.macAddress(mac);
  Serial.begin(115200);
  EEPROM.begin(EEPROM_SIZE);
  pinMode(SW_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);

  loadConfig();
  printConfig();
  showMenu();

  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Ready to Queue!!");
  lcd.setCursor(0, 1);
  lcd.print(deviceID);
  delay(2000);

  setup_wifi();
  client.setCallback(callback);
}

void loop()
{
  if (Serial.available())
  {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    handleCommand(cmd);
  }

  if (!client.connected())
    reconnect();
  client.loop();

  if (client.connected())
  {
    resendUnsentMessages(); // ✅ หาก MQTT กลับมาให้ส่งใหม่
  }

  bool buttonState = digitalRead(SW_PIN);
  if (lastButtonState == HIGH && buttonState == LOW && millis() - lastPressTime > 500 && status == 1)
  {
    lastPressTime = millis();

    char buffer[64];
    sprintf(buffer, "%s", deviceID);
    Serial.print("publish --> ");
    Serial.print(mqtt_topic);
    Serial.println(buffer);

    if (client.connected())
    {
      client.publish(mqtt_topic, buffer);
    }
    else
    {
      unsentQueue.push(String(buffer)); // ✅ ถ้ายังไม่ได้เชื่อมต่อ เก็บไว้ก่อน
      Serial.println("⚠️ MQTT Offline, saved message.");
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("MQTT Offline, saved message.");
    }

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Wait....");

    tone(BUZZER_PIN, 1000, 200);
    delay(1000);

    status = 0;
    previousMillis1 = millis(); // ⬅️ เริ่มนับใหม่ตรงนี้!
  }
  lastButtonState = buttonState;

  if (status == 0)
  {
    unsigned long currentMillis1 = millis();
    const long interval1 = 10000; // 10 วินาที

    if (currentMillis1 - previousMillis1 >= interval1)
    {
      status = 1;

      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Server no callback");
      delay(2000);
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Ready to queue");
      lcd.setCursor(0, 1);
      lcd.print(deviceID);

      Serial.println("Server no callback");
    }
  }
}
