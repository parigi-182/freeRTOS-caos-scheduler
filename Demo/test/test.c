#ifndef __TEST__
    #define __TEST__
#endif

#include "unity/unity.h"
#include "ptl_pi.h"
#include "ptl_log.h"
#include "ptl_stats.h"

/*you can see parameter sys, since it is declared external*/

void testTaskFunction(void* args) { (void)args; }
void testTaskFunction2(void* args) { (void)args; }


void setUp(void) {
    sys->nTasks = 0;
    sys->currId = 0;
    sys->tasks = NULL;
}
void tearDown(void) {
   if (sys != NULL) {
        if (sys->tasks != NULL) {
            vPortFree(sys->tasks);
        }
        vPortFree(sys);
        sys = NULL; 
    }
}

void vPtlInitTest(void){
    PtlTaskParams_t testTasks[] = {
    { "Task1", testTaskFunction, NULL, 128, 1, 100, 50 },
    { "Task2", testTaskFunction2, NULL, 256, 2, 200, NO_DEADLINE} 
    };
	 
    PtlConfig_t testConf = { 
        .nTasks = 2, 
        .tasks = testTasks, 
        .simDuration = 1000, 
        .globalPolicy = POLICY_SKIP 
    };

    vPtlInit(&testConf);

    TEST_ASSERT_NOT_NULL(sys);

    TEST_ASSERT_EQUAL_UINT8(2, sys->nTasks);
    TEST_ASSERT_EQUAL_UINT32(1000, sys->simDuration);
    TEST_ASSERT_EQUAL_INT(POLICY_SKIP, sys->globalPolicy);

    TEST_ASSERT_NOT_NULL_MESSAGE(sys->tasks, "sys.tasks should be allocated");

    TEST_ASSERT_EQUAL_UINT8(2, sys->currId); /*if i create 2, the currentId = 2 BECAUSE i have  task id 0 and 1, 2 is the next task to create*/
    TEST_ASSERT_EQUAL_UINT8(0, sys->tasks[0].id);
    TEST_ASSERT_EQUAL_UINT8(1, sys->tasks[1].id);
    TEST_ASSERT_NOT_EQUAL_UINT8(2, sys->tasks[1].id);

    TEST_ASSERT_NOT_NULL_MESSAGE(sys, "sys should be allocated.");
    TEST_ASSERT_NOT_NULL_MESSAGE(sys->tasks, "tasks should be allocated.");
    TEST_ASSERT_EQUAL_UINT32(0, sys->globalStats.startTime);
    TEST_ASSERT_EQUAL_UINT32(0, sys->globalStats.idleTime);
    TEST_ASSERT_EQUAL_UINT32(0, sys->globalStats.endTime);


    TEST_ASSERT_EQUAL_INT(POLICY_SKIP, sys->globalPolicy);
}


void xTaskCreateAndWrapperTest(){

    PtlTaskParams_t testTasks[] = {
    { "Task1", testTaskFunction, NULL, 128, 1, 100, 50 },
    { "Task2", testTaskFunction2, NULL, 256, 2, 200, NO_DEADLINE} 
    };
	
	 PtlConfig_t *testConf = (PtlConfig_t*) pvPortMalloc(sizeof(PtlConfig_t));
	 
     *testConf = (PtlConfig_t){ 
    .nTasks = 2, 
    .tasks = testTasks, 
    .simDuration = 5000, 
    .globalPolicy = POLICY_SKIP 
    };

    vPtlInit(testConf);

    PtlTask_t *t1 = &sys->tasks[0];
    TEST_ASSERT_EQUAL_STRING("Task1", t1->name);
    TEST_ASSERT_EQUAL_PTR(testTaskFunction, t1->entry);
    TEST_ASSERT_EQUAL_UINT32(128, t1->stackSize);
    TEST_ASSERT_EQUAL_UINT8(1, t1->priority);
    TEST_ASSERT_EQUAL_UINT32(100, t1->period);
    TEST_ASSERT_EQUAL_UINT32(50, t1->deadline); 

    PtlTask_t *t2 = &sys->tasks[1];
    TEST_ASSERT_EQUAL_STRING("Task2", t2->name);
    TEST_ASSERT_EQUAL_PTR(testTaskFunction2, t2->entry);
    TEST_ASSERT_EQUAL_UINT32(256, t2->stackSize);
    TEST_ASSERT_EQUAL_UINT8(2, t2->priority);
    TEST_ASSERT_EQUAL_UINT32(200, t2->period);
    TEST_ASSERT_EQUAL_UINT32(200, t2->deadline); 
}


