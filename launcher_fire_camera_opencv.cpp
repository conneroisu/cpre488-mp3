#include <fcntl.h>
#include <iostream>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

// OpenCV includes
#include <opencv2/core/core.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/opencv.hpp>

// Launcher commands header
#include "launcher_commands.h"

// Framebuffer dimensions and memory mapping
#define FB_WIDTH 640
#define FB_HEIGHT 480
#define FB_DEPTH 3 // RGB format, 3 bytes per pixel
#define FB_SIZE (FB_WIDTH * FB_HEIGHT * FB_DEPTH)
#define FB_PHYS_ADDR                                                 \
  0x10000000 // Adjust based on your FPGA memory map

// Target recognition parameters
#define MIN_CONTOUR_AREA 500
#define MAX_CONTOUR_AREA 50000
#define MORPH_KERNEL_SIZE 5

// Launcher control parameters
#define LAUNCHER_MOVE_TIMEOUT_MS 1000
#define LAUNCHER_CENTER_X (FB_WIDTH / 2)
#define LAUNCHER_CENTER_Y (FB_HEIGHT / 2)
#define LAUNCHER_DEAD_ZONE 20
#define LAUNCHER_MAX_X_ANGLE 30
#define LAUNCHER_MAX_Y_ANGLE 20

// Z-position (depth) estimation parameters
#define TARGET_ACTUAL_DIAMETER_CM 15.0
#define CAMERA_FOV_HORIZONTAL_DEG 60.0
#define MIN_TARGET_DISTANCE_CM 50.0
#define MAX_TARGET_DISTANCE_CM 300.0
#define FOCAL_LENGTH_PIXELS                                          \
  ((FB_WIDTH * 0.5) /                                                \
   tan((CAMERA_FOV_HORIZONTAL_DEG * 0.5) * M_PI / 180.0))

// Target configuration
enum TargetColor {
  TARGET_RED,
  TARGET_GREEN,
  TARGET_BLUE,
  TARGET_YELLOW,
  TARGET_CYAN,
  TARGET_MAGENTA,
  TARGET_BLACK,
  TARGET_CUSTOM
};

// Default target color - can be changed via command line
TargetColor SELECTED_TARGET = TARGET_RED;

// Structured color threshold definition
struct ColorThreshold {
  cv::Scalar lower;
  cv::Scalar upper;
};

// Detection parameter structure
struct DetectionParams {
  TargetColor targetColor;
  bool useMultiRange;
  ColorThreshold primary;
  ColorThreshold secondary;
  int minArea;
  int maxArea;
  bool enhancedShape;
  float minDistance;
  float maxDistance;
};

// Function prototypes
int open_launcher_device();
int move_launcher(int launcher_fd, unsigned char direction);
int fire_launcher(int launcher_fd);
int stop_launcher(int launcher_fd);
void delay_ms(int ms);
int aim_launcher(int launcher_fd,
                 int current_x,
                 int current_y,
                 int target_x,
                 int target_y,
                 float target_z);
int adjust_aim_for_depth(int launcher_fd, float target_z);
void setup_color_thresholds(TargetColor color,
                            ColorThreshold &primary,
                            ColorThreshold &secondary,
                            bool &useMultiRange);
cv::Point detect_target_hsv(cv::Mat &frame,
                            cv::Mat &debug_frame,
                            DetectionParams &params,
                            bool &detected,
                            float &estimated_z);
cv::Point detect_target_shape(cv::Mat &frame,
                              cv::Mat &debug_frame,
                              DetectionParams &params,
                              bool &detected,
                              float &estimated_z);
void process_frame(cv::Mat &frame,
                   cv::Mat &debug_frame,
                   DetectionParams &params,
                   cv::Point &target_point,
                   bool &detected,
                   float &target_z);
float estimate_z_position(double apparent_diameter_pixels);
void display_help();
std::string get_color_name(TargetColor color);

/**
 * Main function
 */
