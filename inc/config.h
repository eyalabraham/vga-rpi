/********************************************************************
 * config.h
 *
 *  Header file that defines the hardware configuration and GPIO
 *  pin connectivity/function.
 *
 *  June 1, 2019
 *
 *******************************************************************/

#ifndef __config_h__
#define __config_h__

#include    <bcm2835.h>

#include    "fb.h"

/********************************************************************
 *  Emulation
 */
#define     VGA_DEF_MODE            7               // default video mode emulation

#define     VGA_DEF_MONO_FG_TXT     FB_GREEN        // default text mode foreground in monochrome modes
#define     VGA_DEF_MONO_HFG_TXT    FB_LIGHT_GREEN  // default text mode high intensity foreground in monochrome modes
#define     VGA_DEF_MONO_BG_TXT     FB_BLACK        // default text mode background in monochrome modes

#define     VGA_DEF_COLR_FG_TXT     FB_GRAY         // default text mode foreground in color modes
#define     VGA_DEF_COLR_BG_TXT     FB_BLACK        // default text mode background in color modes

/********************************************************************
 *  UART
 */
#define     UART_TEST_CMD       0                   // *** make sure this is '0' for non-test setup ***

#define     UART_UART0          "/dev/serial0"      // default serial link on RPi Zero
#define     UART_BAUD           B57600
#define     UART_BITS           8
#define     UART_RTS            RPI_V2_GPIO_P1_11   // GPIO17 pin.11

/********************************************************************
 *  Debug
 */
#define     UTIL_DEF_DBG_LVL    3                   // 0=quiet, 1=errors, 2=information, 3=verbose

#endif  /* __config_h__ */
