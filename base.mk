BASE_DIR:=$(shell pwd)
CFLAGS += -Wall -Wextra -Werror -O3 -I${BASE_DIR}
LDFLAGS +=
DESTDIR ?=
PREFIX ?= /usr
INSTALL_DIR := ${DESTDIR}${PREFIX}

BUILD_DIR?=build

ifdef LIBS
CFLAGS += $(shell pkg-config ${LIBS} --cflags)
LDFLAGS += $(shell pkg-config ${LIBS} --libs)
endif

ifndef VERBOSE
	V=@
endif

ifndef NAME
$(error "The NAME variable needs to be defined.")
else
BIN:=${BUILD_DIR}/${NAME}
endif


SOURCES:=${wildcard *.c} ${foreach dir,$(SUBDIRS),${wildcard $(dir)/*.c}}
HEADERS:=${wildcard *.c} ${foreach dir,$(SUBDIRS),${wildcard $(dir)/*.h}}
OBJECTS:=$(SOURCES:%.c=${BUILD_DIR}/%.o)
DEPS:=$(SOURCES:%.c=${BUILD_DIR}/%.d)
BUILD_DEP:=${BUILD_DIR}/phony

all: ${BIN}

# Create all the file hierarchy of folders in the build directory
${BUILD_DEP}:
	${V} mkdir -p ${BUILD_DIR}
	${V} ${foreach dir,$(SUBDIRS),mkdir -p ${BUILD_DIR}/$(dir)}
	${V} touch ${BUILD_DEP}

${BIN}: ${OBJECTS} ${BUILD_DEP}
	@echo "Linking $<…"
	${V} ${CC} -o $@ ${OBJECTS} ${LDFLAGS}


${BUILD_DIR}/%.o:%.c ${BUILD_DEP}
	@echo "Compiling $<…"
	${V} ${CC} -MMD -o $@ -c $< ${CFLAGS}

.PHONY: clean all install

clean:
	${V} rm -rf ${BUILD_DIR}

-include ${DEPS}