# iSaab
This is a virtual replacement for the factory CD changer on first generation Saab 9-3's and 9-5's. All controls should behave as described in the owner's manual, with the following exceptions:
* RDM does not change tracks when switching to shuffle mode
* In shuffle mode, the NXT and preset buttons are used to change the display text. NXT will rotate through the tags. Each preset button will select Track Title, Album Title, Album Artist, Track Artist, Genere, or Year, respectively. Pressing the same preset again will return the display to normal.
* The 9-5 intro scan controls have been repurposed for pause and resume


## Parts

This is based on the [BlueSaab 3.5mm module](http://bluesaab.blogspot.com/2014/03/how-to-build-your-own-35mm-version-of.html) with some modifications to incorporate a sound card and reduce power consumption. The following or similar parts are required:

* iSaab board

* [ICs, caps, resistors, etc.](https://www.mouser.com/ProjectManager/ProjectDetail.aspx?AccessID=5A5DA965B5)

* [TE 827229-1 connector](https://www.connectorpeople.com/Connector/TYCO-AMP-TE_CONNECTIVITY/8/827229-1)

* [VS1053 breakout board](https://www.amazon.com/VS1053B-Arduino-Breakout-Real-time-Recording/dp/B07KNV2YTM)

* FTDI cable


## Build

The BOM components are labeled to match the board. Cut the ribbon cable and strip the even wires (only the bottom row is used) so that they can be solderd in place with the MCU from the underside of the board.

| VS1053 Pin | AVR Pin | Ribbon Pin |
| ----------:|:------- | ---------- |
| 5V         | 20      | 1 |
| GND        | 22      | 2 |
| CARDCS     | 6 (D4)  | 3 |
| MISO       | 18      | 4 |
| MOSI       | 17      | 5 |
| SCK        | 19      | 6 |
| XCS        | 13 (D7) | 7 |
| XRESET     | 15 (D9) | 8 |
| XDCS       | 12 (D6) | 9 |
| DREQ       | 5  (D3) | 10 |

![inside](https://raw.githubusercontent.com/mcaldwelva/iSaab/master/data/inside.jpg)


## Software

* Use Arduino 1.8.10

* Apply the provided SD library patch. This includes fixes to support extended ASCII characters and exclude hidden/system files from directory listings:

> C:\Program Files (x86)\Arduino\libraries> patch -p1 < %HOMEPATH%\Documents\Arduino\iSaab\data\SD.diff

* Upload the iSaab code to the module using an FTDI cable


## Usage
* Format an SD card with FAT32. Third-party tools are needed to format SD cards larger than 32GB (e.g.
http://www.ridgecrop.demon.co.uk/index.htm?fat32format.htm)

* Place music files (FLAC up to 16b/44kHz, Ogg Vorbis, MP3, MPEG4, WMA) into sub-directories, up-to three levels deep

* Files are played in filesystem order, which may not be as they appear in your OS. Third-party tools can be used to sort the filesystem to play in the desired order (e.g. http://www.anerty.net/software/file/DriveSort.php)

* Place patch053.bin in the root folder to enable FLAC playback.

* Create presets.txt in the root, containing up to 6 comma separated numbers. Each number represents the play-order of the file on the file system.

* Connect the module to the CD changer port in the trunk:
![back](https://raw.githubusercontent.com/mcaldwelva/iSaab/master/data/back.jpg)


## Troubleshooting
* If play doesn't start when the CD changer is selected, ensure the SD card is fully inserted. If the module is still unrespsonive, it may be necessary to disconnect and reconnect it for a hard boot.

* Only connect or disconnnect the module while the car is off and key removed from the ingition.

* The disc and track numbers are changing on the display but nothing is playing. This is expected behavior when seeking tracks. You may see this when the CD changer turns on, when using presets, or between tracks in shuffle mode.
