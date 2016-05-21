CC=clang
LD=clang
CFLAGS=-g -std=gnu99 -Wall -pedantic
INCFLAG=-I$(INCDIR)/

SOURCES=address.c cli.c context.c control.c info.c load.c
SRCDIR=src
FULLSRCS=$(addprefix $(SRCDIR)/,$(SOURCES))
INCDIR=include
OBJECTS=$(patsubst %.c,%.o,$(SOURCES))
OBJDIR=build
FULLOBJS=$(addprefix $(OBJDIR)/,$(OBJECTS))
LIBS=-ldwarf -lelf -lreadline
NAME=tcd

.PHONY: all clean run

all: $(NAME)

$(NAME): $(FULLOBJS)
	$(LD) $(CFLAGS) $(FULLOBJS) -o $(NAME) $(LIBS)
	echo "$(NAME) is" `du -h $(NAME) | cut -f1` "in size"

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(FULLOBJS): $(OBJDIR)/%.o: $(SRCDIR)/%.c $(INCDIR)/tcd.h | $(OBJDIR)
	$(CC) $(CFLAGS) $(INCFLAG) -c $^ -o $@

clean:
	rm -f $(NAME) $(FULLOBJS)

run: $(NAME)
	./$(NAME)
