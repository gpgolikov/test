#pragma once

#include <vector>
#include <deque>
#include <map>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <tuple>
#include <atomic>

namespace griha::test {

/// @brief Interface of consumers of queue values.
/// @tparam Value - Type of values.
/// @tparam Key - Type of key that identifies queue.
template <typename Value, typename Key = unsigned>
struct IConsumer
{
    /// @brief Virual destructor.
    virtual ~IConsumer() {}

    /// @brief Function is called to pass value from queue to consumer.
    /// @param key - Queue key.
    /// @param value - Queue value.
    virtual void Consume(const Key& key, const Value& value) = 0;
};

/// @brief Pointer to store @c IConsumer objects.
/// @tparam Value - Type of values.
/// @tparam Key - Type of key that identifies queue.
template <typename Value, typename Key = unsigned>
using IConsumerPtr = std::shared_ptr<IConsumer<Value, Key>>;

/// @brief Multi queues processor that allows to manage with several queues which
/// associated with single @c IConsumer object.
/// All @c IConsumer::Consume functions are called in single thread.
/// @tparam Value - Type of values.
/// @tparam Key - Type of key that identifies queue.
/// @tparam MaxCapacity - Maximuum size of each queue.
template<typename Value, typename Key = unsigned, size_t MaxCapacity = 1000u>
class MultiQueueProcessor
{
    /// @brief Represents single queue.
    /// Contains consumer that subscribed to receive values from the queue.
    struct Queue {
        IConsumerPtr<Value, Key> consumer;    ///< Appropriate queue consumer.
        std::deque<Value> values;             ///< Actually queue.

        Queue() {}

        explicit Queue(IConsumerPtr<Value, Key> cons)
            : consumer(std::move(cons))
        {}

        Queue(IConsumerPtr<Value, Key> cons, std::deque<Value> vals)
            : consumer(std::move(cons))
            , values(std::move(vals))
        {}
    };

    /// @brief Represents thread safe single queue.
    struct QueueSafe : Queue {
        std::mutex guard;                   ///< Queue guard is used to prevent data racing in case when
                                            ///   queue is accessed concurrently.

        explicit QueueSafe(IConsumerPtr<Value, Key> cons)
            : Queue(std::move(cons))
        {}
    };

    /// @brief Queues container with associations by @c Key.
    using Queues = std::map<Key, QueueSafe>;
    // todo - optimization point - when queues number will be significant increasing then it is better to use container
    //  like a flat_map but could work with non-copyable and non-moveable structures. Or store QueueSafe in heap and
	//  use pointers.

public:
    /// @brief Default constructor
    MultiQueueProcessor() = default;

    /// @name Explicitly deleted special function
    /// @{
    MultiQueueProcessor(const MultiQueueProcessor&) = delete;
    MultiQueueProcessor& operator=(const MultiQueueProcessor&) = delete;
    MultiQueueProcessor(MultiQueueProcessor&&) = delete;
    MultiQueueProcessor& operator=(MultiQueueProcessor&&) = delete;
    /// @}

    /// @brief Desctructor
    /// Interrupts execution till consumer thread execution is finished.
    ~MultiQueueProcessor()
    {
        Stop();
        if (th_.joinable())
        {
            th_.join();
        }
    }

    /// @brief Starts consumer thread execution.
    void Start()
    {
        if (running_)
        {
            return;
        }

        running_ = true;
        th_ = std::thread { [this] { process(); }};
    }

    /// @brief Stops consumer thread execution.
    void Stop()
    {
        running_ = false;
    }

    /// @brief Adds association of consumer with queue specified by @c key
    /// @param key - Queue key.
    /// @param consumer - Queue consumer.
    void Subscribe(const Key& key, const IConsumerPtr<Value, Key>& consumer)
    {
        std::lock_guard lock { guard_ };
        const auto it = queues_.try_emplace(key, consumer);
        if (!it.second && it.first->second.consumer == nullptr)
        {
            it.first->second.consumer = consumer;
        }
    }

