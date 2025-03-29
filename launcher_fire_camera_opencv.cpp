/**
 * launcher_fire_camera_opencv.cpp
 *
 * Advanced target detection using OpenCV for the USB missile launcher
 * sentry system. This implementation provides more robust detection methods.
 * Added Z-position (depth) estimation for improved targeting.
 */

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

// OpenCV includes
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/opencv.hpp>

// Launcher commands header
#include "launcher_commands.h"

// Framebuffer dimensions
#define FB_WIDTH 640
#define FB_HEIGHT 480
#define FB_DEPTH 3 // RGB format, 3 bytes per pixel
#define FB_SIZE (FB_WIDTH * FB_HEIGHT * FB_DEPTH)

// Framebuffer physical address (adjust based on your system)
#define FB_PHYS_ADDR 0x10000000

// Launcher control parameters
#define LAUNCHER_MOVE_TIMEOUT_MS 1000
#define LAUNCHER_CENTER_X (FB_WIDTH / 2)
#define LAUNCHER_CENTER_Y (FB_HEIGHT / 2)
#define LAUNCHER_DEAD_ZONE 20
#define LAUNCHER_MAX_X_ANGLE 30
#define LAUNCHER_MAX_Y_ANGLE 20

// Target configurations
enum TargetColor {
  TARGET_RED,
  TARGET_GREEN,
  TARGET_BLUE,
  TARGET_YELLOW,
  TARGET_CYAN,
  TARGET_MAGENTA,
  TARGET_BLACK
};

// Selected target color (change to your preferred color)
#define SELECTED_TARGET TARGET_RED

// Z-position (depth) estimation parameters
#define TARGET_ACTUAL_DIAMETER_CM 15.0  // Actual target diameter in cm (adjust based on your target)
#define CAMERA_FOV_HORIZONTAL_DEG 60.0  // Camera field of view in degrees
#define MIN_TARGET_DISTANCE_CM 50.0     // Minimum expected target distance
#define MAX_TARGET_DISTANCE_CM 300.0    // Maximum expected target distance
#define FOCAL_LENGTH_PIXELS ((FB_WIDTH * 0.5) / tan((CAMERA_FOV_HORIZONTAL_DEG * 0.5) * M_PI / 180.0))

// Function prototypes
int open_launcher_device();
int move_launcher(int launcher_fd, unsigned char direction);
int fire_launcher(int launcher_fd);
int stop_launcher(int launcher_fd);
void delay_ms(int ms);
int aim_launcher(int launcher_fd, int current_x, int current_y, int target_x,
                 int target_y, float target_z);
cv::Point detect_target_hsv(cv::Mat &frame, TargetColor target_color,
                            bool &detected, float &estimated_z);
cv::Point detect_target_shape(cv::Mat &frame, TargetColor target_color,
                              bool &detected, float &estimated_z);
void setup_color_thresholds(TargetColor color, cv::Scalar &lower,
                            cv::Scalar &upper);
float estimate_z_position(double apparent_diameter_pixels);
int adjust_aim_for_depth(int launcher_fd, float target_z);

/**
 * Main function
 */
