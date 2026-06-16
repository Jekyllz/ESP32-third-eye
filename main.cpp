#include <Arduino.h>
#include <SPI.h>
#include <Arduino_GFX_Library.h>
#include <OneButton.h>

// --- HARDWARE PINS ---
#define TFT_MOSI 6
#define TFT_SCLK 7
#define TFT_CS   21
#define TFT_DC   10 
#define TFT_RST  9  
#define TFT_BLK  5 
#define BUTTON_PIN 4 

#define SCREEN_RES 240
#define SKIN_TONE  0xFDB2   

// --- DISPLAY SETTINGS ---
int displayBrightness = 20; 

// --- DISPLAY HARDWARE ---
Arduino_DataBus *bus = new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCLK, TFT_MOSI, -1);
Arduino_GFX *gfx = new Arduino_GC9A01(bus, TFT_RST, 0, true);
Arduino_Canvas *canvas = new Arduino_Canvas(SCREEN_RES, SCREEN_RES, gfx);

// --- POWER CONFIG ---
const unsigned long SLEEP_TIMEOUT_MS = 15UL * 60UL * 1000UL; 
volatile unsigned long lastInteractionTime = 0; 
OneButton button(BUTTON_PIN, true); 

// --- RTC MEMORY & MODES ---
RTC_DATA_ATTR int currentModeIndex = 0; 
const int eyeThemeSequence[] = {9, 12, 1, 0, 2, 3, 4, 5, 6, 7, 8, 10, 11};

// --- PHYSICS & ANIMATION VARIABLES ---
float curX = 120, curY = 120, tarX = 120, tarY = 120;
float snapSpeed = 2.0;
bool isWideMovement = true;

enum EyeState { MOVING, PAUSING };
EyeState eyeState = PAUSING;
uint32_t stateEndTime = 0;

const int TARGET_FPS = 85; 
const int FRAME_DELAY = 1000 / TARGET_FPS;
uint32_t nextFrameTime = 0;

volatile bool isJittering = false;
volatile uint32_t jitterEndTime = 0;

// --- SECRET MODES & MATRIX LOGIC ---
volatile bool isMatrixMode = false;
int secretEffectIndex = 0; 
struct MatrixColumn { int y, speed; uint32_t lastUpdate; };
MatrixColumn matrixCols[24];

int16_t sinLUT[256];
void initSinLUT() { for (int i = 0; i < 256; i++) sinLUT[i] = (int16_t)(sin(i * 2.0 * PI / 256.0) * 127); }
inline int16_t fastSin(int16_t angle) { return sinLUT[angle & 0xFF]; }
inline int16_t fastCos(int16_t angle) { return sinLUT[(angle + 64) & 0xFF]; }

