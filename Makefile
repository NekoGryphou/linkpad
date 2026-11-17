STEAMLINK_DIR ?= $(or $(XDG_DATA_HOME),$(HOME)/.local/share)/SteamLink
OUT_DIR ?= out

QT_LIB := $(STEAMLINK_DIR)/Qt-5.14.1/lib
QT_PLUGINS := $(STEAMLINK_DIR)/Qt-5.14.1/plugins
QT_HEADERS ?= $(patsubst %/QtCore/qglobal.h,%,$(firstword $(wildcard \
	.cache/qt-headers/usr/include/*/qt5/QtCore/qglobal.h \
	/usr/include/*/qt5/QtCore/qglobal.h /usr/include/qt5/QtCore/qglobal.h)))
JSON_HEADERS ?= $(patsubst %/nlohmann/json.hpp,%,$(firstword $(wildcard \
	.cache/json-headers/usr/include/nlohmann/json.hpp /usr/include/nlohmann/json.hpp)))

WARN := -Wall -Wextra
CXXSTD := -O2 -std=c++17
JSON_INC := -isystem "$(JSON_HEADERS)"
QT_INC := -I"$(QT_HEADERS)" $(foreach m,QtCore QtGui QtWidgets QtNetwork,-I"$(QT_HEADERS)/$(m)")
QT_LINK := -L"$(QT_LIB)" -Wl,-rpath-link,"$(QT_LIB)"
QT_LIBS := -l:libQt5Widgets.so.5 -l:libQt5Gui.so.5 -l:libQt5Core.so.5
QT_RUN := LD_LIBRARY_PATH="$(STEAMLINK_DIR)/lib:$(QT_LIB)" QT_QPA_PLATFORM=offscreen QT_PLUGIN_PATH="$(QT_PLUGINS)"
PRELOAD = LD_PRELOAD="$(abspath $(OUT_DIR))/bootstrap.so"

HEADERS := $(wildcard src/*.hpp)
UNIT_TESTS := config chord spawn native-records
QT_TESTS := home lifecycle input

.PHONY: all build headers headers-json test test-unit test-input install uninstall clean check-json check-qt
all: build
build: $(OUT_DIR)/bootstrap.so $(OUT_DIR)/native.so

headers:
	./scripts/fetch-headers all
headers-json:
	./scripts/fetch-headers json

check-json:
	@test -f "$(JSON_HEADERS)/nlohmann/json.hpp" || { \
		echo 'nlohmann/json headers missing: run make headers (or make headers-json), or set JSON_HEADERS.' >&2; exit 1; }
check-qt:
	@test -r "$(QT_LIB)/libQt5Core.so.5" || { echo "Missing bundled Qt in $(QT_LIB). Set STEAMLINK_DIR." >&2; exit 1; }
	@test -f "$(QT_HEADERS)/QtCore/qglobal.h" || { \
		echo 'Qt 5 headers missing: run make headers, or set QT_HEADERS to the directory containing QtCore.' >&2; exit 1; }

$(OUT_DIR):
	mkdir -p $@

$(OUT_DIR)/bootstrap.so: src/bootstrap.c | $(OUT_DIR)
	$(CC) -O2 $(WARN) -fPIC -shared $< -ldl -o $@

# Links against Valve's bundled Qt; undefined imports fail the link.
$(OUT_DIR)/native.so: src/native.cpp $(HEADERS) | $(OUT_DIR) check-qt check-json
	$(CXX) $(CXXSTD) $(WARN) -fPIC -shared -Isrc $(JSON_INC) $(QT_INC) $(QT_LINK) $< \
		-Wl,--no-undefined $(QT_LIBS) -l:libQt5Network.so.5 -o $@

# Test build only: exports hooks used by test-input.
$(OUT_DIR)/native-test.so: src/native.cpp $(HEADERS) | $(OUT_DIR) check-qt check-json
	$(CXX) $(CXXSTD) $(WARN) -DLINKPAD_TEST_HOOKS -fPIC -shared -Isrc $(JSON_INC) $(QT_INC) $(QT_LINK) $< \
		-Wl,--no-undefined $(QT_LIBS) -l:libQt5Network.so.5 -o $@

$(OUT_DIR)/command-target: tests/command-target.cpp | $(OUT_DIR)
	$(CXX) $(CXXSTD) $< -o $@

$(UNIT_TESTS:%=$(OUT_DIR)/test-%): $(OUT_DIR)/test-%: tests/test-%.cpp $(HEADERS) | $(OUT_DIR) check-json
	$(CXX) $(CXXSTD) $(WARN) -Werror -pthread -Isrc $(JSON_INC) $< -o $@

$(QT_TESTS:%=$(OUT_DIR)/test-%): $(OUT_DIR)/test-%: tests/test-%.cpp $(HEADERS) | $(OUT_DIR) check-qt
	$(CXX) $(CXXSTD) $(WARN) -fPIC -Isrc $(QT_INC) $(QT_LINK) $< $(QT_LIBS) -o $@

# Tests that need neither Qt nor Steam Link.
test-unit: $(UNIT_TESTS:%=$(OUT_DIR)/test-%) $(OUT_DIR)/command-target
	@for test in $(UNIT_TESTS); do $(OUT_DIR)/test-$$test || exit 1; done
	./tests/test-packaging.sh

test: test-unit build $(OUT_DIR)/test-home $(OUT_DIR)/test-lifecycle
	$(QT_RUN) $(OUT_DIR)/test-home
	stage=$$(mktemp -d) && trap 'rm -rf "$$stage"' EXIT && \
	printf '{"shortcuts": []}\n' > "$$stage/shortcuts.json" && \
	$(QT_RUN) LINKPAD_CONFIG="$$stage/shortcuts.json" LINKPAD_SOCKET="$$stage/state.sock" \
		LINKPAD_MODULE="$(abspath $(OUT_DIR))/native.so" $(PRELOAD) timeout 10s $(OUT_DIR)/test-lifecycle

# Virtual controllers through /dev/uinput; stop other client instances first.
test-input: $(OUT_DIR)/bootstrap.so $(OUT_DIR)/native-test.so $(OUT_DIR)/test-input $(OUT_DIR)/command-target
	stage=$$(mktemp -d) && trap 'rm -rf "$$stage"' EXIT && \
	$(QT_RUN) LINKPAD_CONFIG="$$stage/shortcuts.json" LINKPAD_SOCKET="$$stage/state.sock" \
		LINKPAD_MODULE="$(abspath $(OUT_DIR))/native-test.so" $(PRELOAD) timeout 40s $(OUT_DIR)/test-input

install:
	OUT_DIR="$(OUT_DIR)" ./scripts/install install
uninstall:
	./scripts/install uninstall

clean:
	rm -rf $(OUT_DIR)
