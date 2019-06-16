/********************************************************************
 * uart.c
 *
 *  UART interface module.
 *  This module interfaces with the PCXT Z80-SIO Ch.B serial port
 *
 *  June 1, 2019
 *
 *******************************************************************/

#include    <string.h>
#include    <unistd.h>
#include    <fcntl.h>
#include    <errno.h>
#include    <termios.h>
#include    <bcm2835.h>         // use for software controlled CTS and RTS

#include    "config.h"
#include    "uart.h"
#include    "util.h"

#define     UART_CMD_Q_LEN       10

#define     SLIP_END            0xC0        // start and end of every packet
#define     SLIP_ESC            0xDB        // escape start (one byte escaped data follows)
#define     SLIP_ESC_END        0xDC        // following escape: original byte is 0xC0 (END)
#define     SLIP_ESC_ESC        0xDD        // following escape: original byte is 0xDB (ESC)

/********************************************************************
 * Module static functions
 *
 */
static int uart_set_interface_attr(int, int);
static int uart_set_blocking(int, int);

static  int         uart_module_initialized = 0;

static  cmd_q_t     command_queue[UART_CMD_Q_LEN];
static  int         cmd_in = 0;
static  int         cmd_out = 0;
static  int         cmd_count = 0;

/********************************************************************
 * Module globals
 *
 */
static int   uart_fd;

/********************************************************************
 * uart_init()
 *
 * Initialize the UART and GPIO subsystems of BCM2835.
 * Failure to initialize any of the above three IO subsystems
 * will result in closing all open IO devices and exiting with an error.
 *
 *  param:  none
 *  return: 0 if no error,
 *         -1 if error initializing any subcomponent or library
 *
 */
int uart_init(void)
{
    // initialize GPIO subsystem
    if ( !bcm2835_init() )
    {
        debug(DB_ERR, "%s: bcm2835_init failed. Are you running as root?\n", __FUNCTION__);
        return -1;
    }

    debug(DB_INFO, "initialized GPIO\n");

    // initialize RTS GPIO pin
    bcm2835_gpio_write(UART_RTS, HIGH);
    bcm2835_gpio_fsel(UART_RTS, BCM2835_GPIO_FSEL_OUTP);

    // open UART0 port
    uart_fd = open(UART_UART0, O_RDWR | O_NOCTTY | O_NDELAY);
    if ( uart_fd == -1 )
    {
        debug(DB_ERR, "%s: error %d opening %s\n", __FUNCTION__, errno, UART_UART0);
        // Close GPIO
        bcm2835_close();

        return -1;
    }
    else
    {
        // Setup UART options
        uart_set_interface_attr(uart_fd, UART_BAUD);
        uart_set_blocking(uart_fd, 0);
        fcntl(uart_fd, F_SETFL, FNDELAY);

        debug(DB_INFO, "initialized UART0 %s\n", UART_UART0);
    }

    uart_module_initialized = 1;

    return 0;
}

/********************************************************************
 * uart_close()
 *
 *  Close the UART and RPi GPIO interfaces with the PCXT.
 *
 *  param:  none
 *  return: none
 */
void uart_close(void)
{
    bcm2835_close();
    close(uart_fd);

    uart_module_initialized = 0;
}

/********************************************************************
 * uart_get_cmd()
 *
 *  Check command queue for pending data and return pointer to data or 0 if none.
 *
 *  param:  none
 *  return: 0 if no commands, queue empty,
 *          pointer to next command
 */
cmd_q_t* uart_get_cmd(void)
{
    cmd_q_t* command;

    if ( cmd_count )
    {
        command = &command_queue[cmd_out];
        cmd_count--;
        cmd_out++;
        if ( cmd_out == UART_CMD_Q_LEN )
            cmd_out = 0;

        return command;
    }

    return 0;
}

