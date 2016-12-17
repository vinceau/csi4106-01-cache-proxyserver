# the compiler
CC = gcc

# compiler flags:
#   -Wall        turns on most compiler warnings
#   -Wextra      adds extra warnings
#   -std=c99     compile to the c99 standard
#   -g           adds debugging information to the executable file

CFLAGS = -Wall -Wextra -std=c99 -g -lpthread

# the build target executable
TARGET = project_4

SOURCES = project_4.c time.c

all: $(TARGET) 

$(TARGET): $(TARGET).c
	$(CC) $(SOURCES) $(CFLAGS) -o $(TARGET)

clean:
	$(RM) $(TARGET) 
