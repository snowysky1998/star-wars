
all:
	g++ src/main.cpp lodepng/lodepng.cpp -std=c++20 -I./src -I./lodepng -I./glm -Wconversion -o main

clean:
	rm main
	rm output.png
