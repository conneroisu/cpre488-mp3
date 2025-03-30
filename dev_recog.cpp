/**
 * optimized_recognition.cpp
 *
 * Optimized version of target recognition system without GUI dependencies.
 * Improved color detection and target tracking with better Z-position
 * estimation. Support for multiple target colors with adaptive thresholding.
 */

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// OpenCV includes
#include <opencv2/imgcodecs/imgcodecs.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/opencv.hpp>
#include <opencv2/videoio/videoio.hpp>

// Target configuration
enum TargetColor {
  TARGET_RED,
  TARGET_GREEN,
  TARGET_BLUE,
  TARGET_YELLOW,
  TARGET_CYAN,
  TARGET_MAGENTA,
  TARGET_BLACK,
  TARGET_AUTO // Automatic detection based on most prominent color
};

// Global configuration - can be set via command line
TargetColor g_selectedTarget = TARGET_RED;
bool g_enableDebug = true;
bool g_enableAdaptiveThreshold = true;
int g_historyLength = 5; // Number of frames to keep for smoothing

// Parameters for target detection
#define MIN_CONTOUR_AREA 300
#define MAX_CONTOUR_AREA 100000
#define MORPH_KERNEL_SIZE 5
#define CANNY_THRESHOLD_LOW 30
#define CANNY_THRESHOLD_HIGH 150
#define HOUGH_CIRCLE_DP 1.2
#define HOUGH_CIRCLE_MIN_DIST 30
#define HOUGH_CIRCLE_PARAM1 50
#define HOUGH_CIRCLE_PARAM2 30
#define HOUGH_CIRCLE_MIN_RADIUS 10
#define HOUGH_CIRCLE_MAX_RADIUS 300
#define LAUNCHER_DEAD_ZONE 10

// Z-position (depth) estimation parameters
#define TARGET_ACTUAL_DIAMETER_CM 15.0 // Actual target diameter in cm
#define CAMERA_FOV_HORIZONTAL_DEG 60.0 // Camera field of view in degrees
#define DEFAULT_FRAME_WIDTH 640        // Default frame width in pixels
#define DEFAULT_FRAME_HEIGHT 480       // Default frame height in pixels
#define MIN_TARGET_DISTANCE_CM 30.0    // Minimum expected target distance
#define MAX_TARGET_DISTANCE_CM 500.0   // Maximum expected target distance

// Output directory for saved images
#define OUTPUT_DIR "output/"

// Smoothing filter for target tracking
class TargetTracker {
private:
  std::vector<cv::Point> history;
  std::vector<float> zHistory;
  int maxHistory;
  cv::Point lastValidPoint;
  float lastValidZ;
  bool hasValidPoint;

public:
  TargetTracker(int historyLength = 5)
      : maxHistory(historyLength), lastValidPoint(0, 0),
        lastValidZ(MAX_TARGET_DISTANCE_CM), hasValidPoint(false) {}

  void addPoint(const cv::Point &point, float z, bool detected) {
    if (detected) {
      history.push_back(point);
      zHistory.push_back(z);

      if (history.size() > static_cast<size_t>(maxHistory)) {
        history.erase(history.begin());
        zHistory.erase(zHistory.begin());
      }

      lastValidPoint = point;
      lastValidZ = z;
      hasValidPoint = true;
    }
  }

  cv::Point getSmoothedPoint() const {
    if (history.empty()) {
      return hasValidPoint ? lastValidPoint : cv::Point(0, 0);
    }

    int sumX = 0, sumY = 0;
    for (const auto &p : history) {
      sumX += p.x;
      sumY += p.y;
    }
    return cv::Point(sumX / history.size(), sumY / history.size());
  }

  float getSmoothedZ() const {
    if (zHistory.empty()) {
      return hasValidPoint ? lastValidZ : MAX_TARGET_DISTANCE_CM;
    }

    float sumZ = 0;
    for (float z : zHistory) {
      sumZ += z;
    }
    return sumZ / zHistory.size();
  }

  bool isTracking() const { return !history.empty() || hasValidPoint; }

  void reset() {
    history.clear();
    zHistory.clear();
    hasValidPoint = false;
  }
};

// Function Prototypes
void setupColorThresholds(TargetColor color, cv::Scalar &lowerPrimary,
                          cv::Scalar &upperPrimary, cv::Scalar &lowerSecondary,
                          cv::Scalar &upperSecondary, bool &useTwoRanges);
