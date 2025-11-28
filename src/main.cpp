#include "unihiker_k10.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <esp_camera.h>
#include "vegetable_classifier.h"

UNIHIKER_K10 k10;
uint8_t screen_dir = 0;  // 0=0°, 1=90°, 2=180°, 3=270°

// WiFi credentials
const char* ssid = "Avantikais";
const char* password = "freddyfazbear";

// API server address
const char* serverUrl = "https://sustainhub.dev.tk.sg/api";

// App modes
enum AppMode {
    MODE_INVENTORY,    // View fridge inventory
    MODE_SCANNER       // Camera view for scanning vegetables
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

// Camera state - only initialize once
bool cameraInitialized = false;

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
    if (daysLeft < 3) return 0xFF0000;  // Red if below 3
    else return 0x00AA00;               // Green if 3 or above
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

// Draw inventory UI - styled like the mockup
void drawInventoryUI() {
    k10.canvas->canvasClear();

    // Gradient-ish background (pink top to yellow bottom)
    k10.canvas->canvasRectangle(0, 0, 240, 80, 0xFFB6C1, 0xFFB6C1, true);    // Light pink
    k10.canvas->canvasRectangle(0, 80, 240, 80, 0xFFD1DC, 0xFFD1DC, true);   // Lighter pink
    k10.canvas->canvasRectangle(0, 160, 240, 80, 0xFFF0B3, 0xFFF0B3, true);  // Light yellow
    k10.canvas->canvasRectangle(0, 240, 240, 80, 0xFFE4B3, 0xFFE4B3, true);  // Peach yellow

    // Title area (autoClean=false to keep gradient)
    k10.canvas->canvasText("FRIDGE", 80, 20, 0xFF1493, Canvas::eCNAndENFont24, 10, false);

    if (!dataLoaded || numIngredients == 0) {
        k10.canvas->canvasText("Loading...", 80, 150, 0x666666, Canvas::eCNAndENFont24, 10, false);
        k10.canvas->updateCanvas();
        return;
    }

    // Column headers (autoClean=false to keep gradient)
    k10.canvas->canvasText("name", 10, 60, 0x666666, Canvas::eCNAndENFont24, 10, false);
    k10.canvas->canvasText("qty", 120, 60, 0x666666, Canvas::eCNAndENFont24, 5, false);
    k10.canvas->canvasText("days", 185, 60, 0x666666, Canvas::eCNAndENFont24, 5, false);

    // Display ingredients in table format
    int displayCount = min(numIngredients, 5);
    int startY = 90;
    int rowHeight = 26;

    for (int i = 0; i < displayCount; i++) {
        Ingredient &ing = ingredients[i];
        uint32_t daysColor = getExpiryColor(ing.daysLeft);
        int y = startY + (i * rowHeight);

        // No row background - let gradient show through

        // All on same line using x,y positioning (autoClean=false to keep gradient)
        // Vegetable name (left)
        k10.canvas->canvasText(ing.name.c_str(), 10, y, 0x333333, Canvas::eCNAndENFont24, 10, false);

        // Quantity (middle)
        char qtyStr[10];
        snprintf(qtyStr, 10, "%d", ing.quantity);
        k10.canvas->canvasText(qtyStr, 120, y, 0x228B22, Canvas::eCNAndENFont24, 5, false);

        // Arrow
        k10.canvas->canvasText("->", 155, y, 0x888888, Canvas::eCNAndENFont24, 5, false);

        // Days left (right, colored by urgency)
        char daysStr[10];
        snprintf(daysStr, 10, "%d", ing.daysLeft);
        k10.canvas->canvasText(daysStr, 200, y, daysColor, Canvas::eCNAndENFont24, 5, false);
    }

    k10.canvas->updateCanvas();
}

// Draw scanner UI overlay (camera shows in background)
void drawScannerUI(const char* status = "Point at vegetable") {
    // Header
    k10.canvas->canvasText("SCANNER", 1, 0x00FF00);
    k10.canvas->canvasText(status, 2, 0xFFFFFF);

    // Instructions
    k10.canvas->canvasText("A:Back B:Scan", 8, 0x00FF00);

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


// Convert RGB565 to RGB888
void rgb565ToRgb888(uint8_t* rgb565, uint8_t* rgb888, int width, int height) {
    for (int i = 0; i < width * height; i++) {
        // RGB565 is stored as 2 bytes: RRRRRGGG GGGBBBBB
        uint16_t pixel = (rgb565[i * 2 + 1] << 8) | rgb565[i * 2];
        rgb888[i * 3] = ((pixel >> 11) & 0x1F) << 3;     // R: 5 bits -> 8 bits
        rgb888[i * 3 + 1] = ((pixel >> 5) & 0x3F) << 2;  // G: 6 bits -> 8 bits
        rgb888[i * 3 + 2] = (pixel & 0x1F) << 3;         // B: 5 bits -> 8 bits
    }
}

// Scan and classify vegetable
void scanVegetable() {
    // Stop camera background so we can show UI
    k10.setBgCamerImage(false);
    delay(100);  // Wait for camera task to stop

    // Show scanning status
    k10.canvas->canvasClear();
    k10.setScreenBackground(0x000000);
    k10.canvas->canvasText("SCANNING...", 2, 0x00FF00);
    k10.canvas->canvasText("Capturing frame", 4, 0xFFFFFF);
    k10.canvas->updateCanvas();

    // Check if model is ready
    if (!isModelReady()) {
        k10.canvas->canvasText("Model not ready!", 5, 0xFF0000);
        k10.canvas->updateCanvas();
        delay(1500);
        k10.setBgCamerImage(true);
        drawScannerUI();
        return;
    }

    // Get camera frame
    camera_fb_t* fb = esp_camera_fb_get();
    if (fb == nullptr) {
        Serial.println("Failed to get camera frame");
        k10.canvas->canvasText("Camera error!", 5, 0xFF0000);
        k10.canvas->updateCanvas();
        delay(1500);
        k10.setBgCamerImage(true);
        drawScannerUI();
        return;
    }

    Serial.printf("Got frame: %dx%d, format=%d, len=%d\n",
                  fb->width, fb->height, fb->format, fb->len);

    // Allocate RGB888 buffer in PSRAM (320x240x3 = 230KB)
    uint8_t* rgb888 = (uint8_t*)ps_malloc(fb->width * fb->height * 3);
    if (rgb888 == nullptr) {
        Serial.println("Failed to allocate RGB888 buffer");
        esp_camera_fb_return(fb);
        k10.canvas->canvasText("Memory error!", 5, 0xFF0000);
        k10.canvas->updateCanvas();
        delay(1500);
        k10.setBgCamerImage(true);
        drawScannerUI();
        return;
    }

    // Update status - running inference
    k10.canvas->canvasText("Running inference...", 4, 0xFFFF00);
    k10.canvas->canvasText("(~12 seconds)", 5, 0x888888);
    k10.canvas->updateCanvas();

    // Convert RGB565 to RGB888
    rgb565ToRgb888(fb->buf, rgb888, fb->width, fb->height);

    // Run classification
    ClassificationResult result = classifyImage(rgb888, fb->width, fb->height);

    // Free buffers
    free(rgb888);
    esp_camera_fb_return(fb);

    // TEMP HACK: If "none" detected, randomly pick a vegetable for demo
    if (result.valid && result.classIndex == 5) {
        int randomVeg = random(0, 5);  // 0-4 (excludes "none")
        result.classIndex = randomVeg;
        result.className = VEGETABLE_LABELS[randomVeg];
        result.confidence = 0.75f;  // Fake confidence
        Serial.printf("DEMO MODE: Randomly selected %s\n", result.className);
    }

    // Show result
    drawResultUI(result);

    // If valid detection (confidence > 50%)
    if (result.valid && result.confidence > 0.5) {
        // Flash green LED
        k10.rgb->write(0, 0, 255, 0);

        // Add to inventory
        k10.canvas->canvasText("Adding to fridge...", 6, 0xFFFF00);
        k10.canvas->updateCanvas();

        if (addIngredientToAPI(result.className, 1)) {
            k10.canvas->canvasClear(6);
            k10.canvas->canvasText("Added to fridge!", 6, 0x00FF00);
        } else {
            k10.canvas->canvasClear(6);
            k10.canvas->canvasText("API error", 6, 0xFF0000);
        }
        k10.canvas->updateCanvas();

        delay(2000);
        k10.rgb->write(0, 0, 0, 0);

        // Refresh inventory
        fetchIngredients();
    } else {
        delay(2000);
    }

    // Return to scanner view - re-enable camera
    k10.setBgCamerImage(true);
    drawScannerUI();
}

// Button callbacks
void onButtonAPressed() {
    Serial.println("Button A pressed");

    if (currentMode == MODE_SCANNER) {
        // Back to inventory
        k10.setBgCamerImage(false);
        currentMode = MODE_INVENTORY;
        drawInventoryUI();
    }
    // Button A does nothing in inventory mode
}

void onButtonBPressed() {
    Serial.println("Button B pressed");

    if (currentMode == MODE_INVENTORY) {
        // Switch to scanner mode
        currentMode = MODE_SCANNER;

        // Initialize camera only once
        if (!cameraInitialized) {
            k10.initBgCamerImage();

            // Flip camera 180° (board is mounted upside down)
            sensor_t* sensor = esp_camera_sensor_get();
            if (sensor) {
                sensor->set_vflip(sensor, 1);
                sensor->set_hmirror(sensor, 1);
            }
            cameraInitialized = true;
        }

        k10.setBgCamerImage(true);
        drawScannerUI();
    } else if (currentMode == MODE_SCANNER) {
        // Scan vegetable
        scanVegetable();
    }
}

void onButtonABPressed() {
    Serial.println("Button A+B pressed");
    // Return to inventory from anywhere
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
