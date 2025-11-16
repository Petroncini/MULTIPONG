#include <algorithm>
#include <iostream>
#include <sys/select.h>
#include <termios.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace std;

#define WIDTH 51
#define HEIGHT 20

struct GAMESTATE {
  int p1y;
  int p2y;
  int p1score;
  int p2score;
  float bx, by;
  float bvx, bvy;
};

GAMESTATE gameState;

struct termios oldt, newt;

void enableRawMode() {
  tcgetattr(STDIN_FILENO, &oldt);
  newt = oldt;
  newt.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(STDIN_FILENO, TCSANOW, &newt);
}

void disableRawMode() { tcsetattr(STDIN_FILENO, TCSANOW, &oldt); }

int kbhit() {
  struct timeval tv = {0L, 0L};
  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(STDIN_FILENO, &fds);
  return select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
}

char getch() {
  char c;
  if (read(STDIN_FILENO, &c, 1) < 0)
    return 0;
  return c;
}

void graphicsThread() {

  while (true) {
    vector<vector<char> > grid(HEIGHT, vector<char>(WIDTH, '.'));
    std::cout << "\033[2J\033[1;1H";

    for (int i = 0; i < HEIGHT; i++) {
      grid[i][0] = '.';
      grid[i][WIDTH / 2] = '|';
      grid[i][WIDTH - 1] = '.';
    }

    grid[max(gameState.p1y - 1, 0)][0] = '#';
    grid[gameState.p1y][0] = '#';
    grid[min(gameState.p1y + 1, HEIGHT - 1)][0] = '#';

    grid[max(gameState.p2y - 1, 0)][WIDTH - 1] = '#';
    grid[gameState.p2y][WIDTH - 1] = '#';
    grid[min(gameState.p2y + 1, HEIGHT - 1)][WIDTH - 1] = '#';

    grid[int(gameState.by)][int(gameState.bx)] = 'O';

    for (int i = 0; i < HEIGHT; i++) {
      for (int j = 0; j < WIDTH; j++) {
        cout << grid[i][j];
      }
      cout << endl;
    }
    usleep(5000);
  }
}

void playerThread() {
  while (true) {
    if (kbhit()) {
      char c = getch();
      if (c == 'w') {
        gameState.p1y = max(gameState.p1y - 1, 1);
      } else if (c == 's') {
        gameState.p1y = min(gameState.p1y + 1, HEIGHT - 1);
      }
      if (c == 'i') {
        gameState.p2y = max(gameState.p2y - 1, 1);
      } else if (c == 'k') {
        gameState.p2y = min(gameState.p2y + 1, HEIGHT - 1);
      }
    }
  }
}

void ballThread() {
  while (true) {
    float ox = gameState.bx;
    float oy = gameState.by;
    float nx = ox + gameState.bvx;
    float ny = oy + gameState.bvy;

    if (ny >= HEIGHT - 1 || ny <= 0) {
      gameState.bvy *= -1;
    }
    if (nx >= WIDTH - 1 || nx <= 0) {
      gameState.bvx *= -1;
    }

    nx = max(0.0f, min(float(WIDTH - 1), nx));
    ny = max(0.0f, min(float(HEIGHT - 1), ny));

    gameState.bx = nx;
    gameState.by = ny;

    usleep(10000);
  }
}

int main(void) {
  gameState.bx = 10;
  gameState.by = 10;
  gameState.bvx = 0.2;
  gameState.bvy = -0.2;
  gameState.p1y = HEIGHT / 2;
  gameState.p2y = HEIGHT / 2;

  enableRawMode();

  thread graphics_worker(graphicsThread);
  thread p1_worker(playerThread);
  thread ball_worker(ballThread);

  graphics_worker.join();
  p1_worker.join();
  ball_worker.join();
}