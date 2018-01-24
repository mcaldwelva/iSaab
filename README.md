# iSaab
This is a virtual replacement for the factory CD changer on first generation Saab 9-3's and 9-5's. All controls should behave as described in the owner's manual, with the following exceptions:
* RDM does not change tracks when switching to shuffle mode
* In shuffle mode, the NXT and preset buttons are used to change the display text
* The 9-5 intro scan controls have been repurposed for pause and resume


## Parts

This is built on the [BlueSaab 3.5mm module](http://bluesaab.blogspot.com/2014/03/how-to-build-your-own-35mm-version-of.html) with some small modifications to incorporate a sound card and reduce power consumption. The following or similar parts are required:

* [BlueSaab 3.5mm board](https://oshpark.com/shared_projects/uMyNRBbZ)

* [ICs, caps, resistors, etc.](https://www.mouser.com/ProjectManager/ProjectDetail.aspx?AccessID=112dd4f257)

* [TE 827229-1 connector](https://www.connectorpeople.com/Connector/TYCO-AMP-TE_CONNECTIVITY/8/827229-1)

* [Geeetech VS1053 breakout board](https://www.amazon.com/Geeetech-VS1053-breakout-board-card/dp/B0755PQCPS)

* [3.5mm stereo right-angle plug](https://www.amazon.com/3-5mm-Stereo-Right-Angle-Plug/dp/B004GIKSN6)

* FTDI cable


## Build

Cut the ribbon cable and strip the even wires (only the bottom row is used) so that they can be solderd in place with the MCU from the underside of the board.

| VS1053 Pin | AVR Pin | Ribbon Pin |
| ----------:|:------- | ---------- |
| 5V         | 7       | 2 |
| GND        | 8       | 4 |
| CARDCS     | 6 (D4)  | 6 |
| MISO       | 18      | 8 |
| MOSI       | 17      | 10 |
| SCK        | 19      | 12 |
| XCS        | 13 (D7) | 14 |
| XRESET     | 15 (D9) | 16 |
| XDCS       | 12 (D6) | 18 |
| DREQ       | 5  (D3) | 20 |

![inside](https://raw.githubusercontent.com/mcaldwelva/iSaab/master/data/inside.jpg)


## Software

* Use Arduino 1.8.x or later

* Apply the provided SD library patch. This includes fixes to support extended ASCII characters, exclude system and hidden files from directory listings, allow SD card swapping while shutdown, and use full-speed SPI by default:

> C:\Program Files (x86)\Arduino\libraries> patch -p1 < %HOMEPATH%\Documents\Arduino\iSaab\data\SD.diff

* Upload the iSaab code to the module using an FTDI cable


## Usage
* Format an SD card with FAT32. Third-party tools are needed to format SD cards larger than 32GB (e.g.
http://www.ridgecrop.demon.co.uk/index.htm?fat32format.htm)

* Place music files (FLAC up to 16b/44kHz, Ogg Vorbis, MP3, MPEG4, WMA) into sub-directories, up-to three levels deep

* Files are played in filesystem order, which may not be as they appear in your OS. Third-party tools can be used to sort the filesystem to play in the desired order (e.g. http://www.anerty.net/software/file/DriveSort.php)

* Place patch053.bin in the root folder to enable FLAC playback.

* Create presets.txt in the root, containing up to 6 space separated numbers representing the play-order of the file.

* Connect the module to the CD changer port in the trunk:
![back](https://raw.githubusercontent.com/mcaldwelva/iSaab/master/data/back.jpg)
