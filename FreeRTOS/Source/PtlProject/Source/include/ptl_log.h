#ifndef __PTL_LOG__
#define __PTL_LOG__

#include "ptl_pi.h"

     #if (configUSE_PTL_SYSTEM == 1)
    
        /**
        * @brief PTL scheduler events
        * @details Enumeration of all the possible events that can be logged by the
        *          Periodic Task Layer. Used in the RAM buffer to identify the type
        *          of scheduler event.
        * * **Fields:**
        * - **PTL_EVT_RELEASE**: Task job released (activation)
        * - **PTL_EVT_START**: Task started execution
        * - **PTL_EVT_COMPLETE**: Task completed execution
        * - **PTL_EVT_DEADLINE_MISS**: Task finished after its deadline
        * - **PTL_EVT_OVERRUN_SKIP**: Overrun occurred, policy SKIP applied
        * - **PTL_EVT_OVERRUN_KILL**: Overrun occurred, policy KILL applied
        * - **PTL_EVT_OVERRUN_CATCH_UP**: Overrun occurred, policy CATCH_UP applied
        */
        typedef enum {
            ePtlEvtRelease = 0,
            ePtlEvtStart,
            ePtlEvtCompleteEnd,
            ePtlEvtDeadLineMiss,
            ePtlEvtOverrunSkip,
            ePtlEvtOverrunKill,
            ePtlEvtOverrunCatchUp,

            ePtlEvtServerReplenish,     /* Budget restored to max */
            ePtlEvtServerExhausted,     /* Budget hit 0 */
            ePtlEvtAperiodicEnqueue,    /* Job added to queue */
            ePtlEvtAperiodicStart,      /* Job started execution */
            ePtlEvtAperiodicDone,       /* Job finished execution */
            ePtlEvtAperiodicDrop,       /*queue is full, drop       */
            ePtlEvtAperiodicOverrun,
            ePtlEvtAperiodicKill
        } PtlEvent_t;

        /**
        * @brief Structure of logging in the RAM BUFFER.
        * @details Each instance corresponds to one scheduler event generated
        *          by the Periodic Task Layer (PTL) and stored in the RAM-based
        *          circular log buffer. When the buffer is full, new entries
        *          overwrite the oldest ones. The log is intended for offline
        *          analysis and debugging, and is not printed during runtime.
        * * **Fields:**
        * - **timestamp**: System tick count at which the event was generated.
        * - **taskId**: ID of the task that generated the event.
        * - **event**: The type of scheduler event (see ::PtlEvent_t).
        * - **jobSeq**: Sequential job number of the task (k-th released job)
        */
        typedef struct {
            TickType_t xTimestamp;
            uint8_t    ucTaskId;
            PtlEvent_t eEvent;
            uint32_t   ulJobSeq;
        } PtlLogTask_t;

        #define PTL_LOG_BUFFER_SIZE 2048

        extern PtlLogTask_t xPtlLogBuffer[PTL_LOG_BUFFER_SIZE];
        extern uint32_t ulPtlLogWriteIdx;
        extern BaseType_t xPtlLogFull;

        /**
        * @brief Dump all logged events to stdout
        * @details Prints all entries stored in the circular RAM buffer. Called typically
        *          at the end of the simulation. Does not affect real-time execution.
        */
        void vPtlLogDump(void);

        /**
        * @brief Dump runtime statistics for all periodic tasks
        * @details Prints per-task statistics including:
        *          - Total jobs released
        *          - Deadline misses
        *          - Overruns
        *          - Worst-Case Execution Time (WCET)
        *          - Average execution time
        *          Typically called at the end of the simulation.
        */
        void vPtlStatsDump(void);

        /**
        * @brief Log an event for a specific task
        * @details Writes a scheduler event in the RAM buffer. This function is real-time
        *          safe and uses a circular buffer to avoid memory overflow.
        *
        * @param[in] pxTask Pointer to the task generating the event
        * @param[in] evt    Type of event to log (see ::PtlEvent_t)
        */
        #define _PTL_LOG( _ucId, _ulSeq, _evt, _enterCrit, _exitCrit, _tickCount )          \
            do {                                                                         \
                UBaseType_t uxSavedStatus;                                           \
                (void) uxSavedStatus;                                               \
                _enterCrit;                                                              \
                                                                                        \
                xPtlLogBuffer[ulPtlLogWriteIdx].xTimestamp = (_tickCount); \
                xPtlLogBuffer[ulPtlLogWriteIdx].ucTaskId   = ( _ucId );                  \
                xPtlLogBuffer[ulPtlLogWriteIdx].eEvent     = (PtlEvent_t)( _evt );                   \
                xPtlLogBuffer[ulPtlLogWriteIdx].ulJobSeq   = ( _ulSeq );                 \
                                                                                        \
                ulPtlLogWriteIdx++;                                                      \
                if( ulPtlLogWriteIdx >= PTL_LOG_BUFFER_SIZE )                            \
                {                                                                        \
                    ulPtlLogWriteIdx = 0;                                                \
                    xPtlLogFull = pdTRUE;                                                \
                }                                                                        \
                                                                                        \
                _exitCrit;                                                               \
            } while( 0 )

        /* PERIODIC TASKS                                                        */
        /* --------------------------------------------------------------------- */
        #define BUFFER_RAM_WRITE(pxTask, evt) \
            _PTL_LOG( (pxTask)->ucId, (pxTask)->xTaskStats.ulTotalJobsReleased, evt, \
                           taskENTER_CRITICAL(), taskEXIT_CRITICAL(), xTaskGetTickCount())

        #define BUFFER_RAM_WRITE_FROM_ISR(pxTask, evt) \
            _PTL_LOG( (pxTask)->ucId, (pxTask)->xTaskStats.ulTotalJobsReleased, evt, \
                           uxSavedStatus = taskENTER_CRITICAL_FROM_ISR(), taskEXIT_CRITICAL_FROM_ISR(uxSavedStatus), xTaskGetTickCountFromISR())

        /* APERIODIC/SERVER                                                      */
        /* --------------------------------------------------------------------- */
        #define BUFFER_RAM_WRITE_APERIODIC(ucId, ulSeq, evt) \
            _PTL_LOG( ucId, ulSeq, evt, \
                           taskENTER_CRITICAL(), taskEXIT_CRITICAL(), xTaskGetTickCount() )

        #define BUFFER_RAM_WRITE_APERIODIC_FROM_ISR(ucId, ulSeq, evt) \
            _PTL_LOG( ucId, ulSeq, evt, \
                           uxSavedStatus = taskENTER_CRITICAL_FROM_ISR(), taskEXIT_CRITICAL_FROM_ISR(uxSavedStatus), xTaskGetTickCountFromISR() )


    #endif /* (configUSE_PTL_SYSTEM == 1)*/

#endif /*__PTL_LOG__*/