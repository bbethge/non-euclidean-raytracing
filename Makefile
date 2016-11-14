SED = sed
PKG_CONFIG = pkg-config
ifdef WIN32
	GL_LIBS = -lopengl32
	MAIN = main.exe
else ifdef WIN64
	GL_LIBS = -lopengl64
	MAIN = main.exe
else
	GL_LIBS = -lGL
	MAIN = main
endif

all: $(MAIN)

$(MAIN): main.o strings.o
	$(CC) -o $@ $^ $$($(PKG_CONFIG) --libs sdl2 glew) $(GL_LIBS) -lm \
		-lgslcblas

%.o: CFLAGS += $$($(PKG_CONFIG) --cflags sdl2 glew)

strings.c: *.glsl
	$(RM) $@
	for file in $^; do \
		$(SED) -e "s/\(.*\)/\"\\1\\\\n\"/; \$$s/\$$/;/; \
			1s/^/const char $${file%%.glsl}_source[] =\\n/" \
			$$file >> $@; \
	done

.PHONY: clean
clean:
	$(RM) main main.exe strings.c main.o strings.o