int main(int argc, char *argv[]) {
  int ret = 0;
  int mem_fd, launcher_fd;
  void *fb_mem;
  bool target_detected = false;
  cv::Point target_point;
  float target_z = MAX_TARGET_DISTANCE_CM;
  int fire_cooldown = 0;
  int fire_threshold =
      5; // Number of consecutive detections before firing
  int consecutive_detections = 0;
  bool debug_mode = false;

  // Process command line arguments
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "-t" && i + 1 < argc) {
      int colorCode = std::stoi(argv[++i]);
      if (colorCode >= 0 && colorCode <= TARGET_CUSTOM) {
        SELECTED_TARGET = static_cast<TargetColor>(colorCode);
      }
    } else if (arg == "-d" || arg == "--debug") {
      debug_mode = true;
    } else if (arg == "-h" || arg == "--help") {
      display_help();
      return 0;
    }
  }

  std::cout << "Starting FPGA Target Recognition and Launcher "
               "Control System..."
            << std::endl;
  std::cout << "Selected target color: "
            << get_color_name(SELECTED_TARGET) << std::endl;

  // Open /dev/mem for framebuffer access
  mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
  if (mem_fd < 0) {
    perror("Failed to open /dev/mem");
    return -1;
  }

  // Map framebuffer memory
  fb_mem = mmap(NULL, FB_SIZE, PROT_READ, MAP_SHARED, mem_fd,
                FB_PHYS_ADDR);
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

  std::cout << "System initialized. Starting target detection loop..."
            << std::endl;

  // Set up detection parameters
  DetectionParams params;
  params.targetColor = SELECTED_TARGET;
  params.minArea = MIN_CONTOUR_AREA;
  params.maxArea = MAX_CONTOUR_AREA;
  params.enhancedShape = true;
  params.minDistance = MIN_TARGET_DISTANCE_CM;
  params.maxDistance = MAX_TARGET_DISTANCE_CM;
  setup_color_thresholds(SELECTED_TARGET, params.primary,
                         params.secondary, params.useMultiRange);

  // Create OpenCV Mat header for the framebuffer
  cv::Mat frame(FB_HEIGHT, FB_WIDTH, CV_8UC3, fb_mem);
  cv::Mat debug_frame;

  // Main detection and targeting loop
  while (1) {
    // Create a copy of the framebuffer data for OpenCV processing
    cv::Mat frame_copy = frame.clone();

    if (debug_mode) {
      debug_frame = frame_copy.clone();
    }

    // Process frame for target detection
    process_frame(frame_copy, debug_mode ? debug_frame : frame_copy,
                  params, target_point, target_detected, target_z);

    if (target_detected) {
      std::cout << "Target detected at position (" << target_point.x
                << ", " << target_point.y << ", " << target_z
                << " cm)" << std::endl;

      consecutive_detections++;

      // Only fire when we have consistent detections to avoid false
      // positives
      if (consecutive_detections >= fire_threshold &&
          fire_cooldown <= 0) {
        // Aim launcher at the target
        ret = aim_launcher(launcher_fd, LAUNCHER_CENTER_X,
                           LAUNCHER_CENTER_Y, target_point.x,
                           target_point.y, target_z);

        if (ret == 0) {
          std::cout << "Target locked, firing!" << std::endl;
          // Fire the launcher
          fire_launcher(launcher_fd);

          // Set cooldown period after firing
          fire_cooldown = 20; // Approx 2 seconds at 100ms loop time
          consecutive_detections = 0;
        }
      }
    } else {
      consecutive_detections =
          0; // Reset consecutive detection counter
    }

    // Decrease fire cooldown counter
    if (fire_cooldown > 0) {
      fire_cooldown--;
    }

    // Add delay to prevent excessive CPU usage
    delay_ms(100);
  }

  // Cleanup (this code would never be reached in the endless loop
  // above)
  stop_launcher(launcher_fd);
  close(launcher_fd);
  munmap(fb_mem, FB_SIZE);
  close(mem_fd);

  return 0;
}

/**
 * Display help information
 */
