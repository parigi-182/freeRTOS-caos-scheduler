#include "FreeRTOS.h"
#include "task.h"
#include "ptl.h"
#include "ptl_log.h"
#include <stdlib.h>
#include <stdio.h>
#include "uart.h"
#include "timers.h"   

/* ========================================================================== */
/* CONFIGURATION SELECTOR                                                     */
/* ========================================================================== */

/* EXISTING TESTS */
#define TEST_HEALTHY            1   /* U=88%. Standard Rate Monotonic. Safe. */
#define TEST_QUEUE_FLOOD        2   /* Bursts > Server Capacity. Tests Queue. */
#define TEST_OVERRUN_POLICY     3   /* Job > Budget. Tests policing. */
#define TEST_SERVER_STARVATION  4   /* Server Low Prio. Tests Preemption. */
#define TEST_TOTAL_MELTDOWN     5   /* U=140%. Deterministic failure. */
#define TEST_STRESS_BOUNDARY    6   /* U=96%. Edge of stability. */

/* NEW TESTS */
/* 7. TEST_RR_DANGER:
 * Demonstrates the weakness of Round Robin. 
 * Even with Low Load (50%), a short-deadline task will miss 
 * because it shares priority with a long-running task.
 */
#define TEST_RR_DANGER          7   

/* 8. TEST_CONTEXT_SWITCH_HELL:
 * Many tiny tasks. High Overhead.
 * Mathematically U=60% (Safe), but the sheer number of switches
 * might cause overhead failures if the scheduler is slow.
 */
#define TEST_CONTEXT_SWITCH_HELL 8

/* 9. TEST_SERVER_AGGRESSION:
 * Server is Highest Priority and runs frequently (Period 5).
 * It constantly interrupts a Heavy Periodic Task.
 * Tests if Context Switching latency breaks the periodic task.
 */
#define TEST_SERVER_AGGRESSION   9

#define TEST_EFFICIENCY_LIMIT   10

/*11. test made ad hoc for the written comparison*/
#define TEST_WRITTEN            11

/* ========================================================================== */
/* SIMULATION TIME                                                            */
/* ========================================================================== */
#if(DEFINEWRITTEN == 1)
    #define SIMULATION_TIME 50
#else 
    #define SIMULATION_TIME 1000
#endif
/* ========================================================================== */
/* GLOBAL VARIABLES                                                               */
/* ========================================================================== */

volatile uint32_t g_ulLoopsPerTick = 225000; 

/* Standard RM Priorities (Higher ID = Higher Priority) */
#define PRIO_SERVER     ( tskIDLE_PRIORITY + 1 ) 
#define PRIO_HIGH_RATE  ( tskIDLE_PRIORITY + 4 ) 
#define PRIO_MID_RATE   ( tskIDLE_PRIORITY + 3 ) 
#define PRIO_LOW_RATE   ( tskIDLE_PRIORITY + 2 ) 

/* ========================================================================== */
/* PROTOTYPES & HELPERS                                                       */
/* ========================================================================== */
void vBurnCPU(uint32_t ulTicksToBurn);
void vPeriodicTask(void *pvParameters);      
void vAperiodicWorkload(void *pvParameters); 
void vAperiodicGenerator(void *pvParameters);


