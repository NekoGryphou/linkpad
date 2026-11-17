.PHONY: all build headers headers-json test test-unit test-input install uninstall clean
all: build
build:
	./scripts/build
headers:
	./scripts/fetch-headers all
headers-json:
	./scripts/fetch-headers json
test: build
	./scripts/test
test-unit:
	./scripts/test-unit
test-input: build
	./scripts/test-input
install:
	./scripts/install install
uninstall:
	./scripts/install uninstall
clean:
	rm -rf out