void display_help() {
  std::cout << "FPGA Target Recognition and Launcher Control System "
               "- Command Line Options:"
            << std::endl;
  std::cout << "-----------------------------------------------------"
               "-------------------"
            << std::endl;
  std::cout << "-t <color>  - Target color (0=red, 1=green, 2=blue, "
               "3=yellow, 4=cyan, 5=magenta, 6=black, 7=custom)"
            << std::endl;
  std::cout << "-d/--debug  - Enable debug mode with visualization"
            << std::endl;
  std::cout << "-h/--help   - Display this help message" << std::endl;
}

/**
 * Process a frame for target detection
 */
void process_frame(cv::Mat &frame,
                   cv::Mat &debug_frame,
                   DetectionParams &params,
                   cv::Point &target_point,
                   bool &detected,
                   float &target_z) {
  detected = false;
  target_z = MAX_TARGET_DISTANCE_CM;

  // Try HSV-based detection first
  target_point = detect_target_hsv(frame, debug_frame, params,
                                   detected, target_z);

  // If HSV detection fails, try shape-based detection
  if (!detected) {
    target_point = detect_target_shape(frame, debug_frame, params,
                                       detected, target_z);
  }

  // Add informational text to debug display
  if (debug_frame.data) {
    std::string status_text;
    if (detected) {
      status_text = "Target: (" + std::to_string(target_point.x) +
                    ", " + std::to_string(target_point.y) + ", " +
                    std::to_string(int(target_z)) + "cm)";
    } else {
      status_text = "No target detected";
    }

    cv::putText(debug_frame, status_text, cv::Point(10, 30),
                cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0),
                2);

    // If target is detected, add a crosshair in debug mode
    if (detected) {
      // Draw a crosshair on the target
      cv::line(debug_frame,
               cv::Point(target_point.x - 20, target_point.y),
               cv::Point(target_point.x + 20, target_point.y),
               cv::Scalar(0, 0, 255), 2);
      cv::line(debug_frame,
               cv::Point(target_point.x, target_point.y - 20),
               cv::Point(target_point.x, target_point.y + 20),
               cv::Scalar(0, 0, 255), 2);

      // Draw circle with radius relative to distance (smaller for
      // further targets)
      int circle_radius = 30;
      if (target_z > 0) {
        circle_radius = int(30 * (200.0f / target_z));
        if (circle_radius < 15)
          circle_radius = 15;
        if (circle_radius > 50)
          circle_radius = 50;
      }
      cv::circle(debug_frame, target_point, circle_radius,
                 cv::Scalar(0, 0, 255), 2);
    }
  }
}

/**
 * Get color name from enum
 */
std::string get_color_name(TargetColor color) {
  switch (color) {
  case TARGET_RED:
    return "Red";
  case TARGET_GREEN:
    return "Green";
  case TARGET_BLUE:
    return "Blue";
  case TARGET_YELLOW:
    return "Yellow";
  case TARGET_CYAN:
    return "Cyan";
  case TARGET_MAGENTA:
    return "Magenta";
  case TARGET_BLACK:
    return "Black";
  case TARGET_CUSTOM:
    return "Custom";
  default:
    return "Unknown";
  }
}

/**
 * Sets up color thresholds for HSV detection based on the target
 * color
 */
