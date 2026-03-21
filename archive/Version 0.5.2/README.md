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

For the 7 inch version, Gotek-Touchscreen-interface-7
(more testing and adjusting to make it work, so not complete)

This version of the GTI (ADF/DSK browser) is slightly different, as the hardware for the Waveshare ESP32-S3-TOUCH-LCD-7 Rev 1.2, needs a bit of rework.

When loading the .ino, you need to ensure that your board is as follows:
ESP32S3 Dev Module
The settings as follows:

USB CDC On Boot: "Enabled"
CPU Frequency: "240MHz (WiFi)"
Core Debug Level: "None"
USB DFU On Boot: "Disabled"
Erase All Flash Before Sketch Upload: "Disabled"
Events Run On: "Core 1"
Flash Mode: "QIO 80MHz"
Flash Size: "16MB (128Mb)"
JTAG Adapter: "Disabled"
Arduino Runs On: "Core 1"
USB Firmware MSC On Boot: "Disabled"
Partition Scheme: "16M Flash (3MB APP/9.9MB FATFS)"
PSRAM: "OPI PSRAM"
Upload Mode: "UART0 / Hardware CDC"
Upload Speed: "921600"
USB Mode: "USB-OTG (TinyUSB)"
Zigbee Mode: "Disabled"

There may be library dependencies, such as 
ESP32 by espressif systems 3.3.7 (boards)
LovyanGFX by lovyan03 1.2.19 (library)

The behavior of this device should be identical to the 2.8 inch version, although formatting may seem different.


