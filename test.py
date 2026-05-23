python << 'EOF'
import serial, time, sys

port = '/dev/tty.usbmodem11301'
try:
    ser = serial.Serial(port, 115200, timeout=1, dsrdtr=False, rtscts=False)
    ser.reset_input_buffer()
    ser.reset_output_buffer()
except Exception as e:
    print(f'Cannot open: {e}')
    sys.exit(1)

# Toggle DTR to reset the chip
ser.dtr = False
time.sleep(0.1)
ser.dtr = True
time.sleep(0.1)
ser.dtr = False

print(f'Opened {port}, waiting...')
start = time.time()
buf = b''
while time.time() - start < 20:
    try:
        waiting = ser.in_waiting
        if waiting > 0:
            chunk = ser.read(waiting)
            buf += chunk
            text = chunk.decode('utf-8', errors='replace')
            sys.stdout.write(text)
            sys.stdout.flush()
            if b'DONE' in buf:
                break
        else:
            time.sleep(0.1)
    except Exception as e:
        sys.stdout.write(f'\n[err: {e}]')
        time.sleep(0.5)

if not buf:
    print('(no output received after 20s)')
ser.close()
EOF