void setup_color_thresholds(TargetColor color,
                            ColorThreshold &primary,
                            ColorThreshold &secondary,
                            bool &useMultiRange) {
  useMultiRange = false;

  switch (color) {
  case TARGET_RED:
    useMultiRange = true;
    primary.lower = cv::Scalar(160, 100, 100);
    primary.upper = cv::Scalar(179, 255, 255);
    secondary.lower = cv::Scalar(0, 100, 100);
    secondary.upper = cv::Scalar(10, 255, 255);
    break;
  case TARGET_GREEN:
    primary.lower = cv::Scalar(35, 100, 100);
    primary.upper = cv::Scalar(85, 255, 255);
    break;
  case TARGET_BLUE:
    primary.lower = cv::Scalar(100, 100, 100);
    primary.upper = cv::Scalar(140, 255, 255);
    break;
  case TARGET_YELLOW:
    primary.lower = cv::Scalar(20, 100, 100);
    primary.upper = cv::Scalar(30, 255, 255);
    break;
  case TARGET_CYAN:
    primary.lower = cv::Scalar(85, 100, 100);
    primary.upper = cv::Scalar(100, 255, 255);
    break;
  case TARGET_MAGENTA:
    primary.lower = cv::Scalar(140, 100, 100);
    primary.upper = cv::Scalar(160, 255, 255);
    break;
  case TARGET_BLACK:
    primary.lower = cv::Scalar(0, 0, 0);
    primary.upper = cv::Scalar(179, 255, 30);
    break;
  case TARGET_CUSTOM:
    primary.lower = cv::Scalar(0, 100, 100);
    primary.upper = cv::Scalar(15, 255, 255);
    break;
  default:
    primary.lower = cv::Scalar(160, 100, 100);
    primary.upper = cv::Scalar(179, 255, 255);
  }
}

/**
 * Detects a target using HSV color filtering
 * Returns the center point of the detected target and sets detected
 * flag Also adds visualization to the debug frame and updates
 * estimated_z
 */
cv::Point detect_target_hsv(cv::Mat &frame,
                            cv::Mat &debug_frame,
                            DetectionParams &params,
                            bool &detected,
                            float &estimated_z) {
  cv::Point target_center(0, 0);
  detected = false;
  estimated_z = MAX_TARGET_DISTANCE_CM;

  // Convert the frame from BGR to HSV color space
  cv::Mat hsv_frame;
  cv::cvtColor(frame, hsv_frame, cv::COLOR_BGR2HSV);

  // Create binary masks for the selected color range
  cv::Mat mask1, mask2, mask;
  cv::inRange(hsv_frame, params.primary.lower, params.primary.upper,
              mask1);

  if (params.useMultiRange) {
    cv::inRange(hsv_frame, params.secondary.lower,
                params.secondary.upper, mask2);
    cv::bitwise_or(mask1, mask2, mask);
  } else {
    mask = mask1;
  }

  // Apply morphological operations to clean up the mask
  cv::Mat kernel = cv::getStructuringElement(
      cv::MORPH_ELLIPSE,
      cv::Size(MORPH_KERNEL_SIZE, MORPH_KERNEL_SIZE));
  cv::morphologyEx(mask, mask, cv::MORPH_OPEN, kernel);
  cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, kernel);

  // Find contours in the mask
  std::vector<std::vector<cv::Point>> contours;
  cv::findContours(mask, contours, cv::RETR_EXTERNAL,
                   cv::CHAIN_APPROX_SIMPLE);

  // Process the found contours to identify potential targets
  if (!contours.empty()) {
    // Find the largest contour (assuming it's the target)
    int largest_idx = 0;
    double largest_area = 0;

    for (size_t i = 0; i < contours.size(); i++) {
      double area = cv::contourArea(contours[i]);
      if (area > params.minArea && area < params.maxArea &&
          area > largest_area) {
        largest_area = area;
        largest_idx = i;
      }
    }

    // If we found a sufficiently large contour
    if (largest_area > params.minArea) {
      // Calculate the center of the contour
      cv::Moments moments = cv::moments(contours[largest_idx]);

      if (moments.m00 != 0) {
        target_center.x = int(moments.m10 / moments.m00);
        target_center.y = int(moments.m01 / moments.m00);
        detected = true;

        // Calculate the equivalent diameter for z-position estimation
        double equivalent_diameter = 2 * sqrt(largest_area / M_PI);
        estimated_z = estimate_z_position(equivalent_diameter);

        // Draw the contour and center point for visualization in
        // debug mode
        if (debug_frame.data) {
          cv::drawContours(debug_frame, contours, largest_idx,
                           cv::Scalar(0, 255, 0), 2);
          cv::circle(debug_frame, target_center, 5,
                     cv::Scalar(0, 0, 255), -1);

          // Draw area information
          std::string area_text =
              "Area: " + std::to_string(int(largest_area));
          cv::putText(
              debug_frame, area_text,
              cv::Point(target_center.x + 10, target_center.y + 10),
              cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 0),
              1);
        }
      }
    }
  }

  return target_center;
}