int main() {
  int ret = 0;
  int mem_fd, launcher_fd;
  void *fb_mem;
  bool target_detected = false;
  cv::Point target_point;
  float target_z = 0.0;  // Z-position (depth) in cm

  printf("Starting USB Missile Launcher Sentry with OpenCV and Z-position estimation...\n");

  // Open /dev/mem for framebuffer access
  mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
  if (mem_fd < 0) {
    perror("Failed to open /dev/mem");
    return -1;
  }

  // Map framebuffer memory
  fb_mem = mmap(NULL, FB_SIZE, PROT_READ, MAP_SHARED, mem_fd, FB_PHYS_ADDR);
  if (fb_mem == MAP_FAILED) {
    perror("Failed to mmap framebuffer");
    close(mem_fd);
    return -1;
  }

  // Open the launcher device
  launcher_fd = open_launcher_device();
  if (launcher_fd < 0) {
    perror("Failed to open launcher device");
    munmap(fb_mem, FB_SIZE);
    close(mem_fd);
    return -1;
  }

  printf("Sentry system initialized. Starting target detection loop...\n");

  // Create OpenCV Mat header for the framebuffer
  cv::Mat frame(FB_HEIGHT, FB_WIDTH, CV_8UC3, fb_mem);

  // Main detection and targeting loop
  while (1) {
    // Create a copy of the framebuffer data for OpenCV processing
    cv::Mat frame_copy = frame.clone();

    // Detect target using HSV color filtering (primary method)
    target_point =
        detect_target_hsv(frame_copy, SELECTED_TARGET, target_detected, target_z);

    // If HSV detection fails, try shape-based detection as fallback
    if (!target_detected) {
      target_point =
          detect_target_shape(frame_copy, SELECTED_TARGET, target_detected, target_z);
    }

    if (target_detected) {
      printf("Target detected at position (%d, %d, %.2f cm)\n", target_point.x,
             target_point.y, target_z);

      // Aim launcher at the target
      ret = aim_launcher(launcher_fd, LAUNCHER_CENTER_X, LAUNCHER_CENTER_Y,
                         target_point.x, target_point.y, target_z);

      if (ret == 0) {
        printf("Target locked, firing!\n");
        // Fire the launcher
        fire_launcher(launcher_fd);

        // Add delay after firing to avoid continuous firing
        delay_ms(2000);
      }
    } else {
      // No target detected, add small delay to avoid CPU overuse
      delay_ms(100);
    }
  }

  // Cleanup (this code would never be reached in the endless loop above)
  stop_launcher(launcher_fd);
  close(launcher_fd);
  munmap(fb_mem, FB_SIZE);
  close(mem_fd);

  return 0;
}

/**
 * Opens the launcher device
 * Returns file descriptor or -1 on error
 */
int open_launcher_device() {
  int fd = open("/dev/launcher0", O_WRONLY);
  return fd;
}

/**
 * Moves the launcher in the specified direction
 * Returns 0 on success, -1 on error
 */
int move_launcher(int launcher_fd, unsigned char direction) {
  int ret;

  if (launcher_fd < 0)
    return -1;

  ret = write(launcher_fd, &direction, 1);
  if (ret != 1) {
    perror("Error sending launcher movement command");
    return -1;
  }

  return 0;
}

/**
 * Fires the launcher
 * Returns 0 on success, -1 on error
 */
int fire_launcher(int launcher_fd) {
  int ret;
  unsigned char fire_cmd = LAUNCHER_FIRE;

  if (launcher_fd < 0)
    return -1;

  ret = write(launcher_fd, &fire_cmd, 1);
  if (ret != 1) {
    perror("Error sending launcher fire command");
    return -1;
  }

  return 0;
}

/**
 * Stops the launcher movement
 * Returns 0 on success, -1 on error
 */
int stop_launcher(int launcher_fd) {
  int ret;
  unsigned char stop_cmd = LAUNCHER_STOP;

  if (launcher_fd < 0)
    return -1;

  ret = write(launcher_fd, &stop_cmd, 1);
  if (ret != 1) {
    perror("Error sending launcher stop command");
    return -1;
  }

  return 0;
}

/**
 * Sets up color thresholds for HSV detection based on the target color
 */
