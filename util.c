/********************************************************************
 * util.c
 *
 *  Utility and helper functions (debug print etc)
 *
 *  April 5, 2019
 *
 *******************************************************************/

#include    <stdio.h>
#include    <stdlib.h>
#include    <stdarg.h>

#include    "config.h"
#include    "util.h"
#include    "uart.h"

/********************************************************************
 * Definitions
 *
 */

/********************************************************************
 * Static function prototypes
 *
 */

/********************************************************************
 * Module globals (static)
 *
 */
static  int debug_level = UTIL_DEF_DBG_LVL;

/********************************************************************
 * debug_lvl()
 *
 *  Set global debug level
 *  0 no output
 *  1 errors only
 *  2 errors and info
 *
 *  param:  debug level
 *  return: none
 */
void debug_lvl(int level)
{
    debug_level = level;
}

/********************************************************************
 * debug()
 *
 *  Print debug messages
 *
 *  param:  debug type, printf-style debug string and parameters
 *  return: number of characters printed
 *          -1 fail
 */
int debug(int type, char *format, ...)
{
    va_list aptr;
    int     ret;

    if ( debug_level == 0 )
    {
        return 0;
    }
    else if ( debug_level == 1 && (type == DB_INFO || type == DB_VERBOSE) )
    {
        return 0;
    }
    else if ( debug_level == 2 && type == DB_VERBOSE )
    {
        return 0;
    }

    if ( type == DB_ERR )
        printf("ERR: ");

    va_start(aptr, format);
    ret = vprintf(format, aptr);
    va_end(aptr);

    return(ret);
}

/********************************************************************
 * echo_reply()
 *
 *  Reply to an echo command.
 *
 *  param:  none
 *  return: none
 */
void echo_reply(void)
{
    uint8_t     i;

    for (i = 6; i > 0; i--)
    {
        uart_send(i);
    }
}
