import paho.mqtt.client as mqtt
import json
from datetime import datetime, timezone
import base64
import os
from dotenv import load_dotenv
import requests

from .database.models import Gate, Vehicle, AccessLog
from .database.bigquery_db import BigQueryDB
from . import db
from .database.sqlite_db import SQLiteDB

# Cargar variables de entorno
load_dotenv()

# Initialize SQLite database
sqlite = SQLiteDB(os.getenv('LOCAL_DATABASE_URL'))

# MQTT Topics
TOPIC_GATE_STATUS = "gate/+/status"
TOPIC_GATE_ACCESS = "gate/+/access"
TOPIC_GATE_SYNC = "gate/+/sync"
TOPIC_SERVER_RESPONSE = "server/response/{gate_id}"

TOPIC_GATE_SYNC_REQUEST = "gate/+/sync/request"
TOPIC_GATE_SYNC_RESPONSE = "gate/{gate_id}/sync/response"
TOPIC_GATE_SYNC_LOGS = "gate/+/sync/logs"
TOPIC_GATE_SYNC_LOGS_ACK = "gate/{gate_id}/sync/logs/ack"

# YOLO API Configuration
YOLO_API_URL = os.getenv('YOLO_API_URL')

# Global MQTT client
mqtt_client = None

def process_image_with_yolo(image_data, url=None):
    """
    Process an image using the YOLO API service
    
    Args:
        image_data (bytes): Raw image data in bytes
        
    Returns:
        tuple: (plate_text, confidence) from the ANPR service
    """
    try:
        if url is None:
            # Prepare the image file for the request
            files = {'image': ('image.jpg', image_data, 'image/jpeg')}
            
            # Make request to YOLO API
            response = requests.post(f"{YOLO_API_URL}/api/anpr", files=files,verify="/home/mah-iot/certs/yolo.crt")
        else:
            # Make request to YOLO API
            print(f"Processing image from URL: {url}")
            response = requests.get(f"{YOLO_API_URL}/api/anpr/url", params={'image': url}, verify="/home/mah-iot/certs/yolo.crt")
        
        if response.status_code == 200:
            result = response.json()
            print(f"YOLO API Response: {result}")
            return result['plate_text'], result['confidence']
        else:
            print(f"Error from YOLO API: {response.text}")
            return 'UNKNOWN', 0.0
               
    except Exception as e:
        print(f"Error processing image with YOLO API: {e}")
        return 'UNKNOWN', 0.0

def init_mqtt(app):
    """Initialize MQTT with application context"""
    global mqtt_client
    
    # Check if we're in the reloader process
    if os.environ.get('WERKZEUG_RUN_MAIN') != 'true':
        return
    
    # Get MQTT configuration from environment
    broker_url = os.getenv('MQTT_BROKER_URL', 'localhost')
    broker_port = int(os.getenv('MQTT_BROKER_PORT', 1883))
    username = os.getenv('MQTT_USERNAME', '')
    password = os.getenv('MQTT_PASSWORD', '')
    
    # Initialize MQTT client
    client_id = f'anpr_server_{os.getpid()}'
    mqtt_client = mqtt.Client(client_id=client_id)
    
    # Set auth if provided
    if username and password:
        mqtt_client.username_pw_set(username, password)
    
    # Store app context for handlers
    mqtt_client.app = app
    
    # Set up callbacks
    mqtt_client.on_connect = on_connect
    mqtt_client.on_message = on_message
    
    # Connect to broker
    try:
        mqtt_client.connect(broker_url, broker_port, keepalive=60)
        mqtt_client.loop_start()
    except Exception as e:
        print(f"Failed to connect to MQTT broker: {e}")

def on_connect(client, userdata, flags, rc, properties=None):
    """Callback for when the client connects to the broker"""
    if rc == 0:
        print('Connected to MQTT broker')
        
        # Subscribe to all topics
        topics = [
            TOPIC_GATE_STATUS,
            TOPIC_GATE_ACCESS,
            TOPIC_GATE_SYNC,
            TOPIC_GATE_SYNC_REQUEST,
            TOPIC_GATE_SYNC_LOGS
        ]
        
        for topic in topics:
            client.subscribe(topic)
            print(f'Subscribed to {topic}')
    else:
        print(f'Bad connection. Code: {rc}')

