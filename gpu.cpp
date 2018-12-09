/*
 * P8X Game System
 * Copyright (c) 2016-2017 Marco Maccaferri
 *
 * MIT Licensed
 */

#include <uzebox/mode3.h>

int main()
{
    gpu_init();
    hs_rx_init();

    __asm__ (
        "    nop\n"
        "    nop\n"
    );

    while(1) {
        hs_rx(mailbox);
    }

    return 0;
}
