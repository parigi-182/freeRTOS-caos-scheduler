#include "include/ptl_pi.h"
#include "include/ptl_log.h"
#include "include/uart.h"
#include "ptl_log.h"
#include "polling_server_pi.h"
#include <stdlib.h>


#if ( configUSE_PTL_SYSTEM == 1 )

    Ptl_t *pxSys;

        void vPtlInit(PtlConfig_t *conf){

            /*singleton guard*/
            UART_init();
            if (pxSys != NULL) return;
            
            /*since calloc is used all other fields are authomatically initialized at 0                */
            pxSys = (Ptl_t*) pvPortCalloc(1, sizeof(Ptl_t)); 

            pxSys->ucNTasks         = conf->ucNTasks + 1;

            pxSys->pxTasks          = pvPortCalloc(pxSys->ucNTasks, sizeof(PtlTask_t));

            pxSys->xSimDuration    = conf->xSimDuration;
            pxSys->xGlobalPolicy   = conf->eGlobalPolicy;
            pxSys->ucCurrId        = 0;  
            
            
            /*==========================================================================================*/
            /*                                  Creating Tasks                                          */
            /*==========================================================================================*/
            
            /*https://forums.freertos.org/t/is-there-a-big-difference-between-vportmalloc-and-malloc/23047*/
            /*in a multitasking system it is much safer to use pvPortMalloc  */
            
            /* since in the main tasks are created in a static way (tasks[] = {...}) if we want to use pvPortMalloc we have to copy
            the pointer of the tasks array, allocate with malloc and then copy the original data, otherwise they'll be overwritten 
            when it call init */

            if(vPollingServerInit(conf->pxPollingServerConfig->xMaxBudget, conf->pxPollingServerConfig->ulNAperiodicTasks) == pdFAIL)
                vPtlExit();
        
            xPtlTaskCreate(
                        "PollingServer",
                        NULL,
                        NULL,
                        conf->pxPollingServerConfig->ulStackDepth, // Use Config
                        conf->pxPollingServerConfig->uxPriority,   // Use Config
                        conf->pxPollingServerConfig->xPeriod,      // Use Config
                        NO_DEADLINE,
                        pdTRUE
            );
            
            for(int i = 0; i < conf->ucNTasks; i++){
                xPtlTaskCreate(
                    (char*) conf->pxTasks[i].pcName,
                            conf->pxTasks[i].pxEntry,
                            conf->pxTasks[i].pvParameters,
                            conf->pxTasks[i].ulStackDepth,
                            conf->pxTasks[i].uxPriority,
                            conf->pxTasks[i].xPeriod,
                            conf->pxTasks[i].xDeadline,
                            pdFALSE
                );

            }

            
        }

        void vPtlStart() {
            pxSys->xGlobalStats.xStartTime = xTaskGetTickCount();
        
            vTaskStartScheduler();

        }


        BaseType_t xPtlTaskCreate(
            char *name, 
            void (*entry)(void*), 
            void *args, 
            uint32_t stackSize, 
            UBaseType_t priority,                                                                                                                                                                                                                                                                                   
            TickType_t period, 
            TickType_t deadline,
            BaseType_t pollingServer
        )
        {
            if(pxSys->ucCurrId == pxSys->ucNTasks)    
                return pdFAIL;
            PtlTask_t *tcb = &pxSys->pxTasks[pxSys->ucCurrId];
            if(pollingServer == pdTRUE){
                pxSys->pxPollingServer->pxPtlPollingTCB = tcb;
            }




            if(deadline == NO_DEADLINE)
                deadline = period;
                
            int i = 0;
            while (name[i] != '\0' && i < (configMAX_TASK_NAME_LEN - 1)) {
                tcb->pcName[i] = name[i];
                i++;
            }
            tcb->pcName[i] = '\0';      

            tcb->pxEntry             =   entry;
            tcb->pvParameters        =   args;
            tcb->ulStackSize         =   stackSize;
            tcb->uxPriority          =   priority;
            tcb->xPeriod             =   period;
            tcb->xDeadline           =   deadline;


            tcb->ucId = pxSys->ucCurrId; 
            
            tcb->xNextArrivalTime    = period;
            tcb->xAbsoluteDeadline   = deadline;
            tcb->xJobFinished        = pdTRUE;
            tcb->xJobKilled          = pdFALSE;

            pxSys->ucCurrId++;

            BaseType_t xReturn;

            if(pollingServer == pdTRUE){
                xReturn = xTaskCreate(
                vPtlPollingServerWrapper,       
                name,                  
                stackSize,            
                (void *)tcb,       
                priority,              
                &tcb->xTaskHandlePtl 
                );
            }
            else{
                xReturn = xTaskCreate(
                vPtlTaskWrapper,       
                name,                  
                stackSize,            
                (void *)tcb,       
                priority,              
                &tcb->xTaskHandlePtl 
                );
            }


            /*passing to the scheduler the value of the tcb of this particular task: linking task.c to this module*/
            vTaskSetPtlInScheduler(tcb->xTaskHandlePtl, (void *)tcb);

            

        /* WHEN creating the task, REMEMBER to stop its execution immediatly using the function vTaskSuspend()*/
        /* The PTL, when its all ready, will eventually ruin the command xTaskResumeAll()                     */
        /*Cannot return NULL, return values are pdFAIL or pdPASS*/
        return xReturn;
        }     


        /*statistics are still not accurate at all, need redefinement and also the ptl on conctext switch 
        is needed in order to check actual times*/
        void vPtlTaskWrapper(void* pvParameters){
            PtlTask_t *tcb = (PtlTask_t*)pvParameters;
            TickType_t xNow;      

            tcb->xJobKilled = pdFALSE;
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

            for(;;){ 
                taskENTER_CRITICAL();
                BUFFER_RAM_WRITE(tcb, ePtlEvtStart);   
                xNow = xTaskGetTickCount();       
                /*JOB is starting right now*/
                tcb->xJobFinished = pdFALSE;
                
                /*AND its start time is NOW*/
                tcb->xTaskStats.xStartTime = xNow;

                tcb->xCurrentExecTime = 0;
                /* Also reset the anchor point for the new job */
                tcb->xLastSwitchedInTime = xNow; 

                
                /*We need a local variable holding the real value of the deadline when the task started.
                In Catch Up policy, a deadline miss occurs, but it also updates the value of absolute deadline
                This way, this code, cannot be aware of the deadline miss. Using a local variable instead of 
                xAbsoluteDeadline is a way to  get rid of this consistency error*/
                TickType_t xThisJobDeadline = tcb->xAbsoluteDeadline;
                taskEXIT_CRITICAL();

                /*Actual Logic*/
                tcb->pxEntry(tcb->pvParameters);

                
                /*necessary, if a context switch happens right there i wouldnt imagine the mess..*/
                taskENTER_CRITICAL();
                xNow = xTaskGetTickCount();
                
                TickType_t xTimeChunk = xNow - tcb->xLastSwitchedInTime;
                
                tcb->xCurrentExecTime += xTimeChunk;

                tcb->xLastSwitchedInTime = xNow; /*Need this since you can exit this function in 2 ways..there is not the update
                inside the increseTickCount, so you need to manually compute the exact exec time right there
                in order to keep the values consistent!*/
                
                /*job finished, right now*/
                tcb->xTaskStats.xFinishTime = xNow;
                
                /*Update WCET if necessary*/
                if(tcb->xTaskStats.xMaxExecutionTime < tcb->xCurrentExecTime)
                    tcb->xTaskStats.xMaxExecutionTime = tcb->xCurrentExecTime;
                
                tcb->xTaskStats.ulSumExecutionTime += tcb->xCurrentExecTime;
                
                /*Checks for Deadline Miss (xNow is the finish time)*/
                if (xNow > xThisJobDeadline)
                {
                    tcb->xTaskStats.ulDeadlineMisses++;
                    BUFFER_RAM_WRITE(tcb, ePtlEvtDeadLineMiss);
                }
                
                tcb->xJobFinished = pdTRUE;
                BUFFER_RAM_WRITE(tcb, ePtlEvtCompleteEnd);
                taskEXIT_CRITICAL();
                ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            }
        }

        void vPtlExit(){
            taskDISABLE_INTERRUPTS();
            pxSys->xGlobalStats.xEndTime = xTaskGetTickCount();

            vPtlLogDump();
            vPtlStatsDump();       
                        
            UART_printf("#================================================================================================#\n");
            UART_printf("#                                         Simulation Ended                                       #\n");
            UART_printf("#---Executing infinite for(;;); cycle                                                            #\n");
            UART_printf("#================================================================================================#\n");

            vTaskEndScheduler();

            for(;;);
        }

    
#endif /* ( configUSE_PTL_SYSTEM == 1 )*/

