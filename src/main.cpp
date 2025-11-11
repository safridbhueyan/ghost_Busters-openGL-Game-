// --------------------------------------------------------------------------
//                Ghost Busters — OpenGL mini-arcade (visual upgrade)
//    Upgrades: gradient sky, parallax stars, glow, particles, shake, trails
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
#include <algorithm>

// Function declarations and shared data
void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void processInput(GLFWwindow *window, float deltaTime);

// =====================[ Shaders ]=====================
// Vertex: send through transform and also pass transformed position to fragment
const char* vertexShaderSource = R"GLSL(
#version 330 core
layout (location = 0) in vec3 aPos;
uniform mat4 transform;
out vec3 vWorldPos;   // position after transform (NDC-ish during our simple pipeline)
void main() {
    vec4 p = transform * vec4(aPos, 1.0);
    vWorldPos = p.xyz;
    gl_Position = p;
}
)GLSL";

// Fragment: two modes — solid color, or vertical gradient
// Also supports a "glow" multiplier (for pulsing ghosts/objects)
const char *fragmentShaderSource = R"GLSL(
#version 330 core
out vec4 FragColor;
in vec3 vWorldPos;

uniform vec4  ourColor;
uniform int   useGradient;
uniform vec3  gradTop;
uniform vec3  gradBottom;
uniform float glow;        // 1.0 = normal, >1 brighter, <1 dimmer

void main() {
    vec3 color;
    if (useGradient == 1) {
        // Map NDC y (-1..1) -> 0..1
        float t = clamp(vWorldPos.y * 0.5 + 0.5, 0.0, 1.0);
        color = mix(gradBottom, gradTop, t);
        FragColor = vec4(color, 1.0);
    } else {
        color = ourColor.rgb * glow;
        FragColor = vec4(color, ourColor.a);
    }
}
)GLSL";

// =====================[ Constants ]===================
const unsigned int SCR_WIDTH  = 800;
const unsigned int SCR_HEIGHT = 600;

// world units are NDC-like in [-1,1]
const float PLAYER_W = 0.18f;
const float PLAYER_H = 0.06f;
const float PLAYER_Y = -0.85f;

const float BULLET_W = 0.02f;
const float BULLET_H = 0.06f;
const float BULLET_SPEED = 2.6f;     // slightly faster for snappier feel
const float SHOOT_COOLDOWN = 0.22f;  // a touch tighter

const int   MAX_GHOSTS = 8;
const float GHOST_W = 0.10f;
const float GHOST_H = 0.10f;
const float GHOST_SPEED_MIN = 0.35f;
const float GHOST_SPEED_MAX = 0.75f;
const float GHOST_DROP = 0.04f;

const glm::vec3 COLOR_BG_TOP     = glm::vec3(0.12f, 0.00f, 0.20f);
const glm::vec3 COLOR_BG_BOTTOM  = glm::vec3(0.02f, 0.02f, 0.08f);
const glm::vec3 COLOR_PLAYER     = glm::vec3(0.10f, 0.90f, 0.90f);
const glm::vec3 COLOR_BULLET     = glm::vec3(1.00f, 0.95f, 0.30f);
const glm::vec3 COLOR_GHOST      = glm::vec3(0.90f, 0.10f, 0.95f);
const glm::vec3 COLOR_EYES       = glm::vec3(1.00f, 1.00f, 1.00f);
const glm::vec3 COLOR_DIVIDER    = glm::vec3(0.28f, 0.28f, 0.32f);

// =====================[ Globals ]=====================
float playerX = 0.0f;
float playerSpeed = 1.7f;

bool  bulletActive = false;
float bulletX = 0.0f, bulletY = -1.5f;
float shootTimer = 0.0f;

int   score = 0;
int   lives = 3;
bool  gameOver = false;

float timeNow = 0.0f;

// Screen shake on life loss
float shakeTimer = 0.0f;
float shakeStrength = 0.0f;

