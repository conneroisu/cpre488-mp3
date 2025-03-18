#!/bin/sh

while true; do
	bttn_status=$(devmem 0x41210000)
	sw_status=$(devmem 0x41220000)
	led_output="0x00000000"
	if [ "$sw_status" == "0x00000001" ]; then
		led_output=$((led_output + 0x00000001))
	fi
	if [ "$sw_status" == "0x00000002" ]; then
		led_output=$((led_output + 0x00000002))
	fi
	if [ "$sw_status" == "0x00000004" ]; then
		led_output=$((led_output + 0x00000004))
	fi
	if [ "$sw_status" == "0x00000008" ]; then
		led_output=$((led_output + 0x00000008))
	fi
	if [ "$sw_status" == "0x00000010" ]; then
		led_output=$((led_output + 0x00000010))
	fi
	if [ "$sw_status" == "0x00000020" ]; then
		led_output=$((led_output + 0x00000020))
	fi
	if [ "$sw_status" == "0x00000040" ]; then
		led_output=$((led_output + 0x00000040))
	fi
	if [ "$sw_status" = "0x00000080" ]; then
		led_output=$((led_output + 0x00000080))
	fi


	if [ "$bttn_status" == "0x00000000" ]; then
		devmem 0x41200000 32 $led_output
	elif [[ "$bttn_status" == "0x00000001" || "$bttn_status" == "0x00000004" || "$bttn_status" == "0x00000008" || "$bttn_status" == "0x00000010" ]]; then
		led_output=$((0x000000FF - led_output))
		devmem 0x41200000 32 $led_output
	else
		devmem 0x41200000 32 0x000000FF
	fi

done