SRC=main.c adsb_encode.c
DEST=pluto-adsb-sim
OBJS=$(SRC:.c=.o)
LDFLAGS=-lm $(shell pkg-config --libs libiio libad9361)
CFLAGS=-g -Wall $(shell pkg-config --cflags libiio libad9361)

all: $(DEST)

$(DEST): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)
%.o: %.c
	$(CC) $(CFLAGS) -o $@ -c $<
clean:
	rm -f *.o $(DEST)
