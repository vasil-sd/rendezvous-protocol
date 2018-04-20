#ifndef __RENDEZVOUS_MODCOUNTER__
#define __RENDEZVOUS_MODCOUNTER__

namespace rendezvous
{

template<bool B, class T = void>
struct enable_if {};
 
template<class T>
struct enable_if<true, T> { typedef T type; };


// Mdoular integral type suitable for rendezvous
template<typename Int = int, const Int MASK = 0xFFFF, const Int MOD = MASK + 1>
class ModCounter
{
    typedef typename enable_if<(MASK & MOD) == 0>::type mask_and_mod_should_be_compatible;
public:

    typedef volatile Int type_atomic;
    typedef Int type;

    ModCounter() : value(0) {}
    ModCounter& operator=(const type v)
    {
        value = v;
        return *this;
    }
    ModCounter& operator++()
    {
        type m = value;
        ++m;
        m &= MASK;
        value = m;
        return *this;
    }
    bool operator>(const ModCounter& o)
    {
        const type a = value;
        const type b = o.value;
        type r;
        if (a >= b)
        {
            r =  a - b;
        }
        else
        {
            r = a + MOD - b;
        }
        return r == 1;
    }
    operator type()
    {
        return static_cast<type>(value);
    }
private:
    type_atomic value;
};

}                        // end of namespace rendezvous

#endif // __RENDEZVOUS_MODCOUNTER__
