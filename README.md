# Lora-Shuttle-examples

![Lora Shuttle](images/top1.jpg)

This repo contains code examples for the Lora Shuttle devboard designed around HT-CT62 module. The module is basically Esp32-C3 + SX1262. Don't forget to set your lorawan region in tools before the compilation, I've tested the board on EU868.

 - I2CShuttleSlave - allows the board to act as I2C slave. In this mode the board can publish data to The Thing Network via Lorawan. You can trigger a send once every 10 seconds, but note that you should comply both with The Things Network fair usage policy(30 seconds uplink per day) and EU 1% duty cycle rule(if you are in EU).
 - I2CShuttleSlave - this code allows any Arduino compatible board to use I2C slave from the example above. 

To be able to flash the board, install [Heltec framework](https://github.com/Heltec-Aaron-Lee/WiFi_Kit_series) for the Arduino Ide. 
When flashing, set the board to 'Wireless Mini Shell'. 
If you need uart printing, enable 'CDC on boot' or use TX/RX board pins. If there is no 'CDC on boot' option in tools you can manually enable it in you boards.txt file by setting: heltec_wireless_mini_shell.build.cdc_on_boot=1.
