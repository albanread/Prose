# Every example, in order. "make" builds them all, "make clean" removes what
# was built. Each folder can also be built on its own, the same way.

EXAMPLES = \
	01-hello \
	02-hello-c++ \
	03-window \
	04-drawing \
	05-attributes \
	06-threads \
	07-game

all clean:
	@for example in $(EXAMPLES); do \
		echo "== $$example"; \
		$(MAKE) -C $$example $@ || exit 1; \
	done

.PHONY: all clean
