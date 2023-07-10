﻿// PlaceholderEngine.cpp : Defines the entry point for the application.
//

#include <string>
#include <vector>
#include <memory>
#include <chrono>

#include <donut/core/vfs/VFS.h>
#include <donut/core/log.h>
#include <donut/core/string_utils.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/ConsoleInterpreter.h>
#include <donut/engine/ConsoleObjects.h>
#include <donut/engine/FramebufferFactory.h>
#include <donut/engine/Scene.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/TextureCache.h>
#include <donut/render/BloomPass.h>
#include <donut/render/CascadedShadowMap.h>
#include <donut/render/DeferredLightingPass.h>
#include <donut/render/DepthPass.h>
#include <donut/render/DrawStrategy.h>
#include <donut/render/ForwardShadingPass.h>
#include <donut/render/GBuffer.h>
#include <donut/render/GBufferFillPass.h>
#include <donut/render/LightProbeProcessingPass.h>
#include <donut/render/PixelReadbackPass.h>
#include <donut/render/SkyPass.h>
#include <donut/render/SsaoPass.h>
#include <donut/render/TemporalAntiAliasingPass.h>
#include <donut/render/ToneMappingPasses.h>
#include <donut/render/MipMapGenPass.h>
#include <donut/app/ApplicationBase.h>
#include <donut/app/UserInterfaceUtils.h>
#include <donut/app/Camera.h>
#include <donut/app/DeviceManager.h>
#include <donut/app/imgui_console.h>
#include <donut/app/imgui_renderer.h>
#include <nvrhi/utils.h>
#include <nvrhi/common/misc.h>
#include <iostream>
#include "include/RenderTargets.h"
#ifdef DONUT_WITH_TASKFLOW
#include <taskflow/taskflow.hpp>
#endif
#include "Config.cpp"
#include "include/UIRenderer.h"

using namespace donut;
using namespace donut::math;
using namespace donut::app;
using namespace donut::vfs;
using namespace donut::engine;
using namespace donut::render;

static bool g_PrintSceneGraph = false;
static bool g_PrintFormats = false;

static const char* g_WindowTitle = "Donut Example: Basic Triangle";

class PlaceholderEngine : public ApplicationBase
{
private:
    typedef ApplicationBase Super;

    std::shared_ptr<RootFileSystem>     m_RootFs;
    std::vector<std::string>            m_SceneFilesAvailable;
    std::string                         m_CurrentSceneName;
    std::shared_ptr<Scene>				m_Scene;
    std::shared_ptr<ShaderFactory>      m_ShaderFactory;
    std::shared_ptr<DirectionalLight>   m_SunLight;
    std::shared_ptr<CascadedShadowMap>  m_ShadowMap;
    std::shared_ptr<FramebufferFactory> m_ShadowFramebuffer;
    std::shared_ptr<DepthPass>          m_ShadowDepthPass;
    std::shared_ptr<InstancedOpaqueDrawStrategy> m_OpaqueDrawStrategy;
    std::shared_ptr<TransparentDrawStrategy> m_TransparentDrawStrategy;
    std::unique_ptr<RenderTargets>      m_RenderTargets;
    std::shared_ptr<ForwardShadingPass> m_ForwardPass;
    std::unique_ptr<GBufferFillPass>    m_GBufferPass;
    std::unique_ptr<DeferredLightingPass> m_DeferredLightingPass;
    std::unique_ptr<SkyPass>            m_SkyPass;
    std::unique_ptr<TemporalAntiAliasingPass> m_TemporalAntiAliasingPass;
    std::unique_ptr<BloomPass>          m_BloomPass;
    std::unique_ptr<ToneMappingPass>    m_ToneMappingPass;
    std::unique_ptr<SsaoPass>           m_SsaoPass;
    std::shared_ptr<LightProbeProcessingPass> m_LightProbePass;
    std::unique_ptr<MaterialIDPass>     m_MaterialIDPass;
    std::unique_ptr<PixelReadbackPass>  m_PixelReadbackPass;
    std::unique_ptr<MipMapGenPass>      m_MipMapGenPass;

    std::shared_ptr<IView>              m_View;
    std::shared_ptr<IView>              m_ViewPrevious;

    nvrhi::CommandListHandle            m_CommandList;
    bool                                m_PreviousViewsValid = false;
    FirstPersonCamera                   m_FirstPersonCamera;
    ThirdPersonCamera                   m_ThirdPersonCamera;
    BindingCache                        m_BindingCache;

    float                               m_CameraVerticalFov = 60.f;
    float3                              m_AmbientTop = 0.f;
    float3                              m_AmbientBottom = 0.f;
    uint2                               m_PickPosition = 0u;
    bool                                m_Pick = false;

    std::vector<std::shared_ptr<LightProbe>> m_LightProbes;
    nvrhi::TextureHandle                m_LightProbeDiffuseTexture;
    nvrhi::TextureHandle                m_LightProbeSpecularTexture;

    float                               m_WallclockTime = 0.f;

    UIData& m_ui;

public:

