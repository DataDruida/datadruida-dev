# dd — runtime DataDruida para MicroPython
# Licencia Apache-2.0 — DataDruida (https://app.datadruida.com.ar/developers)
#
# API corta para programas hechos a mano o generados por Claude:
#
#   import dd
#   th = dd.sensor("slicetex-th-mb-02s", 1)        # sensor por catálogo
#   r = dd.relays()                                # los relays de la placa
#   def loop():
#       v = th.read()                              # dict o None
#       r.set(1, dd.in_window("08:00", "20:00"))
#   dd.run(loop, every_s=5)

VERSION = "0.2.0"

from dd.relays import relays            # noqa: E402,F401
from dd.sensors import sensor           # noqa: E402,F401
from dd.clock import in_window, now     # noqa: E402,F401
from dd.control import Hysteresis, Cycle, average, vpd  # noqa: E402,F401
from dd.app import run                  # noqa: E402,F401
from dd import inputs                   # noqa: E402,F401
from dd.extras import beep, led         # noqa: E402,F401
