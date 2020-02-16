/********************************************************************
 * fb.c
 *
 *  Frame buffer and graphics emulation module.
 *
 *  April 5, 2019
 *
 *******************************************************************/

#include    <unistd.h>
#include    <stdio.h>
#include    <stdlib.h>
#include    <string.h>
#include    <fcntl.h>
#include    <time.h>
#include    <linux/fb.h>
#include    <linux/kd.h>
#include    <sys/mman.h>
#include    <sys/ioctl.h>

#include    "config.h"
#include    "util.h"
#include    "uart.h"
#include    "fb.h"
#include    "iv8x16u.h"
#include    "ic8x8u.h"
//#include    "im9x14u.h"

/********************************************************************
 * Definitions and types
 *
 */
#define     MODE_TX             1
#define     MODE_GR             2
#define     MODE_NO             0           // not implemented
#define     MAX_MODES           10

#define     FONT_UNDEF          0
#define     FONT_8X8            1
#define     FONT_8X16           2
#define     FONT_9X14           3

#define     TEXT_PAGE_MIRROR    10240       // do not change! max(160x64,40x25x8,80x25x4) uint16_t
#define     FB_CUR_BLINK_INT    250000      // in uSec
#define     FB_TRANSPARENT      255         // Special color definition
#define     FB_XOR_PIXEL        0x80        // Special pixel color XOR if but 7 is set

#define     FB_COMMAND          (emul_command->uint8_param_t.cmd)
#define     FB_PAGE             (emul_command->uint8_param_t.b1)
#define     FB_MODE             (emul_command->uint8_param_t.b1)
#define     FB_SCROLL_ROWS      (emul_command->uint8_param_t.b1)
#define     FB_CUR_TOP_LINE     (emul_command->uint8_param_t.b1)
#define     FB_PALETTE          (emul_command->uint8_param_t.b1)
#define     FB_CUR_BOT_LINE     (emul_command->uint8_param_t.b2)
#define     FB_CHARACTER        (emul_command->uint8_param_t.b2)
#define     FB_PIX_COLOR        (emul_command->uint8_param_t.b2)
#define     FB_TOP_LEFT_COL     (emul_command->uint8_param_t.b2)
#define     FB_PALETTE_ID       (emul_command->uint8_param_t.b2)
#define     FB_TOP_LEFT_ROW     (emul_command->uint8_param_t.b3)
#define     FB_CUR_COLUMN       (emul_command->uint8_param_t.b3)
#define     FB_CUR_ROW          (emul_command->uint8_param_t.b4)
#define     FB_BOT_RIGHT_COL    (emul_command->uint8_param_t.b4)
#define     FB_BOT_RIGHT_ROW    (emul_command->uint8_param_t.b5)
#define     FB_CHAR_ATTRIB      (emul_command->uint8_param_t.b6)
#define     FB_PIX_COL          (emul_command->uint16_param_t.w1)
#define     FB_PIX_ROW          (emul_command->uint16_param_t.w2)

struct mode
{
    int cols;
    int rows;
    int mode;
    int font;
    int pages;
};

/********************************************************************
 * Static function prototypes
 *
 */
static void fb_cursor_on_off(int);
static void fb_clear_screen(int);
static int  fb_set_tty(const int);
static void fb_clear_fbuffer_window(int, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t);
static void fb_clear_tbuffer_window(int, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t);
static void fb_draw_pixel(int, uint16_t, uint16_t, uint8_t);
static void fb_draw_char(int, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, int);
static void fb_put_char(int, uint8_t, uint8_t, uint8_t, uint8_t);
static void fb_scroll_fbuffer(uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t);
static void fb_scroll_tbuffer(uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t);
static void fb_get_char_and_attrib(uint8_t, uint8_t, uint8_t);
static void fb_put_pixel(int, uint8_t, uint16_t, uint16_t);
static void fb_get_pixel(int, uint16_t, uint16_t);

/********************************************************************
 * Module globals (static)
 *
 */
static int fbfd = 0;                        // frame buffer file descriptor
static struct fb_var_screeninfo var_info;
static struct fb_fix_screeninfo fix_info;
static long int screen_size = 0;
static int page_size = 0;
static int active_emulation = -1;
static int active_page = -1;
static uint8_t *fbp = 0;
static int  palette = 0;

static int font_w, font_h;
static uint8_t* font_img;

static uint16_t text_pages[TEXT_PAGE_MIRROR];

static int cursor_flag_show = 0;
static int cursor_start_line = 0;
static int cursor_end_line = 0;

static int cursor_row = 0;
static int cursor_column = 0;
static int cursor_row_prev = 0;
static int cursor_column_prev = 0;

static clock_t time_check;

/*  This structure holds the graphics mode emulation parameters:
 *  (in text mode, resolution should be calculated based on FONT_WxFONT_H size)
 *
 *   Notes:
 *   (1) mode 0 and 1 are the same, system used to turn off color for TV displays to show only B/W
 *       (example: GW-Basic's SCREEN command [colorswitch] argument)
 *   (2) mode 2 and 3 are the same, system used to turn off color for TV displays to show only B/W
 *       (example: GW-Basic's SCREEN command [colorswitch] argument)
 *   (3) modes 8 and 9 are special internal modes; 9 used for my 'new BIOS' monitor mode
 *
 *                                     col, row, mode,    font,         pages, INT 10h,00 reg AL
 */
