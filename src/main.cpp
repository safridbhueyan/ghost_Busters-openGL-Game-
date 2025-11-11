// --------------------------------------------------------------------------
//                Ghost Busters — a tiny OpenGL arcade shooter
//     (Built by editing the provided Pong skeleton to a full mini-game)
// --------------------------------------------------------------------------

#include "glad.h"
#include "glfw3.h"
#include "glm/glm/glm.hpp"
#include "glm/glm/gtc/matrix_transform.hpp"
#include "glm/glm/gtc/type_ptr.hpp"
#include <iostream>
#include <string>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <vector>

// Function declarations and shared data
void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void processInput(GLFWwindow *window, float deltaTime);

// =====================[ Shaders ]=====================
const char* vertexShaderSource = "#version 330 core\n"
"layout (location = 0) in vec3 aPos;\n"
"uniform mat4 transform;\n"
"void main()\n"
"{\n"
"    gl_Position = transform * vec4(aPos, 1.0);\n"
"}\0";

const char *fragmentShaderSource = "#version 330 core\n"
"out vec4 FragColor;\n"
"uniform vec4 ourColor;\n"
"void main()\n"
"{\n"
"    FragColor = ourColor;\n"
"}\n\0";

// =====================[ Constants ]===================
const unsigned int SCR_WIDTH  = 800;
const unsigned int SCR_HEIGHT = 600;

// world units are NDC-like in [-1,1]
const float PLAYER_W = 0.18f;
const float PLAYER_H = 0.06f;
const float PLAYER_Y = -0.85f;

const float BULLET_W = 0.02f;
const float BULLET_H = 0.06f;
const float BULLET_SPEED = 2.2f;
const float SHOOT_COOLDOWN = 0.25f; // seconds

const int   MAX_GHOSTS = 8;
const float GHOST_W = 0.10f;
const float GHOST_H = 0.10f;
const float GHOST_SPEED_MIN = 0.35f;
const float GHOST_SPEED_MAX = 0.75f;
const float GHOST_DROP = 0.04f;

const glm::vec3 COLOR_BG      = glm::vec3(0.05f, 0.05f, 0.07f);
const glm::vec3 COLOR_PLAYER  = glm::vec3(0.10f, 0.90f, 0.25f);
const glm::vec3 COLOR_BULLET  = glm::vec3(1.00f, 0.95f, 0.30f);
const glm::vec3 COLOR_GHOST   = glm::vec3(0.85f, 0.10f, 0.90f);
const glm::vec3 COLOR_EYES    = glm::vec3(1.00f, 1.00f, 1.00f);
const glm::vec3 COLOR_DIVIDER = glm::vec3(0.35f, 0.35f, 0.38f);

// =====================[ Globals ]=====================
float playerX = 0.0f;
float playerSpeed = 1.5f;

bool  bulletActive = false;
float bulletX = 0.0f, bulletY = -1.5f;
float shootTimer = 0.0f;

int   score = 0;
int   lives = 3;
bool  gameOver = false;

struct Ghost {
    float x, y;
    float vx;   // horizontal velocity (sign gives direction)
    bool  alive;
};
std::vector<Ghost> ghosts;

const char* windowBase = "Ghost Busters";

// utility: AABB vs AABB
static inline bool aabbHit(float ax, float ay, float aw, float ah,
                           float bx, float by, float bw, float bh)
{
    return std::fabs(ax - bx) * 2.0f < (aw + bw) &&
           std::fabs(ay - by) * 2.0f < (ah + bh);
}

// random helper
static inline float frand(float a, float b) {
    return a + (b - a) * (float)(rand() % 10000) / 10000.0f;
}

static void spawnWave(int n, float speedScale = 1.0f) {
    ghosts.clear();
    n = std::min(n, MAX_GHOSTS);
    for (int i = 0; i < n; ++i) {
        Ghost g;
        g.x = frand(-0.85f, 0.85f);
        g.y = frand(0.20f, 0.90f);
        float sp = frand(GHOST_SPEED_MIN, GHOST_SPEED_MAX) * speedScale;
        g.vx = (rand() % 2 ? sp : -sp);
        g.alive = true;
        ghosts.push_back(g);
    }
}

