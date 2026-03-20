#ifndef __PTL__ 
    #define __PTL__
    #include "FreeRTOS.h"
    #include "task.h"

    #if (configUSE_PTL_SYSTEM == 1)
        #include <stdint.h>

        /*==========================================================================================*/
        /*                                   UserConfigs                                            */
        /*==========================================================================================*/

        /**
         * @brief Deadline: On JobComplete(), compare Fₖ vs Rₖ + D. If Fₖ > Rₖ + D → DEADLINE_MISS.
         * @details
         *   Period: At Rₖ₊₁, if previous job isn’t complete → OVERRUN. 
         *   Apply policy (SKIP/KILL/CATCH_UP), log action.
         * - **POLICY_SKIP**: do not release job k+1 at Rₖ+1 ms; continue running job k; next release at Rₖ+1 + Tₖ ms; log OVERRUN + SKIP.
         * - **POLICY_KILL**: at Rₖ+1 ms the PTL stops job k, releases job k+1 immediately; log OVERRUN + KILL..
         * - **POLICY_CATCH_UP**: At Rₖ+1 ms release job k+1 immediately, mark job k as missed, maintain cadence; log OVERRUN + CATCH_UP..
         */
        typedef enum {
            ePtlPolicySkip = 0,    
            ePtlPolicyKill,
            ePtlPolicyCatchUp
        }PtlOverrunPolicy_t;         

         /**
         * @brief Policies for handling Aperiodic Task Deadline Misses.
         * @details
         * - **eAperiodicOverrun**: Allow the task to continue execution even if it 
         * misses the soft deadline. The miss is logged, but the job finishes.
         * - **eAperiodicKill**: Immediately terminate the aperiodic task upon a 
         * deadline overrun and proceed to the next one in the queue.
         */
        typedef enum {
            eAperiodicOverrun = 0,
            eAperiodicKill
        } PtlAperiodicPolicy_t;

        
        /**
         * @brief Struct containing the configuration parameters for a single periodic task.
         * * This structure is filled by the user to define the static properties of a task.
         * * **Fields:**
         * - **pcName**: Human-readable name for the task (used for debug/trace).
         * - **pxEntry**: Pointer to the function implementing the task body.
         * - **pvParameters**: Pointer to arguments passed to the task function (NULL if unused).
         * - **ulStackDepth**: The stack size allocated for the task (in words).
         * - **uxPriority**: The FreeRTOS priority (higher numbers = higher priority).
         * - **xPeriod**: The activation period (T) in system ticks.
         * - **xDeadline**: The relative deadline (D) in system ticks. If 0, D=T.
         */
        typedef struct {
            const char* pcName;          /* 'pc' = pointer to char */
            TaskFunction_t pxEntry;      /* 'px' = pointer to function (Standard FreeRTOS typedef) */
            void* pvParameters;          /* 'pv' = pointer to void */
            uint32_t ulStackDepth;       /* 'ul' = unsigned long */
            UBaseType_t uxPriority;      /* 'ux' = unsigned base type */
            TickType_t xPeriod;          /* 'x' = complex structure/typedef */
            TickType_t xDeadline;
        } PtlTaskParams_t;

             /**
         * @brief Configuration parameters for the Polling Server.
         * @details The user populates this structure in main.c to define the properties
         * of the Polling Server.
         * * * **Fields:**
         * - **xPeriod**:       The replenishment period (T_server) in ticks.
         * - **xMaxBudget**:    The maximum execution time (Q) allowed per period.
         * - **uxPriority**:    The priority of the server (Ideally highest in the system).
         * - **ulStackDepth**:  Stack size for the server task (must accommodate the aperiodic jobs).
         */
        typedef struct {
            TickType_t      xPeriod;        /* T_server */
            TickType_t      xMaxBudget;     /* Q_server (Capacity) */
            UBaseType_t     uxPriority;     /* P_server */
            uint32_t        ulStackDepth;   /* Stack size in words */
            uint8_t         ulNAperiodicTasks;
        } PtlPollingServerConfig_t;
        
        /**
         * @brief Master configuration structure for the Periodic Task Layer.
         * * This struct aggregates all system-wide settings and the list of user tasks.
         * It is passed to vPtlInit() to bootstrap the scheduler.
         * * **Fields:**,
         * - **ucNTasks**: The number of tasks in the `tasks` array.
         * - **pxTasks**: Pointer to the array of task blueprints defined by the user.
         * - **xSimDuration**: Duration of the simulation in system ticks (system stops or reports stats after this).
         * - **eGlobalPolicy**: The default overrun policy applied to tasks that do not specify a custom one.
         */
        typedef struct PtlConfigStructure{
            uint8_t ucNTasks;
            const PtlTaskParams_t * pxTasks; /* Pointer to the array above */
            TickType_t xSimDuration;
            PtlOverrunPolicy_t eGlobalPolicy;
            PtlPollingServerConfig_t * pxPollingServerConfig;
        }PtlConfig_t;


        /*==========================================================================================*/
        /*                                   Constants&Macros                                       */
        /*==========================================================================================*/    
        
        #define NO_DEADLINE  ((TickType_t) 0)       /* Macro used to manage the defeault behaviour 
                                                    of Deadline = 0                              */
        #define BYTES_TO_WORDS( x ) ( ( x ) / sizeof( StackType_t ) )

        /*==========================================================================================*/
        /*                                   Functions                                              */
        /*==========================================================================================*/    
        /**
         * @brief Initializes the Ptl parameters
         *
         * @details
         * Initialized the structures: PtlConfig_t, PtlTaskStats_t and PtlSystemStats_t and initializes
         * the overrun policy.
         * 
         * @param[in] PtlConfig_t structure created in order to config the Ptl
         * @pre --
         * @post Ptl is initialized and you can finally create tasks
         */
        void vPtlInit(PtlConfig_t*);

        
        /**
         * @brief Runs the scheduler
         *
         * @pre  Ptl is being initialized with vPtlInit function
         * @post Scheduler will run with the Ptl on top of it
         */
        void vPtlStart();

             /**
         * @brief Adds an Aperiodic Job to the Server's Queue.
         * @details This function allocates a job node and appends it to the FIFO queue
         * managed by the Polling Server. It is thread-safe.
         * @param[in] pxCode        Pointer to the function to execute (the job body).
         * @param[in] pvArgs        Pointer to arguments passed to the function.
         * @param[in] xSoftDeadline Relative deadline (D) from the moment of arrival.
         * @param[in] ePolicy       Behavior if the soft deadline is missed.
         * * @return pdPASS if the job was queued successfully, pdFAIL if out of memory.
         */
        BaseType_t xPtlAddAperiodicTask(
            TaskFunction_t pxCode, 
            void *pvArgs, 
            TickType_t xSoftDeadline,
            PtlAperiodicPolicy_t ePolicy
        );


        /*==========================================================================================*/
        /*                                   Doxygen Template                                       */
        /*==========================================================================================*/  
        /**
         * @brief High-level description of what this module does.
         *
         * @details
         * A more detailed description of the module. You can add multiple lines here
         * to explain architectural decisions, dependencies, or usage examples.
         * 
         * @param[in] config  The configuration structure to apply.
         * @param[in] timeout_ms The max time in milliseconds to wait for init.
         * @return ModuleResult status code.
         * @retval RESULT_SUCCESS on success.
         * @retval RESULT_ERROR_PARAM if config is NULL.
         * 
         * @see vPtlInit()
         *
         * @note This function must be called before any other function in this file.
         *
         */
    #endif /* configUSE_PTL_SYSTEM == 1 */
    
#endif     /*PTL.H                                                                          */
