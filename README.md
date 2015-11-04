# iSaab
A Virtual CD Changer for early 9-3's and 9-5's

## Installation
Requirements:
* BlueSaab 3.5mm module

http://bluesaab.blogspot.com/2014/03/how-to-build-your-own-35mm-version-of.html

* Geeetech VS1053 breakout board

http://www.geeetech.com/wiki/index.php/VS1053_MP3_breakout_board_with_SD_card

* 3.5mm stereo cable with right-angle plug

* 10 position ribbon cable with 1x10 IDC connector, e.g.

http://www.newhavendisplay.com/1x10-idc-interface-cable-p-7619.html

* Arduino 1.6.5 or later

Procedure:

* Build the BlueSaab 3.5mm module as described, using a 3.5mm stereo plug instead of a jack
* Solder in the ribbon at the following physical pin locations on the underside of the board:

| VS1053 Pin | AVR Pin | Logical Pin |
| ----------:|:------- | ----------- |
| 5V         | 7       | |
| GND        | 8       | |
| CARDCS     | 6       | D4 |
| MISO       | 18      | |
| MOSI       | 17      | |
| SCK        | 19      | |
| XCS        | 13      | D7 |
| XRESET     | 15      | D9 |
| XDCS       | 12      | D6 |
| DREQ       | 5       | D3 |

* Modify Arduino/libraries/SD/src/SD.cpp to open the card at full speed:

> return card.init(SPI_FULL_SPEED, csPin) &&

* Upload the iSaab code to the module
* Plug it in...

## Usage
* Format an SD card with FAT32. SD cards larger than 32GB may require special tools (e.g.
http://www.ridgecrop.demon.co.uk/index.htm?fat32format.htm)
* Place patch053.bin in the root. The root is reserved for special files.
* Place music files (FLAC, Ogg Vorbis, MP3, MPEG4, WMA) into sub-directories, up-to three levels deep (e.g. /Album/Track or /Artist/Album/Track or /Genre/Artist/Album/Track).
* Files are played in filesystem order, with the exception that all files in a parent folder are played before descending into child folders. You may need special tools to sort files to play in the expected order (e.g. http://www.anerty.net/software/file/DriveSort.php)
* The Arduino SD library ignores files containing non-ASCII characters.  Ensure that the short (8.3) file names do not contain latin characters.

![inside](https://raw.githubusercontent.com/mcaldwelva/iSaab/master/data/inside.jpg)
![front](https://raw.githubusercontent.com/mcaldwelva/iSaab/master/data/front.jpg)
![back](https://raw.githubusercontent.com/mcaldwelva/iSaab/master/data/back.jpg)
