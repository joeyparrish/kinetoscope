BOARD = rp2040:rp2040:rpipicow
BOARD_PATH = $(subst :,.,${BOARD})
SERIAL_PORT = /dev/ttyACM0
SKETCH_NAME = firmware
EXTRA_FLAGS = -DETHERNET_LARGE_BUFFERS -DMAX_SOCK_NUM=1 \"-DSPI_ETHERNET_SETTINGS=SPISettings(80000000, MSBFIRST, SPI_MODE0)\"

.PHONY: default build upload monitor clean

default:
	@echo "The following commands are supported:"
	@echo "  make build: compile the firmware"
	@echo "  make upload: upload the firmware to a connected microcontroller"
	@echo "  make monitor: run the serial monitor"
	@echo "  make clean: clean the build outputs"

arduino_secrets.h:
	touch arduino_secrets.h

build: arduino_secrets.h
	arduino-cli compile -b ${BOARD} \
	    --build-property "build.extra_flags=${EXTRA_FLAGS}" \
	    --export-binaries
	cp build/${BOARD_PATH}/${SKETCH_NAME}.ino.uf2 .

upload: arduino_secrets.h
	arduino-cli compile -b ${BOARD} \
	    --build-property "build.extra_flags=${EXTRA_FLAGS}" \
	    -p ${SERIAL_PORT} \
	    --upload --verify

monitor:
	arduino-cli monitor -b ${BOARD} \
	    -p ${SERIAL_PORT} \
	    --config baudrate=115200

clean:
	rm -rf build ${SKETCH_NAME}.ino.elf
