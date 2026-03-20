#include "polling_server_pi.h"
#include "task.h"
#include "ptl_log.h"
#include <stdlib.h> 

#if (configUSE_PTL_SYSTEM == 1)
    extern Ptl_t *pxSys;
    PtlPollingServer_t *pxPolling;

    BaseType_t vPollingServerInit(const TickType_t xMaxBudget, uint8_t ulNAperiodicTasks)
    {
        if (pxPolling != NULL) return pdTRUE;
            
        pxPolling = (PtlPollingServer_t*) pvPortCalloc(1, sizeof(PtlPollingServer_t)); 
        if (pxPolling == NULL) return pdFAIL;

        if(pxSys == NULL || pxSys->pxTasks == NULL) return pdFAIL;
            
        pxPolling->xMaxBudget = xMaxBudget;
        pxPolling->xCurrentBudget = xMaxBudget;
        pxPolling->ulPendingJobs = 0;
        pxPolling->xAperiodicRunning = pdFALSE;


        pxPolling->xJobQueue = xQueueCreate(ulNAperiodicTasks, sizeof(PtlAperiodicTask_t));
        if (pxPolling->xJobQueue == NULL) {
            vPortFree(pxPolling);
            return pdFAIL;
        }

        pxSys->pxPollingServer = pxPolling;
        return pdPASS;
    }


    BaseType_t xPtlAddAperiodicTask(
        TaskFunction_t pxCode, 
        void *pvArgs, 
        TickType_t xSoftDeadline,
        PtlAperiodicPolicy_t ePolicy
    )
    {
        if (pxPolling == NULL || pxPolling->xJobQueue == NULL) {
            return pdFAIL;
        }

        PtlAperiodicTask_t xNewJob;
        pxPolling->pxAperiodicStats.ulTotalJobsReleased++; 


        xNewJob.pxTaskCode    = pxCode;
        xNewJob.pvParameters  = pvArgs;
        xNewJob.xArrivalTime  = xTaskGetTickCount(); 
        xNewJob.xSoftDeadline = xSoftDeadline;
        xNewJob.xAbsoluteDeadline = xSoftDeadline + xNewJob.xArrivalTime;
        xNewJob.ePolicy       = ePolicy;

        if (xQueueSend(pxPolling->xJobQueue, &xNewJob, (TickType_t)0) != pdPASS) {      
                BUFFER_RAM_WRITE_APERIODIC(POLLING_SERVER_ID, 0, ePtlEvtAperiodicDrop);
                pxPolling->pxAperiodicStats.ulTotalJobsDropped++;
                return pdPASS;
        }

        BUFFER_RAM_WRITE_APERIODIC(POLLING_SERVER_ID, 0, ePtlEvtAperiodicEnqueue);

        return pdPASS;
    }

    void vKillAperiodicHandle(void * pvParameters) {
        PtlTask_t * pxPollingTCB = (PtlTask_t *) pvParameters;

        /* 1. STATISTICS & STATE CLEANUP */
        // Recover CPU time lost during the killed job
        TickType_t xNow = pxPollingTCB->xTaskStats.ulSumExecutionTime + pxPollingTCB->xCurrentExecTime;
        TickType_t xStart = pxPolling->pxAperiodicStats.xCurrentJobStartTime;
        pxPolling->xAperiodicRunning = pdFALSE;
        
        if (xNow > xStart) {
            pxPolling->pxAperiodicStats.xTotalAperiodicExecTime += (xNow - xStart);
        }

        pxPolling->pxAperiodicStats.ulTotalJobsKilled++;
        pxPolling->pxAperiodicStats.ulTotalDeadlineMisses++;
        pxPolling->xAperiodicRunning = pdFALSE;
        pxPollingTCB->xJobKilled = pdFALSE;      
        
        BUFFER_RAM_WRITE_APERIODIC(POLLING_SERVER_ID, 0, ePtlEvtAperiodicKill);
        
        if ( pxPollingTCB->xCurrentExecTime < pxPolling->xMaxBudget ) 
            vPollingServerBody( pxPollingTCB );
        vPtlPollingServerWrapper((void*)pxPollingTCB);
    }


    void vPtlPollingServerWrapper(void * pvParameters){
        (void)pvParameters;
        PtlTask_t * pxPollingTCB = pxPolling->pxPtlPollingTCB;
        pxPollingTCB->xJobKilled = pdFALSE;
        TickType_t xNow;
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        for(;;)
        {     
            taskENTER_CRITICAL();
                BUFFER_RAM_WRITE(pxPollingTCB, ePtlEvtStart);
                pxPollingTCB->xJobFinished = pdFALSE;
                xNow = xTaskGetTickCount();  
                pxPollingTCB->xLastSwitchedInTime = xNow; 
            taskEXIT_CRITICAL();
            
            vPollingServerBody(pxPollingTCB);

            taskENTER_CRITICAL();                
                pxPollingTCB->xJobFinished = pdTRUE;
            taskEXIT_CRITICAL();

            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        }
    }

    void vPollingServerBody(PtlTask_t * pxTCB)
    {
        if(pxTCB == NULL) return;
        
        TickType_t xJobStart, xJobEnd, xNow; 

        BUFFER_RAM_WRITE_APERIODIC(POLLING_SERVER_ID, 0, ePtlEvtServerReplenish);

        while ( (pxTCB->xCurrentExecTime < pxPolling->xMaxBudget) && 
                (uxQueueMessagesWaiting(pxPolling->xJobQueue) > 0) )
        {
            if (xQueueReceive(pxPolling->xJobQueue, &pxPolling->pxCurrentAperiodicTask, 0) == pdPASS) 
            {
                taskENTER_CRITICAL();
                xNow = xTaskGetTickCount();
                pxPolling->xAperiodicRunning = pdTRUE;
                
                if (xNow > pxPolling->pxCurrentAperiodicTask.xAbsoluteDeadline) {
                    BUFFER_RAM_WRITE_APERIODIC(POLLING_SERVER_ID, 0, ePtlEvtAperiodicDrop);
                    pxPolling->pxAperiodicStats.ulTotalJobsDropped++; 
                    pxPolling->pxAperiodicStats.ulTotalDeadlineMisses++; 
                    pxPolling->xAperiodicRunning = pdFALSE;
                    taskEXIT_CRITICAL();
                    continue; 
                }

                BUFFER_RAM_WRITE_APERIODIC(POLLING_SERVER_ID, 0, ePtlEvtAperiodicStart);
                
                /* For now, let's assume you fix the pointer issue separately. */
                xJobStart = pxTCB->xTaskStats.ulSumExecutionTime + pxTCB->xCurrentExecTime;
                pxPolling->pxAperiodicStats.xCurrentJobStartTime = xJobStart; 
                pxPolling->pxAperiodicStats.ulTotalJobsExecuted++;
            taskEXIT_CRITICAL();
                
                if (pxPolling->pxCurrentAperiodicTask.pxTaskCode != NULL) {
                    pxPolling->pxCurrentAperiodicTask.pxTaskCode(pxPolling->pxCurrentAperiodicTask.pvParameters);
                }
                taskENTER_CRITICAL();
                    pxPolling->xAperiodicRunning = pdFALSE;
                    xJobEnd = pxTCB->xTaskStats.ulSumExecutionTime + pxTCB->xCurrentExecTime;                  
                    pxPolling->pxAperiodicStats.xTotalAperiodicExecTime += (xJobEnd - pxPolling->pxAperiodicStats.xCurrentJobStartTime);
                taskEXIT_CRITICAL();

                
                if (xJobEnd > pxPolling->pxCurrentAperiodicTask.xAbsoluteDeadline) {
                    BUFFER_RAM_WRITE_APERIODIC(POLLING_SERVER_ID, 0, ePtlEvtAperiodicOverrun);
                    pxPolling->pxAperiodicStats.ulTotalDeadlineMisses++;
                }
                else{
                    BUFFER_RAM_WRITE_APERIODIC(POLLING_SERVER_ID, 0, ePtlEvtAperiodicDone);
                }
            }
        }
    }

#endif /* configUSE_PTL_SYSTEM */