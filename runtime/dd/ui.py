# dd/ui.py — pantallas con LVGL (firmware DataDruida con pantalla)
# Licencia Apache-2.0 — DataDruida
#
#   import dd
#   ui = dd.ui()                          # arranca pantalla + touch de la placa
#   lv = ui.lv                            # LVGL 9 completo (lv.label, lv.button, lv.chart...)
#   t = ui.card("Temperatura", "C")       # tarjeta lista para mostrar un valor
#   def loop():
#       t.set(24.6)
#   dd.run(loop, every_s=2)
#
# Necesita el firmware DataDruida con pantalla (MicroPython + LVGL + ddhw). La
# página lo instala solo en las placas que lo requieren. LVGL corre en el hilo
# de MicroPython: un Timer agenda lv.timer_handler() cada 10 ms con
# micropython.schedule, así nunca se pisa con el código del usuario.

_ui = None

# paleta oscura de DataDruida
BG = 0x0E1317
CARD = 0x1A232B
TEXT = 0xE6EDF3
MUTED = 0x8592A0
ACCENT = 0x3FE0B0


class _Card:
    def __init__(self, ui, title, unit, parent=None):
        lv = ui.lv
        self.unit = unit
        self.obj = lv.obj(parent or ui.grid)
        self.obj.set_size(ui.card_w, ui.card_h)
        self.obj.set_style_bg_color(lv.color_hex(CARD), 0)
        self.obj.set_style_border_width(0, 0)
        self.obj.set_style_radius(14, 0)
        self.obj.remove_flag(lv.obj.FLAG.SCROLLABLE)
        t = lv.label(self.obj)
        t.set_text(title)
        t.set_style_text_color(lv.color_hex(MUTED), 0)
        t.set_style_text_font(ui.font_small, 0)
        t.align(lv.ALIGN.TOP_LEFT, 0, 0)
        self.value = lv.label(self.obj)
        self.value.set_text("--")
        self.value.set_style_text_color(lv.color_hex(TEXT), 0)
        self.value.set_style_text_font(ui.font_big, 0)
        self.value.align(lv.ALIGN.BOTTOM_LEFT, 0, 0)

    def set(self, v, fmt="%.1f", color=None):
        """Muestra el valor (None = "--"). color: 0xRRGGBB para resaltar (alarma)."""
        lv = _ui.lv
        txt = "--" if v is None else (fmt % v) + (" " + self.unit if self.unit else "")
        self.value.set_text(txt)
        self.value.set_style_text_color(lv.color_hex(color if color is not None else TEXT), 0)


class UI:
    def __init__(self, board_id):
        try:
            import lvgl as lv
            import ddhw
        except ImportError:
            raise RuntimeError("esta pantalla necesita el firmware DataDruida con pantalla: "
                               "instalalo desde la página (paso 1)")
        self.lv = lv
        self._ddhw = ddhw
        if not lv.is_initialized():
            lv.init()
        self.width, self.height = ddhw.init(board_id)
        self.scr = lv.screen_active()
        self.scr.set_style_bg_color(lv.color_hex(BG), 0)
        self.scr.set_style_text_color(lv.color_hex(TEXT), 0)
        big = self.width >= 800
        self.font_small = lv.font_montserrat_16 if big else lv.font_montserrat_14
        self.font_big = lv.font_montserrat_48 if big else lv.font_montserrat_24
        self.card_w = (self.width - 48) // (3 if big else 2)
        self.card_h = 130 if big else 80
        self.grid = None
        self._start_loop()

    def _start_loop(self):
        import micropython
        from machine import Timer
        lv = self.lv
        busy = [False]

        def tick(_):
            if busy[0]:
                return
            busy[0] = True
            try:
                lv.timer_handler()
            finally:
                busy[0] = False

        def irq(_t):
            try:
                micropython.schedule(tick, None)
            except RuntimeError:
                pass                     # cola llena: se dibuja en el próximo

        self._timer = Timer(1)
        self._timer.init(period=10, mode=Timer.PERIODIC, callback=irq)

    def _ensure_grid(self):
        if self.grid is None:
            lv = self.lv
            self.grid = lv.obj(self.scr)
            self.grid.set_size(self.width, self.height)
            self.grid.set_style_bg_opa(0, 0)
            self.grid.set_style_border_width(0, 0)
            self.grid.set_style_pad_all(12, 0)
            self.grid.set_style_pad_gap(12, 0)
            self.grid.set_flex_flow(lv.FLEX_FLOW.ROW_WRAP)

    def card(self, title, unit=""):
        """Tarjeta con título y valor grande; devuelve un objeto con .set(valor)."""
        self._ensure_grid()
        return _Card(self, title, unit)

    def title(self, text):
        """Título arriba de las tarjetas."""
        self._ensure_grid()
        lab = self.lv.label(self.grid)
        lab.set_text(text)
        lab.set_width(self.width - 24)
        lab.set_style_text_color(self.lv.color_hex(ACCENT), 0)
        return lab

    def button(self, text, on_click):
        """Botón: on_click() se llama al tocarlo."""
        self._ensure_grid()
        lv = self.lv
        b = lv.button(self.grid)
        lab = lv.label(b)
        lab.set_text(text)
        lab.center()
        b.add_event_cb(lambda e: on_click(), lv.EVENT.CLICKED, None)
        return b

    def brightness(self, pct):
        self._ddhw.backlight(int(pct))


def ui():
    """Pantalla de la placa con LVGL. Error claro si la placa no tiene."""
    global _ui
    if _ui is None:
        from dd import board
        b = board.current()
        if b.get("ui") != "lvgl":
            raise ValueError("esta placa no tiene pantalla LVGL")
        _ui = UI(board.current_id())
    return _ui