struct Ghost {
    float x, y;
    float vx;   // horizontal velocity (sign gives direction)
    bool  alive;
    float phase; // per-ghost sine wave offset
};
std::vector<Ghost> ghosts;

struct Particle {
    glm::vec2 pos;
    glm::vec2 vel;
    float life;     // 0..1
    float size;
};
std::vector<Particle> particles;

// Parallax stars
struct Star {
    glm::vec2 pos;
    float speed; // vertical speed
    float size;
    float alpha;
};
std::vector<Star> stars;

const int STAR_COUNT = 120;

const char* windowBase = "Ghost Busters";

// utility: AABB vs AABB (center/extent style)
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
        g.phase = frand(0.0f, 6.28318f);
        ghosts.push_back(g);
    }
}

static void initStars() {
    stars.clear();
    stars.reserve(STAR_COUNT);
    for (int i=0;i<STAR_COUNT;++i) {
        Star s;
        s.pos = glm::vec2(frand(-1.0f, 1.0f), frand(-1.0f, 1.0f));
        float layer = frand(0.0f, 1.0f);
        s.speed = 0.05f + layer * 0.25f;  // parallax
        s.size = 0.004f + layer * 0.01f;
        s.alpha = 0.5f + layer * 0.5f;
        stars.push_back(s);
    }
}

static void resetGame() {
    score = 0;
    lives = 3;
    gameOver = false;
    playerX = 0.0f;
    bulletActive = false;
    shootTimer = 0.0f;
    particles.clear();
    initStars();
    spawnWave(6);
}

// ============ OpenGL helpers =============
static unsigned int shaderProgram;
static int uTransformLoc, uColorLoc, uUseGradientLoc, uGradTopLoc, uGradBottomLoc, uGlowLoc;

