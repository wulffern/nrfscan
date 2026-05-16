######################################################################
##        Copyright (c) 2026 Carsten Wulff Software, Norway
######################################################################

BOARD ?= nrf52833dk/nrf52833
SCAN_DWELL_US ?= 400
SCAN_INTERVAL_MS ?= 1000

build:
	west build -b $(BOARD) -- \
		-DSCAN_DWELL_US=$(SCAN_DWELL_US) \
		-DSCAN_INTERVAL_MS=$(SCAN_INTERVAL_MS)

flash:
	west flash

clean:
	rm -rf build

.PHONY: build flash clean