static struct mode graphics_mode[] = {{40,  25,  MODE_TX, FONT_8X8,     8}, // 0
                                      {40,  25,  MODE_TX, FONT_8X8,     8}, // 1
                                      {80,  25,  MODE_TX, FONT_8X16,    4}, // 2
                                      {80,  25,  MODE_TX, FONT_8X16,    4}, // 3
                                      {40,  25,  MODE_GR, FONT_8X8,     1}, // 4
                                      {40,  25,  MODE_GR, FONT_8X8,     1}, // 5
                                      {80,  25,  MODE_GR, FONT_8X8,     1}, // 6
                                      {80,  25,  MODE_TX, FONT_8X16,    1}, // 7
                                                                            // ** only modes 0 to 7 are standard BIOS modes **
                                      {80,  25,  MODE_NO, FONT_UNDEF,   1}, // 8 Hercules high res graphics
                                      {160, 64,  MODE_TX, FONT_8X16,    1}  // 9 special mode for mon88
                                     };

/********************************************************************
 * fb_init()
 *
 *  Initialize the RPi frame buffer device.
 *
 *  param:  emulation type match BIOS INT 10h,00 reg AL,
 *          except for Hercules card in graphic mode
 *  return: 0 if no error,
 *         -1 if error initializing
 */
int fb_init(int emulation)
{
    int     x_pix, y_pix;

    if ( graphics_mode[emulation].mode == MODE_NO )
    {
        debug(DB_ERR, "%s: emulation type %d not supported\n", __FUNCTION__, emulation);
        return -1;
    }

    active_emulation = emulation;

    // Open the frame buffer device file for reading and writing
    if ( fbfd == 0 )
    {
        fbfd = open("/dev/fb0", O_RDWR);
        if (fbfd == -1)
        {
            debug(DB_ERR, "%s: cannot open frame buffer /dev/fb0\n", __FUNCTION__);
            return -1;
        }
    }

    debug(DB_INFO, "frame buffer device is open\n");

    // Get variable screen information
    if (ioctl(fbfd, FBIOGET_VSCREENINFO, &var_info))
    {
        debug(DB_ERR, "%s: error reading variable screen info\n", __FUNCTION__);
        return -1;
    }

    // Change screen resolution and color depth
    active_page = 0;
    x_pix = graphics_mode[emulation].cols;
    y_pix = graphics_mode[emulation].rows;
    if ( graphics_mode[emulation].font == FONT_8X8 )
    {
        font_w = 8;
        font_h = 8;
        font_img = &font_img8x8[0][0];
    }
    else if ( graphics_mode[emulation].font == FONT_8X16 )
    {
        font_w = 8;
        font_h = 16;
        font_img = &font_img8x16[0][0];
    }
/*
    else if ( graphics_mode[emulation].font == FONT_9X14 )
    {
        font_w = 9;
        font_h = 14;
        font_img = &font_img9x14[0][0];
    }
*/
    else
    {
        debug(DB_ERR, "%s: undefined font type\n", __FUNCTION__);
        return -1;
    }

    x_pix *= font_w;
    y_pix *= font_h;

    var_info.bits_per_pixel = 8;
    var_info.xres = x_pix;
    var_info.yres = y_pix;
    var_info.xres_virtual = x_pix;
    var_info.yres_virtual = y_pix * graphics_mode[emulation].pages;
    if ( ioctl(fbfd, FBIOPUT_VSCREENINFO, &var_info) )
    {
        debug(DB_ERR, "%s: error setting variable information\n", __FUNCTION__);
    }

    debug(DB_INFO, "display info: %dx%d, %d bpp, emulation %d\n",
           var_info.xres, var_info.yres,
           var_info.bits_per_pixel,
           active_emulation);

    // Get fixed screen information
    if ( ioctl(fbfd, FBIOGET_FSCREENINFO, &fix_info) )
    {
        debug(DB_ERR, "%s: error reading fixed information\n", __FUNCTION__);
        return -1;
    }

    debug(DB_INFO, "device ID: %s\n", fix_info.id);

    // map frame buffer to user memory
    screen_size = var_info.xres * var_info.yres_virtual * var_info.bits_per_pixel / 8;
    page_size = var_info.xres * var_info.yres;

    debug(DB_VERBOSE, "screen_size=%d, page_size=%d\n", screen_size, page_size);

    if ( screen_size > fix_info.smem_len )
    {
        debug(DB_ERR, "%s: screen_size over buffer limit\n", __FUNCTION__);
        return -1;
    }

    fbp = (uint8_t*)mmap(0,
                         screen_size,
                         PROT_READ | PROT_WRITE,
                         MAP_SHARED,
                         fbfd, 0);

    if ( (int)fbp == -1 )
    {
        debug(DB_ERR, "%s: failed to mmap()\n", __FUNCTION__);
        return -1;
    }

    // initialize time base
    time_check = clock();

    if ( fb_set_tty(1) )
    {
        debug(DB_ERR, "%s: could not set tty0 mode.\n", __FUNCTION__);
        return -1;
    }

    return 0;
}

/********************************************************************
 * fb_close()
 *
 *  Close the RPi frame buffer device.
 *
 *  param:  none
 *  return: none
 */
