APP = launch-ctrl

# Add any other object files to this list below
APP_OBJS = launch-ctrl.o controls/control-interface.o

all: build

build: $(APP)

$(APP): $(APP_OBJS)
	$(CC) -o $@ $(APP_OBJS) $(LDFLAGS) $(LDLIBS)
clean:
	rm -f $(APP) *.o

