#pragma once
#include <string>
#include "../../networkEDS/include/nlohmann/json.hpp"

namespace Sys {

    class cJsonSerializer {
    public:
        template<typename T>
        static std::string fnSerialize(const T& obj) {
            return nlohmann::json(obj).dump();
        }

        template<typename T>
        static T fnDeserialize(const std::string& s) {
            return nlohmann::json::parse(s).get<T>();
        }
    };

} // namespace Sys
