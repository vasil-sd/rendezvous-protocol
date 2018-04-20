#ifndef __RENDEZVOUS__
#define __RENDEZVOUS__

#include "rendezvous_modcounter.h"
#include "rendezvous_list.h"

namespace rendezvous
{

/*
 * Implementation of dynamic variant of Leslie Lamport's algorithm from "Implementing Dataflow With Threads", Distributed Computing 21, 3 (2008), 163-181.
 */

struct DefaultAtomicConfig
{
    static void BusyWaitHandler(int&)
    {
    }

    static bool CAS(void** ptr,
                    void*  oldValue,
                    void*  newValue)
    {
        return __sync_bool_compare_and_swap(ptr, oldValue, newValue);
    }

};

struct DefaultRendezvousType {};

template<typename Data,
         typename Type = DefaultRendezvousType,
         typename ModCounter = ModCounter<>>
class Rendezvous
{
    struct WaitAndActiveList {};
    struct RemoveList {};

    enum class RemoveAction{
        GO,
        WAIT,
        SYNC
    };

public:
    Rendezvous() {}
    Rendezvous(const Rendezvous&)  = delete;
    Rendezvous(const Rendezvous&&) = delete;

    class Place : public list::Element<Place, WaitAndActiveList>,
                  public list::Element<Place, RemoveList>
    {
        Rendezvous&     rendezvous; // reference to common rendezvous object
        bool volatile   wait;       // flag for waiting activities
        RemoveAction volatile   remove;     // flag for activities going to be removed
        ModCounter      counter;    // sync. counter

        void(&BusyWaitHandler)(int&);     // handler for busy wait loops
                                          // (can, for instance, yield control to 
                                          //  another thread after N attempts)

        bool(&CAS)(void**, void*, void*); // user specified Compare-And-Swap primitive
                                          // sometime it is desirable to disable interrups
                                          // around CAS, sometimes not, even in case
                                          // of single system (think of syncronization
                                          // between threads and interrupt handlers, for
                                          // instance), so the desired beh. may be 
                                          // customized by user

        Data* data; // user data to share between activities on rendezvous point

        // here is original Leslie Lamport's sync algorithm
        int Sync()
        {
            int passed = 0;
            int loopCount = 0;
            // loop until all counters of active threads become
            // greater or equal to this one
            while (rendezvous.active.Search([this, &passed](auto& a)
                                            {
                                                ++passed;
                                                return counter > a.counter;
                                            }))
            {
                passed = 0;
                BusyWaitHandler(loopCount);
            }
            return passed;
        }

        // master thread is responsible for maintainance operations:
        // - adding from waiting list to active one
        // - removing from active list threads from removing list
        // - handle wait/remove flags of threads in consistent way
        bool IsMaster()
        {
            return rendezvous.active.GetHead() == this;
        }

        void ProcessWaiting()
        {
            if(rendezvous.waiting)
            {
                typename ModCounter::type c = counter; // counter is volatile, so to do not
                                                       // load it each time, just load it
                                                       // to temp var

                // Atomically gat waiting list to w, waiting list becomes empty
                auto w = rendezvous.waiting.AcquireAtomic(BusyWaitHandler, CAS);

                w.Iterate([&c](auto& p)
                          {
                              p.counter = c;
                          });

                // append waiting to active
                rendezvous.active.Append(w);

                // activate waiting
                w.Iterate([](auto& p)
                          {
                              p.wait = false;
                          });
            }
        }

        // This function is needed only very first time, where no active
        // activities are present.
        bool TryToBecomeMaster()
        {
            if(rendezvous.waiting.GetHead() == this &&
               !rendezvous.active)
            {
                rendezvous.active.SetAtomic(BusyWaitHandler, CAS, rendezvous.waiting);
                rendezvous.active.Iterate([](auto& p)
                                          {
                                              p.wait = false;
                                          });
                return true;
            }
            return false;
        }

    public:
        // object is non-movable, non-copyable
        // TODO: may be make it movable?
        Place() = delete;
        Place(const Place&)  = delete;
        Place(const Place&&) = delete;
        Place& operator=(const Place&)  = delete;
        Place& operator=(const Place&&) = delete;

        template<typename Cfg>
        Place(Rendezvous& r,
              const Cfg & cfg) : rendezvous(r), wait(true), remove(RemoveAction::GO), BusyWaitHandler(
                Cfg::BusyWaitHandler), CAS(Cfg::CAS), data(nullptr)
        {
            counter = 0;
            rendezvous.waiting.AddAtomic(BusyWaitHandler, CAS, *this);
        }

        Place(Rendezvous& r) : Place(r, DefaultAtomicConfig())
        {
        }

        ~Place()
        {
            bool master;

            // There may be optimization, when we just delete ourselfs from
            // waiting list. But it should be carefully modelchecked, because
            // I'm not sure, that it may be programmed reliable
            int  loopCount = 0;
            while (wait && !TryToBecomeMaster())
            {
                BusyWaitHandler(loopCount);
            }

            if ((master = IsMaster()))
            {
                ProcessWaiting();
            }

            remove = RemoveAction::WAIT;
            rendezvous.removing.AddAtomic(BusyWaitHandler, CAS, *this);

            ++counter;
            Sync();

            if (master)
            {
                auto r = rendezvous.removing.AcquireAtomic(BusyWaitHandler, CAS);
                rendezvous.active.Remove(r);
                r.Iterate([](auto& p)
                          {
                              p.remove = RemoveAction::SYNC;
                          });
                remove = RemoveAction::WAIT;
                loopCount = 0;
                while (r.Search([](auto &p){return p.remove != RemoveAction::WAIT;}))
                {
                    BusyWaitHandler(loopCount);
                }
                r.Iterate([](auto& p)
                          {
                              p.remove = RemoveAction::GO;
                          });
            }
            else
            {
                loopCount = 0;
                while (remove == RemoveAction::WAIT)
                {
                    BusyWaitHandler(loopCount);
                }
                if(remove == RemoveAction::SYNC)
                {
                    remove = RemoveAction::WAIT;
                    loopCount = 0;
                    while (remove != RemoveAction::GO)
                    {
                        BusyWaitHandler(loopCount);
                    }
                }
            }
        }

        // TODO: Do we need a variant of Attend, parameterized by CAS and BusyWaitHandler?

        template<typename InitFunc, typename FoldFunc, typename ComputeFunc>
        auto Attend(InitFunc Init, // gets amount of threads on rendezvous point, should allocate some accumulator structure and
                                   // return reference to it

                    FoldFunc Fold, // receives reference to accumulator and data structures of other participants in sequence

                    ComputeFunc Compute, // receives ref to accum and returns ref to result

                    Data& d) -> decltype(Compute(Init(1)))&
        {
            data = &d;

            bool master;

            int  loopCount = 0;
            while (wait && !TryToBecomeMaster())
            {
                BusyWaitHandler(loopCount);
            }

            if ((master = IsMaster()))
            {
                ProcessWaiting();
            }

            ++counter;
            Sync();

            decltype(rendezvous.removing) to_remove;
            if (master && rendezvous.removing)
            {
                to_remove = rendezvous.removing.AcquireAtomic(BusyWaitHandler, CAS);
                rendezvous.active.Remove(to_remove);
            }

            ++counter;
            int passed = Sync();

            to_remove.Iterate([](auto& p)
                              {
                                  p.remove = RemoveAction::GO;
                              });

            // TODO: think about std::ref analog here
            auto& accumulator = Init(passed);

            rendezvous.active.Iterate([&accumulator, &Fold](Place& elt)
                                      {
                                          Fold(accumulator, const_cast<const Data&>(*elt.data));
                                      });

            auto& result = Compute(accumulator);

            ++counter;
            Sync();

            return result;
        }

    };
private:
    list::List<Place, WaitAndActiveList> waiting;
    list::List<Place, RemoveList> removing;
    list::List<Place, WaitAndActiveList> active;
};

}                        // end of namespace rendezvous

#endif // __RENDEZVOUS__
