#include <algorithm>
#include <iostream>
#include <semaphore>
#include <sys/select.h>
#include <termios.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include <ctime>
#include <cstdlib>

using namespace std;

// Dimensões do display do jogo
#define WIDTH 51
#define HEIGHT 20

struct Ball {
  short id;
  float x, y;
  float vx, vy;

  Ball(int b_id) : id(b_id) {
    x = 10;
    y = 10;
    srand(time(0));
    vx = (rand() % 3) / 10;
    srand(time(0));
    vy = (rand() % 3) / 10;
  }

  Ball() {
    x = 10;
    y = 10;
    srand(time(0));
    vx = (rand() % 3) / 10;
    srand(time(0));
    vy = (rand() % 3) / 10;
  }
};

// Definição dos atributos do jogo
struct GAMESTATE {
  int round;
  int phase;
  int p1y;
  int p2y;
  int p1score;
  int p2score;
  vector<Ball> balls;
  vector<vector<char> > grid;
};

// Instância única global do game state
GAMESTATE gameState;
// Semáforo para controle da atualização do display
binary_semaphore updateGraphics(1);

// Armazenamento das configurações do terminal
struct termios oldt, newt;

// Returns 1 for collision with p1, 2 for collision with p2 and 0 for no collision
int ballCollidePaddle(Ball& b) {
  int ix = int(b.x);
  int iy = int(b.y);

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

// Função que guarda a configuração original do terminal e gera uma
// nova em raw mode (dispensa enter para processar input)
void enableRawMode() {
  tcgetattr(STDIN_FILENO, &oldt);
  newt = oldt;
  newt.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(STDIN_FILENO, TCSANOW, &newt);
}

// Restaura o estado original do terminal
void disableRawMode() {
  tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
}

int kbhit() {
  struct timeval tv = {0L, 0L};
  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(STDIN_FILENO, &fds);
  return select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
}

// Leitura de um caracter da entrada padrão
char getch() {
  char c;
  if (read(STDIN_FILENO, &c, 1) < 0)
    return 0;
  return c;
}

void ballThread(int b_id);

void resetGame() {
  gameState.round++;

  if (gameState.round % 3 == 0) {
    Ball b = Ball(gameState.phase);
    gameState.balls.push_back(b);

    thread ballworker(ballThread, gameState.phase);
    ballworker.join();
  }

  for (int b_id = 0; b_id <= gameState.phase; b_id++) {
    Ball& b = gameState.balls[b_id];
    gameState.grid[int(b.y)][int(b.x)] = 'O';
  }

  gameState.p1y = HEIGHT / 2;
  gameState.p2y = HEIGHT / 2;

}

void graphicsThread() {
  // Loop infinito que atualiza o display sempre que há alguma
  // alteração no game state
  while (true) {
    updateGraphics.acquire();
    cout << "\033[2J\033[1;1H";

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

void ballThread(int b_id) {
  Ball b = gameState.balls[b_id];
  while (true) {
    float ox = b.x;
    float oy = b.y;
    float nx = b.vx;
    float ny = oy + b.vy;

    if (ny >= HEIGHT - 1 || ny <= 0) {
      b.vy *= -1;
    }
    if (nx >= WIDTH - 1 || nx <= 0) {
      b.vx *= -1;
    }

    nx = max(0.0f, min(float(WIDTH - 1), nx));
    ny = max(0.0f, min(float(HEIGHT - 1), ny));

    int ix = int(ox);
    int iy = int(oy);
    int collision = ballCollidePaddle(b);

    // aqui depois tem que verificar se collison é 1 ou 2 pra alterar a
    // pontuação;
    if (collision != 0) {
      gameState.grid[iy][ix] = '#';
      resetGame();
    } else if (ix == WIDTH / 2) {
      gameState.grid[iy][ix] = '|';
    } else {
      gameState.grid[iy][ix] = '.';
    }

    b.x = nx;
    b.y = ny;
    gameState.grid[ny][nx] = 'O';
    updateGraphics.release();

    // isso devia ser uma variável que vai diminuindo com o tempo
    usleep(20000);
  }
}

void initGameState(void) {
  gameState.phase = 0;
  gameState.round = 0;

  Ball b = Ball(0);
  gameState.balls[0] = b;

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

  gameState.grid[int(b.y)][int(b.x)] = 'O';

}

int main(void) {
  initGameState();

  enableRawMode();

  thread graphics_worker(graphicsThread);
  thread p1_worker(playerThread);
  thread ball_worker(ballThread, gameState.phase);

  graphics_worker.join();
  p1_worker.join();
  ball_worker.join();
}