#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace ghostcut
{

/**
    Hands immutable objects from the message thread to the audio thread without
    locks, allocation or deallocation on the audio thread.

    Message thread:  publish()  — offers a new object.
                     collectGarbage() — frees objects the audio thread has let go.
    Audio thread:    acquire()  — call once at the start of each block; returns the
                                  object to use for this block (may be nullptr).

    Ownership: the message thread keeps a shared_ptr to every object that the audio
    thread might still be looking at. When the audio thread swaps to a newer object
    it pushes the old raw pointer into a lock-free retire FIFO; the message thread
    then drops its shared_ptr. Nothing is ever freed on the audio thread.
*/
template <typename T>
class AudioThreadExchange
{
public:
    AudioThreadExchange() = default;
    ~AudioThreadExchange() = default; // shared_ptrs in `held` release everything

    // ---- message thread ---------------------------------------------------
    void publish (std::shared_ptr<const T> object)
    {
        collectGarbage();

        const T* raw = object.get();
        if (raw != nullptr)
            held.push_back (std::move (object));

        // If the audio thread never picked up the previous pending object, we can
        // release it right here: the exchange proves it was never seen.
        if (const T* stale = pending.exchange (raw, std::memory_order_acq_rel))
            release (stale);

        if (raw == nullptr)
            clearRequested.store (true, std::memory_order_release);
    }

    void collectGarbage()
    {
        int start1, size1, start2, size2;
        retireFifo.prepareToRead (retireFifo.getNumReady(), start1, size1, start2, size2);
        for (int i = 0; i < size1; ++i) release (retired[(size_t) (start1 + i)]);
        for (int i = 0; i < size2; ++i) release (retired[(size_t) (start2 + i)]);
        retireFifo.finishedRead (size1 + size2);
    }

    /** Call only when the audio thread is guaranteed not to be running
        (e.g. in the processor destructor or releaseResources). */
    void resetWhileAudioStopped()
    {
        pending.store (nullptr);
        current = nullptr;
        held.clear();
        retireFifo.reset();
    }

    // ---- audio thread -----------------------------------------------------
    /** Returns the object to use for this block and whether it changed. */
    const T* acquire (bool& changed) noexcept
    {
        changed = false;
        const T* next = pending.exchange (nullptr, std::memory_order_acq_rel);

        if (next == nullptr && clearRequested.exchange (false, std::memory_order_acq_rel))
        {
            if (current != nullptr) { retireInternal (current); current = nullptr; changed = true; }
            return current;
        }

        if (next != nullptr)
        {
            clearRequested.store (false, std::memory_order_release);
            if (current != nullptr) retireInternal (current);
            current = next;
            changed = true;
        }
        return current;
    }

    const T* getCurrentOnAudioThread() const noexcept { return current; }

    /** Like acquire(), but does NOT retire the previous object: it is handed back in
        `previous` and the audio thread must call retire() once nothing uses it. */
    const T* acquireKeepPrevious (bool& changed, const T*& previous) noexcept
    {
        changed = false;
        previous = nullptr;
        const T* next = pending.exchange (nullptr, std::memory_order_acq_rel);
        if (next == nullptr && clearRequested.exchange (false, std::memory_order_acq_rel))
        {
            previous = current; current = nullptr; changed = previous != nullptr;
            return current;
        }
        if (next != nullptr)
        {
            clearRequested.store (false, std::memory_order_release);
            previous = current;
            current = next;
            changed = true;
        }
        return current;
    }

    /** Audio thread: hands an object back for freeing on the message thread. */
    void retire (const T* p) noexcept
    {
        if (p == nullptr) return;
        int start1, size1, start2, size2;
        retireFifo.prepareToWrite (1, start1, size1, start2, size2);
        if (size1 > 0)       retired[(size_t) start1] = p;
        else if (size2 > 0)  retired[(size_t) start2] = p;
        retireFifo.finishedWrite (size1 + size2 > 0 ? 1 : 0);
    }

private:
    void retireInternal (const T* p) noexcept { retire (p); }

    void release (const T* p)
    {
        for (auto it = held.begin(); it != held.end(); ++it)
            if (it->get() == p) { held.erase (it); return; }
    }

    static constexpr int retireCapacity = 64;

    std::atomic<const T*> pending { nullptr };
    std::atomic<bool> clearRequested { false };
    const T* current = nullptr;                        // audio thread only

    juce::AbstractFifo retireFifo { retireCapacity };
    std::array<const T*, (size_t) retireCapacity> retired {};

    std::vector<std::shared_ptr<const T>> held;        // message thread only

    JUCE_DECLARE_NON_COPYABLE (AudioThreadExchange)
};

/** Fixed-size single-producer / single-consumer queue of trivially copyable items. */
template <typename T, int Capacity>
class SpscQueue
{
public:
    bool push (const T& item) noexcept
    {
        int s1, n1, s2, n2;
        fifo.prepareToWrite (1, s1, n1, s2, n2);
        if (n1 > 0)      items[(size_t) s1] = item;
        else if (n2 > 0) items[(size_t) s2] = item;
        else return false;
        fifo.finishedWrite (1);
        return true;
    }

    bool pop (T& item) noexcept
    {
        int s1, n1, s2, n2;
        fifo.prepareToRead (1, s1, n1, s2, n2);
        if (n1 > 0)      item = items[(size_t) s1];
        else if (n2 > 0) item = items[(size_t) s2];
        else return false;
        fifo.finishedRead (1);
        return true;
    }

private:
    juce::AbstractFifo fifo { Capacity };
    std::array<T, (size_t) Capacity> items {};
};

} // namespace ghostcut
