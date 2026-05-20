######################################################################
##        Copyright (c) 2026 Carsten Wulff Software, Norway
######################################################################

BOARD ?= nrf52833dk/nrf52833
BOARD_54L15 ?= nrf54l15dk/nrf54l15/cpuapp
SCAN_INTERVAL_MS ?= 500
SCAN_SETTLE_US ?= 2
SCAN_DISABLE_PERIOD_CH ?= 15
SCAN_RADIO_2MBIT ?= 1

TX_RADIO_2MBIT ?= 1
TX_DWELL_MS ?= 1000

build:
	west build -b $(BOARD) -- \
		-DSCAN_INTERVAL_MS=$(SCAN_INTERVAL_MS) \
		-DSCAN_SETTLE_US=$(SCAN_SETTLE_US) \
		-DSCAN_DISABLE_PERIOD_CH=$(SCAN_DISABLE_PERIOD_CH) \
		-DSCAN_RADIO_2MBIT=$(SCAN_RADIO_2MBIT)

build-54l15:
	$(MAKE) build BOARD=$(BOARD_54L15) SCAN_SETTLE_US=15

flash:
	west flash

tx-build-54l15:
	$(MAKE) tx-build BOARD=$(BOARD_54L15)

tx-build:
	west build -b $(BOARD) -d build-tx tx -- \
		-DTX_RADIO_2MBIT=$(TX_RADIO_2MBIT) \
		-DTX_DWELL_MS=$(TX_DWELL_MS)

tx-flash:
	west flash -d build-tx

clean:
	rm -rf build build-tx

# Print SCAN_* compile definitions from the last build (run after `make build`).
config:
	@grep -E 'SCAN_(INTERVAL|SETTLE|DISABLE|RADIO)' build/nrfscan/build.ninja 2>/dev/null | head -5 || \
		echo "No build yet — run 'make build' first."

.PHONY: build build-54l15 flash tx-build tx-build-54l15 tx-flash clean config
