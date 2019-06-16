/********************************************************************
 * fb.h
 *
 *  Frame buffer and graphics emulation module.
 *
 *  April 5, 2019
 *
 *******************************************************************/

#ifndef __fb_h__
#define __fb_h__

#include    <stdint.h>

#include    "uart.h"

#define     FB_BLACK            0
#define     FB_BLUE             1
#define     FB_GREEN            2
#define     FB_CYAN             3
#define     FB_RED              4
#define     FB_MAGENTA          5
#define     FB_BROWN            6
#define     FB_GRAY             7
#define     FB_DARK_GRAY        8
#define     FB_LIGHT_BLUE       9
#define     FB_LIGHT_GREEN      10
#define     FB_LIGHT_CYAN       11
#define     FB_LIGHT_RED        12
#define     FB_LIGHT_MAGENT     13
#define     FB_YELLOW           14
#define     FB_WHITE            15

// Monochrome attributes
#define     FB_ATTR_USECURRECT  0xff    // use existing attribute in screen buffer
#define     FB_ATTR_HIDE        0x00    // TODO: not displayed
#define     FB_ATTR_UNDERLIN    0x01    // TODO: underline
#define     FB_ATTR_NORMAL      0x07    // Normal
#define     FB_ATTR_HIGHINTUL   0x09    // TODO: high intensity underline
#define     FB_ATTR_HIGHINT     0x0f    // TODO: high intensity
#define     FB_ATTR_INV         0x70    // inverse

/********************************************************************
 * Function prototypes
 *
 */
int  fb_init(int);
void fb_close(void);
void fb_emul(cmd_param_t*);
void fb_cursor_blink();

#endif  /* __fb_h__ */