cv::Point detectTargetHSV(cv::Mat &frame, cv::Mat &debug_frame,
                          TargetColor target_color, bool &detected,
                          float &estimated_z, bool adaptiveThreshold);
cv::Point detectTargetShape(cv::Mat &frame, cv::Mat &debug_frame,
                            bool &detected, float &estimated_z);
void processFrame(cv::Mat &frame, cv::Mat &debug_frame, TargetTracker &tracker);
void saveImage(const cv::Mat &image, const std::string &prefix);
void displayHelp();
std::string getColorName(TargetColor color);
void ensureDirectoryExists(const std::string &dirPath);
float estimateZPosition(double apparent_diameter_pixels, int frame_width);
float calculateFocalLength(int frame_width);
void detectDominantColor(const cv::Mat &frame, const cv::Mat &mask,
                         TargetColor &color);
void autoAdjustThresholds(const cv::Mat &hsv_frame, TargetColor color,
                          cv::Scalar &lowerPrimary, cv::Scalar &upperPrimary);
TargetColor parseColorOption(const std::string &colorStr);

/**
 * Main function
 */
int main(int argc, char *argv[]) {
  // Handle command line options
  bool useWebcam = true;
  std::string imageFile = "";
  int cameraID = 0;
  int frames = 100;
  int frameDelay = 500; // milliseconds

  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "-i" && i + 1 < argc) {
      imageFile = argv[++i];
      useWebcam = false;
    } else if (arg == "-c" && i + 1 < argc) {
      cameraID = std::stoi(argv[++i]);
    } else if (arg == "-f" && i + 1 < argc) {
      frames = std::stoi(argv[++i]);
    } else if (arg == "-d" && i + 1 < argc) {
      frameDelay = std::stoi(argv[++i]);
    } else if (arg == "-color" && i + 1 < argc) {
      g_selectedTarget = parseColorOption(argv[++i]);
    } else if (arg == "-adaptive" && i + 1 < argc) {
      g_enableAdaptiveThreshold = (std::stoi(argv[++i]) != 0);
    } else if (arg == "-history" && i + 1 < argc) {
      g_historyLength = std::stoi(argv[++i]);
      if (g_historyLength < 1)
        g_historyLength = 1;
      if (g_historyLength > 30)
        g_historyLength = 30;
    } else if (arg == "-debug" && i + 1 < argc) {
      g_enableDebug = (std::stoi(argv[++i]) != 0);
    } else if (arg == "-h" || arg == "--help") {
      displayHelp();
      return 0;
    }
  }

  // Ensure output directory exists
  ensureDirectoryExists(OUTPUT_DIR);

  // Setup video capture or image
  cv::VideoCapture cap;
  cv::Mat frame, debug_frame;

  // Initialize target tracker
  TargetTracker tracker(g_historyLength);

  if (useWebcam) {
    // Open the webcam
    std::cout << "Opening camera " << cameraID << "..." << std::endl;
    cap.open(cameraID);
    if (!cap.isOpened()) {
      std::cerr << "Error: Could not open camera " << cameraID << std::endl;
      return -1;
    }

    // Set resolution if needed
    cap.set(cv::CAP_PROP_FRAME_WIDTH, DEFAULT_FRAME_WIDTH);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, DEFAULT_FRAME_HEIGHT);

    std::cout << "Camera opened successfully at "
              << cap.get(cv::CAP_PROP_FRAME_WIDTH) << "x"
              << cap.get(cv::CAP_PROP_FRAME_HEIGHT) << std::endl;
  } else {
    // Load image from file
    std::cout << "Loading image from: " << imageFile << std::endl;
    frame = cv::imread(imageFile);
    if (frame.empty()) {
      std::cerr << "Error: Could not open image file: " << imageFile
                << std::endl;
      return -1;
    }

    std::cout << "Image loaded successfully." << std::endl;
  }

  std::cout << "Target Recognition System" << std::endl;
  std::cout << "===============================" << std::endl;
  std::cout << "Selected target color: " << getColorName(g_selectedTarget)
            << std::endl;
  std::cout << "Adaptive thresholding: "
            << (g_enableAdaptiveThreshold ? "Enabled" : "Disabled")
            << std::endl;
  std::cout << "History length: " << g_historyLength << std::endl;
  std::cout << "Debug mode: " << (g_enableDebug ? "Enabled" : "Disabled")
            << std::endl;
  std::cout << "Output directory: " << OUTPUT_DIR << std::endl;

  if (useWebcam) {
    std::cout << "Processing " << frames << " frames from camera..."
              << std::endl;

    for (int i = 0; i < frames; i++) {
      // Get new frame from video
      cap >> frame;
      if (frame.empty()) {
        std::cerr << "Error: Could not read frame from camera" << std::endl;
        break;
      }

      // Create debug frame
      debug_frame = frame.clone();

      // Process the frame
      processFrame(frame, debug_frame, tracker);

      // Save the original and debug frames if debug is enabled
      if (g_enableDebug) {
        std::stringstream ss;
        ss << "frame_" << std::setw(3) << std::setfill('0') << i;
        saveImage(frame, ss.str() + "_original");
        saveImage(debug_frame, ss.str() + "_debug");
      }

      std::cout << "Processed frame " << (i + 1) << "/" << frames << std::endl;

      // Add delay between frames if needed
      if (frameDelay > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(frameDelay));
      }
    }

    // Clean up
    cap.release();
  } else {
    // Create debug frame
    debug_frame = frame.clone();

    // Process the single image
    processFrame(frame, debug_frame, tracker);

    // Save the original and debug images
    saveImage(frame, "image_original");
    saveImage(debug_frame, "image_debug");

    std::cout << "Image processed and saved." << std::endl;
  }

  std::cout << "Processing complete. Results saved to " << OUTPUT_DIR
            << std::endl;

  return 0;
}

