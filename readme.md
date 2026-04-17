# rf1d-m4sk

RFID reader and writer

## Layout

- `docker/` container files for deterministic builds
- `firmware/` ESP8266 RTOS SDK firmware
- `hardware/` wiring and hardware notes

## Build (Docker)

```bash
docker compose run --rm esp8266-build make
```

The resulting binaries are in `firmware/build/`.

## Flash (macOS host)

Docker Desktop on macOS is not ideal for direct USB serial passthrough. Build in Docker, then flash on host:

```bash
docker compose run --rm esp8266-build make flash monitor

```

Adjust the serial port name to your board.
