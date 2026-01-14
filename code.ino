#include <SPI.h>
#include <MFRC522.h>
#include <FastLED.h>
#include <TFT_eSPI.h>

// --- 腳位與基礎設定 ---
#define PIN_LCD_BL 15
#define LED_PIN 16
#define WIDTH 16
#define HEIGHT 16
#define NUM_LEDS (WIDTH * HEIGHT)
#define BRIGHTNESS 30

#define BTN_UP 2
#define BTN_DOWN 3
#define BTN_LEFT 43
#define BTN_RIGHT 44
#define BTN_ACTION 17

#define NFC_SS_PIN 10
#define NFC_MOSI 11
#define NFC_SCK 12
#define NFC_MISO 13
#define NFC_RST_PIN 1

#define SPEAKER_PIN 21    // 無源蜂鳴器正極接這裡，負極接地
#define DINO_BEEP_STEP 50 // 恐龍每多少分觸發一次蜂鳴

byte ID_DINO[] = {0xF3, 0x99, 0x89, 0x28};
byte ID_GALAGA[] = {0xA3, 0xE9, 0x3F, 0x29};
byte ID_TETRIS[] = {0xE4, 0x54, 0x01, 0x89};
// 貪吃蛇卡
byte ID_SNAKE[] = {0x21, 0x0F, 0x8A, 0x26};

enum GameState
{
    STATE_MENU,
    STATE_DINO,
    STATE_GALAGA,
    STATE_TETRIS,
    STATE_SNAKE
};
GameState currentState = STATE_MENU;

CRGB leds[NUM_LEDS];
MFRC522 mfrc522(NFC_SS_PIN, NFC_RST_PIN);
TFT_eSPI tft = TFT_eSPI();

long gameScore = 0;
long lastDrawnScore = -1;

struct InputState
{
    bool actionPressed, leftPressed, rightPressed, upPressed, downPressed;
    bool lastAction;
} input;

unsigned long lastMoveTime = 0;
const int moveDelay = 150;

// --- 音效相關變數 ---
unsigned long buzzerBlockUntil = 0; // 用來阻擋背景音，讓事件音優先播放
unsigned long lastBackgroundToggle = 0;
int lastDinoBeepScoreLevel = -1;

// --- 俄羅斯方塊變數 ---
const uint16_t TETROMINO[7][4] = {
    {0x0F00, 0x2222, 0x00F0, 0x4444}, {0x8E00, 0x6440, 0x0E20, 0x44C0}, {0x2E00, 0x4460, 0x0E80, 0xC440}, {0x6600, 0x6600, 0x6600, 0x6600}, {0x6C00, 0x4620, 0x06C0, 0x8C40}, {0x4E00, 0x4640, 0x0E40, 0x4C40}, {0xC600, 0x2640, 0x0C60, 0x4C80}};
struct Piece
{
    int shape;
    int rot;
    int x, y;
} currentPiece;
unsigned long lastTetrisFall = 0;
const unsigned long TETRIS_GRAVITY = 500;
const unsigned long TETRIS_SOFT = 50;
bool tetrisGameOver = false;
unsigned long tetrisGameOverAt = 0;
uint16_t board[HEIGHT];

// --- 恐龍變數 ---
const float JUMP_IMPULSE = -1.5;          //|JUMP_IMPULSE| Bigger lower
const float GRAVITY = 0.15;               // Bigger Drops Faster
const unsigned long PHYSICS_MS = 25;      // Physics Update Interval
const unsigned long SCROLL_MS_BASE = 150; // Obstacle Moving interval at start
const unsigned long SCROLL_MS_MIN = 60;   // Obstacle Moving interval min
const unsigned long DINO_SCROLL_DECREASE_STEP = 5;
const unsigned long DINO_SCORE_STEP = 100;
const int DINO_MAX_LEVEL = (SCROLL_MS_BASE - SCROLL_MS_MIN) / DINO_SCROLL_DECREASE_STEP;

int dinoSpeedLevel = 0;

const unsigned long SPAWN_MIN_MS = 800;  // Minimum Interval of Obstacle Spawning
const unsigned long SPAWN_MAX_MS = 1600; // Maximum Interval of Obstacle Spawning
const int groundY = HEIGHT - 1;
const int dinoX = 3;
float dinoY;
float velY = 0;
unsigned long lastPhysics = 0;
unsigned long lastScroll = 0;
unsigned long nextSpawnAt = 0;
struct Obstacle
{
    int x;
    int h;
    bool counted;
};
#define MAX_OBS 6
Obstacle obs[MAX_OBS];
bool dinoGameOver = false;
unsigned long dinoGameOverAt = 0;

