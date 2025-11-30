#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <semaphore>
#include <sys/select.h>
#include <termios.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace std;

// Display dimensions
#define WIDTH 51
#define HEIGHT 20

// Ball structure
struct Ball {
  float x, y;
  float vx, vy;

  Ball() {
    x = WIDTH / 2;
    y = HEIGHT / 2;

    // Random angle between -30° and 30° for shallow trajectory
    float angle = (rand() % 60 - 30) * M_PI / 180.0f;
    float speed = 0.30f;

    vx = cos(angle) * speed;
    vy = sin(angle) * speed;

    // Randomize initial direction
    if (rand() % 2 == 0) {
      vx *= -1;
    }
    if (rand() % 2 == 0) {
      vy *= -1;
    }
  }
};

// Game state attributes
struct GAMESTATE {
  int round;
  int phase;
  int p1y;
  int p2y;
  int p1score;
  int p2score;
  int win;
  vector<Ball> balls;
};

// Global game state instance
GAMESTATE gameState;
// Display grid
vector<vector<char> > grid;
// Vector to store ball threads
vector<thread> ballThreads;
// Semaphore for display update control
binary_semaphore updateGraphics(1);
// Mutex for game state protection
std::mutex lockGameState;
// Mutex for grid protection
std::mutex lockGrid;
// Global quit flag
bool QUIT = false;

// Terminal configuration storage
struct termios oldt, newt;

// Saves original terminal config and enables raw mode (no enter needed)
void enableRawMode() {
  tcgetattr(STDIN_FILENO, &oldt);
  newt = oldt;
  newt.c_lflag &= ~(ICANON | ECHO);
  newt.c_cc[VMIN] = 0;
  newt.c_cc[VTIME] = 0;
  tcsetattr(STDIN_FILENO, TCSANOW, &newt);
}

// Restores original terminal state
void disableRawMode() {
  tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
}

// Checks if a key was pressed
int kbhit() {
  struct timeval tv = {0L, 0L};
  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(STDIN_FILENO, &fds);
  return select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
}

// Reads a character from stdin
char getch() {
  char c;
  if (read(STDIN_FILENO, &c, 1) < 0)
    return 0;
  return c;
}

// Thread responsible for rendering the game
void graphicsThread() {
  const string BLUE = "\033[34m";
  const string GREEN = "\033[32m";
  const string RESET = "\033[0m";

  cout << "\033[2J";
  while (!QUIT) {
    updateGraphics.acquire();
    cout << "\033[H";

    string buffer = "";

    // Build header with scores
    lockGameState.lock();
    buffer += "                     ROUND " + to_string(gameState.round + 1) +
              "                     \n";
    buffer += "        " + BLUE + "PLAYER 1" + RESET + "       vs        " + 
              GREEN + "PLAYER 2" + RESET + "        \n";
    buffer += "           " + to_string(gameState.p1score) +
              "                        " + to_string(gameState.p2score) +
              "             \n";
    lockGameState.unlock();

    // Build grid display
    lockGrid.lock();
    for (int i = 0; i < HEIGHT; i++) {
      for (int j = 0; j < WIDTH; j++) {
        char c = grid[i][j];
        buffer += c;
      }
      buffer += '\n';
    }
    buffer += "Press Q to quit.";
    lockGrid.unlock();

    cout << buffer << flush;
  }
}

// Recreates the grid
void resetGrid(void) {
  lockGrid.lock();
  grid.assign(HEIGHT, vector<char>(WIDTH, '.'));

  // Draw center line
  for (int i = 0; i < HEIGHT; i++) {
    grid[i][WIDTH / 2] = '|';
  }

  // Draw P1 paddle
  grid[max(gameState.p1y - 1, 0)][0] = '#';
  grid[gameState.p1y][0] = '#';
  grid[min(gameState.p1y + 1, HEIGHT - 1)][0] = '#';

  // Draw P2 paddle
  grid[max(gameState.p2y - 1, 0)][WIDTH - 1] = '#';
  grid[gameState.p2y][WIDTH - 1] = '#';
  grid[min(gameState.p2y + 1, HEIGHT - 1)][WIDTH - 1] = '#';
  lockGrid.unlock();
}

// Resets game after a round ends
void resetGame() {
  lockGameState.lock();
  gameState.round++;

  // Add new ball every 5 rounds
  if (gameState.round % 3 == 0) {
    gameState.phase++;
    Ball b = Ball();
    gameState.balls.push_back(b);
  }

  // Reset all balls
  for (int b_id = 0; b_id < gameState.balls.size(); b_id++) {
    gameState.balls[b_id] = Ball();
  }

  // Reset paddle positions
  gameState.p1y = HEIGHT / 2;
  gameState.p2y = HEIGHT / 2;
  gameState.win = 0;
  usleep(100000);
  lockGameState.unlock();
  resetGrid();
}

