import cv2
import requests
import threading
import urllib.request
import numpy as np
import time
from ultralytics import YOLO

print("Đang khởi tạo mô hình YOLOv11 (best.pt)...")
model = YOLO("best.pt")

ESP32_IP = "192.168.4.1"
STREAM_URL = f"http://{ESP32_IP}:81/stream"
CONTROL_URL = f"http://{ESP32_IP}/"

current_cmd = "stop"

latest_frame = None
stream_running = True


def send_command_async(cmd):
    """Gửi lệnh tới ESP32 chạy ngầm"""
    global current_cmd
    if cmd != current_cmd:
        current_cmd = cmd
        url = f"{CONTROL_URL}{cmd}"

        def fetch():
            try:
                requests.get(url, timeout=1.0)
            except Exception:
                pass

        threading.Thread(target=fetch, daemon=True).start()
        print(f"Đã gửi lệnh: {cmd.upper()}")


def capture_stream():
    global latest_frame, stream_running

    while stream_running:
        try:
            stream = urllib.request.urlopen(STREAM_URL, timeout=5)
            bytes_data = bytes()
            print("\nĐã kết nối luồng hình ảnh. Bắt đầu thu thập khung hình...")

            while stream_running:
                bytes_data += stream.read(4096)

                a = bytes_data.find(b'\xff\xd8')
                b = bytes_data.find(b'\xff\xd9')

                if a != -1 and b != -1:
                    if a < b:
                        jpg = bytes_data[a:b + 2]

                        bytes_data = bytes_data[b + 2:]
                        if bytes_data.count(b'\xff\xd8') > 1:
                            last_start = bytes_data.rfind(b'\xff\xd8')
                            bytes_data = bytes_data[last_start:]

                        if len(jpg) > 0:
                            img_np = np.frombuffer(jpg, dtype=np.uint8)
                            frame = cv2.imdecode(img_np, cv2.IMREAD_COLOR)
                            if frame is not None:
                                latest_frame = frame
                    else:
                        bytes_data = bytes_data[a:]
        except Exception as e:
            if stream_running:
                print(f"[CẢNH BÁO] Mất kết nối stream, đang thử lại... ({e})")
                time.sleep(1)


def main():
    global latest_frame, stream_running

    print("Khởi động hệ thống Tự động / Thủ công...")

    camera_thread = threading.Thread(target=capture_stream, daemon=True)
    camera_thread.start()

    while latest_frame is None:
        time.sleep(0.1)

    print("Hệ thống đã sẵn sàng!")

    while True:
        if latest_frame is None:
            continue

        frame = latest_frame.copy()

        frame = cv2.resize(frame, None, fx=2.0, fy=2.0, interpolation=cv2.INTER_LINEAR)

        height, width, _ = frame.shape
        zone_w = width // 3

        results = model.predict(frame, verbose=False, conf=0.45)
        annotated_frame = results[0].plot()

        cv2.line(annotated_frame, (zone_w, 0), (zone_w, height), (0, 255, 255), 2)
        cv2.line(annotated_frame, (zone_w * 2, 0), (zone_w * 2, height), (0, 255, 255), 2)

        cv2.putText(annotated_frame, "PHAI", (10, 30), cv2.FONT_HERSHEY_SIMPLEX, 1, (0, 255, 255), 2)
        cv2.putText(annotated_frame, "TIEN", (zone_w + 10, 30), cv2.FONT_HERSHEY_SIMPLEX, 1, (0, 255, 255), 2)
        cv2.putText(annotated_frame, "TRAI", (zone_w * 2 + 10, 30), cv2.FONT_HERSHEY_SIMPLEX, 1, (0, 255, 255), 2)

        auto_cmd = None
        boxes = results[0].boxes.xyxy.cpu().numpy()

        if len(boxes) > 0:
            x1, y1, x2, y2 = boxes[0]
            center_x = (x1 + x2) / 2

            cv2.circle(annotated_frame, (int(center_x), int((y1 + y2) / 2)), 5, (0, 0, 255), -1)

            if center_x < zone_w:
                auto_cmd = 'right'
            elif center_x < zone_w * 2:
                auto_cmd = 'go'
            else:
                auto_cmd = 'left'

        cv2.imshow('AI Tracking & Manual Control', annotated_frame)

        key = cv2.waitKey(1) & 0xFF

        if key != 255:
            if key == ord('q'):
                send_command_async('stop')
                stream_running = False
                break
            elif key == ord('w'):
                send_command_async('go')
            elif key == ord('s'):
                send_command_async('back')
            elif key == ord('a'):
                send_command_async('left')
            elif key == ord('d'):
                send_command_async('right')
        else:
            if auto_cmd is not None:
                send_command_async(auto_cmd)
            else:
                send_command_async('stop')

    cv2.destroyAllWindows()


if __name__ == '__main__':
    main()