#pragma once

#include <functional>
#include <memory>

namespace panda::utils
{

template <typename F>
class Sender;
template <typename F>
class Receiver;

template <typename F>
class Signal
{
public:
    using Id = size_t;

    auto connect(F&& connection) -> Receiver<F>;
    auto disconnect(const Receiver<F>& receiver) -> void;

private:
    friend class Sender<F>;
    Signal() = default;

    std::unordered_map<Id, F> connections;
    std::mutex connectionsMutex;
    Id currentId = 0;
};

template <typename F>
class Receiver
{
public:
    Receiver(const Receiver&) = delete;
    Receiver(Receiver&&) noexcept = default;
    auto operator=(const Receiver&) -> Receiver& = delete;
    auto operator=(Receiver&&) noexcept -> Receiver& = default;

    ~Receiver() noexcept
    {
        signal.get().disconnect(*this);
    }

    const Signal<F>::Id id;

private:
    friend class Signal<F>;

    Receiver(Signal<F>& signalRef, Signal<F>::Id receiverId)
        : id {receiverId},
          signal {signalRef}
    {
    }

    std::reference_wrapper<Signal<F>> signal;
};

template <typename F>
class Sender
{
public:
    Sender() = default;
    Sender(const Sender&) = delete;
    Sender(Sender&&) noexcept = default;
    auto operator=(const Sender&) -> Sender& = delete;
    auto operator=(Sender&&) noexcept -> Sender& = default;
    ~Sender() noexcept = default;

    template <typename... Args>
    auto send(Args&&... args) const -> void
    {
        for (auto& [key, value] : signal.connections)
        {
            value(std::forward<Args>(args)...);
        }
    }

    [[nodiscard]] auto getSignal() const noexcept -> Signal<F>&
    {
        return signal;
    }

private:
    mutable Signal<F> signal;
};

template <typename F>
auto Signal<F>::connect(F&& connection) -> Receiver<F>
{
    std::lock_guard<std::mutex> lock {connectionsMutex};
    connections[currentId] = connection;
    return {*this, currentId++};
}

template <typename F>
auto Signal<F>::disconnect(const Receiver<F>& receiver) -> void
{
    connections.erase(receiver.id);
}

}
