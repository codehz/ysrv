LSS = $(wildcard js/*.ls)
JSS = $(LSS:js/%.ls=build/%.js)

all: $(JSS)

build/%.js: js/%.ls
	lsc -o build -c $<

watch:
	while true; do \
		make; \
		inotifywait -qre close_write js; \
	done