    /// @brief Removes association queue specified by @c key with appropriate consumer.
    /// @note Queue will be removed as well.
    /// @param key - Queue key.
    void Unsubscribe(const Key& key)
    {
        std::lock_guard lock { guard_ };
        queues_.erase(key);
    }

    /// @brief Creates new queue with specified @c key without any consumer.
    /// It is possible when a producer writes into queue earlier then consumer subscribed.
    /// It this case producer should call @c AllocateQueue function to guarantee that
    /// the values will be stored in queue properly.
    /// @param key - Queue key.
    void AllocateQueue(const Key& key)
    {
        std::lock_guard lock { guard_ };
        queues_.try_emplace(key, nullptr);
    }

    /// @brief Pushes value into queue specified by @c key.
    /// @note @c Subscribe or @c AllocateQueue should be called before calling of this function,
    /// otherwise function returns @c false and value is lost. If this function is called after calling of
    /// @c Unsubscribe then @c false will be returned too.
    /// @tparam Args - Types of value constructor arguments to create value.
    /// @param key - Queue key.
    /// @param args - Value constructor arguments.
    /// @return Returns @c true if value is successfully pushed into queue, otherwise @c false.
    template <typename... Args>
    std::enable_if_t
    <
        std::is_constructible_v<Value, Args...>,
        bool
    >
    Enqueue(const Key& key, Args&&... args)
    {
        auto notify = false;
        {
            std::shared_lock lock { guard_ };
            const auto it = queues_.find(key);
            if (it == queues_.end())
            {
                return false;
            }

            notify = it->second.consumer != nullptr;

            std::lock_guard lock_queue { it->second.guard };
            if (it->second.values.size() >= MaxCapacity)
            {
                return false;
            }
            it->second.values.emplace_back(std::forward<Args>(args)...);
        }
        if (notify)
        {
            cv_.notify_one();
        }

        return true;
    }

private:

    /// @brief Provides main functionality to manage multiple queues in single thread.
    /// User code (@c IConsumer::Consume function) is called out of any lock to prevent
    /// interlocking.
    /// This function is executed in own thread and waits for notification about new value is added
    /// in any queue.
    void process()
    {
        std::vector<std::pair<Key, Queue>> queues;

        while (running_) // check if processing has been stopped
        {
            std::shared_lock lock { guard_ };

            queues.reserve(queues_.size()); // reserve maximum number of available queues

            // Read queues under lock (read-only lock for queues guard and unique lock for each queue guard)
            auto queues_empty = true;
            for (auto& queue : queues_)
            {
                if (queue.second.consumer == nullptr)
                {
                    continue;
                }

                std::lock_guard lock_queue { queue.second.guard };
                if (!queue.second.values.empty())
                {
                    queues.emplace_back(queue.first, Queue { queue.second.consumer, std::move(queue.second.values) });
                    queues_empty = false;
                }
            }

            if (queues_empty)
            {
                // There are no values in queues - wait for notification
                cv_.wait(lock);
            }
            else
            {
                lock.unlock();

                // Pass values to consumers from non-empty queues out of any lock
                for (const auto& queue : queues)
                {
                    for (const auto& value : queue.second.values)
                    {
                        try
                        {
                            queue.second.consumer->Consume(queue.first, value);
                        }
                        catch (std::exception&)
                        {
                            // todo - process exception somehow, for example log it
                        }
                    }
                }
                queues.clear();
            }
        }
    }

private:
    Queues queues_;                        ///< Queues with consumers.
    std::shared_mutex guard_;            ///< @c queues_ read-write guard
    std::condition_variable_any cv_;    ///< Conditional variable to notify working thread
                                        ///     about new value in any queue.
    std::atomic_bool running_;            ///< Flag indicates whether execution is stopped or run.
    std::thread th_;                    ///< Working thread.
};

} // namespace griha::test