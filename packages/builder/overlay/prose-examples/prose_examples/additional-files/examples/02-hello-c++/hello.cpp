/*
 * C++ with the standard library: strings, a vector, an algorithm and a
 * range-based for loop. Build it with "make", or by hand:
 *
 *     clang++ -O2 -Wall -std=c++17 -o hello hello.cpp
 *
 * The C++ library is the one the system already carries (libstdc++), which
 * is why no extra library has to be named on the command line.
 */

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

int
main()
{
	std::vector<std::string> words = { "Prose", "compiles", "its", "own", "programs" };

	std::sort(words.begin(), words.end());

	std::cout << "sorted:";
	for (const std::string& word : words)
		std::cout << " " << word;
	std::cout << std::endl;

	std::string joined;
	for (const std::string& word : words)
		joined += word.substr(0, 1);

	std::cout << "first letters: " << joined << std::endl;
	return 0;
}
