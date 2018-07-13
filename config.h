namespace rendezvous
{

struct DefAtomicCfg
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

}
