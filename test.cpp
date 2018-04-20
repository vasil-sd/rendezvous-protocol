#include <stdint.h>
#include <stdio.h>
#include <vector>
#include <random>
#include <chrono>
#include <thread>
#include <stdio.h>
#include "rendezvous.h"

struct R1
{
    int n;
};

namespace
{

struct RCfg : public rendezvous::DefaultAtomicConfig
{
    static void BusyWaitHandler(int& i)
    {
        if (++i > 5)
        {
            i = 0;
            std::this_thread::yield();
        }
    }

};

rendezvous::Rendezvous<R1> r1;
} // end of anonymous namespace

void
RFunc(int           m,
      int           n,
      volatile int* r,
      volatile int* min)
{
    (void)m;
    decltype(r1)::Place p(r1, RCfg());
    (*r)++;
    while (*r)
    {
    }
    int i;
    for (i = 0; i < n; i++)
    {

        // Example of calculating minimum among all participants
        R1 rr;
        rr.n = std::rand() % 500;

        struct {
            int n;
            int passed;
        } acc;
        acc.n = rr.n;

        int& a = p.Attend(
           // Init
           [&acc](int passed) -> decltype(acc)&
           {
               acc.passed = passed;
               return acc;
           },
           // Fold
           [] (auto& accum, const R1& rr)
           {
              if (rr.n < accum.n)
              {
                  accum.n = rr.n;
              }
           },
           // Compute
           [](auto& r) -> decltype(r.n)&
           {
               return r.n;
           },
        rr);

        rr.n = a;

        int& b = p.Attend(
           // Init
           [&acc](int passed) -> decltype(acc)&
           {
               acc.passed = passed;
               return acc;
           },
           // Fold
           [] (auto& accum, const R1& rr)
           {
              if (rr.n != accum.n)
              {
                  accum.passed = -1;
              }
           },
           // Compute
           [](auto& r) -> decltype(r.passed)&
           {
               return r.passed;
           },
        rr);

        *min = *min >= 0 ? b : *min;

        *r += acc.passed;
        if ((std::rand() & 0x1) == 0)
        {
            std::this_thread::yield();
        }
    }
}

bool
RunRendezTest(int n,
              int m)
{
    volatile int r[ m ];
    volatile int min[ m ];
    int j = 0;
    std::vector<std::thread> threads;
    for (auto& i : r)
    {
        i = 0;
        min[ j ] = 1;
        threads.push_back(std::thread(RFunc, m, n, &i, &min[ j ]));
        ++j;
    }
    while (true)
    {
        bool brk = true;
        for (auto& i : r)
        {
            if (!i)
            {
                brk = false;
                break;
            }
        }
        if (brk)
        {
            break;
        }
    }
    for (auto& i : r)
    {
        i = 0;
    }
    for (auto &t : threads)
    {
        t.join();
    }
    for (auto& i : r)
    {
        if (i != n * m)
        {
            return false;
        }
    }
    if(min[0] < 0 )
    {
        return false;
    }
    for (auto& mm : min)
    {
        if (mm != min[ 0 ])
        {
            return false;
        }
    }
    return true;
}


void
RFuncAsync(int           m,
           int           n,
           volatile int* r)
{
    (void)m;
    decltype(r1)::Place p(r1, RCfg());
    int i;
    for (i = 0; i < n; i++)
    {

        // Example of calculating minimum among all participants
        R1 rr;
        rr.n = i;

        struct {
            int n;
            int passed;
        } acc;
        acc.n = 0;

        int& a = p.Attend(
           // Init
           [&acc](int passed) -> decltype(acc)&
           {
               acc.passed = passed;
               return acc;
           },
           // Fold
           [] (auto& accum, const R1& rr)
           {
              accum.n += rr.n;
           },
           // Compute
           [](auto& a) -> decltype(a.n)&
           {
               return a.n;
           },
        rr);

        *r += a;
        if ((std::rand() & 0x1) == 0)
        {
            std::this_thread::yield();
        }
    }
}


bool
RunRendezTestAsync(int n,
              int m)
{
    volatile int r[ m ];
    volatile int sum[ m ];
    int j = 0;
    std::vector<std::thread> threads;
    for (auto& i : r)
    {
        i = 0;
        sum[ j ] = 1;
        threads.push_back(std::thread(RFunc, m, n, &i, &sum[ j ]));
        ++j;
    }
    for (auto &t : threads)
    {
        t.join();
    }
    return true;
}


int main()
{
    for (int i = 0; i < 10000; i++)
    {
        int     n = 1 + std::rand() % 10000;
        int     m = 2 + std::rand() % 50;
        fprintf(stderr, "Rendezvous test #%03d : threads = %d, iterations = %d, usec = ...", i, m, n);
        auto    start = std::chrono::high_resolution_clock::now();
        if(!RunRendezTest(n, m))
        {
            fprintf(stderr, "\b\b\bFail!\n");
        }
        else
        {
            auto    elapsed = std::chrono::high_resolution_clock::now() - start;
            int64_t microseconds = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
            fprintf(stderr, "\b\b\b%ld (%.2f sec)\n", microseconds, (double)microseconds / 1000000.0);
        }
    }
    return 0;
}
