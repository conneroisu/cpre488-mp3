#include "launcher-commands.h"
#include "controls/control-interface.h"
#include <fcntl.h>
#include <unistd.h>

#define MISS_LAUNCH_DEVICE "/dev/miss_launch0"

// us
#define POLL_TIME 10000

// us
#define FIRE_TIME 3000000

// us
#define MOVE_TIME 500000

int send_command(uint8_t command, int miss_launch_fd);

int main()
{
    int exit, left, right, up, down, fire, did_action = 0;

    uint32_t btn_val, sw_val = 0;

    gpio_addr_maps_t maps = init_interface();

    // Open the missile launcher device.
    int miss_launch = open(MISS_LAUNCH_DEVICE, O_WRONLY);

    if(miss_launch < 0)
    {
        printf("ERROR: Could not open %s\n", MISS_LAUNCH_DEVICE);

        // Return the error.
        return miss_launch;
    }

    while(1)
    {
        // Stop launchers previous command
        send_command(LAUNCHER_STOP, miss_launch);

        // Get button and switch values
        sw_val = get_switch_states(maps);
        btn_val = get_button_states(maps);
        

        // Set state variables
        exit = sw_val & 0x1;
        left = button_pressed(LEFT, btn_val);
        right = button_pressed(RIGHT, btn_val);
        up = button_pressed(UP, btn_val);
        down = button_pressed(DOWN, btn_val);
        fire = button_pressed(CENTER, btn_val);

        // Exit prioritized first.
        // Left wins over right
        // Up wins over down
        // Fire can only run if it is the only button pressed.
        if(exit)
        {
            break;
        }
        else if(!left && !right && !up && !down && fire)
        {
            send_command(LAUNCHER_FIRE, miss_launch);
            usleep(FIRE_TIME);
            send_command(LAUNCHER_STOP, miss_launch);

            did_action = 1;
        }
        else
        {
            // Note: Left/Right and Up/Down can be at the same time.
            if(left && right)
            {
                right = 0;
            }

            if(up && down)
            {
                down = 0;
            }

            uint8_t command = 0;

            if(right)
            {
                command |= LAUNCHER_RIGHT;
            }

            if(left)
            {
                command |= LAUNCHER_LEFT;
            }

            if(up)
            {
                command |= LAUNCHER_UP;
            }

            if(down)
            {
                command |= LAUNCHER_DOWN;
            }

            send_command(command, miss_launch);
            usleep(MOVE_TIME);

            did_action = 1;
        }

        // Only wait POLL_TIME when an action has not been done.
        if(!did_action)
        {
            usleep(POLL_TIME);
        }

        did_action = 0;
        
    }

    close(miss_launch);
    cleanup_interface(maps);
    return 0;
}

int send_command(uint8_t command, int miss_launch_fd)
{
    ssize_t retval = write(miss_launch_fd, &command, 1);

    if(retval < 0)
    {
        printf("ERROR: Could not send command %d\n", command);
    }

    return retval;
}