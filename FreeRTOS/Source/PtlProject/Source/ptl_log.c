#include "ptl_log.h"
#include "uart.h"

#if (configUSE_PTL_SYSTEM == 1)

    PtlLogTask_t xPtlLogBuffer[PTL_LOG_BUFFER_SIZE];
    uint32_t ulPtlLogWriteIdx = 0;
    BaseType_t xPtlLogFull = pdFALSE;

    void vPtlLogDump(void)
    {
        uint32_t i, start, count;

        if (xPtlLogFull) {
            start = ulPtlLogWriteIdx;
            count = PTL_LOG_BUFFER_SIZE;
        } else {
            start = 0;
            count = ulPtlLogWriteIdx;
        }

        UART_printf("\n===== PTL EVENT TRACE =====\n");
        UART_printf("Time(tk)   Task  Job     Event\n");
        UART_printf("==========================================\n");

        for (i = 0; i < count; i++) {
            uint32_t idx = (start + i) % PTL_LOG_BUFFER_SIZE;
            PtlLogTask_t *rec = &xPtlLogBuffer[idx];

            const char *evtStr = "UNKNOWN";
            switch (rec->eEvent) {
                case ePtlEvtRelease:           evtStr = "RELEASE"; break;
                case ePtlEvtStart:             evtStr = "START"; break;
                case ePtlEvtCompleteEnd:          evtStr = "END"; break;
                case ePtlEvtDeadLineMiss:      evtStr = "DEADLINE_MISS"; break;
                case ePtlEvtOverrunSkip:       evtStr = "OVERRUN_SKIP"; break;
                case ePtlEvtOverrunKill:       evtStr = "OVERRUN_KILL"; break;
                case ePtlEvtOverrunCatchUp:    evtStr = "OVERRUN_CATCHUP"; break;

                case ePtlEvtServerReplenish:   evtStr = "SRV_REPLENISH"; break;
                case ePtlEvtServerExhausted:   evtStr = "SRV_EMPTY"; break;
                case ePtlEvtAperiodicEnqueue:  evtStr = "AP_ENQUEUE"; break;
                case ePtlEvtAperiodicStart:    evtStr = "AP_START"; break;
                case ePtlEvtAperiodicDone:     evtStr = "AP_DONE"; break;
                case ePtlEvtAperiodicDrop:     evtStr = "AP_DROP"; break;
                case ePtlEvtAperiodicOverrun:  evtStr = "AP_OVERRUN"; break;
                case ePtlEvtAperiodicKill:     evtStr = "AP_KILL"; break;
            }

            UART_printf("[");
            
            if (rec->xTimestamp < 100000) UART_printf(" ");
            if (rec->xTimestamp < 10000)  UART_printf(" ");
            if (rec->xTimestamp < 1000)   UART_printf(" ");
            if (rec->xTimestamp < 100)    UART_printf(" ");
            if (rec->xTimestamp < 10)     UART_printf(" ");
            
            UART_print_uint32((uint32_t)rec->xTimestamp);
            UART_printf("]   "); 

            UART_printf("T");
            UART_print_uint32((uint32_t)rec->ucTaskId);
            
            if (rec->ucTaskId < 10) {
                 UART_printf("     "); 
            } else {
                 UART_printf("    ");  
            }

            UART_print_uint32(rec->ulJobSeq);

            if (rec->ulJobSeq < 10) {
                UART_printf("       ");
            } else if (rec->ulJobSeq < 100) {
                UART_printf("      ");
            } else if (rec->ulJobSeq < 1000) {
                UART_printf("     ");
            } else {
                UART_printf("    ");
            }

            UART_printf(evtStr);
            UART_printf("\n");
        }

        UART_printf("================ END TRACE ================\n\n");
    }

    /* ptl_log.c */

    void vPtlStatsDump(void){
        uint8_t i;

        UART_printf("\n===== PTL TASKS STATISTICS =====\n");

        for (i = 0; i < pxSys->ucNTasks; i++) {
            PtlTask_t *t = &pxSys->pxTasks[i];
            PtlTaskStats_t *s = &t->xTaskStats;

            UART_printf("\nTask T");
            UART_print_uint32((uint32_t)t->ucId);
            UART_printf(" (");
            UART_printf(t->pcName); 
            UART_printf(")\n");

            UART_printf("  Jobs released   : ");
            UART_print_uint32(s->ulTotalJobsReleased);
            UART_printf("\n");
                    
            UART_printf("  Deadline misses : ");
            UART_print_uint32(s->ulDeadlineMisses);
            UART_printf("\n");

            UART_printf("  Overruns        : ");
            UART_print_uint32(s->ulOverruns);
            UART_printf("\n");

            UART_printf("  WCET (ticks)    : ");
            UART_print_uint32((uint32_t)s->xMaxExecutionTime);
            UART_printf("\n");

            if (s->ulTotalJobsReleased > 1) {
                UART_printf("  Avg Exec Time   : ");
                UART_print_uint32((uint32_t)(s->ulSumExecutionTime / s->ulTotalJobsReleased));
                UART_printf("\n");
            }
            else{
                UART_printf("  Tot Exec Time   : ");
                UART_print_uint32((uint32_t)(s->ulSumExecutionTime));
                UART_printf("\n");
            }
        }

        /* ================================================================= */
        /* NEW: POLLING SERVER APERIODIC STATISTICS                          */
        /* ================================================================= */
        if (pxSys->pxPollingServer != NULL) {
            PtlPollingStats_t *apStats = &pxSys->pxPollingServer->pxAperiodicStats;
            
            UART_printf("\n===== POLLING SERVER APERIODIC STATS =====\n");
            
            UART_printf("  Total AP Releases  : ");
            UART_print_uint32(apStats->ulTotalJobsReleased);
            UART_printf("\n");

            UART_printf("  Total AP Executed  : ");
            UART_print_uint32(apStats->ulTotalJobsExecuted);
            UART_printf("\n");

            UART_printf("  AP Deadline Misses : ");
            UART_print_uint32(apStats->ulTotalDeadlineMisses);
            UART_printf("\n");

            UART_printf("  AP Jobs dropped    : ");
            UART_print_uint32(apStats->ulTotalJobsDropped);
            UART_printf("\n");

            UART_printf("  AP Jobs Killed     : ");
            UART_print_uint32(apStats->ulTotalJobsKilled);
            UART_printf("\n");

            UART_printf("  Total AP Exec Time : ");
            UART_print_uint32((uint32_t)apStats->xTotalAperiodicExecTime);
            UART_printf("\n");

            if (apStats->ulTotalJobsExecuted > 0) {
                UART_printf("  Avg AP Job Cost    : ");
                UART_print_uint32((uint32_t)(apStats->xTotalAperiodicExecTime / apStats->ulTotalJobsExecuted));
                UART_printf("\n");
            }
        }

        /* ================================================================= */
        /* GLOBAL STATISTICS                                                 */
        /* ================================================================= */
        UART_printf("\n===== PTL GLOBAL STATISTICS =====\n");

        UART_printf("  TotaljobsExecTime: ");
        uint32_t ulTotalTime = pxSys->xSimDuration;
        /* Note: Ensure xIdleTime is being updated correctly in task.c hooks */
        uint32_t ulBusyTime  = ulTotalTime - pxSys->xGlobalStats.xIdleTime;
        UART_print_uint32(ulBusyTime);
        UART_printf("\n");

        UART_printf("  Total CpuLoad    : ");
        float fCpuLoad = ((float)ulBusyTime * 100.0f) / (float)ulTotalTime;
        UART_print_float(fCpuLoad, 3);
        UART_printf("\n");


        UART_printf("\n===== END STATS =====\n");
    }

#endif /*(configUSE_PTL_SYSTEM == 1)*/