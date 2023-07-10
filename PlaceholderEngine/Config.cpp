
#include <string>
#include <donut/render/SkyPass.h>
#include <donut/engine/Scene.h>
#include <donut/render/ToneMappingPasses.h>
#include <donut/render/TemporalAntiAliasingPass.h>
#include <donut/render/SsaoPass.h>

using namespace donut::render;
using namespace donut::engine;

enum class AntiAliasingMode
{
    NONE,
    TEMPORAL,
    MSAA_2X,
    MSAA_4X,
    MSAA_8X
};

struct UIData
{
    bool                                ShowUI = true;
    bool                                ShowConsole = false;
    bool                                UseDeferredShading = true;
    bool                                Stereo = false;
    bool                                EnableSsao = true;
    SsaoParameters                      SsaoParams;
    ToneMappingParameters               ToneMappingParams;
    TemporalAntiAliasingParameters      TemporalAntiAliasingParams;
    SkyParameters                       SkyParams;
    enum AntiAliasingMode               AntiAliasingMode = AntiAliasingMode::TEMPORAL;
    enum TemporalAntiAliasingJitter     TemporalAntiAliasingJitter = TemporalAntiAliasingJitter::MSAA;
    bool                                EnableVsync = true;
    bool                                ShaderReoladRequested = false;
    bool                                EnableProceduralSky = true;
    bool                                EnableBloom = true;
    float                               BloomSigma = 32.f;
    float                               BloomAlpha = 0.05f;
    bool                                EnableTranslucency = true;
    bool                                EnableMaterialEvents = false;
    bool                                EnableShadows = true;
    float                               AmbientIntensity = 1.0f;
    bool                                EnableLightProbe = true;
    float                               LightProbeDiffuseScale = 1.f;
    float                               LightProbeSpecularScale = 1.f;
    float                               CsmExponent = 4.f;
    bool                                DisplayShadowMap = false;
    bool                                UseThirdPersonCamera = false;
    bool                                EnableAnimations = false;
    bool                                TestMipMapGen = false;
    std::shared_ptr<Material>           SelectedMaterial;
    std::shared_ptr<SceneGraphNode>     SelectedNode;
    std::string                         ScreenshotFileName;
    std::shared_ptr<SceneCamera>        ActiveSceneCamera;
};