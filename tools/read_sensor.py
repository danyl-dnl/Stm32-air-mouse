import re
import time
import ctypes
import threading

import serial

# ============================================================
# SETTINGS  (tune these)
# ============================================================
PORT = "COM5"
BAUD = 115200

SCALE = 131.0          # gyro counts per deg/s (+-250 dps range)
X_DIR = -1             # GZ -> horizontal
Y_DIR = 1              # GX -> vertical

DEADZONE = 1.5         # deg/s ignored around zero
GAIN_X = 0.12          # px per (deg/s) per packet  <- main speed knob
GAIN_Y = 0.12
MAX_STEP = 20.0        # max px per packet (only a safety cap now)

CAL_PACKETS = 150      # ~1.5 s of stillness at startup / on F9
CAL_MAX_SPREAD = 800   # counts; larger means the board moved, so retry

STILL_DPS = 2.0        # below this = "still" for bias tracking
STILL_PACKETS = 30     # ~0.3 s of stillness before bias tracking starts
BIAS_RATE = 0.01       # how fast the bias follows drift

DEBOUNCE = 3           # packets (~30 ms) for the button
SEND_HZ = 125          # how often accumulated pixels are sent to Windows

# Global hotkeys (work even when the console has no focus)
VK_F8, VK_F9, VK_F10 = 0x77, 0x78, 0x79   # pause/resume, recalibrate, quit

PAT = re.compile(r"GX=(-?\d+)\s+GZ=(-?\d+)\s+B=([01])(?:\s+R=([01]))?")
user32 = ctypes.windll.user32

MOUSEEVENTF_MOVE = 0x0001
MOUSEEVENTF_LEFTDOWN = 0x0002
MOUSEEVENTF_LEFTUP = 0x0004
MOUSEEVENTF_RIGHTDOWN = 0x0008
MOUSEEVENTF_RIGHTUP = 0x0010

# ============================================================
# SHARED STATE
# ============================================================
lock = threading.Lock()
acc_x = 0.0            # accumulated pixels (fractions are kept)
acc_y = 0.0
bias_x = 0.0           # residual gyro bias in counts
bias_z = 0.0
cal_buf = None         # list while calibrating, otherwise None
enabled = True
running = True
last_gx = 0
last_gz = 0
last_b = 0
last_r = 0


def curve(w, gain):
    """deg/s -> pixels for one packet. Linear after the dead zone."""
    a = abs(w)
    if a <= DEADZONE:
        return 0.0
    v = min(gain * (a - DEADZONE), MAX_STEP)
    return v if w > 0 else -v


# ============================================================
# SERIAL READER: processes every packet exactly once
# ============================================================
def reader(ser):
    global acc_x, acc_y, bias_x, bias_z, cal_buf, running, last_gx, last_gz, last_b, last_r

    still = 0
    btn_state = 0
    btn_count = 0
    r_state = 0
    r_count = 0

    while running:
        try:
            line = ser.readline().decode("ascii", errors="ignore")
        except Exception as e:
            print("\nSerial error:", e)
            running = False
            return

        m = PAT.search(line)
        if not m:
            continue

        gx, gz, b = int(m[1]), int(m[2]), int(m[3])
        r = int(m[4]) if m[4] else 0

        with lock:
            last_gx, last_gz, last_b, last_r = gx, gz, b, r

            # ---------------- calibration ----------------
            if cal_buf is not None:
                cal_buf.append((gx, gz))
                if len(cal_buf) >= CAL_PACKETS:
                    xs = [p[0] for p in cal_buf]
                    zs = [p[1] for p in cal_buf]
                    if (max(xs) - min(xs) > CAL_MAX_SPREAD
                            or max(zs) - min(zs) > CAL_MAX_SPREAD):
                        print("\nBoard moved during calibration, retrying...")
                        cal_buf = []
                    else:
                        bias_x = sum(xs) / len(xs)
                        bias_z = sum(zs) / len(zs)
                        cal_buf = None
                        still = 0
                        print(f"\nCalibrated: bias GX={bias_x:.1f} GZ={bias_z:.1f}")
                continue

            # ---------------- gyro -> motion ----------------
            wx = (gx - bias_x) / SCALE      # GX -> vertical
            wz = (gz - bias_z) / SCALE      # GZ -> horizontal

            # slowly follow drift while the board is still
            if abs(wx) < STILL_DPS and abs(wz) < STILL_DPS:
                still += 1
                if still >= STILL_PACKETS:
                    bias_x += BIAS_RATE * (gx - bias_x)
                    bias_z += BIAS_RATE * (gz - bias_z)
            else:
                still = 0

            if enabled:
                acc_x += X_DIR * curve(wz, GAIN_X)
                acc_y += Y_DIR * curve(wx, GAIN_Y)

            # ---------------- button -> left click ----------------
            if b == btn_state:
                btn_count = 0
            else:
                btn_count += 1
                if btn_count >= DEBOUNCE:
                    btn_state = b
                    btn_count = 0
                    if b and enabled:
                        user32.mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0)
                        print('\n[LEFT button DOWN sent]')
                    elif not b:
                        user32.mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0)
                        print('\n[LEFT button UP sent]')

            # ---------------- button 2 -> right click ----------------
            if r == r_state:
                r_count = 0
            else:
                r_count += 1
                if r_count >= DEBOUNCE:
                    r_state = r
                    r_count = 0
                    if r and enabled:
                        user32.mouse_event(MOUSEEVENTF_RIGHTDOWN, 0, 0, 0, 0)
                        print('\n[RIGHT button DOWN sent]')
                    elif not r:
                        user32.mouse_event(MOUSEEVENTF_RIGHTUP, 0, 0, 0, 0)
                        print('\n[RIGHT button UP sent]')


