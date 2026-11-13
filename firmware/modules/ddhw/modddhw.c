// ddhw/modddhw.c — módulo MicroPython `ddhw`: pantalla + touch para LVGL
// Licencia Apache-2.0 — DataDruida
//
//   import lvgl as lv, ddhw
//   lv.init()
//   w, h = ddhw.init("waveshare-esp32-s3-touch-lcd-7b")   # panel + touch + backlight
//   ... widgets de LVGL ...
//   lv.timer_handler()        # lo llama dd.ui cada ~10 ms desde el hilo de MicroPython
//
// LVGL corre SIEMPRE en el hilo de MicroPython (no hay task aparte): así los
// objetos que crea Python nunca se tocan desde dos lados a la vez.

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "py/obj.h"
#include "py/runtime.h"
#include "lvgl/lvgl.h"
#include "ddhw.h"

static const dd_board_t *s_board = NULL;
static esp_lcd_panel_handle_t s_panel = NULL;
static lv_display_t *s_disp = NULL;
static lv_indev_t *s_indev = NULL;

static uint32_t tick_cb(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

bool ddhw_spi_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *ev, void *ctx) {
    if (s_disp) {
        lv_display_flush_ready(s_disp);
    }
    return false;
}

static void flush_cb(lv_display_t *disp, const lv_area_t *a, uint8_t *px) {
    if (s_board->bus == DD_BUS_SPI) {
        // el ST7789 recibe RGB565 en big-endian
        lv_draw_sw_rgb565_swap(px, lv_area_get_size(a));
        esp_lcd_panel_draw_bitmap(s_panel, a->x1, a->y1, a->x2 + 1, a->y2 + 1, px);
        // flush_ready lo da ddhw_spi_done al terminar el DMA
    } else {
        // RGB: copia síncrona al framebuffer del panel
        esp_lcd_panel_draw_bitmap(s_panel, a->x1, a->y1, a->x2 + 1, a->y2 + 1, px);
        lv_display_flush_ready(disp);
    }
}

static void touch_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    int16_t x = 0, y = 0;
    if (s_board && dd_touch_read(s_board, &x, &y)) {
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// ddhw.init(board_id) -> (ancho, alto)
static mp_obj_t ddhw_init(mp_obj_t id_in) {
    const char *id = mp_obj_str_get_str(id_in);
    const dd_board_t *b = dd_board_find(id);
    if (!b) {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("placa sin pantalla LVGL: %s"), id);
    }
    if (s_disp) {
        if (b != s_board) {
            mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("la pantalla ya se inició con otra placa; reiniciá"));
        }
        return mp_obj_new_tuple(2, (mp_obj_t[]){ MP_OBJ_NEW_SMALL_INT(b->w), MP_OBJ_NEW_SMALL_INT(b->h) });
    }
    if (!lv_is_initialized()) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("falta lv.init() antes de ddhw.init()"));
    }
    s_board = b;
    esp_err_t e = dd_panel_init(b, &s_panel);
    if (e != ESP_OK) {
        s_board = NULL;
        mp_raise_msg_varg(&mp_type_OSError, MP_ERROR_TEXT("no arrancó la pantalla (%d)"), (int)e);
    }

    lv_tick_set_cb(tick_cb);

    // Buffers de dibujo parciales: 1/10 de pantalla, dos para alternar.
    // SPI necesita memoria interna con DMA; RGB copia al framebuffer y usa PSRAM.
    size_t lines = b->bus == DD_BUS_SPI ? 40 : b->h / 10;
    size_t sz = (size_t)b->w * lines * 2;
    uint32_t caps = b->bus == DD_BUS_SPI ? (MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL) : MALLOC_CAP_SPIRAM;
    void *buf1 = heap_caps_aligned_alloc(64, sz, caps);
    void *buf2 = heap_caps_aligned_alloc(64, sz, caps);
    if (!buf1 || !buf2) {
        mp_raise_msg(&mp_type_MemoryError, MP_ERROR_TEXT("sin memoria para los buffers de pantalla"));
    }

    s_disp = lv_display_create(b->w, b->h);
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(s_disp, buf1, buf2, sz, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(s_disp, flush_cb);

    if (b->touch != DD_TP_NONE) {
        s_indev = lv_indev_create();
        lv_indev_set_type(s_indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(s_indev, touch_cb);
        lv_indev_set_display(s_indev, s_disp);
    }
    return mp_obj_new_tuple(2, (mp_obj_t[]){ MP_OBJ_NEW_SMALL_INT(b->w), MP_OBJ_NEW_SMALL_INT(b->h) });
}
static MP_DEFINE_CONST_FUN_OBJ_1(ddhw_init_obj, ddhw_init);

// ddhw.backlight(0..100)
static mp_obj_t ddhw_backlight(mp_obj_t pct) {
    if (!s_board) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("pantalla sin iniciar"));
    }
    dd_backlight(s_board, mp_obj_get_int(pct));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(ddhw_backlight_obj, ddhw_backlight);

// ddhw.touch() -> (x, y) o None, para usar sin widgets
static mp_obj_t ddhw_touch(void) {
    int16_t x, y;
    if (s_board && dd_touch_read(s_board, &x, &y)) {
        return mp_obj_new_tuple(2, (mp_obj_t[]){ MP_OBJ_NEW_SMALL_INT(x), MP_OBJ_NEW_SMALL_INT(y) });
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(ddhw_touch_obj, ddhw_touch);

// ddhw.boards() -> lista de ids con pantalla soportada
static mp_obj_t ddhw_boards(void) {
    int n = 0;
    const dd_board_t *all = dd_board_list(&n);
    mp_obj_t l = mp_obj_new_list(0, NULL);
    for (int i = 0; i < n; i++) {
        mp_obj_list_append(l, mp_obj_new_str(all[i].id, strlen(all[i].id)));
    }
    return l;
}
static MP_DEFINE_CONST_FUN_OBJ_0(ddhw_boards_obj, ddhw_boards);

static const mp_rom_map_elem_t ddhw_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_ddhw) },
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&ddhw_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_backlight), MP_ROM_PTR(&ddhw_backlight_obj) },
    { MP_ROM_QSTR(MP_QSTR_touch), MP_ROM_PTR(&ddhw_touch_obj) },
    { MP_ROM_QSTR(MP_QSTR_boards), MP_ROM_PTR(&ddhw_boards_obj) },
};
static MP_DEFINE_CONST_DICT(ddhw_globals, ddhw_globals_table);

const mp_obj_module_t ddhw_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&ddhw_globals,
};

MP_REGISTER_MODULE(MP_QSTR_ddhw, ddhw_module);
