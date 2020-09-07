#pragma once

#include <memory>
#include <utility>

#include "multi_queue_processor.h"

namespace griha::test
{

/// @brief Implements @c IConsumer interface and holds functional object.
/// @tparam Value - Type of values.
/// @tparam Func - Type of functional object.
template <typename Value, typename Func>
class Consumer : public IConsumer<Value>
{
public:
    template <typename F>
    explicit Consumer(F&& func) : func_(std::forward<F>(func)) {}

private:
    /// @name @c IConsumer interface implementation.
    /// @{
    void Consume(const unsigned& key, const Value& value) override
    {
        func_(key, value);
    }
    /// @}

    Func func_; ///< Functional object that will be called on @c Consume calling.
};

/// @brief Creates @c Consumer object.
template <typename Value, typename Func>
IConsumerPtr<Value> MakeConsumer(Func&& func)
{
    return std::make_shared<Consumer<Value, Func>>(std::forward<Func>(func));
}

/// @brief Manages with several consumers that represented by functional objects.
/// @tparam Value - Type of values.
/// @tparam GroupKey - Queue key of first consumer in list.
/// @tparam Funcs - Type of functional objects.
template <typename Value, unsigned GroupKey = 1000, typename... Funcs>
class Consumers
{
    static_assert(sizeof...(Funcs) > 0, "Funcs template argument should be set");

public:
    template <typename... Fs>
    explicit Consumers(Fs&&... fs)
    {
        static_assert(sizeof...(Funcs) == sizeof...(Fs), "Number of element in Fs argument should be exactly as for Funcs");
        consumers = { MakeConsumer<Value>(std::forward<Fs>(fs))... };
    }

    /// @brief Calls @c Subscribe for all stored consumer.
    /// @tparam MaxCapacity - @c MaxCapacity parameter in @c MultiQueueProcessor specification.
    /// @param processor - @c MultiQueueProcessor object.
    template <size_t MaxCapacity>
    void SubscribeTo(MultiQueueProcessor<Value, unsigned, MaxCapacity>& processor) const
    {
        for (unsigned i = 0; i < consumers.size(); ++i)
        {
            processor.Subscribe(GroupKey + i, consumers[i]);
        }
    }

    /// @brief Calls @c Unsubscribe for all stored consumer.
    /// @tparam MaxCapacity - @c MaxCapacity parameter in @c MultiQueueProcessor specification.
    /// @param processor - @c MultiQueueProcessor object.
    template <size_t MaxCapacity>
    void UnsubscribeFrom(MultiQueueProcessor<Value, unsigned, MaxCapacity>& processor) const
    {
        for (unsigned i = 0; i < consumers.size(); ++i)
        {
            processor.Unsubscribe(GroupKey + i);
        }
    }

private:
    using ConsumerList = std::array<IConsumerPtr<Value>, sizeof...(Funcs)>;

    ConsumerList consumers; ///< Consumers associated with user functional objects.
};

/// @brief Creates @c Consumers object by passing functional objects.
/// Queue key associated with consumer is calculated by @c Consumers class and
/// starts from @c GroupKey value, next consumer associated with `GroupKey + 1` queue,
/// and etc.
/// @tparam Value - Type of values.
/// @tparam GroupKey - Queue key of first consumer in list.
/// @tparam Funcs - Type of functional objects.
/// @param funcs - Functional objects that will be managed as consumers of appropriate key.
template <typename Value, unsigned GroupKey = 1000, typename... Funcs>
auto MakeConsumers(Funcs&&... funcs)
{
    return Consumers<Value, GroupKey, Funcs...> { std::forward<Funcs>(funcs)... };
}

} // namespace griha::test