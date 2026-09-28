# Miblo firmware — common tasks.
#
#   make build                      native tests + both images into dist/
#   make fleet-plan                 find units on the bench network, print what would happen
#   make fleet                      install Miblo on every stock/installer unit found
#   make fleet-update               also update units already on an older Miblo (asks each code)
#   make fleet HOSTS="192.168.0.41 192.168.0.42"   only these units
#
# Variables (override on the command line):
#   BOARD   board name in the image files          (default geekmagic_ultra)
#   SUBNET  network to scan, CIDR                   (default: this Mac's /24, auto-detected)
#   HOSTS   space-separated unit IPs; skips the scan when set
#   JOBS    units flashed in parallel               (default 50)
#   ARGS    extra flags passed to flash-fleet.py    (e.g. ARGS="--build 9d4215b")

BOARD  ?= geekmagic_ultra
JOBS   ?= 50
HOSTS  ?=
ARGS   ?=
PYTHON ?= python3

# Auto-detect the bench network: first active interface address, as a /24.
SUBNET ?= $(shell ip=$$(ipconfig getifaddr en0 2>/dev/null || ipconfig getifaddr en1 2>/dev/null || hostname -I 2>/dev/null | cut -d' ' -f1); \
          [ -n "$$ip" ] && echo "$${ip%.*}.0/24")

FLEET   := $(PYTHON) scripts/flash-fleet.py --board $(BOARD) --jobs $(JOBS)
TARGETS := $(if $(strip $(HOSTS)),$(foreach h,$(HOSTS),--host $(h)),--subnet $(SUBNET))

.PHONY: help build test fleet-plan fleet fleet-update fleet-test check-target

help:
	@sed -n '1,/^$$/p' Makefile | sed 's/^# \{0,1\}//'

build:
	./scripts/build.sh $(BOARD)

test:
	@if [ -x .venv/bin/pio ]; then .venv/bin/pio test -e native; else pio test -e native; fi
	$(PYTHON) -m unittest scripts/test_flash_fleet.py

check-target:
	@if [ -z "$(strip $(HOSTS))" ] && [ -z "$(SUBNET)" ]; then \
	  echo "Could not detect the network. Use SUBNET=192.168.0.0/24 or HOSTS=\"ip1 ip2\"." >&2; exit 2; fi
	@echo "Targets: $(TARGETS)"

fleet-plan: check-target
	$(FLEET) $(TARGETS) --dry-run $(ARGS)

fleet: check-target
	@ls dist/miblo-loader-$(BOARD)-*.bin dist/miblo-$(BOARD)-*.bin >/dev/null 2>&1 || $(MAKE) build
	$(FLEET) $(TARGETS) $(ARGS)

fleet-update: check-target
	@ls dist/miblo-$(BOARD)-*.bin >/dev/null 2>&1 || $(MAKE) build
	$(FLEET) $(TARGETS) --update $(ARGS)

fleet-test:
	$(PYTHON) -m unittest -v scripts/test_flash_fleet.py
