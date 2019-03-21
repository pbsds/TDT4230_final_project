#include "renderlogic.hpp"
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
#include <algorithm>
#include <utilities/glfont.h>
#include <utilities/shader.hpp>
#include <utilities/timeutils.h>

using glm::vec3;
using glm::vec4;
using glm::mat4;
typedef unsigned int uint;

sf::Sound* sound;
sf::SoundBuffer* buffer;

void mouse_callback(GLFWwindow* window, double x, double y) {
    static bool mouse_mode = false; 
    int winw, winh;
    glfwGetWindowSize(window, &winw, &winh);
    glViewport(0, 0, winw, winh);

    double mx = (x - winw/2) / double(winh) * 2; // winh instead of winw, like the hudNode space
    double my = (winh/2 - y) / double(winh) * 2;
    
    bool reset_mouse = mouse_position_handler(mx, my, winh/2);
    
    if (reset_mouse)
        glfwSetCursorPos(window, winw/2, winh/2);
    if (reset_mouse != mouse_mode) {
        mouse_mode = reset_mouse;
        glfwSetInputMode(window, GLFW_CURSOR, (reset_mouse)
            ? GLFW_CURSOR_DISABLED
            : GLFW_CURSOR_NORMAL);
    }
}

void initRenderer(GLFWwindow* window, CommandLineOptions options) {
    buffer = new sf::SoundBuffer();
    if (!buffer->loadFromFile("../res/Hall of the Mountain King.ogg")) {
        return;
    }

    glfwSetCursorPosCallback(window, mouse_callback);
    
    init_scene(options);
    
    // init
    getTimeDeltaSeconds();
}

// traverses and updates matricies
void updateNodeTransformations(SceneNode* node, mat4 transformationThusFar, mat4 const& V, mat4 const& P) {
    mat4 transformationMatrix
        = glm::translate(mat4(1.0), node->position)
        * glm::translate(mat4(1.0), node->referencePoint)
        * glm::rotate(mat4(1.0), node->rotation.z, vec3(0,0,1))
        * glm::rotate(mat4(1.0), node->rotation.y, vec3(0,1,0))
        * glm::rotate(mat4(1.0), node->rotation.x, vec3(1,0,0))
        * glm::scale(mat4(1.0), node->scale)
        * glm::translate(mat4(1.0), -node->referencePoint);

    mat4 M = transformationThusFar * transformationMatrix;

    node->MV = V*M;
    node->MVP = P*node->MV;
    node->MVnormal = glm::inverse(glm::transpose(node->MV));

    for(SceneNode* child : node->children)
        updateNodeTransformations(child, M, V, P);
}

// step
void updateFrame(GLFWwindow* window, int windowWidth, int windowHeight) {
    double timeDelta = getTimeDeltaSeconds();
    float aspect = float(windowWidth) / float(windowHeight);
    
    // main action:
    step_scene(timeDelta);

    // calculate camera
    mat4 projection = glm::perspective(
        glm::radians(45.0f), // fovy
        aspect, // aspect
        0.1f, 5000.f // near, far
    );

    mat4 cameraTransform
        = glm::lookAt(cameraPosition, cameraLookAt, cameraUpward);
    
    // update scene with camera
    updateNodeTransformations(rootNode, mat4(1.0), cameraTransform, projection);

    // We orthographic now, bitches!
    // set orthographic VP for hud
    cameraTransform = mat4(1.0);
    projection = glm::ortho(-aspect, aspect, -1.0f, 1.0f);
    
    // update hud
    updateNodeTransformations(hudNode, mat4(1.0), cameraTransform, projection);
    
    // update spots
    for (SceneNode* node : lightNode) {
        if (node->nodeType == SPOT_LIGHT && node->spot_target) {
            node->spot_direction = glm::normalize(
                vec3(node->spot_target->MV * vec4(0,0,0,1))
                - vec3(node->MV * vec4(0,0,0,1)));
        }
    }
    
}

