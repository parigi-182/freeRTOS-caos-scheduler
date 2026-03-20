#ifndef __PTL_PI__
    #define __PTL_PI__
    #include "ptl.h"
    #if (configUSE_PTL_SYSTEM == 1)
        #include "queue.h"

        /*==========================================================================================*/
        /*                                   Constants&Macros                                       */
        /*==========================================================================================*/

        #define PTL_MAX_TASKS 10
        #define MAX_PTL_NAME_LENGTH 20
        #define PTL_TASK_SWITCHED_IN(void)

        #define POLLING_SERVER_ID       0
        #define IS_POLLING_SERVER(pxPtlTask) ( (pxPtlTask)->ucId == POLLING_SERVER_ID )

        /*==========================================================================================*/
        /*                                  Statistics                                              */
        /*==========================================================================================*/

        typedef struct xPOLLING_GLOBAL_STATISTICS{
            uint32_t    ulTotalJobsReleased;
            uint32_t    ulTotalJobsExecuted;
            uint32_t    ulTotalDeadlineMisses;
            uint32_t    ulTotalJobsKilled;
            uint32_t    ulTotalJobsDropped;
            TickType_t  xTotalAperiodicExecTime; 
            TickType_t  xCurrentJobStartTime;
        }PtlPollingStats_t;

        /**
         * @brief Runtime statistics for a single periodic task.
         * * This structure accumulates real-time metrics updated by the wrapper.
         * * **Fields:**
         * - **wakeTime**: Timestamp when the task transitioned from Ready -> Running.
         * - **finishTime**: Timestamp when the current job finished execution.
         * - **maxExecutionTime**: Worst-Case Execution Time (WCET) observed so far (in ticks).
         * - **sumExecutionTime**: Cumulative execution time (used to compute average utilization).
         * - **totalJobsReleased**: Total number of jobs released (activations).
         * - **deadlineMisses**: Counter for when FinishTime > ReleaseTime + Deadline.
         * - **overruns**: Counter for when a new release arrived while the previous job was running.
         * - **policyExecutions**: Counter for how many times the overrun policy was triggered.
         */
        typedef struct xPTL_TASK_STATISTICS{
            /*Most Recent Jobs Statistics                                                            */
            TickType_t          xStartTime;            /* Timestamp of a task from ready -> running  */
            TickType_t          xFinishTime;          /* Timestamp of the finish time of a task      */
            /*All exec per task statistics                                                           */
            TickType_t          xMaxExecutionTime;    /* Worst-Case Execution Time (WCET) seen so far*/
            uint32_t            ulSumExecutionTime;    /* To compute CPU uf_k                         */
            uint32_t            ulTotalJobsReleased;   /* Total number of periodic activations        */
            uint32_t            ulDeadlineMisses;      /* How many times F_k > R_k + D                */
            uint32_t            ulOverruns;            /* How many times the task exceedes T          */
        }PtlTaskStats_t;

        /**
         * @brief Global System-level Statistics.
         * * **Fields:**
         * - **startTime**: Timestamp when the PTL simulation started (t0).
         * - **endTime**: Timestamp when the simulation ended.
         * - **idleTime**: Total time spent in the Idle task (requires Hook integration).
         */
        typedef struct xPTL_GLOBAL_STATISTICS{
            TickType_t          xStartTime;           /* Timestamp of the start of simulation        */
            TickType_t          xEndTime;             /* Timestamp of the start of simulation        */
            TickType_t          xIdleTime;            /* Time where idle task was running            */
        }PtlSystemStats_t;
        

        typedef struct xPTL_APERIODIC_STATISTICS {
            TickType_t      xArrivalTime;       /* Arrival Timestamp */
            TickType_t      xStartTime;         /* Execution Start Timestamp */
            TickType_t      xFinishTime;        /* Completion Timestamp */
        }PtlAperiodicStats_t;    



        /*==========================================================================================*/
        /*                                  RunTime structures                                      */
        /*==========================================================================================*/

        /**
         * @brief Runtime Control Block for a single Periodic Task (TCB).
         * * This structure holds the dynamic context of a task managed by the PTL.
         * * **Fields:**
         * - **name**: Human-readable name of the task.
         * - **entry**: Pointer to the user's task function (the job).
         * - **args**: User arguments passed to the task function.
         * - **id**: Internal Task ID (0 to N-1).
         * - **stackSize**: Stack depth in words.
         * - **priority**: FreeRTOS Priority level.
         * - **period**: The activation period (T) in system ticks.
         * - **deadline**: The relative deadline (D) in system ticks.
         * - **nextReleaseTime**: The calculated timestamp for the next job release (R_{k+1}).
         * - **jobFinished**: used to detet overrun if an instance of the same process arrives.
         * - **tHandler**: Native FreeRTOS Task Handle used to manage the task.
         * - **tStats**: Runtime statistics structure for this specific task.
         *
         */


        /*==========================================================================================*/
        /*                                  RunTime structures                                      */
        /*==========================================================================================*/

        /**
         * @brief Aperiodic Job Control Block (The Node).
         * @details Contains the execution context, scheduling parameters, and stats.
         * These nodes form the FIFO queue.
         */
        typedef struct xPTL_APERIODID_JOB {
            TaskFunction_t          pxTaskCode;         /* Function Pointer */
            void *                  pvParameters;       /* Function Args */
            
            /* Scheduling Parameters */
            TickType_t              xSoftDeadline;      /* Relative D (Config) */
            TickType_t              xAbsoluteDeadline;  /* Relative D (Config) */
            TickType_t              xArrivalTime;
            PtlAperiodicPolicy_t    ePolicy;            /* Kill vs Overrun */
            
            /* Stats Container */
            PtlAperiodicStats_t     xJobStats;          /* Statistics for this job */

        }PtlAperiodicTask_t;
        

        typedef struct xPTL_TASK_CONTROL_BLOCK {

            /*                                  Config variables                                    */
            char pcName[MAX_PTL_NAME_LENGTH];
            void                (*pxEntry)(void*);   /* Task body = entry                           */
            void* pvParameters;
            uint8_t             ucId;                /* Each task will have ID to be identified. 
                                                    The scheduler will manage its creation       */
            uint32_t            ulStackSize;         /* Amout of memory space occupied by the task  */
            UBaseType_t         uxPriority;          /* P                                           */
            /* TickType_t          xReleaseTime;           OPTIONAL : R -> when a task is supposed
                                                        to be deployed                              */          
            TickType_t          xPeriod;             /* T                                           */      
            TickType_t          xDeadline;           /* D                                           */


            TickType_t          xAbsoluteDeadline;   /* d_k = R_k + D. Calculated at every wake-up. */
            TickType_t          xNextArrivalTime;    /* R_{k+1} : R_{k} + T_{k}                     */ 
            /*--------------------------------------------------------------------------------------*/

            volatile BaseType_t xJobFinished;        /* pdTRUE: finished  pdFALSE: still running    */
            volatile BaseType_t xJobKilled;          /* pdTRUE: finished  pdFALSE: still running    */

            TickType_t          xLastSwitchedInTime; /* Context Switching back to this task         */
            volatile TickType_t xCurrentExecTime;    /* For how long the task has been executing    */


            PtlTaskStats_t      xTaskStats;          /* Statistic per task structure                */


            TaskHandle_t        xTaskHandlePtl;      /* Handler to remotely manage the task         */
        } PtlTask_t;

        /**
         * @brief Runtime Control Block for the Polling Server.
         * * **Fields:**
         * - **xMaxBudget**: The maximum execution capacity ($Q_s$) replenished at the start of every period ($T_s$).
         * - **xCurrentBudget**: The dynamic remaining budget available for the current period.
         * - **pxJobQueueHead**: Pointer to the head of the FIFO queue (next aperiodic job to execute).
         * - **pxJobQueueTail**: Pointer to the tail of the FIFO queue (where new jobs are enqueued).
         * - **ulPendingJobs**: Counter of currently active aperiodic requests in the queue.
         * - **pxPtlPollingTCB**: Pointer to the underlying Periodic Task wrapper that manages the server's scheduling.
         */

        typedef struct xPTL_POLLING_SERVER {
            /* Configuration Copy */
            TickType_t              xMaxBudget;         /* Q_server */
            
            /* Dynamic Status */
            volatile TickType_t     xCurrentBudget;     /* Current Q remaining */
            QueueHandle_t           xJobQueue;          
            volatile uint32_t       ulPendingJobs;      /* Count */

            PtlTask_t *             pxPtlPollingTCB;

            PtlPollingStats_t       pxAperiodicStats;

            PtlAperiodicTask_t      pxCurrentAperiodicTask;
            BaseType_t              xAperiodicRunning;
        }PtlPollingServer_t;


        /**
         * @brief Global Runtime Manager for the Periodic Task Layer.
         * * This singleton structure maintains the state of the entire PTL system.
         * * **Fields:**
         * - **currId**: Initialization counter used during creation.
         * - **nTasks**: Total number of periodic tasks configured.
         * - **tasks**: Dynamic array of Task Control Blocks (allocated in heap).
         * - **simDuration**: Simulation duration in system ticks.
         * - **globalPolicy**: Default Overrun Policy (SKIP, KILL, CATCH_UP).
         * - **globalStats**: System-wide statistics.
         */
        typedef struct xPTL_RUNTIME_STRUCTURE {
            uint8_t             ucCurrId;            /* Current running task                        */
            uint8_t             ucNTasks;            /* Num of tasks                                */
            /* uint8_t             ucMaxTasks;              Max tasks                               */
            PtlTask_t*          pxTasks;             /* Vector of tasks                             */
            TickType_t          xSimDuration;        /* Time of simulation execution in ms          */
            PtlOverrunPolicy_t  xGlobalPolicy;       /* Default policy for all tasks                */
            PtlSystemStats_t    xGlobalStats;        /* Pointer to global Ptl stats of the sim      */
            PtlPollingServer_t *pxPollingServer;     	
        } Ptl_t;


        extern Ptl_t *pxSys;

        /*==========================================================================================*/
        /*                                  Hidden Functions                                        */
        /*==========================================================================================*/



        /**
         * @brief create a wrapper for the entry task (body of the job)
         *
         * @param[in] void* args of the function
         * 
         * @pre  xPtlTaskCreate must be used by the user with all the data of the task
         * @post The wrapper will have the logic of task (that is a fuction)
         */ 
        void vPtlTaskWrapper(void*);

        /**
         * @brief Simple task that stops the simulation after a given time
         * 
         * @param[in] pvParameters  duration time of the simulation    
         * @pre  Ptl is being initialized with vPtlInit function and scheduler is being run
         * @post PtlExit() function is called after a fixed period of time
         */ 
        void vPtlDurationMonitor(void *pvParameters);

        /**
         * @brief deletes all the structures characterizing the Ptl
         *
         * @pre Ptl is being initialized with vPtlInit function
         * @post ptl is completely deallocated
         */
        void vPtlExit();


        /**
         * @brief create a FreeRTOS task that is also periodic 
         *
         * @param[in] char_ptr      name of the task
         * @param[in] entry         body of the task to be created    
         * @param[in] args          arguments passed to the function     
         * @param[in] uint32_t      StackSize to be dedicated to the task    
         * @param[in] UBaseType_t   priority    
         * @param[in] TickType_t    period    
         * @param[in] TickType_t    deadline 
         * @param[in] BaseType_t    pollingServer
         * 
         *    
         * @pre  Ptl is being initialized with vPtlInit function
         * @post Scheduler will run with the provided configuration
                 periodicity or deadlines
        */ 
        BaseType_t xPtlTaskCreate(
        char*, 
        void(*)(void*), 
        void*, 
        uint32_t, 
        UBaseType_t,                                                                                                                                                                                                                                                                                   
        TickType_t, 
        TickType_t,
        BaseType_t
        );
        

        void vPtlUpdateStats(PtlTask_t *);

        struct PtlPollingServer {
        TickType_t xMaxBudget;   /* Polling server capacity (Q) */	
        };

        #endif /* (configUSE_PTL_SYSTEM == 1)*/

#endif /* PTL_PI */


/*
 * FreeRTOS Kernel V10.x.x
 * Copyright (C) 2021 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, ... [Copy the rest of the text]
 *
 * http://www.FreeRTOS.org
 * http://aws.amazon.com/freertos
 *
 */
