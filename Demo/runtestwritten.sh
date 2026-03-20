#!/bin/bash

#max duration for each test
TIMEOUT_SEC="3s"

# Output directory for log
LOG_DIR="test_logs"

#elf file
ELF_FILE="Output/demo.elf"

#create log directory
mkdir -p "$LOG_DIR"

echo "============================================="
echo "   AUTOMATIC TEST SESSION FOR TEST 11        "
echo "============================================="

i=11
    echo ""
    echo ">>> PREPARING TEST #$i"

    #clean before compiling the new test
    echo "    1. Cleaning the previous build..."
    make clean > /dev/null 2>&1

    #compile the new test, pass TEST_ID to makefile
    EXTRA_FLAGS=""
    
    if [ "$i" -eq 11 ]; then
        echo "       ! Detecting Written Test: Setting Short Simulation Time !"
        # Correct: Assign the string to the variable (no extra spaces, no $ at the start)
        EXTRA_FLAGS="DEFINEWRITTEN=1"
    fi

    echo "    2. Compiling Test #$i..."
    
    # 3. Pass the variable inside the make command line
    # (Do NOT put $EXTRA_FLAGS on a separate line before this)
    make all TEST_ID=$i $EXTRA_FLAGS > /dev/null 2>&1


    if [ $? -ne 0 ]; then
        echo "    [ERROR] Compiling of Test #$i failed!"
        exit 1
    fi

    #define the name of the log file
    LOG_FILE="$LOG_DIR/result_test_$i.txt"
    
    echo "    3. Execution QEMU ($TIMEOUT_SEC)..."
    echo "       Log destination: $LOG_FILE"

    # COMANDO CRUCIALE:
    # -serial file:"$LOG_FILE" -> QEMU open the file and write the output of the test
    timeout $TIMEOUT_SEC qemu-system-arm \
        -machine mps2-an385 \
        -cpu cortex-m3 \
        -kernel "$ELF_FILE" \
        -monitor none \
        -nographic \
        -serial file:"$LOG_FILE"
    
    echo "    [OK] Test #$i finished."

echo ""
echo "============================================="
echo " ALL TESTS COMPLETED SUCCESFULLY "
echo " Results are in directory: '$LOG_DIR'"
echo "============================================="