def on_message(client, userdata, message):
    """Callback for when a message is received"""
    try:
        topic = message.topic
        payload = json.loads(message.payload.decode("utf-8"))

        print(f"Received message on {topic}")
        
        # Extract gate_id from topic
        topic_parts = topic.split('/')
        if len(topic_parts) < 3:
            return
        
        gate_id = topic_parts[1]
        action = topic_parts[2]
        
        # Add topic to payload for sync handling
        payload['topic'] = topic

        # Create app context for database operations
        with client.app.app_context():
            if action == 'status':
                print(f"Handling gate status for gate {gate_id}")
                handle_gate_status(gate_id, payload)
            elif action == 'access':
                print(f"Handling gate access for gate {gate_id}")
                handle_gate_access(gate_id, payload)
            elif action == 'sync':
                print(f"Handling gate sync for gate {gate_id}")
                handle_gate_sync(gate_id, payload)
            
    except json.JSONDecodeError as e:
        print(f"Error decoding message payload: {e}")
    except Exception as e:
        print(f"Error processing message: {e}")

def handle_gate_status(gate_id, payload):
    """Handle gate status updates"""
    with mqtt_client.app.app_context():
        gate = db.get_gate(gate_id)
        if gate:
            success = db.update_gate_status(
                gate_id=gate_id,
                status=payload.get('status', 'offline'),
                last_online=datetime.now() if payload.get('status') == 'online' else gate.last_online,
            )
            if success:
                print(f"Gate {gate_id} status updated successfully. Status: {payload.get('status', 'offline')}")
            else:
                print(f"Failed to update gate {gate_id} status")
        else: 
            print(f"Gate {gate_id} not found, creating new gate")
            success, errors = db.add_gate(gate_id, payload.get('location', 'Unknown'))
            if success:
                print(f"Gate {gate_id} created successfully")
            else:
                print(f"Error creating gate {gate_id}: {errors}")

def handle_gate_access(gate_id, payload, url=None):
    """Handle access requests from gates"""
    with mqtt_client.app.app_context():
        # Process image with YOLO API
        image_data = None
        if url is None:
            # Decode base64 image data
            image_data = base64.b64decode(payload['image'])
        
        plate_text, confidence = process_image_with_yolo(image_data, url=url)
        print(f"Detected plate: {plate_text} with confidence: {confidence}")        
        # Check authorization
        is_authorized = False
        accessing = False
        vehicle_row = sqlite.get_vehicle_by_plate_number(plate_text)
        vehicle_data = dict(vehicle_row) if vehicle_row else None
        print(f"Vehicle data for plate {plate_text}: {vehicle_data}")
        if vehicle_data:
            vehicle = Vehicle(vehicle_data)
            if vehicle.is_authorized and vehicle.is_currently_valid():
                is_authorized = True
                accessing = sqlite.is_vehicle_in_parking(vehicle.plate_number) 
                accessing = not accessing

        # Log access attempt    
        success = sqlite.create_access_log(
            plate_number=plate_text,
            gate_id=gate_id,
            access_granted=is_authorized,
            confidence_score=confidence,
            accessing=accessing
        )

        # Send response back to gate
        print(f"Sending response to gate {gate_id}: {is_authorized}")
        response_topic = TOPIC_SERVER_RESPONSE.format(gate_id=gate_id)
        response = {
            'plate_number': plate_text,
            'access_granted': is_authorized,
            'confidence': confidence,
            'timestamp': datetime.utcnow().isoformat(),
            'accessing': accessing

        }
        mqtt_client.publish(response_topic, json.dumps(response))

