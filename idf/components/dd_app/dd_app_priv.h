#pragma once
#include "dd.h"
#include "dd_app.h"

typedef struct {
    const char *id;
    const char *name;
    int  relay_count;
    bool tca9554;          /* relés por expansor I2C (si no, GPIO directo) */
    int  sda, scl;
    int  relay_pins[8];
    int  input_count;
    int  input_pins[8];    /* contacto seco contra GND: 0 = cerrado */
    int  rs485_tx, rs485_rx, rs485_de;   /* -1 = no hay / dirección automática */
} dd_board_t;

extern const dd_board_t DD_BOARDS[];
extern const int DD_N_BOARDS;

const dd_board_t *dd_board(void);     /* NULL si la placa no está elegida */
esp_err_t dd_board_init(void);
void dd_relays_heal_and_limits(void); /* cada 1 s, desde el runtime */

void dd_clock_start(void);
void dd_log_init(void);
void dd_web_routes(httpd_handle_t s);
void dd_web_status(void *cjson);
