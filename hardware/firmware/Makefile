BOARD = rp2040:rp2040:rpipicow
BOARD_PATH = $(subst :,.,${BOARD})
SERIAL_PORT = /dev/ttyACM0
SKETCH_NAME = firmware

.PHONY: default build upload monitor clean

default:
	@echo "The following commands are supported:"
	@echo "  make build: compile the firmware"
	@echo "  make upload: upload the firmware to a connected microcontroller"
	@echo "  make monitor: run the serial monitor"
	@echo "  make clean: clean the build outputs"

build:
	arduino-cli compile -b ${BOARD} \
	    --export-binaries
	cp build/${BOARD_PATH}/${SKETCH_NAME}.ino.elf .

upload:
	arduino-cli compile -b ${BOARD} \
	    -p ${SERIAL_PORT} \
	    --upload --verify

monitor:
	arduino-cli monitor -p ${SERIAL_PORT} --config baudrate=115200

clean:
	rm -rf build ${SKETCH_NAME}.ino.elf
