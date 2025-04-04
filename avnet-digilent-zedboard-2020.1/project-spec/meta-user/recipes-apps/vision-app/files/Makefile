# files/Makefile

APP = vision-app
APP_OBJS = vision-app.o

# Add necessary OpenCV libraries here for linking
# Adjust this list based on the specific OpenCV functions you use
OPENCV_LIBS = -lopencv_core -lopencv_imgproc -lopencv_highgui

all: build

build: $(APP)

$(APP): $(APP_OBJS)
# Use CXX and LDFLAGS from environment, add specific OpenCV libs
	$(CXX) $(LDFLAGS) -o $@ $(APP_OBJS) $(OPENCV_LIBS) $(LDLIBS)

clean:
	rm -f $(APP) *.o