void fb_close(void)
{
    fb_set_tty(0);
    munmap(fbp, screen_size);
    close(fbfd);
}

/*------------------------------------------------
 * fb_emul()
 *
 * Display card emulation function.
 * Source: http://stanislavs.org/helppc/int_10.html
 *
 * param:  list of 8088 CPU registers matching INT 10h BIOS call
 * return: none
 *
 */
void fb_emul(cmd_param_t* emul_command)
{
    int         i;

    // Initialization check
    if ( active_emulation == -1 || active_page == -1 || page_size == 0 )
    {
        debug(DB_ERR, "%S: emulation is not initialized\n", __FUNCTION__);
        return;
    }

    debug(DB_VERBOSE, "fb_emul(): cmd=%d, b1=%d, b2=%d, b3=%d b4=%d, b5=%d, b6=%d\n",
                      FB_COMMAND,
                      emul_command->uint8_param_t.b1, emul_command->uint8_param_t.b2,
                      emul_command->uint8_param_t.b3, emul_command->uint8_param_t.b4,
                      emul_command->uint8_param_t.b5, emul_command->uint8_param_t.b6);

    // Process emulation commands
    if ( FB_COMMAND == UART_CMD_VID_MODE )
    {
        /* Set video mode
         *
         */
        fb_init(FB_MODE);
        for (i = 0; i < graphics_mode[active_emulation].pages; i++)
            fb_clear_screen(i);
    }
    else if ( FB_COMMAND == UART_CMD_DSP_PAGE )
    {
        /* Set display page
         *
         */
        if ( FB_PAGE < graphics_mode[active_emulation].pages )
        {
            active_page = FB_PAGE;

            var_info.yoffset = active_page * var_info.yres;
            var_info.activate = FB_ACTIVATE_VBL;
            if ( ioctl(fbfd, FBIOPAN_DISPLAY, &var_info) )
                debug(DB_ERR, "%S: error changing display page\n", __FUNCTION__);
        }
    }
    else if ( FB_COMMAND == UART_CMD_CUR_POS )
    {
        /* Cursor position
         *
         */
        if ( FB_PAGE >= graphics_mode[active_emulation].pages )
            return;

        if ( FB_CUR_COLUMN < graphics_mode[active_emulation].cols )
            cursor_column = FB_CUR_COLUMN;

        if ( FB_CUR_ROW < graphics_mode[active_emulation].rows )
            cursor_row = FB_CUR_ROW;
    }
    else if ( FB_COMMAND == UART_CMD_CUR_MODE )
    {
        /* Cursor size/mode from scan lines
         *
         */
        cursor_start_line = FB_CUR_TOP_LINE;
        cursor_end_line = FB_CUR_BOT_LINE;
        if ( cursor_start_line == 0x20 && cursor_end_line == 0x00 )
            cursor_flag_show = 0;
        else
            cursor_flag_show = 1;
    }
    else if ( FB_COMMAND == UART_CMD_PUT_CHRA )
    {
        /* Put character on the display
         *
         */
        fb_put_char(FB_PAGE, FB_CUR_COLUMN, FB_CUR_ROW, FB_CHARACTER, FB_CHAR_ATTRIB);
    }
    else if ( FB_COMMAND == UART_CMD_GET_CHR )
    {
        /* Get character and attribute at cursor position
         *
         */
        fb_get_char_and_attrib(FB_PAGE, FB_CUR_COLUMN, FB_CUR_ROW);
    }
    else if ( FB_COMMAND == UART_CMD_PUT_CHR )
    {
        /* Put character on the display
         *
         */
        fb_put_char(FB_PAGE, FB_CUR_COLUMN, FB_CUR_ROW, FB_CHARACTER, FB_ATTR_USECURRECT);
    }
    else if ( FB_COMMAND == UART_CMD_SCR_UP )
    {
        /* Scroll up
         *
         */
        fb_cursor_on_off(0);
        fb_scroll_fbuffer(0, FB_TOP_LEFT_COL, FB_TOP_LEFT_ROW, FB_BOT_RIGHT_COL, FB_BOT_RIGHT_ROW, FB_SCROLL_ROWS, FB_CHAR_ATTRIB);
        fb_scroll_tbuffer(0, FB_TOP_LEFT_COL, FB_TOP_LEFT_ROW, FB_BOT_RIGHT_COL, FB_BOT_RIGHT_ROW, FB_SCROLL_ROWS, FB_CHAR_ATTRIB);
    }
    else if ( FB_COMMAND == UART_CMD_SCR_DOWN )
    {
        /* Scroll down
         *
         */
        fb_cursor_on_off(0);
        fb_scroll_fbuffer(1, FB_TOP_LEFT_COL, FB_TOP_LEFT_ROW, FB_BOT_RIGHT_COL, FB_BOT_RIGHT_ROW, FB_SCROLL_ROWS, FB_CHAR_ATTRIB);
        fb_scroll_tbuffer(1, FB_TOP_LEFT_COL, FB_TOP_LEFT_ROW, FB_BOT_RIGHT_COL, FB_BOT_RIGHT_ROW, FB_SCROLL_ROWS, FB_CHAR_ATTRIB);
    }
    else if ( FB_COMMAND == UART_CMD_PUT_PIX )
    {
        /* Put pixel
         *
         */
        fb_put_pixel(FB_PAGE, FB_PIX_COLOR, FB_PIX_COL, FB_PIX_ROW);
    }
    else if ( FB_COMMAND == UART_CMD_GET_PIX )
    {
        /* Get pixel
         *
         */
        fb_get_pixel(FB_PAGE, FB_PIX_COL, FB_PIX_ROW);
    }
    else if ( FB_COMMAND == UART_CMD_PALETTE )
    {
        /* Set color palette
         *
         */
        palette = FB_PALETTE;       // TODO trust BIOS or range check?
    }
    else if ( FB_COMMAND == UART_CMD_CLR_SCR )
    {
        /* Clear screen
         *
         */
        fb_clear_screen(FB_PAGE);
    }
    else
    {
        debug(DB_ERR, "%s: emulation function %d not supported\n", __FUNCTION__, FB_COMMAND);
    }
}

