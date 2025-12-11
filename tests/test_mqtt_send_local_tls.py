import time
import json
import base64
import requests
import argparse

import paho.mqtt.client as mqtt

BROKER = "localhost"
PORT = 1883
YOLO_API_URL = "https://192.168.60.254/api/anpr"

TOPIC_GATE_ACCESS = "gate/1/access"

USERNAME = "mqtt_iot2"
PASSWORD = "mah-iot2"

LOCAL_IMAGE_PATH = "tests/test_plate_2.jpg"   # Imagen local


def load_local_image():
    """Load image from local filesystem (offline test)."""
    try:
        with open(LOCAL_IMAGE_PATH, "rb") as f:
            return f.read()
    except Exception as e:
        print(f"Error loading local image: {e}")
        return None


def test_yolo_api_local():
    """Test YOLO API using a local image (offline)."""
    try:
        img_bytes = load_local_image()
        if img_bytes is None:
            return None

        files = {
            "image": ("test_plate.jpg", img_bytes, "image/jpeg")
        }

        response = requests.post(YOLO_API_URL, files=files, 
                                verify="/home/mah-iot/certs/yolo.crt",
                                timeout=3)
        response.raise_for_status()

        print(f"YOLO_API_URL: {YOLO_API_URL}")

        result = response.json()
        print("YOLO API Response:")
        print(f"Plate Text: {result.get('plate_text')}")
        print(f"Confidence: {result.get('confidence')}")
        return result

    except requests.exceptions.RequestException as e:
        print(f"Error testing YOLO API: {e}")
        return None


def test_mqtt_local():
    """Test sending a local image through MQTT."""
    client = mqtt.Client()
    client.username_pw_set(USERNAME, PASSWORD)

    try:
        client.connect(BROKER, PORT, 60)
        client.loop_start()

        img_bytes = load_local_image()
        if img_bytes is None:
            return None

        msg = base64.b64encode(img_bytes).decode('utf-8')
        payload = json.dumps({"image": msg})

        client.publish(TOPIC_GATE_ACCESS, payload)
        print("Sent local image through MQTT")

        time.sleep(1)

    except Exception as e:
        print(f"Error in MQTT test: {e}")

    finally:
        client.loop_stop()
        client.disconnect()


def main():
    parser = argparse.ArgumentParser(description="Test ANPR system offline")
    parser.add_argument('--mode', choices=['mqtt', 'api', 'both'], default='both')
    args = parser.parse_args()

    if args.mode in ['api', 'both']:
        print("\n=== Testing YOLO API (LOCAL IMAGE) ===")
        test_yolo_api_local()

    if args.mode in ['mqtt', 'both']:
        print("\n=== Testing MQTT (LOCAL IMAGE) ===")
        test_mqtt_local()


if __name__ == "__main__":
    main()