void setup_color_thresholds(TargetColor color, cv::Scalar &lower,
                            cv::Scalar &upper) {
  switch (color) {
  case TARGET_RED:
    // Red is tricky in HSV as it wraps around the hue spectrum
    // Using two ranges and combining them is more accurate
    // This is a simplified version using one range
    lower = cv::Scalar(160, 100, 100);
    upper = cv::Scalar(179, 255, 255);
    break;
  case TARGET_GREEN:
    lower = cv::Scalar(35, 100, 100);
    upper = cv::Scalar(85, 255, 255);
    break;
  case TARGET_BLUE:
    lower = cv::Scalar(100, 100, 100);
    upper = cv::Scalar(140, 255, 255);
    break;
  case TARGET_YELLOW:
    lower = cv::Scalar(20, 100, 100);
    upper = cv::Scalar(30, 255, 255);
    break;
  case TARGET_CYAN:
    lower = cv::Scalar(85, 100, 100);
    upper = cv::Scalar(100, 255, 255);
    break;
  case TARGET_MAGENTA:
    lower = cv::Scalar(140, 100, 100);
    upper = cv::Scalar(160, 255, 255);
    break;
  case TARGET_BLACK:
    // Black is defined by low value in HSV
    lower = cv::Scalar(0, 0, 0);
    upper = cv::Scalar(179, 255, 30);
    break;
  default:
    // Default to red
    lower = cv::Scalar(160, 100, 100);
    upper = cv::Scalar(179, 255, 255);
  }
}

/**
 * Estimates the Z-position (depth) based on the apparent size of the target
 * Returns estimated distance in centimeters
 */
float estimate_z_position(double apparent_diameter_pixels) {
  // Using the pinhole camera model: Z = (F * W) / P
  // Where F is focal length in pixels, W is actual object size, P is apparent object size in pixels
  if (apparent_diameter_pixels <= 0) {
    return MAX_TARGET_DISTANCE_CM;  // Default to max distance if object is too small
  }
  
  float estimated_distance = (FOCAL_LENGTH_PIXELS * TARGET_ACTUAL_DIAMETER_CM) / apparent_diameter_pixels;
  
  // Clamp the estimated distance to reasonable values
  if (estimated_distance < MIN_TARGET_DISTANCE_CM) {
    estimated_distance = MIN_TARGET_DISTANCE_CM;
  } else if (estimated_distance > MAX_TARGET_DISTANCE_CM) {
    estimated_distance = MAX_TARGET_DISTANCE_CM;
  }
  
  return estimated_distance;
}

/**
 * Detects a target using HSV color filtering
 * Returns the center point of the detected target, sets detected flag, and updates estimated_z
 */
cv::Point detect_target_hsv(cv::Mat &frame, TargetColor target_color,
                            bool &detected, float &estimated_z) {
  cv::Scalar lower_thresh, upper_thresh;
  cv::Point target_center(0, 0);
  detected = false;
  estimated_z = MAX_TARGET_DISTANCE_CM;  // Default to max distance

  // Convert the frame from BGR to HSV color space
  cv::Mat hsv_frame;
  cv::cvtColor(frame, hsv_frame, cv::COLOR_BGR2HSV);

  // Set color thresholds based on selected target color
  setup_color_thresholds(target_color, lower_thresh, upper_thresh);

  // Create a binary mask for the selected color range
  cv::Mat mask;
  cv::inRange(hsv_frame, lower_thresh, upper_thresh, mask);

  // Apply morphological operations to clean up the mask
  cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5, 5));
  cv::morphologyEx(mask, mask, cv::MORPH_OPEN, kernel);
  cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, kernel);

  // Find contours in the mask
  std::vector<std::vector<cv::Point>> contours;
  cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

  // Process the found contours to identify potential targets
  if (!contours.empty()) {
    // Find the largest contour (assuming it's the target)
    int largest_idx = 0;
    double largest_area = 0;

    for (size_t i = 0; i < contours.size(); i++) {
      double area = cv::contourArea(contours[i]);

      // Filter out tiny contours (noise)
      if (area > 100 && area > largest_area) {
        largest_area = area;
        largest_idx = i;
      }
    }

    // If we found a sufficiently large contour
    if (largest_area > 100) {
      // Calculate the center of the contour
      cv::Moments moments = cv::moments(contours[largest_idx]);

      if (moments.m00 != 0) {
        target_center.x = int(moments.m10 / moments.m00);
        target_center.y = int(moments.m01 / moments.m00);
        detected = true;

        // Calculate the equivalent diameter for z-position estimation
        double equivalent_diameter = 2 * sqrt(largest_area / M_PI);
        estimated_z = estimate_z_position(equivalent_diameter);

        // Draw the contour and center point (for debugging)
        // cv::drawContours(frame, contours, largest_idx, cv::Scalar(0, 255, 0), 2);
        // cv::circle(frame, target_center, 5, cv::Scalar(0, 0, 255), -1);
      }
    }
  }

  return target_center;
}

