######################################################################
##        Copyright (c) 2026 Carsten Wulff Software, Norway
######################################################################

BOARD ?= nrf52833dk/nrf52833
SCAN_INTERVAL_MS ?= 1000
SCAN_SETTLE_US ?= 1
SCAN_DISABLE_PERIOD_CH ?= 20
SCAN_RADIO_2MBIT ?= 1

build:
	west build -b $(BOARD) -- \
		-DSCAN_INTERVAL_MS=$(SCAN_INTERVAL_MS) \
		-DSCAN_SETTLE_US=$(SCAN_SETTLE_US) \
		-DSCAN_DISABLE_PERIOD_CH=$(SCAN_DISABLE_PERIOD_CH) \
		-DSCAN_RADIO_2MBIT=$(SCAN_RADIO_2MBIT)

flash:
	west flash

clean:
	rm -rf build

# Print SCAN_* compile definitions from the last build (run after `make build`).
config:
	@grep -E 'SCAN_(INTERVAL|SETTLE|DISABLE|RADIO)' build/nrfscan/build.ninja 2>/dev/null | head -5 || \
		echo "No build yet — run 'make build' first."

.PHONY: build flash clean config
