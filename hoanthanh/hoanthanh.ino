#include <ESP8266WiFi.h>      // Thư viện kết nối WiFi cho ESP8266
#include <PubSubClient.h>     // Thư viện dùng để giao tiếp MQTT
#include <SPI.h>              // Thư viện hỗ trợ giao tiếp SPI (cho RFID)
#include <MFRC522.h>          // Thư viện điều khiển module RFID RC522

// Định nghĩa chân kết nối phần cứng
#define SS_PIN D4         // Chân SDA của RFID nối với D4 (GPIO2)
#define RST_PIN D3        // Chân RST của RFID nối với D3 (GPIO0)
#define BUZZER_PIN D8     // Còi kết nối với chân D8 (GPIO15)
#define GAS_SENSOR A0     // Cảm biến khí gas kết nối chân A0 (analog)

// Thông tin WiFi
const char* ssid = "Vuongphutho 2";         
const char* password = "12345679";       

// Cấu hình địa chỉ MQTT broker
const char* mqtt_server = "192.168.1.10"; 
WiFiClient espClient;
PubSubClient client(espClient);

// Khởi tạo module RFID
MFRC522 rfid(SS_PIN, RST_PIN);

// UID hợp lệ cho phép vào hệ thống
byte validUID[] = {0xA3, 0x4B, 0x08, 0x05}; 

// Đếm số lần quét thẻ sai
int failedAttempts = 0;
const int maxFailedAttempts = 3; // Sau 3 lần sai thì cảnh báo

// Trạng thái điều khiển còi từ MQTT
bool mqttBuzzerState = false;

// Hàm so sánh 2 UID xem có giống nhau không
bool isSameUID(byte *a, byte *b, byte len) {
  for (byte i = 0; i < len; i++) {
    if (a[i] != b[i]) return false;
  }
  return true;
}

// Hàm xử lý khi có tin nhắn MQTT nhận về
void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  Serial.print("📩 Nhận lệnh từ topic ");
  Serial.print(topic);
  Serial.print(": ");
  Serial.println(message);

  // Kiểm tra lệnh điều khiển còi từ MQTT
  if (String(topic) == "esp8266/buzzer/control") {
    if (message == "ON") {
      mqttBuzzerState = true;
      digitalWrite(BUZZER_PIN, HIGH);  // Bật còi
      Serial.println("🔊 Còi được bật từ MQTT");
    } else if (message == "OFF") {
      mqttBuzzerState = false;
      digitalWrite(BUZZER_PIN, LOW);   // Tắt còi
      Serial.println("🔇 Còi được tắt từ MQTT");
    }
  }
}

// Hàm kết nối WiFi
void setup_wifi() {
  delay(10);
  Serial.println("🔌 Đang kết nối WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✅ Đã kết nối WiFi!");
  Serial.print("📡 IP của ESP8266: ");
  Serial.println(WiFi.localIP());
}

// Hàm kết nối lại MQTT nếu bị mất kết nối
void reconnect() {
  while (!client.connected()) {
    Serial.print("🔁 Kết nối MQTT...");
    if (client.connect("ESP8266Client")) {
      Serial.println("✅ Thành công!");
      client.subscribe("esp8266/buzzer/control"); // Lắng nghe topic điều khiển còi
    } else {
      Serial.print("❌ Thất bại, mã lỗi: ");
      Serial.println(client.state());
      delay(3000);
    }
  }
}

// Thiết lập ban đầu
void setup() {
  Serial.begin(115200);       // Mở cổng Serial để debug
  SPI.begin();                // Khởi động giao tiếp SPI
  rfid.PCD_Init();            // Khởi động RFID

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW); // Tắt còi ban đầu

  setup_wifi();                  // Kết nối WiFi
  client.setServer(mqtt_server, 1883);  // Cài địa chỉ MQTT broker
  client.setCallback(callback);        // Cài hàm xử lý tin nhắn đến

  Serial.println("🚀 Hệ thống khởi động!");
}

// Vòng lặp chính
void loop() {
  if (!client.connected()) {
    reconnect(); // Kết nối lại nếu mất MQTT
  }
  client.loop(); // Duy trì kết nối MQTT

  // Nếu MQTT đang yêu cầu tắt còi
  if (!mqttBuzzerState) {
    digitalWrite(BUZZER_PIN, LOW);
  }

  // Đọc giá trị từ cảm biến khí gas
  int gasValue = analogRead(GAS_SENSOR);
  Serial.print("🌫️ Nồng độ khí gas: ");
  Serial.println(gasValue);

  // Gửi dữ liệu khí gas lên MQTT
  char gasStr[8];
  itoa(gasValue, gasStr, 10);
  client.publish("esp8266/sensors/gas", gasStr);

  // Nếu không bị MQTT chi phối, điều khiển còi theo cảm biến khí gas
  if (!mqttBuzzerState) {
    if (gasValue > 160) {
      digitalWrite(BUZZER_PIN, HIGH); // Còi kêu khi khí vượt ngưỡng
    } else {
      digitalWrite(BUZZER_PIN, LOW);
    }
  }

  // Kiểm tra nếu có thẻ RFID được quét
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
    delay(500); // Không có thẻ thì nghỉ 0.5s
    return;
  }

  // Lấy UID từ thẻ
  byte readUID[4];
  for (byte i = 0; i < 4; i++) {
    readUID[i] = rfid.uid.uidByte[i];
  }

  // In UID và gửi lên MQTT
  char uidStr[20];
  sprintf(uidStr, "%02X%02X%02X%02X", readUID[0], readUID[1], readUID[2], readUID[3]);
  Serial.print("🪪 UID quét được: ");
  Serial.println(uidStr);
  client.publish("esp8266/rfid/uid", uidStr);

  // Kiểm tra UID đúng hay sai
  if (isSameUID(readUID, validUID, 4)) {
    Serial.println("✅ Thẻ hợp lệ");
    failedAttempts = 0;
    beepSuccess();
  } else {
    Serial.println("❌ Thẻ không hợp lệ");
    failedAttempts++;
    beepError();
    if (failedAttempts >= maxFailedAttempts) {
      beepAlert();
    }
  }

  rfid.PICC_HaltA(); // Kết thúc phiên làm việc với thẻ
  delay(500);        // Nghỉ tránh quét lặp
}

// Phản hồi còi nếu thẻ đúng
void beepSuccess() {
  if (!mqttBuzzerState) {
    tone(BUZZER_PIN, 1500, 200); // Beep tần số 1500Hz trong 200ms
    delay(200);
  }
}

// Phản hồi còi nếu thẻ sai
void beepError() {
  if (!mqttBuzzerState) {
    for (int i = 0; i < 2; i++) {
      tone(BUZZER_PIN, 1000, 200); // Beep tần số 1000Hz
      delay(300);
    }
  }
}

// Cảnh báo khi sai quá số lần cho phép
void beepAlert() {
  if (!mqttBuzzerState) {
    for (int i = 0; i < 5; i++) {
      tone(BUZZER_PIN, 800, 400); // Còi cảnh báo
      delay(500);
    }
  }
}
