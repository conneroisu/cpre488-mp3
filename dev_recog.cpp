/**
 * headless_recognition.cpp
 *
 * Headless version of target recognition system without GUI dependencies.
 * This version focuses on the computer vision aspects of target detection
 * and works without requiring a display or GUI components.
 * Added Z-position (depth) estimation for improved 3D target localization.
 */

#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// OpenCV includes - using only core functionality
#include <opencv2/imgcodecs/imgcodecs.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/opencv.hpp>
#include <opencv2/videoio/videoio.hpp>

// Target configuration
enum TargetColor
{
  TARGET_RED,
  TARGET_GREEN,
  TARGET_BLUE,
  TARGET_YELLOW,
  TARGET_CYAN,
  TARGET_MAGENTA,
  TARGET_BLACK
};

// Selected target color (modify this to test different colors)
#define SELECTED_TARGET TARGET_RED

// Parameters for target detection
#define MIN_CONTOUR_AREA 500
#define MAX_CONTOUR_AREA 50000
#define MORPH_KERNEL_SIZE 5
#define LAUNCHER_DEAD_ZONE 20

// Z-position (depth) estimation parameters
#define TARGET_ACTUAL_DIAMETER_CM 15.0 // Actual target diameter in cm (adjust based on your target)
#define CAMERA_FOV_HORIZONTAL_DEG 60.0 // Camera field of view in degrees
#define DEFAULT_FRAME_WIDTH 640        // Default frame width in pixels
#define DEFAULT_FRAME_HEIGHT 480       // Default frame height in pixels
#define MIN_TARGET_DISTANCE_CM 50.0    // Minimum expected target distance
#define MAX_TARGET_DISTANCE_CM 300.0   // Maximum expected target distance

// Output directory for saved images
#define OUTPUT_DIR "output/"

// Function Prototypes
void setupColorThresholds(TargetColor color, cv::Scalar &lower, cv::Scalar &upper);
cv::Point detectTargetHSV(cv::Mat &frame, cv::Mat &debug_frame, TargetColor target_color,
                          bool &detected, float &estimated_z);
cv::Point detectTargetShape(cv::Mat &frame, cv::Mat &debug_frame, TargetColor target_color,
                            bool &detected, float &estimated_z);
void processFrame(cv::Mat &frame, cv::Mat &debug_frame);
void saveImage(const cv::Mat &image, const std::string &prefix);
void displayHelp();
std::string getColorName(TargetColor color);
void ensureDirectoryExists(const std::string &dirPath);
float estimateZPosition(double apparent_diameter_pixels, int frame_width);
float calculateFocalLength(int frame_width);

/**
 * Main function
 */