def handle_gate_sync(gate_id, payload):
    """Handle gate synchronization requests"""
    try:
        with mqtt_client.app.app_context():
            print(f"Handling sync request from gate {gate_id}")
            
            # Check message type
            topic_parts = payload.get('topic', '').split('/')
            sync_type = topic_parts[-1] if len(topic_parts) >= 2 else ''

            if sync_type == 'request':
                # Handle vehicle list sync request
                print(f"Processing sync request with version {payload.get('sync_version', 0)}")
                
                # Get all authorized vehicles from BigQuery
                vehicles_data = db.get_vehicles()

                print(f"Found {len(vehicles_data)} authorized vehicles for sync")

                sync_info = db.get_sync_info()
                last_sync = sqlite.get_vehicle_last_sync_time()

                if last_sync is not None:
                    dt = datetime.fromisoformat(last_sync)
                    last_sync = dt.replace(tzinfo=None)

                sync_version = sync_info['sync_version']

                print(f"Last sync: {last_sync}")

                vehicle_list = []
                for v in vehicles_data:
                    # Si el vehículo nunca fue sincronizado o fue sincronizado antes del último sync global
                    if last_sync is None or (last_sync and last_sync < v.last_sync):
                        vehicle_list.append({
                            'plate_number': Vehicle(v).plate_number,
                            'owner_name': Vehicle(v).owner_name,
                            'valid_from': Vehicle(v).valid_from.isoformat() if Vehicle(v).valid_from else None,
                            'valid_until': Vehicle(v).valid_until.isoformat() if Vehicle(v).valid_until else None,
                            'is_authorized': Vehicle(v).is_authorized
                        })

                print(f"Prepared vehicle list with {len(vehicle_list)} vehicles for gate {gate_id}")

                # Send response with vehicle list
                response_topic = TOPIC_GATE_SYNC_RESPONSE.format(gate_id=gate_id)             
                
                response = {
                    'vehicles': vehicle_list,
                    'sync_version': db.get_sync_info()['sync_version'],
                    'timestamp': datetime.now().isoformat()
                }

                mqtt_client.publish(response_topic, json.dumps(response))
                print(f"Sent sync response with {len(vehicle_list)} vehicles")

            elif sync_type == 'logs':
                # Handle access logs sync
                logs = payload.get('logs', [])
                print(f"Processing {len(logs)} logs from gate {gate_id}")
                
                success_logs = []
                failed_logs = []

                def normalize_log_for_bigquery(log_obj):
                    log_obj['access_granted'] = bool(log_obj['access_granted'])

                    if 'timestamp' in log_obj and log_obj['timestamp']:
                        ts_obj = datetime.fromisoformat(log_obj['timestamp'])
                        ts_utc = ts_obj.replace(tzinfo=None)
                        log_obj['timestamp'] = ts_utc.strftime('%Y-%m-%d %H:%M:%S')

                    return log_obj

                for log in logs:
                    try:
                        log['gate_id'] = gate_id
                        log = normalize_log_for_bigquery(log)
                        print(log)
                        if db.create_access_log(**log):
                            success_logs.append(log['id'])
                        else:
                            failed_logs.append(log['id'])
                    except Exception as e:
                        print(f"Error processing log {log['id']}: {e}")
                        failed_logs.append(log['id'])

                # Send acknowledgment
                response_topic = TOPIC_GATE_SYNC_LOGS_ACK.format(gate_id=gate_id)
                response = {
                    'status': 'success' if not failed_logs else 'partial',
                    'log_ids': success_logs,
                    'failed_log_ids': failed_logs,
                    'timestamp': datetime.now().isoformat()
                }
                mqtt_client.publish(response_topic, json.dumps(response))
                print(f"Sent logs sync response. Success: {len(success_logs)}, Failed: {len(failed_logs)}")

    except Exception as e:
        print(f"Error in handle_gate_sync: {e}")
        # Send error response if possible
        if gate_id:
            error_topic = TOPIC_GATE_SYNC_RESPONSE.format(gate_id=gate_id)
            error_response = {
                'error': str(e),
                'timestamp': datetime.now().isoformat()
            }
            mqtt_client.publish(error_topic, json.dumps(error_response))
