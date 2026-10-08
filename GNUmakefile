# ProsperoStore - App configuration on the unmodified boilerplate build.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

APP_DEFINITIONS := GL_GLEXT_PROTOTYPES=1 STORE_NATIVE_CURL=1 HUI_EXTERNAL_LOG=1 STORE_INSTALLER=1 STORE_FILE_WORKER=1
PACBREW_PACKAGES := libcurl
APP_INCLUDE_PATHS := src .deps/ps5-opengl/current/include examples/self-update examples/update-check
APP_STATIC_ARCHIVES := .deps/ps5-opengl/libps5opengl-group.a
APP_IMPORT_STUBS := .deps/ps5-opengl/current/lib/libSceAgc.so .deps/ps5-opengl/current/lib/libSceAgcDriver.so build/system-keyboard/libSceCommonDialog.so
APP_WRAP_SYMBOLS := malloc calloc realloc free posix_memalign malloc_usable_size sceSystemServiceHideSplashScreen
APP_HEAP_SIZE := 0x10000000
APP_LAPY_HELPER := 1
STORE_WORKER := build/store-worker/store-worker.elf
SELF_UPDATE_HELPER := build/self-update/self-updater.elf
APP_ROOT_FILES := $(STORE_WORKER) $(SELF_UPDATE_HELPER)
DEVELOPMENT ?= 0
# The trace behind the "Debug log" setting (About, klog and debug-trace.txt) is in every
# build; it writes nothing until the setting is switched on.
APP_DEFINITIONS += STORE_DEBUG_TRACE=1
ifeq ($(DEVELOPMENT),1)
APP_DEFINITIONS += STORE_DEVELOPMENT=1
ifeq ($(SANDBOX_CONTROL),1)
APP_DEFINITIONS += STORE_SANDBOX_CONTROL=1
endif
endif

include Makefile

.PHONY: opengl host-snapshots foundations-check test-store
opengl:
	@bash tools/prepare-opengl.sh
app ffpkg ffpfsc packages: opengl system-keyboard-imports store-worker self-update-helper

# The ZIP handed to people: every entry stored as 0777, so a tool that keeps the
# archive permissions still leaves an app the console will start.
.PHONY: release-zip
release-zip: app
	@python3 tools/store-zip-modes.py dist/PPSA99000.zip

# The console limits how fast an app writes to its storage; this payload, sent
# to the loader for each job, unpacks and removes apps at full speed.
.PHONY: store-worker
store-worker:
	@printf '%s\n' '==> [store-worker] Building the file worker'
	@bash tools/setup-native-dependencies.sh >/dev/null
	@$(MAKE) --no-print-directory -C helper \
		PS5_PAYLOAD_SDK="$(abspath .deps/native/ps5-payload-sdk)" \
		OUTPUT="$(abspath $(STORE_WORKER))"
	@python3 tools/validate-loader-elf.py "$(STORE_WORKER)"

# The boilerplate's self-update helper: sent to the loader when the store
# updates itself, it swaps the store's files in place after the store closed.
.PHONY: self-update-helper
self-update-helper:
	@printf '%s\n' '==> [self-update] Building the update helper'
	@bash tools/setup-native-dependencies.sh >/dev/null
	@$(MAKE) --no-print-directory -s -C examples/self-update-helper \
		PS5_PAYLOAD_SDK="$(abspath .deps/native/ps5-payload-sdk)" \
		ROOT="$(abspath src)" OUTPUT="$(abspath $(SELF_UPDATE_HELPER))"
	@python3 tools/validate-loader-elf.py "$(SELF_UPDATE_HELPER)"

.PHONY: test-self-update
test-self-update:
	@mkdir -p build/tests/self-update
	@for name in miniz miniz_tinfl miniz_tdef miniz_zip; do \
		$(HOST_CC) -std=c11 -O2 -w -g -fsanitize=address,undefined \
			-c src/third_party/miniz/$$name.c -o build/tests/self-update/$$name.o || exit 1; \
	done
	@$(HOST_CXX) $(HOST_TEST_CXXFLAGS) -g -fsanitize=address,undefined -fno-sanitize-recover=all \
		-Isrc/third_party -Iexamples/self-update -Iexamples/update-check \
		tests/test_self_update.cpp examples/self-update-helper/updater.cpp \
		examples/self-update-helper/archive.cpp examples/self-update-helper/files.cpp \
		build/tests/self-update/*.o -pthread $(HOST_TEST_LDFLAGS) -o build/tests/test_self_update
	@build/tests/test_self_update
	@printf '%s\n' 'Self-update check, download, staging, apply and refusal checks passed.'

.PHONY: system-keyboard-imports
system-keyboard-imports:
	@bash examples/system-keyboard/build-import.sh >/dev/null

host-snapshots:
	@bash tools/store-host.sh

foundations-check:
	@python3 tools/check-foundations.py

test-store:
	@bash tools/store-test.sh

test: foundations-check test-store test-self-update

# The self-update kit test includes vendored and kit headers by name; the
# imported tidy script passes only src, so lint gets them through CPATH.
lint: export CPATH := $(abspath src/third_party):$(abspath examples/self-update):$(abspath examples/update-check)
