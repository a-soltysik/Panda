#pragma once

#include <concepts>
#include <functional>
#include <memory>

namespace panda::utils
{

template <typename... Args>
class Sender;
template <typename... Args>
class Receiver;

template <typename... Args>
class Signal
{
public:
    using SenderT = Sender<Args...>;
    using ReceiverT = Receiver<Args...>;
    using ChannelT = std::function<void(Args...)>;

    auto registerSender() const -> SenderT;
    auto connect(ChannelT&& connection) -> ReceiverT;
    auto disconnect(const ReceiverT& receiver) -> void;

private:
    friend class Sender<Args...>;

    template <std::convertible_to<Args>... Params>
    auto emit(Params&&... params) const -> void
    {
        std::lock_guard<std::mutex> lock {connectionsMutex};
        for ([[maybe_unused]] auto& [key, value] : connections)
        {
            value(std::forward<Params>(params)...);
        }
    }

    std::vector<std::pair<const ReceiverT*, ChannelT>> connections;
    mutable std::mutex connectionsMutex;
};

template <typename... Args>
class Receiver
{
public:
    Receiver() = default;
    Receiver(const Receiver&) = delete;
    Receiver(Receiver&&) noexcept = default;
    auto operator=(const Receiver&) -> Receiver& = delete;
    auto operator=(Receiver&&) noexcept -> Receiver& = default;

    ~Receiver() noexcept
    {
        if (!signal) [[unlikely]]
        {
            return;
        }
        signal->disconnect(*this);
    }

private:
    friend class Signal<Args...>;

    explicit Receiver(Signal<Args...>& signalRef)
        : signal {&signalRef}
    {
    }

    Signal<Args...>* signal;
};

template <typename... Args>
class Sender
{
public:
    Sender() = default;

    explicit Sender(const Signal<Args...>& signalRef)
        : signal {&signalRef}
    {
    }

    Sender(const Sender&) = delete;
    Sender(Sender&&) noexcept = default;
    auto operator=(const Sender&) -> Sender& = delete;
    auto operator=(Sender&&) noexcept -> Sender& = default;
    ~Sender() = default;

    template <std::convertible_to<Args>... Params>
    auto operator()(Params&&... params) const -> void
    {
        if (signal) [[likely]]
        {
            signal->emit(std::forward<Params>(params)...);
        }
    }

private:
    const Signal<Args...>* signal;
};

template <typename... Args>
auto Signal<Args...>::registerSender() const -> SenderT
{
    return SenderT {*this};
}

template <typename... Args>
auto Signal<Args...>::connect(ChannelT&& connection) -> ReceiverT
{
    std::lock_guard<std::mutex> lock {connectionsMutex};
    auto receiver = ReceiverT {*this};
    connections.emplace_back(&receiver, std::move(connection));
    return receiver;
}

template <typename... Args>
auto Signal<Args...>::disconnect(const ReceiverT& receiver) -> void
{
    std::lock_guard<std::mutex> lock {connectionsMutex};
    std::erase_if(connections, [&receiver](const auto& elem) noexcept {
        return elem.first == &receiver;
    });
}

}