// --- 隕石變數 ---
struct Meteor
{
    int x;
    float y;
    bool active;
    int type;
};
Meteor meteors[6];
int playerX = 0;
int playerY = HEIGHT - 1;
unsigned long lastMeteorTick = 0;
const unsigned long METEOR_TICK_MS = 30;
bool meteorGameOver = false;
unsigned long meteorGameOverAt = 0;

// --- 貪吃蛇變數 ---
struct Point
{
    int x;
    int y;
};
Point snake[WIDTH * HEIGHT];
int snakeLen = 0;
int snakeDir = 1;
Point food;
bool snakeGameOver = false;
unsigned long lastSnakeMove = 0;
const unsigned long SNAKE_INTERVAL_BASE = 150;
const unsigned long SNAKE_INTERVAL_MIN = 60;
const unsigned long SNAKE_SPEED_STEP = 8;
const int SNAKE_MAX_LEVEL = (SNAKE_INTERVAL_BASE - SNAKE_INTERVAL_MIN) / SNAKE_SPEED_STEP;
int snakeLevel = 0;
unsigned long snakeGameOverAt = 0;

// --- 聲音播放函數 (無源蜂鳴器專用) ---
// freq: 頻率(Hz), duration: 持續時間(ms)
void playTone(int freq, int duration)
{
    tone(SPEAKER_PIN, freq, duration);
}

// 播放事件音效 (會短暫暫停背景節拍)
void playEventSound(int freq, int duration)
{
    playTone(freq, duration);
    buzzerBlockUntil = millis() + duration + 50; // 加一點緩衝時間
}

// --- 基礎函數 ---
uint16_t XY(uint8_t x, uint8_t y)
{
    if (x >= WIDTH || y >= HEIGHT)
        return 0;
    return (y % 2 == 0) ? (y * WIDTH + x) : (y * WIDTH + (WIDTH - 1 - x));
}

void updateScoreDisplay()
{
    if (gameScore != lastDrawnScore)
    {
        tft.fillRect(0, 50, 320, 60, TFT_BLACK);
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.setTextDatum(MC_DATUM);
        tft.setTextSize(3);
        tft.drawString(String(gameScore), tft.width() / 2, 80);
        lastDrawnScore = gameScore;
    }
}

void showTitle(String title)
{
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(3);
    tft.drawString(title, tft.width() / 2, 30);
    tft.setTextSize(2);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString("Score:", tft.width() / 2, 110);
    gameScore = 0;
    lastDrawnScore = 0;
    updateScoreDisplay();

    // 開始遊戲的音效
    playEventSound(1000, 100);
    delay(120);
    playEventSound(1500, 200);
}

void readInputs()
{
    bool cAction = !digitalRead(BTN_ACTION);
    bool cLeft = !digitalRead(BTN_LEFT);
    bool cRight = !digitalRead(BTN_RIGHT);
    bool cUp = !digitalRead(BTN_UP);
    bool cDown = !digitalRead(BTN_DOWN);
    unsigned long now = millis();

    input.actionPressed = (cAction && !input.lastAction);
    input.lastAction = cAction;

    if (now - lastMoveTime > moveDelay)
    {
        if (cLeft)
        {
            input.leftPressed = true;
            lastMoveTime = now;
        }
        else
            input.leftPressed = false;
        if (cRight)
        {
            input.rightPressed = true;
            lastMoveTime = now;
        }
        else
            input.rightPressed = false;
        if (cUp)
        {
            input.upPressed = true;
            lastMoveTime = now;
        }
        else
            input.upPressed = false;
        if (cDown)
        {
            input.downPressed = true;
            lastMoveTime = now;
        }
        else
            input.downPressed = false;
    }
    else
    {
        input.leftPressed = input.rightPressed = input.upPressed = input.downPressed = false;
    }
}

bool checkID(byte *readID, byte *targetID, byte size)
{
    for (byte i = 0; i < size; i++)
        if (readID[i] != targetID[i])
            return false;
    return true;
}

