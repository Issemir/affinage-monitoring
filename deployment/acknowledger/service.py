import json
import logging
import os
import time

import paho.mqtt.client as mqtt
from influxdb_client import InfluxDBClient, Point, WritePrecision
from influxdb_client.client.write_api import SYNCHRONOUS


# --------------------------------------------------
# Configuration
# --------------------------------------------------

MQTT_HOST = "mosquitto"
MQTT_PORT = 1883
MQTT_USERNAME = os.environ["MQTT_USERNAME"]
MQTT_PASSWORD = os.environ["MQTT_PASSWORD"]

MQTT_INPUT_TOPIC = "affinage-fridge/measurements/#"
MQTT_ACK_PREFIX = "affinage-fridge/ack/"

INFLUX_URL = "http://influxdb:8086"
INFLUX_TOKEN = os.environ["INFLUXDB_TOKEN"]
INFLUX_ORG = os.environ["INFLUXDB_ORG"]
INFLUX_BUCKET = os.environ["INFLUXDB_BUCKET"]

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s"
)

log = logging.getLogger("affinage-acknowledger")


# --------------------------------------------------
# InfluxDB
# --------------------------------------------------

influx = InfluxDBClient(
    url=INFLUX_URL,
    token=INFLUX_TOKEN,
    org=INFLUX_ORG,
)

write_api = influx.write_api(write_options=SYNCHRONOUS)


# --------------------------------------------------
# MQTT callbacks
# --------------------------------------------------

def on_connect(client, userdata, flags, reason_code, properties):
    if reason_code == 0:
        log.info("Connected to Mosquitto")
        client.subscribe(MQTT_INPUT_TOPIC, qos=1)
        log.info(f"Subscribed to {MQTT_INPUT_TOPIC}")
    else:
        log.error(f"MQTT connection failed: {reason_code}")


def on_message(client, userdata, msg):
    try:
        payload = json.loads(msg.payload.decode("utf-8"))

        reading_id = str(payload["id"])
        timestamp = int(payload["timestamp"])
        temperature = float(payload["temperature"])
        humidity = float(payload["humidity"])

        if not reading_id or timestamp <= 0:
            raise ValueError("Invalid reading ID or timestamp")

        if not (-40 <= temperature <= 125):
            raise ValueError("Temperature outside valid range")

        if not (0 <= humidity <= 100):
            raise ValueError("Humidity outside valid range")

        point = (
            Point("fridge")
            .tag("topic", msg.topic)
            .field("temperature", temperature)
            .field("humidity", humidity)
            .time(timestamp, WritePrecision.S)
        )

        # Wait for InfluxDB to confirm the write before acknowledging.
        write_api.write(
            bucket=INFLUX_BUCKET,
            org=INFLUX_ORG,
            record=point,
        )

        device = msg.topic.rsplit("/", 1)[-1]
        ack_topic = f"{MQTT_ACK_PREFIX}{device}"
        ack_payload = json.dumps({"id": reading_id})

        result = client.publish(
            ack_topic,
            ack_payload,
            qos=1,
            retain=False,
        )

        if result.rc != mqtt.MQTT_ERR_SUCCESS:
            raise RuntimeError(f"Could not queue MQTT acknowledgement: {result.rc}")

        log.info(
            f"Stored reading {reading_id}: "
            f"{temperature:.2f} °C, {humidity:.2f}% RH"
        )

    except Exception as error:
        log.exception(f"Failed to process message on {msg.topic}: {error}")


# --------------------------------------------------
# Main
# --------------------------------------------------

client = mqtt.Client(
    callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
    client_id="affinage-acknowledger",
    protocol=mqtt.MQTTv311,
)

client.username_pw_set(MQTT_USERNAME, MQTT_PASSWORD)

client.on_connect = on_connect
client.on_message = on_message

while True:
    try:
        log.info("Connecting to Mosquitto...")
        client.connect(MQTT_HOST, MQTT_PORT, keepalive=60)
        client.loop_forever()

    except Exception as error:
        log.exception(f"MQTT connection lost: {error}. Retrying in 5 seconds.")
        time.sleep(5)