void vAperiodicTimerCallback(TimerHandle_t xTimer) {
    TickType_t xNextWakeTime = pdMS_TO_TICKS(10); // Default safe fallback

    /* ---------------------------------------------------------------------- */
    /* 1. HEALTHY: Burst of 10 tasks, then long recovery sleep                 */
    /* ---------------------------------------------------------------------- */
    #if configUse == TEST_HEALTHY
        /* Burst: Spawn 10 tasks instantly */
        for(int i = 0; i < 10; i++) {
            xPtlAddAperiodicTask(vAperiodicWorkload, (void*)2, 500, eAperiodicKill);
        }
        xNextWakeTime = 150; 

    /* ---------------------------------------------------------------------- */
    /* 2. QUEUE FLOOD: Mini-burst of 5 tasks                                  */
    /* ---------------------------------------------------------------------- */
    #elif configUse == TEST_QUEUE_FLOOD
        for(int i = 0; i < 5; i++) {
            xPtlAddAperiodicTask(vAperiodicWorkload, (void*)2, 100, eAperiodicKill);
        }
        xNextWakeTime = pdMS_TO_TICKS(5 + (rand() % 10));

    /* ---------------------------------------------------------------------- */
    /* 3. OVERRUN POLICY: Toggle between Kill and Overrun types               */
    /* ---------------------------------------------------------------------- */
    #elif configUse == TEST_OVERRUN_POLICY
        static int mode = 0;
        mode = !mode; 
        
        /* * Budget is 5. Task Cost is 8.
         * Case 0 (Kill): Server runs 5 ticks, stops. Task dies. Witness survives.
         * Case 1 (Overrun): Server runs 8 ticks (violating limit). Witness might suffer.
         */
        xPtlAddAperiodicTask(vAperiodicWorkload, (void*)8, 10, mode ? eAperiodicOverrun : eAperiodicKill);
        xPtlAddAperiodicTask(vAperiodicWorkload, (void*)8, 20, mode ? eAperiodicOverrun : eAperiodicKill);
        xPtlAddAperiodicTask(vAperiodicWorkload, (void*)8, 40, mode ? eAperiodicOverrun : eAperiodicKill);
        /* Give enough time for the system to recover before next test */
        xNextWakeTime = 150;

    /* ---------------------------------------------------------------------- */
    /* 4 & 5. STARVATION / MELTDOWN: High load injection                      */
    /* ---------------------------------------------------------------------- */
    #elif configUse == TEST_SERVER_STARVATION || configUse == TEST_TOTAL_MELTDOWN
        xPtlAddAperiodicTask(vAperiodicWorkload, (void*)5, 20, eAperiodicKill);
        xNextWakeTime = pdMS_TO_TICKS(5 + (rand() % 10));

    /* ---------------------------------------------------------------------- */
    /* 6. STRESS: Matches server generation rate                              */
    /* ---------------------------------------------------------------------- */
    #elif configUse == TEST_STRESS_BOUNDARY
        xPtlAddAperiodicTask(vAperiodicWorkload, (void*)2, 20, eAperiodicKill);
        xNextWakeTime = pdMS_TO_TICKS(5 + (rand() % 10));

    /* ---------------------------------------------------------------------- */
    /* 7. RR DANGER: Low load                                                 */
    /* ---------------------------------------------------------------------- */
    #elif configUse == TEST_RR_DANGER
        xPtlAddAperiodicTask(vAperiodicWorkload, (void*)1, 50, eAperiodicKill);
        xNextWakeTime = pdMS_TO_TICKS(5 + (rand() % 10));

    /* ---------------------------------------------------------------------- */
    /* 8. SWITCH HELL: Noise generation                                       */
    /* ---------------------------------------------------------------------- */
    #elif configUse == TEST_CONTEXT_SWITCH_HELL
        xPtlAddAperiodicTask(vAperiodicWorkload, (void*)1, 10, eAperiodicKill);
        xNextWakeTime = pdMS_TO_TICKS(5 + (rand() % 10));

    /* ---------------------------------------------------------------------- */
    /* 9. AGGRESSION: Keep server busy                                        */
    /* ---------------------------------------------------------------------- */
    #elif configUse == TEST_SERVER_AGGRESSION
        xPtlAddAperiodicTask(vAperiodicWorkload, (void*)1, 20, eAperiodicKill);
        xNextWakeTime = pdMS_TO_TICKS(5 + (rand() % 10));

    /* ---------------------------------------------------------------------- */
    /* 10. EFFICIENCY: Saturation                                             */
    /* ---------------------------------------------------------------------- */
    #elif configUse == TEST_EFFICIENCY_LIMIT
        xPtlAddAperiodicTask(vAperiodicWorkload, (void*)10, 50, eAperiodicKill);
        xNextWakeTime = pdMS_TO_TICKS(5 + (rand() % 10));

    /* ---------------------------------------------------------------------- */
    /* 11. WRITTEN: Standard Load                                             */
    /* ---------------------------------------------------------------------- */
    #elif configUse == TEST_WRITTEN
        xPtlAddAperiodicTask(vAperiodicWorkload, (void*)1, 50, eAperiodicKill);
        xNextWakeTime = pdMS_TO_TICKS(5 + (rand() % 10));

    #endif

    /* Re-arm the timer for the calculated next wake time. 
       This replaces vTaskDelay(). */
    if (xTimer != NULL) {
        xTimerChangePeriod(xTimer, xNextWakeTime, 0);
    }
}