// --- 遊戲邏輯 ---
bool tetrominoCell(int shape, int rot, int px, int py)
{
    return (TETROMINO[shape][rot & 3] & (1 << (15 - (py * 4 + px)))) != 0;
}

bool pieceCollides(int testX, int testY, int shape, int rot)
{
    for (int py = 0; py < 4; py++)
    {
        for (int px = 0; px < 4; px++)
        {
            if (!tetrominoCell(shape, rot, px, py))
                continue;
            int gx = testX + px, gy = testY + py;
            if (gx < 0 || gx >= WIDTH || gy < 0 || gy >= HEIGHT)
                return true;
            if (board[gy] & (1 << gx))
                return true;
        }
    }
    return false;
}

void lockPiece()
{
    for (int py = 0; py < 4; py++)
        for (int px = 0; px < 4; px++)
            if (tetrominoCell(currentPiece.shape, currentPiece.rot, px, py))
                if (currentPiece.y + py >= 0)
                    board[currentPiece.y + py] |= (1 << (currentPiece.x + px));

    bool lineCleared = false;
    for (int y = HEIGHT - 1; y >= 0; y--)
    {
        if (board[y] == 0xFFFF)
        {
            for (int yy = y; yy > 0; yy--)
                board[yy] = board[yy - 1];
            board[0] = 0;
            y++;
            gameScore += 100;
            updateScoreDisplay();
            lineCleared = true;
        }
    }

    if (lineCleared)
        playEventSound(1200, 150); // 消除音效 (較高頻)
    else
        playEventSound(200, 50); // 落地音效 (低頻短促)

    // 鎖定後立刻生下一個
    currentPiece.shape = random(7);
    currentPiece.rot = 0;
    currentPiece.x = (WIDTH / 2) - 2;
    currentPiece.y = 0;
    if (pieceCollides(currentPiece.x, currentPiece.y, currentPiece.shape, currentPiece.rot))
    {
        tetrisGameOver = true;
        playEventSound(100, 800); // Game Over 音效
    }
}

void initDino()
{
    dinoY = groundY;
    velY = 0;
    dinoGameOver = false;
    dinoSpeedLevel = 0;
    for (int i = 0; i < MAX_OBS; i++)
        obs[i].x = -1;
    lastDinoBeepScoreLevel = -1;
    showTitle("DINO RUN");
}
void initMeteor()
{
    playerX = WIDTH / 2;
    meteorGameOver = false;
    for (int i = 0; i < 6; i++)
        meteors[i].active = false;
    showTitle("METEOR");
}
void initTetris()
{
    for (int i = 0; i < HEIGHT; i++)
        board[i] = 0;
    tetrisGameOver = false;
    currentPiece.shape = random(7);
    currentPiece.x = 6;
    currentPiece.y = 0;
    showTitle("TETRIS");
}

// --- 貪吃蛇相關函數 ---

bool placeFood()
{
    int tries = 0;
    while (tries < 500)
    {
        int fx = random(WIDTH);
        int fy = random(HEIGHT);
        bool ok = true;
        for (int i = 0; i < snakeLen; i++)
            if (snake[i].x == fx && snake[i].y == fy)
            {
                ok = false;
                break;
            }
        if (ok)
        {
            food.x = fx;
            food.y = fy;
            return true;
        }
        tries++;
    }
    return false;
}

void initSnake()
{
    snakeLen = 3;
    int sx = WIDTH / 2 - 1;
    int sy = HEIGHT / 2;
    for (int i = 0; i < snakeLen; i++)
    {
        snake[i].x = sx - i;
        snake[i].y = sy;
    }
    snakeDir = 1;
    placeFood();
    snakeGameOver = false;
    lastSnakeMove = millis();
    snakeLevel = 0;
    showTitle("SNAKE");
}

