.PHONY: build upload monitor clean check bars bars-upload

all: upload

build:
	pio run

upload: build
	pio run -t upload

monitor:
	pio device monitor --filter esp32_exception_decoder

clean:
	pio run -t clean

check:
	pio check --fail-on-defect=high

# Standalone display test: SMPTE-style color bars, no buttons/battery/BLE
bars:
	pio run -e color_bars

bars-upload: bars
	pio run -e color_bars -t upload
