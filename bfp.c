/*
    Copyright (c) 2020 Aiman Maharana

    A port of my n4ofunc.py better_frame and better_planes to C.

    This function is written with some code from exprfilters.c by Fredrik Mellbin
    This is basically a limited version of my n4ofunc version does.

    Main parameters:
    bfp.Frame(clips clip[], props str, show_info bool)
    bfp.Planes(clips clip[], props str[], show_info bool)

    In bfp.Frame, you can only use "max" or "min" or "avg" as a string in the props field
    In bfp.Planes, you need to write it using list: ["max"] or ["max", "min"] or ["max", "avg", "min"] (Maximum of 3)
*/

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "VapourSynth.h"
#include "VSHelper.h"

#define MAX_VIDEO_INPUT 32

// Helper
int findMaxIndex(int arr[])
{ 
    int index;
    int max;
    int n = sizeof(arr) / sizeof(arr[0]);
    for (int i = 0; i < n; i++) {
        if (arr[i] > max) {
            index = i;
            max = arr[i];
        }
    }
    return index;
}

int findMinIndex(int arr[])
{ 
    int index;
    int smallest;
    int n = sizeof(arr) / sizeof(arr[0]);
    for (int i = 0; i < n; i++) {
        if (smallest > arr[i]) {
            index = i;
            smallest = arr[i];
        }
    }
    return index;
}

// Essentials stuff

typedef struct {
    VSNodeRef *node[MAX_VIDEO_INPUT];
    VSVideoInfo vi;
    int numInputs;
    char property;
    char properties[3];
    bool show_info;
} bfpData;

static void VS_CC bfpInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    bfpData *d = (bfpData *) * instanceData;
    vsapi->setVideoInfo(&d->vi, 1, node);
};

static void VS_CC bfpFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    bfpData *d = (bfpData *)instanceData;
    for (int i = 0; i < d->numInputs; i++)
        vsapi->freeNode(d->node[i]);
    free(d);
};

static float VS_CC getStats(const VSFrameRef *src, char props, int plane, VSCore *core, const VSAPI *vsapi) {
    float res;
    int err;
    VSMap *propsdata = vsapi->getFramePropsRO(src);
    res = (float)vsapi->propGetFloat(propsdata, props, 0, &err);
    return res;
};

////////////////////////
//    Better frame    //
////////////////////////

static const VSFrameRef *VS_CC betterFrameGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    bfpData *d = (bfpData *) * instanceData;
    int numInputs = d->numInputs;

    if (activationReason == arInitial) {
        for (int i = 0; i < numInputs; i++) {
            vsapi->requestFrameFilter(n, d->node[i], frameCtx);
        }
    } else if (activationReason == arAllFramesReady) {
        VSFrameRef *src[MAX_VIDEO_INPUT];
        for (int i = 0; i < numInputs; i++) {
            src[i] = vsapi->getFrameFilter(n, d->node[i], frameCtx);
        }

        int nbest;
        int dataset[MAX_VIDEO_INPUT];
        for (int i = 0; i < numInputs; i++) {
            int error;
            char infotext[100];
            float fsize = getStats(src[i], d->property, 0, core, vsapi);

            dataset[i] = fsize;
            // if (d->show_info) {
            //     char infotext[20];
            //     VSMap *ret, *args;
            //     args = vsapi->createMap();
            //     snprintf(infotext, sizeof(infotext), "Video Index Number: %d (%s)", i+1, d->property);
            //     vsapi->propSetFrame(args, "clip", src[i], paReplace);
            //     vsapi->propSetData(args, "text", infotext, sizeof(infotext), paReplace);
            //     ret = vsapi->invoke(
            //         vsapi->getPluginById("com.vapoursynth.text", core),
            //         "Text",
            //         args
            //     );
            //     src[i] = vsapi->propGetNode(ret, "clip", 0, &error);
            //     vsapi->freeMap(args);
            //     vsapi->freeMap(ret);
            // }
        }
        if (d->property == "max") {
            nbest = findMaxIndex(dataset);
        } else if (d->property == "min") {
            nbest = findMinIndex(dataset);
        } else {
            // Default to Maximum Property
            nbest = findMaxIndex(dataset);
        }

        VSFrameRef *best_frame = vsapi->copyFrame(src[nbest], core);
        vsapi->propSetFloat(best_frame, "bfpBestNum", dataset[nbest], paAppend);
        vsapi->propSetInt(best_frame, "bfpBestIndex", nbest, paAppend);

        for (int i = 0; i < numInputs; i++) {
            vsapi->freeFrame(src[i]);
        }

        return best_frame;
    }

    return NULL;
}