/********************************************************************
 * uart_recv_cmd()
 *
 *  Check UART and add incoming commands to command queue.
 *  UART polling function run in a separate thread or periodic poll.
 *  If there are not input bytes this the function blocks for
 *  a time-out set in uart_set_blocking()
 *  Function tries to assemble a complete command and exit once
 *  one command has been assembled of a read time-out has occurred.
 *
 *  param:  none
 *  return: Number of commands received
 *         -1 if error
 */
int uart_recv_cmd(void)
{
    int                 read_result;
    int                 commands = 0;

    static uint8_t      c;

    static uint8_t      cmd[sizeof(cmd_param_t)] = {0};
    static int          slip_esc_received = 0;
    static int          count = 0;
    static int          done_cmd_packet = 0;

    if ( !uart_module_initialized )
    {
        debug(DB_ERR, "%s: UART is not initialized (line:%d)\n", __FUNCTION__, __LINE__);
        return -1;
    }

    /* collect bytes from the serial stream into a command sequence
     * bytes are received framed as a SLIP packet
     */
    uart_rts_active();

    while ( done_cmd_packet == 0 )
    {
        read_result = read(uart_fd, &c, 1);

/*
        if ( read_result == 1 )
        {
            debug(DB_VERBOSE, "c=%d\n", c);
        }
*/

        // exit if nothing read or time out
        if ( read_result == -1 && errno == EAGAIN )
        {
            // TODO if running in a separate thread, maybe use 'continue'?
            break;
        }
        // exit on IO error
        else if ( read_result == -1 && errno != EAGAIN )
        {
            debug(DB_ERR, "%s: error %d reading UART\n", __FUNCTION__, errno);
            count = 0;
            return -1;
        }
        // first check if previous character was a SLIP ESC
        else if ( slip_esc_received )
        {
            slip_esc_received = 0;
            if ( c == SLIP_ESC_END )
            {
                c = SLIP_END;
            }
            else if ( c == SLIP_ESC_ESC )
            {
                c = SLIP_ESC;
            }
        }
        // handle packet delimiter
        else if ( c == SLIP_END )
        {
            // back-to-back END
            if ( count == 0 )
            {
                continue;
            }
            // command bytes received and packet is done
            else
            {
                done_cmd_packet = 1;
                count = 0;
                break;
            }
        }
        // handle SLIP escape in the byte stream
        else if ( c == SLIP_ESC )
        {
            slip_esc_received = 1;
            continue;
        }
        // handle full packet with no delimiter
        else if ( count == sizeof(cmd_param_t) )
        {
            debug(DB_ERR, "%s: invalid command frame; discarding\n", __FUNCTION__);
            count = 0;
            break;
        }

        cmd[count] = c;
        count++;

/*
        debug(DB_VERBOSE, "%3d | %3d %3d %3d %3d %3d %3d (count->%d)\n",
                          cmd[0], cmd[1], cmd[2], cmd[3], cmd[4], cmd[5], cmd[6], count);
*/
    }

    uart_rts_not_active();

    /* if there is a complete command packet ready
     * process it
     */
    if ( done_cmd_packet )
    {
        done_cmd_packet = 0;

        // check if there is room in the queue for this command
        if ( cmd_count == UART_CMD_Q_LEN )
        {
            count = 0;
            debug(DB_ERR, "%s: command buffer overflow; discarding\n", __FUNCTION__);
            return -1;
        }

        // copy new command to the queue
        commands = 1;
        command_queue[cmd_in].queue = (int)((cmd[0] >> 6) & 0x03);
        memcpy(&command_queue[cmd_in].cmd_param, &cmd, sizeof(cmd_param_t));

        debug(DB_VERBOSE, "uart_recv_cmd(): [%d] %3d | %3d %3d %3d %3d %3d %3d\n",
                          command_queue[cmd_in].queue,
                          cmd[0], cmd[1], cmd[2], cmd[3], cmd[4], cmd[5], cmd[6]);

        memset(&cmd, 0, sizeof(cmd_param_t));
        cmd_count++;
        cmd_in++;
        if ( cmd_in == UART_CMD_Q_LEN )
            cmd_in = 0;
    }

    return commands;
}


