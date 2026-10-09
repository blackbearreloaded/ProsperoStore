# ps5-native-app-boilerplate - Linux/WSL build entry points.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

SHELL := /bin/bash
.DEFAULT_GOAL := app

-include .env

APP_DEFINITIONS ?=
APP_INCLUDE_PATHS ?=
APP_STATIC_ARCHIVES ?=
APP_IMPORT_STUBS ?=
APP_WRAP_SYMBOLS ?=
APP_HEAP_SIZE ?= 0xffffffffffffffff
APP_RUNTIME_MODULES ?=
APP_SOURCE_DIR ?=
APP_PARAM ?=
APP_SCE_SYS ?=
APP_ASSETS ?= assets
APP_ROOT_FILES ?=
APP_LAPY_HELPER ?= 0
PACBREW_PACKAGES ?=
PACBREW_INCLUDE_PATHS ?=
PACBREW_STATIC_ARCHIVES ?=
PS5_HOST ?=
FTP_PORT ?= 2121
PS5_FTP_USER ?= anonymous
PS5_FTP_PASSWORD ?= codex
DEPLOY_DRY_RUN ?= 0
TITLE_ID ?=
APP_NAME ?=
APP_CATEGORY ?= game
CONTENT_SUFFIX ?=
HOST_CC ?= clang
HOST_CXX ?= clang++
HOST_TEST_CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror \
	-ffunction-sections -fdata-sections
HOST_TEST_CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror \
	-ffunction-sections -fdata-sections
HOST_TEST_LDFLAGS ?= -Wl,--gc-sections
GTEST_ARGS ?=
BUILD_JOBS ?= $(shell nproc 2>/dev/null || echo 2)
USE_CCACHE ?= 1
export BUILD_JOBS USE_CCACHE
export HOST_CXX HOST_TEST_CXXFLAGS HOST_TEST_LDFLAGS
export APP_DEFINITIONS APP_INCLUDE_PATHS APP_STATIC_ARCHIVES APP_RUNTIME_MODULES
export APP_IMPORT_STUBS APP_WRAP_SYMBOLS APP_HEAP_SIZE
export APP_SOURCE_DIR APP_PARAM APP_SCE_SYS APP_ASSETS APP_ROOT_FILES
export APP_LAPY_HELPER
export PACBREW_PACKAGES PACBREW_INCLUDE_PATHS PACBREW_STATIC_ARCHIVES
export PS5_HOST FTP_PORT PS5_FTP_USER PS5_FTP_PASSWORD DEPLOY_DRY_RUN
export TITLE_ID APP_NAME APP_CATEGORY CONTENT_SUFFIX

