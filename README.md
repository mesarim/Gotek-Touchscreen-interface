# Gotek-Touchscreen-interface
Using a Waveshare ESP32-S3-Touch-LCD-2.8 this interface loads from an SD card the .ADF for Amiga, or (.DSK for ZX or CPC) onto a usb-presentable ram drive. The interface is touchscreen, and includes support for images and information, along with the ability to disk swap.  

The .ino has requirements regarding adding the ESP32 libraries, and the device need to be explicity set to Waveshare ESP32-S3-Touch-LCD-2.8 for the screen compatability, pinouts, and display size.

Arduino settings in the Tools section are as follows:
USB CDC on boot: "Disabled"
CPU FrequencyL "240Mhz"
Core Debug level: "none"
USB DFU on Boot: "disabled"
Erase all flash before Sketch upload: "Disabled"
Events run on: "core 0"
Flash mode: "QIO 120Mhz"
JTAG Adapter: "Disabled"
Arduino Runs on: "Core 1"
USB Firmware MSC On Boot: "Disabled"
Partition Scheme: "!6M Flash (3MB APP/9.9MB FATFS)"
PSRAM "Enabled" --- IMPORTANT

Once you upload the code to your device, press the reboot button (middle of the three). This will start it. 
If you need to make changes, and reboot the device into programming mode, press BOOT and RESET together, let go of RESET, and then let go of BOOT.
now your machine should see the Waveshare again and the com port it is on.