/**
 * Parse color option from command line
 */
TargetColor parseColorOption(const std::string &colorStr) {
  if (colorStr == "red")
    return TARGET_RED;
  if (colorStr == "green")
    return TARGET_GREEN;
  if (colorStr == "blue")
    return TARGET_BLUE;
  if (colorStr == "yellow")
    return TARGET_YELLOW;
  if (colorStr == "cyan")
    return TARGET_CYAN;
  if (colorStr == "magenta")
    return TARGET_MAGENTA;
  if (colorStr == "black")
    return TARGET_BLACK;
  if (colorStr == "auto")
    return TARGET_AUTO;

  // Default to red if not recognized
  std::cerr << "Unrecognized color: " << colorStr << ". Using red."
            << std::endl;
  return TARGET_RED;
}

/**
 * Calculate focal length in pixels based on frame width and camera FOV
 */
float calculateFocalLength(int frame_width) {
  return (frame_width * 0.5f) /
         tan((CAMERA_FOV_HORIZONTAL_DEG * 0.5f) * M_PI / 180.0f);
}

/**
 * Estimates the Z-position (depth) based on the apparent size of the target
 * Returns estimated distance in centimeters
 */
float estimateZPosition(double apparent_diameter_pixels, int frame_width) {
  // Using the pinhole camera model: Z = (F * W) / P
  // Where F is focal length in pixels, W is actual object size, P is apparent
  // object size in pixels
  if (apparent_diameter_pixels <= 0) {
    return MAX_TARGET_DISTANCE_CM; // Default to max distance if object is too
                                   // small
  }

  // Calculate focal length based on the frame width
  float focal_length_pixels = calculateFocalLength(frame_width);

  // Calculate the estimated distance
  float estimated_distance = (focal_length_pixels * TARGET_ACTUAL_DIAMETER_CM) /
                             apparent_diameter_pixels;

  // Apply a smoothing function to reduce sudden jumps in distance estimation
  // This is a simple polynomial fit that can be adjusted with real-world
  // calibration
  float calibrated_distance = 0.8f * estimated_distance + 10.0f;

  // Clamp the estimated distance to reasonable values
  if (calibrated_distance < MIN_TARGET_DISTANCE_CM) {
    calibrated_distance = MIN_TARGET_DISTANCE_CM;
  } else if (calibrated_distance > MAX_TARGET_DISTANCE_CM) {
    calibrated_distance = MAX_TARGET_DISTANCE_CM;
  }

  return calibrated_distance;
}

/**
 * Process a frame for target detection
 */
