/*
 * Vegetable Classifier Implementation
 *
 * Uses EloquentTinyML for on-device inference.
 */

#include "vegetable_classifier.h"
#include "vegetable_model.h"

// Define the class labels (declared extern in header)
// Order must match labels.txt: 0=Eggplant, 1=Lemon, 2=Cucumber, 3=Tomato, 4=Onion, 5=None
const char* VEGETABLE_LABELS[NUM_CLASSES] = {
    "eggplant",
    "lemon",
    "cucumber",
    "tomato",
    "onion",
    "none"
};

// Store last classification probabilities
float lastProbabilities[NUM_CLASSES] = {0};
bool modelReady = false;

#if !MODEL_IS_PLACEHOLDER

#include <EloquentTinyML.h>

// Model input: 224x224x3 = 150528 floats
// Model output: 6 classes
#define NUMBER_OF_INPUTS  (MODEL_INPUT_WIDTH * MODEL_INPUT_HEIGHT * MODEL_INPUT_CHANNELS)
#define NUMBER_OF_OUTPUTS NUM_CLASSES
// Tensor arena size - needs to be large enough for the model
#define TENSOR_ARENA_SIZE (256 * 1024)

Eloquent::TinyML::TfLite<NUMBER_OF_INPUTS, NUMBER_OF_OUTPUTS, TENSOR_ARENA_SIZE> ml;

#endif // !MODEL_IS_PLACEHOLDER

bool classifierInit() {
    Serial.println("Initializing vegetable classifier...");

    #if MODEL_IS_PLACEHOLDER
    Serial.println("WARNING: Using placeholder model!");
    Serial.println("Please train and export a real model from Teachable Machine.");
    modelReady = false;
    return true;
    #else

    // Load the model
    bool loaded = ml.begin(vegetable_model_tflite);
    if (!loaded) {
        Serial.println("Failed to load model!");
        return false;
    }

    Serial.println("Model loaded successfully");
    Serial.printf("Input size: %d x %d x %d\n", MODEL_INPUT_WIDTH, MODEL_INPUT_HEIGHT, MODEL_INPUT_CHANNELS);
    Serial.printf("Output classes: %d\n", NUM_CLASSES);

    modelReady = true;
    Serial.println("Classifier initialized successfully!");
    return true;
    #endif
}

ClassificationResult classifyImage(uint8_t* imageData, int width, int height) {
    ClassificationResult result = {-1, "unknown", 0.0f, false};

    #if MODEL_IS_PLACEHOLDER
    Serial.println("Cannot classify: placeholder model loaded");
    return result;
    #else

    if (!modelReady) {
        Serial.println("Classifier not initialized!");
        return result;
    }

    // Prepare input - resize and normalize image to float array
    float* inputBuffer = new float[NUMBER_OF_INPUTS];
    if (!inputBuffer) {
        Serial.println("Failed to allocate input buffer!");
        return result;
    }

    float xRatio = (float)width / MODEL_INPUT_WIDTH;
    float yRatio = (float)height / MODEL_INPUT_HEIGHT;

    int idx = 0;
    for (int y = 0; y < MODEL_INPUT_HEIGHT; y++) {
        for (int x = 0; x < MODEL_INPUT_WIDTH; x++) {
            int srcX = (int)(x * xRatio);
            int srcY = (int)(y * yRatio);
            int srcIdx = (srcY * width + srcX) * 3;

            // Normalize to 0-1 range
            inputBuffer[idx++] = imageData[srcIdx] / 255.0f;
            inputBuffer[idx++] = imageData[srcIdx + 1] / 255.0f;
            inputBuffer[idx++] = imageData[srcIdx + 2] / 255.0f;
        }
    }

    // Run inference
    unsigned long startTime = millis();
    float output[NUMBER_OF_OUTPUTS];
    ml.predict(inputBuffer, output);
    unsigned long inferenceTime = millis() - startTime;

    delete[] inputBuffer;

    Serial.printf("Inference time: %lu ms\n", inferenceTime);

    // Find max probability
    int maxIdx = 0;
    float maxProb = -999.0f;

    for (int i = 0; i < NUM_CLASSES; i++) {
        lastProbabilities[i] = output[i];
        Serial.printf("  %s: %.1f%%\n", VEGETABLE_LABELS[i], output[i] * 100);

        if (output[i] > maxProb) {
            maxProb = output[i];
            maxIdx = i;
        }
    }

    result.classIndex = maxIdx;
    result.className = VEGETABLE_LABELS[maxIdx];
    result.confidence = maxProb;
    result.valid = true;

    Serial.printf("Result: %s (%.1f%%)\n", result.className, result.confidence * 100);

    return result;
    #endif
}

void getClassProbabilities(float* probabilities) {
    for (int i = 0; i < NUM_CLASSES; i++) {
        probabilities[i] = lastProbabilities[i];
    }
}

bool isModelReady() {
    return modelReady;
}

String getModelInfo() {
    #if MODEL_IS_PLACEHOLDER
    return "Placeholder (train model)";
    #else
    if (!modelReady) return "Not loaded";
    return String("Ready ") + MODEL_INPUT_WIDTH + "x" + MODEL_INPUT_HEIGHT;
    #endif
}