/**
 * Detects a target using shape detection (circles/ellipses)
 * This is a fallback method if HSV color detection fails
 * Returns the center point of the detected target, sets detected flag, and updates estimated_z
 */
cv::Point detect_target_shape(cv::Mat &frame, TargetColor target_color,
                              bool &detected, float &estimated_z) {
  cv::Point target_center(0, 0);
  detected = false;
  estimated_z = MAX_TARGET_DISTANCE_CM;  // Default to max distance

  // Convert to grayscale
  cv::Mat gray;
  cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);

  // Apply Gaussian blur to reduce noise
  cv::GaussianBlur(gray, gray, cv::Size(9, 9), 2, 2);

  // Use Hough Circle Transform to detect circles
  std::vector<cv::Vec3f> circles;
  cv::HoughCircles(gray, circles, cv::HOUGH_GRADIENT, 1,
                   gray.rows / 8, // Minimum distance between circles
                   100, 30,       // Canny edge detection parameters
                   10, 100);      // Min and max circle radius

  // Process the found circles
  if (!circles.empty()) {
    // Find the most centered circle (closest to image center)
    float min_distance = FLT_MAX;
    int best_circle = -1;
    cv::Point image_center(frame.cols / 2, frame.rows / 2);

    for (size_t i = 0; i < circles.size(); i++) {
      cv::Point center(cvRound(circles[i][0]), cvRound(circles[i][1]));
      float distance = cv::norm(center - image_center);

      if (distance < min_distance) {
        min_distance = distance;
        best_circle = i;
      }
    }

    if (best_circle >= 0) {
      target_center.x = cvRound(circles[best_circle][0]);
      target_center.y = cvRound(circles[best_circle][1]);
      detected = true;

      // Use the detected circle radius for z-position estimation
      float radius = circles[best_circle][2];
      float diameter = 2.0f * radius;
      estimated_z = estimate_z_position(diameter);

      // Draw the circle (for debugging)
      // cv::circle(frame, target_center, cvRound(circles[best_circle][2]),
      //           cv::Scalar(0, 255, 0), 2);
      // cv::circle(frame, target_center, 3, cv::Scalar(0, 0, 255), -1);
    }
  }

  return target_center;
}

/**
 * Makes adjustments to launcher targeting based on the target's depth
 * Returns 0 on success, -1 on error
 */
int adjust_aim_for_depth(int launcher_fd, float target_z) {
  // Simple adjustment based on depth - adjust trajectory for gravity
  // The further away the target, the more we need to aim higher to compensate
  
  // Skip adjustment for close targets
  if (target_z <= 100.0f) {
    return 0;
  }
  
  // Calculate adjustment time - more adjustment for distant targets
  // This is a simplified model that can be refined with actual testing
  int adjustment_ms = (int)((target_z - 100.0f) * 0.5f);
  
  if (adjustment_ms > 0) {
    printf("Depth adjustment: Moving UP for %d ms to compensate for distance\n", adjustment_ms);
    move_launcher(launcher_fd, LAUNCHER_UP);
    delay_ms(adjustment_ms);
    stop_launcher(launcher_fd);
    return 0;
  }
  
  return 0;
}

/**
 * Aims the launcher at the target coordinates with depth consideration
 * Returns 0 when aiming is complete, -1 on error
 */
