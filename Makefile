CC	:= gcc

TOPDIR	:= .
INCDIR	:= $(TOPDIR)/include
LIBDIR	:= $(TOPDIR)/lib
SRCDIR	:= $(TOPDIR)/src
DSTDIR	:= $(TOPDIR)/dst

CFLAGS	:= -g -std=gnu99 -I$(INCDIR)
LDFLAGS	:= -pthread -lgcc_s

NCPUS :=

docker: $(TOPDIR)/docker.sh
	@$(TOPDIR)/docker.sh

all: $(DSTDIR)/symmetriccore $(DSTDIR)/specificcore $(DSTDIR)/singlecore

$(DSTDIR)/symmetriccore: $(DSTDIR)/symmetriccore.o
	@mkdir -p $(@D)
	$(CC) -o $@ $< $(LDFLAGS)

$(DSTDIR)/specificcore: $(DSTDIR)/specificcore.o $(DSTDIR)/ffwd.o
	@mkdir -p $(@D)
	$(CC) -o $@ $^ $(LDFLAGS)

$(DSTDIR)/singlecore: $(DSTDIR)/singlecore.o
	@mkdir -p $(@D)
	$(CC) -o $@ $^ $(LDFLAGS)

$(DSTDIR)/ffwd.o: $(LIBDIR)/ffwd.c
	@mkdir -p $(@D)
	$(CC) -o $@ -c $(CFLAGS) $<

$(DSTDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(@D)
	$(CC) -o $@ -c $(CFLAGS) $<

.DEFAULT_GOAL: all
.PHONY: symmetric specific single clean

symmetric: $(DSTDIR)/symmetriccore
	@$(DSTDIR)/symmetriccore $(NCPUS)

specific: $(DSTDIR)/specificcore
	@$(DSTDIR)/specificcore $(NCPUS)

single: $(DSTDIR)/singlecore $(NCPUS)
	@$(DSTDIR)/singlecore

clean:
	rm -rf $(DSTDIR)
