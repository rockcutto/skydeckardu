#!/usr/bin/env python3
"""
skydeck_joystick_sender.py
Serial ESP32 → CRSF.
"""

import argparse
import logging
import sys
import threading
import time
from contextlib import contextmanager
from typing import List, Dict

import serial
from inputs import get_gamepad, UnpluggedError

# --- Константы ---
DEFAULT_BAUD = 115200
DEFAULT_HZ   = 100
CHAN_COUNT   = 8

MAX_JOY_VAL  = 32767.0
MAX_TRIG_VAL = 255.0
DEADZONE     = 0.05

def map_axis(v: float) -> int:
    """[-1..1] → [0..800]"""
    return int((v + 1) * 400)

def map_trigger(v: float) -> int:
    """[0..1] → [0..800]"""
    return int(v * 800)

class Ctrl:
    """
    Фоновый поток для get_gamepad(), актуальные значения доступны через self.read_values()
    """
    def __init__(self):
        self._lock = threading.Lock()
        self._state: Dict[str, float] = {}
        self._stop = threading.Event()
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.thread.start()

    def stop(self):
        self._stop.set()
        self.thread.join(timeout=1)

    def read_values(self) -> List[float]:
        """[LX, LY, RX, RY, LT, LB, RT, RB]"""
        with self._lock:
            s = self._state.copy()
        return [
            s.get("ABS_X", 0.0),
            -s.get("ABS_Y", 0.0),
            s.get("ABS_RX", 0.0),
            -s.get("ABS_RY", 0.0),
            s.get("ABS_Z", 0.0),
            s.get("BTN_TL", 0),
            s.get("ABS_RZ", 0.0),
            s.get("BTN_TR", 0),
        ]

    def _run(self):
        normalize = {
            "ABS_X":  self._norm_axis,
            "ABS_Y":  self._norm_axis,
            "ABS_RX": self._norm_axis,
            "ABS_RY": self._norm_axis,
            "ABS_Z":  self._norm_trig,
            "ABS_RZ": self._norm_trig,
            "BTN_TL": lambda v: v,
            "BTN_TR": lambda v: v,
        }
        while not self._stop.is_set():
            try:
                for e in get_gamepad():
                    if e.code in normalize:
                        val = normalize[e.code](e.state)
                        with self._lock:
                            self._state[e.code] = val
            except UnpluggedError:
                logging.warning("Gamepad unplugged, retry in 0.5s")
                time.sleep(0.5)
            except Exception:
                logging.exception("Unexpected get_gamepad error")
                time.sleep(0.1)

    @staticmethod
    def _norm_axis(v: int) -> float:
        x = v / MAX_JOY_VAL
        return x if abs(x) >= DEADZONE else 0.0

    @staticmethod
    def _norm_trig(v: int) -> float:
        return max(0.0, min(1.0, v / MAX_TRIG_VAL))

def format_packet(vals: List[float]) -> bytes:
    """
    vals = [LX, LY, RX, RY, LT, LB, RT, RB, ...]
    возвращает b"LY LX RY RX LT RT LB RB :"
    """
    lx, ly, rx, ry, lt, lb, rt, rb = vals[:CHAN_COUNT]
    s = (
        f"{map_axis(ly):03d}"
        f"{map_axis(lx):03d}"
        f"{map_axis(ry):03d}"
        f"{map_axis(rx):03d}"
        f"{map_trigger(lt):03d}"
        f"{map_trigger(rt):03d}"
        f"{map_trigger(lb):03d}"
        f"{map_trigger(rb):03d}:"
    )
    return s.encode("ascii")

@contextmanager
def open_serial(port: str, baud: int):
    ser = serial.Serial(port, baud, timeout=1)
    try:
        yield ser
    finally:
        ser.close()

def auto_find_port(baud: int) -> str:
    for c in ("/dev/ttyACM0", "/dev/ttyACM1"):
        try:
            with serial.Serial(c, baud, timeout=0.1):
                return c
        except Exception:
            continue
    logging.error("Не найден ни один /dev/ttyACM[01]")
    sys.exit(1)

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("-p", "--port", help="e.g. /dev/ttyACM0")
    p.add_argument("-b", "--baud", type=int, default=DEFAULT_BAUD)
    p.add_argument("-r", "--rate", type=int, default=DEFAULT_HZ)
    args = p.parse_args()

    logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(message)s")

    port = args.port or auto_find_port(args.baud)
    logging.info("Открываю Serial %s @%d", port, args.baud)
    ctrl = Ctrl()
    interval = 1.0 / args.rate

    try:
        with open_serial(port, args.baud) as ser:
            logging.info("Старт цикла @%d Hz", args.rate)
            while True:
                t0 = time.time()
                pkt = format_packet(ctrl.read_values())
                ser.write(pkt)
                dt = time.time() - t0
                if dt < interval:
                    time.sleep(interval - dt)
    except KeyboardInterrupt:
        logging.info("Выход по Ctrl-C")
    finally:
        ctrl.stop()

if __name__ == "__main__":
    main()
