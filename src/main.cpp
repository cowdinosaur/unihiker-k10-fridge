#include "unihiker_k10.h"

UNIHIKER_K10 k10;
uint8_t screen_dir = 0;  // 0=0°, 1=90°, 2=180°, 3=270°

// Mock ingredient data structure
struct Ingredient {
    const char* name;
    int quantity;
    int daysLeft;
};

// Mock ingredients data
Ingredient ingredients[] = {
    {"tomato", 3, 3},
    {"onion", 2, 4},
    {"lemon", 1, 7},
    {"cucumber", 5, 9},
    {"radish", 1, 10}
};

const int numIngredients = 5;
int currentScroll = 0;

// Get color based on days left
uint32_t getExpiryColor(int daysLeft) {
    if (daysLeft <= 3) return 0xFF0000;      // Red - urgent
    else if (daysLeft <= 5) return 0xFFAA00; // Orange - warning
    else if (daysLeft <= 7) return 0xFFFF00; // Yellow - caution
    else return 0x00AA00;                     // Green - good
}

void drawUI() {
    // Clear screen
    k10.canvas->canvasClear();
    k10.setScreenBackground(0xFFFFFF);

    // Draw blue border
    k10.canvas->canvasRectangle(2, 2, 236, 316, 0x0080FF, 0x0080FF, false);
    k10.canvas->canvasRectangle(3, 3, 234, 314, 0x0080FF, 0x0080FF, false);

    // Draw header (smaller text to fit)
    k10.canvas->canvasText("Fridge Manager", 1, 0x000000);
    k10.canvas->canvasText("qty -> days left", 2, 0x666666);

    // Starting Y position for ingredients list
    int yPos = 60;
    int lineHeight = 45;

    // Draw each ingredient
    for (int i = 0; i < numIngredients; i++) {
        if (yPos + lineHeight > 300) break; // Don't draw beyond screen

        Ingredient &ing = ingredients[i];
        uint32_t color = getExpiryColor(ing.daysLeft);

        // Create display string
        char line[50];
        sprintf(line, "%s:%d->%dd", ing.name, ing.quantity, ing.daysLeft);

        // Draw text (line 3, 4, 5, 6, 7 for different Y positions)
        // Using canvas positioning instead
        k10.canvas->canvasText(line, 3 + i, color);

        yPos += lineHeight;
    }

    // Draw status bar at bottom
    k10.canvas->canvasText("BTN:Refresh", 8, 0x666666);

    k10.canvas->updateCanvas();
}

void setup() {
    // Initialize K10 hardware
    k10.begin();
    k10.initScreen(screen_dir);
    k10.creatCanvas();

    // Draw initial UI
    drawUI();
}

void loop() {
    // Simulate ingredient updates every 5 seconds
    static unsigned long lastUpdate = 0;
    if (millis() - lastUpdate > 5000) {
        // Randomly update one ingredient's quantity
        int randomIdx = random(0, numIngredients);
        ingredients[randomIdx].quantity = random(1, 10);

        // Redraw UI
        drawUI();

        lastUpdate = millis();
    }

    delay(100);
}