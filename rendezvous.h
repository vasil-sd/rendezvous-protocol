#ifndef _RENDEZVOUS_
#define _RENDEZVOUS_

#include "modcounter.h"
#include "list.h"
#include "config.h"

namespace rendezvous
{

/*
 * Idea comes from "Implementing Dataflow With Threads" by Leslie Lamport, Distributed Computing 21, 3 (2008), 163-181.
 * Also appeared as Microsoft Research Technical Report MSR-TR-2006-181 (December 2006).
 */

struct Default {};

template<typename Data    = Default,
         typename Type    = Default,
         typename ModCntr = ModCntr<>
         >
class Rendezvous
{

    struct WaitOrActiveList {};
    struct RemoveList {};

    enum class RemoveAction
    {
        GO,
        WAIT,
        SYNC
    };

public:
    Rendezvous()
    {
    }

    ~Rendezvous()
    {
        while (active || removing || waiting)
        {
        }
    }

    Rendezvous(const Rendezvous&)  = delete;
    Rendezvous(const Rendezvous&&) = delete;

    class Place : public list::Element<Place, WaitOrActiveList>,
        public list::Element<Place, RemoveList>
    {
        Rendezvous  & rendezvous;
        bool volatile wait;
        RemoveAction volatile remove;
        ModCntr counter;

        void(&BusyWaitHandler)(int&);
        bool(&CAS)(void**, void*, void*);

        Data* data;

        template<typename F>
        void WaitWhile(F f)
        {
            int loopCount = 0;
            while (f())
            {
                BusyWaitHandler(loopCount);
            }
        }

        template<typename F>
        void WaitUntil(F f)
        {
            int loopCount = 0;
            while (!f())
            {
                BusyWaitHandler(loopCount);
            }
        }

        int Sync()
        {
            ++counter;
            int passed = 0;
            WaitWhile([ this, &passed ]
                        {
                            return rendezvous.active.Search([ this, &passed ] (auto& a)
                            {
                                passed = counter > a.counter ? 0 : passed + 1;
                                return passed == 0;
                            });
                        });
            return passed;
        }

        bool IsMaster()
        {
            return rendezvous.active.GetHead() == this;
        }

        void ProcessWaitingList()
        {
            if (rendezvous.waiting)
            {
                typename ModCntr::type c = counter;
                auto w = rendezvous.waiting.AcquireAtomic(BusyWaitHandler, CAS);
                w.Iterate([ &c ] (auto& p) { p.counter = c; });
                rendezvous.active.Append(w);
                w.Iterate([ ] (auto& p) { p.wait = false; });
            }
        }

        bool TryToBecomeMaster()
        {
            if ((rendezvous.waiting.GetHead() == this)
                && !rendezvous.active)
            {
                rendezvous.active.SetAtomic(BusyWaitHandler, CAS, rendezvous.waiting);
                rendezvous.active.Iterate([ ] (auto& p) { p.wait = false; });
                return true;
            }
            return false;
        }

    public:
        Place() = delete;
        Place(const Place&)  = delete;
        Place(const Place&&) = delete;
        Place& operator=(const Place&)  = delete;
        Place& operator=(const Place&&) = delete;

        template<typename Cfg>
        Place(Rendezvous& r, const Cfg & cfg) :
          rendezvous(r),
          wait(true),
          remove(RemoveAction::GO),
          BusyWaitHandler(Cfg::BusyWaitHandler),
          CAS(Cfg::CAS),
          data(nullptr)
        {
            counter = 0;
            rendezvous.waiting.AddAtomic(BusyWaitHandler, CAS, *this);
        }

        Place(Rendezvous& r) : Place(r, DefAtomicCfg())
        {
        }

        ~Place()
        {
            bool master;

            WaitWhile([ this ] { return wait && !TryToBecomeMaster(); });

            if ((master = IsMaster()))
            {
                ProcessWaitingList();
            }

            remove = RemoveAction::WAIT;
            rendezvous.removing.AddAtomic(BusyWaitHandler, CAS, *this);

            Sync();

            if (master)
            {
                auto r = rendezvous.removing.AcquireAtomic(BusyWaitHandler, CAS);
                rendezvous.active.Remove(r);
                r.Iterate([ ] (auto& p) { p.remove = RemoveAction::SYNC; });

                remove    = RemoveAction::WAIT;
                WaitWhile ([&r]
                             { return r.Search([ ] (auto &p)
                                {
                                    return p.remove != RemoveAction::WAIT;
                                });
                            });
                r.Iterate([ ] (auto& p) { p.remove = RemoveAction::GO; });
            }
            else
            {
                WaitWhile([ this ] { return remove == RemoveAction::WAIT; });
                if (remove == RemoveAction::SYNC)
                {
                    remove = RemoveAction::WAIT;
                    WaitUntil([ this ] { return remove == RemoveAction::GO; });
                }
            }
        }

        int Attend()
        {
            Data tmp;
            struct { int passed; } acc;
            int& r = Attend([ &acc ] (int passed) -> decltype(acc)&
                               {
                                   acc.passed = passed;
                                   return acc;
                               },
                            [ ] (auto&, auto&) {},
                            [ ] (auto& a) -> int& { return a.passed; },
                            tmp);
            return r;
        }

        template<typename InitFunc, typename FoldFunc, typename ComputeFunc>
        auto Attend(InitFunc    Init,
                    FoldFunc    Fold,
                    ComputeFunc Compute,
                    Data      & d)->decltype(Compute(Init(1)))
        &
        {
            data = &d;

            bool master;

            WaitWhile([ this ] { return wait && !TryToBecomeMaster(); });

            if ((master = IsMaster()))
            {
                ProcessWaitingList();
            }

            Sync();

            decltype(rendezvous.removing)to_remove;
            if (master && rendezvous.removing)
            {
                to_remove = rendezvous.removing.AcquireAtomic(BusyWaitHandler, CAS);
                rendezvous.active.Remove(to_remove);
            }

            int passed = Sync();

            to_remove.Iterate([ ] (auto& p) { p.remove = RemoveAction::GO; });

            auto& accumulator = Init(passed);

            rendezvous.active.Iterate([ &accumulator, &Fold ] (Place& elt)
                        {
                            Fold(accumulator, const_cast<const Data&>(*elt.data));
                        });

            auto& result = Compute(accumulator);

            Sync();

            return result;
        }

    };
private:
    list::LockFreeList<Place, WaitOrActiveList> waiting;
    list::LockFreeList<Place, RemoveList> removing;
    list::LockFreeList<Place, WaitOrActiveList> active;
};

}

#endif // _RENDEZVOUS_
