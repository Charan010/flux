CXX      := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -pthread
LDLIBS   := -lblake3 -pthread

SRCS := main.cpp \
        dedup/chunker.cpp \
        dedup/hasher.cpp \
        dedup/dedup_engine.cpp \
        dedup/object_store/index.cpp \
        dedup/object_store/object_store.cpp \
        codecs/lz4/lz4.cpp \
        io/mmap_file.cpp \
        threadpool/threadpool.cpp

OBJS := $(SRCS:.cpp=.o)
TARGET := flux

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: clean
