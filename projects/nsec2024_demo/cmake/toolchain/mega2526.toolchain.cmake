set(ARDUINO_BOARD "AVR_ADK")
set(ARDUINO_MCU "atmega2560")
set(ARDUINO_F_CPU "16000000L")
set(ARDUINO_VARIANT "mega")
set(ARDUINO_AVRDUDE_PROTOCOL "avr109")
set(ARDUINO_AVRDUDE_SPEED "57600")
set(ARDUINO_USB On)
set(ARDUINO_USB_VID "0x2341")
set(ARDUINO_USB_PID "0x0042")
set(ARDUINO_USB_PRODUCT "Arduino Mega")

include(${CMAKE_CURRENT_LIST_DIR}/avr.toolchain.cmake)      