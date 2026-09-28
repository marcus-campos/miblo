# Miblo firmware — common tasks.
#
#   make build                      native tests + both images into dist/
#   make release                    ask the version, test, build, commit; then offer tag + GitHub release
#   make fleet-plan                 find units on the bench network, print what would happen
#   make fleet                      install Miblo on every stock/installer unit found
#   make fleet-update               also update units already on an older Miblo (asks each code)
#   make fleet HOSTS="192.168.0.41 192.168.0.42"   only these units
#   make fleet-ap-plan              scan nearby Wi-Fi for GIFTV/SmallTV/GeekMagic/Miblo-* units (no joining)
#   make fleet-ap                   flash/update every unit through its own access point (sequential)
#   make fleet-ap-loop              keep scanning: plug units in one after another (Ctrl-C to stop)
#   make fleet-ap SSIDS="Miblo-Setup-4F2A GIFTV"   only these unit networks, joined by name (no scan)
#   make fleet-ap-scan              print the Wi-Fi scan (builds the MibloWiFiScan helper on first use)
#
# The builds are refreshed automatically: fleet targets run `make build` unless dist/ holds both
# images for the sources' MIBLO_FW_VERSION and they are newer than every firmware source file.
# LAN installs leave units in setup mode (Wi-Fi erased); on macOS the Mac then joins each unit's
# Miblo-Setup-XXXX to check it (ARGS=--keep-wifi keeps them on the LAN, --no-ap-verify skips it).
# Access-point mode scans through a small helper app: allow "MibloWiFiScan" to use your location
# when macOS asks (System Settings -> Privacy & Security -> Location Services).
#
# Variables (override on the command line):
#   BOARD   board name in the image files          (default geekmagic_ultra)
#   SUBNET  network to scan, CIDR                   (default: this Mac's /24, auto-detected)
#   HOSTS   space-separated unit IPs; skips the scan when set
#   JOBS    units flashed in parallel               (default 50)
#   SSIDS   unit networks for the fleet-ap targets, each passed as --ssid (no scan needed)
#   ARGS    extra flags passed to flash-fleet.py    (e.g. ARGS="--build 9d4215b", ARGS="--try-ssid MyTV")
#   VERSION release version; skips the question   (e.g. make release VERSION=0.3.0)

BOARD  ?= geekmagic_ultra
JOBS   ?= 50
HOSTS  ?=
SSIDS  ?=
ARGS   ?=
PYTHON ?= python3

# Auto-detect the bench network: first active interface address, as a /24.
SUBNET ?= $(shell ip=$$(ipconfig getifaddr en0 2>/dev/null || ipconfig getifaddr en1 2>/dev/null || hostname -I 2>/dev/null | cut -d' ' -f1); \
          [ -n "$$ip" ] && echo "$${ip%.*}.0/24")

FLEET   := $(PYTHON) scripts/flash-fleet.py --board $(BOARD) --jobs $(JOBS)
TARGETS := $(if $(strip $(HOSTS)),$(foreach h,$(HOSTS),--host $(h)),--subnet $(SUBNET))
AP      := $(PYTHON) scripts/flash-fleet.py --board $(BOARD) --via-ap $(foreach s,$(SSIDS),--ssid $(s))

# Images for the version the sources declare; rebuilt when missing or older than any source.
VER     := $(shell sed -n 's/.*MIBLO_FW_VERSION "\(.*\)".*/\1/p' include/miblo_version.h)
IMAGES  := dist/miblo-$(BOARD)-$(VER).bin dist/miblo-loader-$(BOARD)-$(VER).bin
SOURCES := src lib/miblo_core lib/miblo_ui loader include platformio.ini
define ensure_images
	@stale=; for f in $(IMAGES); do \
	  if [ ! -f "$$f" ] || [ -n "$$(find $(SOURCES) -type f -newer "$$f" 2>/dev/null | head -n 1)" ]; then stale=1; fi; \
	done; \
	if [ -n "$$stale" ]; then echo "Images for $(VER) missing or older than the sources: building..."; \
	  $(MAKE) build || exit 1; else echo "Images for $(VER) are up to date."; fi
endef

.PHONY: help build test release images fleet-ap-plan fleet-ap fleet-ap-loop fleet-ap-scan fleet-plan fleet fleet-update fleet-test check-target

