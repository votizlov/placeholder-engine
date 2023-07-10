#pragma once

#include <donut/app/imgui_console.h>
#include <donut/app/imgui_renderer.h>

class UIRenderer : public ImGui_Renderer
{
private:
    std::shared_ptr<PlaceholderEngine> m_app;

    ImFont* m_FontOpenSans = nullptr;
    ImFont* m_FontDroidMono = nullptr;

    std::unique_ptr<ImGui_Console> m_console;
    std::shared_ptr<engine::Light> m_SelectedLight;

    UIData& m_ui;
    nvrhi::CommandListHandle m_CommandList;

public:
    UIRenderer(DeviceManager* deviceManager, std::shared_ptr<FeatureDemo> app, UIData& ui);

protected:
    virtual void buildUI(void) override;
};