/********************************************************************
 * vga.c
 *
 *  Main module for PC/XT graphics card emulation on VGA on Raspberry Pi.
 *
 *  Usage:
 *      vga [ < -q | -e | -i | -v > ]
 *
 *  April 5, 2019
 *
 *******************************************************************/

#include    <ctype.h>
#include    <stdio.h>
#include    <stdlib.h>
#include    <unistd.h>

#include    "config.h"
#include    "util.h"
#include    "fb.h"
#include    "uart.h"

/********************************************************************
 * Module globals
 *
 */
cmd_q_t* command_q = 0;

/********************************************************************
 * main()
 *
 * return:  0 if ok
 *         -1 if any errors
 */
int main (int argc, char* argv[])
{
    int     c;

    // Process command line to extract test number
    opterr = 0;
    while ((c = getopt (argc, argv, "qeiv")) != -1)
    {
        switch (c)
        {
            // No output
            case 'q':
                debug_lvl(0);
                break;

            // Errors only
            case 'e':
                debug_lvl(1);
                break;

            // Errors and information
            case 'i':
                debug_lvl(2);
                break;

            // Verbose
            case 'v':
                debug_lvl(3);
                break;

            case '?':
                if (isprint(optopt))
                    debug(DB_ERR, "%s: unknown option `-%c'\n", __FUNCTION__, optopt);
                else
                    debug(DB_ERR, "%s: unknown option character `\\x%x'\n", __FUNCTION__, optopt);
                return -1;

            default:
                exit(-1);
        }
    }

    if ( fb_init(VGA_DEF_MODE) == 0 && uart_init() == 0 )
    {
        uart_flush();
        uart_rts_active();      // this signals a ready state to the PCXT

        /* VGA card emulator processing loop
         */
        while (1)
        {
            command_q = uart_get_cmd();

            if ( command_q )
            {
                /* Handle VGA emulation
                 */
                if ( command_q->queue == UART_Q_VGA )
                {
                    fb_emul(&(command_q->cmd_param));
                }
                /* Handle queue #1
                 */
                else if ( command_q->queue == UART_Q_OTHER1 )
                {
                }
                /* Handle queue #2
                 */
                else if ( command_q->queue == UART_Q_OTHER2 )
                {
                }
                /* Handle system commands
                 */
                else if ( command_q->queue == UART_Q_SYSTEM )
                {
                    if ( command_q->cmd_param.uint8_param_t.cmd == 255 )
                    {
                        debug(DB_INFO, "echo reply\n");
                        echo_reply();
                    }
                }
            }

            fb_cursor_blink();

            uart_recv_cmd();
        }
    }
    else
    {
        debug(DB_ERR, "%s: frame buffer and/or GPIO initialization failed\n", __FUNCTION__);
    }

    /* Shutdown; probably never get to this point
     */
    uart_rts_not_active();  // this signals a not ready state to the PCXT
    fb_close();
    uart_close();

	return 0;
}
