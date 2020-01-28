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

## Software
The RPi software works in conjunction with the PC/XT BIOS code (https://github.com/eyalabraham/new-xt-bios)
The main component in the RPi software is the Frame Buffer. Two options to use the frame buffer, one from Linux and the other on bare-metal RPi with no OS.

**Executing emulation code under Linux**
Main resource (http://raspberrycompote.blogspot.com/). This module is written to run under a strip-down RPi Linux distribution. The goal is to create a headless driver that is as OS independent as possible so that porting to bare-meta will be as direct as possible.
RPi OS installation is tuned to eliminate IO to SD card so that power on/off will not depend on or affect SD card data storage/caching. Possible setup will use [TinyCore](http://tinycorelinux.net/welcome.html) or [Make the root partition read only](https://hallard.me/raspberry-pi-read-only/).
This [Ada Fruit read only setup](https://learn.adafruit.com/read-only-raspberry-pi) seemed best because the read-only mode is select-able with a jumper on the GPIO lines.

**Emulated graphics cards and modes**

 | mode | resolution | color      | test/graph | pages | card | emulated |
 |------|------------|------------|------------|-------|------|----------|
 | 0    | 40x25      | Monochrome | text       |  8    | CGA  |   no     |
 | 1    | 40x25      | 16 color   | text       |  8    | CGA  |   yes    |
 | 2    | 80x25      | 16 gray    | text       |  4    | CGA  |   no     |
 | 3    | 80x25      | 16 color   | text       |  4    | CGA  |   yes    |
 | 4    | 320x200    | 4 color    | graphics   |  1    | CGA  |   no     |
 | 5    | 320x200    | 4 color    | graphics   |  1    | CGA  |   no     |
 | 6    | 640x200    | Monochrome | graphics   |  1    | CGA  |   no     |
 | 7    | 80x25      | Monochrome | text       |  1    | MDA  |   yes    |
 | 8    | 720x350    | Monochrome | graphics   |  1    | HERC |   no     |
 | 9    | 1280x1024  | Monochrome | text (1)   |  1    | VGA  |   no     |

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
| Cursor size/mode  | 3 (11) | Top scan line       | Bottom scan line| 0             | 0         | 0          | 0          |
| Put character (1) | 4      | Page                | char code       | col=0..79(39) | row=0..24 | 0          | Attrib.(2) |
| Get character (6) | 5      | Page                | 0               | col=0..79(39) | row=0..24 | 0          | 0          |
| Put character (7) | 6      | Page                | char code       | col=0..79(39) | row=0..24 | 0          | 0          |
| Scroll up (4)     | 7      | Rows                | T.L col         | T.L row       | B.R col   | B.R row    | Attrib.(2) |
| Scroll down (4)   | 8      | Rows                | T.L col         | T.L row       | B.R col   | B.R row    | Attrib.(2) |
| Put pixel         | 9      | Page                | Pixel color (3) |       16-bit column       |       16-bit row        |
| Get pixel (8)     | 10     | Page                | 0               |       16-bit column       |       16-bit row        |
| Set palette       | 11     | palette/color       | palette ID      | 0             | 0         | 0          | 0          |
| Clear screen      | 12     | Page                | 0               | 0             | 0         | 0          | Attrib.(2) |
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
(11) A value of 2000h turns cursor off.

**INT 10h mapping to display control commands**
Functions not listed below will be handled by BIOS, and not transferred to the display controller.
The display controller will keep cursor position but will not track movement through writes; cursor reposition will be done by BIOS with command #2.
Put Character commands #4 and #6 provide direct character placement parameters, these do not change display controller's stored cursor position. Only command #2 can change display controller's stored cursor position. Therefore, INT 10h functions 0Eh and 13h will output the characters with commands #4 and #6 and then provide one cursor reposition command #2. For visual effect, cursor 'off' and 'on' with command #3 can be used.
Command #11 can be used with scroll commands if the text screen needs to be cleared.

| INT           | Function                                   | Command   |
|---------------|--------------------------------------------|-----------|
| INT 10,0      | Set video mode                             | #0        |
| INT 10,1      | Set cursor type/size                       | #3        |
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

**RPi setup changes**
- Setup access through USB OTG and disable (turn off) Wifi
- Disable, turn off, blue tooth
- Reallocated the UART to GPIO pins 14 and 15 instead of the mini-UART?
- Redirect boot messages to the mini-UART after it is moved/swapped with the UART?
- Get rid of the boot messages on frame buffer tty1 (see above)

**Files**
- *vga.c* main module and emulator control loop
- *fb.c* frame buffer and graphics emulation
- *uart.c* UART IO driver
- *iv8x16u.h* 8x16 font bitmap definition [bitmap font source](https://github.com/farsil/ibmfonts) for code page 437 characters
- *ic8x8u.h*  8x8 font bitmap definition [bitmap font source](https://github.com/farsil/ibmfonts) for code page 437 characters
- *im9x14u.h* 9x14 font bitmap definition [bitmap font source](https://github.com/farsil/ibmfonts) for code page 437 characters
- *config.h* compile time module configuration
- *util.c* utility and helper functions (debug print etc)

**VGA emulation bare-metal**
TBD