extern void VS_CC betterFrameCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    bfpData d;
    bfpData *data;
    int err;
    char errmsg[100];

    d.numInputs = vsapi->propNumElements(in, "clips");
    if (d.numInputs > MAX_VIDEO_INPUT) {
        snprintf(errmsg, sizeof(errmsg), "Frame: More than %d input clips are provided.", MAX_VIDEO_INPUT);
        goto error;
    }

    if (d.numInputs < 2) {
        snprintf(errmsg, sizeof(errmsg), "Frame: Please provide 2 or more clips.");
        goto error;
    }

    for (int i = 0; i < d.numInputs; i++) {
        d.node[i] = vsapi->propGetNode(in, "clips", i, &err);
    }

    VSVideoInfo *vid[MAX_VIDEO_INPUT];
    for (int i = 0; i < d.numInputs; i++) {
        if (d.node[i])
            vid[i] = vsapi->getVideoInfo(d.node[i]);
    }

    for (int i = 0; i < d.numInputs; i++) {
        if (vid[0]->format->numPlanes != vid[i]->format->numPlanes
            || vid[0]->format->subSamplingW != vid[i]->format->subSamplingW
            || vid[0]->format->subSamplingH != vid[i]->format->subSamplingH
            || vid[0]->width != vid[i]->width
            || vid[0]->height != vid[i]->height)
        {
            snprintf(errmsg, sizeof(errmsg), "Frame: All inputs must have the same number of planes and the same dimensions, subsampling included");
            goto error;
        }
    }

    d.vi = *vid[0];
    char props = vsapi->propGetData(in, "props", 0, &err);
    if (err) {
        props = "avg";
    }
    props = tolower(props);
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
        snprintf(errmsg, sizeof(errmsg), "Planes: Unknown props %s, must be 'max' or 'min' or 'avg'", props);
        goto error;
    }
    d.property = props;

    for (int i = 0; i < d.numInputs; i++) {
        VSMap *args, *ret;
        args = vsapi->createMap();
        vsapi->propSetNode(args, "clip", d.node[i], paReplace);
        vsapi->propSetInt(args, "plane", 0, paReplace);
        ret = vsapi->invoke(
            vsapi->getPluginById("com.vapoursynth.std", core),
            "PlaneStats",
            args
        );
        vsapi->freeMap(args);
        d.node[i] = vsapi->propGetNode(ret, "clip", 0, &err);
        vsapi->freeMap(ret);
    }

    if (vsapi->propGetInt(in, "show_info", 0, &err)) {
        d.show_info = false;
    } else {
        d.show_info = false;
    }

    data = malloc(sizeof(d));
    *data = d;

    vsapi->createFilter(in, out, "Frame", bfpInit, betterFrameGetFrame, bfpFree, fmParallel, 0, data, core);
error:
    for (int i = 0; i < MAX_VIDEO_INPUT; i++) {
        vsapi->freeNode(d.node[i]);
    }
    vsapi->setError(out, errmsg);
    return;
}

////////////////////////
//    Better planes   //
////////////////////////

static const VSFrameRef *VS_CC betterPlanesGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    bfpData *d = (bfpData *) * instanceData;
    int numInputs = d->numInputs;

    if (activationReason == arInitial) {
        for (int i = 0; i < numInputs; i++)
            vsapi->requestFrameFilter(n, d->node[i], frameCtx);
    } else if (activationReason == arAllFramesReady) {
        VSFrameRef *src[MAX_VIDEO_INPUT];
        for (int i = 0; i < numInputs; i++)
            src[i] = vsapi->getFrameFilter(n, d->node[i], frameCtx);
        
        VSFrameRef *dstSet[3];

        for (int pesawat = 0; pesawat < 3; pesawat++) {
            int nbest;
            int dataset[3];
            // if (d->show_info) {
            //     for (int i = 0; i < numInputs; i++) {
            //         char infotext[20];
            //         VSMap *ret, *args;
            //         args = vsapi->createMap();
            //         snprintf(infotext, sizeof(infotext), "Video Index Number: %d (%s) [Plane %d]", i+1, d->properties[pesawat], pesawat+1);
            //         vsapi->propSetNode(args, "clip", src[i], node, paReplace);
            //         vsapi->propSetData(args, "text", infotext, sizeof(infotext), paReplace);
            //         ret = vsapi->invoke(
            //             vsapi->getPluginById("com.vapoursynth.text", core),
            //             "Text",
            //             args
            //         );
            //         src[i] = vsapi->propGetNode(ret, "clip", 0, 0);
            //         vsapi->freeMap(args);
            //         vsapi->freeMap(ret);
            //     }
            // }
            for (int i = 0; i < numInputs; i++) {
                float fsize = getStats(src[i], d->properties[pesawat], pesawat, core, vsapi);
                dataset[i] = fsize;
            }
            if (d->property == "max") {
                nbest = findMaxIndex(dataset);
            } else if (d->property == "min") {
                nbest = findMinIndex(dataset);
            } else {
                // Default to Maximum Property
                nbest = findMaxIndex(dataset);
            }

            dstSet[pesawat] = vsapi->copyFrame(src[nbest], core);
        }

        for (int i = 0; i < numInputs; i++) {
            vsapi->freeFrame(src[i]);
        }

        const VSFormat *fi = d->vi.format;
        const int YUV[3] = {0, 1, 2};
        int height = vsapi->getFrameHeight(dstSet[0], 0);
        int width = vsapi->getFrameWidth(dstSet[0], 0);
        VSFrameRef *dstFinal = vsapi->newVideoFrame2(fi, width, height, dstSet, YUV, dstSet[0], core);

        for (int i = 0; i < 3; i++) {
            vsapi->freeFrame(dstSet[i]);
        }

        return dstFinal;
    }

    return NULL;
}

