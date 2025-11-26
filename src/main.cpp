#include "unihiker_k10.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "vegetable_classifier.h"

UNIHIKER_K10 k10;
uint8_t screen_dir = 0;  // 0=0°, 1=90°, 2=180°, 3=270°

// WiFi credentials
const char* ssid = "YSC2025";
const char* password = "##ysc2025";

// API server address
const char* serverUrl = "https://sustainhub.dev.tk.sg/api";

// App modes
enum AppMode {
    MODE_INVENTORY,    // View fridge inventory
    MODE_SCANNER,      // Camera view for scanning vegetables
    MODE_CAPTURE       // Capture training images
};

AppMode currentMode = MODE_INVENTORY;

// Ingredient data structure
struct Ingredient {
    String name;
    int quantity;
    int daysLeft;
};

// Dynamic ingredients array
Ingredient ingredients[10];
int numIngredients = 0;
bool dataLoaded = false;

// Training capture variables
int captureClassIndex = 0;  // Which vegetable class to capture
int captureCount = 0;       // Number of images captured for current class

// Camera frame buffer
extern camera_fb_t* fb;

// Calculate days until expiry
int calculateDaysLeft(String expiryDateStr) {
    int year = expiryDateStr.substring(0, 4).toInt();
    int month = expiryDateStr.substring(5, 7).toInt();
    int day = expiryDateStr.substring(8, 10).toInt();

    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);

    struct tm expiry = {0};
    expiry.tm_year = year - 1900;
    expiry.tm_mon = month - 1;
    expiry.tm_mday = day;

    time_t expiryTime = mktime(&expiry);
    int daysLeft = (expiryTime - now) / 86400;

    return daysLeft;
}

// Get color based on days left
uint32_t getExpiryColor(int daysLeft) {
    if (daysLeft <= 3) return 0xFF0000;
    else if (daysLeft <= 5) return 0xFFAA00;
    else if (daysLeft <= 7) return 0xFFFF00;
    else return 0x00AA00;
}

// Fetch ingredients from API
bool fetchIngredients() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi not connected");
        return false;
    }

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(15000);

    HTTPClient http;
    String url = String(serverUrl) + "/ingredients";

    if (!http.begin(client, url)) {
        Serial.println("Failed to initialize HTTP connection");
        return false;
    }

    http.setTimeout(15000);
    int httpResponseCode = http.GET();

    if (httpResponseCode == 200) {
        String payload = http.getString();

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);

        if (error) {
            http.end();
            return false;
        }

        numIngredients = 0;
        JsonArray array = doc.as<JsonArray>();
        for (JsonObject ing : array) {
            if (numIngredients >= 10) break;

            ingredients[numIngredients].name = ing["name"].as<String>();
            ingredients[numIngredients].quantity = ing["quantity"].as<int>();

            String expiryDate = ing["expiry_date"].as<String>();
            ingredients[numIngredients].daysLeft = calculateDaysLeft(expiryDate);

            numIngredients++;
        }

        http.end();
        return true;
    }

    http.end();
    return false;
}

// Add ingredient to inventory via API
bool addIngredientToAPI(const char* name, int quantity) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi not connected");
        return false;
    }

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(15000);

    HTTPClient http;
    String url = String(serverUrl) + "/ingredients";

    if (!http.begin(client, url)) {
        return false;
    }

    http.addHeader("Content-Type", "application/json");

    // Calculate expiry date (7 days from now for vegetables)
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    timeinfo->tm_mday += 7;
    mktime(timeinfo);
    char expiryDate[11];
    strftime(expiryDate, 11, "%Y-%m-%d", timeinfo);

    JsonDocument doc;
    doc["name"] = name;
    doc["category"] = "vegetable";
    doc["quantity"] = quantity;
    doc["unit"] = "pieces";
    doc["expiry_date"] = expiryDate;

    String payload;
    serializeJson(doc, payload);

    int httpResponseCode = http.POST(payload);
    http.end();

    if (httpResponseCode == 200 || httpResponseCode == 201) {
        Serial.printf("Added %s to inventory\n", name);
        return true;
    }

    Serial.printf("Failed to add ingredient: %d\n", httpResponseCode);
    return false;
}