void ballThread(int b_id);
// Thread that controls reset after scoring
void resetThread() {
  while (!QUIT) {
    // Wait for all ball threads to finish
    for (auto &ballThread : ballThreads) {
      ballThread.join();
    }
    ballThreads.clear();
    if (QUIT)
      break;
    resetGame();
    // Restart ball threads
    lockGameState.lock();
    for (int b_id = 0; b_id < gameState.balls.size(); b_id++) {
      thread ballWorker(ballThread, b_id);
      ballThreads.push_back(std::move(ballWorker));
    }
    lockGameState.unlock();
  }
}

// Thread that reads keys and moves paddles
void playerThread() {
  while (!QUIT) {
    if (kbhit()) {
      char c = getch();
      // Check for quit
      if (c == 'q') {
        QUIT = true;
        updateGraphics.release();
        break;
      }
      lockGrid.lock();
      lockGameState.lock();
      // Player 1 up
      if (c == 'w') {
        int oy = gameState.p1y;
        gameState.p1y = max(gameState.p1y - 1, 1);
        if (gameState.p1y != oy) {
          grid[oy + 1][0] = '.';
          grid[gameState.p1y - 1][0] = '#';
          updateGraphics.release();
        }
      // Player 1 down
      } else if (c == 's') {
        int oy = gameState.p1y;
        gameState.p1y = min(gameState.p1y + 1, HEIGHT - 2);
        if (gameState.p1y != oy) {
          grid[oy - 1][0] = '.';
          grid[gameState.p1y + 1][0] = '#';
          updateGraphics.release();
        }
      }
      // Player 2 up
      if (c == 'i') {
        int oy = gameState.p2y;
        gameState.p2y = max(gameState.p2y - 1, 1);
        if (gameState.p2y != oy) {
          grid[oy + 1][WIDTH - 1] = '.';
          grid[gameState.p2y - 1][WIDTH - 1] = '#';
          updateGraphics.release();
        }
      // Player 2 down
      } else if (c == 'k') {
        int oy = gameState.p2y;
        gameState.p2y = min(gameState.p2y + 1, HEIGHT - 2);
        if (gameState.p2y != oy) {
          grid[oy - 1][WIDTH - 1] = '.';
          grid[gameState.p2y + 1][WIDTH - 1] = '#';
          updateGraphics.release();
        }
      }
      lockGrid.unlock();
      lockGameState.unlock();
    }
  }
}

// Changes ball angle based on where it hits the paddle
void changeBallAngle(Ball &b, int collidedPaddle) {
    float paddleCenterY = (collidedPaddle == 1) ? gameState.p1y : gameState.p2y;
    float relativeIntersectY = b.y - paddleCenterY;
    float normalizedRelativeIntersectionY = relativeIntersectY / 1.5f;
    
    // Clamp normalized value
    if (normalizedRelativeIntersectionY > 1.0f)
      normalizedRelativeIntersectionY = 1.0f;

    if (normalizedRelativeIntersectionY < -1.0f)
      normalizedRelativeIntersectionY = -1.0f;

    // Calculate new angle
    float maxBounceAngle = 60.0f * M_PI / 180.0f;
    float bounceAngle = normalizedRelativeIntersectionY * maxBounceAngle;
    float currentSpeed = std::sqrt(b.vx * b.vx + b.vy * b.vy);
    float direction = (collidedPaddle == 1) ? 1.0f : -1.0f;

    // Apply new velocity
    b.vx = currentSpeed * std::cos(bounceAngle) * direction;
    b.vy = currentSpeed * std::sin(bounceAngle);
}

// Returns 1 for P1 collision, 2 for P2 collision, 0 for no collision
int ballCollidePaddle(Ball &b) {
  int ix = int(b.x);
  int iy = int(b.y);

  if (ix <= 0 && (iy == gameState.p1y - 1 || iy == gameState.p1y ||
                  iy == gameState.p1y + 1)) {
    return 1;
  } else if (ix >= WIDTH - 1 &&
             (iy == gameState.p2y - 1 || iy == gameState.p2y ||
              iy == gameState.p2y + 1)) {
    return 2;
  } else {
    return 0;
  }
}

