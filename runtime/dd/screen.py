# dd/screen.py — pantalla de la placa (ST7789 por SPI) con framebuf
# Licencia Apache-2.0 — DataDruida
#
#   import dd
#   scr = dd.screen()                     # 240 x 320 vertical; dd.screen(landscape=True) 320 x 240
#   scr.fill(dd.BLACK)
#   scr.text("Temperatura", 10, 10, dd.GRAY)
#   scr.text("24.6 C", 10, 30, dd.GREEN, size=4)
#   scr.show()                            # recién acá se ve (manda todo de una vez)
#   t = scr.touch()                       # (x, y) si están tocando, si no None
#
# Se dibuja en memoria (framebuf RGB565, 150 KB en la PSRAM) y show() lo manda
# entero por SPI: así no parpadea. Los colores se arman con dd.rgb(r, g, b).
#
# El ST7789 espera los píxeles en big-endian y framebuf los guarda en
# little-endian: por eso rgb() devuelve el color con los bytes ya invertidos.

import time

BLACK = 0x0000
WHITE = 0xFFFF


def rgb(r, g, b):
    """Color para la pantalla a partir de r, g, b (0..255)."""
    c = ((int(r) & 0xF8) << 8) | ((int(g) & 0xFC) << 3) | ((int(b) & 0xFF) >> 3)
    return ((c & 0xFF) << 8) | (c >> 8)


RED = rgb(230, 50, 50)
GREEN = rgb(60, 220, 140)
BLUE = rgb(60, 130, 240)
YELLOW = rgb(240, 200, 40)
ORANGE = rgb(245, 140, 30)
CYAN = rgb(60, 210, 230)
GRAY = rgb(140, 150, 160)
DARK = rgb(20, 26, 32)


class Screen:
    def __init__(self, cfg, landscape=False):
        import framebuf
        from machine import Pin, SPI
        self.cfg = cfg
        self.landscape = bool(landscape)
        w, h = cfg["width"], cfg["height"]
        self.width, self.height = (h, w) if self.landscape else (w, h)
        self._cs = Pin(cfg["cs"], Pin.OUT, value=1)
        self._dc = Pin(cfg["dc"], Pin.OUT, value=0)
        self._spi = SPI(cfg.get("spi", 2), baudrate=cfg.get("freq", 40000000), polarity=0, phase=0,
                        sck=Pin(cfg["sck"]), mosi=Pin(cfg["mosi"]))
        self._buf = bytearray(self.width * self.height * 2)
        self.fb = framebuf.FrameBuffer(self._buf, self.width, self.height, framebuf.RGB565)
        self._bl = None
        self._init_panel()
        self.brightness(100)
        self._tp = None

    # --- bajo nivel ---
    def _cmd(self, c, data=None):
        self._cs.value(0)
        self._dc.value(0)
        self._spi.write(bytes([c]))
        if data:
            self._dc.value(1)
            self._spi.write(data)
        self._cs.value(1)

    def _init_panel(self):
        self._cmd(0x01)                # reset por software
        time.sleep_ms(150)
        self._cmd(0x11)                # salir de sleep
        time.sleep_ms(120)
        self._cmd(0x3A, b"\x55")       # 16 bits por píxel
        self._cmd(0x36, b"\x60" if self.landscape else b"\x00")   # orientación
        if self.cfg.get("invert", True):
            self._cmd(0x21)            # este panel IPS va invertido
        self._cmd(0x13)
        self._cmd(0x29)                # pantalla encendida
        time.sleep_ms(20)

    # --- API ---
    def brightness(self, pct):
        """Brillo de la luz de fondo, 0..100."""
        bl = self.cfg.get("bl")
        if bl is None:
            return
        from machine import Pin, PWM
        if self._bl is None:
            self._bl = PWM(Pin(bl), freq=1000)
        pct = max(0, min(100, int(pct)))
        self._bl.duty_u16(pct * 65535 // 100)

    def show(self):
        """Manda lo dibujado a la pantalla."""
        w, h = self.width, self.height
        self._cmd(0x2A, bytes([0, 0, (w - 1) >> 8, (w - 1) & 0xFF]))
        self._cmd(0x2B, bytes([0, 0, (h - 1) >> 8, (h - 1) & 0xFF]))
        self._cs.value(0)
        self._dc.value(0)
        self._spi.write(b"\x2C")
        self._dc.value(1)
        self._spi.write(self._buf)
        self._cs.value(1)

    def fill(self, color=BLACK):
        self.fb.fill(color)

    def pixel(self, x, y, color):
        self.fb.pixel(x, y, color)

    def line(self, x1, y1, x2, y2, color):
        self.fb.line(x1, y1, x2, y2, color)

    def rect(self, x, y, w, h, color, fill=False):
        if fill:
            self.fb.fill_rect(x, y, w, h, color)
        else:
            self.fb.rect(x, y, w, h, color)

    def text(self, s, x, y, color=WHITE, size=1):
        """Texto con la letra de 8x8 píxeles; size la agranda (2 = 16 px, 4 = 32 px)."""
        s = str(s)
        size = max(1, int(size))
        if size == 1:
            self.fb.text(s, x, y, color)
            return
        import framebuf
        w = 8 * len(s)
        tmp = bytearray((w + 7) // 8 * 8)
        g = framebuf.FrameBuffer(tmp, w, 8, framebuf.MONO_HLSB)
        g.text(s, 0, 0, 1)
        fr = self.fb.fill_rect
        for yy in range(8):
            for xx in range(w):
                if g.pixel(xx, yy):
                    fr(x + xx * size, y + yy * size, size, size, color)

    def text_width(self, s, size=1):
        return 8 * len(str(s)) * max(1, int(size))

    def center(self, s, y, color=WHITE, size=1):
        """Texto centrado en horizontal."""
        self.text(s, (self.width - self.text_width(s, size)) // 2, y, color, size)

    def bar(self, x, y, w, h, value, vmin, vmax, color, back=DARK):
        """Barra horizontal llena según value entre vmin y vmax (value None = vacía)."""
        self.fb.fill_rect(x, y, w, h, back)
        if value is None or vmax <= vmin:
            return
        k = (value - vmin) / (vmax - vmin)
        k = 0 if k < 0 else 1 if k > 1 else k
        self.fb.fill_rect(x, y, int(w * k), h, color)

    def touch(self):
        """(x, y) donde están tocando, o None. Sin touch en la placa: siempre None."""
        t = self.cfg.get("touch")
        if not t:
            return None
        from dd import board
        i2c = board.i2c()
        if i2c is None:
            return None
        try:
            d = i2c.readfrom_mem(t["addr"], 0x02, 5)
        except OSError:
            return None
        if not d[0] & 0x0F:
            return None
        x = ((d[1] & 0x0F) << 8) | d[2]
        y = ((d[3] & 0x0F) << 8) | d[4]
        if self.landscape:
            # MADCTL 0x60: el eje largo pasa a ser x
            x, y = y, self.cfg["width"] - 1 - x
        return (x, y)


_screen = None


def screen(landscape=False):
    """Pantalla de la placa. Error claro si la placa no tiene."""
    global _screen
    from dd import board
    cfg = board.current().get("screen")
    if not cfg:
        raise ValueError("esta placa no tiene pantalla")
    if _screen is None or _screen.landscape != bool(landscape):
        _screen = Screen(cfg, landscape)
    return _screen