help:
	@sed -n '1,/^$$/p' Makefile | sed 's/^# \{0,1\}//'

build:
	./scripts/build.sh $(BOARD)

images:
	$(ensure_images)

test:
	@if [ -x .venv/bin/pio ]; then .venv/bin/pio test -e native; else pio test -e native; fi
	$(PYTHON) -m unittest scripts/test_flash_fleet.py

check-target:
	@if [ -z "$(strip $(HOSTS))" ] && [ -z "$(SUBNET)" ]; then \
	  echo "Could not detect the network. Use SUBNET=192.168.0.0/24 or HOSTS=\"ip1 ip2\"." >&2; exit 2; fi
	@echo "Targets: $(TARGETS)"

fleet-plan: check-target
	$(FLEET) $(TARGETS) --dry-run $(ARGS)

fleet: check-target images
	$(FLEET) $(TARGETS) $(ARGS)

fleet-update: check-target images
	$(FLEET) $(TARGETS) --update $(ARGS)

fleet-test:
	$(PYTHON) -m unittest -v scripts/test_flash_fleet.py

VERSION ?=

# Release: choose the version (patch/minor/major/keep/other, Enter = patch), run every test,
# build both images, commit the version bump, then ask before tagging and pushing. Pushing the
# vX.Y.Z tag triggers .github/workflows/release.yml, which rebuilds and publishes the release.
release:
	@set -e; cd ..; \
	if [ -n "$$(git status --porcelain --untracked-files=no)" ]; then \
	  echo "Working tree has uncommitted changes. Commit or stash them first." >&2; git status --short >&2; exit 1; fi; \
	branch=$$(git rev-parse --abbrev-ref HEAD); \
	[ "$$branch" = "main" ] || echo "Note: releasing from branch '$$branch' (not main)." >&2; \
	version=$$($(PYTHON) firmware/scripts/bump-version.py $(VERSION)); \
	tag="v$$version"; \
	if git rev-parse -q --verify "refs/tags/$$tag" >/dev/null; then echo "Tag $$tag already exists." >&2; exit 1; fi; \
	echo "== Release $$tag =="; \
	(cd plugin && npm test >/dev/null) && echo "plugin tests: ok"; \
	$(PYTHON) -m unittest -q firmware/scripts/test_flash_fleet.py 2>/dev/null && echo "script tests: ok"; \
	(cd firmware && ./scripts/build.sh $(BOARD)); \
	if [ -n "$$(git status --porcelain firmware/include/miblo_version.h plugin/.claude-plugin/plugin.json)" ]; then \
	  git add firmware/include/miblo_version.h plugin/.claude-plugin/plugin.json; \
	  git commit -q -m "chore: release $$tag"; echo "committed: chore: release $$tag"; fi; \
	echo; echo "Images in firmware/dist/:"; ls -1 firmware/dist/*-$$version.bin; echo; \
	if [ ! -t 0 ]; then echo "No terminal — not tagging. To publish: git tag -a $$tag -m $$tag && git push origin $$branch $$tag"; exit 0; fi; \
	printf "Create tag $$tag and publish the GitHub release now? (pushes $$branch + tag) [y/N] "; read ans; \
	case "$$ans" in y|Y|yes|s|S|sim) \
	  git tag -a "$$tag" -m "Miblo $$tag"; \
	  git push origin "$$branch" "$$tag"; \
	  echo "Pushed $$tag. GitHub Actions is building the release:"; \
	  command -v gh >/dev/null && gh run list --workflow release.yml --limit 1 || true; \
	  echo "https://github.com/marcus-campos/miblo/releases/tag/$$tag";; \
	*) echo "Not tagged. Later: git tag -a $$tag -m $$tag && git push origin $$branch $$tag";; esac

# Access-point mode (macOS): the Mac joins each unit's own Wi-Fi, flashes it at 192.168.4.1 and
# rejoins the original network at the end. The scan runs in the MibloWiFiScan helper app (allow
# its location access once); SSIDS="..." skips scanning.
fleet-ap-plan:
	$(AP) --dry-run $(ARGS)

fleet-ap-scan:
	$(PYTHON) scripts/flash-fleet.py --board $(BOARD) --via-ap --scan-only $(ARGS)

fleet-ap: images
	$(AP) $(ARGS)

fleet-ap-loop: images
	$(AP) --loop $(ARGS)
