#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>

/*-------- CONSTANTS --------*/

// Button addresses and masks
#define BTN_ADDRESS 0x41210000
#define BTNC 0x01               // Center button (fire)
#define BTNU 0x10               // Up
#define BTND 0x02               // Down
#define BTNL 0x04               // Left
#define BTNR 0x08               // Right

// Launcher commands
#define LAUNCHER_NODE "/dev/miss_launch0"
#define LAUNCHER_FIRE 0x10
#define LAUNCHER_STOP 0x20
#define LAUNCHER_UP 0x02
#define LAUNCHER_DOWN 0x01
#define LAUNCHER_LEFT 0x04
#define LAUNCHER_RIGHT 0x08
#define LAUNCHER_UP_LEFT (LAUNCHER_UP | LAUNCHER_LEFT)
#define LAUNCHER_DOWN_LEFT (LAUNCHER_DOWN | LAUNCHER_LEFT)
#define LAUNCHER_UP_RIGHT (LAUNCHER_UP | LAUNCHER_RIGHT)
#define LAUNCHER_DOWN_RIGHT (LAUNCHER_DOWN | LAUNCHER_RIGHT)

// Movement duration in milliseconds
#define MOVE_DURATION 500

/*-------- FUNCTION DECLARATIONS --------*/

static void launcher_cmd(int fd, int cmd);
void cleanup(int status, void* fd);

/*-------- MAIN PROGRAM --------*/

int main() {
    int fd;                     // Launcher file descriptor
    int* buttons;               // Mapped button memory
    int memfd;                  // Memory file descriptor
    int cmd = LAUNCHER_STOP;    // Current command
    char* dev = LAUNCHER_NODE;  // Launcher device node

    // Open memory for button mapping
    memfd = open("/dev/mem", O_RDWR);
    if (memfd < 0) {
        perror("Failed to open /dev/mem");
        exit(EXIT_FAILURE);
    }

    // Map button memory
    buttons = (int*)mmap(NULL, sizeof(int), PROT_READ, MAP_SHARED, memfd, BTN_ADDRESS);
    if (buttons == MAP_FAILED) {
        perror("Failed to map button memory");
        close(memfd);
        exit(EXIT_FAILURE);
    }

    // Open launcher device
    fd = open(dev, O_RDWR);
    if (fd == -1) {
        perror("Failed to open launcher device");
        munmap(buttons, sizeof(int));
        close(memfd);
        exit(EXIT_FAILURE);
    }

    // Register cleanup handler
    on_exit(cleanup, &fd);

    printf("Launcher control started. Press buttons to control (Center to fire).\n");

    // Main control loop
    while (1) {
        // Read button state
        int btn_state = *buttons;
        
        // Determine command based on button presses
        if (btn_state & BTNC) {
            cmd = LAUNCHER_FIRE;
        } 
        // Diagonal movements
        else if ((btn_state & BTNU) && (btn_state & BTNR)) {
            cmd = LAUNCHER_UP_RIGHT;
        } 
        else if ((btn_state & BTNR) && (btn_state & BTND)) {
            cmd = LAUNCHER_DOWN_RIGHT;
        } 
        else if ((btn_state & BTND) && (btn_state & BTNL)) {
            cmd = LAUNCHER_DOWN_LEFT;
        } 
        else if ((btn_state & BTNL) && (btn_state & BTNU)) {
            cmd = LAUNCHER_UP_LEFT;
        } 
        // Single direction movements
        else if (btn_state & BTNU) {
            cmd = LAUNCHER_UP;
        } 
        else if (btn_state & BTNR) {
            cmd = LAUNCHER_RIGHT;
        } 
        else if (btn_state & BTND) {
            cmd = LAUNCHER_DOWN;
        } 
        else if (btn_state & BTNL) {
            cmd = LAUNCHER_LEFT;
        } 
        else {
            cmd = LAUNCHER_STOP;
        }

        // Send command to launcher
        launcher_cmd(fd, cmd);
        
        // For movement commands, send stop after short delay
        if (cmd != LAUNCHER_FIRE && cmd != LAUNCHER_STOP) {
            usleep(MOVE_DURATION * 1000);
            launcher_cmd(fd, LAUNCHER_STOP);
        }
    }

    // Cleanup (unreachable in this loop)
    munmap(buttons, sizeof(int));
    close(memfd);
    return EXIT_SUCCESS;
}

/**
 * Sends a command to the launcher device
 */
static void launcher_cmd(int fd, int cmd) {
    int retval = write(fd, &cmd, 1);
    
    while (retval != 1) {
        if (retval < 0) {
            perror("Command failed");
            return;
        } 
        else if (retval == 0) {
            printf("Launcher busy, retrying...\n");
        }
        retval = write(fd, &cmd, 1);
    }

    // Extra delay for fire command
    if (cmd == LAUNCHER_FIRE) {
        usleep(2000000); // 2 second delay for firing
    }
}

/**
 * Cleanup handler registered with on_exit
 */
void cleanup(int status, void* fd) {
    close(*((int*)fd));
    printf("Launcher device closed\n");
}
