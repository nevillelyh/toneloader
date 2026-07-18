IMAGE := toneloader-build
BUILD := build

.PHONY: all image configure build test validate install clean

all: build

image:
	docker build -t $(IMAGE) .

configure: image
	docker run --rm -v "$(CURDIR):/src" $(IMAGE) cmake -S . -B $(BUILD) -DCMAKE_BUILD_TYPE=Release

build: configure
	docker run --rm -v "$(CURDIR):/src" $(IMAGE) cmake --build $(BUILD) --parallel

test: build
	docker run --rm -v "$(CURDIR):/src" $(IMAGE) ctest --test-dir $(BUILD) --output-on-failure

validate: build
	docker run --rm -e LV2_PATH=/src/$(BUILD)/lv2 -v "$(CURDIR):/src" $(IMAGE) lv2info urn:neville:toneloader

install: build
	mkdir -p "$(HOME)/.lv2"
	cp -a $(BUILD)/lv2/ToneLoader.lv2 "$(HOME)/.lv2/"

clean:
	rm -rf $(BUILD)