int main(int argc, char *argv[])
{
  // Handle command line options
  bool useWebcam = true;
  std::string imageFile = "";
  int cameraID = 0;
  int frames = 10;
  int frameDelay = 500; // milliseconds

  for (int i = 1; i < argc; i++)
  {
    std::string arg = argv[i];
    if (arg == "-i" && i + 1 < argc)
    {
      imageFile = argv[++i];
      useWebcam = false;
    }
    else if (arg == "-c" && i + 1 < argc)
    {
      cameraID = std::stoi(argv[++i]);
    }
    else if (arg == "-f" && i + 1 < argc)
    {
      frames = std::stoi(argv[++i]);
    }
    else if (arg == "-d" && i + 1 < argc)
    {
      frameDelay = std::stoi(argv[++i]);
    }
    else if (arg == "-h" || arg == "--help")
    {
      displayHelp();
      return 0;
    }
  }

  // Ensure output directory exists
  ensureDirectoryExists(OUTPUT_DIR);

  // Setup video capture or image
  cv::VideoCapture cap;
  cv::Mat frame, debug_frame;

  if (useWebcam)
  {
    // Open the webcam
    std::cout << "Opening camera " << cameraID << "..." << std::endl;
    cap.open(cameraID);
    if (!cap.isOpened())
    {
      std::cerr << "Error: Could not open camera " << cameraID << std::endl;
      return -1;
    }

    // Set resolution if needed
    cap.set(cv::CAP_PROP_FRAME_WIDTH, DEFAULT_FRAME_WIDTH);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, DEFAULT_FRAME_HEIGHT);

    std::cout << "Camera opened successfully." << std::endl;
  }
  else
  {
    // Load image from file
    std::cout << "Loading image from: " << imageFile << std::endl;
    frame = cv::imread(imageFile);
    if (frame.empty())
    {
      std::cerr << "Error: Could not open image file: " << imageFile
                << std::endl;
      return -1;
    }

    std::cout << "Image loaded successfully." << std::endl;
  }

  std::cout << "Target Recognition Headless Mode" << std::endl;
  std::cout << "===============================" << std::endl;
  std::cout << "Selected target color: " << getColorName(SELECTED_TARGET)
            << std::endl;
  std::cout << "Output directory: " << OUTPUT_DIR << std::endl;

  if (useWebcam)
  {
    std::cout << "Processing " << frames << " frames from camera..."
              << std::endl;

    for (int i = 0; i < frames; i++)
    {
      // Get new frame from video
      cap >> frame;
      if (frame.empty())
      {
        std::cerr << "Error: Could not read frame from camera" << std::endl;
        break;
      }

      // Create debug frame
      debug_frame = frame.clone();

      // Process the frame
      processFrame(frame, debug_frame);

      // Save the original and debug frames
      std::stringstream ss;
      ss << "frame_" << std::setw(3) << std::setfill('0') << i;
      saveImage(frame, ss.str() + "_original");
      saveImage(debug_frame, ss.str() + "_debug");

      std::cout << "Processed frame " << (i + 1) << "/" << frames << std::endl;

      // Add delay between frames if needed
      if (frameDelay > 0)
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(frameDelay));
      }
    }

    // Clean up
    cap.release();
  }
  else
  {
    // Create debug frame
    debug_frame = frame.clone();

    // Process the single image
    processFrame(frame, debug_frame);

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
 * Calculate focal length in pixels based on frame width and camera FOV
 */
float calculateFocalLength(int frame_width)
{
  return (frame_width * 0.5f) / tan((CAMERA_FOV_HORIZONTAL_DEG * 0.5f) * M_PI / 180.0f);
}

/**
 * Estimates the Z-position (depth) based on the apparent size of the target
 * Returns estimated distance in centimeters
 */
float estimateZPosition(double apparent_diameter_pixels, int frame_width)
{
  // Using the pinhole camera model: Z = (F * W) / P
  // Where F is focal length in pixels, W is actual object size, P is apparent object size in pixels
  if (apparent_diameter_pixels <= 0)
  {
    return MAX_TARGET_DISTANCE_CM; // Default to max distance if object is too small
  }

  // Calculate focal length based on the frame width
  float focal_length_pixels = calculateFocalLength(frame_width);

  // Calculate the estimated distance
  float estimated_distance = (focal_length_pixels * TARGET_ACTUAL_DIAMETER_CM) / apparent_diameter_pixels;

  // Clamp the estimated distance to reasonable values
  if (estimated_distance < MIN_TARGET_DISTANCE_CM)
  {
    estimated_distance = MIN_TARGET_DISTANCE_CM;
  }
  else if (estimated_distance > MAX_TARGET_DISTANCE_CM)
  {
    estimated_distance = MAX_TARGET_DISTANCE_CM;
  }

  return estimated_distance;
}

/**
 * Process a frame for target detection
 */
void processFrame(cv::Mat &frame, cv::Mat &debug_frame)
{
  // Attempt target detection
  bool target_detected = false;
  cv::Point target_point;
  float target_z = MAX_TARGET_DISTANCE_CM; // Default to max distance

  // Try HSV-based detection first
  target_point =
      detectTargetHSV(frame, debug_frame, SELECTED_TARGET, target_detected, target_z);

  // If HSV detection fails, try shape-based detection
  if (!target_detected)
  {
    target_point =
        detectTargetShape(frame, debug_frame, SELECTED_TARGET, target_detected, target_z);
  }

  // Add informational text to display
  std::string status_text;
  if (target_detected)
  {
    status_text = "Target detected at: (" + std::to_string(target_point.x) +
                  ", " + std::to_string(target_point.y) +
                  ", " + std::to_string(int(target_z)) + " cm)";
  }
  else
  {
    status_text = "No target detected";
  }

  cv::putText(debug_frame, status_text, cv::Point(10, 30),
              cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2);

  // If target is detected, add a crosshair
  if (target_detected)
  {
    // Draw a crosshair on the target
    cv::line(debug_frame, cv::Point(target_point.x - 20, target_point.y),
             cv::Point(target_point.x + 20, target_point.y),
             cv::Scalar(0, 0, 255), 2);
    cv::line(debug_frame, cv::Point(target_point.x, target_point.y - 20),
             cv::Point(target_point.x, target_point.y + 20),
             cv::Scalar(0, 0, 255), 2);

    // Draw circle with radius relative to distance (smaller for further targets)
    int circle_radius = 30;
    if (target_z > 0)
    {
      // Adjust circle radius based on distance (inversely proportional)
      circle_radius = int(30 * (200.0f / target_z));
      if (circle_radius < 15)
        circle_radius = 15;
      if (circle_radius > 50)
        circle_radius = 50;
    }
    cv::circle(debug_frame, target_point, circle_radius, cv::Scalar(0, 0, 255), 2);

    // Add depth information to visualization
    std::string depth_text = "Distance: " + std::to_string(int(target_z)) + " cm";
    cv::putText(debug_frame, depth_text,
                cv::Point(target_point.x + 20, target_point.y - 20),
                cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 255, 255), 2);

    // Print target info to console
    std::cout << "Target detected at: (" << target_point.x << ", "
              << target_point.y << ", " << target_z << " cm)" << std::endl;
  }
  else
  {
    std::cout << "No target detected in this frame." << std::endl;
  }
}

