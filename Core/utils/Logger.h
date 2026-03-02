#pragma once
#include <mutex>
#include <string>


namespace utils{//TODO: Сделать очередь вывода
    class Logger {
    public:
        Logger(std::string name_);
        ~Logger() = default;
    private:
        std::string loggerName;

    public:
        enum class Level { Info, Warning, Error, Debug };

        void Log(Level eLevel, const std::string& sMsg);

        std::string getName();

    private:
        void printTimestamp();
        void printName();

        // std::mutex loggerMutex; 
    };
}