/********************************************************************
 * uart_send()
 *
 *  Send a data byte to host PC/XT.
 *
 *  param:  data byte to send
 *  return: none
 */
void uart_send(uint8_t byte)
{
    if ( !uart_module_initialized )
    {
        debug(DB_ERR, "%s: UART is not initialized (line:%d)\n", __FUNCTION__, __LINE__);
        return;
    }

    write(uart_fd, (void*) &byte, 1);
}

/********************************************************************
 * uart_flush()
 *
 *  Flush UART buffer.
 *
 *  param:  none
 *  return: '0' on success. '-1' on failure and set errno to indicate the error
 *
 */
int uart_flush(void)
{
    return tcflush(uart_fd, TCIOFLUSH);
}

/********************************************************************
 * uart_rts_active()
 *
 *  RTS line to '0'
 *
 *  param:  none
 *  return: none
 *
 */
void uart_rts_active(void)
{
    if ( uart_module_initialized )
    {
        bcm2835_gpio_write(UART_RTS, LOW);
    }
}

/********************************************************************
 * uart_rts_not_active()
 *
 *
 *  RTS line to '1'
 *
 *  param:  none
 *  return: none
 *
 */
void uart_rts_not_active(void)
{
    if ( uart_module_initialized )
    {
        bcm2835_gpio_write(UART_RTS, HIGH);
    }
}

/********************************************************************
 * uart_set_interface_attr()
 *
 *  Initialize UART attributes.
 *
 *  param:  file descriptor, baud rate, and parity type (to enable: PARENB + for odd: PARODD)
 *  return: 0 if no error,
 *         -1 if error initializing
 *
 */
int uart_set_interface_attr(int fd, int speed)
{
    struct termios tty;

    memset (&tty, 0, sizeof tty);
    if (tcgetattr (fd, &tty) != 0)
    {
        debug(DB_ERR, "%s: error %d from tcgetattr", __FUNCTION__, errno);
        return -1;
    }

    cfsetospeed (&tty, speed);
    cfsetispeed (&tty, speed);

    tty.c_iflag = IGNBRK;                           // ignore break, no xon/xoff

    tty.c_oflag = 0;

    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;     // 8-bit chars
    tty.c_cflag |= CLOCAL;                          // ignore modem controls
    tty.c_cflag |= CREAD;                           // enable reading
    tty.c_cflag &= ~(PARENB | PARODD);              // no parity
    tty.c_cflag &= ~CSTOPB;                         // one stop bit
    tty.c_cflag &= ~CRTSCTS;                        // no flow control

    tty.c_lflag = 0;                                // no signaling chars, no echo, no canonical processing

    if (tcsetattr (fd, TCSANOW, &tty) != 0)
    {
        debug(DB_ERR, "%s: error %d from tcsetattr", __FUNCTION__, errno);
        return -1;
    }

    debug(DB_VERBOSE, "UART settings:\n\tc_iflag=0x%x\n\tc_oflag=0x%x\n\tc_cflag=0x%x\n\tc_lflag=0x%x\n",
                      tty.c_iflag, tty.c_oflag, tty.c_cflag, tty.c_lflag);

    return 0;
}

/********************************************************************
 * uart_set_blocking()
 *
 *  Initialize UART attributes.
 *
 *  param:  file descriptor, '0' non-blocking read or '1' blocking read
 *  return: 0 if no error,
 *         -1 if error initializing
 *
 */
int uart_set_blocking(int fd, int should_block)
{
    struct termios tty;

    memset (&tty, 0, sizeof tty);
    if (tcgetattr (fd, &tty) != 0)
    {
        debug(DB_ERR, "%s: error %d from tggetattr", __FUNCTION__, errno);
        return -1;
    }

    tty.c_cc[VMIN]  = should_block ? 1 : 0;
    tty.c_cc[VTIME] = 5;            // 0.5 seconds read timeout

    if (tcsetattr (fd, TCSANOW, &tty) != 0)
    {
        debug(DB_ERR, "%s: error %d setting term attributes", __FUNCTION__, errno);
        return -1;
    }

    return 0;
}