void updateSnake()
{
    if (snakeGameOver)
    {
        if (millis() - snakeGameOverAt > 2000)
            currentState = STATE_MENU;
        return;
    }

    int newDir = snakeDir;
    if (input.upPressed && snakeDir != 2)
        newDir = 0;
    if (input.rightPressed && snakeDir != 3)
        newDir = 1;
    if (input.downPressed && snakeDir != 0)
        newDir = 2;
    if (input.leftPressed && snakeDir != 1)
        newDir = 3;
    snakeDir = newDir;

    int effectiveLevel = snakeLevel;
    if (effectiveLevel > SNAKE_MAX_LEVEL)
        effectiveLevel = SNAKE_MAX_LEVEL;
    unsigned long currentInterval;
    unsigned long reduction = (unsigned long)effectiveLevel * SNAKE_SPEED_STEP;
    if (reduction >= (SNAKE_INTERVAL_BASE - SNAKE_INTERVAL_MIN))
        currentInterval = SNAKE_INTERVAL_MIN;
    else
        currentInterval = SNAKE_INTERVAL_BASE - reduction;

    if (millis() - lastSnakeMove > currentInterval)
    {
        lastSnakeMove = millis();
        Point head = snake[0];
        Point newHead = head;
        if (snakeDir == 0)
            newHead.y--;
        else if (snakeDir == 1)
            newHead.x++;
        else if (snakeDir == 2)
            newHead.y++;
        else if (snakeDir == 3)
            newHead.x--;

        if (newHead.x < 0 || newHead.x >= WIDTH || newHead.y < 0 || newHead.y >= HEIGHT)
        {
            snakeGameOver = true;
            snakeGameOverAt = millis();
            playEventSound(150, 600);
            return;
        }

        for (int i = 0; i < snakeLen; i++)
            if (snake[i].x == newHead.x && snake[i].y == newHead.y)
            {
                snakeGameOver = true;
                snakeGameOverAt = millis();
                playEventSound(150, 600);
                return;
            }

        bool ate = (newHead.x == food.x && newHead.y == food.y);

        if (ate)
        {
            for (int i = snakeLen; i > 0; i--)
                snake[i] = snake[i - 1];
            snake[0] = newHead;
            snakeLen++;
            gameScore += 10;
            updateScoreDisplay();
            placeFood();
            playEventSound(1800, 80); // 吃到食物：高音短促
            if (snakeLevel < SNAKE_MAX_LEVEL)
                snakeLevel++;
        }
        else
        {
            for (int i = snakeLen - 1; i > 0; i--)
                snake[i] = snake[i - 1];
            snake[0] = newHead;
        }
    }
}

void updateDino()
{
    unsigned long now = millis();

    if (input.actionPressed && !dinoGameOver && fabs(dinoY - groundY) < 0.1)
    {
        velY = JUMP_IMPULSE;
        playEventSound(600, 50); // 跳躍音效
    }

    if (now - lastPhysics >= PHYSICS_MS)
    {
        lastPhysics = now;
        if (!dinoGameOver)
        {
            velY += GRAVITY;
            dinoY += velY;
            if (dinoY >= groundY)
            {
                dinoY = groundY;
                velY = 0;
            }
        }
    }

    int targetLevel = (int)(gameScore / DINO_SCORE_STEP);
    if (targetLevel > DINO_MAX_LEVEL)
        targetLevel = DINO_MAX_LEVEL;
    if (targetLevel != dinoSpeedLevel)
        dinoSpeedLevel = targetLevel;

    unsigned long currentScroll = SCROLL_MS_BASE;
    if (dinoSpeedLevel > 0)
    {
        unsigned long reduce = (unsigned long)dinoSpeedLevel * DINO_SCROLL_DECREASE_STEP;
        if (reduce >= (SCROLL_MS_BASE - SCROLL_MS_MIN))
            currentScroll = SCROLL_MS_MIN;
        else
            currentScroll = SCROLL_MS_BASE - reduce;
    }

    if (!dinoGameOver && now - lastScroll >= currentScroll)
    {
        lastScroll = now;
        gameScore++;
        updateScoreDisplay();

        int dinoLevel = gameScore / DINO_BEEP_STEP;
        if (dinoLevel != lastDinoBeepScoreLevel)
        {
            lastDinoBeepScoreLevel = dinoLevel;
            playEventSound(2000, 150); // 分數里程碑：很高音
        }

        if (now >= nextSpawnAt)
        {
            for (int i = 0; i < MAX_OBS; i++)
                if (obs[i].x < 0)
                {
                    obs[i].x = WIDTH - 1;
                    obs[i].h = random(1, 4);
                    break;
                }
            nextSpawnAt = now + random(SPAWN_MIN_MS, SPAWN_MAX_MS);
        }

        for (int i = 0; i < MAX_OBS; i++)
        {
            if (obs[i].x >= 0)
            {
                obs[i].x--;
                if (obs[i].x == dinoX && round(dinoY) >= groundY - (obs[i].h - 1))
                {
                    dinoGameOver = true;
                    dinoGameOverAt = millis();
                    playEventSound(100, 800); // 死亡音效
                }
            }
        }
    }

    if (dinoGameOver && (millis() - dinoGameOverAt > 2000))
        currentState = STATE_MENU;
}