void setUpBuffer(void) {
    sys->nTasks = 0;
    sys->currId = 0;
    sys->tasks = NULL;

    vPtlLogInit();
}

void tearDownBuffer(void) {
    if (sys != NULL) {
        if (sys->tasks != NULL) {
            vPortFree(sys->tasks);
        }
        vPortFree(sys);
        sys = NULL;
    }
}

/* Dummy task to simulate a job */
void dummyTaskFunc(void* args) { (void)args; }

/* ================= TESTS LOG ================= */

void test_vPtlLogInit(void) {
    vPtlLogInit();

    TEST_ASSERT_EQUAL_UINT32(0, ulPtlLogWriteIdx);
    TEST_ASSERT_EQUAL_INT(pdFALSE, xPtlLogFull);
}

void test_vPtlLogEvent_single(void) {
    vPtlLogInit();

    PtlTask_t dummyTask = { 
        .id = 1, 
        .tStats = { .totalJobsReleased = 0 } 
    };

    vPtlLogEvent(&dummyTask, PTL_EVT_START);

    TEST_ASSERT_EQUAL_UINT32(1, ulPtlLogWriteIdx);
    TEST_ASSERT_EQUAL_UINT8(1, xPtlLogBuffer[0].taskId);
    TEST_ASSERT_EQUAL_INT(PTL_EVT_START,xPtlLogBuffer[0].event);
    TEST_ASSERT_EQUAL_UINT32(0, xPtlLogBuffer[0].jobSeq);
}

void test_vPtlLogEvent_overflow(void) {
    vPtlLogInit();

    PtlTask_t dummyTask = { 
        .id = 2, 
        .tStats = { .totalJobsReleased = 0 } 
    };

    for (int i = 0; i < PTL_LOG_BUFFER_SIZE + 5; i++) {
        vPtlLogEvent(&dummyTask, PTL_EVT_RELEASE);
    }

    TEST_ASSERT_EQUAL_INT(pdTRUE, xPtlLogFull);
    TEST_ASSERT_EQUAL_UINT32(5, ulPtlLogWriteIdx);
}

/* Test dump buffer (It doesn’t check the output, but it verifies that there is no crash) */
void test_vPtlLogDump(void) {
    vPtlLogInit();

    PtlTask_t dummyTask = { 
        .id = 3, 
        .tStats = { .totalJobsReleased = 0 } 
    };
    
    vPtlLogEvent(&dummyTask, PTL_EVT_COMPLETE);

    vPtlLogDump();

    TEST_ASSERT_EQUAL_UINT32(1, ulPtlLogWriteIdx);
}

/* ================= TEST STATISTICS ================= */

void test_vPtlStatsDump(void) {
    PtlTask_t dummyTask = { 
        .id = 4,
        .tStats = {
            .totalJobsReleased = 3,
            .deadlineMisses = 1,
            .overruns = 2,
            .maxExecutionTime = 50,
            .sumExecutionTime = 120
        }
    };
    sys->nTasks = 1;
    sys->tasks = &dummyTask;

    vPtlStatsDump();

    TEST_ASSERT_EQUAL_UINT32(3, sys->tasks[0].tStats.totalJobsReleased);
    TEST_ASSERT_EQUAL_UINT32(1, sys->tasks[0].tStats.deadlineMisses);
}

int main(void) {
    UART_init(); 
    UNITY_BEGIN();
    
    RUN_TEST(vPtlInitTest);
    RUN_TEST(xTaskCreateAndWrapperTest);
    RUN_TEST(test_vPtlLogInit);
    RUN_TEST(test_vPtlLogEvent_single);
    RUN_TEST(test_vPtlLogEvent_overflow);
    RUN_TEST(test_vPtlLogDump);
    RUN_TEST(test_vPtlStatsDump);

    UNITY_END();

    while(1) { } 
}
