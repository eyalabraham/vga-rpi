/********************************************************************
 * uart.h
 *
 *  UART interface module.
 *  This module interfaces with the PCXT Z80-SIO Ch.B serial port
 *
 *  June 1, 2019
 *
 *******************************************************************/

#ifndef __uart_h__
#define __uart_h__

#include    <stdint.h>

#define     UART_Q_VGA      0
#define     UART_Q_OTHER1   1
#define     UART_Q_OTHER2   2
#define     UART_Q_SYSTEM   3

#pragma pack(1)

typedef union
{
    uint8_t data_bytes[7];
    struct
    {
        uint8_t cmd;
        uint8_t b1, b2, b3, b4, b5, b6;
    } uint8_param_t;
    struct
    {
        uint8_t cmd;
        uint8_t b1, b2;
        uint16_t w1, w2;
    } uint16_param_t;
} cmd_param_t;

typedef struct
{
    int         queue;
    cmd_param_t cmd_param;
} cmd_q_t;

/********************************************************************
 * Function prototypes
 *
 */
int      uart_init(void);
void     uart_close(void);
cmd_q_t* uart_get_cmd(void);
int      uart_recv_cmd(void);
void     uart_send(uint8_t);
int      uart_flush(void);
void     uart_rts_active(void);
void     uart_rts_not_active(void);

#endif      /* __uart_h__ */
