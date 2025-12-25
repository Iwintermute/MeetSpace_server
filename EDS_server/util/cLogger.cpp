#include "cLogger.h"
#include <iostream>

std::mutex Sys::cLogger::m_mtx;

void Sys::cLogger::fnLog(Level eLevel, const std::string& sMsg) {
    std::lock_guard<std::mutex> lg(m_mtx);
    switch (eLevel) {
    case Level::Info:    std::cout << "[INFO] "; break;
    case Level::Warning: std::cout << "[WARN] "; break;
    case Level::Error:   std::cerr << "[ERROR] "; break;
    }
    std::cout << sMsg << std::endl;
}