/**
 * Detects a target using shape detection (circles/ellipses)
 * This is a fallback method if HSV color detection fails
 * Returns the center point of the detected target and sets detected
 * flag Also adds visualization to the debug frame and updates
 * estimated_z
 */
cv::Point detect_target_shape(cv::Mat &frame,
                              cv::Mat &debug_frame,
                              DetectionParams &params,
                              bool &detected,
                              float &estimated_z) {
  cv::Point target_center(0, 0);
  detected = false;
  estimated_z = MAX_TARGET_DISTANCE_CM;

  // Convert to grayscale
  cv::Mat gray;
  cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);

  // Apply Gaussian blur to reduce noise
  cv::GaussianBlur(gray, gray, cv::Size(9, 9), 2, 2);

  // Use Hough Circle Transform to detect circles
  std::vector<cv::Vec3f> circles;
  cv::HoughCircles(gray, circles, cv::HOUGH_GRADIENT, 1,
                   gray.rows / 8, 100, 30, 10, 100);

  // Process the found circles
  if (!circles.empty()) {
    // Find the most centered circle (closest to image center)
    float min_distance = FLT_MAX;
    int best_circle = -1;
    cv::Point image_center(frame.cols / 2, frame.rows / 2);

    for (size_t i = 0; i < circles.size(); i++) {
      cv::Point center(cvRound(circles[i][0]),
                       cvRound(circles[i][1]));
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

      // Draw the circle for visualization in debug mode
      if (debug_frame.data) {
        cv::circle(debug_frame, target_center,
                   cvRound(circles[best_circle][2]),
                   cv::Scalar(255, 0, 0), 2);
        cv::circle(debug_frame, target_center, 3,
                   cv::Scalar(0, 0, 255), -1);

        // Draw radius information
        std::string radius_text =
            "Radius: " +
            std::to_string(cvRound(circles[best_circle][2]));
        cv::putText(
            debug_frame, radius_text,
            cv::Point(target_center.x + 10, target_center.y + 30),
            cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 0, 0), 1);
      }
    }
  }

  // If circle detection fails and enhanced shape detection is enabled
  if (!detected && params.enhancedShape) {
    // Use Canny edge detection
    cv::Mat edges;
    cv::Canny(gray, edges, 50, 150);

    // Find contours in the edge image
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(edges, contours, cv::RETR_LIST,
                     cv::CHAIN_APPROX_SIMPLE);

    // Look for circular contours
    double maxCircularity = 0;
    int bestContour = -1;

    for (size_t i = 0; i < contours.size(); i++) {
      double area = cv::contourArea(contours[i]);
      if (area < params.minArea || area > params.maxArea)
        continue;

      double perimeter = cv::arcLength(contours[i], true);
      if (perimeter <= 0)
        continue;

      // Circularity = 4π × Area / Perimeter²
      // Perfect circle has circularity = 1
      double circularity = 4 * M_PI * area / (perimeter * perimeter);

      if (circularity > 0.7 && circularity > maxCircularity) {
        maxCircularity = circularity;
        bestContour = i;
      }
    }

    if (bestContour >= 0) {
      cv::Moments moments = cv::moments(contours[bestContour]);
      if (moments.m00 != 0) {
        target_center.x = int(moments.m10 / moments.m00);
        target_center.y = int(moments.m01 / moments.m00);
        detected = true;

        double area = cv::contourArea(contours[bestContour]);
        double equivalent_diameter = 2 * sqrt(area / M_PI);
        estimated_z = estimate_z_position(equivalent_diameter);

        // Debug visualization
        if (debug_frame.data) {
          cv::drawContours(debug_frame, contours, bestContour,
                           cv::Scalar(255, 0, 255), 2);
          cv::circle(debug_frame, target_center, 5,
                     cv::Scalar(0, 0, 255), -1);

          std::string shape_text =
              "Shape: " + std::to_string(maxCircularity).substr(0, 4);
          cv::putText(
              debug_frame, shape_text,
              cv::Point(target_center.x + 10, target_center.y + 50),
              cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 0, 255),
              1);
        }
      }
    }
  }

  return target_center;
}

