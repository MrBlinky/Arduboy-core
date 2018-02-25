# Arduboy-core
An optimized Arduino core for Arduboy

* Heavily assembly optimized wiring.h and other optimisation to other core files to reduce code size.
* Added soft bootloader and reset button feature. (Bootloader: Press LEFT+UP+A+B for two seconds, Reset: Press DOWN+RIGHT+A+B for two seconds)
* Fixes the bootloader magic number problem.
* New functions:
### unsigned char buttonsIdleTime()
Returns the time passed since the last button was released in 4.096 second units (ms/4096)

### unsigned char millisChar()
Returns the least significant byte of **millis()** 

### void delayShort(unsigned short ms)
Same as **delay()** but but takes an unsigned short (16-bit) value instead of an unsigned long (32-bit value). All occurances of **delay()** in the core are replaced by **delayShort()** Use of **Delay()** is depreciated as it will increase the sketches code size.