uint16_t pColor(uint32_t speed, int offset) {
  uint32_t t = millis() / speed;
  uint8_t r = fastSin(t + offset) + 128, g = fastSin(t + offset + 85) + 128, b = fastSin(t + offset + 170) + 128;
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

void initMatrixColumn(int i) { matrixCols[i].y = random(-300, 0); matrixCols[i].speed = random(30, 120); matrixCols[i].lastUpdate = millis(); }
void buttonReadTask(void *pvParameters) { while (true) { button.tick(); vTaskDelay(10 / portTICK_PERIOD_MS); } }

void powerOff() {
  gfx->displayOff(); analogWrite(TFT_BLK, 0); 
  while (digitalRead(BUTTON_PIN) == LOW) { delay(10); } 
  delay(100); esp_deep_sleep_enable_gpio_wakeup(1ULL << BUTTON_PIN, ESP_GPIO_WAKEUP_GPIO_LOW); esp_deep_sleep_start();
}

void singleClick() {
  lastInteractionTime = millis(); 
  if (isMatrixMode) { secretEffectIndex = (secretEffectIndex + 1) % 11; } 
  else {
    currentModeIndex = (currentModeIndex + 1) % 13; 
  }
}

void doubleClick() {
  lastInteractionTime = millis(); 
  if (!isMatrixMode) { isJittering = true; jitterEndTime = millis() + 500; }
}

void multiClick() { lastInteractionTime = millis(); if (button.getNumberClicks() == 3) { isMatrixMode = !isMatrixMode; if (isMatrixMode) for(int i = 0; i < 24; i++) initMatrixColumn(i); } }

void setup() {
  initSinLUT();
  if (currentModeIndex > 12 || currentModeIndex < 0) currentModeIndex = 0; 
  if (!gfx->begin(80000000)) { gfx->begin(40000000); }
  gfx->displayOn(); 
  pinMode(TFT_BLK, OUTPUT); analogWrite(TFT_BLK, displayBrightness);
  
  if (!canvas->begin()) { while(1) { delay(1000); } }
  
  button.setClickMs(600); button.setDebounceMs(80);
  button.attachClick(singleClick); button.attachDoubleClick(doubleClick);
  button.attachMultiClick(multiClick); button.attachLongPressStart(powerOff);
  
  lastInteractionTime = millis(); stateEndTime = millis() + 500; nextFrameTime = millis();
  xTaskCreate(buttonReadTask, "ButtonTask", 2048, NULL, 2, NULL);
}

void loop() {
  uint32_t now = millis();
  if (now < nextFrameTime) { delay(1); return; } 
  nextFrameTime = now + FRAME_DELAY;
  if (now - lastInteractionTime >= SLEEP_TIMEOUT_MS) powerOff();
  canvas->fillScreen(BLACK);

  if (isMatrixMode) {
    switch (secretEffectIndex) {
      case 0: for (int i = 0; i < 24; i++) { if (now - matrixCols[i].lastUpdate > matrixCols[i].speed) { matrixCols[i].y += 10; matrixCols[i].lastUpdate = now; } int x = i * 10, y = matrixCols[i].y; if (y > -10 && y < 240) { canvas->setCursor(x, y); canvas->setTextColor(0xBE76); canvas->print((char)random(33, 126)); } for (int j = 1; j < 15; j++) { int tY = y - (j * 10); if (tY > -10 && tY < 240) { int gV = 255 - (j * 18); if (gV < 40) gV = 40; canvas->setCursor(x, tY); canvas->setTextColor(gfx->color565(0, gV, 0)); canvas->print((char)random(33, 126)); } } if (matrixCols[i].y > 390) initMatrixColumn(i); } break;
      
      case 1: { // 3D ROTATING CUBE
        float cube[8][3] = {{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}};
        int edges[12][2] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
        float rX = now/1000.0f, rY = now/1300.0f, rZ = now/1700.0f;
        int px[8], py[8];
        for(int i=0; i<8; i++) {
          float x=cube[i][0], y=cube[i][1], z=cube[i][2];
          float ty = y*cos(rX) - z*sin(rX), tz = y*sin(rX) + z*cos(rX); y=ty; z=tz;
          float tx = x*cos(rY) + z*sin(rY); tz = -x*sin(rY) + z*cos(rY); x=tx; z=tz;
          tx = x*cos(rZ) - y*sin(rZ); ty = x*sin(rZ) + y*cos(rZ); x=tx; y=ty;
          float p = 3.0f / (3.0f - z); px[i] = 120 + (int)(x*p*50); py[i] = 120 + (int)(y*p*50);
        }
        for(int i=0; i<12; i++) canvas->drawLine(px[edges[i][0]], py[edges[i][0]], px[edges[i][1]], py[edges[i][1]], pColor(5, i*15));
        break;
      }

      case 2: { // FLOWING PLASMA
        uint32_t t = now + 50000;
        for (int x = 0; x < 240; x += 12) {
          for (int y = 0; y < 240; y += 12) {
            float v = fastSin(x / 16.0 + t / 800.0) + fastSin((y + t / 10.0) / 20.0) + fastSin((x + y + t / 15.0) / 30.0);
            canvas->fillRect(x, y, 12, 12, pColor(v * 4 + 10, x / 2 + y / 2));
          }
        }
        break;
      }

      case 3: { // 4D TESSERACT
        float nodes[16][4] = {{-1,-1,-1,-1},{1,-1,-1,-1},{1,1,-1,-1},{-1,1,-1,-1},{-1,-1,1,-1},{1,-1,1,-1},{1,1,1,-1},{-1,1,1,-1},{-1,-1,-1,1},{1,-1,-1,1},{1,1,-1,1},{-1,1,-1,1},{-1,-1,1,1},{1,-1,1,1},{1,1,1,1},{-1,1,1,1}};
        int edges[32][2] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7},{8,9},{9,10},{10,11},{11,8},{12,13},{13,14},{14,15},{15,12},{8,12},{9,13},{10,14},{11,15},{0,8},{1,9},{2,10},{3,11},{4,12},{5,13},{6,14},{7,15}};
        float r = now/1000.0f;
        int px[16], py[16];
        for(int i=0; i<16; i++) {
          float x=nodes[i][0], y=nodes[i][1], z=nodes[i][2], w=nodes[i][3];
          float tw = w*cos(r) - x*sin(r); float tx = w*sin(r) + x*cos(r); w=tw; x=tx;
          float ty = y*cos(r*0.8f) - z*sin(r*0.8f); float tz = y*sin(r*0.8f) + z*cos(r*0.8f); y=ty; z=tz;
          float p = 4.0f / (4.0f - w); float p2 = 3.0f / (3.0f - z);
          px[i] = 120 + (int)(x*p*p2*45); py[i] = 120 + (int)(y*p*p2*45);
        }
        for(int i=0; i<32; i++) canvas->drawLine(px[edges[i][0]], py[edges[i][0]], px[edges[i][1]], py[edges[i][1]], pColor(8, i*4));
        break;
      }

      case 4: { // DOOM CORRIDOR (Continuous circular hallway - Fixed Ordering)
        float speed = now / 250.0f;
        float turn = 0.8f; 
        float flicker = (random(100) < 3) ? (float)random(3, 7) / 10.0f : 1.0f;
        
        const int NUM_SEG = 15; 
        float spacing = 1.0f;
        float max_z = (float)NUM_SEG * spacing;
        struct Seg { int x1, y1, x2, y2, size; uint16_t col, dark; };
        Seg s[NUM_SEG];

        float offset = fmod(speed, spacing);
        for (int i = 0; i < NUM_SEG; i++) {
          float z = max_z - (i * spacing) - offset;
          if (z <= 0.1f) z = 0.1f;
          s[i].size = (int)(240.0f / z);
          int cx = 120 + (int)(turn * (z * z) * 1.5f); 
          s[i].x1 = cx - s[i].size/2; s[i].y1 = 120 - s[i].size/2;
          s[i].x2 = cx + s[i].size/2; s[i].y2 = 120 + s[i].size/2;
          uint16_t base = pColor(15, (int)(speed * 10 + i * 20)); 
          uint8_t r = (base >> 11) << 3, g = ((base >> 5) & 0x3F) << 2, b = (base & 0x1F) << 3;
          s[i].col = gfx->color565(r * flicker, g * flicker, b * flicker);
          s[i].dark = gfx->color565(r * flicker / 5, g * flicker / 4, b * flicker / 3);
        }

        for (int i = 0; i < NUM_SEG - 1; i++) {
          if (s[i].size < 2 || s[i+1].size > 1000) continue;
          canvas->fillTriangle(s[i+1].x1, s[i+1].y1, s[i+1].x1, s[i+1].y2, s[i].x1, s[i].y2, s[i].dark);
          canvas->fillTriangle(s[i+1].x1, s[i+1].y1, s[i+1].x1, s[i+1].y2, s[i].x1, s[i].y1, s[i].dark); 
          canvas->fillTriangle(s[i+1].x2, s[i+1].y1, s[i+1].x2, s[i+1].y2, s[i].x2, s[i].y2, s[i].dark);
          canvas->fillTriangle(s[i+1].x2, s[i+1].y1, s[i+1].x2, s[i+1].y2, s[i].x2, s[i].y1, s[i].dark); 
          canvas->fillTriangle(s[i+1].x1, s[i+1].y1, s[i+1].x2, s[i+1].y1, s[i].x2, s[i].y1, s[i].dark);
          canvas->fillTriangle(s[i+1].x1, s[i+1].y1, s[i+1].x2, s[i+1].y1, s[i].x1, s[i].y1, s[i].dark); 
          canvas->fillTriangle(s[i+1].x1, s[i+1].y2, s[i+1].x2, s[i+1].y2, s[i].x2, s[i].y2, s[i].dark);
          canvas->fillTriangle(s[i+1].x1, s[i+1].y2, s[i+1].x2, s[i+1].y2, s[i].x1, s[i].y2, s[i].dark); 
          canvas->drawLine(s[i+1].x1, s[i+1].y1, s[i].x1, s[i].y1, s[i].col);
          canvas->drawLine(s[i+1].x1, s[i+1].y2, s[i].x1, s[i].y2, s[i].col);
          canvas->drawLine(s[i+1].x2, s[i+1].y1, s[i].x2, s[i].y1, s[i].col);
          canvas->drawLine(s[i+1].x2, s[i+1].y2, s[i].x2, s[i].y2, s[i].col);
          canvas->drawRect(s[i].x1, s[i].y1, s[i].size, s[i].size, s[i].col);
        }
        canvas->drawRect(s[NUM_SEG-1].x1, s[NUM_SEG-1].y1, s[NUM_SEG-1].size, s[NUM_SEG-1].size, s[NUM_SEG-1].col);
        break;
      }

      case 5: for (int x = 0; x < 240; x += 15) { for (int y = 0; y < 240; y += 15) { int v = fastSin(x + now/16) + fastSin(y + now/12); canvas->fillRect(x, y, 15, 15, pColor(v / 8 + 5, x + y)); } } break;
      case 6: for (int i = 0; i < 10; i++) { int br = (fastSin(now / 8) + 150) * 40 / 127; canvas->drawCircle(120, 120, br + (i * 10), pColor(10, i * 15)); } canvas->fillCircle(120, 120, 30, BLACK); break;
      case 7: for (int i = 0; i < 12; i++) { int size = (now / 10 + i * 20) % 240; canvas->drawRect(120 - size/2, 120 - size/2, size, size, pColor(5, i * 20)); } break;
      case 8: for (int i = 0; i < 10; i++) { canvas->fillRect(0, random(240), 240, random(5, 15), pColor(1, i * 50)); } break;
      case 9: { for (int x = 0; x < 240; x += 20) { for (int y = 0; y < 240; y += 20) { int dist = sqrt(pow(x - 120, 2) + pow(y - 120, 2)); int v = fastSin(dist - now / 8) + fastSin(x / 10) + fastSin(y / 10); canvas->fillRect(x, y, 20, 20, pColor(8, (now / 10) - dist / 2 + v * 5)); } } break; }
      case 10: { for (int i = 0; i < 60; i++) { int angle = i * 4.25f; int spd = ((now + (i * 100)) % 2000) / 8; canvas->drawLine(120, 120, 120 + (fastCos(angle) * spd / 127), 120 + (fastSin(angle) * spd / 127), pColor(5, i * 10)); } break; }
    }
  } else {
    int theme = eyeThemeSequence[currentModeIndex];
    if (isJittering) { if (now > jitterEndTime) isJittering = false; else { curX = 120 + random(-40, 40); curY = 120 + random(-40, 40); } }
    else {
      if (eyeState == MOVING) {
        float dx = tarX - curX, dy = tarY - curY, dist = sqrt(dx*dx + dy*dy);
        if (dist < 1.0f || now > stateEndTime) { 
          curX = tarX; curY = tarY; 
          eyeState = PAUSING; 
          stateEndTime = now + random(400, 1500); 
        }
        else { 
          if (dist > snapSpeed) { curX += (dx/dist)*snapSpeed; curY += (dy/dist)*snapSpeed; } 
          else { curX = tarX; curY = tarY; } 
        }
      } else {
        if (now > stateEndTime) {
          int rType = random(100); isWideMovement = (rType < 60); float newTarX, newTarY;
          if (rType > 60) { 
            newTarX = constrain(curX + random(-35, 35), 35, 205); 
            newTarY = constrain(curY + random(-35, 35), 35, 205); 
            snapSpeed = (float)random(100, 400) / 10.0f; 
          } else { 
            do { 
              newTarX = random(40, 200); newTarY = random(40, 200); 
              float tdx = newTarX - curX, tdy = newTarY - curY; 
              if (sqrt(tdx*tdx + tdy*tdy) > 20.0f) break; 
            } while(true);
            int rSpd = random(100); 
            if (rSpd < 30) snapSpeed = (float)random(30, 80) / 10.0f; 
            else if (rSpd < 70) snapSpeed = (float)random(120, 300) / 10.0f; 
            else snapSpeed = (float)random(400, 700) / 10.0f; 
          }
          tarX = newTarX; tarY = newTarY; eyeState = MOVING; stateEndTime = now + 1500; 
        }
      }
    }
    float gzX = (curX-120), gzY = (curY-120); 
    int irX = 120+(int)(gzX*0.4), irY = 120+(int)(gzY*0.4); 
    int puX = 120+(int)(gzX*0.7), puY = 120+(int)(gzY*0.7);
    
    switch(theme) {
      case 0: for(int i=0; i<15; i++) canvas->drawCircle(120, 120, (int)(now/10 + i*20) % 118, pColor(10, i*15)); canvas->fillCircle(irX, irY, 65, 0xFFFF); break;
      case 1: canvas->fillCircle(120, 120, 118, pColor(2, 0)); if(random(0,10)>7) canvas->fillRect(0, random(240), 240, 8, 0xFFFF); canvas->fillCircle(irX+random(-8,8), irY, 70, 0xFFFF); break;
      case 2: for(int i=0; i<20; i++) { int angle = (now / 8) + (i * 12); canvas->drawCircle(120 + (fastCos(angle)*20/127), 120 + (fastSin(angle)*20/127), 118-i*5, pColor(6, i*10)); } canvas->fillCircle(irX, irY, 65, 0xFFFF); break;
      case 3: { int bt = (fastSin(now / 4) + 127) * 20 / 254; canvas->fillCircle(120, 120, 100+bt, pColor(3, 0)); canvas->fillCircle(120, 120, 80+bt, 0); } canvas->fillCircle(irX, irY, 65, 0xFFFF); break;
      case 4: for(int x=0; x<240; x+=20) { for(int y=0; y<240; y+=20) canvas->drawRect(x, y, 18, 18, pColor(20, x+y)); } canvas->fillCircle(irX, irY, 60, 0); canvas->fillCircle(irX, irY, 55, 0xFFFF); break;
      case 5: for(int i=0; i<400; i++) canvas->drawPixel(random(240), random(240), random(0xFFFF)); canvas->fillCircle(irX, irY, 70, 0xFFFF); break;
      case 6: for(int i=0; i<12; i++) canvas->fillCircle(120, 120, 118-(i*10), (i+now/100)%2==0 ? pColor(5,0):0); canvas->fillCircle(irX, irY, 70, 0xFFFF); break;
      case 7: canvas->fillCircle(120, 120, 118, 0x0010); canvas->fillCircle(irX, irY, 80, 0xFFFF); break;
      case 8: canvas->fillRect(irX-60, irY-60, 120, 120, 0xFFFF); canvas->fillRect(puX-25, puY-25, 50, 50, 0x0010); canvas->fillRect(puX-15, puY-15, 30, 30, 0); break;
      case 9: canvas->fillCircle(120, 120, 118, 0xF81F); canvas->fillCircle(irX, irY, 80, 0xFFFF); break;
      case 10: for(int i=0; i<100; i+=10) canvas->fillRect(random(240), random(240), 10, 10, pColor(1, random(255))); canvas->fillCircle(irX, irY, 75, 0xFFFF); break;
      case 11: // --- AZTEC DIAMOND (Experimental) ---
        for(int i=0; i<8; i++) {
          int s = 120 - (i*15) - (now/20 % 15);
          if(s>0) {
            uint16_t c = (i%2==0) ? 0x8410 : 0x4208; // Stone-like grey steps
            canvas->drawRect(120-s, 120-s, s*2, s*2, c);
            canvas->drawLine(120-s, 120-s, 120+s, 120+s, c);
            canvas->drawLine(120+s, 120-s, 120-s, 120+s, c);
          }
        }
        for(int i=0; i<4; i++) {
          int a = (now/10) + (i*64);
          canvas->drawLine(120, 120, 120+(fastCos(a)*100/127), 120+(fastSin(a)*100/127), 0xFD20);
        }
        canvas->fillCircle(irX, irY, 70, 0xFFFF); 
        break;
      case 12: for (int x = 0; x < 240; x += 15) { for (int y = 0; y < 240; y += 15) { int v = fastSin(x + now/16) + fastSin(y + now/12); canvas->fillRect(x, y, 15, 15, pColor(v / 8 + 5, x + y)); } } canvas->fillCircle(irX, irY, 70, 0xFFFF); break;
    }
    
    // MANDATORY IRIS AND PUPIL LAYER (Except theme 8)
    if (theme != 8) { 
      // 1. Draw Iris (Custom colors for specific modes)
      uint16_t irisCol;
      if (theme == 9) irisCol = 0xF81F;      // Magenta
      else if (theme == 7) irisCol = 0x0010; // Deep Blue (Match background)
      else if (theme == 11) irisCol = 0xFD20; // Aztec Gold
      else irisCol = 0x7FFF;                 // Light Blue default
      
      int irisRadius = (theme == 4) ? 25 : 38;
      int pupilRadius = (theme == 4) ? 12 : 20;
      int glintSize = (theme == 4) ? 4 : 6;
      int glintOff = (theme == 4) ? 6 : 10;

      if (theme == 11) { // Diamond Shape for Aztec
        for(int i=0; i<irisRadius; i++) canvas->drawLine(puX-i, puY-(irisRadius-i), puX+i, puY-(irisRadius-i), irisCol);
        for(int i=0; i<irisRadius; i++) canvas->drawLine(puX-i, puY+(irisRadius-i), puX+i, puY+(irisRadius-i), irisCol);
        for(int i=0; i<pupilRadius; i++) canvas->drawLine(puX-i, puY-(pupilRadius-i), puX+i, puY-(pupilRadius-i), 0);
        for(int i=0; i<pupilRadius; i++) canvas->drawLine(puX-i, puY+(pupilRadius-i), puX+i, puY+(pupilRadius-i), 0);
      } else {
        canvas->fillCircle(puX, puY, irisRadius, irisCol); 
        canvas->fillCircle(puX, puY, pupilRadius, 0); 
      }
      
      // 3. Draw Reflection (The white glint)
      canvas->fillCircle(puX - glintOff, puY - glintOff, glintSize, 0xFFFF); 
    }
  }
  canvas->flush();
}

