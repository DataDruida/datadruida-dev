/*
 * dd_ui.h — pantalla, sólo en placas con pantalla (LVGL 9).
 *
 * El programa dibuja dentro de la pestaña "Mi programa":
 *
 *     static lv_obj_t *lbl;
 *     void dd_setup(void) {
 *         if (dd_ui_lock()) {
 *             lbl = lv_label_create(dd_ui_root());
 *             lv_obj_set_style_text_font(lbl, dd_font(28), 0);
 *             dd_ui_unlock();
 *         }
 *     }
 *     void dd_loop(void) {
 *         if (dd_ui_lock()) { lv_label_set_text_fmt(lbl, "%.1f °C", t); dd_ui_unlock(); }
 *     }
 *
 * ⚠ Toda llamada lv_* va entre dd_ui_lock() y dd_ui_unlock(): LVGL corre en
 * otro hilo. Leer sensores FUERA del lock (si no, la pantalla se traba
 * mientras espera al sensor).
 */
#pragma once

#if defined(__has_include)
#if __has_include("lvgl.h")
#include "lvgl.h"
#define DD_HAS_SCREEN 1

lv_obj_t *dd_ui_root(void);          /* contenedor de la pestaña "Mi programa" */
bool dd_ui_lock(void);               /* espera hasta 1 s; false si no pudo */
void dd_ui_unlock(void);
const lv_font_t *dd_font(int px);    /* 16, 20 o 28 px, con acentos y ñ */
void dd_ui_show(void);               /* lleva la pantalla a "Mi programa" */

#endif
#endif