static void resetGame() {
    score = 0;
    lives = 3;
    gameOver = false;
    playerX = 0.0f;
    bulletActive = false;
    shootTimer = 0.0f;
    spawnWave(6);
}

int main()
{
    srand((unsigned)time(NULL));
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, windowBase, NULL, NULL);
    if (window == NULL)
    {
        std::cout << "Failed to create GLFW window\n";
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cout << "Failed to initialize GLAD\n";
        return -1;
    }

    // ----[ SHADER COMPILATION / PROGRAM LINKING ]----
    unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
    glCompileShader(vertexShader);
    int success; char infoLog[512];
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
        std::cout << "ERROR::SHADER::VERTEX::COMPILATION_FAILED\n" << infoLog << std::endl;
    }
    unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
    glCompileShader(fragmentShader);
    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(fragmentShader, 512, NULL, infoLog);
        std::cout << "ERROR::SHADER::FRAGMENT::COMPILATION_FAILED\n" << infoLog << std::endl;
    }
    unsigned int shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);
    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(shaderProgram, 512, NULL, infoLog);
        std::cout << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n" << infoLog << std::endl;
    }
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    // ----[ VERTEX ARRAY / VERTEX BUFFER ]----
    float vertices[] = {
         0.5f,  0.5f, 0.0f,
         0.5f, -0.5f, 0.0f,
        -0.5f, -0.5f, 0.0f,
         0.5f,  0.5f, 0.0f,
        -0.5f, -0.5f, 0.0f,
        -0.5f,  0.5f, 0.0f
    };
    unsigned int VBO, VAO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glUseProgram(shaderProgram);
    int transformLoc = glGetUniformLocation(shaderProgram, "transform");
    int colorLoc     = glGetUniformLocation(shaderProgram, "ourColor");
    float lastFrame  = 0.0f;

    resetGame();

    // =====================[ Main Loop ]=====================
    while (!glfwWindowShouldClose(window))
    {
        float currentFrame = (float)glfwGetTime();
        float deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;
        shootTimer += deltaTime;

        processInput(window, deltaTime);

        // restart if 'R'
        if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS && gameOver) {
            resetGame();
        }

        if (!gameOver)
        {
            // ---- Bullet update ----
            if (bulletActive) {
                bulletY += BULLET_SPEED * deltaTime;
                if (bulletY > 1.1f) bulletActive = false;
            }

            // ---- Ghosts update ----
            int aliveCount = 0;
            for (auto &g : ghosts) {
                if (!g.alive) continue;
                aliveCount++;

                // horizontal movement + wall bounce and drop
                g.x += g.vx * deltaTime;
                if (g.x + GHOST_W * 0.5f > 1.0f) {
                    g.x = 1.0f - GHOST_W * 0.5f;
                    g.vx = -std::fabs(g.vx);
                    g.y -= GHOST_DROP;
                } else if (g.x - GHOST_W * 0.5f < -1.0f) {
                    g.x = -1.0f + GHOST_W * 0.5f;
                    g.vx =  std::fabs(g.vx);
                    g.y -= GHOST_DROP;
                }

                // reached player line?
                if (g.y - GHOST_H * 0.5f <= PLAYER_Y + PLAYER_H * 0.5f) {
                    g.alive = false;
                    if (--lives <= 0) {
                        gameOver = true;
                    }
                }

                // bullet collision
                if (bulletActive &&
                    aabbHit(bulletX, bulletY, BULLET_W, BULLET_H,
                            g.x, g.y, GHOST_W, GHOST_H))
                {
                    g.alive = false;
                    bulletActive = false;
                    score += 10;
                    // slight global speed-up as difficulty ramp
                    for (auto &gg : ghosts) {
                        gg.vx *= 1.03f;
                    }
                }
            }

            // all ghosts cleared → next wave
            if (!gameOver && aliveCount == 0) {
                int nextCount = std::min(MAX_GHOSTS, 4 + (score / 20)); // gradually increase count
                float speedScale = 1.0f + (score / 100.0f);
                spawnWave(nextCount, speedScale);
            }
        }

        // ---- Dynamic window title ----
        std::string title;
        if (gameOver) {
            title = std::string("Ghost Busters  |  SCORE: ") + std::to_string(score) +
                    "   GAME OVER  (press R to restart)";
        } else {
            title = std::string("Ghost Busters  |  SCORE: ") + std::to_string(score) +
                    "   LIVES: " + std::to_string(lives) +
                    "   [A/D or \xE2\x86\x90\xE2\x86\x92 to move, SPACE to shoot]";
        }
        glfwSetWindowTitle(window, title.c_str());

        // =====================[ Rendering ]=====================
        glClearColor(COLOR_BG.r, COLOR_BG.g, COLOR_BG.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(shaderProgram);
        glBindVertexArray(VAO);

        // helper to draw rectangles
        auto drawRect = [&](const glm::vec3& pos, const glm::vec2& size, const glm::vec4& color){
            glm::mat4 t = glm::translate(glm::mat4(1.0f), pos);
            t = glm::scale(t, glm::vec3(size.x, size.y, 1.0f));
            glUniformMatrix4fv(transformLoc, 1, GL_FALSE, glm::value_ptr(t));
            glUniform4f(colorLoc, color.r, color.g, color.b, color.a);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        };

        // bottom divider line
        drawRect(glm::vec3(0.0f, PLAYER_Y + PLAYER_H*0.5f + 0.02f, 0.0f),
                 glm::vec2(0.01f, 2.0f), glm::vec4(COLOR_DIVIDER, 1.0f));

        // player blaster (a little "turret" look using two rectangles)
        drawRect(glm::vec3(playerX, PLAYER_Y, 0.0f),
                 glm::vec2(PLAYER_W, PLAYER_H), glm::vec4(COLOR_PLAYER, 1.0f));
        drawRect(glm::vec3(playerX, PLAYER_Y + PLAYER_H*0.35f, 0.0f),
                 glm::vec2(PLAYER_W*0.35f, PLAYER_H*0.6f), glm::vec4(COLOR_PLAYER, 1.0f));

        // bullet
        if (bulletActive) {
            drawRect(glm::vec3(bulletX, bulletY, 0.0f),
                     glm::vec2(BULLET_W, BULLET_H), glm::vec4(COLOR_BULLET, 1.0f));
        }

        // ghosts (body + eyes using rectangles for simplicity)
        for (auto &g : ghosts) if (g.alive) {
            // body
            drawRect(glm::vec3(g.x, g.y, 0.0f),
                     glm::vec2(GHOST_W, GHOST_H), glm::vec4(COLOR_GHOST, 1.0f));
            // eyes
            float eyeOffX = GHOST_W * 0.18f;
            float eyeOffY = GHOST_H * 0.10f;
            glm::vec2 eyeSize = glm::vec2(GHOST_W*0.14f, GHOST_H*0.14f);
            drawRect(glm::vec3(g.x - eyeOffX, g.y + eyeOffY, 0.0f),
                     eyeSize, glm::vec4(COLOR_EYES, 1.0f));
            drawRect(glm::vec3(g.x + eyeOffX, g.y + eyeOffY, 0.0f),
                     eyeSize, glm::vec4(COLOR_EYES, 1.0f));
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // Resource cleanup
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteProgram(shaderProgram);
    glfwTerminate();
    return 0;
}

// =====================[ Input ]=====================
void processInput(GLFWwindow *window, float deltaTime)
{
    // move player
    float move = playerSpeed * deltaTime;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS)
        playerX -= move;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS)
        playerX += move;
    // keep on screen
    if (playerX + PLAYER_W*0.5f > 1.0f)  playerX = 1.0f - PLAYER_W*0.5f;
    if (playerX - PLAYER_W*0.5f < -1.0f) playerX = -1.0f + PLAYER_W*0.5f;

    // shooting (single bullet on screen, basic cooldown)
    if (!gameOver && glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) {
        if (!bulletActive && shootTimer >= SHOOT_COOLDOWN) {
            bulletActive = true;
            bulletX = playerX;
            bulletY = PLAYER_Y + PLAYER_H*0.5f + BULLET_H*0.6f;
            shootTimer = 0.0f;
        }
    }

    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);
}

// =====================[ Resize ]=====================
void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    glViewport(0, 0, width, height);
}
