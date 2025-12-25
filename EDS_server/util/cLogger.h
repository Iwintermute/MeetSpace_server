#pragma once
#include <iostream>
#include <mutex>
#include <string>

namespace Sys {

    class cLogger {
    public:
        enum class Level { Info, Warning, Error };

        static void fnLog(Level eLevel, const std::string& sMsg);

    private:
        static std::mutex m_mtx; 
    };

} // namespace Sys