/*------------------------------------------------
 * fb_cursor_blink()
 *
 *  Call periodically to position and blink cursor
 *
 * param:  none
 * return: none
 *
 */
void fb_cursor_blink()
{
    clock_t     elapsed;

    static uint8_t cursor_on = 0;

    // initialization check
    if ( active_emulation == -1 || active_page == -1 || page_size == 0 )
    {
        return;
    }

    // No cursor in graphics modes
    if ( graphics_mode[active_emulation].mode == MODE_NO ||
         graphics_mode[active_emulation].mode == MODE_GR )
    {
        return;
    }

    // handle cursor display and blinking
    if ( cursor_flag_show == 0 )
    {
        // Turn cursor off
        if ( cursor_on )
        {
            fb_cursor_on_off(0);
            cursor_on = 0;
        }
    }
    else
    {
        // position and blink cursor
        elapsed = (clock() - time_check);

        if ( elapsed > FB_CUR_BLINK_INT )
        {
            // if cursor moved turn off the current cursor location
            // otherwise toggle cursor state at blink rate
            if ( cursor_column != cursor_column_prev || cursor_row != cursor_row_prev )
                cursor_on = 0;
            else
                cursor_on = cursor_on ? 0 : 1;

            fb_cursor_on_off(cursor_on);

            cursor_column_prev = cursor_column;
            cursor_row_prev = cursor_row;

            time_check = clock();
        }
    }
}

/*------------------------------------------------
 * fb_cursor_on_off()
 *
 *  Turn cursor on or off.
 *
 * param:  1=on, 0=off
 * return: none
 *
 */
void fb_cursor_on_off(int cursor_state)
{
    int         page_offset;
    uint8_t     cur_attr, cur_char, fg_color, bg_color;

    // retrieve character attribute at cursor position
    page_offset = active_page * graphics_mode[active_emulation].cols * graphics_mode[active_emulation].rows +
                  cursor_column_prev +
                  (cursor_row_prev * graphics_mode[active_emulation].cols);

    cur_char = (uint8_t)(text_pages[page_offset] & 0x00ff);
    cur_attr = (uint8_t)((text_pages[page_offset] >> 8) & 0x00ff);

    // color settings
    if ( active_emulation >= 0 && active_emulation <= 3 )
    {
        fg_color = cur_attr & 0x0f;
        bg_color = (cur_attr >> 4) & 0x0f;
    }
    // Ignore in graphics modes
    else if ( active_emulation >= 4 && active_emulation <= 6 )
    {
        /* No cursor in graphics modes, so do nothing.
         * This is here just for protection and as place holder.
         *
         */
    }
    // Monochrome text modes
    else if ( active_emulation == 7 || active_emulation == 9 )
    {
        if ( cur_attr == FB_ATTR_HIGHINTUL || cur_attr == FB_ATTR_HIGHINT )
            fg_color = VGA_DEF_MONO_HFG_TXT;
        else if ( cur_attr == FB_ATTR_HIDE )
            fg_color = VGA_DEF_MONO_BG_TXT;
        else
            fg_color = VGA_DEF_MONO_FG_TXT;

        bg_color = VGA_DEF_MONO_BG_TXT;
    }
    else
    {
        debug(DB_ERR, "%s: invalid page emulation %d\n", __FUNCTION__, active_emulation);
        return;
    }

    // Redraw the character
    fb_draw_char(active_page, cursor_column_prev, cursor_row_prev, cur_char, fg_color, bg_color, cur_attr, cursor_state);
}

/********************************************************************
 * fb_clear_screen()
 *
 *  Clear the selected page to selected color/attribute.
 *  Display frame buffer and, if in text mode, also the text buffer are cleared.
 *  Cursor is reset to 0,0.
 *
 *  param:  page number, color, and text attribute
 *  return: none
 */
