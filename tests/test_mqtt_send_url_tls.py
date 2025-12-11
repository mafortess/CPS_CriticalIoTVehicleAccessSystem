import time
import json
import base64
import requests
import argparse

import paho.mqtt.client as mqtt

BROKER = "localhost"  # Public test broker
PORT = 1883
YOLO_API_URL = "https://192.168.60.254/api/anpr"

TOPIC_GATE_STATUS = "gate/1/status"
TOPIC_GATE_ACCESS = "gate/1/access"
TOPIC_GATE_SYNC = "gate/1/sync"
TOPIC_SERVER_RESPONSE = "server/response/{gate_id}"

USERNAME = "mqtt_iot2"  # Replace with actual username
PASSWORD = "mah-iot2"  # Replace with actual password

def test_yolo_api(image_url):
    """Test the YOLO API directly"""
    try:
        # Get the image
        image_response = requests.get(image_url)
        image_response.raise_for_status()
        
        # Send to YOLO API
        files = {'image': ('image.jpg', image_response.content, 'image/jpeg')}
        response = requests.post(YOLO_API_URL, files=files, verify="/home/mah-iot/certs/yolo.crt")
        
        response.raise_for_status()
        print(f"YOLO_API_URL: {YOLO_API_URL}")
        # Print results
        result = response.json()
        print("YOLO API Response:")
        print(f"Plate Text: {result.get('plate_text')}")
        print(f"Confidence: {result.get('confidence')}")
        return result
    except requests.exceptions.RequestException as e:
        print(f"Error testing YOLO API: {e}")
        return None

def test_mqtt(image_url):
    """Test sending image through MQTT"""
    client = mqtt.Client()
    client.username_pw_set(USERNAME, PASSWORD)

    try:
        client.connect(BROKER, PORT, 60)
        client.loop_start()
        
        response = requests.get(image_url)
        response.raise_for_status()
        print(f"YOLO_API_URL: {YOLO_API_URL}")

        image_bytes = response.content
        msg = base64.b64encode(image_bytes).decode('utf-8')
        payload = json.dumps({"image": msg})
        client.publish(TOPIC_GATE_ACCESS, payload)
        print(f"Sent image through MQTT")
        time.sleep(1)
    except Exception as e:
        print(f"Error in MQTT test: {e}")
    finally:
        client.loop_stop()
        client.disconnect()

def main():
    parser = argparse.ArgumentParser(description='Test ANPR system')
    parser.add_argument('--mode', choices=['mqtt', 'api', 'both'], default='both',
                      help='Test mode: mqtt, api, or both')
    args = parser.parse_args()

    image_url = "https://external-content.duckduckgo.com/iu/?u=https%3A%2F%2Fwww.articulo14.es%2Fmain-files%2Fuploads%2F2025%2F03%2Fmatricula-espana-162x95.jpg&f=1&nofb=1&ipt=aa1d633da11e464a5c28d0e3f10dba0a3a33f415e7b96c48609bdbafb42841d1"

    if args.mode in ['api', 'both']:
        print("\n=== Testing YOLO API ===")
        test_yolo_api(image_url)

    if args.mode in ['mqtt', 'both']:
        print("\n=== Testing MQTT ===")
        test_mqtt(image_url)

if __name__ == "__main__":
    main()