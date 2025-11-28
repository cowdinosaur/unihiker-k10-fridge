/*
 * Vegetable Classifier Implementation
 *
 * Uses TensorFlow Lite Micro for on-device inference.
 * Handles quantized (int8/uint8) models from Teachable Machine.
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

#include <Chirale_TensorFlowLite.h>
#include <tensorflow/lite/micro/all_ops_resolver.h>
#include <tensorflow/lite/micro/micro_interpreter.h>
#include <tensorflow/lite/schema/schema_generated.h>

// Tensor arena size - allocate in PSRAM to handle large models
#define TENSOR_ARENA_SIZE (1300 * 1024)  // 1.3MB

// TFLite globals
static tflite::AllOpsResolver tflOpsResolver;
static const tflite::Model* tflModel = nullptr;
static tflite::MicroInterpreter* tflInterpreter = nullptr;
static TfLiteTensor* inputTensor = nullptr;
static TfLiteTensor* outputTensor = nullptr;
static uint8_t* tensor_arena = nullptr;

#endif // !MODEL_IS_PLACEHOLDER

bool classifierInit() {
    Serial.println("Initializing vegetable classifier...");

    #if MODEL_IS_PLACEHOLDER
    Serial.println("WARNING: Using placeholder model!");
    modelReady = false;
    return true;
    #else

    // Allocate tensor arena in PSRAM
    tensor_arena = (uint8_t*)ps_malloc(TENSOR_ARENA_SIZE);
    if (tensor_arena == nullptr) {
        Serial.println("Failed to allocate tensor arena in PSRAM!");
        return false;
    }
    Serial.printf("Allocated %d KB tensor arena in PSRAM\n", TENSOR_ARENA_SIZE / 1024);

    // Load the model
    tflModel = tflite::GetModel(vegetable_model_tflite);
    if (tflModel->version() != TFLITE_SCHEMA_VERSION) {
        Serial.printf("Model schema mismatch: %d vs %d\n", tflModel->version(), TFLITE_SCHEMA_VERSION);
        return false;
    }

    // Create interpreter
    tflInterpreter = new tflite::MicroInterpreter(tflModel, tflOpsResolver, tensor_arena, TENSOR_ARENA_SIZE);

    // Allocate tensors
    if (tflInterpreter->AllocateTensors() != kTfLiteOk) {
        Serial.println("Failed to allocate tensors!");
        return false;
    }

    // Get input/output tensors
    inputTensor = tflInterpreter->input(0);
    outputTensor = tflInterpreter->output(0);

    // Print tensor info for debugging
    Serial.printf("Input tensor: type=%d, dims=[%d,%d,%d,%d], bytes=%d\n",
                  inputTensor->type,
                  inputTensor->dims->data[0], inputTensor->dims->data[1],
                  inputTensor->dims->data[2], inputTensor->dims->data[3],
                  inputTensor->bytes);
    Serial.printf("Output tensor: type=%d, dims=[%d,%d], bytes=%d\n",
                  outputTensor->type,
                  outputTensor->dims->data[0], outputTensor->dims->data[1],
                  outputTensor->bytes);

    // Print quantization params if quantized
    if (inputTensor->type == kTfLiteUInt8 || inputTensor->type == kTfLiteInt8) {
        Serial.printf("Input quant: scale=%.6f, zero_point=%d\n",
                      inputTensor->params.scale, inputTensor->params.zero_point);
    }
    if (outputTensor->type == kTfLiteUInt8 || outputTensor->type == kTfLiteInt8) {
        Serial.printf("Output quant: scale=%.6f, zero_point=%d\n",
                      outputTensor->params.scale, outputTensor->params.zero_point);
    }

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

    // Prepare input - resize image to model input size
    float xRatio = (float)width / MODEL_INPUT_WIDTH;
    float yRatio = (float)height / MODEL_INPUT_HEIGHT;

    unsigned long startTime = millis();

    // Fill input tensor based on its type
    if (inputTensor->type == kTfLiteUInt8) {
        // Quantized uint8 input (0-255 maps to 0-255, typically)
        uint8_t* inputData = inputTensor->data.uint8;
        int idx = 0;
        for (int y = 0; y < MODEL_INPUT_HEIGHT; y++) {
            for (int x = 0; x < MODEL_INPUT_WIDTH; x++) {
                int srcX = (int)(x * xRatio);
                int srcY = (int)(y * yRatio);
                int srcIdx = (srcY * width + srcX) * 3;
                inputData[idx++] = imageData[srcIdx];       // R
                inputData[idx++] = imageData[srcIdx + 1];   // G
                inputData[idx++] = imageData[srcIdx + 2];   // B
            }
        }
    } else if (inputTensor->type == kTfLiteInt8) {
        // Quantized int8 input (0-255 maps to -128 to 127)
        int8_t* inputData = inputTensor->data.int8;
        int idx = 0;
        for (int y = 0; y < MODEL_INPUT_HEIGHT; y++) {
            for (int x = 0; x < MODEL_INPUT_WIDTH; x++) {
                int srcX = (int)(x * xRatio);
                int srcY = (int)(y * yRatio);
                int srcIdx = (srcY * width + srcX) * 3;
                inputData[idx++] = (int8_t)(imageData[srcIdx] - 128);       // R
                inputData[idx++] = (int8_t)(imageData[srcIdx + 1] - 128);   // G
                inputData[idx++] = (int8_t)(imageData[srcIdx + 2] - 128);   // B
            }
        }
    } else if (inputTensor->type == kTfLiteFloat32) {
        // Float input (normalize to 0-1)
        float* inputData = inputTensor->data.f;
        int idx = 0;
        for (int y = 0; y < MODEL_INPUT_HEIGHT; y++) {
            for (int x = 0; x < MODEL_INPUT_WIDTH; x++) {
                int srcX = (int)(x * xRatio);
                int srcY = (int)(y * yRatio);
                int srcIdx = (srcY * width + srcX) * 3;
                inputData[idx++] = imageData[srcIdx] / 255.0f;       // R
                inputData[idx++] = imageData[srcIdx + 1] / 255.0f;   // G
                inputData[idx++] = imageData[srcIdx + 2] / 255.0f;   // B
            }
        }
    }

    // Run inference
    if (tflInterpreter->Invoke() != kTfLiteOk) {
        Serial.println("Inference failed!");
        return result;
    }

    unsigned long inferenceTime = millis() - startTime;
    Serial.printf("Inference time: %lu ms\n", inferenceTime);

    // Read outputs based on tensor type
    float output[NUM_CLASSES];
    int numOutputs = outputTensor->dims->data[1];  // Second dimension is num classes

    if (outputTensor->type == kTfLiteUInt8) {
        uint8_t* outputData = outputTensor->data.uint8;
        float scale = outputTensor->params.scale;
        int zeroPoint = outputTensor->params.zero_point;
        for (int i = 0; i < numOutputs && i < NUM_CLASSES; i++) {
            output[i] = (outputData[i] - zeroPoint) * scale;
        }
    } else if (outputTensor->type == kTfLiteInt8) {
        int8_t* outputData = outputTensor->data.int8;
        float scale = outputTensor->params.scale;
        int zeroPoint = outputTensor->params.zero_point;
        for (int i = 0; i < numOutputs && i < NUM_CLASSES; i++) {
            output[i] = (outputData[i] - zeroPoint) * scale;
        }
    } else if (outputTensor->type == kTfLiteFloat32) {
        float* outputData = outputTensor->data.f;
        for (int i = 0; i < numOutputs && i < NUM_CLASSES; i++) {
            output[i] = outputData[i];
        }
    }

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
