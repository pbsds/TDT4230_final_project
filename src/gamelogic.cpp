#include "gamelogic.h"
#include "sceneGraph.hpp"
#include <GLFW/glfw3.h>
#include <SFML/Audio/Sound.hpp>
#include <SFML/Audio/SoundBuffer.hpp>
#include <chrono>
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/vec3.hpp>
#include <iostream>
#include <string>
#include <utilities/glfont.h>
#include <utilities/glutils.h>
#include <utilities/imageLoader.hpp>
#include <utilities/mesh.h>
#include <utilities/shader.hpp>
#include <utilities/shapes.h>
#include <utilities/timeutils.h>

using glm::vec3;
using glm::mat4;
typedef unsigned int uint;

enum KeyFrameAction {
    BOTTOM, TOP
};

#include <timestamps.h>

uint currentKeyFrame = 0;
uint previousKeyFrame = 0;

SceneNode* rootNode;
SceneNode* plainNode;
SceneNode* boxNode;
SceneNode* ballNode;
SceneNode* padNode;
SceneNode* hudNode;
SceneNode* textNode;

const uint N_LIGHTS = 1;
SceneNode* lightNode[N_LIGHTS];

// These are heap allocated, because they should not be initialised at the start of the program
sf::Sound* sound;
sf::SoundBuffer* buffer;
Gloom::Shader* default_shader;
Gloom::Shader* test_shader;
Gloom::Shader* plain_shader;
Gloom::Shader* post_shader;

vec3 cameraPosition = vec3(0, 0, 400);
vec3 cameraLookAt = vec3(500, 500, 0);
vec3 cameraUpward = vec3(0, 0, 1);

CommandLineOptions options;

bool hasStarted = false;
bool hasLost = false;
bool jumpedToNextFrame = false;

// Modify if you want the music to start further on in the track. Measured in seconds.
const float debug_startTime = 45;
double totalElapsedTime = debug_startTime;

// textures
PNGImage t_charmap       = loadPNGFile("../res/textures/charmap.png");
PNGImage t_cobble_diff   = loadPNGFile("../res/textures/cobble_diff.png");
PNGImage t_cobble_normal = loadPNGFile("../res/textures/cobble_normal.png");
PNGImage t_plain_diff    = loadPNGFile("../res/textures/plain_diff.png");
PNGImage t_plain_normal  = loadPNGFile("../res/textures/plain_normal.png");
PNGImage t_perlin        = makePerlinNoisePNG(256, 256, {0.1, 0.2, 0.3});


void mouseCallback(GLFWwindow* window, double x, double y) {
    int windowWidth, windowHeight;
    glfwGetWindowSize(window, &windowWidth, &windowHeight);
    glViewport(0, 0, windowWidth, windowHeight);

    float mousePositionX = x / double(windowHeight); // like the hudNode space
    float mousePositionY = y / double(windowHeight);
    /*
    if(padPositionX > 1) {
        padPositionX = 1;
        glfwSetCursorPos(window, windowWidth, y);
    } else if(padPositionX < 0) {
        padPositionX = 0;
        glfwSetCursorPos(window, 0, y);
    }
    if(padPositionY > 1) {
        padPositionY = 1;
        glfwSetCursorPos(window, x, windowHeight);
    } else if(padPositionY < 0) {
        padPositionY = 0;
        glfwSetCursorPos(window, x, 0);
    }
    */
}

void initGame(GLFWwindow* window, CommandLineOptions gameOptions) {
    buffer = new sf::SoundBuffer();
    if (!buffer->loadFromFile("../res/Hall of the Mountain King.ogg")) {
        return;
    }

    options = gameOptions;

    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
    glfwSetCursorPosCallback(window, mouseCallback);

    // load shaders
    default_shader = new Gloom::Shader();
    default_shader->makeBasicShader("../res/shaders/simple.vert", "../res/shaders/simple.frag");
    
    Mesh plain = generateSegmentedPlane(1000, 1000, 100, 100);
    Mesh hello_world = generateTextGeometryBuffer("Skjer'a bagera?", 1.3, 2);

    rootNode = createSceneNode();
    hudNode = createSceneNode();
    
    plainNode = createSceneNode();
    plainNode->setTexture(&t_plain_diff, &t_plain_normal);
    plainNode->setMesh(&plain);
    plainNode->position = {0, 0, 0};
    plainNode->shinyness = 30;
    rootNode->children.push_back(plainNode);

    // add lights
    for (uint i = 0; i<N_LIGHTS; i++) {
        lightNode[i] = createSceneNode(POINT_LIGHT);
        lightNode[i]->lightID = i;
        rootNode->children.push_back(lightNode[0]);
    }
    lightNode[0]->position = {200, 800, 600};
    lightNode[0]->color_emissive = vec3(0.2);
    lightNode[0]->color_diffuse  = vec3(0.8);
    lightNode[0]->color_specular = vec3(0.0);
    lightNode[0]->attenuation = vec3(1.0, 0.0, 0.000000);
    
    
    textNode = createSceneNode();
    textNode->setTexture(&t_charmap);
    textNode->setMesh(&hello_world);
    textNode->position = vec3(-1.0, -1.0, 0.0);
    textNode->isIlluminated = false;
    textNode->isInverted = true;
    hudNode->children.push_back(textNode);
    
    
    getTimeDeltaSeconds();

    std::cout << "Ready. Click to start!" << std::endl;
}

