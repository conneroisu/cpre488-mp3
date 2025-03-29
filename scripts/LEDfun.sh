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
LED_PASS=0
LED_INV_PASS=0

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
while :
do
    # Get buttons and switches
    get_mem $BTN_ADDR
    BTN_VAL=$?

    get_mem $SW_ADDR
    SW_VAL=$?

    # Set modes
    # Note that tests when pass return exit code of 0
    # [ ] is a test, result gets set to $?
    # Also, BTN_VAL evaluated in a subshell for hex parsing.

    # Check if no buttons are being pressed
    [ $(($BTN_VAL)) -eq 0 ]
    LED_PASS=$?

    # Check if one button is being pressed
    [ $(($BTN_VAL)) -eq 1 ] || [ $(($BTN_VAL)) -eq 2 ] || [ $(($BTN_VAL)) -eq 4 ] || [ $(($BTN_VAL)) -eq 8 ] || [ $(($BTN_VAL)) -eq 12 ]
    LED_INV_PASS=$?

    if [ "$LED_PASS" -eq 0 ]
    then
        LED_VAL=$(($SW_VAL))

    elif [ "$LED_INV_PASS" -eq 0 ]
    then
        LED_VAL=$((~$SW_VAL & 0xFF))
    else
        LED_VAL=0xFF
    fi

    echo LED Val: $LED_VAL

done