    PlaceholderEngine(DeviceManager* deviceManager, UIData& ui, const std::string& sceneName)
        : Super(deviceManager)
        , m_ui(ui)
        , m_BindingCache(deviceManager->GetDevice())
    {
        std::shared_ptr<NativeFileSystem> nativeFS = std::make_shared<NativeFileSystem>();

        std::filesystem::path mediaPath = app::GetDirectoryWithExecutable().parent_path() / "media";
        std::filesystem::path frameworkShaderPath = app::GetDirectoryWithExecutable() / "shaders/framework" / app::GetShaderTypeName(GetDevice()->getGraphicsAPI());

        m_RootFs = std::make_shared<RootFileSystem>();
        m_RootFs->mount("/media", mediaPath);
        m_RootFs->mount("/shaders/donut", frameworkShaderPath);
        m_RootFs->mount("/native", nativeFS);

        std::filesystem::path scenePath = "/media/glTF-Sample-Models/2.0";
        m_SceneFilesAvailable = FindScenes(*m_RootFs, scenePath);

        if (sceneName.empty() && m_SceneFilesAvailable.empty())
        {
            log::fatal("No scene file found in media folder '%s'\n"
                "Please make sure that folder contains valid scene files.", scenePath.generic_string().c_str());
        }

        m_TextureCache = std::make_shared<TextureCache>(GetDevice(), m_RootFs, nullptr);

        m_ShaderFactory = std::make_shared<ShaderFactory>(GetDevice(), m_RootFs, "/shaders");
        m_CommonPasses = std::make_shared<CommonRenderPasses>(GetDevice(), m_ShaderFactory);

        m_OpaqueDrawStrategy = std::make_shared<InstancedOpaqueDrawStrategy>();
        m_TransparentDrawStrategy = std::make_shared<TransparentDrawStrategy>();


        const nvrhi::Format shadowMapFormats[] = {
            nvrhi::Format::D24S8,
            nvrhi::Format::D32,
            nvrhi::Format::D16,
            nvrhi::Format::D32S8 };

        const nvrhi::FormatSupport shadowMapFeatures =
            nvrhi::FormatSupport::Texture |
            nvrhi::FormatSupport::DepthStencil |
            nvrhi::FormatSupport::ShaderLoad;

        nvrhi::Format shadowMapFormat = nvrhi::utils::ChooseFormat(GetDevice(), shadowMapFeatures, shadowMapFormats, std::size(shadowMapFormats));

        m_ShadowMap = std::make_shared<CascadedShadowMap>(GetDevice(), 2048, 4, 0, shadowMapFormat);
        m_ShadowMap->SetupProxyViews();

        m_ShadowFramebuffer = std::make_shared<FramebufferFactory>(GetDevice());
        m_ShadowFramebuffer->DepthTarget = m_ShadowMap->GetTexture();

        DepthPass::CreateParameters shadowDepthParams;
        shadowDepthParams.slopeScaledDepthBias = 4.f;
        shadowDepthParams.depthBias = 100;
        m_ShadowDepthPass = std::make_shared<DepthPass>(GetDevice(), m_CommonPasses);
        m_ShadowDepthPass->Init(*m_ShaderFactory, shadowDepthParams);

        m_CommandList = GetDevice()->createCommandList();

        m_FirstPersonCamera.SetMoveSpeed(3.0f);
        m_ThirdPersonCamera.SetMoveSpeed(3.0f);

        SetAsynchronousLoadingEnabled(true);

        if (sceneName.empty())
            SetCurrentSceneName(app::FindPreferredScene(m_SceneFilesAvailable, "Sponza.gltf"));
        else
            SetCurrentSceneName("/native/" + sceneName);

        CreateLightProbes(4);
    }

    std::shared_ptr<vfs::IFileSystem> GetRootFs() const
    {
        return m_RootFs;
    }

    BaseCamera& GetActiveCamera() const
    {
        return m_ui.UseThirdPersonCamera ? (BaseCamera&)m_ThirdPersonCamera : (BaseCamera&)m_FirstPersonCamera;
    }

    std::vector<std::string> const& GetAvailableScenes() const
    {
        return m_SceneFilesAvailable;
    }

    std::string GetCurrentSceneName() const
    {
        return m_CurrentSceneName;
    }

    void SetCurrentSceneName(const std::string& sceneName)
    {
        if (m_CurrentSceneName == sceneName)
            return;

        m_CurrentSceneName = sceneName;

        BeginLoadingScene(m_RootFs, m_CurrentSceneName);
    }

    void CopyActiveCameraToFirstPerson()
    {
        if (m_ui.ActiveSceneCamera)
        {
            dm::affine3 viewToWorld = m_ui.ActiveSceneCamera->GetViewToWorldMatrix();
            dm::float3 cameraPos = viewToWorld.m_translation;
            m_FirstPersonCamera.LookAt(cameraPos, cameraPos + viewToWorld.m_linear.row2, viewToWorld.m_linear.row1);
        }
        else if (m_ui.UseThirdPersonCamera)
        {
            m_FirstPersonCamera.LookAt(m_ThirdPersonCamera.GetPosition(), m_ThirdPersonCamera.GetPosition() + m_ThirdPersonCamera.GetDir(), m_ThirdPersonCamera.GetUp());
        }
    }

    virtual bool KeyboardUpdate(int key, int scancode, int action, int mods) override
    {
        if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        {
            m_ui.ShowUI = !m_ui.ShowUI;
            return true;
        }

        if (key == GLFW_KEY_GRAVE_ACCENT && action == GLFW_PRESS)
        {
            m_ui.ShowConsole = !m_ui.ShowConsole;
            return true;
        }

        if (key == GLFW_KEY_SPACE && action == GLFW_PRESS)
        {
            m_ui.EnableAnimations = !m_ui.EnableAnimations;
            return true;
        }

        if (key == GLFW_KEY_T && action == GLFW_PRESS)
        {
            CopyActiveCameraToFirstPerson();
            if (m_ui.ActiveSceneCamera)
            {
                m_ui.UseThirdPersonCamera = false;
                m_ui.ActiveSceneCamera = nullptr;
            }
            else
            {
                m_ui.UseThirdPersonCamera = !m_ui.UseThirdPersonCamera;
            }
            return true;
        }

        if (!m_ui.ActiveSceneCamera)
            GetActiveCamera().KeyboardUpdate(key, scancode, action, mods);
        return true;
    }

    virtual bool MousePosUpdate(double xpos, double ypos) override
    {
        if (!m_ui.ActiveSceneCamera)
            GetActiveCamera().MousePosUpdate(xpos, ypos);

        m_PickPosition = uint2(static_cast<uint>(xpos), static_cast<uint>(ypos));

        return true;
    }

    virtual bool MouseButtonUpdate(int button, int action, int mods) override
    {
        if (!m_ui.ActiveSceneCamera)
            GetActiveCamera().MouseButtonUpdate(button, action, mods);

        if (action == GLFW_PRESS && button == GLFW_MOUSE_BUTTON_2)
            m_Pick = true;

        return true;
    }

    virtual bool MouseScrollUpdate(double xoffset, double yoffset) override
    {
        if (!m_ui.ActiveSceneCamera)
            GetActiveCamera().MouseScrollUpdate(xoffset, yoffset);

        return true;
    }

    virtual void Animate(float fElapsedTimeSeconds) override
    {
        if (!m_ui.ActiveSceneCamera)
            GetActiveCamera().Animate(fElapsedTimeSeconds);

        if (m_ToneMappingPass)
            m_ToneMappingPass->AdvanceFrame(fElapsedTimeSeconds);

        if (IsSceneLoaded() && m_ui.EnableAnimations)
        {
            m_WallclockTime += fElapsedTimeSeconds;

            for (const auto& anim : m_Scene->GetSceneGraph()->GetAnimations())
            {
                float duration = anim->GetDuration();
                float integral;
                float animationTime = std::modf(m_WallclockTime / duration, &integral) * duration;
                (void)anim->Apply(animationTime);
            }
        }
    }


