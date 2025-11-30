# **MULTIPONG**

## **Manual do Jogo**

### **Desenvolvedores**

* Caio Petroncini 
* Caroline Akimi Kurosaki Ueda
* Natalie Isernhagen Coelho

## **1\. Instalação e Execução**

Este jogo foi desenvolvido em C++20 e utiliza threads nativas do Linux/Unix. A execução do jogo requer compilador G++ com suporte a C++20 e um terminal Linux.

### **Compilação**

Abra o terminal no diretório onde está o arquivo do código *pong.cpp* e execute o comando abaixo. A flag \-std=c++20 é necessária para a biblioteca dos semáforos.

| g++ pong.cpp \-o pong \-std=c++20 |
| :---- |

### **Como executar**

Após compilar, inicie o jogo com:

| ./pong |
| :---- |

## **2\. Como Jogar**

O objetivo é rebater a bola e evitar que ela passe pela sua "pá" (barra vertical). Se a bola passar pela sua defesa e atingir a parede no seu lado, o oponente ganha um ponto.

**Iniciar Jogo:** Pressione **ENTER** na tela inicial.

**Player 1 (Esquerda):**

- **W**: Move para Cima  
- **S**: Move para Baixo

**Player 2 (Direita):**

- **I**: Move para Cima  
- **K**: Move para Baixo

A cada 5 rodadas, uma nova bola é adicionada ao jogo, progredindo a dificuldade.

## **3\. Implementação de threads e semáforos**

O jogo utiliza threads e semáforos para que as diferentes atividades do programa sejam executadas concorrentemente e para garantir um acesso controlado aos recursos compartilhados.

**Threads**

Foram utilizadas threads para separar as responsabilidades do jogo. A concorrência garante que a física, o input do usuário e o desenho da tela funcionem de forma fluida, sem um travar o outro.

O jogo é dividido em 4 *workers*:

1. **Thread de gráficos (graphicsThread):** Sua única função é desenhar o estado atual do jogo na tela. Ela roda somente quando alguma outra thread sinaliza mudança no estado do jogo pelo semáforo *updateGraphics*.  
2. **Thread do jogador (playerThread):** Lida com a entrada do teclado e muda a posição da pá do jogador no estado do jogo. Ela roda separada para que o jogo não congele enquanto espera o input do jogador.  
3. **Thread da bola (ballThread):** Calcula a física, movimento e colisão da bola. Cada bola nova cria sua própria thread independente.  
4. **Thread de reset (resetThread):** Limpa a tela, reinicia as bolas e começa um novo round quando uma rodada termina.

### **Semáforos**

Como o acesso simultâneo de múltiplas threads às mesmas variáveis compartilhadas do jogo gera uma condição de corrida, alguns semáforos foram implementados para garantir a sincronização e controle do acesso.

**Mutexes *lockGrid* e *lockGameState*:**  
São usados para que mais de uma thread não atualize a matriz de caracteres do display ou os atributos do jogo ao mesmo tempo, o que pode ter resultados indefinidos. Antes de acessar essas variáveis, é preciso “trancar” o acesso via um lock() no semáforo. Enquanto o mutex está em lock, qualquer outra thread que tentar trancar para acessar o recurso será bloqueada até que a thread que trancou primeiro dê um unlock().

* Na **ballThread**, antes de atualizar a posição da bola no jogo e alterar a aparição dela na matriz do grid, é feito um lock() nos mutexes.  
* Na **playerThread**, antes de alterar a posição da pá do jogador ao receber input e de atualizar a posição da barra no grid.   
* Na função **resetGrid**, chamada pela resetThread, antes de limpar o display e reposicionar as pás dos jogadores, o lockGrid é trancado.  
* Na função **resetGame**, o mutex é travado para reinicializar o jogo após uma rodada.  
* Na **resetThread,** há alterações nas threads de ball, por isso, o mutex é necessário.  
* Na função **graphicsThread**, o lockGameState é trancado antes de mostrar qual a rodada e os scores de cada jogador. O lockGrid também é trancado antes de exibir o grid.  
* Na função **initGameState**, é feito um lock() nos mutexes para inicializar o jogo.

**Semáforo updateGraphics:**

É usado para que a thread de gráficos seja acordada somente quando alguma mudança no display for sinalizada por algum dos outros workers. Assim, ela não roda constantemente sem necessidade, o que otimiza o processamento. O semáforo é inicializado com o valor de 1 para que o display seja carregado de início.

* A **graphicsThread** chama um acquire() do semáforo updateGraphics. Se o valor é zero, então a thread gráfica é bloqueada e fica inativa até que as outras threads sinalizem um release().  
* Quando a **playerThread** e a **ballThread** mudam a localização da pá ou das bolas, ou a **resetThread** altera o display, elas fazem um release() no semáforo updateGraphics e assim a thread de gráficos é acordada para renderizar a nova tela.
