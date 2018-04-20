# Implementation of rendezvous protocol

This implementation is dynamic variant of an algorithm proposed by Leslie Lamport
in his paper "Implementing Dataflow With Threads", Distributed Computing 21, 3 (2008), 163-181.

Implementation uses lock-free fifos only on subscribing/removing of activities, highly
parameterized and reliable.

User can supply custom CAS primitive, function to handle busy waiting loops and so on.

During sustained workflow no synchronization primitives, nor memory barriers are needed.

It is possible to put mem barriers in some points to somewhat accelerate synchronization, 
but everything works fine without barriers.
