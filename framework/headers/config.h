#pragma once
#include "includes.h"
#include <map>
#include <variant>
#include <array>

struct checkbox_data
{
    std::string name;
    bool enabled;
};

template<typename T>
struct slider_data
{
    std::string name;
    T callback;
    T min;
    T max;
    std::string format;
};

template<typename T>
struct dropdown_data
{
    std::string name;
    T callback;
    std::vector<std::string> items;
};

struct colorpicker_data
{
    std::string name;
    std::array<float, 4> col;
};

enum config_type
{
    checkbox_type,
    slider_int_type,
    slider_float_type,
    dropdown_type,
    multi_dropdown_type,
    colorpicker_type,
};

using string_t = std::vector<std::string>;
using bool_t = std::vector<bool>;
using col_t = std::array<float, 4>;

using slider_int_t = slider_data<int>;
using slider_float_t = slider_data<float>;
using dropdown_t = dropdown_data<int>;
using multi_dropdown_t = dropdown_data<std::vector<bool>>;

using config_variant = std::variant<checkbox_data, slider_int_t, slider_float_t, dropdown_t, multi_dropdown_t, colorpicker_data>;

class c_config
{
public:

    void init_config();

    template <typename T>
    T& get(const std::string& name) { return std::get<T>(options[name]); }

    template <typename T>
    T* fill(const std::string& name)
    {
        auto& option = options[name];

        return std::get_if<T>(&option);
    }

    std::vector<std::pair<std::string, int>> order;

private:

    template <typename T, typename... Args>
    void add_option(const std::string& name, Args&&... args)
    {
        T option{ name, std::forward<Args>(args)... };
        options[name] = option;
        order.push_back({ name, get_type<T>() });
    }

    template <typename T>
    int get_type() const
    {
        if constexpr (std::is_same_v<T, checkbox_data>) return checkbox_type;
        if constexpr (std::is_same_v<T, slider_int_t>) return slider_int_type;
        if constexpr (std::is_same_v<T, slider_float_t>) return slider_float_type;
        if constexpr (std::is_same_v<T, dropdown_t>) return dropdown_type;
        if constexpr (std::is_same_v<T, multi_dropdown_t>) return multi_dropdown_type;
        if constexpr (std::is_same_v<T, colorpicker_data>) return colorpicker_type;
    }

    std::map<std::string, config_variant> options;
};

inline std::unique_ptr<c_config> cfg = std::make_unique<c_config>();