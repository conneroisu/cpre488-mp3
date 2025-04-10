#!/bin/sh

while true; do
	bttn_status=$(devmem 0x41210000)
	sw_status=$(devmem 0x41220000)
	led_output="0x00000000"
	if [ $((sw_status & 0x00000001)) == 1 ]; then
		led_output=$((led_output + 0x00000001))
	fi
	if [ $((sw_status & 0x00000002)) == 2 ]; then
		led_output=$((led_output + 0x00000002))
	fi
	if [ $((sw_status & 0x00000004)) == 4 ]; then
		led_output=$((led_output + 0x00000004))
	fi
	if [ $((sw_status & 0x00000008)) == 8 ]; then
		led_output=$((led_output + 0x00000008))
	fi
	if [ $((sw_status & 0x00000010)) == 16 ]; then
		led_output=$((led_output + 0x00000010))
	fi
	if [ $((sw_status & 0x00000020)) == 32 ]; then
		led_output=$((led_output + 0x00000020))
	fi
	if [ $((sw_status & 0x00000040)) == 64 ]; then
		led_output=$((led_output + 0x00000040))
	fi
	if [ $((sw_status & 0x00000080)) == 128 ]; then
		led_output=$((led_output + 0x00000080))
	fi


	if [ "$bttn_status" == "0x00000000" ]; then
		led_output=$(printf "0x%08X" $led_output)
		echo "State 1 - led_output = $led_output"
		devmem 0x41200000 32 $led_output
	elif [[ "$bttn_status" == "0x00000001" || "$bttn_status" == "0x00000004" || "$bttn_status" == "0x00000008" || "$bttn_status" == "0x00000010" ]]; then
		led_output=$((0x000000FF - led_output))
		led_output=$(printf "0x%08X" $led_output)
		echo "State 2 - led_output = $led_output"
		devmem 0x41200000 32 $led_output
	else
		devmem 0x41200000 32 0x000000FF
	fi

done
