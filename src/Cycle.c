/* 
 * The MIT License
 *
 * Copyright 2020 The OpenNARS authors.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "Cycle.h"

static long conceptProcessID = 0; //avoids duplicate concept processing

//doing inference within the matched concept, returning whether decisionMaking should continue
static Decision Cycle_ActivateSensorimotorConcept(Concept *c, Event *e, long currentTime)
{
    Decision decision = {0};
    if(e->truth.confidence > MIN_CONFIDENCE)
    {
        c->usage = Usage_use(c->usage, currentTime, false);
        //add event as spike to the concept:
        if(e->type == EVENT_TYPE_BELIEF)
        {
            c->belief_spike = *e;
        }
        else
        {
            //pass spike if the concept doesn't have a satisfying motor command
            decision = Decision_Suggest(c, e, currentTime);
        }
    }
    return decision;
}

//Process an event, by creating a concept, or activating an existing
static Decision Cycle_ProcessSensorimotorEvent(Event *e, long currentTime)
{
    conceptProcessID++; //process the to e related concepts
    Decision best_decision = {0};
    //add a new concept for e if not yet existing
    Memory_Conceptualize(&e->term, currentTime);
    e->processed = true;
    Event_SetTerm(e, e->term); // TODO make sure that hash needs to be calculated once instead already
    IN_DEBUG( puts("Event was selected:"); Event_Print(e); )
    //determine the concept it is related to
    bool e_hasVariable = Variable_hasVariable(&e->term, true, true, true);
    for(int i=0; i<UNIFICATION_DEPTH; i++)
    {
        ConceptChainElement chain_extended = { .c = Memory_FindConceptByTerm(&e->term), .next = InvertedAtomIndex_GetConceptChain(e->term.atoms[i]) };
        ConceptChainElement* chain = &chain_extended;
        while(chain != NULL)
        {
            Concept *c = chain->c;
            chain = chain->next;
            if(c != NULL && c->processID != conceptProcessID)
            {
                c->processID = conceptProcessID;
                Event ecp = *e;
                if(!e_hasVariable)  //concept matched to the event which doesn't have variables
                {
                    Substitution subs = Variable_Unify(&c->term, &e->term); //concept with variables, 
                    if(subs.success)
                    {
                        ecp.term = e->term;
                        Decision decision = Cycle_ActivateSensorimotorConcept(c, &ecp, currentTime);
                        if(decision.execute && decision.desire >= best_decision.desire && (!best_decision.specialized || decision.specialized))
                        {
                            best_decision = decision;
                        }
                    }
                }
                else
                {
                    Substitution subs = Variable_Unify(&e->term, &c->term); //event with variable matched to concept
                    if(subs.success)
                    {
                        bool success;
                        ecp.term = Variable_ApplySubstitute(e->term, subs, &success);
                        if(success)
                        {
                            Decision decision = Cycle_ActivateSensorimotorConcept(c, &ecp, currentTime);
                            if(decision.execute && decision.desire >= best_decision.desire && (!best_decision.specialized || decision.specialized))
                            {
                                best_decision = decision;
                            }
                        }
                    }
                }
            }
        }
    }
    return best_decision;
}

void Cycle_PopEvents(Event *selectionArray, double *selectionPriority, int *selectedCnt, PriorityQueue *queue, int cnt)
{
    *selectedCnt = 0;
    for(int i=0; i<cnt; i++)
    {
        Event *e;
        double priority = 0;
        if(!PriorityQueue_PopMax(queue, (void**) &e, &priority))
        {
            assert(queue->itemsAmount == 0, "No item was popped, only acceptable reason is when it's empty");
            IN_DEBUG( puts("Selecting event failed, maybe there is no event left."); )
            break;
        }
        selectionPriority[*selectedCnt] = priority;
        selectionArray[*selectedCnt] = *e; //needs to be copied because will be added in a batch
        (*selectedCnt)++; //that while processing, would make recycled pointers invalid to use
    }
}

//Propagate subgoals, leading to decisions
static Decision Cycle_PropagateSubgoals(long currentTime)
{
    Decision best_decision = {0};
    //process selected goals
    for(int i=0; i<goalsSelectedCnt; i++)
    {
        Event *goal = &selectedGoals[i];
        IN_DEBUG( fputs("selected goal ", stdout); Narsese_PrintTerm(&goal->term); puts(""); )
        Decision decision = Cycle_ProcessSensorimotorEvent(goal, currentTime);
        if(decision.execute && decision.desire > best_decision.desire && (!best_decision.specialized || decision.specialized))
        {
            best_decision = decision;
        }
    }
    //pass goal spikes on to the next
    for(int i=0; i<goalsSelectedCnt; i++)
    {
        conceptProcessID++; //process subgoaling for the related concepts for each selected goal
        Event *goal = &selectedGoals[i];
        for(int k=0; k<UNIFICATION_DEPTH; k++)
        {
            ConceptChainElement chain_extended = { .c = Memory_FindConceptByTerm(&goal->term), .next = InvertedAtomIndex_GetConceptChain(goal->term.atoms[k]) };
            ConceptChainElement* chain = &chain_extended;
            while(chain != NULL)
            {
                Concept *c = chain->c;
                chain = chain->next;
                if(c != NULL && c->processID != conceptProcessID && Variable_Unify(&c->term, &goal->term).success) //could be <a --> M>! matching to some <... =/> <$1 --> M>>.
                {
                    c->processID = conceptProcessID;
                    bool revised;
                    c->goal_spike = Inference_RevisionAndChoice(&c->goal_spike, goal, currentTime, &revised);
                    for(int opi=0; opi<=OPERATIONS_MAX; opi++)
                    {
                        for(int j=0; j<c->precondition_beliefs[opi].itemsAmount; j++)
                        {
                            Implication *imp = &c->precondition_beliefs[opi].array[j];
                            if(!Memory_ImplicationValid(imp))
                            {
                                Table_Remove(&c->precondition_beliefs[opi], j);
                                j--;
                                continue;
                            }
                            Event newGoal = Inference_GoalDeduction(&c->goal_spike, imp);
                            Event newGoalUpdated = Inference_EventUpdate(&newGoal, currentTime);
                            IN_DEBUG( fputs("derived goal ", stdout); Narsese_PrintTerm(&newGoalUpdated.term); puts(""); )
                            Memory_AddEvent(&newGoalUpdated, currentTime, selectedGoalsPriority[i] * Truth_Expectation(newGoalUpdated.truth), 0, false, true, false, false, false);
                        }
                    }
                }
            }
        }
    }
    return best_decision;
}

//Reinforce link between concept a and b (creating it if non-existent)
static void Cycle_ReinforceLink(Event *a, Event *b)
{
    if(a->type != EVENT_TYPE_BELIEF || b->type != EVENT_TYPE_BELIEF)
    {
        return;
    }
    Term a_term_nop = Narsese_GetPreconditionWithoutOp(&a->term);
    Concept *A = Memory_FindConceptByTerm(&a_term_nop);
    Concept *B = Memory_FindConceptByTerm(&b->term);
    if(A != NULL && B != NULL && A != B)
    {
        //temporal induction
        if(!Stamp_checkOverlap(&a->stamp, &b->stamp))
        {
            bool success;
            Implication precondition_implication = Inference_BeliefInduction(a, b, &success);
            if(success)
            {
                precondition_implication.sourceConcept = A;
                precondition_implication.sourceConceptId = A->id;
                if(precondition_implication.truth.confidence >= MIN_CONFIDENCE)
                {
                    //extensional var intro:
                    bool success;
                    Term general_implication_term_ext = IntroduceImplicationVariables(precondition_implication.term, &success, true);
                    if(success && Variable_hasVariable(&general_implication_term_ext, true, true, false))
                    {
                        NAL_DerivedEvent(general_implication_term_ext, OCCURRENCE_ETERNAL, precondition_implication.truth, precondition_implication.stamp, currentTime, 1, 1, precondition_implication.occurrenceTimeOffset, NULL, 0);
                    }
                    //intensional var intro:
                    bool success2;
                    Term general_implication_term_int = IntroduceImplicationVariables(precondition_implication.term, &success2, false);
                    if(success2 && Variable_hasVariable(&general_implication_term_int, true, true, false))
                    {
                        NAL_DerivedEvent(general_implication_term_int, OCCURRENCE_ETERNAL, precondition_implication.truth, precondition_implication.stamp, currentTime, 1, 1, precondition_implication.occurrenceTimeOffset, NULL, 0);
                    }
                    //specific implication
                    NAL_DerivedEvent(precondition_implication.term, OCCURRENCE_ETERNAL, precondition_implication.truth, precondition_implication.stamp, currentTime, 1, 1, precondition_implication.occurrenceTimeOffset, NULL, 0);
                }
            }
        }
    }
}

void Cycle_PushEvents(long currentTime)
{
    for(int i=0; i<beliefsSelectedCnt; i++)
    {
        Memory_AddEvent(&selectedBeliefs[i], currentTime, selectedBeliefsPriority[i], 0, false, false, true, false, false);
    }
    for(int i=0; i<goalsSelectedCnt; i++)
    {
        Memory_AddEvent(&selectedGoals[i], currentTime, selectedGoalsPriority[i], 0, false, false, true, false, false);
    }
}

void Cycle_ProcessInputBeliefEvents(long currentTime)
{
    //1. process newest event
    if(belief_events.itemsAmount > 0)
    {
        //form concepts for the sequences of different length
        for(int len=0; len<MAX_SEQUENCE_LEN; len++)
        {
            Event *toProcess = FIFO_GetNewestSequence(&belief_events, len);
            if(toProcess != NULL && !toProcess->processed && toProcess->type != EVENT_TYPE_DELETED)
            {
                assert(toProcess->type == EVENT_TYPE_BELIEF, "A different event type made it into belief events!");
                Cycle_ProcessSensorimotorEvent(toProcess, currentTime);
                Event postcondition = *toProcess;
                //Mine for <(&/,precondition,operation) =/> postcondition> patterns in the FIFO:
                if(len == 0) //postcondition always len1
                {
                    int op_id = Narsese_getOperationID(&postcondition.term);
                    Decision_AssumptionOfFailure(op_id, currentTime); //collection of negative evidence, new way
                    for(int k=1; k<belief_events.itemsAmount; k++)
                    {
                        for(int len2=0; len2<MAX_SEQUENCE_LEN; len2++)
                        {
                            Event *precondition = FIFO_GetKthNewestSequence(&belief_events, k, len2);
                            if(len2 > 0)
                            {
                                Event *potential_op = FIFO_GetKthNewestSequence(&belief_events, k+len2, 0);
                                if(potential_op != NULL && potential_op->type != EVENT_TYPE_DELETED && Narsese_isOperation(&potential_op->term))
                                {
                                    break;
                                }
                            }
                            if(precondition != NULL && precondition->type != EVENT_TYPE_DELETED)
                            {
                                Cycle_ReinforceLink(precondition, &postcondition);
                            }
                        }
                    }
                }
            }
        }
    }
}

void Cycle_ProcessInputGoalEvents(long currentTime)
{
    //process goals
    Decision decision = {0};
    if(goal_events.itemsAmount > 0)
    {
        Event *goal = FIFO_GetNewestSequence(&goal_events, 0);
        if(!goal->processed && goal->type!=EVENT_TYPE_DELETED)
        {
            assert(goal->type == EVENT_TYPE_GOAL, "A different event type made it into goal events!");
            decision = Cycle_ProcessSensorimotorEvent(goal, currentTime);
        }
    }
    //allow reasoning into the future by propagating spikes from goals back to potential current events
    if(!decision.execute)
    {
        decision = Cycle_PropagateSubgoals(currentTime);
    }
    if(decision.execute && decision.operationID > 0)
    {
        Decision_Execute(&decision);
        //reset cycling goal events after execution to avoid "residue actions"
        PriorityQueue_RESET(&cycling_goal_events, cycling_goal_events.items, cycling_goal_events.maxElements);
        //also don't re-add the selected goal:
        goalsSelectedCnt = 0;
    }
}

void Cycle_Inference(long currentTime)
{
    //Inferences
#if STAGE==2
    for(int i=0; i<beliefsSelectedCnt; i++)
    {
        conceptProcessID++; //process the related belief concepts
        long countConceptsMatched = 0;
        for(;;)
        {
            long countConceptsMatchedNew = 0;
            //Adjust dynamic firing threshold: (proportional "self"-control)
            double conceptPriorityThresholdCurrent = conceptPriorityThreshold;
            long countConceptsMatchedAverage = Stats_countConceptsMatchedTotal / currentTime;
            double set_point = BELIEF_CONCEPT_MATCH_TARGET;
            double process_value = countConceptsMatchedAverage; 
            double error = process_value - set_point;
            double increment = error*CONCEPT_THRESHOLD_ADAPTATION;
            conceptPriorityThreshold = MIN(1.0, MAX(0.0, conceptPriorityThreshold + increment));
            //IN_DEBUG( printf("conceptPriorityThreshold=%f\n", conceptPriorityThreshold); )
            Event *e = &selectedBeliefs[i];
            double priority = selectedBeliefsPriority[i];
            Term dummy_term = {0};
            Truth dummy_truth = {0};
            RuleTable_Apply(e->term, dummy_term, e->truth, dummy_truth, e->occurrenceTime, e->stamp, currentTime, priority, 1, false, NULL, 0); 
            IN_DEBUG( puts("Event was selected:"); Event_Print(e); )
            for(int k=0; k<UNIFICATION_DEPTH; k++)
            {
                ConceptChainElement* chain = InvertedAtomIndex_GetConceptChain(e->term.atoms[k]);
                while(chain != NULL)
                {
                    Concept *c = chain->c;
                    chain = chain->next;
                    if(c != NULL && c->processID != conceptProcessID)
                    {
                        c->processID = conceptProcessID;
                        long validation_cid = c->id; //allows for lockfree rule table application (only adding to memory is locked)
                        if(c->priority < conceptPriorityThresholdCurrent)
                        {
                            continue;
                        }
                        countConceptsMatchedNew++;
                        countConceptsMatched++;
                        Stats_countConceptsMatchedTotal++;
                        if(c->belief.type != EVENT_TYPE_DELETED && countConceptsMatched <= BELIEF_CONCEPT_MATCH_TARGET)
                        {
                            //use eternal belief as belief
                            Event* belief = &c->belief;
                            Event future_belief = c->predicted_belief;
                            //but if there is a predicted one in the event's window, use this one
                            if(e->occurrenceTime != OCCURRENCE_ETERNAL && future_belief.type != EVENT_TYPE_DELETED &&
                               labs(e->occurrenceTime - future_belief.occurrenceTime) < EVENT_BELIEF_DISTANCE) //take event as belief if it's stronger
                            {
                                future_belief.truth = Truth_Projection(future_belief.truth, future_belief.occurrenceTime, e->occurrenceTime);
                                future_belief.occurrenceTime = e->occurrenceTime;
                                belief = &future_belief;
                            }
                            //unless there is an actual belief which falls into the event's window
                            Event project_belief = c->belief_spike;
                            if(e->occurrenceTime != OCCURRENCE_ETERNAL && project_belief.type != EVENT_TYPE_DELETED &&
                               labs(e->occurrenceTime - project_belief.occurrenceTime) < EVENT_BELIEF_DISTANCE) //take event as belief if it's stronger
                            {
                                project_belief.truth = Truth_Projection(project_belief.truth, project_belief.occurrenceTime, e->occurrenceTime);
                                project_belief.occurrenceTime = e->occurrenceTime;
                                belief = &project_belief;
                            }
                            //Check for overlap and apply inference rules
                            if(!Stamp_checkOverlap(&e->stamp, &belief->stamp))
                            {
                                Stamp stamp = Stamp_make(&e->stamp, &belief->stamp);
                                if(PRINT_CONTROL_INFO)
                                {
                                    fputs("Apply rule table on ", stdout);
                                    Narsese_PrintTerm(&e->term);
                                    printf(" Priority=%f\n", priority);
                                    fputs(" and ", stdout);
                                    Narsese_PrintTerm(&c->term);
                                    puts("");
                                }
                                RuleTable_Apply(e->term, c->term, e->truth, belief->truth, e->occurrenceTime, stamp, currentTime, priority, c->priority, true, c, validation_cid);
                            }
                        }
                    }
                }
            }
            if(countConceptsMatched > Stats_countConceptsMatchedMax)
            {
                Stats_countConceptsMatchedMax = countConceptsMatched;
            }
            if(countConceptsMatched >= BELIEF_CONCEPT_MATCH_TARGET || countConceptsMatchedNew == 0)
            {
                break;
            }
        }
    }
#endif
}

void Cycle_Prediction(long currentTime)
{
    for(int h=0; h<beliefsSelectedCnt; h++)
    {
        Event *e = &selectedBeliefs[h];
        double parentpriority = selectedBeliefsPriority[h];
        #pragma omp parallel for
        for(int j=0; j<concepts.itemsAmount; j++)
        {
            Concept *c = concepts.items[j].address;
            if(c->priority < conceptPriorityThreshold)
            {
                continue;
            }
            for(int k=0; k<c->precondition_beliefs[0].itemsAmount; k++)
            {
                Implication imp = c->precondition_beliefs[0].array[k];
                Term subject = Term_ExtractSubterm(&imp.term, 1);
                if(Variable_Unify(&subject, &e->term).success)
                {
                    for(int i=0; i<c->precondition_beliefs[0].itemsAmount; i++)
                    {
                        if(!Memory_ImplicationValid(&c->precondition_beliefs[0].array[i]))
                        {
                            Table_Remove(&c->precondition_beliefs[0], i--);
                            continue;
                        }
                        Implication *imp = &c->precondition_beliefs[0].array[i];
                        assert(Narsese_copulaEquals(imp->term.atoms[0],'$'), "Not a valid implication term!");
                        Term precondition_with_op = Term_ExtractSubterm(&imp->term, 1);
                        Term precondition = Narsese_GetPreconditionWithoutOp(&precondition_with_op);
                        Concept *c_pre = Memory_FindConceptByTerm(&precondition);
                        if(c_pre != NULL)
                        {
                            Substitution subs = Variable_Unify(&precondition, &e->term);
                            if(subs.success)
                            {
                                Implication updated_imp = *imp;
                                bool success;
                                updated_imp.term = Variable_ApplySubstitute(updated_imp.term, subs, &success);
                                if(success)
                                {
                                    Event predicted = Inference_BeliefDeduction(e, &updated_imp);
                                    #pragma omp critical(Memory)
                                    {
                                        Memory_AddEvent(&predicted, currentTime, parentpriority*Truth_Expectation(predicted.truth), 0, false, true, false, false, true);
                                    }
                                }
                            }
                        }
                    }
                    break;
                }
            }
        }
    }
}

void Cycle_RelativeForgetting(long currentTime)
{
    //Apply event forgetting:
    for(int i=0; i<cycling_belief_events.itemsAmount; i++)
    {
        cycling_belief_events.items[i].priority *= EVENT_DURABILITY;
    }
    for(int i=0; i<cycling_goal_events.itemsAmount; i++)
    {
        cycling_goal_events.items[i].priority *= EVENT_DURABILITY;
    }
    //Apply concept forgetting:
    for(int i=0; i<concepts.itemsAmount; i++)
    {
        Concept *c = concepts.items[i].address;
        c->priority *= CONCEPT_DURABILITY;
        concepts.items[i].priority = Usage_usefulness(c->usage, currentTime); //how concept memory is sorted by, by concept usefulness
    }
    //BEGIN SPECIAL HANDLING FOR USER KNOWLEDGE
    if(ontology_handling)
    {
        //BEGIN SPECIAL HANDLING FOR USER KNOWLEDGE
        for(int i=0; i<concepts.itemsAmount; i++)
        {
            Concept *c = concepts.items[i].address;
            if(c->hasUserKnowledge)
            {
                c->usage = Usage_use(c->usage, currentTime, false); //user implication won't be forgotten
            }
        }
    }
    //END SPECIAL HANDLING FOR USER KNOWLEDGE
    //Re-sort queues
    PriorityQueue_Rebuild(&concepts);
    PriorityQueue_Rebuild(&cycling_belief_events);
    PriorityQueue_Rebuild(&cycling_goal_events);
}

void Cycle_Perform(long currentTime)
{   
    Metric_send("NARNode.Cycle", 1);
    //1. Retrieve BELIEF/GOAL_EVENT_SELECTIONS events from cyclings events priority queue (which includes both input and derivations)
    Cycle_PopEvents(selectedGoals, selectedGoalsPriority, &goalsSelectedCnt, &cycling_goal_events, GOAL_EVENT_SELECTIONS);
    Cycle_PopEvents(selectedBeliefs, selectedBeliefsPriority, &beliefsSelectedCnt, &cycling_belief_events, BELIEF_EVENT_SELECTIONS);
    //2. Process incoming belief events from FIFO, building implications utilizing input sequences and in 1. retrieved events.
    Cycle_ProcessInputBeliefEvents(currentTime);
    //3. Process incoming goal events from FIFO, propagating subgoals according to implications, triggering decisions when above decision threshold
    Cycle_ProcessInputGoalEvents(currentTime);
    //4. Perform inference between in 1. retrieved events and semantically/temporally related, high-priority concepts to derive and process new events
    Cycle_Inference(currentTime);
    Cycle_Prediction(currentTime);
    //5. Apply relative forgetting for concepts according to CONCEPT_DURABILITY and events according to BELIEF_EVENT_DURABILITY
    Cycle_RelativeForgetting(currentTime);
    //6. Push in 1. selected events back to the queue as well, applying relative forgetting based on BELIEF_EVENT_DURABILITY_ON_USAGE
    Cycle_PushEvents(currentTime);
}
