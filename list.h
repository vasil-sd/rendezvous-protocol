namespace rendezvous
{
namespace list
{

template<typename T, typename Typ = T>
struct Element
{
    T* volatile next;
    Element() : next(nullptr)
    {
    }

};

template<typename T, typename Typ>
class LockFreeList
{
    T* volatile head;
    T* volatile tail;

    static T* GetTail(T* h)
    {
        T* t = h;
        while (t && t->Element<T, Typ>::next)
        {
            t = t->Element<T, Typ>::next;
        }
        return t;
    }

public:
    LockFreeList() : head(nullptr), tail(nullptr)
    {
    }

    T* GetHead() const
    {
        return head;
    }

    operator bool() const
    {
        return head != nullptr;
    }

    template<typename BWH, typename CASF>
    void SetAtomic(BWH           BusyWaitHandler,
                   CASF          CAS,
                   LockFreeList& l)
    {
        T*  t = l.tail;
        head = l.head;
        int loopCount = 0;
        while (!CAS(reinterpret_cast<void**>(const_cast<T**>(&l.tail)), t, nullptr))
        {
            BusyWaitHandler(loopCount);
            t = l.tail;
        }
        tail   = GetTail(t);
        l.head = nullptr;
    }

    template<typename BWH, typename CASF>
    LockFreeList AcquireAtomic(BWH  BusyWaitHandler,
                               CASF CAS)
    {
        LockFreeList l;
        if (tail)
        {
            T*  t = tail;
            int loopCount = 0;
            while (!CAS(reinterpret_cast<void**>(const_cast<T**>(&tail)), t, nullptr))
            {
                BusyWaitHandler(loopCount);
                t = tail;
            }
            l.head = head;
            head   = nullptr;
            l.tail = GetTail(t);
        }
        return l;
    }

    template<typename BWH, typename CASF>
    void AddAtomic(BWH  BusyWaitHandler,
                   CASF CAS,
                   T  & elt)
    {
        elt.Element<T, Typ>::next = nullptr;
        int loopCount = 0;
        while (true)
        {
            if (CAS(reinterpret_cast<void**>(const_cast<T**>(&tail)), nullptr, &elt))
            {
                int loopCount = 0;
                while (!CAS(reinterpret_cast<void**>(const_cast<T**>(&head)), nullptr, &elt))
                {
                    BusyWaitHandler(loopCount);
                }
                return;
            }
            T* t = tail;
            if (t && CAS(reinterpret_cast<void**>(const_cast<T**>(&t->Element<T, Typ>::next)), nullptr, &elt))
            {
                CAS(reinterpret_cast<void**>(const_cast<T**>(&tail)), t, t->Element<T, Typ>::next);
                return;
            }
            BusyWaitHandler(loopCount);
        }
    }

    void Append(LockFreeList& l)
    {
        if (tail)
        {
            tail->Element<T, Typ>::next = l.head;
            tail = GetTail(tail);
        }
        else
        {
            head = l.head;
            tail = GetTail(l.tail);
        }
    }

    template<typename Typ1>
    void Remove(LockFreeList<T, Typ1>& l)
    {
        T* h    = head;
        T* prev = nullptr;
        while (h)
        {
            T* next = h->Element<T, Typ>::next;
            if (l.Present(h))
            {
                if (prev)
                {
                    prev->Element<T, Typ>::next = next;
                }
                else
                {
                    head = next;
                }
            }
            else
            {
                prev = h;
            }
            h = next;
        }
        tail = prev;
    }

    template<typename F>
    bool Search(F f)
    {
        T* h = head;
        while (h)
        {
            if (f(*h))
            {
                return true;
            }
            h = h->Element<T, Typ>::next;
        }
        return false;
    }

    template<typename F>
    void Iterate(F f)
    {
        Search([ &f ] (T& t)
                        {
                            f(t);
                            return false;
                        });
    }

    bool Present(T* elt)
    {
        return Search([ &elt ] (auto& t)
                        {
                            return elt == &t;
                        });
    }

};

}
}