void updateNodeTransformations(SceneNode* node, mat4 transformationThusFar, mat4 V, mat4 P) {
    mat4 transformationMatrix(1.0);

    switch(node->nodeType) {
        case GEOMETRY:
            transformationMatrix =
                    glm::translate(mat4(1.0), node->position)
                    * glm::translate(mat4(1.0), node->referencePoint)
                    * glm::rotate(mat4(1.0), node->rotation.z, vec3(0,0,1))
                    * glm::rotate(mat4(1.0), node->rotation.y, vec3(0,1,0))
                    * glm::rotate(mat4(1.0), node->rotation.x, vec3(1,0,0))
                    * glm::translate(mat4(1.0), -node->referencePoint)
                    * glm::scale(mat4(1.0), node->scale);
            break;
        case POINT_LIGHT:
        case SPOT_LIGHT:
            transformationMatrix =
                    glm::translate(mat4(1.0), node->position);
            break;
    }
    mat4 M = transformationThusFar * transformationMatrix;
    mat4 MV = V*M;

    node->MV = MV;
    node->MVP = P*MV;
    node->MVnormal = glm::inverse(glm::transpose(MV));

    for(SceneNode* child : node->children) {
        updateNodeTransformations(child, M, V, P);
    }

    if (node->targeted_by != nullptr) {
        assert(node->targeted_by->nodeType == SPOT_LIGHT);
        node->targeted_by->rotation = vec3(MV*glm::vec4(node->position, 1.0));
    }
}

void updateFrame(GLFWwindow* window, int windowWidth, int windowHeight) {
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    double timeDelta = getTimeDeltaSeconds();

    if(!hasStarted) {

        if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_1)) {
            if (options.enableMusic) {
                sound = new sf::Sound();
                sound->setBuffer(*buffer);
                sf::Time startTime = sf::seconds(debug_startTime);
                sound->setPlayingOffset(startTime);
                sound->play();
            }
            totalElapsedTime = debug_startTime;
            hasStarted = true;
        }
    } else {

        // I really should calculate this using the std::chrono timestamp for this
        // You definitely end up with a cumulative error when doing lots of small additions like this
        // However, for a game that lasts only a few minutes this is fine.
        totalElapsedTime += timeDelta;

        if(hasLost) {
            //ballRadius += 200 * timeDelta;
            //if(ballRadius > 999999) {
            //    ballRadius = 999999;
            //}
        } else {
            for (uint i = currentKeyFrame; i < keyFrameTimeStamps.size(); i++) {
                if (totalElapsedTime < keyFrameTimeStamps.at(i)) {
                    continue;
                }
                currentKeyFrame = i;
            }

            jumpedToNextFrame = currentKeyFrame != previousKeyFrame;
            previousKeyFrame = currentKeyFrame;

        }
    }

    mat4 projection = glm::perspective(
        glm::radians(45.0f), // fovy
        float(windowWidth) / float(windowHeight), // aspect
        0.1f, 50000.f // near, far
    );

    // hardcoded camera position...
    mat4 cameraTransform
        = glm::lookAt(cameraPosition, cameraLookAt, cameraUpward);

    updateNodeTransformations(rootNode, mat4(1.0), cameraTransform, projection);

    // We orthographic now, bitches!
    // set orthographic VP
    cameraTransform = mat4(1.0);
    projection = glm::ortho(-float(windowWidth) / float(windowHeight), float(windowWidth) / float(windowHeight), -1.0f, 1.0f);
    updateNodeTransformations(hudNode, mat4(1.0), cameraTransform, projection);

    // update positions of nodes (like the car)
}


