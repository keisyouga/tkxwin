# makefile for create libtkxwin.so

CFLAGS = -Wall -DUSE_TCL_STUBS -DUSE_TK_STUBS `pkg-config --cflags x11 tk` -fPIC
LDLIBS = `pkg-config --libs x11 tk`
LDFLAGS = -shared -o lib$(PROGRAM).so
PROGRAM = tkxwin
OBJS = tkxwin.o sendunicode.o

lib$(PROGRAM).so: $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) $(LDLIBS)