void fb_clear_screen(int page)
{
    int         i, text_page_offset, text_page_size;
    uint16_t    attr_char;

    // range checks
    if ( page >= graphics_mode[active_emulation].pages )
    {
        debug(DB_ERR, "%s: invalid page number %d\n", __FUNCTION__, page);
        return;
    }

    // color settings
    if ( active_emulation >= 0 && active_emulation <= 3 )
        attr_char = (((uint16_t)VGA_DEF_COLR_BG_TXT << 12) + ((uint16_t)VGA_DEF_COLR_FG_TXT << 8)) + 32;
    else if ( active_emulation >= 4 && active_emulation <= 6 )
        attr_char = 32;
    else if ( active_emulation == 7 || active_emulation == 9 )
        attr_char = ((uint16_t)FB_ATTR_NORMAL << 8) + 32;
    else
    {
        debug(DB_ERR, "%s: invalid page emulation %d\n", __FUNCTION__, active_emulation);
        return;
    }

    // clear shadow text page graphics frame buffer
    text_page_size = graphics_mode[active_emulation].cols * graphics_mode[active_emulation].rows;
    text_page_offset = page * text_page_size;

    for (i = 0; i < text_page_size; i++)
        text_pages[text_page_offset + i] = attr_char;

    memset(fbp + active_page * page_size, FB_BLACK, page_size);

    cursor_row = 0;
    cursor_column = 0;
    cursor_row_prev = 0;
    cursor_column_prev = 0;
}

/********************************************************************
 * fb_set_tty()
 *
 *  Set screen to graphics mode.
 *
 *  param:  tty mode text=0, graphics=1
 *  return: 0 no error
 *         -1 on error
 */
int fb_set_tty(const int mode)
{
    int     console_fd;
    int     result = 0;

    console_fd = open("/dev/tty0", O_RDWR);

    if ( !console_fd )
    {
        debug(DB_ERR, "%s: could not open console.\n", __FUNCTION__);
        return -1;
    }

    if ( mode )
    {
        if (ioctl( console_fd, KDSETMODE, KD_GRAPHICS))
        {
            debug(DB_ERR, "%s: could not set console to KD_GRAPHICS mode.\n", __FUNCTION__);
            result = -1;
        }
    }
    else
    {
        if (ioctl( console_fd, KDSETMODE, KD_TEXT))
        {
            debug(DB_ERR, "%s: could not set console to KD_TEXT mode.\n", __FUNCTION__);
            result = -1;
        }
    }

    close(console_fd);

    return result;
}

/********************************************************************
 * fb_clear_fbuffer_window()
 *
 *  Clear a window in a frame buffer page to a selected color.
 *
 *  param:  page number, window in character coordinates, color
 *  return: none
 */
void fb_clear_fbuffer_window(int page, uint8_t color, uint8_t tl_col, uint8_t tl_row, uint8_t br_col, uint8_t br_row)
{
    int     i, cols, rows, count, pixel_offset;
    void*   fb_from;

    // Simple range check
    cols = (int)(br_col - tl_col + 1);
    rows = (int)(br_row - tl_row + 1);
    if ( cols <= 0 || rows <= 0 || active_page == -1 )
    {
        debug(DB_ERR, "%s: invalid frame buffer clear request\n", __FUNCTION__);
        return;
    }

    count = rows * font_h;

    // Clear the window
    pixel_offset = (active_page * page_size ) +
                   (tl_col * font_w) +
                   (tl_row * font_h * var_info.xres);
    fb_from = (void*)(fbp + pixel_offset);

    for ( i = 0; i < count; i++ )
    {
        pixel_offset = i * var_info.xres;
        memset((void*)((uint8_t*)fb_from + pixel_offset), color, (cols*font_w));
    }
}

/********************************************************************
 * fb_clear_tbuffer_window()
 *
 *  Clear a window in a text buffer page to a selected attribute.
 *
 *  param:  page number, window in character coordinates, attribute
 *  return: none
 */
void fb_clear_tbuffer_window(int page, uint8_t tl_col, uint8_t tl_row, uint8_t br_col, uint8_t br_row, uint8_t attrib)
{
    int     i, j, tb_to, char_offset, cols, rows;

    // Simple range check
    cols = (int)(br_col - tl_col + 1);
    rows = (int)(br_row - tl_row + 1);
    if ( cols <= 0 || rows <= 0 || active_page == -1 )
    {
        debug(DB_ERR, "%s: invalid text buffer clear request\n", __FUNCTION__);
        return;
    }

    // Clear vacated rows with space character and attribute
    tb_to = (page * graphics_mode[active_emulation].cols *  graphics_mode[active_emulation].rows) +
             tl_col +
             tl_row * graphics_mode[active_emulation].cols;

    for ( i = 0; i < rows; i++ )
    {
        for ( j = 0; j < cols; j++ )
        {
            char_offset = i * graphics_mode[active_emulation].cols;
            text_pages[tb_to + char_offset + j] = ((uint16_t)attrib << 8) + 0x20;
        }
    }
}

/********************************************************************
 * fb_draw_pixel()
 *
 *  Plot a pixel in given color.
 *
 *  param:  page number, coordinates, and color
 *  return: none
 */
void fb_draw_pixel(int page, uint16_t x, uint16_t y, uint8_t color)
{
    uint32_t    pix_offset;

    // skip entire process of color is 'transparent'
    if ( color == FB_TRANSPARENT )
        return;

    // range checks
    if ( color > FB_WHITE || color < FB_BLACK )
        return;

    if ( page >= graphics_mode[active_emulation].pages )
        return;

    // calculate the pixel's byte offset inside the buffer
    pix_offset = x + y * var_info.xres;

    // offset by the current buffer start
    pix_offset += active_page * page_size;

    // The same as 'fbp[pix_offset] = value'
    *((uint8_t*)(fbp + pix_offset)) = color;
}