    virtual void SceneUnloading() override
    {
        if (m_ForwardPass) m_ForwardPass->ResetBindingCache();
        if (m_DeferredLightingPass) m_DeferredLightingPass->ResetBindingCache();
        if (m_GBufferPass) m_GBufferPass->ResetBindingCache();
        if (m_LightProbePass) m_LightProbePass->ResetCaches();
        if (m_ShadowDepthPass) m_ShadowDepthPass->ResetBindingCache();
        m_BindingCache.Clear();
        m_SunLight.reset();
        m_ui.SelectedMaterial = nullptr;
        m_ui.SelectedNode = nullptr;

        for (auto probe : m_LightProbes)
        {
            probe->enabled = false;
        }
    }

    virtual bool LoadScene(std::shared_ptr<IFileSystem> fs, const std::filesystem::path& fileName) override
    {
        using namespace std::chrono;

        Scene* scene = new Scene(GetDevice(), *m_ShaderFactory, fs, m_TextureCache, nullptr, nullptr);

        auto startTime = high_resolution_clock::now();

        if (scene->Load(fileName))
        {
            m_Scene = std::unique_ptr<Scene>(scene);

            auto endTime = high_resolution_clock::now();
            auto duration = duration_cast<milliseconds>(endTime - startTime).count();
            log::info("Scene loading time: %llu ms", duration);

            return true;
        }

        return false;
    }

    virtual void SceneLoaded() override
    {
        Super::SceneLoaded();

        m_Scene->FinishedLoading(GetFrameIndex());

        m_WallclockTime = 0.f;
        m_PreviousViewsValid = false;

        for (auto light : m_Scene->GetSceneGraph()->GetLights())
        {
            if (light->GetLightType() == LightType_Directional)
            {
                m_SunLight = std::static_pointer_cast<DirectionalLight>(light);
                break;
            }
        }

        if (!m_SunLight)
        {
            m_SunLight = std::make_shared<DirectionalLight>();
            m_SunLight->angularSize = 0.53f;
            m_SunLight->irradiance = 1.f;

            auto node = std::make_shared<SceneGraphNode>();
            node->SetLeaf(m_SunLight);
            m_SunLight->SetDirection(dm::double3(0.1, -0.9, 0.1));
            m_SunLight->SetName("Sun");
            m_Scene->GetSceneGraph()->Attach(m_Scene->GetSceneGraph()->GetRootNode(), node);
        }

        auto cameras = m_Scene->GetSceneGraph()->GetCameras();
        if (!cameras.empty())
        {
            m_ui.ActiveSceneCamera = cameras[0];
        }
        else
        {
            m_ui.ActiveSceneCamera.reset();

            m_FirstPersonCamera.LookAt(
                float3(0.f, 1.8f, 0.f),
                float3(1.f, 1.8f, 0.f));
            m_CameraVerticalFov = 60.f;
        }

        m_ThirdPersonCamera.SetRotation(dm::radians(135.f), dm::radians(20.f));
        PointThirdPersonCameraAt(m_Scene->GetSceneGraph()->GetRootNode());

        m_ui.UseThirdPersonCamera = string_utils::ends_with(m_CurrentSceneName, ".gltf")
            || string_utils::ends_with(m_CurrentSceneName, ".glb");

        CopyActiveCameraToFirstPerson();

        if (g_PrintSceneGraph)
            PrintSceneGraph(m_Scene->GetSceneGraph()->GetRootNode());
    }

    void PointThirdPersonCameraAt(const std::shared_ptr<SceneGraphNode>& node)
    {
        dm::box3 bounds = node->GetGlobalBoundingBox();
        m_ThirdPersonCamera.SetTargetPosition(bounds.center());
        float radius = length(bounds.diagonal()) * 0.5f;
        float distance = radius / sinf(dm::radians(m_CameraVerticalFov * 0.5f));
        m_ThirdPersonCamera.SetDistance(distance);
        m_ThirdPersonCamera.Animate(0.f);
    }

    bool IsStereo()
    {
        return m_ui.Stereo;
    }

    std::shared_ptr<TextureCache> GetTextureCache()
    {
        return m_TextureCache;
    }

    std::shared_ptr<Scene> GetScene()
    {
        return m_Scene;
    }

    bool SetupView()
    {
        float2 renderTargetSize = float2(m_RenderTargets->GetSize());

        if (m_TemporalAntiAliasingPass)
            m_TemporalAntiAliasingPass->SetJitter(m_ui.TemporalAntiAliasingJitter);

        float2 pixelOffset = m_ui.AntiAliasingMode == AntiAliasingMode::TEMPORAL && m_TemporalAntiAliasingPass
            ? m_TemporalAntiAliasingPass->GetCurrentPixelOffset()
            : float2(0.f);

        std::shared_ptr<StereoPlanarView> stereoView = std::dynamic_pointer_cast<StereoPlanarView, IView>(m_View);
        std::shared_ptr<PlanarView> planarView = std::dynamic_pointer_cast<PlanarView, IView>(m_View);

        dm::affine3 viewMatrix;
        float verticalFov = dm::radians(m_CameraVerticalFov);
        float zNear = 0.01f;
        if (m_ui.ActiveSceneCamera)
        {
            auto perspectiveCamera = std::dynamic_pointer_cast<PerspectiveCamera>(m_ui.ActiveSceneCamera);
            if (perspectiveCamera)
            {
                zNear = perspectiveCamera->zNear;
                verticalFov = perspectiveCamera->verticalFov;
            }

            viewMatrix = m_ui.ActiveSceneCamera->GetWorldToViewMatrix();
        }
        else
        {
            viewMatrix = GetActiveCamera().GetWorldToViewMatrix();
        }

        bool topologyChanged = false;

        if (IsStereo())
        {
            if (!stereoView)
            {
                m_View = stereoView = std::make_shared<StereoPlanarView>();
                m_ViewPrevious = std::make_shared<StereoPlanarView>();
                topologyChanged = true;
            }

            stereoView->LeftView.SetViewport(nvrhi::Viewport(renderTargetSize.x * 0.5f, renderTargetSize.y));
            stereoView->LeftView.SetPixelOffset(pixelOffset);

            stereoView->RightView.SetViewport(nvrhi::Viewport(renderTargetSize.x * 0.5f, renderTargetSize.x, 0.f, renderTargetSize.y, 0.f, 1.f));
            stereoView->RightView.SetPixelOffset(pixelOffset);

            {
                float4x4 projection = perspProjD3DStyleReverse(verticalFov, renderTargetSize.x / renderTargetSize.y * 0.5f, zNear);

                affine3 leftView = viewMatrix;
                stereoView->LeftView.SetMatrices(leftView, projection);

                affine3 rightView = leftView;
                rightView.m_translation -= float3(0.2f, 0, 0);
                stereoView->RightView.SetMatrices(rightView, projection);
            }

            stereoView->LeftView.UpdateCache();
            stereoView->RightView.UpdateCache();

            m_ThirdPersonCamera.SetView(stereoView->LeftView);

            if (topologyChanged)
            {
                *std::static_pointer_cast<StereoPlanarView>(m_ViewPrevious) = *std::static_pointer_cast<StereoPlanarView>(m_View);
            }
        }
        else
        {
            if (!planarView)
            {
                m_View = planarView = std::make_shared<PlanarView>();
                m_ViewPrevious = std::make_shared<PlanarView>();
                topologyChanged = true;
            }

            float4x4 projection = perspProjD3DStyleReverse(verticalFov, renderTargetSize.x / renderTargetSize.y, zNear);

            planarView->SetViewport(nvrhi::Viewport(renderTargetSize.x, renderTargetSize.y));
            planarView->SetPixelOffset(pixelOffset);

            planarView->SetMatrices(viewMatrix, projection);
            planarView->UpdateCache();

            m_ThirdPersonCamera.SetView(*planarView);

            if (topologyChanged)
            {
                *std::static_pointer_cast<PlanarView>(m_ViewPrevious) = *std::static_pointer_cast<PlanarView>(m_View);
            }
        }

        return topologyChanged;
    }

