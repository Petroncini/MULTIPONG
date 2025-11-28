#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <semaphore>
#include <sys/select.h>
#include <termios.h>
#include <thread>
#include <unistd.h>
#include <vector>

/*
TODO:
- Melhorar a colisão com a pá (tem vezes que era pra colidir mas passa direto)
- Adicionar pontuação do jogador (talvez um mutex separado pra isso)
- Adicionar mudança do ângulo da bola dependendo de onde colidiu com a pá (que
nem no PONG original)
- Adicionar q to quit
- Adicionar efeitos sonoros
- Adicionar placar no topo do grid talvez (pra ficar bonitinho)
- Adicionar mensagem de "player 1 scores!" em algum lugar quando faz ponto
*/

using namespace std;

// Dimensões do display do jogo
#define WIDTH 51
#define HEIGHT 20
vector<thread> ballThreads;
std::mutex lockGameState;
std::mutex lockGrid;

struct Ball {
  short id;
  float x, y;
  float vx, vy;

  Ball(int b_id) : id(b_id) {
    x = 10;
    y = 10;
    srand(time(NULL));
    vx = (rand() % 2 + 1) / 7.5f;
    vy = (rand() % 2 + 1) / 7.5f;
  }

  Ball() {
    x = 10;
    y = 10;
    srand(time(NULL));
    vx = (rand() % 2 + 1) / 10.0f;
    vy = (rand() % 2 + 1) / 10.0f;
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
  int win;
  vector<Ball> balls;
};

// Instância única global do game state
GAMESTATE gameState;
vector<vector<char> > grid;
// Semáforo para controle da atualização do display
binary_semaphore updateGraphics(1);

// Armazenamento das configurações do terminal
struct termios oldt, newt;

// Returns 1 for collision with p1, 2 for collision with p2 and 0 for no
// collision
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

// Função que guarda a configuração original do terminal e gera uma
// nova em raw mode (dispensa enter para processar input)
void enableRawMode() {
  tcgetattr(STDIN_FILENO, &oldt);
  newt = oldt;
  newt.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(STDIN_FILENO, TCSANOW, &newt);
}

// Restaura o estado original do terminal
void disableRawMode() { tcsetattr(STDIN_FILENO, TCSANOW, &oldt); }

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

void resetGrid(void) {
  lockGrid.lock();
  grid.assign(HEIGHT, vector<char>(WIDTH, '.'));

  for (int i = 0; i < HEIGHT; i++) {
    grid[i][WIDTH / 2] = '|';
  }

  grid[max(gameState.p1y - 1, 0)][0] = '#';
  grid[gameState.p1y][0] = '#';
  grid[min(gameState.p1y + 1, HEIGHT - 1)][0] = '#';

  grid[max(gameState.p2y - 1, 0)][WIDTH - 1] = '#';
  grid[gameState.p2y][WIDTH - 1] = '#';
  grid[min(gameState.p2y + 1, HEIGHT - 1)][WIDTH - 1] = '#';
  lockGrid.unlock();
}

void resetGame() {
  lockGameState.lock();
  gameState.round++;

  if (gameState.round % 5 == 0) {
    gameState.phase++;
    Ball b = Ball(gameState.phase);
    gameState.balls.push_back(b);
  }

  for (int b_id = 0; b_id < gameState.balls.size(); b_id++) {

    Ball &b = gameState.balls[b_id];
    b.x = 10 + rand() % 10;
    b.y = 10 + rand() % 10;
  }

  gameState.p1y = HEIGHT / 2;
  gameState.p2y = HEIGHT / 2;
  gameState.win = 0;
  usleep(100000);
  lockGameState.unlock();
  resetGrid();
}

void resetThread() {
  while (true) {
    for (auto &ballThread : ballThreads) {
      ballThread.join();
    }
    ballThreads.clear();
    resetGame();
    lockGameState.lock();
    for (int b_id = 0; b_id < gameState.balls.size(); b_id++) {
      thread ballWorker(ballThread, b_id);
      ballThreads.push_back(std::move(ballWorker));
    }
    lockGameState.unlock();
  }
}

void graphicsThread() {
  cout << "\033[2J";
  while (true) {
    updateGraphics.acquire();
    cout << "\033[H";

    string buffer = "";
    lockGrid.lock();
    for (int i = 0; i < HEIGHT; i++) {
      for (int j = 0; j < WIDTH; j++) {
        // tem que tirar esse negócio de colorir a pá, muito trampo e fica feio
        char c = grid[i][j];
        if (c == '#' && ((gameState.win == 1 && j == 0) ||
                         (gameState.win == 2 && j == WIDTH - 1))) {
          // paddle = green
          buffer += "\033[92m#\033[0m";
        } else if (c == '#') {
          buffer += "\033[37m#\033[0m";
        } else if (c == 'O') {
          // ball = white (normal)
          buffer += "\033[37mO\033[0m";
        } else {
          // everything else (center line, dots) = default
          buffer += c;
        }
      }
      buffer += '\n';
    }
    lockGrid.unlock();
    lockGameState.lock();
    buffer += "Num of balls: " + to_string(gameState.balls.size()) + "\n";
    lockGameState.unlock();

    cout << buffer << flush;

    usleep(33000);
  }
}

void playerThread() {
  while (true) {
    if (kbhit()) {
      char c = getch();
      lockGrid.lock();
      lockGameState.lock();
      if (c == 'w') {
        int oy = gameState.p1y;
        gameState.p1y = max(gameState.p1y - 1, 1);
        if (gameState.p1y != oy) {
          grid[oy + 1][0] = '.';
          grid[gameState.p1y - 1][0] = '#';
          updateGraphics.release();
        }
      } else if (c == 's') {
        int oy = gameState.p1y;
        gameState.p1y = min(gameState.p1y + 1, HEIGHT - 2);
        if (gameState.p1y != oy) {
          grid[oy - 1][0] = '.';
          grid[gameState.p1y + 1][0] = '#';
          updateGraphics.release();
        }
      }
      if (c == 'i') {
        int oy = gameState.p2y;
        gameState.p2y = max(gameState.p2y - 1, 1);
        if (gameState.p2y != oy) {
          grid[oy + 1][WIDTH - 1] = '.';
          grid[gameState.p2y - 1][WIDTH - 1] = '#';
          updateGraphics.release();
        }
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

void ballThread(int b_id) {
  while (true) {
    lockGameState.lock();
    Ball &b = gameState.balls[b_id];
    // talvez já da um unlock no gameState aqui? NÃO, só se fizer uma cópia da bola em vez de usar referencia
    float ox = b.x;
    float oy = b.y;
    float nx = ox + b.vx;
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
    b.x = ix;
    b.y = iy;
    int collision = ballCollidePaddle(b);

    // aqui depois tem que verificar se collison é 1 ou 2 pra alterar a
    // pontuação;
    lockGrid.lock();
    if (collision != 0) {
      grid[iy][ix] = '#';
    } else if (ix <= 0 || ix >= WIDTH - 1) {
      if (ix == 0) {
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
      usleep(10000);
      return;
    } else if (ix == WIDTH / 2) {
      grid[iy][ix] = '|';
    } else {
      grid[iy][ix] = '.';
    }

    b.x = nx;
    b.y = ny;
    lockGameState.unlock();
    grid[b.y][b.x] = 'O';
    lockGrid.unlock();
    updateGraphics.release();

    // isso devia ser uma variável que vai diminuindo com o tempo
    usleep(20000);
  }
}

void initGameState(void) {
  lockGameState.lock();
  gameState.phase = 0;
  gameState.round = 0;
  gameState.win = 0;

  Ball b = Ball(0);
  gameState.balls.push_back(b);

  gameState.p1y = HEIGHT / 2;
  gameState.p2y = HEIGHT / 2;

  gameState.p1score = 0;
  gameState.p2score = 0;

  lockGrid.lock();
  grid.resize(HEIGHT, vector<char>(WIDTH, '.'));
  lockGrid.unlock();

  resetGrid();
  lockGrid.lock();
  grid[int(b.y)][int(b.x)] = 'O';
  lockGrid.unlock();
  lockGameState.unlock();
}

int main(void) {
  initGameState();

  enableRawMode();

  thread graphics_worker(graphicsThread);
  thread player_worker(playerThread);
  thread ball_worker(ballThread, gameState.phase);
  thread resetWorker(resetThread);

  ballThreads.push_back(std::move(ball_worker));

  graphics_worker.join();
  player_worker.join();
  resetWorker.join();
}