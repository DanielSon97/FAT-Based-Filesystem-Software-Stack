# Target library
lib := libfs.a

all: $(lib)

# Avoid builtin rules and variables
MAKEFLAGS += -rR

# Don't print the commands unless explicitely requested with `make V=1`
ifneq ($(V),1)
Q = @
V = 0
endif

# Current directory
CUR_PWD := $(shell pwd)

# Define compilation toolchain
CC	= gcc

# General gcc options
CFLAGS	:= -Wall -Werror
CFLAGS	+= -pipe
## Debug flag
ifneq ($(D),1)
CFLAGS	+= -O2
else
CFLAGS	+= -g
endif

# Generate dependencies
DEPFLAGS = -MMD -MF $(@:.o=.d)

# Application objects to compile
my_objs := disk.o fs.o

# Include dependencies
deps := $(patsubst %.o,%.d,$(objs))
-include $(deps)

# Rule for libfs.a
$(lib): $(my_objs)
	ar rcs -o $@ $^

# Generic rule for compiling objects
%.o: %.c
	@echo "CC	$@"
	$(Q)$(CC) $(CFLAGS) $(INCLUDE) -c -o $@ $< $(DEPFLAGS)

# Cleaning rule
clean:
	@echo "CLEAN	$(CUR_PWD)"
	$(Q)rm -rf $(my_objs) $(deps) $(lib)

.PHONY: clean $(libuthread)