#include <algorithm>
#include <iostream>
#include <semaphore>
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
  vector<vector<char> > grid;
};

GAMESTATE gameState;
binary_semaphore updateGraphics(1);

struct termios oldt, newt;

int ballCollidePaddle() {
  int ix = int(gameState.bx);
  int iy = int(gameState.by);

  if (ix == 0 && (iy == gameState.p1y - 1 || iy == gameState.p1y ||
                  iy == gameState.p1y + 1)) {
    return 1;
  } else if (ix == WIDTH - 1 &&
             (iy == gameState.p2y - 1 || iy == gameState.p2y ||
              iy == gameState.p2y + 1)) {
    return 2;
  } else {
    return 0;
  }
}

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
    updateGraphics.acquire();
    std::cout << "\033[2J\033[1;1H";

    for (int i = 0; i < HEIGHT; i++) {
      for (int j = 0; j < WIDTH; j++) {
        cout << gameState.grid[i][j];
      }
      cout << endl;
    }
  }
}

void playerThread() {
  while (true) {
    if (kbhit()) {
      char c = getch();
      if (c == 'w') {
        int oy = gameState.p1y;
        gameState.p1y = max(gameState.p1y - 1, 1);
        if (gameState.p1y != oy) {
          gameState.grid[oy + 1][0] = '.';
          gameState.grid[gameState.p1y - 1][0] = '#';
          updateGraphics.release();
        }
      } else if (c == 's') {
        int oy = gameState.p1y;
        gameState.p1y = min(gameState.p1y + 1, HEIGHT - 2);
        if (gameState.p1y != oy) {
          gameState.grid[oy - 1][0] = '.';
          gameState.grid[gameState.p1y + 1][0] = '#';
          updateGraphics.release();
        }
      }
      if (c == 'i') {
        int oy = gameState.p2y;
        gameState.p2y = max(gameState.p2y - 1, 1);
        if (gameState.p2y != oy) {
          gameState.grid[oy + 1][WIDTH - 1] = '.';
          gameState.grid[gameState.p2y - 1][WIDTH - 1] = '#';
          updateGraphics.release();
        }
      } else if (c == 'k') {
        int oy = gameState.p2y;
        gameState.p2y = min(gameState.p2y + 1, HEIGHT - 2);
        if (gameState.p2y != oy) {
          gameState.grid[oy - 1][WIDTH - 1] = '.';
          gameState.grid[gameState.p2y + 1][WIDTH - 1] = '#';
          updateGraphics.release();
        }
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

    int ix = int(ox);
    int iy = int(oy);
    int collision = ballCollidePaddle();

    // aqui depois tem que verificar se collison é 1 ou 2 pra alterar a
    // pontuação;
    if (collision != 0) {
      gameState.grid[iy][ix] = '#';
    } else if (ix == WIDTH / 2) {
      gameState.grid[iy][ix] = '|';
    } else {
      gameState.grid[iy][ix] = '.';
    }

    gameState.bx = nx;
    gameState.by = ny;
    gameState.grid[ny][nx] = 'O';
    updateGraphics.release();

    // isso devia ser uma variável que vai diminuindo com o tempo
    usleep(20000);
  }
}

void initGameState(void) {
  gameState.bx = 10;
  gameState.by = 10;
  gameState.bvx = 0.2;
  gameState.bvy = 0.2;
  gameState.p1y = HEIGHT / 2;
  gameState.p2y = HEIGHT / 2;

  gameState.grid.resize(HEIGHT, vector<char>(WIDTH, '.'));

  for (int i = 0; i < HEIGHT; i++) {
    gameState.grid[i][0] = '.';
    gameState.grid[i][WIDTH / 2] = '|';
    gameState.grid[i][WIDTH - 1] = '.';
  }

  gameState.grid[max(gameState.p1y - 1, 0)][0] = '#';
  gameState.grid[gameState.p1y][0] = '#';
  gameState.grid[min(gameState.p1y + 1, HEIGHT - 1)][0] = '#';

  gameState.grid[max(gameState.p2y - 1, 0)][WIDTH - 1] = '#';
  gameState.grid[gameState.p2y][WIDTH - 1] = '#';
  gameState.grid[min(gameState.p2y + 1, HEIGHT - 1)][WIDTH - 1] = '#';

  gameState.grid[int(gameState.by)][int(gameState.bx)] = 'O';
}

int main(void) {
  initGameState();

  enableRawMode();

  thread graphics_worker(graphicsThread);
  thread p1_worker(playerThread);
  thread ball_worker(ballThread);

  graphics_worker.join();
  p1_worker.join();
  ball_worker.join();
}