    void CreateRenderPasses(bool& exposureResetRequired)
    {
        uint32_t motionVectorStencilMask = 0x01;

        ForwardShadingPass::CreateParameters ForwardParams;
        ForwardParams.trackLiveness = false;
        m_ForwardPass = std::make_unique<ForwardShadingPass>(GetDevice(), m_CommonPasses);
        m_ForwardPass->Init(*m_ShaderFactory, ForwardParams);

        GBufferFillPass::CreateParameters GBufferParams;
        GBufferParams.enableMotionVectors = true;
        GBufferParams.stencilWriteMask = motionVectorStencilMask;
        m_GBufferPass = std::make_unique<GBufferFillPass>(GetDevice(), m_CommonPasses);
        m_GBufferPass->Init(*m_ShaderFactory, GBufferParams);

        GBufferParams.enableMotionVectors = false;
        m_MaterialIDPass = std::make_unique<MaterialIDPass>(GetDevice(), m_CommonPasses);
        m_MaterialIDPass->Init(*m_ShaderFactory, GBufferParams);

        m_PixelReadbackPass = std::make_unique<PixelReadbackPass>(GetDevice(), m_ShaderFactory, m_RenderTargets->MaterialIDs, nvrhi::Format::RGBA32_UINT);
        m_MipMapGenPass = std::make_unique <MipMapGenPass>(GetDevice(), m_ShaderFactory, m_RenderTargets->ResolvedColor, MipMapGenPass::Mode::MODE_COLOR);

        m_DeferredLightingPass = std::make_unique<DeferredLightingPass>(GetDevice(), m_CommonPasses);
        m_DeferredLightingPass->Init(m_ShaderFactory);

        m_SkyPass = std::make_unique<SkyPass>(GetDevice(), m_ShaderFactory, m_CommonPasses, m_RenderTargets->ForwardFramebuffer, *m_View);

        {
            TemporalAntiAliasingPass::CreateParameters taaParams;
            taaParams.sourceDepth = m_RenderTargets->Depth;
            taaParams.motionVectors = m_RenderTargets->MotionVectors;
            taaParams.unresolvedColor = m_RenderTargets->HdrColor;
            taaParams.resolvedColor = m_RenderTargets->ResolvedColor;
            taaParams.feedback1 = m_RenderTargets->TemporalFeedback1;
            taaParams.feedback2 = m_RenderTargets->TemporalFeedback2;
            taaParams.motionVectorStencilMask = motionVectorStencilMask;
            taaParams.useCatmullRomFilter = true;

            m_TemporalAntiAliasingPass = std::make_unique<TemporalAntiAliasingPass>(GetDevice(), m_ShaderFactory, m_CommonPasses, *m_View, taaParams);
        }

        if (m_RenderTargets->GetSampleCount() == 1)
        {
            m_SsaoPass = std::make_unique<SsaoPass>(GetDevice(), m_ShaderFactory, m_CommonPasses, m_RenderTargets->Depth, m_RenderTargets->GBufferNormals, m_RenderTargets->AmbientOcclusion);
        }

        m_LightProbePass = std::make_shared<LightProbeProcessingPass>(GetDevice(), m_ShaderFactory, m_CommonPasses);

        nvrhi::BufferHandle exposureBuffer = nullptr;
        if (m_ToneMappingPass)
            exposureBuffer = m_ToneMappingPass->GetExposureBuffer();
        else
            exposureResetRequired = true;

        ToneMappingPass::CreateParameters toneMappingParams;
        toneMappingParams.exposureBufferOverride = exposureBuffer;
        m_ToneMappingPass = std::make_unique<ToneMappingPass>(GetDevice(), m_ShaderFactory, m_CommonPasses, m_RenderTargets->LdrFramebuffer, *m_View, toneMappingParams);

        m_BloomPass = std::make_unique<BloomPass>(GetDevice(), m_ShaderFactory, m_CommonPasses, m_RenderTargets->ResolvedFramebuffer, *m_View);

        m_PreviousViewsValid = false;
    }

    virtual void RenderSplashScreen(nvrhi::IFramebuffer* framebuffer) override
    {
        nvrhi::ITexture* framebufferTexture = framebuffer->getDesc().colorAttachments[0].texture;
        m_CommandList->open();
        m_CommandList->clearTextureFloat(framebufferTexture, nvrhi::AllSubresources, nvrhi::Color(0.f));
        m_CommandList->close();
        GetDevice()->executeCommandList(m_CommandList);
        GetDeviceManager()->SetVsyncEnabled(true);
    }