/**
 * Sets up color thresholds for HSV detection based on the target color
 */
void setupColorThresholds(
    TargetColor color, //
    cv::Scalar &lower, //
    cv::Scalar &upper  //
)
{
  switch (color)
  {
  case TARGET_RED:
    // Red is tricky in HSV as it wraps around
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
 * Detects a target using HSV color filtering
 * Returns the center point of the detected target and sets detected flag
 * Also adds visualization to the debug frame and updates estimated_z
 */
cv::Point detectTargetHSV(
    cv::Mat &frame,           //
    cv::Mat &debug_frame,     //
    TargetColor target_color, //
    bool &detected,           //
    float &estimated_z        //
)
{
  cv::Scalar lower_thresh, upper_thresh;
  cv::Point target_center(0, 0);
  detected = false;
  estimated_z = MAX_TARGET_DISTANCE_CM; // Default to max distance

  // Convert the frame from BGR to HSV color space
  cv::Mat hsv_frame;
  cv::cvtColor(frame, hsv_frame, cv::COLOR_BGR2HSV);

  // Set color thresholds based on selected target color
  setupColorThresholds(target_color, lower_thresh, upper_thresh);

  // Create a binary mask for the selected color range
  cv::Mat mask;
  cv::inRange(hsv_frame, lower_thresh, upper_thresh, mask);

  // Apply morphological operations to clean up the mask
  cv::Mat kernel = cv::getStructuringElement(
      cv::MORPH_ELLIPSE, cv::Size(MORPH_KERNEL_SIZE, MORPH_KERNEL_SIZE));
  cv::morphologyEx(mask, mask, cv::MORPH_OPEN, kernel);
  cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, kernel);

  // Save the mask for inspection
  saveImage(mask, "mask_hsv");

  // Add a small version of the mask to the debug frame
  cv::Mat small_mask;
  cv::resize(mask, small_mask, cv::Size(mask.cols / 3, mask.rows / 3));
  cv::Mat roi = debug_frame(cv::Rect(10, 50, small_mask.cols, small_mask.rows));

  // Convert mask to BGR for overlay (it's currently grayscale)
  cv::Mat small_mask_bgr;
  cv::cvtColor(small_mask, small_mask_bgr, cv::COLOR_GRAY2BGR);
  small_mask_bgr.copyTo(roi);

  // Add label for the mask
  cv::putText(
      debug_frame,               //
      "HSV Mask",                //
      cv::Point(10, 45),         //
      cv::FONT_HERSHEY_SIMPLEX,  //
      0.5,                       //
      cv::Scalar(255, 255, 255), //
      1                          //
  );

  // Find contours in the mask
  std::vector<std::vector<cv::Point>> contours;
  cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

  // Process the found contours to identify potential targets
  if (!contours.empty())
  {
    // Find the largest contour (assuming it's the target)
    int largest_idx = 0;
    double largest_area = 0;

    for (size_t i = 0; i < contours.size(); i++)
    {
      double area = cv::contourArea(contours[i]);

      // Filter out tiny contours (noise) and very large contours
      if (area > MIN_CONTOUR_AREA && area < MAX_CONTOUR_AREA &&
          area > largest_area)
      {
        largest_area = area;
        largest_idx = i;
      }
    }

    // If we found a sufficiently large contour
    if (largest_area > MIN_CONTOUR_AREA)
    {
      // Calculate the center of the contour
      cv::Moments moments = cv::moments(contours[largest_idx]);

      if (moments.m00 != 0)
      {
        target_center.x = int(moments.m10 / moments.m00);
        target_center.y = int(moments.m01 / moments.m00);
        detected = true;

        // Calculate the equivalent diameter for z-position estimation
        double equivalent_diameter = 2 * sqrt(largest_area / M_PI);
        estimated_z = estimateZPosition(equivalent_diameter, frame.cols);

        // Draw the contour and center point for visualization
        cv::drawContours(
            debug_frame,           //
            contours,              //
            largest_idx,           //
            cv::Scalar(0, 255, 0), //
            2                      //
        );
        cv::circle(debug_frame, target_center, 5, cv::Scalar(0, 0, 255), -1);

        // Draw area information
        std::string area_text = "Area: " + std::to_string(int(largest_area));
        cv::putText(debug_frame, area_text,
                    cv::Point(target_center.x + 10, target_center.y + 10),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 0), 1);

        std::cout << "HSV detection - Contour area: " << largest_area
                  << ", Estimated diameter: " << equivalent_diameter
                  << "px, Distance: " << estimated_z << " cm" << std::endl;
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
                            TargetColor target_color, bool &detected, float &estimated_z)
{
  cv::Point target_center(0, 0);
  detected = false;
  estimated_z = MAX_TARGET_DISTANCE_CM; // Default to max distance

  // Convert to grayscale
  cv::Mat gray;
  cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);

  // Apply Gaussian blur to reduce noise
  cv::GaussianBlur(gray, gray, cv::Size(9, 9), 2, 2);

  // Save the grayscale image for inspection
  saveImage(gray, "grayscale");

  // Add a small version of the grayscale to the debug frame
  cv::Mat small_gray;
  cv::resize(gray, small_gray, cv::Size(gray.cols / 3, gray.rows / 3));
  cv::Mat roi = debug_frame(cv::Rect(10, 50 + small_gray.rows + 10,
                                     small_gray.cols, small_gray.rows));

  // Convert grayscale to BGR for overlay
  cv::Mat small_gray_bgr;
  cv::cvtColor(small_gray, small_gray_bgr, cv::COLOR_GRAY2BGR);
  small_gray_bgr.copyTo(roi);

  // Add label for the grayscale image
  cv::putText(debug_frame, "Grayscale", cv::Point(10, 50 + small_gray.rows + 5),
              cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 255), 1);

  // Use Canny edge detection
  cv::Mat edges;
  cv::Canny(gray, edges, 50, 150);
  saveImage(edges, "edges");

  // Use Hough Circle Transform to detect circles
  std::vector<cv::Vec3f> circles;
  cv::HoughCircles(
      gray,               //
      circles,            //
      cv::HOUGH_GRADIENT, //
      1,                  //
      gray.rows / 8,      // Minimum distance between circles
      100, 30,            // Canny edge detection parameters
      10, 100             // Min and max circle radius
  );

  // Process the found circles
  if (!circles.empty())
  {
    // Find the most centered circle (closest to image center)
    float min_distance = FLT_MAX;
    int best_circle = -1;
    cv::Point image_center(frame.cols / 2, frame.rows / 2);

    for (size_t i = 0; i < circles.size(); i++)
    {
      cv::Point center(cvRound(circles[i][0]), cvRound(circles[i][1]));
      float distance = cv::norm(center - image_center);

      if (distance < min_distance)
      {
        min_distance = distance;
        best_circle = i;
      }
    }

    if (best_circle >= 0)
    {
      target_center.x = cvRound(circles[best_circle][0]);
      target_center.y = cvRound(circles[best_circle][1]);
      detected = true;

      // Use the detected circle radius for z-position estimation
      float radius = circles[best_circle][2];
      float diameter = 2.0f * radius;
      estimated_z = estimateZPosition(diameter, frame.cols);

      // Draw the circle for visualization
      cv::circle(
          debug_frame,                      //
          target_center,                    //
          cvRound(circles[best_circle][2]), //
          cv::Scalar(255, 0, 0),            //
          2                                 //
      );
      cv::circle(
          debug_frame,           //
          target_center,         //
          3,                     //
          cv::Scalar(0, 0, 255), //
          -1                     //
      );

      // Draw radius information
      std::string radius_text = "Radius: " + std::to_string(cvRound(circles[best_circle][2]));
      cv::putText(
          debug_frame,
          radius_text,
          cv::Point(target_center.x + 10,
                    target_center.y + 30), //
          cv::FONT_HERSHEY_SIMPLEX,        //
          0.5,                             //
          cv::Scalar(255, 0, 0),           //
          1                                //
      );

      std::cout << "Shape detection - Circle radius: " << circles[best_circle][2]
                << "px, Distance: " << estimated_z << " cm" << std::endl;
    }
  }

  return target_center;
}

/**
 * Save an image to file with timestamp
 */
void saveImage(const cv::Mat &image, const std::string &prefix)
{
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
void displayHelp()
{
  std::cout << "Headless Target Recognition with Z-Position - Command Line Options:"
            << std::endl;
  std::cout << "---------------------------------------------------------------" << std::endl;
  std::cout << "-i <file>   - Use image file instead of webcam" << std::endl;
  std::cout << "-c <id>     - Specify camera ID (default: 0)" << std::endl;
  std::cout << "-f <num>    - Number of frames to capture (default: 10)"
            << std::endl;
  std::cout
      << "-d <ms>     - Delay between frames in milliseconds (default: 500)"
      << std::endl;
  std::cout << "-h/--help   - Display this help message" << std::endl;
  std::cout << std::endl;
  std::cout << "Example usage:" << std::endl;
  std::cout << "  ./headless_recognition -c 0 -f 20        # Capture 20 frames "
               "from camera 0"
            << std::endl;
  std::cout
      << "  ./headless_recognition -i target.jpg     # Process single image"
      << std::endl;
}

/**
 * Get color name from enum
 */
std::string getColorName(TargetColor color)
{
  switch (color)
  {
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
  default:
    return "Unknown";
  }
}

/**
 * Ensure a directory exists, create if necessary
 */
void ensureDirectoryExists(const std::string &dirPath)
{
  // Simple implementation - system dependent
  // For Linux/Unix
  std::string command = "mkdir -p " + dirPath;
  int result = system(command.c_str());

  if (result != 0)
  {
    std::cerr << "Warning: Could not create directory: " << dirPath
              << std::endl;
  }
}