void updateMeteor()
{
    if (meteorGameOver)
    {
        if (millis() - meteorGameOverAt > 2000)
            currentState = STATE_MENU;
        return;
    }
    if (input.leftPressed && playerX > 0)
        playerX--;
    if (input.rightPressed && playerX < WIDTH - 1)
        playerX++;
    if (millis() - lastMeteorTick > METEOR_TICK_MS)
    {
        lastMeteorTick = millis();
        if (random(100) < 5)
        {
            for (int i = 0; i < 6; i++)
                if (!meteors[i].active)
                {
                    meteors[i].active = true;
                    meteors[i].x = random(WIDTH);
                    meteors[i].y = -1.0;
                    meteors[i].type = (random(10) > 7);
                    break;
                }
        }
        for (int i = 0; i < 6; i++)
            if (meteors[i].active)
            {
                meteors[i].y += 0.5;
                if (meteors[i].y >= HEIGHT)
                {
                    meteors[i].active = false;
                    gameScore += 10;
                    updateScoreDisplay();
                }
                if ((int)meteors[i].y == playerY && (meteors[i].x == playerX))
                {
                    meteorGameOver = true;
                    meteorGameOverAt = millis();
                    playEventSound(100, 800);
                }
            }
    }
}

void updateTetris()
{
    if (tetrisGameOver)
    {
        if (millis() - tetrisGameOverAt > 2000)
            currentState = STATE_MENU;
        return;
    }
    if (input.leftPressed && !pieceCollides(currentPiece.x - 1, currentPiece.y, currentPiece.shape, currentPiece.rot))
        currentPiece.x--;
    if (input.rightPressed && !pieceCollides(currentPiece.x + 1, currentPiece.y, currentPiece.shape, currentPiece.rot))
        currentPiece.x++;
    if (input.actionPressed)
    {
        int nR = (currentPiece.rot + 1) % 4;
        if (!pieceCollides(currentPiece.x, currentPiece.y, currentPiece.shape, nR))
            currentPiece.rot = nR;
    }
    unsigned long grav = (digitalRead(BTN_DOWN) == LOW) ? TETRIS_SOFT : TETRIS_GRAVITY;
    if (millis() - lastTetrisFall > grav)
    {
        lastTetrisFall = millis();
        if (!pieceCollides(currentPiece.x, currentPiece.y + 1, currentPiece.shape, currentPiece.rot))
            currentPiece.y++;
        else
            lockPiece();
    }
}

void drawGame()
{
    FastLED.clear();
    if (currentState == STATE_MENU)
    {
        fill_solid(leds, NUM_LEDS, CRGB(0, 0, beatsin8(30, 10, 50)));
    }
    else if (currentState == STATE_DINO)
    {
        for (int x = 0; x < WIDTH; x++)
            leds[XY(x, groundY)] = CRGB(20, 20, 20);
        for (int i = 0; i < MAX_OBS; i++)
            if (obs[i].x >= 0)
                for (int k = 0; k < obs[i].h; k++)
                    leds[XY(obs[i].x, groundY - k)] = CRGB::Red;
        leds[XY(dinoX, round(dinoY))] = dinoGameOver ? CRGB::Red : CRGB::Green;
    }
    else if (currentState == STATE_GALAGA)
    {
        leds[XY(playerX, playerY)] = CRGB::Blue;
        for (int i = 0; i < 6; i++)
            if (meteors[i].active)
            {
                int mx = meteors[i].x, my = (int)meteors[i].y;
                if (my >= 0 && my < HEIGHT)
                {
                    leds[XY(mx, my)] = ColorFromPalette(HeatColors_p, random8(160, 255));
                    if (my > 0)
                        leds[XY(mx, my - 1)] = CRGB(100, 30, 0);
                }
            }
    }
    else if (currentState == STATE_TETRIS)
    {
        for (int y = 0; y < HEIGHT; y++)
            for (int x = 0; x < WIDTH; x++)
                if (board[y] & (1 << x))
                    leds[XY(x, y)] = CRGB::Purple;
        for (int py = 0; py < 4; py++)
            for (int px = 0; px < 4; px++)
                if (tetrominoCell(currentPiece.shape, currentPiece.rot, px, py))
                    leds[XY(currentPiece.x + px, currentPiece.y + py)] = CRGB::Cyan;
    }
    else if (currentState == STATE_SNAKE)
    {
        leds[XY(food.x, food.y)] = CRGB::Red;
        for (int i = 0; i < snakeLen; i++)
        {
            if (i == 0)
                leds[XY(snake[i].x, snake[i].y)] = CRGB::Lime;
            else
                leds[XY(snake[i].x, snake[i].y)] = CRGB::Green;
        }
    }
    FastLED.show();
}

