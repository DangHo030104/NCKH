#include <Arduino.h>
#include <LoRa_E32.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>  // Bắt buộc khi dùng MQTT over TLS (port 8883)

// HiveMQ Cloud 
const char *mqtt_server = "49654329d3e64526aefb9dafe5f49166.s1.eu.hivemq.cloud";  // Cluster URL
const int   mqtt_port = 8883;
const char *mqtt_user = "BLACK";
const char *mqtt_password = "Dang030104";

// Init MQTT Client với TLS
WiFiClientSecure secureClient;          // Tạo kết nối TCP có mã hóa TLS.
PubSubClient mqttClient(secureClient);  // Tạo MQTTClient sử dụng kết nối TCP đã mã hóa TLS.

// Khai báo LoRa E32 (TX=16, RX=17, M0=4, M1=5, Not auxPin = -1 )
LoRa_E32 e32ttl100(16, 17, &Serial2, -1, 4, 5, UART_BPS_RATE_9600, SERIAL_8N1);

// WiFi
const char *ssid = "Pho Tro Tret";
const char *password = "Ngoc4795";

float T, H, SM1, SM2;
//int pumpState = 0;

/* Lấy giá trị sau key= */
void getValue(const String &frame, const String &key, float &value)
{
  int pos = frame.indexOf(key); // Tìm vị trí của "KEY="
  if (pos == -1) return; // không tìm thấy key

  pos += key.length();                 // Nhảy con trỏ qua "KEY="
  int end = frame.indexOf(',', pos);   // Tìm vị trí ',' bắt đầu từ pos.
  if (end == -1) end = frame.length(); // key cuối chuỗi k có ',' -> lấy đến hết chuỗi.

  value = frame.substring(pos, end).toFloat(); // Cắt chuỗi con từ pos đến end và chuyển sang float
}

/* Xử lý data frame */
void handleDataFrame(const String &frame)
{
  getValue(frame, "T=", T);
  getValue(frame, "H=", H);
  getValue(frame, "SM1=", SM1);
  getValue(frame, "SM2=", SM2);
  
  // In Log để debug.
  //Serial.println("Temperature: %.2f C | Humidity: %.2f %% | SoilMoisture1: %.2f %% | SoilMoisture2: %.2f %%", T, H, SM1, SM2);
}

// Connect to MQTT Broker
void reconnectMQTT()
{
  while (!mqttClient.connected()) {  // Loop until connected.
    Serial.print("Connecting HiveMQ Cloud...");

    String clientId = "ESP32_Client-";
    clientId += String(random(0xffff), HEX);  // HiveMQ chỉ cho 1 Client/1 ID

    if (mqttClient.connect(clientId.c_str(), mqtt_user, mqtt_password))  // Connect to MQTT Broker có xác thực (username/password) TLS.
    {
      Serial.println("connected");
      mqttClient.subscribe("iot/cmd");  // Subscribe topic CMD.
    }
    else {
      Serial.print("failed, rc=");
      Serial.println(mqttClient.state());
      delay(5000);
    }
  }
}

// Publish dữ liệu lên MQTT Broker
void publishData()
{
  char payload[128];

  snprintf(payload, sizeof(payload),
    "{\"T\":%.2f,\"H\":%.2f,\"SM1\":%.2f,\"SM2\":%.2f}",
    T, H, SM1, SM2);

  mqttClient.publish("iot/data", payload);
}

// Callback khi có Msg (CMD) đến từ topic đã subscribe
void mqttCallback(char* topic, byte* payload, unsigned int length)
{
  String msg;
  for (unsigned int i = 0; i < length; i++)
    msg += (char)payload[i]; // Ghép chuỗi từng byte lưu vào msg.
    // vd: msg = "{"zone":1,"CMD":"ON"}"

  Serial.println("MQTT CMD received: " + msg);

  if (msg.indexOf("\"zone\":1") != -1) 
  {
    if (msg.indexOf("ON") != -1)
    {
      ResponseStatus rs = e32ttl100.sendMessage("<CMD,ZONE=1,IRR=ON>");
      Serial.print("LoRa send status: ");
      Serial.println(rs.getResponseDescription());
      Serial.println("Send: <CMD,ZONE=1,IRR=ON>");
    }
    else if (msg.indexOf("OFF") != -1)
    {
      e32ttl100.sendMessage("<CMD,ZONE=1,IRR=OFF>");
      Serial.println("Send: <CMD,ZONE=1,IRR=OFF>");
    }
  }
  else if (msg.indexOf("\"zone\":2") != -1) 
  {
    if (msg.indexOf("ON") != -1)
    {
      e32ttl100.sendMessage("<CMD,ZONE=2,IRR=ON>");
      Serial.println("Send: <CMD,ZONE=2,IRR=ON>");
    }
    else if (msg.indexOf("OFF") != -1)
    {
      e32ttl100.sendMessage("<CMD,ZONE=2,IRR=OFF>");
      Serial.println("Send: <CMD,ZONE=2,IRR=OFF>");
    }
  }
}

void setup()
{
  Serial.begin(9600);

  // Lora E32
  e32ttl100.begin();
  Serial.println("LoRa Receiver Started");

  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    delay(300);
  }
  Serial.println();
  Serial.println("Wi-Fi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // MQTT Setup
  secureClient.setInsecure(); // Bỏ kiểm tra CA, TLS vẫn mã hóa
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(mqttCallback);  
  Serial.println("ESP32 MQTT Broker Ready!");
}

unsigned long lastMsg = 0;
bool dataUpdated = false; // Cờ báo có dữ liệu mới để publish

void loop()
{
  if (!mqttClient.connected()) 
    reconnectMQTT();

  mqttClient.loop();  // Giữ kết nối MQTT và xử lý callback khi có msg.
 
  if (e32ttl100.available() > 1)
  {
    ResponseContainer rc = e32ttl100.receiveMessage();
    if (rc.data.length() > 0)
    {
      Serial.println("LoRa Rx: " + rc.data);
      handleDataFrame(rc.data);
      dataUpdated = true;  // Đánh dấu có dữ liệu mới.
    }
  }

  if (dataUpdated && millis() - lastMsg >= 2000) // Mỗi 2s publish 1 lần (nếu có dữ liệu mới)
  {
    publishData();
    lastMsg = millis();
    dataUpdated = false;
  }
}
