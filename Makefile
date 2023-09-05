TARGET=	telnetmkn
OBJS=	main.o

SHELL=	/bin/sh
CC=	cc
CFLAGS=	-O2
RM=	rm -f

.PHONY: all clean

all:	$(TARGET)

$(TARGET): $(OBJS)
	cc -o $(TARGET) $(OBJS)

clean:
	$(RM) $(TARGET) $(OBJS)
