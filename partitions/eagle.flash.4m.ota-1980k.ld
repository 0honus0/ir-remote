/*
 * 4 MB ESP8266 OTA layout
 *
 * sketch:  2012 KB maximum linker-reported size
 * OTA:     up to the same size, dynamically placed after the running sketch
 * reserve: 16 KB between the two maximum-size images and the fixed tail
 * EEPROM: 32 KB  at 0x3F4000
 * RF cal:  4 KB  at 0x3FC000
 * WiFi:   12 KB  at 0x3FD000
 */

MEMORY
{
  dport0_0_seg :                        org = 0x3FF00000, len = 0x10
  dram0_0_seg :                         org = 0x3FFE8000, len = 0x14000
  irom0_0_seg :                         org = 0x40201010, len = 0x1f6ff0
}

/* No filesystem; the OTA updater may use all flash before this fixed tail. */
PROVIDE ( _FS_start = 0x405F4000 );
PROVIDE ( _FS_end = 0x405F4000 );
PROVIDE ( _FS_page = 0x0 );
PROVIDE ( _FS_block = 0x0 );
PROVIDE ( _EEPROM_start = 0x405f4000 );
/* The following symbols are DEPRECATED and will be REMOVED in a future release */
PROVIDE ( _SPIFFS_start = 0x405F4000 );
PROVIDE ( _SPIFFS_end = 0x405F4000 );
PROVIDE ( _SPIFFS_page = 0x0 );
PROVIDE ( _SPIFFS_block = 0x0 );

INCLUDE "local.eagle.app.v6.common.ld"