/*------------------------------------------------
 * fb_draw_char()
 *
 * Draw a character in the frame buffer.
 *
 * param:  page          display page number
 *         x             horizontal position of the top left corner of the character, columns from the left edge
 *         y             vertical position of the top left corner of the character, rows from the top edge
 *         c             character to be printed
 *         fg_color      foreground color of the character
 *         bg_color      background color of the character
 *         attribute     character attribute for monochrome modes
 *         cursor_on     draw character with or without cursor
 * return: none
 *
 */
void fb_draw_char(int page, uint8_t x, uint8_t y, uint8_t c,
                  uint8_t fg_color, uint8_t bg_color, uint8_t attribute,
                  int cursor_on)
{
    uint8_t     pix_pos, bit_pattern;
    int         col, row;
    int         px, py;
    int         bit_pattern_index;

    // range checks
    if ( fg_color > FB_WHITE )
        return;

    if ( bg_color > FB_WHITE && bg_color != FB_TRANSPARENT )
        return;

    if ( page >= graphics_mode[active_emulation].pages )
        return;

    // print character rows starting at the top row
    // print the pixel columns, starting on the left
    for ( row = 0; row < font_h; row++ )
    {
        pix_pos = 0x80;
        py = y * font_h + row;

        bit_pattern_index = (int)c * font_h + row;
        bit_pattern = font_img[bit_pattern_index];

        // adjust attributes for monochrome text modes
        if ( active_emulation == 7 || active_emulation == 9 )
        {
            // underline mode
            if ( (row == font_h - 2) && (attribute == FB_ATTR_UNDERLIN || attribute == FB_ATTR_HIGHINTUL) )
                bit_pattern = 0xff;
            else if ( attribute == FB_ATTR_INV )
                bit_pattern = ~bit_pattern;
        }

        // Adjust foreground and background to render cursor
        if ( cursor_on &&
             row >= cursor_start_line && row <= cursor_end_line )
        {
            bit_pattern = ~bit_pattern;
        }

        // TODO NOTE: only cycles through 8 pixel bits even for 9-pix font width
        for ( col = 0; col < 8; col++ )
        {
            // calculate pixel position
            px = x * font_w + col;

            // Bit is set in Font, print pixel(s) in text color
            if ( (bit_pattern & pix_pos) )
            {
                fb_draw_pixel(page, px, py, fg_color);
            }
            // Bit is cleared in Font
            else
            {
                fb_draw_pixel(page, px, py, bg_color);
            }

            // move to the next pixel position
            pix_pos = pix_pos >> 1;
        }
    }
}

/*------------------------------------------------
 * fb_put_char()
 *
 *  Put a character in a page.
 *  This function updates the frame buffer *and*, in text mode, also the appropriate text page.
 *
 * param:  page          display page number
 *         x             horizontal position of the top left corner of the character, columns from the left edge
 *         y             vertical position of the top left corner of the character, rows from the top edge
 *         c             character to be printed
 *         attribute     character attribute for monochrome modes
 * return: none
 *
 */
void fb_put_char(int page, uint8_t x, uint8_t y, uint8_t c, uint8_t attribute)
{
    int         page_offset;
    uint8_t     cur_attr;
    uint16_t    attr_char;
    uint8_t     fg_color, bg_color;

    if ( page < graphics_mode[active_emulation].pages )
    {
        page_offset = page * graphics_mode[active_emulation].cols * graphics_mode[active_emulation].rows +
                      x + (y * graphics_mode[active_emulation].cols);

        /* In all text modes adjust the foreground and background colors
         * per selected attributes and text mode of color or monochrome
         */
        if ( graphics_mode[active_emulation].mode == MODE_TX )
        {
            if ( attribute == FB_ATTR_USECURRECT )
                cur_attr = (uint8_t)(text_pages[page_offset] >> 8);
            else
                cur_attr = attribute;

            attr_char = ((uint16_t)cur_attr << 8) + c;

            // Monochrome text attributes
            if ( active_emulation == 7 || active_emulation == 9 )
            {
                if ( cur_attr == FB_ATTR_HIGHINTUL || cur_attr == FB_ATTR_HIGHINT)
                    fg_color = VGA_DEF_MONO_HFG_TXT;
                else if ( cur_attr == FB_ATTR_HIDE )
                    fg_color = VGA_DEF_MONO_BG_TXT;
                else
                    fg_color = VGA_DEF_MONO_FG_TXT;

                bg_color = VGA_DEF_MONO_BG_TXT;
            }
            // Color text attributes
            else
            {
                fg_color = (cur_attr & 0x0f);
                bg_color = ((cur_attr >> 4) & 0x0f);
            }
        }

        /* In graphics modes (4, 5, and 6) only save the character code in the shadow text page
         * and draw the character with a transparent background over the graphics page
         * using the color provided in the attribute byte
         */
        else
        {
            attr_char = ((uint16_t)attribute << 8) + c;
            fg_color = attribute;
            if ( active_emulation == 6 )
                fg_color = ( fg_color > 0 ) ? FB_WHITE : FB_BLACK;  // adjust for monochrome mode
            bg_color = FB_TRANSPARENT;
            cur_attr = FB_ATTR_USECURRECT;
        }

        text_pages[page_offset] = attr_char;
        fb_draw_char(page, x, y, c, fg_color, bg_color, cur_attr, 0);
    }
    else
    {
        debug(DB_ERR, "%s: invalid page number %d\n", __FUNCTION__, page);
        return;
    }
}