// Draw inventory UI
void drawInventoryUI() {
    k10.canvas->canvasClear();
    k10.setScreenBackground(0xFFFFFF);

    // Border
    k10.canvas->canvasRectangle(2, 2, 236, 316, 0x0080FF, 0x0080FF, false);
    k10.canvas->canvasRectangle(3, 3, 234, 314, 0x0080FF, 0x0080FF, false);

    // Header
    k10.canvas->canvasText("Fridge Manager", 1, 0x000000);

    if (!dataLoaded || numIngredients == 0) {
        k10.canvas->canvasText("Loading...", 3, 0x666666);
        k10.canvas->updateCanvas();
        return;
    }

    k10.canvas->canvasText("qty -> days left", 2, 0x666666);

    int displayCount = min(numIngredients, 5);
    for (int i = 0; i < displayCount; i++) {
        Ingredient &ing = ingredients[i];
        uint32_t color = getExpiryColor(ing.daysLeft);

        char line[50];
        snprintf(line, 50, "%s:%d->%dd", ing.name.c_str(), ing.quantity, ing.daysLeft);
        k10.canvas->canvasText(line, 3 + i, color);
    }

    // Footer
    k10.canvas->canvasText("A:Capture B:Scan", 8, 0x0080FF);

    k10.canvas->updateCanvas();
}

// Draw scanner UI
void drawScannerUI(const char* status = "Point at vegetable") {
    k10.canvas->canvasClear();
    k10.setScreenBackground(0x000000);

    // Header
    k10.canvas->canvasText("SCANNER MODE", 1, 0x00FF00);
    k10.canvas->canvasText(status, 2, 0xFFFFFF);

    // Model status
    String modelInfo = "Model: " + getModelInfo();
    k10.canvas->canvasText(modelInfo.c_str(), 7, 0x888888);

    // Instructions
    k10.canvas->canvasText("B:Scan A:Back", 8, 0x00FF00);

    k10.canvas->updateCanvas();
}

// Draw capture UI for training data collection
void drawCaptureUI() {
    k10.canvas->canvasClear();
    k10.setScreenBackground(0x000000);

    // Header
    k10.canvas->canvasText("CAPTURE MODE", 1, 0xFF8800);

    // Current class
    char classInfo[40];
    snprintf(classInfo, 40, "Class: %s", VEGETABLE_LABELS[captureClassIndex]);
    k10.canvas->canvasText(classInfo, 2, 0xFFFFFF);

    // Capture count
    char countInfo[30];
    snprintf(countInfo, 30, "Captured: %d", captureCount);
    k10.canvas->canvasText(countInfo, 3, 0x00FF00);

    // Instructions
    k10.canvas->canvasText("A:Capture B:Next", 7, 0xFF8800);
    k10.canvas->canvasText("A+B:Back", 8, 0xFF8800);

    k10.canvas->updateCanvas();
}

// Draw classification result
void drawResultUI(ClassificationResult& result) {
    k10.canvas->canvasClear();
    k10.setScreenBackground(0x000000);

    k10.canvas->canvasText("DETECTED:", 1, 0x00FF00);

    if (result.valid) {
        // Show detected vegetable
        char vegName[30];
        snprintf(vegName, 30, "%s", result.className);
        k10.canvas->canvasText(vegName, 3, 0xFFFFFF);

        // Show confidence
        char confStr[30];
        snprintf(confStr, 30, "Conf: %.1f%%", result.confidence * 100);
        k10.canvas->canvasText(confStr, 4, 0x888888);

        k10.canvas->canvasText("Adding to inventory...", 6, 0x00FF00);
    } else {
        k10.canvas->canvasText("Not recognized", 3, 0xFF0000);
        k10.canvas->canvasText("Try again", 4, 0x888888);
    }

    k10.canvas->updateCanvas();
}

// Capture image for training data
void captureTrainingImage() {
    // Initialize SD card if not done
    static bool sdInitialized = false;
    if (!sdInitialized) {
        k10.initSDFile();
        sdInitialized = true;
    }

    // Create directory for class if needed
    char dirPath[50];
    snprintf(dirPath, 50, "/training/%s", VEGETABLE_LABELS[captureClassIndex]);

    if (!SD.exists("/training")) {
        SD.mkdir("/training");
    }
    if (!SD.exists(dirPath)) {
        SD.mkdir(dirPath);
    }

    // Save image
    char imagePath[80];
    snprintf(imagePath, 80, "/training/%s/img_%03d.jpg",
             VEGETABLE_LABELS[captureClassIndex], captureCount);

    k10.photoSaveToTFCard(imagePath);

    captureCount++;
    Serial.printf("Captured: %s\n", imagePath);

    // Flash RGB to confirm
    k10.rgb->write(0, 0, 255, 0);  // Green flash
    delay(200);
    k10.rgb->write(0, 0, 0, 0);
}

