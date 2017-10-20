NAME = agentx1
EXEC = $(NAME)
RM ?= rm -f

OBJS = src/agentx1.o src/control_lan.o src/control_wan.o src/handle_packet.o
SRCS = $(OBJS:.o=.c)

CFLAGS += -W -Wall
LDFLAGS += -lpthread
LDLIBS += -lpthread

.c.o:
	$(CC) $(CFLAGS) -c -o $@ $<

all: $(EXEC)

$(EXEC): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

clean:
	-@$(RM) $(OBJS) $(EXEC)
