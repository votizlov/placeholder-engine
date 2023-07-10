#include "include/UIRenderer.h"

class UIRenderer : public ImGui_Renderer
{
private:
    std::shared_ptr<FeatureDemo> m_app;

    ImFont* m_FontOpenSans = nullptr;
    ImFont* m_FontDroidMono = nullptr;

    std::unique_ptr<ImGui_Console> m_console;
    std::shared_ptr<engine::Light> m_SelectedLight;

    UIData& m_ui;
    nvrhi::CommandListHandle m_CommandList;

public:
    UIRenderer(DeviceManager* deviceManager, std::shared_ptr<FeatureDemo> app, UIData& ui)
        : ImGui_Renderer(deviceManager)
        , m_app(app)
        , m_ui(ui)
    {
        m_CommandList = GetDevice()->createCommandList();

        m_FontOpenSans = this->LoadFont(*(app->GetRootFs()), "/media/fonts/OpenSans/OpenSans-Regular.ttf", 17.f);
        m_FontDroidMono = this->LoadFont(*(app->GetRootFs()), "/media/fonts/DroidSans/DroidSans-Mono.ttf", 14.f);

        ImGui_Console::Options opts;
        opts.font = m_FontDroidMono;
        auto interpreter = std::make_shared<console::Interpreter>();
        // m_console = std::make_unique<ImGui_Console>(interpreter,opts);

        ImGui::GetIO().IniFilename = nullptr;
    }

protected:
    virtual void buildUI(void) override
    {
        if (!m_ui.ShowUI)
            return;

        const auto& io = ImGui::GetIO();

        int width, height;
        GetDeviceManager()->GetWindowDimensions(width, height);

        if (m_app->IsSceneLoading())
        {
            BeginFullScreenWindow();

            char messageBuffer[256];
            const auto& stats = Scene::GetLoadingStats();
            snprintf(messageBuffer, std::size(messageBuffer), "Loading scene %s, please wait...\nObjects: %d/%d, Textures: %d/%d",
                m_app->GetCurrentSceneName().c_str(), stats.ObjectsLoaded.load(), stats.ObjectsTotal.load(), m_app->GetTextureCache()->GetNumberOfLoadedTextures(), m_app->GetTextureCache()->GetNumberOfRequestedTextures());

            DrawScreenCenteredText(messageBuffer);

            EndFullScreenWindow();

            return;
        }

        if (m_ui.ShowConsole && m_console)
        {
            m_console->Render(&m_ui.ShowConsole);
        }

        ImGui::SetNextWindowPos(ImVec2(10.f, 10.f), 0);
        ImGui::Begin("Settings", 0, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("Renderer: %s", GetDeviceManager()->GetRendererString());
        double frameTime = GetDeviceManager()->GetAverageFrameTimeSeconds();
        if (frameTime > 0.0)
            ImGui::Text("%.3f ms/frame (%.1f FPS)", frameTime * 1e3, 1.0 / frameTime);

        const std::string currentScene = m_app->GetCurrentSceneName();
        if (ImGui::BeginCombo("Scene", currentScene.c_str()))
        {
            const std::vector<std::string>& scenes = m_app->GetAvailableScenes();
            for (const std::string& scene : scenes)
            {
                bool is_selected = scene == currentScene;
                if (ImGui::Selectable(scene.c_str(), is_selected))
                    m_app->SetCurrentSceneName(scene);
                if (is_selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        if (ImGui::Button("Reload Shaders"))
            m_ui.ShaderReoladRequested = true;

        ImGui::Checkbox("VSync", &m_ui.EnableVsync);
        ImGui::Checkbox("Deferred Shading", &m_ui.UseDeferredShading);
        if (m_ui.AntiAliasingMode >= AntiAliasingMode::MSAA_2X)
            m_ui.UseDeferredShading = false; // Deferred shading doesn't work with MSAA
        ImGui::Checkbox("Stereo", &m_ui.Stereo);
        ImGui::Checkbox("Animations", &m_ui.EnableAnimations);

        if (ImGui::BeginCombo("Camera (T)", m_ui.ActiveSceneCamera ? m_ui.ActiveSceneCamera->GetName().c_str()
            : m_ui.UseThirdPersonCamera ? "Third-Person" : "First-Person"))
        {
            if (ImGui::Selectable("First-Person", !m_ui.ActiveSceneCamera && !m_ui.UseThirdPersonCamera))
            {
                m_ui.ActiveSceneCamera.reset();
                m_ui.UseThirdPersonCamera = false;
            }
            if (ImGui::Selectable("Third-Person", !m_ui.ActiveSceneCamera && m_ui.UseThirdPersonCamera))
            {
                m_ui.ActiveSceneCamera.reset();
                m_ui.UseThirdPersonCamera = true;
                m_app->CopyActiveCameraToFirstPerson();
            }
            for (const auto& camera : m_app->GetScene()->GetSceneGraph()->GetCameras())
            {
                if (ImGui::Selectable(camera->GetName().c_str(), m_ui.ActiveSceneCamera == camera))
                {
                    m_ui.ActiveSceneCamera = camera;
                    m_app->CopyActiveCameraToFirstPerson();
                }
            }
            ImGui::EndCombo();
        }

        ImGui::Combo("AA Mode", (int*)&m_ui.AntiAliasingMode, "None\0TemporalAA\0MSAA 2x\0MSAA 4x\0MSAA 8x\0");
        ImGui::Combo("TAA Camera Jitter", (int*)&m_ui.TemporalAntiAliasingJitter, "MSAA\0Halton\0R2\0White Noise\0");

        ImGui::SliderFloat("Ambient Intensity", &m_ui.AmbientIntensity, 0.f, 1.f);

        ImGui::Checkbox("Enable Light Probe", &m_ui.EnableLightProbe);
        if (m_ui.EnableLightProbe && ImGui::CollapsingHeader("Light Probe"))
        {
            ImGui::DragFloat("Diffuse Scale", &m_ui.LightProbeDiffuseScale, 0.01f, 0.0f, 10.0f);
            ImGui::DragFloat("Specular Scale", &m_ui.LightProbeSpecularScale, 0.01f, 0.0f, 10.0f);
        }

        ImGui::Checkbox("Enable Procedural Sky", &m_ui.EnableProceduralSky);
        if (m_ui.EnableProceduralSky && ImGui::CollapsingHeader("Sky Parameters"))
        {
            ImGui::SliderFloat("Brightness", &m_ui.SkyParams.brightness, 0.f, 1.f);
            ImGui::SliderFloat("Glow Size", &m_ui.SkyParams.glowSize, 0.f, 90.f);
            ImGui::SliderFloat("Glow Sharpness", &m_ui.SkyParams.glowSharpness, 1.f, 10.f);
            ImGui::SliderFloat("Glow Intensity", &m_ui.SkyParams.glowIntensity, 0.f, 1.f);
            ImGui::SliderFloat("Horizon Size", &m_ui.SkyParams.horizonSize, 0.f, 90.f);
        }
        ImGui::Checkbox("Enable SSAO", &m_ui.EnableSsao);
        ImGui::Checkbox("Enable Bloom", &m_ui.EnableBloom);
        ImGui::DragFloat("Bloom Sigma", &m_ui.BloomSigma, 0.01f, 0.1f, 100.f);
        ImGui::DragFloat("Bloom Alpha", &m_ui.BloomAlpha, 0.01f, 0.01f, 1.0f);
        ImGui::Checkbox("Enable Shadows", &m_ui.EnableShadows);
        ImGui::Checkbox("Enable Translucency", &m_ui.EnableTranslucency);

        ImGui::Separator();
        ImGui::Checkbox("Temporal AA Clamping", &m_ui.TemporalAntiAliasingParams.enableHistoryClamping);
        ImGui::Checkbox("Material Events", &m_ui.EnableMaterialEvents);
        ImGui::Separator();

        const auto& lights = m_app->GetScene()->GetSceneGraph()->GetLights();

        if (!lights.empty() && ImGui::CollapsingHeader("Lights"))
        {
            if (ImGui::BeginCombo("Select Light", m_SelectedLight ? m_SelectedLight->GetName().c_str() : "(None)"))
            {
                for (const auto& light : lights)
                {
                    bool selected = m_SelectedLight == light;
                    ImGui::Selectable(light->GetName().c_str(), &selected);
                    if (selected)
                    {
                        m_SelectedLight = light;
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            if (m_SelectedLight)
            {
                app::LightEditor(*m_SelectedLight);
            }
        }

        ImGui::TextUnformatted("Render Light Probe: ");
        uint32_t probeIndex = 1;
        for (auto probe : m_app->GetLightProbes())
        {
            ImGui::SameLine();
            if (ImGui::Button(probe->name.c_str()))
            {
                m_app->RenderLightProbe(*probe);
            }
        }

        if (ImGui::Button("Screenshot"))
        {
            std::string fileName;
            if (FileDialog(false, "BMP files\0*.bmp\0All files\0*.*\0\0", fileName))
            {
                m_ui.ScreenshotFileName = fileName;
            }
        }

        ImGui::Separator();
        ImGui::Checkbox("Test MipMapGen Pass", &m_ui.TestMipMapGen);
        ImGui::Checkbox("Display Shadow Map", &m_ui.DisplayShadowMap);

        ImGui::End();

        auto material = m_ui.SelectedMaterial;
        if (material)
        {
            ImGui::SetNextWindowPos(ImVec2(float(width) - 10.f, 10.f), 0, ImVec2(1.f, 0.f));
            ImGui::Begin("Material Editor");
            ImGui::Text("Material %d: %s", material->materialID, material->name.c_str());

            MaterialDomain previousDomain = material->domain;
            material->dirty = donut::app::MaterialEditor(material.get(), true);

            if (previousDomain != material->domain)
                m_app->GetScene()->GetSceneGraph()->GetRootNode()->InvalidateContent();

            ImGui::End();
        }

        if (m_ui.AntiAliasingMode != AntiAliasingMode::NONE && m_ui.AntiAliasingMode != AntiAliasingMode::TEMPORAL)
            m_ui.UseDeferredShading = false;

        if (!m_ui.UseDeferredShading)
            m_ui.EnableSsao = false;
    }
};