static void VS_CC betterPlanesCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    bfpData d;
    bfpData *data;
    int err;
    char errmsg[100];

    d.numInputs = vsapi->propNumElements(in, "clips");
    if (d.numInputs > MAX_VIDEO_INPUT) {
        snprintf(errmsg, sizeof(errmsg), "Planes: More than %d input clips are provided.", MAX_VIDEO_INPUT);
        goto error;
    }

    if (d.numInputs < 2) {
        snprintf(errmsg, sizeof(errmsg), "Planes: Please provide 2 or more clips.");
        goto error;
    }

    for (int i = 0; i < d.numInputs; i++) {
        d.node[i] = vsapi->propGetNode(in, "clips", i, &err);
    }

    VSVideoInfo *vid[MAX_VIDEO_INPUT];
    for (int i = 0; i < d.numInputs; i++) {
        if (d.node[i]) {
            vid[i] = vsapi->getVideoInfo(d.node[i]);
        }
    }

    for (int i = 0; i < d.numInputs; i++) {
        if (vid[i]->format->numPlanes != 3) {
            snprintf(errmsg, sizeof(errmsg), "Planes: One of the inputs have less than 3 planes (Must need Y, U, and V Planes)");
            goto error;
        }
        if (vid[0]->format->numPlanes != vid[i]->format->numPlanes
            || vid[0]->format->subSamplingW != vid[i]->format->subSamplingW
            || vid[0]->format->subSamplingH != vid[i]->format->subSamplingH
            || vid[0]->width != vid[i]->width
            || vid[0]->height != vid[i]->height)
        {
            snprintf(errmsg, sizeof(errmsg), "Planes: All inputs must have the same number of planes and the same dimensions, subsampling included");
            goto error;
        }
    }

    d.vi = *vid[0];
    int last_working_plane;
    char props;
    for (int i = 0; i < 3; i++) {
        props = vsapi->propGetNode(in, "props", i, &err);
        if (err) {
            if (last_working_plane) {
                props = vsapi->propGetNode(in, "props", last_working_plane, &err);
            } else {
                props = "avg";
            }
        } else {
            last_working_plane = i;
        }
        props = tolower(props);
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
            snprintf(errmsg, sizeof(errmsg), "Planes: Unknown props %s, must be 'max' or 'min' or 'avg'", props);
            goto error;
        }
        d.properties[i] = props;
    }

    for (int i = 0; i < d.numInputs; i++) {
        VSMap *args, *ret;
        args = vsapi->createMap();
        vsapi->propSetNode(args, "clip", d.node[i], paReplace);
        vsapi->propSetInt(args, "plane", 0, paReplace);
        ret = vsapi->invoke(
            vsapi->getPluginById("com.vapoursynth.std", core),
            "PlaneStats",
            args
        );
        vsapi->freeMap(args);
        d.node[i] = vsapi->propGetNode(ret, "clip", 0, &err);
        vsapi->freeMap(ret);
    }

    if (vsapi->propGetInt(in, "show_info", 0, &err)) {
        d.show_info = false;
    } else {
        d.show_info = false;
    }

    data = malloc(sizeof(d));
    *data = d;

    vsapi->createFilter(in, out, "Planes", bfpInit, betterPlanesGetFrame, bfpFree, fmParallel, 0, data, core);
error:
    for (int i = 0; i < MAX_VIDEO_INPUT; i++) {
        vsapi->freeNode(d.node[i]);
    }
    vsapi->setError(out, errmsg);
    return;
}

VS_EXTERNAL_API(void) VapourSynthPluginInit(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin);

VS_EXTERNAL_API(void) VapourSynthPluginInit(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin)
{
    configFunc("xyz.n4o.bfp", "bfp", "N4O naive better_frame/better_planes auto-chooser", VAPOURSYNTH_API_VERSION, 1, plugin);
    registerFunc("Frame", "clips:clip[];prop:data:opt;show_info:int:opt;", betterFrameCreate, NULL, plugin);
    registerFunc("Planes", "clips:clip[];props:data[]:opt;show_info:int:opt;", betterPlanesCreate, NULL, plugin);
}