int aim_launcher(int launcher_fd, int current_x, int current_y, int target_x,
                 int target_y, float target_z) {
  int dx = target_x - current_x;
  int dy = target_y - current_y;
  int ret;

  // If target is already centered (within dead zone), we're done
  if (abs(dx) < LAUNCHER_DEAD_ZONE && abs(dy) < LAUNCHER_DEAD_ZONE) {
    // Apply depth-based adjustments
    ret = adjust_aim_for_depth(launcher_fd, target_z);
    return ret;
  }

  // Handle horizontal movement first
  if (dx < -LAUNCHER_DEAD_ZONE) {
    // Target is to the left, move left
    printf("Moving launcher LEFT\n");
    ret = move_launcher(launcher_fd, LAUNCHER_LEFT);
    if (ret != 0)
      return -1;

    // Move for a calculated duration based on the distance
    int move_time = (abs(dx) * LAUNCHER_MOVE_TIMEOUT_MS) / FB_WIDTH;
    if (move_time > LAUNCHER_MOVE_TIMEOUT_MS)
      move_time = LAUNCHER_MOVE_TIMEOUT_MS;

    delay_ms(move_time);
    stop_launcher(launcher_fd);
  } else if (dx > LAUNCHER_DEAD_ZONE) {
    // Target is to the right, move right
    printf("Moving launcher RIGHT\n");
    ret = move_launcher(launcher_fd, LAUNCHER_RIGHT);
    if (ret != 0)
      return -1;

    int move_time = (abs(dx) * LAUNCHER_MOVE_TIMEOUT_MS) / FB_WIDTH;
    if (move_time > LAUNCHER_MOVE_TIMEOUT_MS)
      move_time = LAUNCHER_MOVE_TIMEOUT_MS;

    delay_ms(move_time);
    stop_launcher(launcher_fd);
  }

  // Now handle vertical movement
  if (dy < -LAUNCHER_DEAD_ZONE) {
    // Target is above, move up
    printf("Moving launcher UP\n");
    ret = move_launcher(launcher_fd, LAUNCHER_UP);
    if (ret != 0)
      return -1;

    int move_time = (abs(dy) * LAUNCHER_MOVE_TIMEOUT_MS) / FB_HEIGHT;
    if (move_time > LAUNCHER_MOVE_TIMEOUT_MS)
      move_time = LAUNCHER_MOVE_TIMEOUT_MS;

    delay_ms(move_time);
    stop_launcher(launcher_fd);
  } else if (dy > LAUNCHER_DEAD_ZONE) {
    // Target is below, move down
    printf("Moving launcher DOWN\n");
    ret = move_launcher(launcher_fd, LAUNCHER_DOWN);
    if (ret != 0)
      return -1;

    int move_time = (abs(dy) * LAUNCHER_MOVE_TIMEOUT_MS) / FB_HEIGHT;
    if (move_time > LAUNCHER_MOVE_TIMEOUT_MS)
      move_time = LAUNCHER_MOVE_TIMEOUT_MS;

    delay_ms(move_time);
    stop_launcher(launcher_fd);
  }

  // We've moved the launcher, but we might need to fine-tune
  // Wait a little before checking position again
  delay_ms(50);

  // Check if we need to aim again (recursive with a limit to prevent infinite
  // loops)
  static int aim_attempts = 0;
  if (aim_attempts < 5 &&
      (abs(dx) > LAUNCHER_DEAD_ZONE || abs(dy) > LAUNCHER_DEAD_ZONE)) {
    aim_attempts++;
    int result =
        aim_launcher(launcher_fd, current_x, current_y, target_x, target_y, target_z);
    aim_attempts = 0; // Reset attempts counter
    return result;
  }

  // Apply depth-based adjustments (after X-Y positioning is complete)
  ret = adjust_aim_for_depth(launcher_fd, target_z);

  aim_attempts = 0; // Reset attempts counter
  return ret;       // Return the result of the depth adjustment
}

/**
 * Delay function (milliseconds)
 */
void delay_ms(int ms) { usleep(ms * 1000); }