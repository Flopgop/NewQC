//
// Created by maks on 03.12.2024.
//
#include "xr_include.h"
#include "xr_init.h"
#include "gles_init.h"

#include <string.h>
#include <stdlib.h>

#define LOG_TAG __FILE_NAME__
#include "log.h"

xr_state_t xrinfo;


static void loaderInitialize(android_jni_data_t* jniData) {
    PFN_xrInitializeLoaderKHR initializeLoader;
    if(XR_SUCCEEDED(xrGetInstanceProcAddr(
            XR_NULL_HANDLE,
            "xrInitializeLoaderKHR",
            (PFN_xrVoidFunction*)(&initializeLoader)))
            ) {
        XrLoaderInitInfoAndroidKHR loaderInitInfoAndroid = {XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
        loaderInitInfoAndroid.applicationContext = jniData->applicationActivity;
        loaderInitInfoAndroid.applicationVM = jniData->applicationVm;
        initializeLoader((const XrLoaderInitInfoBaseHeaderKHR*)&loaderInitInfoAndroid);
    }
}

static bool createXrInstance(android_jni_data_t* jniData) {

    static const char* instanceExtensions[] = {
            XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME,
            XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME
    };

    XrInstanceCreateInfoAndroidKHR androidCreateInfo = {XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
    androidCreateInfo.applicationActivity = jniData->applicationActivity;
    androidCreateInfo.applicationVM = jniData->applicationVm;

    XrInstanceCreateInfo createInfo = {XR_TYPE_INSTANCE_CREATE_INFO};
    createInfo.next = &androidCreateInfo;
    createInfo.enabledExtensionNames = instanceExtensions;
    createInfo.enabledExtensionCount = sizeof(instanceExtensions) / sizeof(instanceExtensions[0]);

    strcpy(createInfo.applicationInfo.applicationName, "HelloXR");
    createInfo.applicationInfo.apiVersion = XR_API_VERSION_1_0;

    XrResult result;

    result = xrCreateInstance(&createInfo, &xrinfo.instance);

    if(result != XR_SUCCESS) {
        LOGE("XrInstance creation failed: %i", result);
        return false;
    }

    XrSystemGetInfo systemInfo = {XR_TYPE_SYSTEM_GET_INFO};
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    result = xrGetSystem(xrinfo.instance, &systemInfo, &xrinfo.systemId);
    if(result != XR_SUCCESS) {
        LOGE("Failed to get XrSystemId: %i", result);
        xrDestroyInstance(xrinfo.instance);
        return false;
    }

    return true;
}

static bool getXrGraphicsRequirements(XrGraphicsRequirementsOpenGLESKHR* graphicsRequirements) {
    XrResult result;
    PFN_xrGetOpenGLESGraphicsRequirementsKHR xrGetOpenGlesGraphicsRequirements;
    result = xrGetInstanceProcAddr(xrinfo.instance, "xrGetOpenGLESGraphicsRequirementsKHR", (PFN_xrVoidFunction*)&xrGetOpenGlesGraphicsRequirements);
    if(result != XR_SUCCESS) return false;
    result = xrGetOpenGlesGraphicsRequirements(xrinfo.instance, xrinfo.systemId, graphicsRequirements);
    if(result != XR_SUCCESS) return false;
    return true;
}

static bool initializeGLESSession() {
    XrGraphicsRequirementsOpenGLESKHR graphicsRequirements = {XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR};

    if(!getXrGraphicsRequirements(&graphicsRequirements)) {
        LOGE("%s", "Failed to detect OpenXR runtime version requirements");
        return false;
    }

    GLint major, minor;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    const XrVersion currentApiVersion = XR_MAKE_VERSION(major, minor, 0);

    if(graphicsRequirements.minApiVersionSupported > currentApiVersion) {
        LOGE("OpenGL ES version %i.%i not supported by OpenXR runtime", major, minor);
        goto fail;
    }

    XrGraphicsBindingOpenGLESAndroidKHR graphicsBindingOpenGLES = {XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};
    graphicsBindingOpenGLES.display = egl_info.display;
    graphicsBindingOpenGLES.context = egl_info.context;
    graphicsBindingOpenGLES.config = egl_info.config;

    XrSessionCreateInfo sessionCreateInfo = {XR_TYPE_SESSION_CREATE_INFO};
    sessionCreateInfo.next = &graphicsBindingOpenGLES;
    sessionCreateInfo.systemId = xrinfo.systemId;

    if(xrCreateSession(xrinfo.instance, &sessionCreateInfo, &xrinfo.session) != XR_SUCCESS) {
        LOGE("%s","Failed to create XR session");
    }
    return true;
    fail:
    destroyOpenGLES();
    return false;
}

static bool createReferenceSpace() {
    XrResult result;
    XrReferenceSpaceCreateInfo referenceSpaceCreateInfo = {XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
    referenceSpaceCreateInfo.poseInReferenceSpace.orientation.w = 1;
    result = xrCreateReferenceSpace(xrinfo.session, &referenceSpaceCreateInfo, &xrinfo.localReferenceSpace);
    if(result != XR_SUCCESS) {
        LOGE("Failed to create local reference space: %i", result);
        return false;
    }
    return true;
}
static bool createViewSurface() {
    XrResult result;
    xrinfo.configurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    uint32_t viewCount;
    result = xrEnumerateViewConfigurationViews(xrinfo.instance, xrinfo.systemId, xrinfo.configurationType, 0, &viewCount, NULL);
    if(result != XR_SUCCESS) {
        LOGE("Failed to enumerate configuration views: %i", result);
        return false;
    }
    XrViewConfigurationView configurationViews[viewCount];
    for(uint32_t i = 0; i < viewCount; i++) {
        XrViewConfigurationView defaultConfigView = {XR_TYPE_VIEW_CONFIGURATION_VIEW};
        memcpy(&configurationViews[i], &defaultConfigView, sizeof(XrViewConfigurationView));
    }
    result = xrEnumerateViewConfigurationViews(xrinfo.instance, xrinfo.systemId, xrinfo.configurationType, viewCount, &viewCount, configurationViews);
    if(result != XR_SUCCESS) {
        LOGE("Failed to enumerate configuration views: %i", result);
        return false;
    }


    const XrViewConfigurationView *viewConfig = &configurationViews[0];
    uint32_t width = viewConfig->recommendedImageRectWidth;
    uint32_t height = viewConfig->recommendedImageRectHeight;
    XrSwapchain swapchain;
    XrSwapchainCreateInfo swapchainCreateInfo = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
    swapchainCreateInfo.usageFlags =
            XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    swapchainCreateInfo.format = GL_SRGB8_ALPHA8;
    swapchainCreateInfo.width = width;
    swapchainCreateInfo.height = height;
    swapchainCreateInfo.faceCount = 1;
    swapchainCreateInfo.arraySize = 2;
    swapchainCreateInfo.mipCount = 1;
    swapchainCreateInfo.sampleCount = 1;

    result = xrCreateSwapchain(xrinfo.session, &swapchainCreateInfo, &swapchain);

    if (result != XR_SUCCESS) {
        LOGE("Failed to create swapchain: %i", result);
        return false;
    }

    uint32_t imageCount = 0;
    result = xrEnumerateSwapchainImages(swapchain, 0, &imageCount, NULL);
    // Even if xrEnumerateSwapchainImages fails, this will still initialize with a size of 0
    // It wouldn't matter though since if the call fails we go straight to resource cleanup
    XrSwapchainImageOpenGLESKHR glesImages[imageCount];

    if (result != XR_SUCCESS) {
        LOGE("Failed to enumerate images on swapchain: %i", result);
        goto free_swapchain;
    }

    for (uint32_t j = 0; j < imageCount; j++) {
        XrSwapchainImageOpenGLESKHR defaultImage = {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR};
        memcpy(&glesImages[j], &defaultImage, sizeof(XrSwapchainImageOpenGLESKHR));
    }

    result = xrEnumerateSwapchainImages(swapchain, imageCount, &imageCount,
                                        (XrSwapchainImageBaseHeader *) &glesImages);

    if (result != XR_SUCCESS) {
        LOGE("Failed to enumerate images on swapchain: %i", result);
        goto free_swapchain;
    }

    xrinfo.renderTarget.swapchainTextures = calloc(imageCount, sizeof(GLuint));

    xrinfo.renderTarget.swapchain = swapchain;
    xrinfo.renderTarget.width = width;
    xrinfo.renderTarget.height = height;
    xrinfo.renderTarget.swapchainTextureCount = imageCount;
    for (uint32_t j = 0; j < imageCount; j++) {
        xrinfo.renderTarget.swapchainTextures[j] = glesImages[j].image;
    }
    xrinfo.nViews = viewCount;

    return true;
    free_swapchain:
    xrDestroySwapchain(xrinfo.renderTarget.swapchain);
    return false;
}

bool xriStartSession() {
    XrSessionBeginInfo beginInfo = {XR_TYPE_SESSION_BEGIN_INFO};
    beginInfo.primaryViewConfigurationType = xrinfo.configurationType;
    XrResult result = xrBeginSession(xrinfo.session, &beginInfo);
    if(result != XR_SUCCESS) {
        LOGE("Failed to start session: %i", result);
        return false;
    }
    xrinfo.hasSession = true;
    return true;
}

void xriEndSession() {
    xrEndSession(xrinfo.session);
}

bool xriInitSession() {
    if(!initializeGLESSession()) return false;
    if(!createReferenceSpace()) goto fail;
    if(!createViewSurface()) goto fail;
    for(int j = 0; j < xrinfo.renderTarget.swapchainTextureCount; j++) {
        LOGI("Swapchain texture: %i", xrinfo.renderTarget.swapchainTextures[j]);
    }
    xrinfo.hasSession = true;
    return true;
    fail:
    xrDestroySession(xrinfo.session);
    return false;
}

void xriFreeSession() {
    xrDestroySession(xrinfo.session);
    void* swArray = xrinfo.renderTarget.swapchainTextures;
    xrinfo.renderTarget.swapchainTextures = NULL;
    free(swArray);
    xrinfo.hasSession = false;
}

bool xriInitialize(android_jni_data_t* jniData) {
    loaderInitialize(jniData);
    return createXrInstance(jniData);
}

void xriFree() {
    if(xrinfo.hasSession) xriFreeSession();
    xrDestroyInstance(xrinfo.instance);
}