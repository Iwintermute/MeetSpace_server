#include <boost/signals2.hpp>
#include <unordered_map>
#include <typeindex>
#include <memory>
#include <mutex>

class EventBus {
    EventBus() = default;

public:

    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;
    EventBus(EventBus&&) = delete;
    EventBus& operator=(EventBus&&) = delete;

    static std::shared_ptr<EventBus> instance() {
        static std::shared_ptr<EventBus> inst(new EventBus);
        return inst;
    }

    // Подписка на событие типа Event
    template<typename Event>
    boost::signals2::connection subscribe(typename boost::signals2::signal<void(const Event&)>::slot_type slot) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto& signal = getSignal<Event>();
        return signal->connect(slot);
    }

    // Публикация события типа Event
    template<typename Event>
    void publish(const Event& event) {
        std::shared_ptr<boost::signals2::signal<void(const Event&)>> signal_copy;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_signals.find(typeid(Event));
            if (it != m_signals.end()) {
                auto holder = std::dynamic_pointer_cast<SignalHolder<Event>>(it->second);
                if (holder) {
                    signal_copy = holder->signal;
                }
            }
        }
        if (signal_copy) {
            (*signal_copy)(event); // вызов всех подписчиков вне блокировки
        }
    }

private:
    // Базовый класс для хранения сигнала любого типа
    struct SignalHolderBase {
        virtual ~SignalHolderBase() = default;
    };

    // Шаблонный класс‑обёртка для конкретного сигнала
    template<typename Event>
    struct SignalHolder : SignalHolderBase {
        std::shared_ptr<boost::signals2::signal<void(const Event&)>> signal =
            std::make_shared<boost::signals2::signal<void(const Event&)>>();
    };

    // Получение (или создание) сигнала для типа Event
    template<typename Event>
    std::shared_ptr<boost::signals2::signal<void(const Event&)>> getSignal() {
        auto it = m_signals.find(typeid(Event));
        if (it != m_signals.end()) {
            return std::dynamic_pointer_cast<SignalHolder<Event>>(it->second)->signal;
        }
        else {
            auto holder = std::make_shared<SignalHolder<Event>>();
            m_signals[typeid(Event)] = holder;
            return holder->signal;
        }
    }

    std::unordered_map<std::type_index, std::shared_ptr<SignalHolderBase>> m_signals;
    std::mutex m_mutex;
};