// Ball thread
void ballThread(int b_id) {
  while (!QUIT) {
    lockGameState.lock();
    Ball &b = gameState.balls[b_id];

    // Calculate new position
    float nx = b.x + b.vx;
    float ny = b.y + b.vy;

    // Bounce off top/bottom walls
    if (ny > HEIGHT - 1 || ny < 0) {
      b.vy *= -1;
      ny = max(0.0f, min(float(HEIGHT - 1), ny));
    }

    // Store old position for rendering
    int old_x = int(b.x);
    int old_y = int(b.y);

    // Check collision at new position
    Ball temp = b;
    temp.x = nx;
    temp.y = ny;
    int collision = ballCollidePaddle(temp);

    // Handle collision - change angle and bounce
    if (collision != 0) {
      changeBallAngle(b, collision);
      // Clamp position to paddle edge
      if (collision == 1) {
        nx = 1.0f;
      } else {
        nx = WIDTH - 2.0f;
      }
    }

    // Update ball position
    b.x = nx;
    b.y = ny;
    int new_x = int(b.x);
    int new_y = int(b.y);

    lockGrid.lock();

    // Handle scoring (ball passed paddle)
    if (collision == 0 && (new_x <= 0 || new_x >= WIDTH - 1)) {
      if (new_x <= 0) {
        gameState.p2score++;
        gameState.win = 2;
      } else {
        gameState.p1score++;
        gameState.win = 1;
      }
      lockGrid.unlock();
      lockGameState.unlock();
      resetGrid();
      updateGraphics.release();
      return;
    }

    // Clear old position
    if (old_x == WIDTH / 2) {
      grid[old_y][old_x] = '|';
    } else if (grid[old_y][old_x] == 'O') {
      grid[old_y][old_x] = '.';
    }

    // Draw new position
    grid[new_y][new_x] = 'O';

    lockGrid.unlock();
    lockGameState.unlock();
    updateGraphics.release();

    usleep(20000);
  }
}

// Initializes game state
void initGameState(void) {
  gameState.phase = 0;
  gameState.round = 0;
  gameState.win = 0;

  Ball b = Ball();
  gameState.balls.push_back(b);

  gameState.p1y = HEIGHT / 2;
  gameState.p2y = HEIGHT / 2;

  gameState.p1score = 0;
  gameState.p2score = 0;

  grid.resize(HEIGHT, vector<char>(WIDTH, '.'));

  resetGrid();
  grid[int(b.y)][int(b.x)] = 'O';

}

// Start screen
void showStartScreen() {
  cout << "\033[2J\033[H";
  string buffer = "";
  buffer += "\n\n\n";
  buffer += "╔═══════════════════════════════════════════════════╗\n";
  buffer += "║                                                   ║\n";
  buffer += "║   ███╗   ███╗██╗   ██╗██╗  ████████╗██╗           ║\n";
  buffer += "║   ████╗ ████║██║   ██║██║  ╚══██╔══╝██║           ║\n";
  buffer += "║   ██╔████╔██║██║   ██║██║     ██║   ██║           ║\n";
  buffer += "║   ██║╚██╔╝██║██║   ██║██║     ██║   ██║           ║\n";
  buffer += "║   ██║ ╚═╝ ██║╚██████╔╝███████╗██║   ██║           ║\n";
  buffer += "║   ╚═╝     ╚═╝ ╚═════╝ ╚══════╝╚═╝   ╚═╝           ║\n";
  buffer += "║                                                   ║\n";
  buffer += "║           ██████╗  ██████╗ ███╗   ██╗ ██████╗     ║\n";
  buffer += "║           ██╔══██╗██╔═══██╗████╗  ██║██╔════╝     ║\n";
  buffer += "║           ██████╔╝██║   ██║██╔██╗ ██║██║  ███╗    ║\n";
  buffer += "║           ██╔═══╝ ██║   ██║██║╚██╗██║██║   ██║    ║\n";
  buffer += "║           ██║     ╚██████╔╝██║ ╚████║╚██████╔╝    ║\n";
  buffer += "║           ╚═╝      ╚═════╝ ╚═╝  ╚═══╝ ╚═════╝     ║\n";
  buffer += "║                                                   ║\n";
  buffer += "║                                                   ║\n";
  buffer += "║                 Player 1: W/S                     ║\n";
  buffer += "║                 Player 2: I/K                     ║\n";
  buffer += "║                                                   ║\n";
  buffer += "║          >>> PRESS ENTER TO START <<<             ║\n";
  buffer += "║                                                   ║\n";
  buffer += "╚═══════════════════════════════════════════════════╝\n";
  cout << buffer << flush;

  // Wait for player to press Enter
  cin.get();
}

int main(void) {

  showStartScreen();
  initGameState();
  enableRawMode();

  // Start all threads
  thread graphics_worker(graphicsThread);
  thread player_worker(playerThread);
  thread ball_worker(ballThread, gameState.phase);
  thread resetWorker(resetThread);

  ballThreads.push_back(std::move(ball_worker));

  // Wait for threads to finish
  graphics_worker.join();
  player_worker.join();
  resetWorker.join();

  disableRawMode();
}