"""Capture ESP32 USB logs without changing DTR/RTS on open."""
import argparse
from pathlib import Path
import sys
import time

import serial

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--port', required=True)
parser.add_argument('--seconds', type=int, default=120)
parser.add_argument('--output', default='logs/weather.log')
parser.add_argument('--until-weather', action='store_true')
args = parser.parse_args()
path = Path(args.output)
path.parent.mkdir(parents=True, exist_ok=True)
deadline = time.monotonic() + args.seconds
found = False
pending = b''
with path.open('wb') as log:
    while time.monotonic() < deadline and not found:
        try:
            with serial.Serial(port=None, baudrate=115200, timeout=0.3) as port:
                port.dtr = False
                port.rts = False
                port.port = args.port
                port.open()
                while time.monotonic() < deadline:
                    data = port.read(4096)
                    if not data:
                        continue
                    log.write(data)
                    log.flush()
                    sys.stdout.buffer.write(data)
                    sys.stdout.buffer.flush()
                    pending = (pending + data)[-16384:]
                    if args.until_weather and b'WEATHER_OK ' in pending:
                        found = True
                        break
        except serial.SerialException as error:
            print(f'Waiting for {args.port}: {error}', file=sys.stderr, flush=True)
            time.sleep(1)
if args.until_weather and not found:
    print('WEATHER_OK was not observed before timeout', file=sys.stderr)
    sys.exit(1)
