/*
 * Vegetable Classification Model
 *
 * Trained with Google Teachable Machine
 * Model Input: 224x224 RGB image (Teachable Machine default)
 * Model Output: 6 classes probability
 */

#ifndef VEGETABLE_MODEL_H
#define VEGETABLE_MODEL_H

// Include the converted model data
#include "model_data.h"

// Number of vegetable classes
#define NUM_CLASSES 6

// Class labels (must match training order from labels.txt)
// 0 Eggplant, 1 Lemon, 2 Cucumber, 3 Tomato, 4 Onion, 5 None
extern const char* VEGETABLE_LABELS[NUM_CLASSES];

// Model input dimensions (Teachable Machine default is 224x224)
#define MODEL_INPUT_WIDTH 224
#define MODEL_INPUT_HEIGHT 224
#define MODEL_INPUT_CHANNELS 3

// Reference to model data from model_data.h
#define vegetable_model_tflite vegetable_model_data
#define vegetable_model_tflite_len vegetable_model_data_len

// Model is now real (not placeholder)
#define MODEL_IS_PLACEHOLDER false

#endif // VEGETABLE_MODEL_H
