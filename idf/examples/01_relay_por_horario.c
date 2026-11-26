/*
 * Hola Mundo 1 — Relé por horario
 * Placa: cualquiera con relés (ETH-8DI-8RO, Relay-6CH, Relay-1CH) · Sin sensores
 * Prende el relé 1 de 08:00 a 20:00 (luz, por ejemplo). Sin hora confiable
 * (todavía sin internet) queda apagado: es lo seguro.
 */
#include "dd.h"

void dd_setup(void)
{
    dd_every(10000);
}

void dd_loop(void)
{
    bool luz = dd_in_window("08:00", "20:00");
    dd_relay_set(1, luz);
}
