#pragma once
#include <string>

namespace utils {
    // Универсальный генератор случайных строк.
    // - alphabet: набор символов (по умолчанию: без неоднозначных, как I/O/0/1).
    // - length: длина строки (по умолчанию 8, максимум 64).
    // Для эффективности: RNG статический (thread_local для многопоточки).
    // Использование: wordGenerator() для дефолтного, или с параметрами.
    std::string WordGenerator(size_t length = 8, const char* alphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789");

}