.DEFAULT_GOAL := all

BIN := yolov8
SRCS := $(wildcard *.cpp)
OBJS := $(SRCS:.cpp=.o)
DEPS := $(OBJS:.o=.d)
INCLUDES := -Iinclude -I$(STAGING_DIR)/usr/include/opencv4
LIBS := -lVIPlite -lopencv_videoio -lopencv_imgproc -lopencv_dnn -lopencv_core -lpthread

.PHONY: all clean
all: $(BIN)

-include $(DEPS)

%.o: %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(BIN): $(OBJS)
	$(CXX) $(OBJS) $(LDFLAGS) $(LIBS) -o $@

clean:
	rm -f $(BIN) $(OBJS) $(DEPS)
