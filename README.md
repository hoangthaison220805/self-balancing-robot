Two-Wheel Self-Balancing Robot with AI Object Detection
Overview

This project presents a two-wheel self-balancing robot based on ESP32-S3 and MPU6050. The robot maintains balance using a PID control algorithm while integrating real-time object detection through an ESP32-CAM and YOLOv11 model.

The system combines embedded control, computer vision, and wireless communication to create an intelligent mobile robotic platform.

Features
Real-time self-balancing using PID control
MPU6050-based angle estimation
Dual DC motor control with encoder feedback
ESP32-S3 firmware running under FreeRTOS
ESP32-CAM video streaming
YOLOv11 object detection and tracking
HTTP communication between vision and control modules
Remote monitoring through a web interface
System Architecture

ESP32-CAM
│
▼
YOLOv11 Detection (PC)
│
HTTP
│
▼
ESP32-S3 Controller
│
▼
PID Balance Control
│
▼
Motor Driver
│
▼
Two-Wheel Robot

Hardware
ESP32-S3
ESP32-CAM
MPU6050 IMU
Dual DC Motors
Motor Driver
Lithium Battery Pack
Custom PCB (Altium Designer)
Software
Arduino IDE
FreeRTOS
Python
OpenCV
Ultralytics YOLOv11
Repository Structure

firmware/
├── esp32s3_balance/

vision/
├── detect.py
├── best.pt
└── requirements.txt

hardware/
├── schematic/
├── pcb/

docs/
└── report/

Results

The robot is capable of maintaining stable balance in real time while performing object detection through the ESP32-CAM. The system demonstrates the integration of embedded control algorithms and AI-based computer vision on a low-cost robotic platform.