void processFrame(cv::Mat &frame, cv::Mat &debug_frame,
                  TargetTracker &tracker) {
  // Attempt target detection
  bool target_detected = false;
  cv::Point target_point;
  float target_z = MAX_TARGET_DISTANCE_CM; // Default to max distance

  // Try HSV-based detection first
  target_point =
      detectTargetHSV(frame, debug_frame, g_selectedTarget, target_detected,
                      target_z, g_enableAdaptiveThreshold);

  // If HSV detection fails, try shape-based detection
  if (!target_detected) {
    target_point =
        detectTargetShape(frame, debug_frame, target_detected, target_z);
  }

  // Update the tracker with the new detection
  tracker.addPoint(target_point, target_z, target_detected);

  // Get the smoothed position for display and tracking
  cv::Point smoothed_point = tracker.getSmoothedPoint();
  float smoothed_z = tracker.getSmoothedZ();
  bool is_tracking = tracker.isTracking();

  // Add informational text to display
  std::string status_text;
  if (is_tracking) {
    status_text = "Target tracked at: (" + std::to_string(smoothed_point.x) +
                  ", " + std::to_string(smoothed_point.y) + ", " +
                  std::to_string(int(smoothed_z)) + " cm)";

    if (!target_detected) {
      status_text += " [PREDICTED]";
    }
  } else {
    status_text = "No target detected";
  }

  cv::putText(debug_frame, status_text, cv::Point(10, 30),
              cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2);

  // Add color mode text
  std::string color_text = "Target: " + getColorName(g_selectedTarget);
  cv::putText(debug_frame, color_text, cv::Point(10, debug_frame.rows - 10),
              cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(255, 255, 255), 1);

  // If target is being tracked, add a crosshair
  if (is_tracking) {
    // Draw a crosshair on the target
    cv::line(debug_frame, cv::Point(smoothed_point.x - 20, smoothed_point.y),
             cv::Point(smoothed_point.x + 20, smoothed_point.y),
             cv::Scalar(0, 0, 255), 2);
    cv::line(debug_frame, cv::Point(smoothed_point.x, smoothed_point.y - 20),
             cv::Point(smoothed_point.x, smoothed_point.y + 20),
             cv::Scalar(0, 0, 255), 2);

    // Draw circle with radius relative to distance (smaller for further
    // targets)
    int circle_radius = 30;
    if (smoothed_z > 0) {
      // Adjust circle radius based on distance (inversely proportional)
      circle_radius = int(30 * (200.0f / smoothed_z));
      if (circle_radius < 15)
        circle_radius = 15;
      if (circle_radius > 50)
        circle_radius = 50;
    }

    // Use different colors for detected vs predicted
    cv::Scalar circleColor =
        target_detected ? cv::Scalar(0, 0, 255) : cv::Scalar(255, 0, 255);
    cv::circle(debug_frame, smoothed_point, circle_radius, circleColor, 2);

    // Add depth information to visualization
    std::string depth_text =
        "Distance: " + std::to_string(int(smoothed_z)) + " cm";
    cv::putText(debug_frame, depth_text,
                cv::Point(smoothed_point.x + 20, smoothed_point.y - 20),
                cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 255, 255), 2);

    // Print target info to console
    std::cout << "Target tracked at: (" << smoothed_point.x << ", "
              << smoothed_point.y << ", " << smoothed_z << " cm)"
              << (target_detected ? "" : " [PREDICTED]") << std::endl;
  } else {
    std::cout << "No target detected in this frame." << std::endl;
  }
}

/**
 * Sets up color thresholds for HSV detection based on the target color
 * Includes support for colors that wrap around the hue spectrum (like red)
 */