// Scan and classify vegetable
void scanVegetable() {
    drawScannerUI("Scanning...");

    // TODO: Get camera frame buffer
    // For now, this is a placeholder - needs camera integration
    // The UNIHIKER K10 camera frame is typically available via the camera task

    /*
    // When camera frame is available:
    if (fb != nullptr && fb->buf != nullptr) {
        ClassificationResult result = classifyImage(fb->buf, fb->width, fb->height);

        drawResultUI(result);

        if (result.valid && result.confidence > 0.7) {
            // Add to inventory
            addIngredientToAPI(result.className, 1);
            delay(2000);

            // Refresh inventory
            fetchIngredients();
        } else {
            delay(2000);
        }
    }
    */

    // Placeholder message until model is trained
    k10.canvas->canvasClear();
    k10.setScreenBackground(0x000000);
    k10.canvas->canvasText("MODEL NEEDED", 2, 0xFF8800);
    k10.canvas->canvasText("Train model with", 4, 0xFFFFFF);
    k10.canvas->canvasText("Teachable Machine", 5, 0xFFFFFF);
    k10.canvas->canvasText("then update code", 6, 0xFFFFFF);
    k10.canvas->canvasText("B:Back", 8, 0x00FF00);
    k10.canvas->updateCanvas();
}

// Button callbacks
void onButtonAPressed() {
    Serial.println("Button A pressed");

    switch (currentMode) {
        case MODE_INVENTORY:
            // Switch to capture mode
            currentMode = MODE_CAPTURE;
            captureCount = 0;
            k10.initBgCamerImage();
            k10.setBgCamerImage(true);
            drawCaptureUI();
            break;

        case MODE_SCANNER:
            // Back to inventory
            k10.setBgCamerImage(false);
            currentMode = MODE_INVENTORY;
            drawInventoryUI();
            break;

        case MODE_CAPTURE:
            // Capture image
            captureTrainingImage();
            drawCaptureUI();
            break;
    }
}

void onButtonBPressed() {
    Serial.println("Button B pressed");

    switch (currentMode) {
        case MODE_INVENTORY:
            // Switch to scanner mode
            currentMode = MODE_SCANNER;
            k10.initBgCamerImage();
            k10.setBgCamerImage(true);
            drawScannerUI();
            break;

        case MODE_SCANNER:
            // Scan vegetable
            scanVegetable();
            break;

        case MODE_CAPTURE:
            // Next class
            captureClassIndex = (captureClassIndex + 1) % NUM_CLASSES;
            captureCount = 0;
            drawCaptureUI();
            break;
    }
}

void onButtonABPressed() {
    Serial.println("Button A+B pressed");

    // Always return to inventory mode
    k10.setBgCamerImage(false);
    currentMode = MODE_INVENTORY;
    drawInventoryUI();
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("UNIHIKER K10 Fridge Manager Starting...");

    // Initialize K10 hardware
    k10.begin();
    k10.initScreen(screen_dir);
    k10.creatCanvas();

    // Set up button callbacks
    k10.buttonA->setPressedCallback(onButtonAPressed);
    k10.buttonB->setPressedCallback(onButtonBPressed);
    k10.buttonAB->setPressedCallback(onButtonABPressed);

    // Initialize classifier
    classifierInit();

    // Show loading screen
    drawInventoryUI();

    // Connect to WiFi
    Serial.printf("Connecting to WiFi: %s\n", ssid);
    WiFi.begin(ssid, password);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi connected!");
        Serial.print("IP Address: ");
        Serial.println(WiFi.localIP());

        // Configure time
        configTime(0, 0, "pool.ntp.org", "time.nist.gov");
        delay(2000);

        // Fetch initial data
        dataLoaded = fetchIngredients();
        drawInventoryUI();
    } else {
        Serial.println("\nWiFi connection failed!");
        k10.canvas->canvasText("WiFi Failed!", 3, 0xFF0000);
        k10.canvas->updateCanvas();
    }
}

void loop() {
    // Auto-refresh inventory every 30 seconds (only in inventory mode)
    static unsigned long lastUpdate = 0;
    if (currentMode == MODE_INVENTORY && millis() - lastUpdate > 30000) {
        if (fetchIngredients()) {
            dataLoaded = true;
            drawInventoryUI();
        }
        lastUpdate = millis();
    }

    delay(100);
}
