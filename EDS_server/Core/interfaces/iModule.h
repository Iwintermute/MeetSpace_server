#pragma once

#include <string>

class iModule {
public:
    virtual ~iModule() = default;

    virtual int getId() const = 0;
    virtual std::string getName() const = 0;

    // Жизненный цикл
    virtual bool initialize() = 0;
    virtual void shutdown() = 0;

    // Статус
    virtual bool isEnabled() const = 0;
    virtual void setEnabled(bool enabled) = 0;
};