void renderNode(SceneNode* node, Gloom::Shader* parent_shader = default_shader) {
    struct Light { // lights as stored in the shader
        // coordinates in MV space
        vec3  position;
        vec3  spot_target;
        bool  is_spot;
        float spot_cuttof_angle;
        vec3  attenuation;
        vec3  color_emissive;
        vec3  color_diffuse;
        vec3  color_specular;
        
        void push_to_shader(Gloom::Shader* shader, uint id) {
            #define l(x) shader->location("light[" + std::to_string(id) + "]." + #x)
                glUniform1i (l(is_spot)          ,    is_spot);
                glUniform1f (l(spot_cuttof_angle),    spot_cuttof_angle);
                glUniform3fv(l(position)         , 1, glm::value_ptr(position));
                glUniform3fv(l(spot_target)      , 1, glm::value_ptr(spot_target));
                glUniform3fv(l(attenuation)      , 1, glm::value_ptr(attenuation));
                glUniform3fv(l(color_emissive)   , 1, glm::value_ptr(color_emissive));
                glUniform3fv(l(color_diffuse)    , 1, glm::value_ptr(color_diffuse));
                glUniform3fv(l(color_specular)   , 1, glm::value_ptr(color_specular));
            #undef l
        }
    };
    static Light lights[N_LIGHTS];
    static Gloom::Shader* s = nullptr; // The currently active shader
    
    // activate the correct shader
    Gloom::Shader* node_shader = (node->shader != nullptr)
        ? node->shader
        : parent_shader;
    if (s != node_shader) {
        s = node_shader;
        s->activate();
        uint i = 0; for (Light l : lights) l.push_to_shader(s, i++);
    }

    switch(node->nodeType) {
        case GEOMETRY:
            if(node->vertexArrayObjectID != -1) {
                // load uniforms
                glUniformMatrix4fv(s->location("MVP")     , 1, GL_FALSE, glm::value_ptr(node->MVP));
                glUniformMatrix4fv(s->location("MV")      , 1, GL_FALSE, glm::value_ptr(node->MV));
                glUniformMatrix4fv(s->location("MVnormal"), 1, GL_FALSE, glm::value_ptr(node->MVnormal));
                glUniform1f( s->location("shinyness"),               node->shinyness);
                glUniform1f( s->location("displacementCoefficient"), node->displacementCoefficient);
                glUniform1ui(s->location("isTextured"),              node->isTextured);
                glUniform1ui(s->location("isNormalMapped"),          node->isNormalMapped);
                glUniform1ui(s->location("isDisplacementMapped"),    node->isDisplacementMapped);
                glUniform1ui(s->location("isIlluminated"),           node->isIlluminated);
                glUniform1ui(s->location("isInverted"),              node->isInverted);

                if (node->isTextured)           glBindTextureUnit(0, node->diffuseTextureID);
                if (node->isNormalMapped)       glBindTextureUnit(1, node->normalTextureID);
                if (node->isDisplacementMapped) glBindTextureUnit(2, node->displacementTextureID);
                glBindVertexArray(node->vertexArrayObjectID);
                glDrawElements(GL_TRIANGLES, node->VAOIndexCount, GL_UNSIGNED_INT, nullptr);
            }
            break;
        case SPOT_LIGHT:
        case POINT_LIGHT: {
            uint id = node->lightID;
            lights[id].position          = vec3(node->MV * glm::vec4(node->position, 1.0));
            lights[id].is_spot           = node->nodeType == SPOT_LIGHT;
            lights[id].spot_target       = node->rotation;
            lights[id].spot_cuttof_angle = node->spot_cuttof_angle;
            lights[id].attenuation       = node->attenuation;
            lights[id].color_emissive    = node->color_emissive;
            lights[id].color_diffuse     = node->color_diffuse;
            lights[id].color_specular    = node->color_specular;
            lights[id].push_to_shader(s, id);
            break;
        }
        default:
            break;
    }

    for(SceneNode* child : node->children) {
        renderNode(child, node_shader);
    }
}

void renderFrame(GLFWwindow* window, int windowWidth, int windowHeight) {
    glViewport(0, 0, windowWidth, windowHeight);

    renderNode(rootNode);
    renderNode(hudNode);
}
