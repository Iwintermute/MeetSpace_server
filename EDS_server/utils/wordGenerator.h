#pragma once
#include <random>
#include <string>

namespace utils {
    // Универсальный генератор случайных строк.
    // - alphabet: набор символов (по умолчанию: без неоднозначных, как I/O/0/1).
    // - length: длина строки (по умолчанию 8, максимум 64).
    // Для эффективности: RNG статический (thread_local для многопоточки).
    // Использование: wordGenerator() для дефолтного, или с параметрами.
    std::string wordGenerator(size_t length = 8, const char* alphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789")
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

}