# OpenPocket: the same commands on macOS, Linux and Windows (Windows: build.bat, which calls this in Docker).
#   make build      firmware/openpocket.bin, the update loader and docs/firmware/*.fwsc + manifest.json
#   make test       the host tests (tests/run_tests.sh)
#   make package    build + build/SHA256SUMS of the outputs
#   make clean      remove every build output
#   make shell      a shell in the build container
#   make image      (re)build the container image
#   make serve      the installer and editor on http://localhost:8765/ (offline install)
#   make render IN=guitar.wav [OUT=out.wav] [PRESET=1]   a WAV through the engine
#   make appimage DUMP=fm1-1.bin   fm1-300.bin for the SD-card recovery
# Everything runs in the container (image openpocket-build). With the toolchain installed on the machine
# (firmware/setup.sh), `make NATIVE=1 build` skips Docker.
IMAGE   ?= openpocket-build
OUTS     = firmware/openpocket.bin docs/firmware/*.fwsc docs/firmware/manifest.json
ifeq ($(OP_IN_CONTAINER)$(NATIVE),)
RUN      = docker run --rm --platform linux/amd64 --user $(shell id -u):$(shell id -g) -e HOME=/tmp -v "$(CURDIR)":/src -w /src $(IMAGE)
IMG_DEP  = image
else
RUN      =
IMG_DEP  =
endif

.PHONY: build test package clean shell image serve render appimage
build: $(IMG_DEP)
	$(RUN) sh firmware/build.sh
test: $(IMG_DEP)
	$(RUN) sh tests/run_tests.sh
package: build
	$(RUN) sh -c 'cd /src && mkdir -p build && sha256sum $(OUTS) > build/SHA256SUMS && cat build/SHA256SUMS'
clean: $(IMG_DEP)
	$(RUN) sh -c 'rm -rf build firmware/build firmware/openpocket.bin docs/firmware/*.fwsc docs/firmware/manifest.json engine/px_render'
image:
	docker build --platform linux/amd64 -t $(IMAGE) .
shell: $(IMG_DEP)
	docker run --rm -it --platform linux/amd64 -v "$(CURDIR)":/src -w /src $(IMAGE) bash
serve:
	python3 tools/serve.py
render: $(IMG_DEP)
	@test -n "$(IN)" || { echo "usage: make render IN=guitar.wav [OUT=out.wav] [PRESET=1]"; exit 1; }
	$(RUN) bash engine/render.sh "$(IN)" "$(or $(OUT),$(basename $(IN))_OpenPocket.wav)" $(or $(PRESET),1)
appimage: $(IMG_DEP)
	@test -n "$(DUMP)" || { echo "usage: make appimage DUMP=fm1-1.bin"; exit 1; }
	$(RUN) python3 tools/pocketx_appimage.py "$(DUMP)" firmware/openpocket.bin fm1-300.bin