    virtual void RenderScene(nvrhi::IFramebuffer* framebuffer) override
    {
        int windowWidth, windowHeight;
        GetDeviceManager()->GetWindowDimensions(windowWidth, windowHeight);
        nvrhi::Viewport windowViewport = nvrhi::Viewport(float(windowWidth), float(windowHeight));
        nvrhi::Viewport renderViewport = windowViewport;

        m_Scene->RefreshSceneGraph(GetFrameIndex());

        bool exposureResetRequired = false;

        {
            uint width = windowWidth;
            uint height = windowHeight;

            uint sampleCount = 1;
            switch (m_ui.AntiAliasingMode)
            {
            case AntiAliasingMode::MSAA_2X: sampleCount = 2; break;
            case AntiAliasingMode::MSAA_4X: sampleCount = 4; break;
            case AntiAliasingMode::MSAA_8X: sampleCount = 8; break;
            default:;
            }

            bool needNewPasses = false;

            if (!m_RenderTargets || m_RenderTargets->IsUpdateRequired(uint2(width, height), sampleCount))
            {
                m_RenderTargets = nullptr;
                m_BindingCache.Clear();
                m_RenderTargets = std::make_unique<RenderTargets>();
                m_RenderTargets->Init(GetDevice(), uint2(width, height), sampleCount, true, true);

                needNewPasses = true;
            }

            if (SetupView())
            {
                needNewPasses = true;
            }

            if (m_ui.ShaderReoladRequested)
            {
                m_ShaderFactory->ClearCache();
                needNewPasses = true;
            }

            if (needNewPasses)
            {
                CreateRenderPasses(exposureResetRequired);
            }

            m_ui.ShaderReoladRequested = false;
        }

        m_CommandList->open();

        m_Scene->RefreshBuffers(m_CommandList, GetFrameIndex());

        nvrhi::ITexture* framebufferTexture = framebuffer->getDesc().colorAttachments[0].texture;
        m_CommandList->clearTextureFloat(framebufferTexture, nvrhi::AllSubresources, nvrhi::Color(0.f));

        m_AmbientTop = m_ui.AmbientIntensity * m_ui.SkyParams.skyColor * m_ui.SkyParams.brightness;
        m_AmbientBottom = m_ui.AmbientIntensity * m_ui.SkyParams.groundColor * m_ui.SkyParams.brightness;
        if (m_ui.EnableShadows)
        {
            m_SunLight->shadowMap = m_ShadowMap;
            box3 sceneBounds = m_Scene->GetSceneGraph()->GetRootNode()->GetGlobalBoundingBox();

            frustum projectionFrustum = m_View->GetProjectionFrustum();
            const float maxShadowDistance = 100.f;

            dm::affine3 viewMatrixInv = m_View->GetChildView(ViewType::PLANAR, 0)->GetInverseViewMatrix();

            float zRange = length(sceneBounds.diagonal()) * 0.5f;
            m_ShadowMap->SetupForPlanarViewStable(*m_SunLight, projectionFrustum, viewMatrixInv, maxShadowDistance, zRange, zRange, m_ui.CsmExponent);

            m_ShadowMap->Clear(m_CommandList);

            DepthPass::Context context;

            RenderCompositeView(m_CommandList,
                &m_ShadowMap->GetView(), nullptr,
                *m_ShadowFramebuffer,
                m_Scene->GetSceneGraph()->GetRootNode(),
                *m_OpaqueDrawStrategy,
                *m_ShadowDepthPass,
                context,
                "ShadowMap",
                m_ui.EnableMaterialEvents);
        }
        else
        {
            m_SunLight->shadowMap = nullptr;
        }

        std::vector<std::shared_ptr<LightProbe>> lightProbes;
        if (m_ui.EnableLightProbe)
        {
            for (auto probe : m_LightProbes)
            {
                if (probe->enabled)
                {
                    probe->diffuseScale = m_ui.LightProbeDiffuseScale;
                    probe->specularScale = m_ui.LightProbeSpecularScale;
                    lightProbes.push_back(probe);
                }
            }
        }

        m_RenderTargets->Clear(m_CommandList);

        if (exposureResetRequired)
            m_ToneMappingPass->ResetExposure(m_CommandList, 0.5f);

        ForwardShadingPass::Context forwardContext;

        if (!m_ui.UseDeferredShading || m_ui.EnableTranslucency)
        {
            m_ForwardPass->PrepareLights(forwardContext, m_CommandList, m_Scene->GetSceneGraph()->GetLights(), m_AmbientTop, m_AmbientBottom, lightProbes);
        }

        if (m_ui.UseDeferredShading)
        {
            GBufferFillPass::Context gbufferContext;

            RenderCompositeView(m_CommandList,
                m_View.get(), m_ViewPrevious.get(),
                *m_RenderTargets->GBufferFramebuffer,
                m_Scene->GetSceneGraph()->GetRootNode(),
                *m_OpaqueDrawStrategy,
                *m_GBufferPass,
                gbufferContext,
                "GBufferFill",
                m_ui.EnableMaterialEvents);

            nvrhi::ITexture* ambientOcclusionTarget = nullptr;
            if (m_ui.EnableSsao && m_SsaoPass)
            {
                m_SsaoPass->Render(m_CommandList, m_ui.SsaoParams, *m_View);
                ambientOcclusionTarget = m_RenderTargets->AmbientOcclusion;
            }

            DeferredLightingPass::Inputs deferredInputs;
            deferredInputs.SetGBuffer(*m_RenderTargets);
            deferredInputs.ambientOcclusion = m_ui.EnableSsao ? m_RenderTargets->AmbientOcclusion : nullptr;
            deferredInputs.ambientColorTop = m_AmbientTop;
            deferredInputs.ambientColorBottom = m_AmbientBottom;
            deferredInputs.lights = &m_Scene->GetSceneGraph()->GetLights();
            deferredInputs.lightProbes = m_ui.EnableLightProbe ? &m_LightProbes : nullptr;
            deferredInputs.output = m_RenderTargets->HdrColor;

            m_DeferredLightingPass->Render(m_CommandList, *m_View, deferredInputs);
        }
        else
        {
            RenderCompositeView(m_CommandList,
                m_View.get(), m_ViewPrevious.get(),
                *m_RenderTargets->ForwardFramebuffer,
                m_Scene->GetSceneGraph()->GetRootNode(),
                *m_OpaqueDrawStrategy,
                *m_ForwardPass,
                forwardContext,
                "ForwardOpaque",
                m_ui.EnableMaterialEvents);
        }

        if (m_Pick)
        {
            m_CommandList->clearTextureUInt(m_RenderTargets->MaterialIDs, nvrhi::AllSubresources, 0xffff);

            MaterialIDPass::Context materialIdContext;

            RenderCompositeView(m_CommandList,
                m_View.get(), m_ViewPrevious.get(),
                *m_RenderTargets->MaterialIDFramebuffer,
                m_Scene->GetSceneGraph()->GetRootNode(),
                *m_OpaqueDrawStrategy,
                *m_MaterialIDPass,
                materialIdContext,
                "MaterialID");

            if (m_ui.EnableTranslucency)
            {
                RenderCompositeView(m_CommandList,
                    m_View.get(), m_ViewPrevious.get(),
                    *m_RenderTargets->MaterialIDFramebuffer,
                    m_Scene->GetSceneGraph()->GetRootNode(),
                    *m_TransparentDrawStrategy,
                    *m_MaterialIDPass,
                    materialIdContext,
                    "MaterialID - Translucent");
            }

            m_PixelReadbackPass->Capture(m_CommandList, m_PickPosition);
        }

        if (m_ui.EnableProceduralSky)
            m_SkyPass->Render(m_CommandList, *m_View, *m_SunLight, m_ui.SkyParams);

        if (m_ui.EnableTranslucency)
        {
            RenderCompositeView(m_CommandList,
                m_View.get(), m_ViewPrevious.get(),
                *m_RenderTargets->ForwardFramebuffer,
                m_Scene->GetSceneGraph()->GetRootNode(),
                *m_TransparentDrawStrategy,
                *m_ForwardPass,
                forwardContext,
                "ForwardTransparent",
                m_ui.EnableMaterialEvents);
        }

        nvrhi::ITexture* finalHdrColor = m_RenderTargets->HdrColor;

        if (m_ui.AntiAliasingMode == AntiAliasingMode::TEMPORAL)
        {
            if (m_PreviousViewsValid)
            {
                m_TemporalAntiAliasingPass->RenderMotionVectors(m_CommandList, *m_View, *m_ViewPrevious);
            }

            m_TemporalAntiAliasingPass->TemporalResolve(m_CommandList, m_ui.TemporalAntiAliasingParams, m_PreviousViewsValid, *m_View, *m_View);

            finalHdrColor = m_RenderTargets->ResolvedColor;

            if (m_ui.EnableBloom)
            {
                m_BloomPass->Render(m_CommandList, m_RenderTargets->ResolvedFramebuffer, *m_View, m_RenderTargets->ResolvedColor, m_ui.BloomSigma, m_ui.BloomAlpha);
            }
            m_PreviousViewsValid = true;
        }
        else
        {
            std::shared_ptr<FramebufferFactory> finalHdrFramebuffer = m_RenderTargets->HdrFramebuffer;

            if (m_RenderTargets->GetSampleCount() > 1)
            {
                auto subresources = nvrhi::TextureSubresourceSet(0, 1, 0, 1);
                m_CommandList->resolveTexture(m_RenderTargets->ResolvedColor, subresources, m_RenderTargets->HdrColor, subresources);
                finalHdrColor = m_RenderTargets->ResolvedColor;
                finalHdrFramebuffer = m_RenderTargets->ResolvedFramebuffer;
            }

            if (m_ui.EnableBloom)
            {
                m_BloomPass->Render(m_CommandList, finalHdrFramebuffer, *m_View, finalHdrColor, m_ui.BloomSigma, m_ui.BloomAlpha);
            }

            m_PreviousViewsValid = false;
        }

        auto toneMappingParams = m_ui.ToneMappingParams;
        if (exposureResetRequired)
        {
            toneMappingParams.eyeAdaptationSpeedUp = 0.f;
            toneMappingParams.eyeAdaptationSpeedDown = 0.f;
        }
        m_ToneMappingPass->SimpleRender(m_CommandList, toneMappingParams, *m_View, finalHdrColor);

        m_CommonPasses->BlitTexture(m_CommandList, framebuffer, m_RenderTargets->LdrColor, &m_BindingCache);

        if (m_ui.TestMipMapGen)
        {
            m_MipMapGenPass->Dispatch(m_CommandList);
            m_MipMapGenPass->Display(m_CommonPasses, m_CommandList, framebuffer);
        }

        if (m_ui.DisplayShadowMap)
        {
            for (int cascade = 0; cascade < 4; cascade++)
            {
                nvrhi::Viewport viewport = nvrhi::Viewport(
                    10.f + 266.f * cascade,
                    266.f * (1 + cascade),
                    windowViewport.maxY - 266.f,
                    windowViewport.maxY - 10.f, 0.f, 1.f
                );

                engine::BlitParameters blitParams;
                blitParams.targetFramebuffer = framebuffer;
                blitParams.targetViewport = viewport;
                blitParams.sourceTexture = m_ShadowMap->GetTexture();
                blitParams.sourceArraySlice = cascade;
                m_CommonPasses->BlitTexture(m_CommandList, blitParams, &m_BindingCache);
            }
        }

        m_CommandList->close();
        GetDevice()->executeCommandList(m_CommandList);

        if (!m_ui.ScreenshotFileName.empty())
        {
            SaveTextureToFile(GetDevice(), m_CommonPasses.get(), framebufferTexture, nvrhi::ResourceStates::RenderTarget, m_ui.ScreenshotFileName.c_str());
            m_ui.ScreenshotFileName = "";
        }

        if (m_Pick)
        {
            m_Pick = false;
            uint4 pixelValue = m_PixelReadbackPass->ReadUInts();
            m_ui.SelectedMaterial = nullptr;
            m_ui.SelectedNode = nullptr;

            for (const auto& material : m_Scene->GetSceneGraph()->GetMaterials())
            {
                if (material->materialID == int(pixelValue.x))
                {
                    m_ui.SelectedMaterial = material;
                    break;
                }
            }

            for (const auto& instance : m_Scene->GetSceneGraph()->GetMeshInstances())
            {
                if (instance->GetInstanceIndex() == int(pixelValue.y))
                {
                    m_ui.SelectedNode = instance->GetNodeSharedPtr();
                    break;
                }
            }

            if (m_ui.SelectedNode)
            {
                log::info("Picked node: %s", m_ui.SelectedNode->GetPath().generic_string().c_str());
                PointThirdPersonCameraAt(m_ui.SelectedNode);
            }
            else
            {
                PointThirdPersonCameraAt(m_Scene->GetSceneGraph()->GetRootNode());
            }
        }

        m_TemporalAntiAliasingPass->AdvanceFrame();
        std::swap(m_View, m_ViewPrevious);

        GetDeviceManager()->SetVsyncEnabled(m_ui.EnableVsync);
    }