/* ========================================================================== */
/* MAIN                                                                       */
/* ========================================================================== */
int main(int argc, char **argv){
    (void) argc; (void) argv;

    PtlTaskParams_t *pTasks = NULL;
    PtlPollingServerConfig_t serverConfig;
    uint8_t ucNTasks = 0;
    UBaseType_t uxGenPrio = PRIO_LOW_RATE;
    srand(10);

    /* ---------------------------------------------------------------------- */
    /* 1. HEALTHY (88% Load)                                                  */
    /* ---------------------------------------------------------------------- */
    #if configUse == TEST_HEALTHY
        /* In main.c - TEST_HEALTHY block */
        static PtlTaskParams_t tHealthy[] = { 
            /* 68% Periodic Load distributed evenly */
            { "T1", vPeriodicTask, (void *)4, BYTES_TO_WORDS(512), PRIO_MID_RATE, 50, 50 },
            { "T2", vPeriodicTask, (void *)4, BYTES_TO_WORDS(512), PRIO_MID_RATE, 50, 50 },
            { "T3", vPeriodicTask, (void *)5, BYTES_TO_WORDS(512), PRIO_MID_RATE, 50, 50 },
            { "T4", vPeriodicTask, (void *)5, BYTES_TO_WORDS(512), PRIO_MID_RATE, 50, 50 },
            { "T5", vPeriodicTask, (void *)9, BYTES_TO_WORDS(512), PRIO_MID_RATE, 50, 50 },
            { "T6", vPeriodicTask, (void *)9, BYTES_TO_WORDS(512), PRIO_MID_RATE, 50, 50 }
        };
        pTasks = tHealthy; ucNTasks = 6;
        serverConfig.xPeriod = 10; serverConfig.xMaxBudget = 2; serverConfig.uxPriority = PRIO_SERVER; 
        serverConfig.ulStackDepth = BYTES_TO_WORDS(512); serverConfig.ulNAperiodicTasks = 5;

    /* ---------------------------------------------------------------------- */
    /* 2. QUEUE FLOOD (Burst Test)                                            */
    /* ---------------------------------------------------------------------- */
    #elif configUse == TEST_QUEUE_FLOOD
        /* 1. Witness Task: High Priority, Short Period.
         * If the Server "Overruns" (violates budget), this task will MISS.
         * It serves as the "Canary in the coal mine". */
        static PtlTaskParams_t tPolice[] = {
            { "Witness", vPeriodicTask, (void *)2, BYTES_TO_WORDS(512), PRIO_HIGH_RATE, 10, 10 }
        };
        pTasks = tPolice; ucNTasks = 1;
        
        /* 2. Server: Period 50, Budget 5. */
        serverConfig.xPeriod = 50; serverConfig.xMaxBudget = 5; serverConfig.uxPriority = PRIO_SERVER; 
        serverConfig.ulStackDepth = BYTES_TO_WORDS(512); serverConfig.ulNAperiodicTasks = 10;

   /* ---------------------------------------------------------------------- */
    /* 3. OVERRUN POLICY                                                      */
    /* ---------------------------------------------------------------------- */
    #elif configUse == TEST_OVERRUN_POLICY
        /* 1. Witness Task: High Priority, Short Period.
         * We give it less slack so an overrun actually hurts it. */
        static PtlTaskParams_t tPolice[] = {
            /* Cost 2, Period 10. If Server steals > 8 ticks, this misses. */
            { "Witness", vPeriodicTask, (void *)2, BYTES_TO_WORDS(512), PRIO_MID_RATE, 10, 10 }
        };
        pTasks = tPolice; 
        ucNTasks = 1;
        
        /* 2. Server: Period 50, Budget 5.
         * CRITICAL CHANGE: Priority must be HIGHER than Witness (PRIO_HIGH_RATE)
         * to prove it can "bully" the system during an overrun. */
        serverConfig.xPeriod = 50; 
        serverConfig.xMaxBudget = 5; 
        serverConfig.uxPriority = PRIO_HIGH_RATE; // <--- MUST BE HIGHER THAN PRIO_MID_RATE
        serverConfig.ulStackDepth = BYTES_TO_WORDS(512); 
        serverConfig.ulNAperiodicTasks = 5;
    /* ---------------------------------------------------------------------- */
    /* 4. SERVER STARVATION                                                   */
    /* ---------------------------------------------------------------------- */
    #elif configUse == TEST_SERVER_STARVATION
        static PtlTaskParams_t tStarve[] = { 
            /* U = 105% */
            { "T1", vPeriodicTask, (void *)35, BYTES_TO_WORDS(512), PRIO_HIGH_RATE, 100, 100 },
            { "T2", vPeriodicTask, (void *)35, BYTES_TO_WORDS(512), PRIO_HIGH_RATE, 100, 100 },
            { "T3", vPeriodicTask, (void *)35, BYTES_TO_WORDS(512), PRIO_HIGH_RATE, 100, 100 }
        };
        pTasks = tStarve; ucNTasks = 3;
        serverConfig.xPeriod = 20; serverConfig.xMaxBudget = 2; serverConfig.uxPriority = PRIO_LOW_RATE;
        serverConfig.ulStackDepth = BYTES_TO_WORDS(512); serverConfig.ulNAperiodicTasks = 5;

    /* ---------------------------------------------------------------------- */
    /* 5. MELTDOWN                                                            */
    /* ---------------------------------------------------------------------- */
    #elif configUse == TEST_TOTAL_MELTDOWN
        static PtlTaskParams_t tMelt[] = { 
            /* U = 120% */
            { "T1", vPeriodicTask, (void *)30, BYTES_TO_WORDS(512), PRIO_LOW_RATE, 100, 100 },
            { "T2", vPeriodicTask, (void *)30, BYTES_TO_WORDS(512), PRIO_LOW_RATE, 100, 100 },
            { "T3", vPeriodicTask, (void *)30, BYTES_TO_WORDS(512), PRIO_LOW_RATE, 100, 100 },
            { "T4", vPeriodicTask, (void *)30, BYTES_TO_WORDS(512), PRIO_LOW_RATE, 100, 100 }
        };
        pTasks = tMelt; ucNTasks = 4;
        serverConfig.xPeriod = 10; serverConfig.xMaxBudget = 2; serverConfig.uxPriority = PRIO_SERVER; 
        serverConfig.ulStackDepth = BYTES_TO_WORDS(512); serverConfig.ulNAperiodicTasks = 5;

    /* ---------------------------------------------------------------------- */
    /* 6. STRESS BOUNDARY (96% Load)z                                          */
    /* ---------------------------------------------------------------------- */
    #elif configUse == TEST_STRESS_BOUNDARY
        static PtlTaskParams_t tStress[] = { 
            { "T1", vPeriodicTask, (void *)2,  BYTES_TO_WORDS(512), PRIO_HIGH_RATE, 20, 20 },
            { "T2", vPeriodicTask, (void *)2,  BYTES_TO_WORDS(512), PRIO_HIGH_RATE, 20, 20 },
            { "T3", vPeriodicTask, (void *)4,  BYTES_TO_WORDS(512), PRIO_MID_RATE, 50, 50 },
            { "T4", vPeriodicTask, (void *)4,  BYTES_TO_WORDS(512), PRIO_MID_RATE, 50, 50 },
            { "T5", vPeriodicTask, (void *)20, BYTES_TO_WORDS(512), PRIO_LOW_RATE, 100, 100 },
            { "T6", vPeriodicTask, (void *)20, BYTES_TO_WORDS(512), PRIO_LOW_RATE, 100, 100 }
        };
        pTasks = tStress; ucNTasks = 6;
        serverConfig.xPeriod = 10; serverConfig.xMaxBudget = 2; serverConfig.uxPriority = PRIO_MID_RATE; 
        serverConfig.ulStackDepth = BYTES_TO_WORDS(512); serverConfig.ulNAperiodicTasks = 5;

    /* ---------------------------------------------------------------------- */
    /* 7. RR DANGER (Round Robin Failure)                                     */
    /* Intent: 2 Tasks share priority.                                        */
    /* T1: Period 20, Cost 5 (25% U)                                          */
    /* T2: Period 100, Cost 40 (40% U)                                        */
    /* Total U = 65% (Mathematically Safe).                                   */
    /* BUT: If T2 runs first and uses its 40 ticks, T1 misses its 20 deadline.*/
    /* ---------------------------------------------------------------------- */
    #elif configUse == TEST_RR_DANGER
        static PtlTaskParams_t tRR[] = { 
            /* Both are PRIO_MID_RATE. T2 blocks T1 in RR. */
            { "T_Fast", vPeriodicTask, (void *)5,  BYTES_TO_WORDS(512), PRIO_MID_RATE, 20, 20 },
            { "T_Slow", vPeriodicTask, (void *)40, BYTES_TO_WORDS(512), PRIO_MID_RATE, 100, 100 }
        };
        pTasks = tRR; ucNTasks = 2;
        
        serverConfig.xPeriod = 50; serverConfig.xMaxBudget = 2; serverConfig.uxPriority = PRIO_MID_RATE; 
        serverConfig.ulStackDepth = BYTES_TO_WORDS(512); serverConfig.ulNAperiodicTasks = 5;

    /* ---------------------------------------------------------------------- */
    /* 8. CONTEXT SWITCH HELL                                                 */
    /* Intent: 10 Tasks. Tiny costs. High frequency switching.                */
    /* Tests if Scheduler Overhead causes misses despite low mathematical U.    */
    /* ---------------------------------------------------------------------- */
    #elif configUse == TEST_CONTEXT_SWITCH_HELL
        /* 10 Tasks, Cost 1, Period 20. Total U = 10/20 = 50%. Very Safe. */
        static PtlTaskParams_t tHell[10];
        static char names[10][4];
        for(int i=0; i<10; i++) {
            sprintf(names[i], "T%d", i);
            tHell[i].pcName = names[i];
            tHell[i].pxEntry = vPeriodicTask;
            tHell[i].pvParameters = (void*)1; /* 1 Tick cost */
            tHell[i].ulStackDepth = BYTES_TO_WORDS(256);
            tHell[i].uxPriority = PRIO_MID_RATE; /* All RR */
            tHell[i].xPeriod = 20;
            tHell[i].xDeadline = 20;
        }
        pTasks = tHell; ucNTasks = 10;
        
        serverConfig.xPeriod = 10; serverConfig.xMaxBudget = 1; serverConfig.uxPriority = PRIO_MID_RATE; 
        serverConfig.ulStackDepth = BYTES_TO_WORDS(512); serverConfig.ulNAperiodicTasks = 5;

    /* ---------------------------------------------------------------------- */
    /* 9. SERVER AGGRESSION                                                   */
    /* Intent: Server (High Prio) period is very short (5ms).                 */
    /* It will interrupt the Periodic Task (Cost 45, Period 50) 10 times.     */
    /* Periodic U = 90%. Server U = 20%. Total = 110%.                        */
    /* Expectation: Periodic Task T1 fails because Server steals budget.      */
    /* ---------------------------------------------------------------------- */
    #elif configUse == TEST_SERVER_AGGRESSION
        static PtlTaskParams_t tAggr[] = { 
            { "T1", vPeriodicTask, (void *)45, BYTES_TO_WORDS(512), PRIO_MID_RATE, 50, 50 }
        };
        pTasks = tAggr; ucNTasks = 1;

        /* Server: Period 5, Budget 1. U = 20%. Runs very often. */
        serverConfig.xPeriod = 5; 
        serverConfig.xMaxBudget = 1; 
        serverConfig.uxPriority = PRIO_SERVER; 
        serverConfig.ulStackDepth = BYTES_TO_WORDS(512); serverConfig.ulNAperiodicTasks = 5;

    /* ---------------------------------------------------------------------- */
        /* 10. EFFICIENCY LIMIT (98% Load)                                        */
        /* */
        /* Intent: A perfect harmonic fit. Total utilization is 98%.              */
        /* We leave exactly 2 ticks of idle time per 100 ticks.                   */
        /* If scheduler overhead > 2%, T_Canary (Lowest Prio) will fail.          */
        /* ---------------------------------------------------------------------- */
        #elif configUse == TEST_EFFICIENCY_LIMIT
        static PtlTaskParams_t tEff[] = {
            /* P=20, C=4. Runs 5x per 100. (20% Load) */
            { "T_High", vPeriodicTask, (void *)4,  BYTES_TO_WORDS(512), PRIO_HIGH_RATE, 20, 20 },
            
            /* P=50, C=10. Runs 2x per 100. (20% Load) */
            { "T_Mid",  vPeriodicTask, (void *)10, BYTES_TO_WORDS(512), PRIO_MID_RATE,  50, 50 },
            
            /* P=100, C=38. Runs 1x per 100. (38% Load) */
            /* This is the Canary task. It gets the scraps. */
            { "T_Low",  vPeriodicTask, (void *)38, BYTES_TO_WORDS(512), PRIO_LOW_RATE,  100, 100 }
        };
        pTasks = tEff; ucNTasks = 3;

        /* Server: Period 10, Budget 2. Runs 10x per 100. (20% Load) */
        serverConfig.xPeriod = 10; 
        serverConfig.xMaxBudget = 2; 
        serverConfig.uxPriority = PRIO_MID_RATE; 
        serverConfig.ulStackDepth = BYTES_TO_WORDS(512); 
        serverConfig.ulNAperiodicTasks = 5;
/* ---------------------------------------------------------------------- */
    /* 11. Test for comparison                                            */
    /* ---------------------------------------------------------------------- */
  
        #elif configUse == TEST_WRITTEN
            static PtlTaskParams_t tStress[] = { 
                { "T1", vPeriodicTask, (void *)2,  BYTES_TO_WORDS(512), PRIO_HIGH_RATE, 10, 10 },
                { "T2", vPeriodicTask, (void *)3,  BYTES_TO_WORDS(512), PRIO_HIGH_RATE, 20, 20 },
                { "T3", vPeriodicTask, (void *)2,  BYTES_TO_WORDS(512), PRIO_MID_RATE, 20, 20 },
                { "T4", vPeriodicTask, (void *)10,  BYTES_TO_WORDS(512), PRIO_MID_RATE, 50, 50 },
            };
            pTasks = tStress; ucNTasks = 4;
            serverConfig.xPeriod = 10; serverConfig.xMaxBudget = 2; serverConfig.uxPriority = PRIO_SERVER; 
            serverConfig.ulStackDepth = BYTES_TO_WORDS(512); serverConfig.ulNAperiodicTasks = 5;
        #endif

        
        /* ---------------------------------------------------------------------- */
        /* INITIALIZATION                                                         */
        /* ---------------------------------------------------------------------- */
        /* ---------------------------------------------------------------------- */
    /* INITIALIZATION                                                         */
    /* ---------------------------------------------------------------------- */
    PtlConfig_t conf = {
        .ucNTasks = ucNTasks,
        .pxTasks = pTasks,
        .xSimDuration = SIMULATION_TIME,
        .eGlobalPolicy = ePtlPolicyKill,
        .pxPollingServerConfig = &serverConfig
    };

        /* Initialize PTL */
        vPtlInit(&conf);

        /* Create the Software Timer */
        TimerHandle_t xAperiodicTimer = xTimerCreate(
            "AperiodicGen",                 
            pdMS_TO_TICKS(10),              /* Initial start delay */
            pdFALSE,                        /* Auto-reload FALSE: We manually reset it in callback */
            NULL,                           
            vAperiodicTimerCallback         
        );

        if (xAperiodicTimer != NULL) {
            xTimerStart(xAperiodicTimer, 0); 
        }

        /* Start Scheduler */
        vPtlStart();
        
        /* Should not reach here */
        for( ; ; );
    }

void vAperiodicWorkload(void *pvParameters) {
    vBurnCPU((uint32_t)pvParameters);
}

void vPeriodicTask(void * pvParameters){
    vBurnCPU((uint32_t)pvParameters);
}

void vBurnCPU(uint32_t ulTicksToBurn) {
    uint64_t ulTotalLoops = (uint64_t)g_ulLoopsPerTick * (uint64_t)ulTicksToBurn;
    for(volatile uint64_t i = 0; i < ulTotalLoops; i++) {
        __asm volatile("nop");
    }
}