void setupColorThresholds(
    TargetColor color,        // Input: target color
    cv::Scalar &lowerPrimary, // Output: primary lower bound
    cv::Scalar &upperPrimary, // Output: primary upper bound
    cv::Scalar
        &lowerSecondary, // Output: secondary lower bound (for wrapped ranges)
    cv::Scalar
        &upperSecondary, // Output: secondary upper bound (for wrapped ranges)
    bool &useTwoRanges   // Output: whether to use two ranges (for wrapped hues)
) {
  useTwoRanges = false;

  switch (color) {
  case TARGET_RED:
    // Red wraps around in HSV, so we need two ranges
    lowerPrimary = cv::Scalar(160, 100, 100);
    upperPrimary = cv::Scalar(179, 255, 255);
    lowerSecondary = cv::Scalar(0, 100, 100);
    upperSecondary = cv::Scalar(10, 255, 255);
    useTwoRanges = true;
    break;
  case TARGET_GREEN:
    lowerPrimary = cv::Scalar(35, 100, 100);
    upperPrimary = cv::Scalar(85, 255, 255);
    break;
  case TARGET_BLUE:
    lowerPrimary = cv::Scalar(100, 100, 100);
    upperPrimary = cv::Scalar(140, 255, 255);
    break;
  case TARGET_YELLOW:
    lowerPrimary = cv::Scalar(20, 100, 100);
    upperPrimary = cv::Scalar(35, 255, 255);
    break;
  case TARGET_CYAN:
    lowerPrimary = cv::Scalar(85, 100, 100);
    upperPrimary = cv::Scalar(100, 255, 255);
    break;
  case TARGET_MAGENTA:
    lowerPrimary = cv::Scalar(140, 100, 100);
    upperPrimary = cv::Scalar(160, 255, 255);
    break;
  case TARGET_BLACK:
    // Black is defined by low value in HSV
    lowerPrimary = cv::Scalar(0, 0, 0);
    upperPrimary = cv::Scalar(179, 255, 30);
    break;
  default:
    // Default to red
    lowerPrimary = cv::Scalar(160, 100, 100);
    upperPrimary = cv::Scalar(179, 255, 255);
    lowerSecondary = cv::Scalar(0, 100, 100);
    upperSecondary = cv::Scalar(10, 255, 255);
    useTwoRanges = true;
  }
}

/**
 * Auto-adjusts the HSV thresholds based on the image histogram
 * This makes color detection more robust under different lighting conditions
 */
void autoAdjustThresholds(const cv::Mat &hsv_frame, TargetColor color,
                          cv::Scalar &lowerPrimary, cv::Scalar &upperPrimary) {
  // Only adjust saturation and value thresholds, leave hue ranges fixed
  // because they are color-specific
  int channels[] = {1, 2}; // Saturation and Value channels
  int histSize[] = {64, 64};
  float sRanges[] = {0, 256};
  float vRanges[] = {0, 256};
  const float *ranges[] = {sRanges, vRanges};

  cv::Mat hist;
  cv::calcHist(&hsv_frame, 1, channels, cv::Mat(), hist, 2, histSize, ranges,
               true, false);

  // Find the dominant saturation and value ranges
  double minVal, maxVal;
  cv::Point minLoc, maxLoc;
  cv::minMaxLoc(hist, &minVal, &maxVal, &minLoc, &maxLoc);

  // Calculate the total number of pixels (for potential future normalization)
  // double total = hsv_frame.rows * hsv_frame.cols;

  // Adjust thresholds for different lighting conditions
  // For darker scenes, lower the value threshold
  // For less saturated scenes, lower the saturation threshold

  // Get average saturation and value
  cv::Scalar mean = cv::mean(hsv_frame);
  float avgSat = mean[1];
  float avgVal = mean[2];

  if (color != TARGET_BLACK) {
    // For normal color targets
    if (avgVal < 100) {
      // Darker scene
      lowerPrimary[2] =
          std::max(50.0, static_cast<double>(lowerPrimary[2]) - 50.0);
    }

    if (avgSat < 100) {
      // Less saturated scene
      lowerPrimary[1] =
          std::max(50.0, static_cast<double>(lowerPrimary[1]) - 30.0);
    }
  } else {
    // For black targets, we need special handling
    upperPrimary[2] = std::min(50.0, static_cast<double>(avgVal) * 0.5);
  }
}

/**
 * Detect the dominant color in the masked region
 * Used for automatic color detection mode
 */
void detectDominantColor(const cv::Mat &frame, const cv::Mat &mask,
                         TargetColor &color) {
  // Convert to HSV
  cv::Mat hsv_frame;
  cv::cvtColor(frame, hsv_frame, cv::COLOR_BGR2HSV);

  // Apply the mask
  cv::Mat masked_hsv;
  cv::bitwise_and(hsv_frame, hsv_frame, masked_hsv, mask);

  // Calculate average hue in the masked region
  cv::Scalar mean = cv::mean(masked_hsv, mask);
  float avgHue = mean[0];
  float avgSat = mean[1];
  float avgVal = mean[2];

  if (avgVal < 30) {
    color = TARGET_BLACK;
    return;
  }

  if (avgSat < 50) {
    // Low saturation, might be white or gray
    // Let's keep the previous color
    return;
  }

  // Determine color based on hue
  if ((avgHue >= 0 && avgHue <= 10) || (avgHue >= 160 && avgHue <= 179))
    color = TARGET_RED;
  else if (avgHue >= 35 && avgHue <= 85)
    color = TARGET_GREEN;
  else if (avgHue >= 100 && avgHue <= 140)
    color = TARGET_BLUE;
  else if (avgHue >= 20 && avgHue <= 35)
    color = TARGET_YELLOW;
  else if (avgHue >= 85 && avgHue <= 100)
    color = TARGET_CYAN;
  else if (avgHue >= 140 && avgHue <= 160)
    color = TARGET_MAGENTA;
}

