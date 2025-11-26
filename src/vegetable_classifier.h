/*
 * Vegetable Classifier using TensorFlow Lite Micro
 *
 * Handles image classification for vegetables using a trained TFLite model.
 */

#ifndef VEGETABLE_CLASSIFIER_H
#define VEGETABLE_CLASSIFIER_H

#include <Arduino.h>
#include "vegetable_model.h"

// Classification result structure
struct ClassificationResult {
    int classIndex;           // Index of predicted class (0-4)
    const char* className;    // Name of predicted class
    float confidence;         // Confidence score (0.0 - 1.0)
    bool valid;              // Whether classification was successful
};

// Initialize the classifier (call once in setup)
bool classifierInit();

// Classify an image
// imageData: RGB888 image data (width * height * 3 bytes)
// width, height: dimensions of the input image
// Returns classification result
ClassificationResult classifyImage(uint8_t* imageData, int width, int height);

// Get all class probabilities from last classification
// probabilities: array of NUM_CLASSES floats to fill
void getClassProbabilities(float* probabilities);

// Check if model is loaded and ready
bool isModelReady();

// Get model info string
String getModelInfo();

#endif // VEGETABLE_CLASSIFIER_H