RUNTIME := runtime/libc.prx
RUNTIME_INPUTS := tools/rebuild-libc.sh tools/build-host-tools.sh tools/ninja-build.sh \
	$(wildcard tooling/native/*.cpp tooling/native/*.hpp) \
	$(wildcard tooling/native/runtime/*.txt)
HOST_UNIT_TEST := build/tests/demo_renderer_tests

.PHONY: all app build init doctor test test-deps test-unit test-integration libc deps pacbrew pacbrew-list assets-check format format-check tidy lint check update-check-example test-update-check deploy undeploy clean distclean help

all: app
build: app

init:
	@printf '%s\n' '==> [init] Configuring the application identity in sce_sys/param.json'
	@bash tools/init-project.sh sce_sys/param.json

doctor:
	@printf '%s\n' '==> [doctor] Checking the Linux/WSL host without changing it'
	@bash tools/doctor.sh

test: test-unit test-integration test-elevation test-update-check test-crash-report test-https-trust test-pacbrew-curl

.PHONY: test-pacbrew-curl
test-pacbrew-curl:
	@bash tools/test-pacbrew-curl.sh

.PHONY: test-https-trust
test-https-trust:
	@mkdir -p build/tests
	@$(HOST_CXX) $(HOST_TEST_CXXFLAGS) -fsanitize=address,undefined tests/test_https_trust.cpp \
		examples/https-trust/https_trust.cpp -o build/tests/test-https-trust
	@build/tests/test-https-trust

.PHONY: test-crash-report
test-crash-report:
	@mkdir -p build/tests
	@$(HOST_CXX) $(HOST_TEST_CXXFLAGS) -pthread tests/test_crash_report.cpp \
		examples/crash-report/crash_report.cpp -o build/tests/test-crash-report
	@build/tests/test-crash-report

.PHONY: test-elevation
test-elevation:
	@bash tools/setup-native-dependencies.sh >/dev/null
	@mkdir -p build/tests
	@$(HOST_CXX) $(HOST_TEST_CXXFLAGS) -idirafter .deps/native/ps5-payload-sdk/target/include \
		tests/test_elevation.cpp $(HOST_TEST_LDFLAGS) -o build/tests/test_elevation
	@build/tests/test_elevation
	@printf '%s\n' 'Resident/one-shot Lapy client, exchange, and proof checks passed.'

test-update-check:
	@mkdir -p build/tests
	@$(HOST_CXX) $(HOST_TEST_CXXFLAGS) -g -fsanitize=address,undefined -fno-sanitize-recover=all \
		tests/test_update_check.cpp $(HOST_TEST_LDFLAGS) -o build/tests/test_update_check
	@build/tests/test_update_check
	@printf '%s\n' 'Update-check version, parsing and decision checks passed.'
test-deps:
	@printf '%s\n' '==> [test-deps] Fetching the pinned host-only GoogleTest source'
	@bash tools/setup-test-dependencies.sh >/dev/null

test-unit:
	@bash tools/build-tests.sh
	@printf '%s\n' '==> [test-unit] Running host-native GoogleTest application tests'
	@$(HOST_UNIT_TEST) $(GTEST_ARGS)

test-integration:
	@printf '%s\n' '==> [test-integration] Running host tooling integration tests'
	@python3 -m unittest discover -s tests -p 'test_*.py' -v

deps: test-deps
	@printf '%s\n' '==> [deps] Fetching declared native dependencies'
	@bash tools/setup-native-dependencies.sh
	@bash tools/setup-pacbrew-dependencies.sh --environment

pacbrew:
	@printf '%s\n' '==> [pacbrew] Fetching the pinned prebuilt ports sysroot'
	@bash tools/setup-pacbrew-dependencies.sh --all

pacbrew-list:
	@printf '%s\n' '==> [pacbrew] Listing available pkg-config modules'
	@bash tools/setup-pacbrew-dependencies.sh --list

assets-check:
	@printf '%s\n' '==> [assets] Validating icon, backgrounds, and selection audio'
	@bash tools/validate-assets.sh

libc:
	@printf '%s\n' '==> [libc] Rebuilding and verifying the clean-room runtime'
	@bash tools/rebuild-libc.sh

$(RUNTIME): $(RUNTIME_INPUTS)
	@printf '%s\n' '==> [libc] Generating the missing or outdated runtime'
	@bash tools/rebuild-libc.sh

app: $(RUNTIME)
	@printf '%s\n' '==> [app] Compiling, linking, signing, and assembling the app folder'
	@bash tools/build.sh Folder

update-check-example: $(RUNTIME)
	@printf '%s\n' '==> [update-check] Building the catalog update-check example title'
	@APP_SOURCE_DIR=examples/update-check \
		APP_PARAM=examples/update-check/sce_sys/param.json \
		APP_SCE_SYS=sce_sys APP_ASSETS=examples/update-check/assets APP_LAPY_HELPER=0 \
		bash tools/build.sh Folder
deploy:
	@printf '%s\n' '==> [deploy] Building and publishing the app folder over FTP'
	@bash tools/deploy.sh

undeploy:
	@printf '%s\n' '==> [undeploy] Removing staged development files for this title over FTP'
	@bash tools/deploy.sh undeploy

format:
	@printf '%s\n' '==> [format] Formatting C and C++ sources'
	@bash tools/run_clang_format.sh

format-check:
	@printf '%s\n' '==> [format] Checking C and C++ formatting'
	@bash tools/run_clang_format.sh --check

tidy:
	@printf '%s\n' '==> [tidy] Running Clang static analysis'
	@bash tools/run_clang_tidy.sh

lint:
	@printf '%s\n' '==> [lint] Running source, metadata, and shell checks'
	@bash tools/lint.sh

check: lint test app

clean:
	@printf '%s\n' '==> [clean] Removing generated build outputs'
	@rm -rf -- build dist
	@rm -f -- $(RUNTIME)

distclean: clean
	@printf '%s\n' '==> [distclean] Removing downloaded dependency caches'
	@rm -rf -- .deps

help:
	@printf '%s\n' \
	  'make                 Generate libc.prx and build the Hello World folder' \
	  'make init TITLE_ID=PPSA12345 APP_NAME="My App"  Configure app identity' \
	  'make doctor          Check required and optional Linux/WSL tools' \
	  'make test            Run all host unit and integration tests' \
	  'make test-deps       Fetch verified host-only GoogleTest source' \
	  'make test-unit       Run host-native GoogleTest application tests' \
	  'make test-integration  Run host tooling integration tests' \
	  'make deps            Fetch native dependencies into .deps/' \
	  'make pacbrew         Fetch the pinned PacBrew ports sysroot' \
	  'make pacbrew-list    List PacBrew pkg-config module names' \
	  'make assets-check    Validate the current presentation assets' \
	  'make libc            Force a deterministic runtime/libc.prx rebuild' \
	  'make format          Apply the shared Clang formatting policy' \
	  'make format-check    Check formatting without modifying files' \
	  'make tidy            Run the shared Clang static-analysis policy' \
	  'make lint            Run format, tidy, metadata, and shell checks' \
	  'make check           Run lint and build the skeleton app' \
	  'make update-check-example  Build the catalog update-check example title' \
	  'make test-update-check     Run the update-check host tests' \
	  'make deploy PS5_HOST=<address>  Build and FTP-deploy the app folder' \
	  'make undeploy PS5_HOST=<address>  Remove this title from /data/homebrew' \
	  'Build variables:     APP_DEFINITIONS, APP_INCLUDE_PATHS, APP_STATIC_ARCHIVES, APP_RUNTIME_MODULES, APP_LAPY_HELPER' \
	  'PacBrew variables:   PACBREW_PACKAGES, PACBREW_INCLUDE_PATHS, PACBREW_STATIC_ARCHIVES' \
	  'Deploy variables:    FTP_PORT=2121, DEPLOY_DRY_RUN=0|1' \
	  'Local defaults:      Copy .env.example to the ignored .env file' \
	  'Build speed:         BUILD_JOBS defaults to all CPUs; USE_CCACHE=0 disables ccache' \
	  'make clean           Remove build/, dist/, and generated libc.prx' \
	  'make distclean       Also remove the ignored .deps/ cache'
