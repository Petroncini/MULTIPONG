#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>
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
#define ANGLE_DELTA 0
vector<thread> ballThreads;
std::mutex lockGameState;
std::mutex lockGrid;

struct Ball {
  short id;
  float x, y;
  float vx, vy;

  Ball(int b_id) : id(b_id) {
    x = WIDTH / 2;
    y = HEIGHT / 2;

    // Random angle between -30° and 30° for shallow trajectory
    float angle = (rand() % 60 - 30) * M_PI / 180.0f; // -30° to 30°
    float speed = 0.25f;

    vx = cos(angle) * speed;
    vy = sin(angle) * speed;

    if (rand() % 2 == 0) {
      vx *= -1;
    }
    if (rand() % 2 == 0) {
      vy *= -1;
    }
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

// Reinicia a partida após o fim de uma rodada
void resetGame() {
  lockGameState.lock();
  gameState.round++;

  if (gameState.round % 5 == 0) {
    gameState.phase++;
    Ball b = Ball(gameState.phase);
    gameState.balls.push_back(b);
  }

  for (int b_id = 0; b_id < gameState.balls.size(); b_id++) {
    gameState.balls[b_id] = Ball(b_id);
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

    lockGameState.lock();
    buffer += "║                     ROUND " + to_string(gameState.round + 1) +
              "                     ║\n";
    buffer += "║        PLAYER 1       vs        PLAYER 2        ║\n";
    buffer += "║          " + to_string(gameState.p1score) +
              "                        " + to_string(gameState.p2score) +
              "             ║\n";
    lockGameState.unlock();

    lockGrid.lock();
    for (int i = 0; i < HEIGHT; i++) {
      for (int j = 0; j < WIDTH; j++) {
        // tem que tirar esse negócio de colorir a pá, muito trampo e fica feio
        char c = grid[i][j];
        buffer += c;
      }
      buffer += '\n';
    }
    buffer += "\nPress Ctrl+C to quit.\n";
    lockGrid.unlock();

    // lockGameState.lock();
    // buffer += "Num of balls: " + to_string(gameState.balls.size()) + "\n";
    // lockGameState.unlock();

    cout << buffer << flush;
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

void changeBallAngle(Ball &b, int collidedPaddle) {
  b.vy = b.vy;
  b.vx = -b.vx;
}

void ballThread(int b_id) {
  while (true) {
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

    // Check collision at NEW position BEFORE committing
    Ball temp = b;
    temp.x = nx;
    temp.y = ny;
    int collision = ballCollidePaddle(temp);

    // Handle collision - change angle and bounce
    if (collision != 0) {
      changeBallAngle(b, collision);
      // Clamp position to stay at paddle edge, don't recalculate
      if (collision == 1) {
        nx = 1.0f; // Keep ball just after p1 paddle
      } else {
        nx = WIDTH - 2.0f; // Keep ball just before p2 paddle
      }
    }

    // Now commit the position update
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
void initGameState(void) {
  gameState.phase = 0;
  gameState.round = 0;
  gameState.win = 0;

  Ball b = Ball(0);
  gameState.balls.push_back(b);

  gameState.p1y = HEIGHT / 2;
  gameState.p2y = HEIGHT / 2;

  gameState.p1score = 0;
  gameState.p2score = 0;

  grid.resize(HEIGHT, vector<char>(WIDTH, '.'));

  resetGrid();
  grid[int(b.y)][int(b.x)] = 'O';

}

void showStartScreen() {
  cout << "\033[2J\033[H";
  string buffer = "";
  buffer += "\n\n\n";
  buffer += "  ╔═══════════════════════════════════════════════════╗\n";
  buffer += "  ║                                                   ║\n";
  buffer += "  ║   ███╗   ███╗██╗   ██╗██╗  ████████╗██╗           ║\n";
  buffer += "  ║   ████╗ ████║██║   ██║██║  ╚══██╔══╝██║           ║\n";
  buffer += "  ║   ██╔████╔██║██║   ██║██║     ██║   ██║           ║\n";
  buffer += "  ║   ██║╚██╔╝██║██║   ██║██║     ██║   ██║           ║\n";
  buffer += "  ║   ██║ ╚═╝ ██║╚██████╔╝███████╗██║   ██║           ║\n";
  buffer += "  ║   ╚═╝     ╚═╝ ╚═════╝ ╚══════╝╚═╝   ╚═╝           ║\n";
  buffer += "  ║                                                   ║\n";
  buffer += "  ║           ██████╗  ██████╗ ███╗   ██╗ ██████╗     ║\n";
  buffer += "  ║           ██╔══██╗██╔═══██╗████╗  ██║██╔════╝     ║\n";
  buffer += "  ║           ██████╔╝██║   ██║██╔██╗ ██║██║  ███╗    ║\n";
  buffer += "  ║           ██╔═══╝ ██║   ██║██║╚██╗██║██║   ██║    ║\n";
  buffer += "  ║           ██║     ╚██████╔╝██║ ╚████║╚██████╔╝    ║\n";
  buffer += "  ║           ╚═╝      ╚═════╝ ╚═╝  ╚═══╝ ╚═════╝     ║\n";
  buffer += "  ║                                                   ║\n";
  buffer += "  ║                                                   ║\n";
  buffer += "  ║                 Player 1: W/S                     ║\n";
  buffer += "  ║                 Player 2: I/K                     ║\n";
  buffer += "  ║                                                   ║\n";
  buffer += "  ║          >>> PRESS ENTER TO START <<<             ║\n";
  buffer += "  ║                                                   ║\n";
  buffer += "  ╚═══════════════════════════════════════════════════╝\n";
  cout << buffer << flush;

  // Aguarda o jogador pressionar Enter
  cin.get();
}

int main(void) {

  showStartScreen();

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