# champion_generator_remote
Champion Generator Remote. Controls a Champion generator with wireless start. Replicates the on/off that the stock keyfob sends.
You will have to pair this device with your generator just like you would pair a new keyfob.

I use this device in my travel trailer to allow me to run air conditioning overnight as needed without running the generator all
night, and without having to wake up to turn the generator on/off. This device reads the temperature inside the camper, and when the
temperature goes above the set point it will send an "on" signal to the generator. The generator starts, and if your camper thermostat
is set to super cold then the air conditioner will start after the generator is started. When the temperature gets cold enough this device
will send an "off" signal to the generator. The end result is you can sleep with air conditioning coming on and off as needed, and without
running your generator out of gas overnight.

Requires:
Heltec Wifi Kit 32 with built-in oled
DHT12 i2c temp sensor - https://www.amazon.com/gp/product/B07KGGWBNW/
rotary encoder - https://www.amazon.com/gp/product/B08PBLXR27/
433mhz transmitter - https://www.amazon.com/gp/product/B01DKC2EY4/

I designed a custom PCB for this. Contact me at nick.shaver@gmail.com if you would like one.

Libraries that need to be available:
RCSwitch
Heltec
ESP32Encoder
SinricPro (for Amazon Alexa/Google Home integration)
DHT12

I also designed a custom 3d-printed enclosure for this. Contact me if you would like to buy one.

Demonstration of this device is here: https://www.youtube.com/watch?v=-pnF9HI7Xes