    std::shared_ptr<ShaderFactory> GetShaderFactory()
    {
        return m_ShaderFactory;
    }

    std::vector<std::shared_ptr<LightProbe>>& GetLightProbes()
    {
        return m_LightProbes;
    }

    void CreateLightProbes(uint32_t numProbes)
    {
        nvrhi::DeviceHandle device = GetDeviceManager()->GetDevice();

        uint32_t diffuseMapSize = 256;
        uint32_t diffuseMapMipLevels = 1;
        uint32_t specularMapSize = 512;
        uint32_t specularMapMipLevels = 8;

        nvrhi::TextureDesc cubemapDesc;

        cubemapDesc.arraySize = 6 * numProbes;
        cubemapDesc.dimension = nvrhi::TextureDimension::TextureCubeArray;
        cubemapDesc.isRenderTarget = true;
        cubemapDesc.keepInitialState = true;

        cubemapDesc.width = diffuseMapSize;
        cubemapDesc.height = diffuseMapSize;
        cubemapDesc.mipLevels = diffuseMapMipLevels;
        cubemapDesc.format = nvrhi::Format::RGBA16_FLOAT;
        cubemapDesc.initialState = nvrhi::ResourceStates::ShaderResource;
        cubemapDesc.keepInitialState = true;

        m_LightProbeDiffuseTexture = device->createTexture(cubemapDesc);

        cubemapDesc.width = specularMapSize;
        cubemapDesc.height = specularMapSize;
        cubemapDesc.mipLevels = specularMapMipLevels;
        cubemapDesc.format = nvrhi::Format::RGBA16_FLOAT;
        cubemapDesc.initialState = nvrhi::ResourceStates::ShaderResource;
        cubemapDesc.keepInitialState = true;

        m_LightProbeSpecularTexture = device->createTexture(cubemapDesc);

        m_LightProbes.clear();

        for (uint32_t i = 0; i < numProbes; i++)
        {
            std::shared_ptr<LightProbe> probe = std::make_shared<LightProbe>();

            probe->name = std::to_string(i + 1);
            probe->diffuseMap = m_LightProbeDiffuseTexture;
            probe->specularMap = m_LightProbeSpecularTexture;
            probe->diffuseArrayIndex = i;
            probe->specularArrayIndex = i;
            probe->bounds = frustum::empty();
            probe->enabled = false;

            m_LightProbes.push_back(probe);
        }
    }

