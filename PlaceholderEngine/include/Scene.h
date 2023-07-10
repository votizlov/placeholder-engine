#pragma once

#include <donut/engine/Scene.h>

using namespace donut::engine;

public class Scene :: public Scene {

    void LoadMonoBehaviours(const Json::Value& monoBehaviourData)
    {
        if (monoBehaviourData.isObject())
        {
            for (const auto& entry : monoBehaviourData.getMemberNames())
            {
                const std::string& objectName = entry;
                const std::string& objectMetadata = monoBehaviourData[entry].asString();
                metadataMap[objectName] = objectMetadata;
            }
        }
    }
};