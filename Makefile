MODS = gtk+-2.0
INCS = `pkg-config ${MODS} --cflags`
LIBS = `pkg-config ${MODS} --libs`
CFLAGS = -Wall -ansi -pedantic ${INCS}
OBJS = main.o
BIN = detree

all: ${BIN}

.c.o:
	${CC} -c ${CFLAGS} -o $@ $<

${BIN}: ${OBJS}
	${CC} -o $@ ${OBJS} ${LIBS}

clean:
	rm -f ${OBJS} ${BIN}
