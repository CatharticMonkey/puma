.DEFAULT_GOAL := all

WORKINGDIR	:= $(shell pwd)

SRCDIR		:= src
MODULES		:= $(subst src/,,$(shell find $(SRCDIR)/* -type d))
BINDIR		:= bin
BUILDDIR	:= build
INCDIRS		:= include

DOCDIR		:= docs

BUILDMODS	:= $(addprefix $(BUILDDIR)/,$(MODULES))
BINMODS		:= $(addprefix $(BINDIR)/,$(MODULES))

CSRCS		:= $(shell find $(SRCDIR) -name '*.c')
CXXSRCS		:= $(shell find $(SRCDIR) -name '*.cpp')
SRCS		:= $(CSRCS) $(CXXSRCS)

HEADERS		:= $(shell find $(INCDIRS) -name '*.h')

COBJECTS 	:= $(subst src,$(BUILDDIR),$(CSRCS:%.c=%.c.o))
CXXOBJECTS 	:= $(subst src,$(BUILDDIR),$(CXXSRCS:%.cpp=%.cpp.o))
OBJECTS		:= $(COBJECTS) $(CXXOBJECTS)

INCFLAGS	= $(addprefix -I,$(INCDIRS))

CFLAGS		= -std=gnu99 -Wunused-variable -g -fPIC -O2 -Wall -Wextra -Werror -pedantic
CXXFLAGS	= -std=c++11 -Wunused-variable -g -fPIC -O2 -Wall -Wextra -Werror -pedantic

LDFLAGS		= -shared -pthread

FOLDERS		= $(BINDIR) $(BINMODS) $(BUILDDIR) $(BUILDMODS)

DOXYGEN_CONF	:= $(DOCDIR)/doxygen.cfg

VALGRIND			?= 1
DEBUG				?= 0
STATIC_THREADPOOL	?= 0
NUMA				?= 1
OPENMP				?= 1

OS := $(shell uname -s)
ifeq ($(OS),Linux)
	CFLAGS		+= -axSSE4.1
	CXXFLAGS	+= -axSSE4.1
	LDFLAGS		+= -lnuma -lrt -shared-intel
	CC			= icc -x c
	CXX			= icc -x c++ -cxxlib
	LINKER		= icc
	EXT			= so
else ifeq ($(OS),Darwin)
	VALGRIND	= 0
	NUMA		= 0
	OPENMP		= 0
	CC			= gcc
	CXX			= gcc -x c++
	LINKER		= gcc
	EXT			= dylib
endif

ifeq ($(VALGRIND),0)
	CFLAGS		+= -DNOVALGRIND
	CXXFLAGS	+= -DNOVALGRIND
endif

ifeq ($(DEBUG),0)
	CFLAGS		+= -DNDEBUG
	CXXFLAGS	+= -DNNDEBUG
endif

ifdef PUMA_MINNODEPAGES
	CFLAGS		+= -DPUMA_MINNODEPAGES=$(PUMA_MINNODEPAGES)
	CXXFLAGS	+= -DPUMA_MINNODEPAGES=$(PUMA_MINNODEPAGES)
endif

ifeq ($(STATIC_THREADPOOL),1)
	CFLAGS		+= -DSTATIC_THREADPOOL
	CXXFLAGS	+= -DSTATIC_THREADPOOL
endif

ifeq ($(NUMA),0)
	CFLAGS		+= -DNNUMA
	CXXFLAGS	+= -DNNUMA
endif

ifeq ($(OPENMP),0)
	CFLAGS		+= -DNOOPENMP
else
	CFLAGS		+= -openmp
	LDFLAGS 	+= -openmp
endif

TARGET		:= $(BINDIR)/libpuma.$(EXT)

include tests/test.mk

.PHONY: all clean no_test doc docs_clean

all: $(TARGET) doc test

no_test: $(TARGET)

$(FOLDERS):
	@mkdir -p $(FOLDERS)

doc:
	@echo "Running doxygen"
	@doxygen $(DOXYGEN_CONF) 1> /dev/null

$(TARGET): $(OBJECTS) | $(FOLDERS)
	@echo "Linking $@"
	@$(LINKER) -o $@ $^ $(LDFLAGS)

$(BUILDDIR)/%.cpp.o: src/%.cpp | $(FOLDERS)
	@echo "Compiling $<"
	@$(CXX) $(INCFLAGS) $(DEFS) $(CXXFLAGS) -c $< -o $@

$(BUILDDIR)/%.c.o: src/%.c | $(FOLDERS)
	@echo "Compiling $<"
	@$(CC) $(INCFLAGS) $(DEFS) $(CFLAGS) -c $< -o $@

$(SRCS): $(HEADERS)

docs_clean:
	@find docs/* -maxdepth 0 | grep -v "doxygen.cfg" | xargs rm -rf

clean: test_clean docs_clean
	@echo "Cleaning working tree"
	@rm -rf $(BUILDDIR) $(BINDIR)
