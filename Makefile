.PHONY: all build headers test test-unit test-input install uninstall clean
all: build
build:
	./scripts/build
headers:
	./scripts/fetch-headers
test: build
	./scripts/test
test-unit:
	@mkdir -p out
	$(CXX) -std=c++17 -Wall -Wextra -Isrc tests/test-chord.cpp -o out/test-chord
	./out/test-chord
	./tests/test-packaging.sh
test-input: build
	./scripts/test-input
install:
	./scripts/install install
uninstall:
	./scripts/install uninstall
clean:
	rm -rf out