    void RenderLightProbe(LightProbe& probe)
    {
        nvrhi::DeviceHandle device = GetDeviceManager()->GetDevice();

        uint32_t environmentMapSize = 1024;
        uint32_t environmentMapMipLevels = 8;

        nvrhi::TextureDesc cubemapDesc;
        cubemapDesc.arraySize = 6;
        cubemapDesc.width = environmentMapSize;
        cubemapDesc.height = environmentMapSize;
        cubemapDesc.mipLevels = environmentMapMipLevels;
        cubemapDesc.dimension = nvrhi::TextureDimension::TextureCube;
        cubemapDesc.isRenderTarget = true;
        cubemapDesc.format = nvrhi::Format::RGBA16_FLOAT;
        cubemapDesc.initialState = nvrhi::ResourceStates::RenderTarget;
        cubemapDesc.keepInitialState = true;
        cubemapDesc.clearValue = nvrhi::Color(0.f);
        cubemapDesc.useClearValue = true;

        nvrhi::TextureHandle colorTexture = device->createTexture(cubemapDesc);

        const nvrhi::Format depthFormats[] = {
            nvrhi::Format::D24S8,
            nvrhi::Format::D32,
            nvrhi::Format::D16,
            nvrhi::Format::D32S8 };

        const nvrhi::FormatSupport depthFeatures =
            nvrhi::FormatSupport::Texture |
            nvrhi::FormatSupport::DepthStencil |
            nvrhi::FormatSupport::ShaderLoad;

        cubemapDesc.mipLevels = 1;
        cubemapDesc.format = nvrhi::utils::ChooseFormat(GetDevice(), depthFeatures, depthFormats, std::size(depthFormats));
        cubemapDesc.isTypeless = true;
        cubemapDesc.initialState = nvrhi::ResourceStates::DepthWrite;

        nvrhi::TextureHandle depthTexture = device->createTexture(cubemapDesc);

        std::shared_ptr<FramebufferFactory> framebuffer = std::make_shared<FramebufferFactory>(device);
        framebuffer->RenderTargets = { colorTexture };
        framebuffer->DepthTarget = depthTexture;

        CubemapView view;
        view.SetArrayViewports(environmentMapSize, 0);
        const float nearPlane = 0.1f;
        const float cullDistance = 100.f;
        float3 probePosition = GetActiveCamera().GetPosition();
        if (m_ui.ActiveSceneCamera)
            probePosition = m_ui.ActiveSceneCamera->GetWorldToViewMatrix().m_translation;

        view.SetTransform(dm::translation(-probePosition), nearPlane, cullDistance);
        view.UpdateCache();

        std::shared_ptr<SkyPass> skyPass = std::make_shared<SkyPass>(device, m_ShaderFactory, m_CommonPasses, framebuffer, view);

        ForwardShadingPass::CreateParameters ForwardParams;
        ForwardParams.singlePassCubemap = GetDevice()->queryFeatureSupport(nvrhi::Feature::FastGeometryShader);
        std::shared_ptr<ForwardShadingPass> forwardPass = std::make_shared<ForwardShadingPass>(device, m_CommonPasses);
        forwardPass->Init(*m_ShaderFactory, ForwardParams);

        nvrhi::CommandListHandle commandList = device->createCommandList();
        commandList->open();
        commandList->clearTextureFloat(colorTexture, nvrhi::AllSubresources, nvrhi::Color(0.f));

        const nvrhi::FormatInfo& depthFormatInfo = nvrhi::getFormatInfo(depthTexture->getDesc().format);
        commandList->clearDepthStencilTexture(depthTexture, nvrhi::AllSubresources, true, 0.f, depthFormatInfo.hasStencil, 0);

        box3 sceneBounds = m_Scene->GetSceneGraph()->GetRootNode()->GetGlobalBoundingBox();
        float zRange = length(sceneBounds.diagonal()) * 0.5f;
        m_ShadowMap->SetupForCubemapView(*m_SunLight, view.GetViewOrigin(), cullDistance, zRange, zRange, m_ui.CsmExponent);
        m_ShadowMap->Clear(commandList);

        DepthPass::Context shadowContext;

        RenderCompositeView(commandList,
            &m_ShadowMap->GetView(), nullptr,
            *m_ShadowFramebuffer,
            m_Scene->GetSceneGraph()->GetRootNode(),
            *m_OpaqueDrawStrategy,
            *m_ShadowDepthPass,
            shadowContext,
            "ShadowMap");

        ForwardShadingPass::Context forwardContext;

        std::vector<std::shared_ptr<LightProbe>> lightProbes;
        forwardPass->PrepareLights(forwardContext, commandList, m_Scene->GetSceneGraph()->GetLights(), m_AmbientTop, m_AmbientBottom, lightProbes);

        RenderCompositeView(commandList,
            &view, nullptr,
            *framebuffer,
            m_Scene->GetSceneGraph()->GetRootNode(),
            *m_OpaqueDrawStrategy,
            *forwardPass,
            forwardContext,
            "ForwardOpaque");

        skyPass->Render(commandList, view, *m_SunLight, m_ui.SkyParams);

        RenderCompositeView(commandList,
            &view, nullptr,
            *framebuffer,
            m_Scene->GetSceneGraph()->GetRootNode(),
            *m_TransparentDrawStrategy,
            *forwardPass,
            forwardContext,
            "ForwardTransparent");

        m_LightProbePass->GenerateCubemapMips(commandList, colorTexture, 0, 0, environmentMapMipLevels - 1);

        m_LightProbePass->RenderDiffuseMap(commandList, colorTexture, nvrhi::AllSubresources, probe.diffuseMap, probe.diffuseArrayIndex * 6, 0);

        uint32_t specularMapMipLevels = probe.specularMap->getDesc().mipLevels;
        for (uint32_t mipLevel = 0; mipLevel < specularMapMipLevels; mipLevel++)
        {
            float roughness = powf(float(mipLevel) / float(specularMapMipLevels - 1), 2.0f);
            m_LightProbePass->RenderSpecularMap(commandList, roughness, colorTexture, nvrhi::AllSubresources, probe.specularMap, probe.specularArrayIndex * 6, mipLevel);
        }

        m_LightProbePass->RenderEnvironmentBrdfTexture(commandList);

        commandList->close();
        device->executeCommandList(commandList);
        device->waitForIdle();
        device->runGarbageCollection();

        probe.environmentBrdf = m_LightProbePass->GetEnvironmentBrdfTexture();
        box3 bounds = box3(probePosition, probePosition).grow(10.f);
        probe.bounds = frustum::fromBox(bounds);
        probe.enabled = true;
    }
};

