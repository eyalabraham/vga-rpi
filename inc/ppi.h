/********************************************************************
 * ppi.h
 *
 *  Parallel port interface module.
 *  This module interfaces with the PCXT 8255.
 *
 *  April 16, 2019
 *
 *******************************************************************/

#ifndef __ppi_h__
#define __ppi_h__

#include    <stdint.h>

#define     PPI_Q_DBG      -1       // special debug indicator
#define     PPI_Q_VGA       0
#define     PPI_Q_SERIAL    1
#define     PPI_Q_OTHER1    2

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
        uint8_t b1, a2;
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
int      ppi_init(void);
void     ppi_close(void);
cmd_q_t* ppi_get(void);
void     ppi_recv(void);
void     ppi_send(uint8_t);

#endif  /* __ppi_h__ */