/**
 * Detects a target using HSV color filtering
 * Returns the center point of the detected target and sets detected flag
 * Also adds visualization to the debug frame and updates estimated_z
 */
cv::Point detectTargetHSV(
    cv::Mat &frame,           // Input: original frame
    cv::Mat &debug_frame,     // Output: debug visualization
    TargetColor target_color, // Input: color to detect
    bool &detected,           // Output: whether target was detected
    float &estimated_z,       // Output: estimated distance
    bool adaptiveThreshold    // Input: whether to use adaptive thresholds
) {
  cv::Scalar lowerPrimary, upperPrimary, lowerSecondary, upperSecondary;
  bool useTwoRanges = false;
  cv::Point target_center(0, 0);
  detected = false;
  estimated_z = MAX_TARGET_DISTANCE_CM; // Default to max distance

  // Convert the frame from BGR to HSV color space
  cv::Mat hsv_frame;
  cv::cvtColor(frame, hsv_frame, cv::COLOR_BGR2HSV);

  // Set color thresholds based on selected target color
  setupColorThresholds(target_color, lowerPrimary, upperPrimary, lowerSecondary,
                       upperSecondary, useTwoRanges);

  // Auto-adjust thresholds based on lighting if enabled
  if (adaptiveThreshold) {
    autoAdjustThresholds(hsv_frame, target_color, lowerPrimary, upperPrimary);
    if (useTwoRanges) {
      // Also adjust the secondary range
      autoAdjustThresholds(hsv_frame, target_color, lowerSecondary,
                           upperSecondary);
    }
  }

  // Create a binary mask for the selected color range
  cv::Mat mask, mask1, mask2;
  cv::inRange(hsv_frame, lowerPrimary, upperPrimary, mask1);

  if (useTwoRanges) {
    cv::inRange(hsv_frame, lowerSecondary, upperSecondary, mask2);
    cv::bitwise_or(mask1, mask2, mask);
  } else {
    mask = mask1;
  }

  // Apply morphological operations to clean up the mask
  cv::Mat kernel = cv::getStructuringElement(
      cv::MORPH_ELLIPSE, cv::Size(MORPH_KERNEL_SIZE, MORPH_KERNEL_SIZE));
  cv::morphologyEx(mask, mask, cv::MORPH_OPEN, kernel);
  cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, kernel);

  // Save the mask for inspection if debug is enabled
  if (g_enableDebug) {
    saveImage(mask, "mask_hsv");
  }

  // Add a small version of the mask to the debug frame
  if (g_enableDebug) {
    cv::Mat small_mask;
    cv::resize(mask, small_mask, cv::Size(mask.cols / 4, mask.rows / 4));
    cv::Mat roi =
        debug_frame(cv::Rect(10, 50, small_mask.cols, small_mask.rows));

    // Convert mask to BGR for overlay (it's currently grayscale)
    cv::Mat small_mask_bgr;
    cv::cvtColor(small_mask, small_mask_bgr, cv::COLOR_GRAY2BGR);
    small_mask_bgr.copyTo(roi);

    // Add label for the mask
    cv::putText(debug_frame, "HSV Mask", cv::Point(10, 45),
                cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 255), 1);
  }

  // Auto color detection mode
  if (target_color == TARGET_AUTO && !mask.empty()) {
    TargetColor detected_color = TARGET_RED;
    detectDominantColor(frame, mask, detected_color);

    // Update debug frame with detected color
    std::string color_text = "Detected color: " + getColorName(detected_color);
    cv::putText(debug_frame, color_text, cv::Point(10, debug_frame.rows - 30),
                cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(255, 255, 255), 1);
  }

  // Find contours in the mask
  std::vector<std::vector<cv::Point>> contours;
  cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

  // Process the found contours to identify potential targets
  if (!contours.empty()) {
    // Sort contours by area (largest first)
    std::sort(
        contours.begin(), contours.end(),
        [](const std::vector<cv::Point> &c1, const std::vector<cv::Point> &c2) {
          return cv::contourArea(c1) > cv::contourArea(c2);
        });

    // Find suitable contours that match our criteria
    for (size_t i = 0; i < contours.size(); i++) {
      double area = cv::contourArea(contours[i]);

      // Filter out tiny contours (noise) and very large contours
      if (area < MIN_CONTOUR_AREA || area > MAX_CONTOUR_AREA)
        continue;

      // Analyze contour shape to find circular targets
      cv::Rect boundRect = cv::boundingRect(contours[i]);
      double aspect_ratio = boundRect.width / (double)boundRect.height;

      // Check if it's approximately circular/square (aspect ratio close to 1)
      if (aspect_ratio < 0.7 || aspect_ratio > 1.4)
        continue;

      // Fit an ellipse if possible
      if (contours[i].size() >= 5) {
        cv::RotatedRect ellipse = cv::fitEllipse(contours[i]);
        double ellipse_ratio = ellipse.size.width / ellipse.size.height;

        // Check if the ellipse is roughly circular
        if (ellipse_ratio < 0.7 || ellipse_ratio > 1.4)
          continue;
      }

      // Calculate the center of the contour
      cv::Moments moments = cv::moments(contours[i]);

      if (moments.m00 != 0) {
        target_center.x = int(moments.m10 / moments.m00);
        target_center.y = int(moments.m01 / moments.m00);
        detected = true;

        // Calculate the equivalent diameter for z-position estimation
        double equivalent_diameter = 2 * sqrt(area / M_PI);
        estimated_z = estimateZPosition(equivalent_diameter, frame.cols);

        // Draw the contour and center point for visualization
        cv::drawContours(debug_frame, contours, i, cv::Scalar(0, 255, 0), 2);
        cv::circle(debug_frame, target_center, 5, cv::Scalar(0, 0, 255), -1);

        // Draw area information
        std::string area_text = "Area: " + std::to_string(int(area));
        cv::putText(debug_frame, area_text,
                    cv::Point(target_center.x + 10, target_center.y + 10),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 0), 1);

        std::cout << "HSV detection - Contour area: " << area
                  << ", Estimated diameter: " << equivalent_diameter
                  << "px, Distance: " << estimated_z << " cm" << std::endl;

        break; // Found a suitable target, stop processing
      }
    }
  }

  return target_center;
}