/**
 * Estimates the Z-position (depth) based on the apparent size of the
 * target Returns estimated distance in centimeters
 */
float estimate_z_position(double apparent_diameter_pixels) {
  if (apparent_diameter_pixels <= 0) {
    return MAX_TARGET_DISTANCE_CM;
  }

  float estimated_distance =
      (FOCAL_LENGTH_PIXELS * TARGET_ACTUAL_DIAMETER_CM) /
      apparent_diameter_pixels;

  if (estimated_distance < MIN_TARGET_DISTANCE_CM) {
    estimated_distance = MIN_TARGET_DISTANCE_CM;
  } else if (estimated_distance > MAX_TARGET_DISTANCE_CM) {
    estimated_distance = MAX_TARGET_DISTANCE_CM;
  }

  return estimated_distance;
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
  if (launcher_fd < 0)
    return -1;

  int ret = write(launcher_fd, &direction, 1);
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
  if (launcher_fd < 0)
    return -1;

  unsigned char fire_cmd = LAUNCHER_FIRE;
  int ret = write(launcher_fd, &fire_cmd, 1);
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
  if (launcher_fd < 0)
    return -1;

  unsigned char stop_cmd = LAUNCHER_STOP;
  int ret = write(launcher_fd, &stop_cmd, 1);
  if (ret != 1) {
    perror("Error sending launcher stop command");
    return -1;
  }

  return 0;
}

/**
 * Makes adjustments to launcher targeting based on the target's depth
 * Returns 0 on success, -1 on error
 */
int adjust_aim_for_depth(int launcher_fd, float target_z) {
  // Skip adjustment for close targets
  if (target_z <= 100.0f) {
    return 0;
  }

  // Calculate adjustment time - more adjustment for distant targets
  int adjustment_ms = (int)((target_z - 100.0f) * 0.5f);

  if (adjustment_ms > 0) {
    std::cout << "Depth adjustment: Moving UP for " << adjustment_ms
              << " ms to compensate for distance" << std::endl;
    move_launcher(launcher_fd, LAUNCHER_UP);
    delay_ms(adjustment_ms);
    stop_launcher(launcher_fd);
    return 0;
  }

  return 0;
}

/**
 * Aims the launcher at the target coordinates with depth
 * consideration Returns 0 when aiming is complete, -1 on error
 */
int aim_launcher(int launcher_fd,
                 int current_x,
                 int current_y,
                 int target_x,
                 int target_y,
                 float target_z) {
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
    std::cout << "Moving launcher LEFT" << std::endl;
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
    std::cout << "Moving launcher RIGHT" << std::endl;
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
    std::cout << "Moving launcher UP" << std::endl;
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
    std::cout << "Moving launcher DOWN" << std::endl;
    ret = move_launcher(launcher_fd, LAUNCHER_DOWN);
    if (ret != 0)
      return -1;

    int move_time = (abs(dy) * LAUNCHER_MOVE_TIMEOUT_MS) / FB_HEIGHT;
    if (move_time > LAUNCHER_MOVE_TIMEOUT_MS)
      move_time = LAUNCHER_MOVE_TIMEOUT_MS;

    delay_ms(move_time);
    stop_launcher(launcher_fd);
  }

  // Wait a little before checking position again
  delay_ms(50);

  // Check if we need to aim again (recursive with a limit to prevent
  // infinite loops)
  static int aim_attempts = 0;
  if (aim_attempts < 5 && (abs(dx) > LAUNCHER_DEAD_ZONE ||
                           abs(dy) > LAUNCHER_DEAD_ZONE)) {
    aim_attempts++;
    int result = aim_launcher(launcher_fd, current_x, current_y,
                              target_x, target_y, target_z);
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