// traverses and renders one and one node
struct NodeDistShader{
    SceneNode* node;
    Gloom::Shader* s;
    float dist;
    NodeDistShader(SceneNode* node, Gloom::Shader* s, float dist)
        : node(node), s(s), dist(dist) {}
};
void renderNode(SceneNode* node, Gloom::Shader* parent_shader, vector<NodeDistShader>* transparent_nodes=nullptr, bool do_recursive=true) {
    struct Light { // lights as stored in the shader
        // coordinates in MV space
        vec3  position; // MV
        vec3  attenuation;
        vec3  color;
        
        bool  is_spot;
        vec3  spot_direction; // MV, must be normalized
        float spot_cuttof_cos;
        
        void push_to_shader(Gloom::Shader* shader, uint id) {
            #define L(x) shader->location("light[" + std::to_string(id) + "]." #x)
            #define V(x) glUniform3fv(L(x), 1, glm::value_ptr(x))
                glUniform1i (L(is_spot)          , is_spot);
                glUniform1f (L(spot_cuttof_cos), spot_cuttof_cos);
                V(position);
                V(spot_direction);
                V(attenuation);
                V(color);
            #undef V
            #undef L
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
            if (transparent_nodes!=nullptr && node->has_transparancy()) {
                // defer to sorted pass later on
                transparent_nodes->emplace_back(node, node_shader, (float)glm::length(vec3(node->MVP*vec4(0,0,0,1))));
            }
            else if(node->vertexArrayObjectID != -1) {
                // load uniforms
                glUniformMatrix4fv(s->location("MVP")     , 1, GL_FALSE, glm::value_ptr(node->MVP));
                glUniformMatrix4fv(s->location("MV")      , 1, GL_FALSE, glm::value_ptr(node->MV));
                glUniformMatrix4fv(s->location("MVnormal"), 1, GL_FALSE, glm::value_ptr(node->MVnormal));
                glUniform2fv(s->location("uvOffset")      , 1,           glm::value_ptr(node->uvOffset));
                glUniform3fv(s->location("diffuse_color") , 1,           glm::value_ptr(node->diffuse_color));
                glUniform3fv(s->location("emissive_color"), 1,           glm::value_ptr(node->emissive_color));
                glUniform3fv(s->location("specular_color"), 1,           glm::value_ptr(node->specular_color));
                glUniform1f( s->location("opacity"),                 node->opacity);
                glUniform1f( s->location("shininess"),               node->shininess);
                glUniform1f( s->location("reflexiveness"),           node->reflexiveness);
                glUniform1f( s->location("displacementCoefficient"), node->displacementCoefficient);
                glUniform1ui(s->location("isTextured"),              node->isTextured);
                glUniform1ui(s->location("isVertexColored"),         node->isVertexColored);
                glUniform1ui(s->location("isNormalMapped"),          node->isNormalMapped);
                glUniform1ui(s->location("isDisplacementMapped"),    node->isDisplacementMapped);
                glUniform1ui(s->location("isReflectionMapped"),      node->isReflectionMapped);
                glUniform1ui(s->location("isIlluminated"),           node->isIlluminated);
                glUniform1ui(s->location("isInverted"),              node->isInverted);
                
                if (node->isTextured)           glBindTextureUnit(0, node->diffuseTextureID);
                if (node->isNormalMapped)       glBindTextureUnit(1, node->normalTextureID);
                if (node->isDisplacementMapped) glBindTextureUnit(2, node->displacementTextureID);
                if (node->isReflectionMapped)   glBindTextureUnit(3, node->reflectionTextureID);
                glBindVertexArray(node->vertexArrayObjectID);
                glDrawElements(GL_TRIANGLES, node->VAOIndexCount, GL_UNSIGNED_INT, nullptr);
            }
            break;
        case SPOT_LIGHT:
        case POINT_LIGHT: {
            uint id = node->lightID;
            lights[id].position          = vec3(node->MV * vec4(vec3(0.0), 1.0));
            lights[id].is_spot           = node->nodeType == SPOT_LIGHT;
            lights[id].spot_direction    = node->spot_direction; // MV space
            lights[id].spot_cuttof_cos   = node->spot_cuttof_cos;
            lights[id].attenuation       = node->attenuation;
            lights[id].color             = node->light_color;
            lights[id].push_to_shader(s, id);
            break;
        }
        default:
            break;
    }

    if (do_recursive)
    for(SceneNode* child : node->children) {
        renderNode(child, node_shader, transparent_nodes, true);
    }
}

// draw
void renderFrame(GLFWwindow* window, int windowWidth, int windowHeight) {
    glViewport(0, 0, windowWidth, windowHeight);
    
    static vector<NodeDistShader> transparent_nodes;
    transparent_nodes.clear();
    
    // externs from scene.hpp, they must have shaders set
    renderNode(rootNode, nullptr, &transparent_nodes);
    
    // sort and render transparent node, sorted by distance from camera
    std::sort(
        transparent_nodes.begin(),
        transparent_nodes.end(),
        [](NodeDistShader a, NodeDistShader b) {
            return a.dist > b.dist;
    });
    glDepthMask(GL_FALSE);
    //glDisable(GL_DEPTH_TEST);
    for (NodeDistShader a : transparent_nodes)
        renderNode(a.node, a.s, nullptr, false);
    std::cout << transparent_nodes.size() << std::endl;
    glDepthMask(GL_TRUE); 
    //glEnable(GL_DEPTH_TEST);
    
    
    renderNode(hudNode, nullptr);
}
