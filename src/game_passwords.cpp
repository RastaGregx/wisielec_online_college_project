#include <fstream>
#include <string>
#include <vector>
#include <cstdlib>
#include <ctime>

std::string game_load_random_word(const std::string &filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        printf("Cannot open file: %s\n", filename.c_str());
        return "DEFAULT";
    }

    std::vector<std::string> words;
    std::string line;

    while (std::getline(file, line)) {
        if (!line.empty())
            words.push_back(line);
    }

    if (words.empty())
        return "DEFAULT";

    // Losowy wybór
    std::srand(std::time(nullptr));
    int idx = std::rand() % words.size();
    return words[idx];
}