static inline void setSolidMode() {
    glUniform1i(uUseGradientLoc, 0);
    glUniform1f(uGlowLoc, 1.0f);
}
static inline void setGradientMode(const glm::vec3& top, const glm::vec3& bottom) {
    glUniform1i(uUseGradientLoc, 1);
    glUniform3f(uGradTopLoc, top.r, top.g, top.b);
    glUniform3f(uGradBottomLoc, bottom.r, bottom.g, bottom.b);
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
    glfwSwapInterval(1); // vsync for smoother motion

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
    shaderProgram = glCreateProgram();
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
    // Single unit quad centered at origin (size 1x1), we scale/translate in world
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
    uTransformLoc   = glGetUniformLocation(shaderProgram, "transform");
    uColorLoc       = glGetUniformLocation(shaderProgram, "ourColor");
    uUseGradientLoc = glGetUniformLocation(shaderProgram, "useGradient");
    uGradTopLoc     = glGetUniformLocation(shaderProgram, "gradTop");
    uGradBottomLoc  = glGetUniformLocation(shaderProgram, "gradBottom");
    uGlowLoc        = glGetUniformLocation(shaderProgram, "glow");

    float lastFrame  = 0.0f;

    resetGame();

    // =====================[ Main Loop ]=====================
    while (!glfwWindowShouldClose(window))
    {
        timeNow = (float)glfwGetTime();
        float deltaTime = timeNow - lastFrame;
        lastFrame = timeNow;
        shootTimer += deltaTime;

        processInput(window, deltaTime);

        // restart if 'R'
        if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS && gameOver) {
            resetGame();
        }

        // ---- Update ----
        if (!gameOver)
        {
            // Bullet update
            if (bulletActive) {
                bulletY += BULLET_SPEED * deltaTime;
                if (bulletY > 1.1f) bulletActive = false;
            }

            // Ghosts update
            int aliveCount = 0;
            for (auto &g : ghosts) {
                if (!g.alive) continue;
                aliveCount++;

                // horizontal movement + wall bounce and drop
                g.x += g.vx * deltaTime;

                // Add subtle wave/bob to give life
                float bob = sin(timeNow * 2.0f + g.phase) * 0.12f;
                g.x += bob * deltaTime;

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
                    // Trigger a stronger shake on life loss
                    shakeTimer = 0.25f;
                    shakeStrength = 0.025f;
                }

                // bullet collision
                if (bulletActive &&
                    aabbHit(bulletX, bulletY, BULLET_W, BULLET_H,
                            g.x, g.y, GHOST_W, GHOST_H))
                {
                    g.alive = false;
                    bulletActive = false;
                    score += 10;

                    // small global speed-up as difficulty ramp
                    for (auto &gg : ghosts) {
                        gg.vx *= 1.035f;
                    }

                    // Explosion particles
                    int puff = 24;
                    for (int i=0;i<puff;++i) {
                        float ang = frand(0.0f, 6.28318f);
                        float spd = frand(0.25f, 1.0f);
                        Particle p;
                        p.pos = glm::vec2(g.x, g.y);
                        p.vel = glm::vec2(cosf(ang), sinf(ang)) * spd;
                        p.life = 1.0f;
                        p.size = frand(0.012f, 0.028f);
                        particles.push_back(p);
                    }

                    // light camera shake
                    shakeTimer = std::max(shakeTimer, 0.15f);
                    shakeStrength = std::max(shakeStrength, 0.015f);
                }
            }

            // all ghosts cleared → next wave
            if (!gameOver && aliveCount == 0) {
                int nextCount = std::min(MAX_GHOSTS, 4 + (score / 20)); // gradually increase count
                float speedScale = 1.0f + (score / 100.0f);
                spawnWave(nextCount, speedScale);
            }

            // Update particles
            for (auto &p : particles) {
                p.life -= deltaTime * 1.4f;
                p.pos += p.vel * deltaTime;
                p.vel *= (1.0f - 0.9f * deltaTime); // gentle drag
            }
            particles.erase(std::remove_if(particles.begin(), particles.end(),
                [](const Particle& p){ return p.life <= 0.0f; }), particles.end());

            // Update stars (vertical drift, wrap)
            for (auto &s : stars) {
                s.pos.y -= s.speed * deltaTime;
                if (s.pos.y < -1.05f) {
                    s.pos.y = 1.05f;
                    s.pos.x = frand(-1.0f, 1.0f);
                    s.alpha = 0.5f + frand(0.0f, 0.5f);
                    s.size  = 0.004f + frand(0.0f, 0.01f);
                }
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
        glClearColor(0,0,0,1);
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(shaderProgram);

        // View (screen shake)
        glm::mat4 view(1.0f);
        if (shakeTimer > 0.0f) {
            float s = shakeStrength * (shakeTimer / 0.25f);
            float ox = frand(-s, s);
            float oy = frand(-s, s);
            view = glm::translate(glm::mat4(1.0f), glm::vec3(ox, oy, 0.0f));
            shakeTimer -= deltaTime;
            if (shakeTimer < 0.0f) shakeTimer = 0.0f;
        }

        // helper to draw rectangles
        auto drawRect = [&](const glm::vec3& pos, const glm::vec2& size, const glm::vec4& color, float glowMul = 1.0f){
            glm::mat4 t = view;
            t = glm::translate(t, pos);
            t = glm::scale(t, glm::vec3(size.x, size.y, 1.0f));
            glUniformMatrix4fv(uTransformLoc, 1, GL_FALSE, glm::value_ptr(t));
            glUniform4f(uColorLoc, color.r, color.g, color.b, color.a);
            glUniform1f(uGlowLoc, glowMul);
            glUniform1i(uUseGradientLoc, 0);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        };

        // helper to draw gradient fullscreen background
        auto drawGradientBG = [&](){
            glm::mat4 t = glm::scale(glm::mat4(1.0f), glm::vec3(2.0f, 2.0f, 1.0f));
            glUniformMatrix4fv(uTransformLoc, 1, GL_FALSE, glm::value_ptr(t));
            setGradientMode(COLOR_BG_TOP, COLOR_BG_BOTTOM);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        };

        // Background gradient
        drawGradientBG();

        // Parallax stars (render as tiny rects, additive-ish via glow)
        for (auto &s : stars) {
            float twinkle = 0.85f + 0.15f * sinf(timeNow * (2.0f + s.speed*6.0f) + s.pos.x*10.0f);
            float a = s.alpha * twinkle;
            drawRect(glm::vec3(s.pos, 0.0f), glm::vec2(s.size, s.size),
                     glm::vec4(1.0f, 1.0f, 1.0f, a), 1.2f);
        }

        // bottom divider line
        drawRect(glm::vec3(0.0f, PLAYER_Y + PLAYER_H*0.5f + 0.02f, 0.0f),
                 glm::vec2(0.01f, 2.0f), glm::vec4(COLOR_DIVIDER, 1.0f));

        // player blaster (base + turret) with subtle glow pulse while bullet is active cooldown
        float playerPulse = 1.0f + 0.25f * std::max(0.0f, (SHOOT_COOLDOWN - shootTimer)) / SHOOT_COOLDOWN;
        drawRect(glm::vec3(playerX, PLAYER_Y, 0.0f),
                 glm::vec2(PLAYER_W, PLAYER_H), glm::vec4(COLOR_PLAYER, 1.0f), playerPulse);
        drawRect(glm::vec3(playerX, PLAYER_Y + PLAYER_H*0.35f, 0.0f),
                 glm::vec2(PLAYER_W*0.35f, PLAYER_H*0.6f), glm::vec4(COLOR_PLAYER, 1.0f), playerPulse);

        // bullet (with simple trail)
        if (bulletActive) {
            drawRect(glm::vec3(bulletX, bulletY, 0.0f),
                     glm::vec2(BULLET_W, BULLET_H), glm::vec4(COLOR_BULLET, 1.0f), 1.2f);
            // trail quads fading behind
            drawRect(glm::vec3(bulletX, bulletY - BULLET_H*0.8f, 0.0f),
                     glm::vec2(BULLET_W*0.9f, BULLET_H*0.6f), glm::vec4(COLOR_BULLET, 0.6f), 1.0f);
            drawRect(glm::vec3(bulletX, bulletY - BULLET_H*1.5f, 0.0f),
                     glm::vec2(BULLET_W*0.8f, BULLET_H*0.4f), glm::vec4(COLOR_BULLET, 0.35f), 0.9f);
        }

        // ghosts (body + eyes); add glow pulse
        for (auto &g : ghosts) if (g.alive) {
            float glow = 0.85f + 0.35f * sinf(timeNow * 3.0f + g.phase);
            // body
            drawRect(glm::vec3(g.x, g.y, 0.0f),
                     glm::vec2(GHOST_W, GHOST_H), glm::vec4(COLOR_GHOST, 1.0f), glow);
            // eyes
            float eyeOffX = GHOST_W * 0.18f;
            float eyeOffY = GHOST_H * 0.10f;
            glm::vec2 eyeSize = glm::vec2(GHOST_W*0.14f, GHOST_H*0.14f);
            drawRect(glm::vec3(g.x - eyeOffX, g.y + eyeOffY, 0.0f),
                     eyeSize, glm::vec4(COLOR_EYES, 1.0f), 1.0f);
            drawRect(glm::vec3(g.x + eyeOffX, g.y + eyeOffY, 0.0f),
                     eyeSize, glm::vec4(COLOR_EYES, 1.0f), 1.0f);
        }

        // particles (explosions)
        for (auto &p : particles) {
            float a = glm::clamp(p.life, 0.0f, 1.0f);
            glm::vec4 col = glm::vec4(1.0f, 0.85f, 0.25f, a);
            drawRect(glm::vec3(p.pos, 0.0f), glm::vec2(p.size, p.size), col, 1.0f + 0.5f*a);
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