/*------------------------------------------------
 * fb_scroll_fbuffer()
 *
 *  Scroll the displayed active page from the frame buffer.
 *
 * param:  dir           up=0 or down=1
 *         tl_ , br_     window to scroll in character coordinates
 *         count         scroll count in character rows
 *         attrib        attribute to fill in cleared rows
 * return: none
 *
 */
void fb_scroll_fbuffer(uint8_t dir, uint8_t tl_col, uint8_t tl_row, uint8_t br_col, uint8_t br_row, uint8_t count, uint8_t attrib)
{
    int     i, cols, rows, pixel_offset;
    uint8_t fill_color;
    void*   fb_from;
    void*   fb_to;

    // Simple range check
    cols = (int)(br_col - tl_col + 1);
    rows = (int)(br_row - tl_row + 1);
    if ( cols <= 0 || rows <= 0 || active_page == -1 )
    {
        debug(DB_ERR, "%s: invalid frame buffer scroll request\n", __FUNCTION__);
        return;
    }

    // Extract the fill color of the cleared rows from the attribute
    if ( active_emulation >= 0 && active_emulation <= 3 )
    {
        fill_color = ((attrib >> 4) & 0x0f);
    }
    else if ( active_emulation >= 4 && active_emulation <= 6 )
    {
        fill_color = attrib;
    }
    else if ( active_emulation == 7 || active_emulation == 9 )
    {
        // In a monochrome text mode use the background of normal, high intensity or inverse video
        if ( attrib == FB_ATTR_INV)
            fill_color = VGA_DEF_MONO_FG_TXT;
        else
            fill_color = VGA_DEF_MONO_BG_TXT;
    }

    if ( count >= (br_row - tl_row + 1) || count == 0 )
    {
        // Simply clear the window with 'fill_color'
        fb_clear_fbuffer_window(active_page, fill_color, tl_col, tl_row, br_col, br_row);
    }
    else
    {
        if ( dir == 0 )
        {
            // Scroll pixel rows up
            pixel_offset = (active_page * page_size ) +
                           (tl_col * font_w) +
                           ((tl_row + count) * font_h * var_info.xres);
            fb_from = (void*)(fbp + pixel_offset);

            pixel_offset = (active_page * page_size ) +
                           (tl_col * font_w) +
                           (tl_row * font_h * var_info.xres);
            fb_to = (void*)(fbp + pixel_offset);

            for ( i = 0; i < ((rows - count) * font_h); i++ )
            {
                pixel_offset = i * var_info.xres;
                memmove((void*)((uint8_t*)fb_to + pixel_offset), (void*)((uint8_t*)fb_from + pixel_offset), (cols*font_w));
            }

            // Clear vacated rows with 'fill_color'
            fb_clear_fbuffer_window(active_page, fill_color, tl_col, (br_row-count+1), br_col, br_row);
        }
        else
        {
            // Scroll pixel rows down
            pixel_offset = (active_page * page_size ) +
                           (tl_col * font_w) +
                           ((((br_row + 1 - count) * font_h) - 1) * var_info.xres);
            fb_from = (void*)(fbp + pixel_offset);

            pixel_offset = (active_page * page_size ) +
                           (tl_col * font_w) +
                           ((((br_row + 1) * font_h) - 1) * var_info.xres);
            fb_to = (void*)(fbp + pixel_offset);

            for ( i = 0; i < ((rows - count) * font_h); i++ )
            {
                pixel_offset = i * var_info.xres;
                memmove((void*)((uint8_t*)fb_to - pixel_offset), (void*)((uint8_t*)fb_from - pixel_offset), (cols*font_w));
            }

            // Clear vacated rows with 'fill_color'
            fb_clear_fbuffer_window(active_page, fill_color, tl_col, tl_row, br_col, (tl_row+count-1));
        }
    }
}


/*------------------------------------------------
 * fb_scroll_tbuffer()
 *
 *  Scroll the text buffer of the active page.
 *
 * param:  dir           up=0 or down=1
 *         tl_ , br_     window to scroll in character coordinates
 *         count         scroll count in character rows
 *         attrib        attribute to fill in cleared rows
 * return: none
 *
 */
