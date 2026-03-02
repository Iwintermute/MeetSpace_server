#include "utils/WordGenerator.h"
#include <random>

std::string utils::WordGenerator(size_t length, const char* alphabet)
{
    if (length > 64) length = 64; // Ограничение по максимуму
    if (length == 0) return "";

    size_t alphaSize = std::strlen(alphabet);
    if (alphaSize == 0) return "";

    thread_local std::random_device rd; // Инициализация один раз на поток
    thread_local std::mt19937 rng(rd());

    std::uniform_int_distribution<size_t> dist(0, alphaSize - 1);

    std::string out;
    out.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        out.push_back(alphabet[dist(rng)]);
    }
    return out;
}