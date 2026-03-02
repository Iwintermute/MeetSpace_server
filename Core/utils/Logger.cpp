#include "Logger.h"
#include <iostream>
#include <chrono>
#include <iomanip>

using namespace utils;

Logger::Logger(std::string name_) : loggerName(name_){};

void Logger::printTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::tm tm;
    localtime_s(&tm, &time);

    std::cout << "[";
    std::cout << std::put_time(&tm, "%H:%M:%S");
    std::cout << "." << std::setfill('0') << std::setw(3) << ms.count();
    std::cout << "] ";
}
void Logger::printName() {
    std::cout << "[";
    std::cout << loggerName;
    std::cout << "] ";
}

void Logger::Log(Level level, const std::string& msg) {
    Logger::printTimestamp();
    // std::lock_guard<std::mutex> lg(loggerMutex);
    switch (level) {
        case Level::Info:    std::cout << "[INFO] "; break;
        case Level::Warning: std::cout << "[WARN] "; break;
        case Level::Error:   std::cerr << "[ERROR] "; break;
        case Level::Debug:   std::cerr << "[DEBUG] "; break;
    }
    std::cout << msg << std::endl;
}
