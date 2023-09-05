TARGET=	telnetmkn
OBJS=	main.o

SHELL=	/bin/sh
CC=	c99
CFLAGS=	-D_XOPEN_SOURCE=700 -O -s
RM=	rm -f

.PHONY: all clean

all:	$(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $(TARGET) $(OBJS)

clean:
	$(RM) $(TARGET) $(OBJS)
