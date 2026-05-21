"""Real-time IMU data collection via USB serial.
Usage:
    python collect_data.py COM9                    # default port
    python collect_data.py COM9 --baud 115200      # custom baud
Keys (in PyCharm terminal or any terminal):
    1  → walk
    2  → run
    3  → wave
    4  → idle
    5  → flick
    6  → circle
    7  → sit
    8  → fall
"""
import os
os.environ['KMP_DUPLICATE_LIB_OK'] = 'TRUE'

import argparse
import csv
import datetime
import sys
import serial
import serial.tools.list_ports


LABEL_MAP = {
    '1': 'walk', '2': 'run', '3': 'wave', '4': 'idle',
    '5': 'flick', '6': 'circle', '7': 'sit', '8': 'fall',
}


def list_ports():
    ports = serial.tools.list_ports.comports()
    if not ports:
        print("No serial ports found")
        return
    for p in ports:
        print(f"  {p.device}: {p.description}")


def main():
    parser = argparse.ArgumentParser(description='SmartBracelet IMU data collector')
    parser.add_argument('port', nargs='?', default='COM9',
                        help='Serial port (default: COM9)')
    parser.add_argument('--baud', type=int, default=115200,
                        help='Baud rate (default: 115200)')
    parser.add_argument('--list-ports', action='store_true',
                        help='List available ports and exit')
    args = parser.parse_args()

    if args.list_ports:
        list_ports()
        return

    timestamp = datetime.datetime.now().strftime('%Y%m%d_%H%M%S')
    filename = f'imu_data_{timestamp}.csv'
    current_label = 'idle'

    try:
        ser = serial.Serial(args.port, args.baud, timeout=0.1)
    except serial.SerialException as e:
        print(f"Failed to open {args.port}: {e}")
        print("Available ports:")
        list_ports()
        sys.exit(1)

    print(f"Recording to {filename}")
    print(f"Port: {args.port} @ {args.baud} baud")
    print("---")
    print("Labels (press number + Enter):")
    for k, v in sorted(LABEL_MAP.items()):
        print(f"  {k} = {v}")
    print("  q  = quit")
    print("---")

    with open(filename, 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['timestamp', 'ax', 'ay', 'az', 'gx', 'gy', 'gz', 'label'])

        frame_count = 0
        try:
            while True:
                # Check for keyboard input (non-blocking)
                if sys.stdin in select.select([sys.stdin], [], [], 0)[0]:
                    cmd = sys.stdin.readline().strip()
                    if cmd == 'q':
                        break
                    elif cmd in LABEL_MAP:
                        current_label = LABEL_MAP[cmd]
                        print(f"  >>> Label set to: {current_label}")

                # Read serial line
                line = ser.readline()
                if not line:
                    continue

                line = line.decode('utf-8', errors='replace').strip()
                if not line:
                    continue
                if line.startswith('DATA,'):
                    data = line.split(',')
                    writer.writerow(data[1:] + [current_label])
                    frame_count += 1
                    if frame_count % 100 == 0:
                        print(f"  Collected {frame_count} frames, label: {current_label}")

        except KeyboardInterrupt:
            pass
        finally:
            ser.close()

    print(f"---")
    print(f"Saved {frame_count} frames to {filename}")


if __name__ == '__main__':
    import select
    main()
