FROM python:3.12-slim

ENV DEBIAN_FRONTEND=noninteractive
WORKDIR /app
COPY registry/requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt
COPY registry/wsgi.py .
COPY registry/netpulse_registry ./netpulse_registry

ENV NETPULSE_DB=/data/netpulse.db
EXPOSE 5000
CMD ["gunicorn", "--bind", "0.0.0.0:5000", "--workers", "1", "--threads", "4", "wsgi:app"]