/**
 * Detects a target using shape detection (circles/ellipses)
 * This is a fallback method if HSV color detection fails
 * Returns the center point of the detected target and sets detected flag
 * Also adds visualization to the debug frame and updates estimated_z
 */
cv::Point detectTargetShape(cv::Mat &frame, cv::Mat &debug_frame,
                            bool &detected, float &estimated_z) {
  cv::Point target_center(0, 0);
  detected = false;
  estimated_z = MAX_TARGET_DISTANCE_CM; // Default to max distance

  // Convert to grayscale
  cv::Mat gray;
  cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);

  // Apply Gaussian blur to reduce noise
  cv::GaussianBlur(gray, gray, cv::Size(9, 9), 2, 2);

  // Save the grayscale image for inspection if debug is enabled
  if (g_enableDebug) {
    saveImage(gray, "grayscale");
  }

  // Add a small version of the grayscale to the debug frame if debug is enabled
  if (g_enableDebug) {
    cv::Mat small_gray;
    cv::resize(gray, small_gray, cv::Size(gray.cols / 4, gray.rows / 4));
    cv::Mat roi = debug_frame(cv::Rect(10, 50 + small_gray.rows + 10,
                                       small_gray.cols, small_gray.rows));

    // Convert grayscale to BGR for overlay
    cv::Mat small_gray_bgr;
    cv::cvtColor(small_gray, small_gray_bgr, cv::COLOR_GRAY2BGR);
    small_gray_bgr.copyTo(roi);

    // Add label for the grayscale image
    cv::putText(debug_frame, "Grayscale",
                cv::Point(10, 50 + small_gray.rows + 5),
                cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 255), 1);
  }

  // Use Canny edge detection
  cv::Mat edges;
  cv::Canny(gray, edges, CANNY_THRESHOLD_LOW, CANNY_THRESHOLD_HIGH);

  if (g_enableDebug) {
    saveImage(edges, "edges");
  }

  // Use Hough Circle Transform to detect circles
  std::vector<cv::Vec3f> circles;
  cv::HoughCircles(gray, circles, cv::HOUGH_GRADIENT, HOUGH_CIRCLE_DP,
                   HOUGH_CIRCLE_MIN_DIST, // Minimum distance between circles
                   HOUGH_CIRCLE_PARAM1, // Canny edge detection upper threshold
                   HOUGH_CIRCLE_PARAM2, // Accumulator threshold
                   HOUGH_CIRCLE_MIN_RADIUS, // Min circle radius
                   HOUGH_CIRCLE_MAX_RADIUS  // Max circle radius
  );

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
      estimated_z = estimateZPosition(diameter, frame.cols);

      // Draw the circle for visualization
      cv::circle(debug_frame, target_center, cvRound(circles[best_circle][2]),
                 cv::Scalar(255, 0, 0), 2);
      cv::circle(debug_frame, target_center, 3, cv::Scalar(0, 0, 255), -1);

      // Draw radius information
      std::string radius_text =
          "Radius: " + std::to_string(cvRound(circles[best_circle][2]));
      cv::putText(debug_frame, radius_text,
                  cv::Point(target_center.x + 10, target_center.y + 30),
                  cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 0, 0), 1);

      std::cout << "Shape detection - Circle radius: "
                << circles[best_circle][2] << "px, Distance: " << estimated_z
                << " cm" << std::endl;
    }
  }

  return target_center;
}

