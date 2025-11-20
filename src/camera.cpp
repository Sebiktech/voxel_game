#include "camera.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>

void FPSCamera::setViewportSize(uint32_t width, uint32_t height) {
    vpWidth = std::max(1u, width);
    vpHeight = std::max(1u, height);
}

void FPSCamera::setCursorCaptured(GLFWwindow* window, bool captured) {
    cursorCaptured = captured;
    glfwSetInputMode(window, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    if (captured) resetMouse(window);
}

glm::vec3 FPSCamera::forward() const {
    float cy = glm::radians(yaw);
    float cp = glm::radians(pitch);
    glm::vec3 f{ std::cos(cp) * std::cos(cy), std::sin(cp), std::cos(cp) * std::sin(cy) };
    return glm::normalize(f);
}
glm::vec3 FPSCamera::right() const {
    return glm::normalize(glm::cross(forward(), glm::vec3(0, 1, 0)));
}
glm::vec3 FPSCamera::up() const {
    return glm::normalize(glm::cross(right(), forward()));
}

glm::mat4 FPSCamera::view() const {
    auto f = forward();
    return glm::lookAt(position, position + f, up());
}

glm::mat4 FPSCamera::proj() const {
    float aspect = (float)vpWidth / (float)vpHeight;
    glm::mat4 P = glm::perspective(glm::radians(fovYdeg), aspect, zNear, zFar);
    // Vulkan NDC has Y flipped
    P[1][1] *= -1.0f;
    return P;
}

void FPSCamera::resetMouse(GLFWwindow* win) {
    // read current cursor and set as last to avoid a big jump
    double x, y;
    glfwGetCursorPos(win, &x, &y);
    lastMouseX = float(x);
    lastMouseY = float(y);
    firstMouse = true;         // if you use this flag
    // no yaw/pitch change here; just re-arm deltas
}

void FPSCamera::handleMouse(GLFWwindow* window) {
    if (!cursorCaptured) return;
    double mx, my;
    glfwGetCursorPos(window, &mx, &my);
    if (firstMouse) { lastMouseX = mx; lastMouseY = my; firstMouse = false; }

    double dx = mx - lastMouseX;
    double dy = my - lastMouseY;
    lastMouseX = mx; lastMouseY = my;

    yaw += (float)(dx * sensitivity);
    pitch -= (float)(dy * sensitivity);
    pitch = std::clamp(pitch, -89.0f, 89.0f);
}

void FPSCamera::handleKeys(GLFWwindow* window, float dt) {
    glm::vec3 v(0.0f);
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) v += forward();
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) v -= forward();
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) v += right();
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) v -= right();
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) v += glm::vec3(0, 1, 0);
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) v -= glm::vec3(0, 1, 0);

    // speed boost with Ctrl
    float currentSpeed = (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) ? (speed * 2.5f) : speed;

    if (glm::length(v) > 0.0f) v = glm::normalize(v);
    position += v * (currentSpeed * dt);

    // Toggle capture with Esc (optional)
    static bool prevEsc = false;
    bool esc = glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS;
    if (esc && !prevEsc) setCursorCaptured(window, !cursorCaptured);
    prevEsc = esc;
}