.DEFAULT_GOAL := all

BIN := camerademo_self
SRCS := $(wildcard *.cpp)
OBJS := $(SRCS:.cpp=.o)
DEPS := $(OBJS:.o=.d)
INCLUDES := -Iinclude -I$(STAGING_DIR)/usr/include/opencv4
LIBS := -lopencv_videoio -lopencv_imgproc -lopencv_core

.PHONY: all clean
all: $(BIN)

-include $(DEPS)

%.o: %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(BIN): $(OBJS)
	$(CXX) $(OBJS) $(LDFLAGS) $(LIBS) -o $@

clean:
	rm -f $(BIN) $(OBJS) $(DEPS)
