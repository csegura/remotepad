CXX := g++
CXXFLAGS := -std=c++20 -O2 -Wall -Wextra
LDFLAGS :=

# Embed client files into the binary (set to 0 to serve from filesystem)
EMBED_CLIENT ?= 1

# System libraries
LIBS := -lX11 -lXfixes -lXext -ljpeg -lz -lpthread

# Directories
SRC_DIR := src
BUILD_DIR := build
DEPS_DIR := deps
CLIENT_DIR := client

# Source files
SOURCES := $(SRC_DIR)/main.cpp \
           $(SRC_DIR)/app_config.cpp \
           $(SRC_DIR)/draw_color.cpp \
           $(SRC_DIR)/remote_pad.cpp \
           $(SRC_DIR)/x_tools.cpp \
           $(SRC_DIR)/x_screen.cpp \
           $(SRC_DIR)/web_server.cpp \
           $(SRC_DIR)/platform/linux/linux_overlay.cpp \
           $(SRC_DIR)/platform/linux/linux_screen_capture.cpp

OBJECTS := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SOURCES))
TARGET := $(BUILD_DIR)/remotepad

# Dependencies (header-only / source)
USOCKETS_DIR := $(DEPS_DIR)/uSockets
UWEBSOCKETS_DIR := $(DEPS_DIR)/uWebSockets
NLOHMANN_DIR := $(DEPS_DIR)/json

USOCKETS_SRC := $(wildcard $(USOCKETS_DIR)/src/*.c) $(USOCKETS_DIR)/src/eventing/epoll_kqueue.c
USOCKETS_OBJ := $(BUILD_DIR)/usockets_bsd.o \
                $(BUILD_DIR)/usockets_context.o \
                $(BUILD_DIR)/usockets_loop.o \
                $(BUILD_DIR)/usockets_quic.o \
                $(BUILD_DIR)/usockets_socket.o \
                $(BUILD_DIR)/usockets_udp.o \
                $(BUILD_DIR)/usockets_epoll_kqueue.o

# Include paths
INCLUDES := -I$(SRC_DIR) \
            -I$(UWEBSOCKETS_DIR)/src \
            -I$(USOCKETS_DIR)/src \
            -I$(NLOHMANN_DIR)/include

DEFINES := -DLIBUS_NO_SSL
ifeq ($(EMBED_CLIENT),1)
    DEFINES += -DEMBED_CLIENT
    INCLUDES += -I$(BUILD_DIR)
    EMBED_HEADER := $(BUILD_DIR)/embedded_client_data.h
endif

.PHONY: all clean deps

all: deps $(TARGET)

# Fetch dependencies
deps: $(USOCKETS_DIR) $(UWEBSOCKETS_DIR) $(NLOHMANN_DIR)

$(USOCKETS_DIR):
	@mkdir -p $(DEPS_DIR)
	@echo "Fetching uSockets..."
	git clone --depth 1 --branch v0.8.8 https://github.com/uNetworking/uSockets.git $(USOCKETS_DIR)

$(UWEBSOCKETS_DIR):
	@mkdir -p $(DEPS_DIR)
	@echo "Fetching uWebSockets..."
	git clone --depth 1 --branch v20.70.0 https://github.com/uNetworking/uWebSockets.git $(UWEBSOCKETS_DIR)

$(NLOHMANN_DIR):
	@mkdir -p $(DEPS_DIR)
	@echo "Fetching nlohmann/json..."
	git clone --depth 1 --branch v3.11.3 https://github.com/nlohmann/json.git $(NLOHMANN_DIR)

# Build uSockets objects
$(BUILD_DIR)/usockets_%.o: $(USOCKETS_DIR)/src/%.c | $(BUILD_DIR)
	$(CC) -c -O2 $(DEFINES) -I$(USOCKETS_DIR)/src -o $@ $<

$(BUILD_DIR)/usockets_epoll_kqueue.o: $(USOCKETS_DIR)/src/eventing/epoll_kqueue.c | $(BUILD_DIR)
	$(CC) -c -O2 $(DEFINES) -I$(USOCKETS_DIR)/src -o $@ $<

# Generate embedded client header
$(EMBED_HEADER): $(wildcard $(CLIENT_DIR)/*) tools/embed_client.sh | $(BUILD_DIR)
	@echo "Embedding client files..."
	@bash tools/embed_client.sh $(CLIENT_DIR) $@

# Build C++ objects (depend on embed header when enabled)
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp $(EMBED_HEADER) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(DEFINES) $(INCLUDES) -c -o $@ $<

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR) $(BUILD_DIR)/platform/linux

# Link
$(TARGET): $(OBJECTS) $(USOCKETS_OBJ)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LIBS)
	@echo "Build complete: $(TARGET)"

clean:
	rm -rf $(BUILD_DIR)

distclean: clean
	rm -rf $(DEPS_DIR)