def key_down(vk):
    return bool(user32.GetAsyncKeyState(vk) & 0x8000)


# ============================================================
# MAIN
# ============================================================
def main():
    global acc_x, acc_y, cal_buf, enabled, running

    print("=" * 60)
    print("STM32 AIR MOUSE   F8 = pause/resume   F9 = recalibrate   F10 = quit")
    print("=" * 60)

    try:
        ser = serial.Serial(PORT, BAUD, timeout=0.5)
    except Exception as e:
        print("Could not open serial port:", e)
        return

    ser.reset_input_buffer()
    cal_buf = []
    print("Keep the board still, calibrating...")

    threading.Thread(target=reader, args=(ser,), daemon=True).start()

    while running and cal_buf is not None:
        time.sleep(0.05)

    print("Running.")

    prev_f8 = prev_f9 = False
    last_print = 0.0
    interval = 1.0 / SEND_HZ

    try:
        while running:
            t0 = time.time()

            # ---------------- hotkeys ----------------
            f8, f9, f10 = key_down(VK_F8), key_down(VK_F9), key_down(VK_F10)
            if f10:
                break
            if f8 and not prev_f8:
                with lock:
                    enabled = not enabled
                    acc_x = acc_y = 0.0
                print("\nMouse", "ENABLED" if enabled else "PAUSED")
            if f9 and not prev_f9:
                with lock:
                    cal_buf = []
                    acc_x = acc_y = 0.0
                print("\nRecalibrating, keep the board still...")
            prev_f8, prev_f9 = f8, f9

            # ---------------- send whole pixels ----------------
            with lock:
                ix, iy = int(acc_x), int(acc_y)
                acc_x -= ix          # keep the fractional part
                acc_y -= iy
                gx_v, gz_v, bx, bz, b_v, r_v = last_gx, last_gz, bias_x, bias_z, last_b, last_r

            if ix or iy:
                user32.mouse_event(MOUSEEVENTF_MOVE, ix, iy, 0, 0)

            # ---------------- status line ----------------
            if t0 - last_print >= 0.1:
                print(f"\rGX={gx_v:6d} GZ={gz_v:6d}  biasX={bx:7.1f} biasZ={bz:7.1f}  "
                      f"B={b_v} R={r_v}  dx={ix:3d} dy={iy:3d}  {'ON ' if enabled else 'OFF'}",
                      end="", flush=True)
                last_print = t0

            rest = interval - (time.time() - t0)
            if rest > 0:
                time.sleep(rest)

    except KeyboardInterrupt:
        pass
    finally:
        running = False
        user32.mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0)
        user32.mouse_event(MOUSEEVENTF_RIGHTUP, 0, 0, 0, 0)
        ser.close()
        print("\nStopped.")


if __name__ == "__main__":
    main()