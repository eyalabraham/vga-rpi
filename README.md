# VGA display card based on Raspberry Pi
Video display card will mimic MDA/CGA text and graphics cards on a standard VGA monitor.
The emulated graphics card will use a Raspberry Pi (RPi) running custom software as a video generator.
The RPi will connect through Serial GPIO to a Z80-SIO/2 UART to interface with the 8088 PCXT bus.

## Resources
- [RPi GPIO resorces](https://pinout.xyz/pinout/ground#)
- [BCM2835](https://www.raspberrypi.org/app/uploads/2012/02/BCM2835-ARM-Peripherals.pdf) and errata (https://elinux.org/BCM2835_datasheet_errata)
- Google search "Raspberry Pi bare metal"
- ARM1176JZF-S [Technical Reference Manual](http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.ddi0301h/index.html)
- CPU 8088-2 data sheets
- [IBM 5160 technical reference](http://www.retroarchive.org/dos/docs/ibm5160techref.pdf)
- [Z80-SIO UART](http://www.z80.info/zip/um0081.pdf)

## Schematics
- [Schematics GitHub URL](https://github.com/eyalabraham/schematics/tree/master/vga-rpi)

**RPi GPIO setup and mapping to Z80-SIO channel B**

| Pin # | GPIO # | Function  | GPIO Alt |  Z80-SIO   |
|-------|--------|-----------|----------|------------|
| 8     |   14   | TxD1      | ALT5 / 0 |  RxDB p.29 |
| 10    |   15   | RxD1      | ALT5 / 0 |  TxDB p.26 |
| 36    |   16   | CTS1      | ALT5 / 3 |  RTSB p.24 |
| 11    |   17   | RTS1      | ALT5 / 3 |  CTSB p.23 |

**RPi UART**
On RPi Zero W the mini-UART is mapped to GPIO14..17. The mini-UART is not considered a reliable communication path because it is clocked by a variable CPU clock. Application may need to remap the PL011 UART to these GPIO lines.

## Software
The RPi software works in conjunction with the PC/XT BIOS code (https://github.com/eyalabraham/new-xt-bios)
The main component in the RPi software is the Frame Buffer. Two options to use the frame buffer, one from Linux and the other on bare-metal RPi with no OS.

**Executing emulation code under Linux**
Main resource (http://raspberrycompote.blogspot.com/). This module is written to run under a strip-down RPi Linux distribution. The goal is to create a headless driver that is as OS independent as possible so that porting to bare-meta will be as direct as possible.
RPi OS installation is tuned to eliminate IO to SD card so that power on/off will not depend on or affect SD card data storage/caching. Possible setup will use [TinyCore](http://tinycorelinux.net/welcome.html) or [Make the root partition read only](https://hallard.me/raspberry-pi-read-only/)

**Emulated graphics cards and modes**

 | mode | resolution | color      | test/graph | pages | card | emulated |
 |------|------------|------------|------------|-------|------|----------|
 | 0    | 40x25      | Monochrome | text       |  8    | CGA  |   no     |
 | 1    | 40x25      | 16 color   | text       |  8    | CGA  |   yes    |
 | 2    | 80x25      | 16 gray    | text       |  4    | CGA  |   no     |
 | 3    | 80x25      | 16 color   | text       |  4    | CGA  |   yes    |
 | 4    | 320x200    | 4 color    | graphics   |  8    | CGA  |   no     |
 | 5    | 320x200    | 4 color    | graphics   |  8    | CGA  |   no     |
 | 6    | 640x200    | Monochrome | graphics   |  8    | CGA  |   no     |
 | 7    | 80x25      | Monochrome | text       |  1    | MDA  |   yes    |
 | 8    | 720x350    | Monochrome | graphics   |  1    | HERC |   no     |
 | 9    | 1280x1024  | Monochrome | text (1)   |  1    | VGA  |   no?    |

(1) This is a special mode for mon88, text 160x64

**Protocol for display control**
PC/XT will translate INT 10h calls from the application into a set of bytes sent to the RPi. These bytes will form the control primitives that manage the display. The bytes will be sent as "packets" through Z80-SIO UART channel B that is connected to the RPi UART.
Each packet will be delimited by escape characters similar to the Serial IP protocol [SLIP](https://en.wikipedia.org/wiki/Serial_Line_Internet_Protocol), with END escape codes at the start and end of the command primitive packet. This is a simple and efficient way to frame these command packets.

The control packets include:

| Command (5)(10)   | cmd    | byte.1              | byte.2          | byte.3        | byte.4    | byte.5     | byte.6     |
|-------------------|--------|---------------------|-----------------|---------------|-----------|------------|------------|
| Set video mode    | 0      | Mode=0..8 see above | 0               | 0             | 0         | 0          | 0          |
| Set display page  | 1      | Page                | 0               | 0             | 0         | 0          | 0          |
| Cursor position   | 2      | Page                | 0               | col=0..79(39) | row=0..24 | 0          | 0          |
| Cursor enable     | 3      | on=1 / off=0        | 0               | 0             | 0         | 0          | 0          |
| Put character (1) | 4      | Page                | char code       | col=0..79(39) | row=0..24 |            | Attrib.(2) |
| Get character (6) | 5      | Page                | 0               | col=0..79(39) | row=0..24 | 0          | 0          |
| Put character (7) | 6      | Page                | char code       | col=0..79(39) | row=0..24 | 0          | 0          |
| Scroll up (4)     | 7      | Rows                | T.L col         | T.L row       | B.R col   | B.R row    | Attrib.(2) |
| Scroll down (4)   | 8      | Rows                | T.L col         | T.L row       | B.R col   | B.R row    | Attrib.(2) |
| Put pixel         | 9      | Page                | Pixel color (3) |       16-bit column       |       16-bit row        |
| Get pixel (8)     | 10     | Page                | 0               |       16-bit column       |       16-bit row        |
| Clear screen      | 11     | Page                | 0               | 0             | 0         | 0          | Attrib.(2) |
| Echo (9)          | 255    | 1                   | 2               | 3             | 4         | 5          | 6          |

(1) Character is written to cursor position
(2) Attribute: Attribute byte will be decoded per video mode
(3) XOR-ed with current pixel if bit.7=1
(4) Act on active page
(5) PC/XT can send patrial command bytes count, any *trailing* bytes not sent are considered '0'
(6) Return data format: two bytes {character}{attribute}
(7) same at command #4, but use existing attribute
(8) Return data format: one byte {color_code}
(9) Return data format: six bytes {6}{5}{4}{3}{2}{1}
(10) Two high order bits are command queue: '00' VGA emulation, '01' tbd, '10' tbd, '11' system

**INT 10h mapping to display control commands**
Functions not listed below will be handled by BIOS, and not transferred to the display controller.
The display controller will keep cursor position but will not track movement through writes; cursor reposition will be done by BIOS with command #2.
Put Character commands #4 and #6 provide direct character placement parameters, these do not change display controller's stored cursor position. Only command #2 can change display controller's stored cursor position. Therefore, INT 10h functions 0Eh and 13h will output the characters with commands #4 and #6 and then provide one cursor reposition command #2. For visual effect, cursor 'off' and 'on' with command #3 can be used.
Command #11 can be used with scroll commands if the text screen needs to be cleared.

| INT           | Function                                   | Command   |
|---------------|--------------------------------------------|-----------|
| INT 10,0      | Set video mode                             | #0        |
| INT 10,2      | Set cursor position                        | #2        |
| INT 10,5      | Select active display page                 | #1        |
| INT 10,6      | Scroll active page up                      | #7        |
| INT 10,7      | Scroll active page down                    | #8        |
| INT 10,8      | Read character and attribute at cursor     | #5        |
| INT 10,9      | Write character(s) and attribute at cursor | #4        |
| INT 10,A      | Write character(s) at current cursor       | #6        |
| INT 10,C      | Write graphics pixel at coordinate         | #9        |
| INT 10,D      | Read graphics pixel at coordinate          | #10       |
| INT 10,E      | Write text in teletype mode                | #6,#2     |
| INT 10,13     | Write string (BIOS after 1/10/86)          | #4,#2     |

**TODO**
BIOS:
1. cursor off before scroll and then back on
5. enable mon88 keyboard echo to go to VGA

1. call fb_init() with some default initial emulation
   - display some splash screen and wait for mode command from BIOS.
   - BIOS will have to initialize the 8255 and wait for the RPi to boot

2. BIOS senses card is ready through Z80-SIO CTSB line, then sends an INT 10h,0 with card mode based in motherboard [DIP switches 5 and 6](http://www.minuszerodegrees.net/5160/misc/5160_motherboard_switch_settings.htm)

| Switches 5 and 6                                  |
|---------------------------------------------------|
| 5=OFF, 6=OFF:  MDA (monochrome)                   |
| 5=OFF, 6=ON :  CGA, at 40 column by 25 line mode  |
| 5=ON , 6=OFF:  CGA, at 80 column by 25 line mode  |
| 5=ON , 6=ON :  Cards with a BIOS expansion ROM    |
   
3. new function fb_emul() enter with the mode set by BIOS
   - actual function parameters are the INT 10h parameters: function code + register values
   - if card not set, or emulation mode changed from current one, then set/reset emulation mode
     this way if BIOS is rebooted without power cycle the emulation can be reset or reinitialized if DIP switches are changed or if INT 10h, 0 is received
   - fb_emul() runs and exits; it needs to be written to support periodic calling from within a loop:
      read GPIO, call fb_emul(), call uarl_handler(), other task, ... repeat.
   - within fb_emul() a sub-function per INT 10h function

4. maintain a circular buffer for incoming UART data, poll the buffer and send data to PC if any while asserting the RPi IRQ output GPIO.

5. no way to use interrupts under Linux. for GPIO interrupt response we'll need bare-metal setup
   - figure out a way to poll the GPIO PPI and manage IO through it, maybe multi-threading to spawn a thread that handles PPI input.
   - the PPI input thread will constantly poll the PPI ^OBF line and accept bytes into a circular buffer with the bytes queue ID
   - ppi_get() function will read the input queue:
    * queue ID 0 are commends for the VGA card and the next 0 ID bytes will be packed into a 

6. pseudo code:

    ppi_init();     // initialize GPIO lines and pins
    uart_init();    // 
    fb_init();
    
    while ( 1 )
    {
        ppi_command_q = ppi_get();
        
        if ( ppi_command_q )
        {
            if ( ppi_command_q->queue == PPI_Q_UART )
            {
                uart_send(<data>);
            }
            else ( ppi_command_q->queue == PPI_Q_VGA )
            {
                fb_emul(<data>);
            }
            ( ppi_command_q->queue == PPI_Q_OTHER_IO )
            {
                ...
            }
            else
            {
                ...
            }
        }
        
        if ( uart_isdata() )
        {
            byte = uart_get();
            uart_send(byte);
        }
    }

7. PC/XT items
    - update all relevant data in BIOS data area

8. **RPi setup changes**
    - setup access through USB OTG and disable (turn off) Wifi
    - disable, turn off, blue tooth
    - is it possible to reallocated the UART to GPIO pins 14 and 15 instead of the mini-UART?
    - redirect boot messages to the mini-UART after it is moved/swapped with the UART
    - get rid of the boot messages on frame buffer tty1 (see above)
    - make FS read-only
    - https://retropie.org.uk/forum/topic/14299/tutorial-remove-boot-text-on-the-raspberry-pi-for-noobs/2
    - original cmdline:
        dwc_otg.lpm_enable=0 console=tty1 root=PARTUUID=71aa8f65-02 rootfstype=ext4 elevator=deadline fsck.repair=yes rootwait

**Files**
- *vga.c* main module and emulator control loop
- *ppi.c* parallel interface between RPi and PC 8255 PPI
- *fb.c* frame buffer and graphics emulation
- *uart.c* UART IO driver
- *iv8x16u.h* font bitmap definition [bitmap font source](https://github.com/farsil/ibmfonts) for code page 437 characters
- *util.c* utility and helper functions (debug print etc)

**VGA emulation bare-metal**
TBD


