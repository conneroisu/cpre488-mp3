# SB-3 Subgroup B Submission

## NOTE:
We were not able to get OpenCV to work with Petalinux. The OpenCV code is called `vision-app.cpp` and has been tested with a laptop webcam and is functional.

Also, I renamed `laucher-fire-buttons.c` to `launch-ctrl.c` for brevity.

`control-interface.c` and `control-interface.h` are used by `launch-ctrl.c` to interface to the buttons on the zedboard.