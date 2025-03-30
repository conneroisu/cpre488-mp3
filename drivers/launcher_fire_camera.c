#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/mman.h>

// Constants
#define FRAME_BUFFER_ADDR 0x10000000
#define VDMA_BASE_ADDR 0x43000000
#define LAUNCHER_NODE "/dev/launcher0"

// Launcher commands
#define LAUNCHER_FIRE   0x10
#define LAUNCHER_STOP   0x20
#define LAUNCHER_UP     0x02
#define LAUNCHER_DOWN   0x01
#define LAUNCHER_LEFT   0x04
#define LAUNCHER_RIGHT  0x08

// Target detection parameters (adjust these based on your chosen color)
#define TARGET_Y_MIN    0
#define TARGET_Y_MAX    50    // Luminance range
#define TARGET_U_MIN    160   // Chrominance (red)
#define TARGET_U_MAX    255
#define TARGET_V_MIN    0
#define TARGET_V_MAX    130   // Chrominance (blue)

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t count;
} Target;

void launcher_cmd(int fd, int cmd);
int detect_target(uint16_t (*frame)[1920], Target* target);
void aim_and_fire(int fd, Target* target);

int main() {
    int fd_launcher = open(LAUNCHER_NODE, O_RDWR);
    if (fd_launcher == -1) {
        perror("Failed to open launcher");
        return 1;
    }

    // Map frame buffer
    int mem_fd = open("/dev/mem", O_RDWR);
    uint16_t (*frame)[1920] = mmap(NULL, 1920*1080*2, PROT_READ, MAP_SHARED, mem_fd, FRAME_BUFFER_ADDR);
    if (frame == MAP_FAILED) {
        perror("Failed to map frame buffer");
        close(fd_launcher);
        return 1;
    }

    Target target = {0};
    
    while (1) {
        if (detect_target(frame, &target)) {
            printf("Target detected at (%u, %u) with %u pixels\n", 
                  target.x, target.y, target.count);
            aim_and_fire(fd_launcher, &target);
        } else {
            usleep(100000); // Small delay when no target found
        }
    }

    // Cleanup (unreachable in this simple example)
    munmap(frame, 1920*1080*2);
    close(fd_launcher);
    return 0;
}

int detect_target(uint16_t (*frame)[1920], Target* target) {
    uint64_t x_sum = 0, y_sum = 0;
    uint32_t count = 0;
    
    // Scan entire frame for target color
    for (int y = 0; y < 1080; y++) {
        for (int x = 0; x < 1920; x += 2) {
            uint16_t pixel1 = frame[y][x];
            uint16_t pixel2 = frame[y][x+1];
            
            // Extract YUV components (adjust based on your camera format)
            uint8_t y1 = pixel1 & 0xFF;
            uint8_t u = (pixel1 >> 8) & 0xFF;
            uint8_t y2 = pixel2 & 0xFF;
            uint8_t v = (pixel2 >> 8) & 0xFF;
            
            // Check for target color (example for red target)
            if (y1 > TARGET_Y_MIN && y1 < TARGET_Y_MAX &&
                u > TARGET_U_MIN && v < TARGET_V_MAX) {
                x_sum += x;
                y_sum += y;
                count++;
            }
        }
    }
    
    if (count > 1000) { // Minimum pixel threshold
        target->x = x_sum / count;
        target->y = y_sum / count;
        target->count = count;
        return 1;
    }
    return 0;
}

void aim_and_fire(int fd, Target* target) {
    // Center of screen (adjust based on your launcher's calibration)
    const uint32_t center_x = 1920 / 2;
    const uint32_t center_y = 1080 / 2;
    
    // Calculate offsets
    int x_offset = (int)target->x - (int)center_x;
    int y_offset = (int)target->y - (int)center_y;
    
    // Adjust for dart spread (empirically determined)
    center_y += (15000 - target->count) / 300;
    
    // Horizontal adjustment
    if (x_offset < -50) {
        launcher_cmd(fd, LAUNCHER_LEFT);
        usleep(abs(x_offset) * 100); // Proportional timing
        launcher_cmd(fd, LAUNCHER_STOP);
    } 
    else if (x_offset > 50) {
        launcher_cmd(fd, LAUNCHER_RIGHT);
        usleep(abs(x_offset) * 100);
        launcher_cmd(fd, LAUNCHER_STOP);
    }
    
    // Vertical adjustment
    if (y_offset < -50) {
        launcher_cmd(fd, LAUNCHER_UP);
        usleep(abs(y_offset) * 100);
        launcher_cmd(fd, LAUNCHER_STOP);
    } 
    else if (y_offset > 50) {
        launcher_cmd(fd, LAUNCHER_DOWN);
        usleep(abs(y_offset) * 100);
        launcher_cmd(fd, LAUNCHER_STOP);
    }
    
    // Fire if target is centered enough
    if (abs(x_offset) < 50 && abs(y_offset) < 50) {
        launcher_cmd(fd, LAUNCHER_FIRE);
        usleep(2000000); // Wait for firing to complete
    }
}

void launcher_cmd(int fd, int cmd) {
    int ret = write(fd, &cmd, 1);
    while (ret != 1) {
        if (ret < 0) {
            perror("Command failed");
            return;
        }
        usleep(10000);
        ret = write(fd, &cmd, 1);
    }
}