bool ProcessCommandLine(int argc, const char* const* argv, DeviceCreationParameters& deviceParams, std::string& sceneName)
{
    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "-width"))
        {
            deviceParams.backBufferWidth = std::stoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "-height"))
        {
            deviceParams.backBufferHeight = std::stoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "-fullscreen"))
        {
            deviceParams.startFullscreen = true;
        }
        else if (!strcmp(argv[i], "-debug"))
        {
            deviceParams.enableDebugRuntime = true;
            deviceParams.enableNvrhiValidationLayer = true;
        }
        else if (!strcmp(argv[i], "-no-vsync"))
        {
            deviceParams.vsyncEnabled = false;
        }
        else if (!strcmp(argv[i], "-print-graph"))
        {
            g_PrintSceneGraph = true;
        }
        else if (!strcmp(argv[i], "-print-formats"))
        {
            g_PrintFormats = true;
        }
        else if (argv[i][0] != '-')
        {
            sceneName = argv[i];
        }
    }

    return true;
}


int main(int __argc, const char** __argv)
{
    nvrhi::GraphicsAPI api = nvrhi::GraphicsAPI::VULKAN;//app::GetGraphicsAPIFromCommandLine(__argc, __argv);

    app::DeviceCreationParameters deviceParams;

    // deviceParams.adapter = VrSystem::GetRequiredAdapter();
    deviceParams.backBufferWidth = 1920;
    deviceParams.backBufferHeight = 1080;
    deviceParams.swapChainSampleCount = 1;
    deviceParams.swapChainBufferCount = 3;
    deviceParams.startFullscreen = false;
    deviceParams.vsyncEnabled = true;

    std::string sceneName;
    if (!ProcessCommandLine(__argc, __argv, deviceParams, sceneName))
    {
        log::error("Failed to process the command line.");
        return 1;
    }

    DeviceManager* deviceManager = DeviceManager::Create(api);
    const char* apiString = nvrhi::utils::GraphicsAPIToString(deviceManager->GetGraphicsAPI());

    std::string windowTitle = "Donut Feature Demo (" + std::string(apiString) + ")";

    if (!deviceManager->CreateWindowDeviceAndSwapChain(deviceParams, windowTitle.c_str()))
    {
        log::error("Cannot initialize a %s graphics device with the requested parameters", apiString);
        return 1;
    }

    if (g_PrintFormats)
    {
        for (uint32_t format = 0; format < (uint32_t)nvrhi::Format::COUNT; format++)
        {
            auto support = deviceManager->GetDevice()->queryFormatSupport((nvrhi::Format)format);
            const auto& formatInfo = nvrhi::getFormatInfo((nvrhi::Format)format);

            char features[13];
            features[0] = (support & nvrhi::FormatSupport::Buffer) != 0 ? 'B' : '.';
            features[1] = (support & nvrhi::FormatSupport::IndexBuffer) != 0 ? 'I' : '.';
            features[2] = (support & nvrhi::FormatSupport::VertexBuffer) != 0 ? 'V' : '.';
            features[3] = (support & nvrhi::FormatSupport::Texture) != 0 ? 'T' : '.';
            features[4] = (support & nvrhi::FormatSupport::DepthStencil) != 0 ? 'D' : '.';
            features[5] = (support & nvrhi::FormatSupport::RenderTarget) != 0 ? 'R' : '.';
            features[6] = (support & nvrhi::FormatSupport::Blendable) != 0 ? 'b' : '.';
            features[7] = (support & nvrhi::FormatSupport::ShaderLoad) != 0 ? 'L' : '.';
            features[8] = (support & nvrhi::FormatSupport::ShaderSample) != 0 ? 'S' : '.';
            features[9] = (support & nvrhi::FormatSupport::ShaderUavLoad) != 0 ? 'l' : '.';
            features[10] = (support & nvrhi::FormatSupport::ShaderUavStore) != 0 ? 's' : '.';
            features[11] = (support & nvrhi::FormatSupport::ShaderAtomic) != 0 ? 'A' : '.';
            features[12] = 0;

            log::info("%17s: %s", formatInfo.name, features);
        }
    }

    {
        UIData uiData;

        std::shared_ptr<PlaceholderEngine> demo = std::make_shared<PlaceholderEngine>(deviceManager, uiData, sceneName);
        std::shared_ptr<UIRenderer> gui = std::make_shared<UIRenderer>(deviceManager, demo, uiData);

        gui->Init(demo->GetShaderFactory());

        deviceManager->AddRenderPassToBack(demo.get());
        deviceManager->AddRenderPassToBack(gui.get());

        deviceManager->RunMessageLoop();
    }

    deviceManager->Shutdown();
#ifdef _DEBUG
    deviceManager->ReportLiveObjects();
#endif
    delete deviceManager;


    return 0;
}