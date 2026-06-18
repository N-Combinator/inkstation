# InkStation — convenience targets. The real logic lives in deploy.sh so there
# is a single source of truth; these are just shortcuts.
#
#   make build         cross-compile build/inkstation.app
#   make deploy        wired (USB) build + install   (alias: deploy-usb)
#   make deploy-wifi   wireless build + install over WiFi (SCP); pass IP=...
#   make test          run the host parser unit tests
#   make clean         remove the build directory
#
# Examples:
#   make deploy
#   make deploy-wifi IP=192.168.1.42
#   make deploy-wifi FIND=1
#   make deploy-wifi DROP=1 IP=192.168.1.42 PIN=1234

SHELL := /usr/bin/env bash

# Optional knobs for the WiFi target.
IP   ?=
PIN  ?=
FIND ?=
DROP ?=

WIFI_ARGS :=
ifeq ($(FIND),1)
WIFI_ARGS += --find
endif
ifneq ($(strip $(IP)),)
WIFI_ARGS += --ip $(IP)
endif
ifeq ($(DROP),1)
WIFI_ARGS += --http-drop
endif
ifneq ($(strip $(PIN)),)
WIFI_ARGS += --pin $(PIN)
endif

.PHONY: build deploy deploy-usb deploy-wifi test clean

build:
	./deploy.sh --build-only

deploy: deploy-usb

deploy-usb:
	./deploy.sh --usb

deploy-wifi:
	./deploy.sh --wifi $(WIFI_ARGS)

test:
	bash tests/run_host_tests.sh

clean:
	rm -rf build
