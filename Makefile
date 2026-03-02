CXX = g++
CXXFLAGS = -std=c++17 -O3 -Wall -Wextra -Werror -luring -march=native
LDFLAGS = 

# 源文件和目标文件
SRCS = benchmark_main.cpp utils.cpp io_backends.cpp
OBJS = $(SRCS:.cpp=.o)
TARGET = benchmark_main

# 默认构建目标
all: $(TARGET)

# 链接生成可执行文件
$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

# 编译 C++ 源文件为对象文件
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# 清理编译产物和测试文件
clean:
	rm -f $(OBJS) $(TARGET) test.bin

.PHONY: all clean