/**
 * Save an image to file with timestamp
 */
void saveImage(const cv::Mat &image, const std::string &prefix) {
  // Generate timestamp
  auto now = std::chrono::system_clock::now();
  auto time_t_now = std::chrono::system_clock::to_time_t(now);
  std::stringstream timestamp;
  timestamp << std::put_time(std::localtime(&time_t_now), "%Y%m%d-%H%M%S");

  // Create filename
  std::string filename = OUTPUT_DIR + prefix + "_" + timestamp.str() + ".jpg";

  // Save image
  cv::imwrite(filename, image);
}

/**
 * Display help information
 */
void displayHelp() {
  std::cout << "Optimized Target Recognition - Command Line Options:"
            << std::endl;
  std::cout << "---------------------------------------------------------------"
            << std::endl;
  std::cout << "-i <file>     - Use image file instead of webcam" << std::endl;
  std::cout << "-c <id>       - Specify camera ID (default: 0)" << std::endl;
  std::cout << "-f <num>      - Number of frames to capture (default: 10)"
            << std::endl;
  std::cout
      << "-d <ms>       - Delay between frames in milliseconds (default: 500)"
      << std::endl;
  std::cout << "-color <name> - Set target color "
               "(red/green/blue/yellow/cyan/magenta/black/auto)"
            << std::endl;
  std::cout
      << "-adaptive <0|1> - Enable/disable adaptive thresholds (default: 1)"
      << std::endl;
  std::cout << "-history <num> - Set history length for smoothing (default: 5)"
            << std::endl;
  std::cout << "-debug <0|1>  - Enable/disable debug mode (default: 1)"
            << std::endl;
  std::cout << "-h/--help     - Display this help message" << std::endl;
  std::cout << std::endl;
  std::cout << "Example usage:" << std::endl;
  std::cout << "  ./target_recognition -c 0 -f 20 -color red       # Capture "
               "20 frames looking for red targets"
            << std::endl;
  std::cout << "  ./target_recognition -i target.jpg -color auto   # Process "
               "image with auto color detection"
            << std::endl;
}

/**
 * Get color name from enum
 */
std::string getColorName(TargetColor color) {
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
  case TARGET_AUTO:
    return "Auto";
  default:
    return "Unknown";
  }
}

/**
 * Ensure a directory exists, create if necessary
 */
void ensureDirectoryExists(const std::string &dirPath) {
  // Simple implementation - system dependent
  // For Linux/Unix
  std::string command = "mkdir -p " + dirPath;
  int result = system(command.c_str());

  if (result != 0) {
    std::cerr << "Warning: Could not create directory: " << dirPath
              << std::endl;
  }
}
