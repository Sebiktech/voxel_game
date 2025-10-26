 #pragma once
#include <glm/glm.hpp>

// Forward-declare to avoid including GLFW in the header
struct GLFWwindow;

class FPSCamera {
public:
    // Public state you may tweak
    glm::vec3 position{ 8.0f, 8.0f, 30.0f };
    float yaw = -90.0f;  // degrees, -Z forward
    float pitch = 0.0f;  // degrees
    float speed = 10.0f;   // units/sec
    float sensitivity = 0.1f; // deg per pixel
    float fovYdeg = 60.0f;
    float zNear = 0.1f, zFar = 1000.0f;

    // Call on startup and after swapchain resize
    void setViewportSize(uint32_t width, uint32_t height);

    // Per-frame input
    void handleMouse(GLFWwindow* window);
    void handleKeys(GLFWwindow* window, float dt);

    // Matrices
    glm::mat4 view()  const;
    glm::mat4 proj()  const;     // Vulkan clip space (Y flipped)
    glm::mat4 model() const { return glm::mat4(1.0f); }
    glm::mat4 mvp()   const { return proj() * view() * model(); }

    // Cursor capture toggle (optional)
    void setCursorCaptured(GLFWwindow* window, bool captured);
    bool isCursorCaptured() const { return cursorCaptured; }

    // derived
    glm::vec3 forward() const;
    glm::vec3 right()   const;
    glm::vec3 up()      const;
private:
    uint32_t vpWidth = 1, vpHeight = 1;
    bool cursorCaptured = true;

    // mouse state
    bool firstMouse = true;
    double lastMouseX = 0.0, lastMouseY = 0.0;
};