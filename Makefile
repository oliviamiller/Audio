OUTPUT_NAME = audio-module
BIN := build-conan/build/RelWithDebInfo/audio-module

.PHONY: build lint setup appimage test

build: $(BIN)


build/build.ninja: build CMakeLists.txt
	cd build && cmake -G Ninja -DVIAM_REALSENSE_ENABLE_SANITIZER=$(SANITIZE) -DCMAKE_BUILD_TYPE=RelWithDebInfo ..

$(BIN): conanfile.py src/* bin/* test/*
	bin/build.sh

test: $(BIN)
	cd build-conan/build/RelWithDebInfo && ctest --output-on-failure

clean:
	rm -rf packaging/appimages/deploy module.tar.gz

setup:
	bin/setup.sh