void fb_scroll_tbuffer(uint8_t dir, uint8_t tl_col, uint8_t tl_row, uint8_t br_col, uint8_t br_row, uint8_t count, uint8_t attrib)
{
    int     i, j;
    int     cols, rows, char_offset, tb_from, tb_to;

    // Simple range check
    cols = (int)(br_col - tl_col + 1);
    rows = (int)(br_row - tl_row + 1);
    if ( cols <= 0 || rows <= 0 || active_page == -1 )
    {
        debug(DB_ERR, "%s: invalid text buffer scroll request\n", __FUNCTION__);
        return;
    }

    // scroll up down or clear the screen
    if ( count >= (br_row - tl_row + 1) || count == 0 )
    {
        // Simply clear the window with 'fill_color'
        fb_clear_tbuffer_window(active_page, tl_col, tl_row, br_col, br_row, attrib);
    }
    else
    {
        if ( dir == 0 )
        {
            // Scroll character and attribute rows up
            tb_from = (active_page * graphics_mode[active_emulation].cols *  graphics_mode[active_emulation].rows) +
                       tl_col +
                      (tl_row + count) * graphics_mode[active_emulation].cols;

            tb_to = (active_page * graphics_mode[active_emulation].cols *  graphics_mode[active_emulation].rows) +
                     tl_col +
                    (tl_row * graphics_mode[active_emulation].cols);

            for ( i = 0; i < (rows - count); i++ )
            {
                for ( j = 0; j < cols; j++ )
                {
                    char_offset = i * graphics_mode[active_emulation].cols;
                    text_pages[tb_to + char_offset + j] = text_pages[tb_from + char_offset + j];
                }
            }

            fb_clear_tbuffer_window(active_page, tl_col, (br_row-count+1), br_col, br_row, attrib);
        }
        else
        {
            // Scroll character and attribute rows down
            tb_from = (active_page * graphics_mode[active_emulation].cols *  graphics_mode[active_emulation].rows) +
                       tl_col +
                      (br_row - count) * graphics_mode[active_emulation].cols;

            tb_to = (active_page * graphics_mode[active_emulation].cols *  graphics_mode[active_emulation].rows) +
                     tl_col +
                    (br_row * graphics_mode[active_emulation].cols);

            for ( i = 0; i < (rows - count); i++ )
            {
                for ( j = 0; j < cols; j++ )
                {
                    char_offset = i * graphics_mode[active_emulation].cols;
                    text_pages[tb_to - char_offset + j] = text_pages[tb_from - char_offset + j];
                }
            }

            fb_clear_tbuffer_window(active_page, tl_col, tl_row, br_col, (tl_row+count-1), attrib);
        }
    }
}

/*------------------------------------------------
 * fb_get_char_and_attrib()
 *
 *  Retrieve and send character and attribute of
 *  character at specified position.
 *
 * param:  page number
 *         column and row of cursor position
 * return: none
 *
 */
void fb_get_char_and_attrib(uint8_t page, uint8_t column, uint8_t row)
{
    int         page_offset;
    uint8_t     character, attribute;

    if ( page >= graphics_mode[active_emulation].pages ||
         column >= graphics_mode[active_emulation].cols ||
         row >= graphics_mode[active_emulation].rows )
    {
        debug(DB_ERR, "%s: invalid page, or cursor position\n", __FUNCTION__);
        character = 0;
        attribute = 0;
    }
    else
    {
        // Retrieve character attribute at cursor position
        page_offset = page * graphics_mode[active_emulation].cols * graphics_mode[active_emulation].rows +
                      column +
                      (row * graphics_mode[active_emulation].cols);

        character = (uint8_t)(text_pages[page_offset] & 0x00ff);
        attribute = (uint8_t)((text_pages[page_offset] >> 8) & 0x00ff);
    }

    uart_send(attribute);
    uart_send(character);
}

/*------------------------------------------------
 * fb_put_pixel()
 *
 *  Put a pixel on a graphics-mode screen
 *
 * param:  page number, pixel color, x and y coordinates
 * return: none
 *
 */
void fb_put_pixel(int page, uint8_t color, uint16_t x, uint16_t y)
{
    uint8_t     c, pixel_color;
    int         do_color_xor, pixel_offset;

    if (  graphics_mode[active_emulation].mode != MODE_GR )
    {
        debug(DB_ERR, "%s: invalid mode\n", __FUNCTION__);
        return;
    }

    do_color_xor = color & FB_XOR_PIXEL;    // check if XOR required
    c = color & ~FB_XOR_PIXEL;              // isolate color

    if ( c != FB_BLACK )
    {
        if ( active_emulation == 4 || active_emulation == 5 )
        {
            c = ((c << 1) + palette) & 0x07;
        }
        else if ( active_emulation == 6 )
        {
            c = (c != 0) ? FB_WHITE : FB_BLACK;
        }
    }

    if ( do_color_xor )
    {
        // calculate the pixel's byte offset inside the buffer
        pixel_offset = x + y * var_info.xres;
        pixel_offset += page * page_size;

        // Get the pixel's color value and use it to XOR with new color
        pixel_color = *((uint8_t*)(fbp + pixel_offset));
        c ^= pixel_color;
    }

    fb_draw_pixel(page, x, y, c);
}

/*------------------------------------------------
 * fb_get_pixel()
 *
 *  Get a pixel's color value from a graphics-mode screen
 *
 * param:  page number, x and y coordinates
 * return: send color value through UART
 *
 */
void fb_get_pixel(int page, uint16_t x, uint16_t y)
{
    int     pixel_offset;
    uint8_t color = 0;

    // range checks
    if ( page >= graphics_mode[active_emulation].pages ||
         x > (graphics_mode[active_emulation].cols * font_w) ||
         y > (graphics_mode[active_emulation].rows * font_h) )
    {
        debug(DB_ERR, "%s: invalid mode, or page, or pixel position\n", __FUNCTION__);
        color = 0;
    }
    else
    {
        // calculate the pixel's byte offset inside the buffer
        pixel_offset = x + y * var_info.xres;

        // offset by the current buffer start
        pixel_offset += page * page_size;

        // Get the pixel's color value
        color = *((uint8_t*)(fbp + pixel_offset));
    }

    uart_send(color);
}
