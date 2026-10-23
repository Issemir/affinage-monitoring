# Affinage Monitoring

An MQTT-to-Grafana monitoring stack for an affinage fridge. The deployment collects sensor messages with Telegraf, stores them in InfluxDB, and provisions a Grafana dashboard for viewing fridge temperature and humidity.

## Components

- **Mosquitto** — authenticated MQTT broker on port `1883`.
- **Telegraf** — subscribes to `affinage-fridge/measurements/#` and forwards JSON messages to InfluxDB.
- **InfluxDB 2.7** — stores measurements in the `fridge` bucket (three months' retention by default).
- **Grafana** — available on port `3001`, with the Affinage Fridge dashboard provisioned automatically.

The dashboard expects measurements named `fridge` with numeric fields `temperature_c` and `humidity_pct`. Sensor firmware should publish JSON containing those fields to a topic under `affinage-fridge/measurements/`. The `firmware/monitor` sketch is currently empty, so firmware publishing is not yet implemented in this repository.

## Run the monitoring stack

Requirements: Docker with the Docker Compose plugin.

1. From the repository root, create the local environment file:

   ```sh
   cp deployment/.env.example deployment/.env
   ```

2. Edit `deployment/.env` and replace each `REPLACE_WITH_...` value with a unique password or long random token. Keep the MQTT username as `telegraf` unless you also change the username in the Compose configuration.

3. Create the Mosquitto password file. Use the same MQTT password entered in `deployment/.env` when prompted:

   ```sh
   mkdir -p secrets
   docker run --rm -it \
     -v "$PWD/secrets:/mosquitto/config" \
     eclipse-mosquitto:2 \
     mosquitto_passwd -c /mosquitto/config/mosquitto_passwordfile telegraf
   ```

   The Compose deployment mounts this file from `secrets/mosquitto_passwordfile`.

4. Start the services:

   ```sh
   cd deployment
   docker compose up -d
   ```

   Open Grafana at <http://localhost:3001>. Sign in with the `GRAFANA_ADMIN_USER` and `GRAFANA_ADMIN_PASSWORD` values from `.env` (defaults to username `admin`). The **Affinage Fridge** dashboard is provisioned under the **Affinage** folder.

To view service logs, run `docker compose logs -f` from `deployment/`. To stop the services, run `docker compose down`; named volumes retain database, broker, and Grafana data. To remove stored data too, run `docker compose down -v`.

## Configuration

`deployment/.env.example` lists the supported deployment settings:

| Variable | Default | Purpose |
| --- | --- | --- |
| `INFLUXDB_USERNAME` | — | Initial InfluxDB admin username (required) |
| `INFLUXDB_PASSWORD` | — | Initial InfluxDB admin password (required) |
| `INFLUXDB_ORG` | `affinage` | InfluxDB organization |
| `INFLUXDB_BUCKET` | `fridge` | InfluxDB bucket |
| `INFLUXDB_TOKEN` | — | Token used by Telegraf and Grafana (required) |
| `MQTT_USERNAME` | `telegraf` | MQTT username used by Telegraf |
| `MQTT_PASSWORD` | — | MQTT password used by Telegraf (required) |
| `GRAFANA_ADMIN_USER` | `admin` | Grafana administrator username |
| `GRAFANA_ADMIN_PASSWORD` | — | Grafana administrator password (required) |

Do not commit `deployment/.env` or files in `secrets/`; both are excluded by `.gitignore`.

## Repository layout

- `deployment/compose.yml` — Docker Compose services, volumes, and network.
- `deployment/telegraf/telegraf.conf` — MQTT subscription and InfluxDB output.
- `deployment/mosquitto/config/mosquitto.conf` — broker authentication and persistence settings.
- `deployment/grafana/` — provisioned InfluxDB datasource and fridge dashboard.
- `firmware/monitor/` — planned fridge monitoring firmware.
- `firmware/firmware/blink/` — basic Arduino blink sketch.
