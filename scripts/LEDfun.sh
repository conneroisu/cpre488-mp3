#!/bin/sh
# LED Fun Script!

# Defines

# Addresses
LED_ADDR=0x41200000
BTN_ADDR=$(($LED_ADDR + 0x10000))
SW_ADDR=$(($BTN_ADDR + 0x10000))

# GPIO Offsets
DATA_OFFSET=0x0
TRI_OFFSET=0x4

# GPIO Values
LED_VAL=0
BTN_VAL=0
SW_VAL=0

# Modes
EXIT=0
LED_PASS=0
LED_INV_PASS=0
ALL_LED=0

# Functions
# devmem syntax
# devmem <addr> <size> <write data>
set_mem()
{
    # $(devmem $1 32 $2)
    echo setting $1 to $2
}

get_mem()
{
    # return $(devmem $1 32)
    echo getting $1
}

# GPIO setup
# Buttons and switches need to be inputs.
set_mem $BTN_ADDR 0x1
set_mem $SW_ADDR 0x1

# Main loop
while [ $EXIT -eq 0 ]
do
    # Get buttons and switches
    get_mem $BTN_ADDR
    BTN_VAL=$?

    get_mem $SW_ADDR
    SW_VAL=$?

    BTN_VAL=0x0

    # Set modes
    EXIT=$(($(($BTN_VAL & 0x2))-eq0x2))
    LED_PASS=$(($BTN_VAL-eq0x0))
    LED_INV_PASS=$(($BTN_VAL-eq0x1))

    # Mode precedence:
    # 1: Exit, 2: Inv Passthrough, 3: Passthrough
    if [ $EXIT -eq 1 ]
    then
        echo Exiting LED Fun!

    elif [ $LED_INV_PASS -eq 1 ]
    then
        echo Inv pass
    elif [ $LED_PASS -eq 1 ]
    then
        echo Pass
    else
        echo No command given, which should not happen!
    fi

done