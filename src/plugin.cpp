// SynTHBloK-Pulse: registers this plugin's modules.
#include "plugin.hpp"

Plugin* pluginInstance;

extern Model* modelSynTHBloKPulse;

void init(Plugin* p) {
    pluginInstance = p;
    p->addModel(modelSynTHBloKPulse);
}
