# the compiler
CC = gcc

# compiler flags:
#   -Wall        turns on most compiler warnings
#   -Wextra      adds extra warnings
#   -std=c99     compile to the c99 standard
#   -g           adds debugging information to the executable file

CFLAGS = -Wall -Wextra -std=c99 -g
LDFLAGS = -lpthread

# the build target executable
TARGET = project_4

SOURCES = time.c network.c cache.c project_4.c
OBJECTS = $(SOURCES:.c=.o)

.PHONY: all clean depend

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) $(OBJECTS) -o $(TARGET) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $<

clean:
	$(RM) $(OBJECTS) $(TARGET)

depend:
	makedepend -- $(CFLAGS) -- $(SOURCES)

# End of makefile
