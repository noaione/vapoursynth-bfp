#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cctype>
#include <memory>
#include <stdexcept>

#include "VapourSynth.h"
#include "VSHelper.h"

#define MAX_VIDEO_INPUT 32

int findMinIndex (int arr[])
{
    int min, i, arr_size;
    arr_size = sizeof(arr) / sizeof(arr[0]);
    min = arr[0];
    for (i = 0; i < arr_size; i++) {
        if (arr[i] < min)
            min = arr[i];
    };
    return min;
};

int findMaxIndex (int arr[])
{
    int max, i, arr_size;
    arr_size = sizeof(arr) / sizeof(arr[0]);
    max = arr[0];
    for (i = 0; i < arr_size; i++) {
        if (max < arr[i])
            max = arr[i];
    };
    return max;
};

namespace {
    typedef struct {
        VSNodeRef *node[MAX_VIDEO_INPUT];
        VSVideoInfo vi;
        std::string property;
        int numInputs;
        char properties[3];
        bool show_info;

        void (VS_CC *freeNode)(VSNodeRef *);
    } bfpData;
}

static void VS_CC bfpInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    bfpData *d = reinterpret_cast<bfpData *>(*instanceData);
    vsapi->setVideoInfo(&d->vi, 1, node);
};

static void VS_CC bfpFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    bfpData *d = reinterpret_cast<bfpData *>(instanceData);
    d->freeNode = vsapi->freeNode;
    delete d;
}

static double VS_CC getStats(const VSFrameRef *src, std::string props, int plane, VSCore *core, const VSAPI *vsapi) {
    const VSMap *propsdata = vsapi->getFramePropsRO(src);
    double res = vsapi->propGetFloat(propsdata, props.c_str(), 0, nullptr);
    return res;
};


///////////////////
// Better Frames //
///////////////////

static const VSFrameRef *VS_CC betterFrameGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    bfpData *d = reinterpret_cast<bfpData *>(*instanceData);
    int numInputs = d->numInputs;

    if (activationReason == arInitial) {
        for (int i = 0; i < numInputs; i++) {
            vsapi->requestFrameFilter(n, d->node[i], frameCtx);
        }
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src[MAX_VIDEO_INPUT];
        for (int i = 0; i < numInputs; i++) {
            src[i] = vsapi->getFrameFilter(n, d->node[i], frameCtx);
        }

        int nbest;
        int dataset[MAX_VIDEO_INPUT];
        for (int i = 0; i < numInputs; i++) {
            int err;
            char infoText[100];
            double fsize = getStats(src[i], d->property, 0, core, vsapi);

            dataset[i] = fsize;
            if (d->show_info) {
                VSMap *ret, *args;
                args = vsapi->createMap();
                snprintf(infoText, sizeof(infoText), "Video Index Number: %d (%s)", i+1, d->property);
                vsapi->propSetFrame(args, "clip", src[i], paReplace);
                vsapi->propSetData(args, "text", infoText, sizeof(infoText), paReplace);
                ret = vsapi->invoke(
                    vsapi->getPluginById("com.vapoursynth.text", core),
                    "Text",
                    args
                );
                src[i] = vsapi->propGetFrame(ret, "clip", 0, &err);
                vsapi->freeMap(args);
                vsapi->freeMap(ret);
            };
        }

        if (d->property == std::string("max")) {
            nbest = findMaxIndex(dataset);
        } else if (d->property == std::string("min")) {
            nbest = findMinIndex(dataset);
        } else {
            // Default to Maximum Property
            nbest = findMaxIndex(dataset);
        }

        VSFrameRef *best_frame = vsapi->copyFrame(src[nbest], core);
        VSMap *rwprops = vsapi->getFramePropsRW(best_frame);
        vsapi->propSetFloat(rwprops, "bfpBestNum", dataset[nbest], paReplace);
        vsapi->propSetInt(rwprops, "bfpBestIndex", nbest, paReplace);

        for (int i = 0; i < numInputs; i++) {
            vsapi->freeFrame(src[i]);
        }
        vsapi->freeMap(rwprops);
        return best_frame;
    };

    return nullptr;
};

static void VS_CC betterFrameCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<bfpData> d(new bfpData());

    int err, i;
    d->numInputs = vsapi->propNumElements(in, "clips");
    try {
        if (d->numInputs > MAX_VIDEO_INPUT) {
            throw std::runtime_error("maximum of 32 clips are allowed.");
        };
        if (d->numInputs < 2) {
            throw std::runtime_error("please provide 2 or more clips.");
        };

        for (i = 0; i < d->numInputs; i++) {
            d->node[i] = vsapi->propGetNode(in, "clips", i, &err);
        };

        const VSVideoInfo *vid[MAX_VIDEO_INPUT];
        for (i = 0; i < d->numInputs; i++) {
            if (d->node[i])
                vid[i] = vsapi->getVideoInfo(d->node[i]);
        };

        for (i = 0; i < d->numInputs; i++) {
            if (vid[0]->format->numPlanes != vid[i]->format->numPlanes
                || vid[0]->format->subSamplingW != vid[i]->format->subSamplingW
                || vid[0]->format->subSamplingH != vid[i]->format->subSamplingH
                || vid[0]->width != vid[i]->width
                || vid[0]->height != vid[i]->height)
            {
                throw std::runtime_error("all inputs must have the same number of planes, dimensions, and also same subsampling.");
            };
        };

        d->vi = *vid[0];
        const char *props = vsapi->propGetData(in, "props", 0, &err);
        if (err) {
            props = "avg";
        }
        if (props == "max"
            || props == "maximum"
            || props == "highest")
        {
            props = "PlaneStatsMax";
        } else if (props == "min"
                || props == "minimum"
                || props == "lowest")
        {
            props = "PlaneStatsMin";
        } else if (props == "avg"
                    || props == "average")
        {
            props = "PlaneStatsAverage";
        } else {
            throw std::runtime_error(("Unknown props " + std::string(props) + ", must be 'max' or 'min' or 'avg'").c_str());
        }
        d->property = std::string(props);

        for (int i = 0; i < d->numInputs; i++) {
            VSMap *args, *ret;
            args = vsapi->createMap();
            vsapi->propSetNode(args, "clip", d->node[i], paReplace);
            vsapi->propSetInt(args, "plane", 0, paReplace);
            ret = vsapi->invoke(
                vsapi->getPluginById("com.vapoursynth.std", core),
                "PlaneStats",
                args
            );
            vsapi->freeMap(args);
            d->node[i] = vsapi->propGetNode(ret, "clip", 0, &err);
            vsapi->freeMap(ret);
        }

        if (vsapi->propGetInt(in, "show_info", 0, &err)) {
            d->show_info = false;
        } else {
            d->show_info = false;
        }

        vsapi->createFilter(in, out, "Frame", bfpInit, betterFrameGetFrame, bfpFree, fmParallel, 0, d.release(), core);
    } catch (const std::runtime_error &e) {
        for (i = 0; i < d->numInputs; i++) {
            vsapi->freeNode(d->node[i]);
        }
        vsapi->setError(out, ("Frame: " + std::string(e.what())).c_str());
    };
};


/////////////////////////////////////////////
// Init func

void VS_CC bfpInitialize(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
    configFunc("xyz.n4o.bfp", "bfp", "N4O naive better_frame/better_planes auto-chooser", VAPOURSYNTH_API_VERSION, 1, plugin);
    registerFunc("Frame", "clips:clip[];props:data:opt;show_info:int:opt;", betterFrameCreate, 0, plugin);
};
