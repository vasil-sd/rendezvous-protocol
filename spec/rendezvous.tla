----------------------------- MODULE rendezvous -----------------------------

EXTENDS Integers, FiniteSets, Sequences

CONSTANTS P, C

SeqToSet(S) == {S[i] : i \in 1..Len(S)}
InSeq(e, S) == e \in SeqToSet(S)
SubMod(a, b) == IF a > b THEN a - b ELSE a + C - b 

(*
--algorithm Rendezvous

  variables 
     waiting  = <<>>, \* list of waiting for subscription
     removing = <<>>, \* list of being removed
     active   = <<>>, \* list of active processes
     counters = [p \in P |-> 0],
     passed   = [p \in P |-> 0], \* number of active processes that passed rendezvous point
     wait     = [p \in P |-> TRUE], \* flags for initial waiting on first Attend
     remove   = [p \in P |-> 0]; \* flags for manage processes being unsubscribed

  define
     is_active(self)            == InSeq(self, active)
     is_waiting(self)           == InSeq(self, waiting)
     is_removing(self)          == InSeq(self, removing)
     is_master(self)            == active # <<>> /\ Head(active) = self
     is_potential_master(self)  == waiting # <<>> /\ Head(waiting) = self
     is_lagging(self, p)        == SubMod(counters[self], counters[p]) = 1
     
     \* constants
     removeAction_GO   == 0
     removeAction_SYNC == 2
     removeAction_WAIT == 1
  end define;
      
  fair process Proc \in P
  variable am_i_master = FALSE, to_remove = {}, select = FALSE
  begin
  main_loop:   while TRUE do

                 \* if not subscribed try to subscribe
  subscribe:     if ~is_active(self) /\ ~is_waiting(self) then
                     waiting        := Append(waiting, self);
                     wait[self]     := TRUE;
                     counters[self] := 0;
                 end if;

                 \* here is entry point to synchronization
  attend:        skip;

                 \* Consider curr. process as master if
                 \* active list is empty and curr. process
                 \* is in the head of waiting list.
                 \* And perform subscription to rendezvous 
                 \* of waiting processes.
  wait_loop:     while wait[self] do
                   if active = <<>> /\ is_potential_master(self) then
                     active  := waiting; 
                     wait    := [ p \in P |-> IF p \in SeqToSet(waiting) 
                                              THEN FALSE 
                                              ELSE wait[p] ];
                     waiting := <<>>;
                   end if;
                 end while;
  
                 \* If curr. process is master then
                 \* process waiting list if it is not empty.
  pr_waiting0:   am_i_master := is_master(self);
                 if am_i_master /\ waiting # <<>> then
                   \* set counters of waiting processes to
                   \* counter of master, so their counters
                   \* become consistent with current system state
                   counters := [ p \in P |-> IF p \in SeqToSet(waiting) 
                                             THEN counters[self]
                                             ELSE counters[p] ];
                   active   := active \o waiting;
                   \* force waiting processes to leave
                   \* waiting loop (wait_loop)
                   wait     := [ p \in P |-> IF p \in SeqToSet(waiting)
                                             THEN FALSE
                                             ELSE wait[p] ];
                   waiting     := <<>>;
                 end if;

                 \* first point of sync to bring system to
                 \* consistent state, regarding addition of
                 \* waiting processes
                 \* TODO: may be simulate it more close to sources?
                 \*       to be more confident about correctness
                 \*       taking into account mutating of active list
                 \*       during process of counters comparision
  sync1_0:       counters[self] := (counters[self] + 1) % C;
  sync1_1:       await { p \in SeqToSet(active) : is_lagging(self,p) } = {};
  
                 \* Check processes that are waiting to be removed
                 \* Step1: remove these processes from active list
  check_remove:  if am_i_master /\ removing # <<>> then
                   to_remove   := SeqToSet(removing);
                   removing    := <<>>;
                   active      := SelectSeq(active, LAMBDA x: x \notin to_remove);
                 end if;
                 am_i_master := FALSE;

                 \* second point of synchronization
                 \* here is main point of synchronization
                 \* it will be passed only by active processes
                 \* i.e. waiting already have been added, and
                 \* will participate in rendezvous, and
                 \* removing already have been removed
                 \* and won't influence rendezvous
  sync2_0:       counters[self] := (counters[self] + 1) % C;
  sync2_1:       await { p \in SeqToSet(active) : is_lagging(self,p) } = {};
                 passed[self]   := Len(active);

                 \* Continue to remove processes of removing list
                 \* Step2: notify removed processes by setting
                 \*        their remove[] flag
                 \* In current model there is no need to factorize
                 \* removing process into two stages, but
                 \* in implementation it is very important, because
                 \* of time to live of objects in active list, to avoid
                 \* situations of iterating over already freeed objects.
                 
  remove0:       remove := [p \in P |-> IF p \in to_remove
                                        THEN removeAction_GO
                                        ELSE remove[p]];
                 to_remove := {};

                 \* Third point of synchronization for
                 \* pr_waitingX actions.
                 \* When we call Attend continuosly
                 \* we may observe quirks with sync2 and
                 \* addition of waiting processes in case of absence
                 \* of sync3
  sync3_0:       counters[self] := (counters[self] + 1) % C;
  sync3_1:       await { p \in SeqToSet(active) : is_lagging(self,p) } = {};

                 \* So, comparing to original Lamport's algorithm,
                 \* we need three points of sync instead of one, because
                 \* we need to handle add/remove processes to list of active
                 \* In practice there is a neglible overhead because in
                 \* steady state of system (i.e. no new additions/ removings),
                 \* after first point of sync, second and third are taken
                 \* very fast, in a few dozens of cycles. Esp. if we disable 
                 \* preemption/interrupts during call to Attend function.

  sync_point:    skip;

                 \* Here is randomly choosen unsubscription activity
  unsubscribe:   with s \in BOOLEAN do
                   select := s;
                 end with;

                 \* If we've selected a blue pill :)
  unsubscribe0:  if select then
  
                   select := FALSE;
  
                   \* if process being removed is master one
                   \* then it is responsible for processing
                   \* removing list
  pr_waiting1_0:   am_i_master := is_master(self);
                   if am_i_master /\ waiting # <<>> then
                     counters := [ p \in P |-> IF p \in SeqToSet(waiting)
                                               THEN counters[self]
                                               ELSE counters[p] ];
                     active   := active \o waiting;
                     wait     := [ p \in P |-> IF p \in SeqToSet(waiting)
                                               THEN FALSE
                                               ELSE wait[p] ];
                     waiting  := <<>>;
                   end if;

                   \* set flag, indicating that process wants to
                   \* unsubscribe from rendezvous
  unsubscribe1:    remove[self] := removeAction_WAIT;

                   \* append itself to removing list
                   removing := Append(removing, self);

                   \* sync point, corresponding to sync1,
                   \* and used to sync active processes with
                   \* the set of being removed ones
  sync4_0:         counters[self] := (counters[self] + 1) % C;
  sync4_1:         await { p \in SeqToSet(active) : is_lagging(self,p) } = {};

                   \* if current process is master, then
                   \* it is still responsible to perform master's
                   \* tasks, despite of being removed
  check_master1:   if am_i_master then
                     am_i_master := FALSE;
                     to_remove := SeqToSet(removing);
                     removing  := <<>>;

                     \* clean active list
                     active    := SelectSeq(active, LAMBDA x: x \notin to_remove);

                     \* send all of the being removed processes a
                     \* flag, indicating the need of extra sync 
                     remove    := [p \in P |-> IF p \in to_remove
                                               THEN IF p = self
                                                    THEN removeAction_WAIT
                                                    ELSE removeAction_SYNC
                                               ELSE remove[p]];
  
                     \* wait until all being removed set flag WAIT
  check_remove1_5:   await \A p \in to_remove: remove[p] = removeAction_WAIT;

                     \* release all
                     remove    := [p \in P |-> IF p \in to_remove
                                               THEN removeAction_GO
                                               ELSE remove[p]];
                     to_remove := {};    
                   else

                     \* wait for GO or SYNC
  unsubscribe3:      await remove[self] # removeAction_WAIT;

                     \* if SYNC
                     if remove[self] = removeAction_SYNC then
                       remove[self] := removeAction_WAIT;
  unsubscribe6:        await remove[self] = removeAction_GO;
                     end if;
                   end if
                 end if;

               end while;
  end process

end algorithm
*)

\* BEGIN TRANSLATION
\* END TRANSLATION

\* for all processes on label "sync_point"
\* values of "passed" variables must be equal to each other
Invariant ==
 \A p1, p2 \in P: 
   (pc[p1] = "sync_point" /\ pc[p2] = "sync_point")
      => 
    passed[p1] = passed[p2]

\* All processes are alive and if process passed attend state it must eventually
\* in fufture pass sync_point, i.e. no infinite loops and dead-locks
Liveness == \A p \in P: []<>(pc[p] = "attend" ~> pc[p] = "sync_point")

=============================================================================
\* Modification History
\* Last modified Fri Jun 13 14:11:03 MSK 2018 by Vasil S. Dyadov
