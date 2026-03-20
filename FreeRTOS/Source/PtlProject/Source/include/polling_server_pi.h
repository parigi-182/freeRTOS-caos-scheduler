#ifndef __POLLING_SERVER_PI_H__
    #define __POLLING_SERVER_PI_H__

    #include "FreeRTOS.h"
    #include "ptl_pi.h"  
    #if (configUSE_PTL_SYSTEM == 1)


        /*==========================================================================================*/
        /*                                  Hidden Functions                                        */
        /*==========================================================================================*/
        
        void vKillAperiodicHandle(void * pvParameters);

        /**
         * @brief Initializes the Server (Internal).
         * @param[in] pxConfig User configuration from main.c
         */
        BaseType_t vPollingServerInit(const  TickType_t , uint8_t);


        /**
         * @brief create a wrapper for the entry of the polling server (body of the job)
         *
         * @param[in] void* args of the function
         * 
         * @pre  xPtlTaskCreate must be used by the user with all the data of the task
         * @post The wrapper will have the logic of task (that is a fuction)
         */ 
        void vPtlPollingServerWrapper(void *);


        /**
         * @brief The Server Task Loop.
         * @param[in] pvParameters Pointer to the PtlTask_t (Server TCB).
         */
        void vPollingServerBody(PtlTask_t *);

    #endif /* configUSE_PTL_SYSTEM */
#endif /* __POLLING_SERVER_PI_H__ */