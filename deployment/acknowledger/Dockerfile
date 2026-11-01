FROM python:3.12-slim

WORKDIR /app

RUN pip install --no-cache-dir \
    paho-mqtt==2.1.0 \
    influxdb-client==1.48.0

COPY service.py .

CMD ["python", "-u", "service.py"]
