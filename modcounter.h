namespace rendezvous
{

template<typename Int = int, const Int MOD = 0x100>
class ModCntr
{
    static constexpr Int MASK = MOD - 1;
    typedef typename std::enable_if<(MASK & MOD) == 0>::type mod_should_be_power_of_two;
public:

    typedef volatile Int type_;
    typedef Int type;

    ModCntr() : value(0) {}

    ModCntr& operator=(const type v)
    {
        value = v;
        return *this;
    }

    ModCntr& operator++()
    {
        value++;
        value &= MASK;
        return *this;
    }

    bool operator>(const ModCntr& o) const
    {
        const type a = value;
        const type b = o.value;
        type r;
        r = a >= b ? a - b :  a + MOD - b;
        return r == 1;
    }

    operator type() const
    {
        return static_cast<type>(value);
    }

private:
    type_ value;
};

}