void updateBuzzer()
{
    unsigned long now = millis();

    // 如果有重要事件正在發生(吃東西、撞到)，先暫停背景節奏
    if (now < buzzerBlockUntil)
        return;

    // 背景音效控制 (使用短促的Tone模擬節拍，而非持續音)
    switch (currentState)
    {
    case STATE_DINO:
        if (now - lastBackgroundToggle >= 300)
        {
            lastBackgroundToggle = now;
            playTone(300, 20); // 恐龍：中低頻短音，模擬腳步
        }
        break;
    case STATE_TETRIS:
        if (now - lastBackgroundToggle >= 180)
        {
            lastBackgroundToggle = now;
            playTone(800, 10); // Tetris：較高頻，極短的電子音
        }
        break;
    case STATE_SNAKE:
        if (now - lastBackgroundToggle >= 600)
        {
            lastBackgroundToggle = now;
            playTone(200, 30); // 貪吃蛇：低頻，慢節奏
        }
        break;
    case STATE_GALAGA:
        if (now - lastBackgroundToggle >= 220)
        {
            lastBackgroundToggle = now;
            if (random(100) < 25)
            {
                playTone(1200, 15); // 隕石：隨機出現的高頻雜訊
            }
        }
        break;
    default:
        // MENU 狀態不發聲
        break;
    }
}

void setup()
{
    pinMode(PIN_LCD_BL, OUTPUT);
    digitalWrite(PIN_LCD_BL, HIGH);
    tft.init();
    tft.setRotation(3);
    tft.fillScreen(TFT_BLACK);
    pinMode(BTN_UP, INPUT_PULLUP);
    pinMode(BTN_DOWN, INPUT_PULLUP);
    pinMode(BTN_LEFT, INPUT_PULLUP);
    pinMode(BTN_RIGHT, INPUT_PULLUP);
    pinMode(BTN_ACTION, INPUT_PULLUP);

    // 無源蜂鳴器不需要設置 digitalWrite HIGH，只需要 OUTPUT 即可
    pinMode(SPEAKER_PIN, OUTPUT);

    SPI.begin(NFC_SCK, NFC_MISO, NFC_MOSI, NFC_SS_PIN);
    mfrc522.PCD_Init();
    FastLED.addLeds<WS2812, LED_PIN, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(BRIGHTNESS);

    // 開機音效
    playTone(500, 100);
    delay(100);
    playTone(1000, 100);
    delay(100);
    playTone(1500, 200);
}

void loop()
{
    readInputs();
    if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial())
    {
        if (checkID(mfrc522.uid.uidByte, ID_DINO, 4))
        {
            currentState = STATE_DINO;
            initDino();
        }
        else if (checkID(mfrc522.uid.uidByte, ID_GALAGA, 4))
        {
            currentState = STATE_GALAGA;
            initMeteor();
        }
        else if (checkID(mfrc522.uid.uidByte, ID_TETRIS, 4))
        {
            currentState = STATE_TETRIS;
            initTetris();
        }
        else if (checkID(mfrc522.uid.uidByte, ID_SNAKE, 4))
        {
            currentState = STATE_SNAKE;
            initSnake();
        }
        mfrc522.PICC_HaltA();
        mfrc522.PCD_StopCrypto1();
    }
    switch (currentState)
    {
    case STATE_DINO:
        updateDino();
        break;
    case STATE_GALAGA:
        updateMeteor();
        break;
    case STATE_TETRIS:
        updateTetris();
        break;
    case STATE_SNAKE:
        updateSnake();
        break;
    default:
        break;
    }

    updateBuzzer();

    EVERY_N_MILLISECONDS(30) { drawGame(); }
}