# Thanks to Job Vranish (https://spin.atomicobject.com/2016/08/26/makefile-c-projects/)
TARGET_EXEC := main

BUILD_DIR := ./build
SHADER_BUILD_DIR := $(BUILD_DIR)/shaders
SRC_DIRS := ./include ./src

LDFLAGS := -lm -lvulkan -ldl -lpthread -lX11 -lxcb -lXxf86vm -lXrandr -lXi -fsanitize=address -fsanitize=leak
CFLAGS := -O0 -fsanitize=address -fsanitize=leak -std=gnu23

# Find all the C and C++ files we want to compile
# Note the single quotes around the * expressions. The shell will incorrectly expand these otherwise, but we want to send the * directly to the find command.
SRCS := $(shell find $(SRC_DIRS) -name '*.cpp' -or -name '*.c' -or -name '*.s')
SHADER_SRCS :=$(shell find $(SRC_DIRS) -name '*.glsl' -or -name '*.frag' -or -name '*.vert')
SHADER_FILES := $(notdir $(SHADER_SRCS))

# Prepends BUILD_DIR and appends .o to every src file
# As an example, ./your_dir/hello.cpp turns into ./build/./your_dir/hello.cpp.o
OBJS := $(SRCS:%=$(BUILD_DIR)/%.o)
SHADER_OBJS := $(SHADER_FILES:%=$(SHADER_BUILD_DIR)/%.spv)

# Every folder in ./src will need to be passed to GCC so that it can find header files
INC_DIRS := $(shell find $(SRC_DIRS) -type d)
# Add a prefix to INC_DIRS. So moduleA would become -ImoduleA. GCC understands this -I flag
INC_FLAGS := $(addprefix -I,$(INC_DIRS))

CPPFLAGS := $(INC_FLAGS) -ggdb

$(info $(INC_FLAGS))

$(BUILD_DIR)/$(TARGET_EXEC): $(OBJS) $(SHADER_OBJS)
	gcc $(OBJS) -o $@ $(LDFLAGS)

# Build step for C source
$(BUILD_DIR)/%.c.o: %.c
	clang --analyze -Xanalyzer -analyzer-checker=core $< $(INC_FLAGS)
	mkdir -p $(dir $@)
	gcc $(CPPFLAGS) $(CFLAGS) -c $< -o $@

# Build step for C++ source
$(BUILD_DIR)/%.cpp.o: %.cpp
	mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(SHADER_BUILD_DIR)/%.glsl.spv: ./src/shaders/%.glsl
	mkdir -p $(dir $@)
	glslc $< -o $@

.PHONY: clean run
run: $(BUILD_DIR)/$(TARGET_EXEC)
	./$(BUILD_DIR)/$(TARGET_EXEC)
clean:
	rm -r $(BUILD_DIR)
