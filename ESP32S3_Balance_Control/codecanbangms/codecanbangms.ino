#include "PID_v1.h"
#include "I2Cdev.h"
#include "MPU6050_6Axis_MotionApps20.h"
#include "Wire.h"

// THÔNG SỐ CÂN BẰNG 
double originalSetpoint = 183.66; //177.3
double setpoint = originalSetpoint;
double movingAngleOffset = 2.0; 
double turnSpeedOffset = 40;    
volatile char command = 'S'; 

double input, output;
double Kp = 40.0;  
double Ki = 150.0; 
double Kd = 1.0;  
PID pid(&input, &output, &setpoint, Kp, Ki, Kd, DIRECT);

const int ENA = 25, IN1 = 26, IN2 = 27, IN3 = 14, IN4 = 12, ENB = 13;
double motorSpeedFactor = 0.8; 
#define MIN_ABS_SPEED 45        

// THÔNG SỐ SIÊU ÂM 
#define TRIG_PIN 4   
#define ECHO_PIN 2
volatile int distance = 999; 

// MPU6050 
MPU6050 mpu;
bool dmpReady = false;  
uint8_t devStatus;      
uint16_t packetSize;    
uint8_t fifoBuffer[64]; 
Quaternion q;           
VectorFloat gravity;    
float ypr[3];           

TaskHandle_t TaskDistance;

void DistanceTaskCode(void * pvParameters) {
  pinMode(TRIG_PIN, OUTPUT); pinMode(ECHO_PIN, INPUT);
  for(;;) {
    digitalWrite(TRIG_PIN, LOW); delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);
    long duration = pulseIn(ECHO_PIN, HIGH, 25000); 
    int tempDist = duration * 0.034 / 2;
    distance = (tempDist > 0 && tempDist < 400) ? tempDist : 999;
    vTaskDelay(100 / portTICK_PERIOD_MS); 
  }
}

//  ĐIỀU KHIỂN ĐỘNG CƠ CÓ DEADBAND
void moveTurn(double speedLeft, double speedRight) {
  // NGUYÊN LÝ 2: Vùng chết (Deadband) - Chống rung khi xe đã cân bằng
  if (abs(speedLeft) < 5) speedLeft = 0;
  if (abs(speedRight) < 5) speedRight = 0;

  // Bánh trái
  if (speedLeft < 0) { digitalWrite(IN1, LOW); digitalWrite(IN2, HIGH); } 
  else if (speedLeft > 0) { digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW); }
  else { digitalWrite(IN1, LOW); digitalWrite(IN2, LOW); } // Phanh
  
  int pwmLeft = 0;
  if (speedLeft != 0) {
      pwmLeft = constrain(abs(speedLeft) * motorSpeedFactor, 0, 255);
      if (pwmLeft < MIN_ABS_SPEED) pwmLeft = MIN_ABS_SPEED;
  }
  ledcWrite(ENA, pwmLeft);

  // Bánh phải
  if (speedRight < 0) { digitalWrite(IN3, LOW); digitalWrite(IN4, HIGH); } 
  else if (speedRight > 0) { digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW); }
  else { digitalWrite(IN3, LOW); digitalWrite(IN4, LOW); } // Phanh
  
  int pwmRight = 0;
  if (speedRight != 0) {
      pwmRight = constrain(abs(speedRight) * motorSpeedFactor, 0, 255);
      if (pwmRight < MIN_ABS_SPEED) pwmRight = MIN_ABS_SPEED;
  }
  ledcWrite(ENB, pwmRight);
}

void setup() {
    Serial.begin(115200); 
    Serial2.begin(115200, SERIAL_8N1, 16, 17);
    
    pinMode(IN1, OUTPUT); pinMode(IN2, OUTPUT); pinMode(IN3, OUTPUT); pinMode(IN4, OUTPUT);
    ledcAttach(ENA, 500, 8); ledcAttach(ENB, 500, 8);
    Wire.begin(21, 22); Wire.setClock(400000);
    
    xTaskCreatePinnedToCore(DistanceTaskCode, "DistTask", 4000, NULL, 1, &TaskDistance, 0);

    mpu.initialize(); 
    devStatus = mpu.dmpInitialize();
    mpu.setXGyroOffset(73); mpu.setYGyroOffset(-50); mpu.setZGyroOffset(41); mpu.setZAccelOffset(805); //-54

    if (devStatus == 0) {
        mpu.setDMPEnabled(true);
        dmpReady = true;
        packetSize = mpu.dmpGetFIFOPacketSize();
        pid.SetMode(AUTOMATIC);
        pid.SetSampleTime(10);
        pid.SetOutputLimits(-255, 255);  
    }
}

void loop() {
    if (!dmpReady) return;

    if (Serial2.available()) {
        char c = Serial2.read();
        if (c == 'F' || c == 'B' || c == 'L' || c == 'R' || c == 'S') {
            command = c;
        }
    }

    if (distance > 0 && distance < 20 && command == 'F') {
        command = 'S'; 
    }

    double targetSetpoint = originalSetpoint;
    double leftTurn = 0, rightTurn = 0;

    // NGUYÊN LÝ 1: Vgo - Cộng offset cố định vào mục tiêu
    switch (command) {
        case 'F': targetSetpoint = originalSetpoint + movingAngleOffset; break;
        case 'B': targetSetpoint = originalSetpoint - movingAngleOffset; break;
        case 'L': leftTurn = turnSpeedOffset; rightTurn = -turnSpeedOffset; break;
        case 'R': leftTurn = -turnSpeedOffset; rightTurn = turnSpeedOffset; break;
        default:  targetSetpoint = originalSetpoint; // Xóa sạch tích lũy Ki khi dừng để không bị trôi
         break;
    }

    // NGUYÊN LÝ LÀM MƯỢT BẰNG LỌC THÔNG THẤP (Low-pass filter)
    
    
    setpoint = (setpoint * 0.95) + (targetSetpoint * 0.05);

    if (mpu.getFIFOCount() >= packetSize) {
        mpu.getFIFOBytes(fifoBuffer, packetSize);
        mpu.dmpGetQuaternion(&q, fifoBuffer);
        mpu.dmpGetGravity(&gravity, &q);
        mpu.dmpGetYawPitchRoll(ypr, &q, &gravity);
        input = ypr[1] * 180/M_PI + 180;
        
        // Ngắt động cơ nếu góc nghiêng quá lớn (Ngã xe)
        if (input < 150 || input > 210) {
            moveTurn(0, 0);
            return;
        }

        pid.Compute();
        
        // NGUYÊN LÝ 3: Trộn tín hiệu Rẽ trực tiếp vào Output
        moveTurn(output + leftTurn, output + rightTurn);
    }
}