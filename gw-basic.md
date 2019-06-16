# GW-Basic SCREEN statement vs. graphics modes

Resources:
- [GW-Basic manual](http://www.divonasperi.it/divona/tam/tecnologia/dow-all/GW%20Basic%20(inglese).pdf)
- [BIOS modes](http://stanislavs.org/helppc/int_10-0.html)
- [Color Graphics Adapter, CGA](https://en.wikipedia.org/wiki/Color_Graphics_Adapter)

```
    SCREEN [mode] [,[colorswitch]][,[apage]][,[vpage]]
```

This table lists resulting BIOS modes when executing GW-Basic SCREEN command with parameters 0, 1, or 2. All other values exit with an 'Illigal function call' error.

| BIOS mode | BIOS mode                     | Emulation | SCREEN [mode] |
|-----------|-------------------------------|-----------|---------------|
| 0         | 40x25 B/W text                | CGA       |               |
| 1         | 40x25 16 color text           | CGA 40x25 | 0             |
| 2         | 80x25 16 shades of gray text  | CGA       |               |
| 3         | 80x25 16 color text           | CGA 80x25 | 0             |
| 4         | 320x200 4 color graphics      | CGA       |               |
| 5         | 320x200 4 color graphics      | CGA       | 1             |
| 6         | 640x200 B/W graphics          | CGA       | 2             |
| 7         | 80x25 Monochrome text         | MDA/HERC  | 0             |
